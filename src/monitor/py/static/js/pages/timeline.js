// Timeline：按 worker 分泳道的 task 执行窗口 Gantt。
//
// 负载着色模型（用户裁定：主色+斜纹）：
//   · 主色维度可选（CPU / IO / Wait，按钮组切换）——条基色按该维度负载
//     档位呈现：≥50% 实色、10~50% 半透明、<10% 中性暗灰（该维度不显著）。
//   · 条纹开关（默认关）：开启时，除主色维度外的其它维度任一 ≥30% 的
//     task 叠加斜纹——表示复合负载；具体构成点击条形查看。
//   · Fast（<500ms）绿色整条；Failed 红描边（内部保留负载构成）。
//
// 泳道紧凑（每泳道 34px）；泳道标签含 host，点击跳转 Worker 详情（顶部
// 「返回 Timeline」条，返回时恢复缩放/排序/滚动/着色选项）。
// 滚轮 + 滑块拖选（高亮填充+手柄+选区数据背景）；「复原缩放」回全程；
// 实时显示当前视图时间范围。
import { getJson, escapeHtml, shortName, fmtTimeFull, fmtMs, fmtTime, fmtPct } from '../api.js';
import { makeChart, chartColors } from '../charts.js';
import { cssVar } from '../theme.js';
import { t } from '../i18n.js';
import { navigate, gotoPage } from '../app.js';

const DIM_LABEL = { cpu: 'CPU', io: 'IO', wait: 'Wait', queue: 'Queue' };

// 负载维度色经 CSS 变量实时读取（主题切换后页面重建取新值）；fallback
// 为深色默认（cssVar 读不到时保证可读，见 theme.js）。
function dimColors() {
  return { cpu: cssVar('--ser-blue', '#4aa8ff'), io: cssVar('--ser-yellow', '#e8b339'),
           wait: cssVar('--pending', '#8a7ca8'), queue: cssVar('--ser-cyan', '#6fd3e8') };
}
function fastColor() { return cssVar('--ser-green', '#3fb972'); }
function failStroke() { return cssVar('--err', '#e0564f'); }
function neutralColor() { return cssVar('--gantt-neutral', '#61707f'); }

// grid 几何（泳道点击的坐标判定与 ganttOption 配置一致）。
// top 预留给时间标签条（悬停/框选起止点时间显示区，不覆盖 task 条）。
const GRID = { left: 170, top: 48, bottom: 34, barH: 16 };
const LANE_H = 34;   // 每泳道高度（紧凑：间距较常规减半）
const HEAVY = 0.30;  // 复合负载阈值（其它维度 ≥30% 叠加条纹）
const STRIPE_GAP = 6;

let chart = null;
let lastTasks = [];
let hostMap = {};
let lanes = [];
let extent = { lo: 0, hi: 1 };
let savedState = null;   // { sort, colorDim, stripe, zoom, scrollTop }

// 负载占比：cpu/io/wait 相对执行窗口；queue（排队等待 ready→started）
// 相对 task 生命周期（queue+exec）——提交后大部分时间在等调度即为高。
function ratios(tk) {
  const exec = Math.max(1, (tk.exec_end_ms || tk.exec_start_ms + 1) - tk.exec_start_ms);
  const cpu = (tk.cpu_time_ms || 0) / exec;
  const io = ((tk.read_time_ms || 0) + (tk.write_time_ms || 0)) / exec;
  const queueMs = Math.max(0, (tk.started_ms || 0) - (tk.ready_ms || tk.created_ms || 0));
  const queue = queueMs / (queueMs + exec);
  return { cpu, io, wait: Math.max(0, 1 - cpu - io), queue, queueMs, execMs: exec };
}

// 主色维度档位色：≥50% 实色，10~50% 半透明，<10% 中性灰。
function dimTierColor(dim, ratio) {
  const c = dimColors();
  if (ratio >= 0.5) return c[dim];
  if (ratio >= 0.1) return c[dim] + '80';  // 50% 透明度
  return neutralColor();
}

function isFast(tk) {
  return ((tk.exec_end_ms || tk.exec_start_ms + 1) - tk.exec_start_ms) < 500;
}

// 复合判定：除主色维度外任一维度 ≥ HEAVY。
function isCompound(tk, dim) {
  const r = ratios(tk);
  return Object.entries(r).some(([k, v]) => k !== dim && v >= HEAVY);
}

function currentZoom() {
  if (!chart) return { start: 0, end: 100 };
  const dz = (chart.getOption().dataZoom || [{}])[0];
  return { start: dz.start ?? 0, end: dz.end ?? 100 };
}

export function destroy(ctx) {
  if (chart) {
    savedState = {
      sort: (ctx && ctx.tlSort) || 'id',
      colorDim: (ctx && ctx.tlColorDim) || 'cpu',
      stripe: !!(ctx && ctx.tlStripe),
      zoom: currentZoom(),
      scrollTop: (ctx && ctx.main) ? ctx.main.scrollTop : 0,
    };
    chart.dispose();
    chart = null;
  }
  brushOverlay = null;  // overlay 随 tl-chart DOM 重建而消失，仅清引用
  hoverLineEl = null; hoverTimeEl = null;
  brushTimeL = null; brushTimeR = null; brushActive = false;
  axisTimeL = null; axisTimeR = null;
  laneTipEl = null;
  lastTasks = [];
  hostMap = {};
  lanes = [];
}

export function mount(ctx) {
  const restore = savedState || { sort: 'id', colorDim: 'cpu', stripe: false,
                                  zoom: null, scrollTop: 0 };
  savedState = null;
  ctx.tlSort = restore.sort;
  ctx.tlColorDim = restore.colorDim;
  ctx.tlStripe = restore.stripe;
  ctx._tlRestoreZoom = restore.zoom;
  ctx._tlRestoreScroll = restore.scrollTop;

  ctx.main.innerHTML = `
    <div class="panel">
      <div class="controls">
        <select id="tl-sort" title="${t('tl.sortTitle')}">
          <option value="id">${t('tl.sortId')}</option>
          <option value="host">${t('tl.sortHost')}</option>
        </select>
        <span class="tl-sep"></span>
        <select id="tl-dim" title="${t('tl.dimTitle')}">
          <option value="cpu">${t('tl.dim', 'CPU')}</option>
          <option value="io">${t('tl.dim', 'IO')}</option>
          <option value="wait">${t('tl.dim', 'Wait')}</option>
          <option value="queue">${t('tl.dim', 'Queue')}</option>
        </select>
        <span class="tl-sep"></span>
        <button id="tl-stripe"></button>
        <button id="tl-reset-zoom">${t('tl.resetZoom')}</button>
        <span class="tl-range" id="tl-range"></span>
      </div>
      <div class="tl-legend" id="tl-legend"></div>
      <div id="tl-chart" style="width:100%;height:240px"></div>
      <div class="tl-slider" id="tl-slider" title="${t('tl.sliderTitle')}">
        <div class="tl-slider-sel" id="tl-slider-sel"></div>
      </div>
      <div class="tl-hint" style="margin-top:4px">${t('tl.hint')}</div>
    </div>
    <div class="panel" id="tl-info" style="display:none"></div>`;
  chart = makeChart(document.getElementById('tl-chart'), ganttOption([], []));

  document.getElementById('tl-sort').onchange = (e) => {
    ctx.tlSort = e.target.value; syncControls(ctx); navigate();
  };
  document.getElementById('tl-dim').onchange = (e) => {
    ctx.tlColorDim = e.target.value; syncControls(ctx); navigate();
  };
  document.getElementById('tl-stripe').onclick = () => {
    ctx.tlStripe = !ctx.tlStripe; syncControls(ctx); navigate();
  };
  document.getElementById('tl-reset-zoom').onclick = () => {
    if (chart) chart.dispatchAction({ type: 'dataZoom', start: 0, end: 100 });
  };

  chart.on('click', (params) => {
    if (suppressClick) return;  // 刚完成刷选，不当作点击
    if (params.seriesType !== 'custom') return;
    const tk = lastTasks.find(x => x.task_id === params.value[3]);
    if (tk) showInfo(tk);
  });
  chart.getZr().on('click', (e) => {
    if (suppressClick) return;
    if (!e.target) hideInfo();
    const laneIdx = laneAt(e.offsetX, e.offsetY);
    if (laneIdx >= 0 && lanes[laneIdx] != null) {
      gotoPage('workers', { workerId: lanes[laneIdx] }, 'Timeline');
    }
  });
  // 泳道标签区 hover：pointer 光标 + 跟随小提示（可点击跳转的可见暗示）。
  chart.getZr().on('mousemove', (e) => {
    const chartEl = document.getElementById('tl-chart');
    if (!chartEl) return;
    const idx = laneAt(e.offsetX, e.offsetY);
    if (idx >= 0 && lanes[idx] != null) {
      chartEl.style.cursor = 'pointer';
      showLaneTip(chartEl, e.offsetX, e.offsetY,
                  t('tl.laneTip', lanes[idx]));
    } else {
      chartEl.style.cursor = 'default';
      hideLaneTip();
    }
  });
  chart.getZr().on('mouseout', hideLaneTip);
  chart.on('dataZoom', () => { updateRangeHint(); syncSliderVisual(); updateAxisTimes(); });
  attachBrushSelect(ctx);
  attachSlider(ctx);
  attachHoverGuide(ctx);
  attachAxisTimes(ctx);
  syncControls(ctx);
}

// ---- 悬停竖线时间定位（用户裁定：常显，不悬停延时）----
// plot 区内移动竖线跟随、时间标签实时显示于 grid 上方的专用条（不覆盖
// task 条）；框选拖动时由 attachBrushSelect 更新起止点时间（brushL/R）。
let hoverLineEl = null, hoverTimeEl = null;
let brushTimeL = null, brushTimeR = null;   // 框选起止点时间标签

function attachHoverGuide(ctx) {
  const chartEl = document.getElementById('tl-chart');
  if (!chartEl) return;
  hoverLineEl = document.createElement('div');
  hoverLineEl.className = 'tl-hover-line';
  hoverTimeEl = document.createElement('div');
  hoverTimeEl.className = 'tl-hover-time';
  brushTimeL = document.createElement('div');
  brushTimeL.className = 'tl-hover-time tl-brush-time';
  brushTimeR = document.createElement('div');
  brushTimeR.className = 'tl-hover-time tl-brush-time';
  for (const el of [hoverLineEl, hoverTimeEl, brushTimeL, brushTimeR]) {
    chartEl.appendChild(el);
  }

  const zr = chart.getZr();
  zr.on('mousemove', (e) => {
    const r = plotRect();
    if (e.offsetX < r.left || e.offsetX > r.right ||
        e.offsetY < r.top || e.offsetY > r.bottom) {
      hideHoverGuide();
      return;
    }
    if (brushActive) { hideHoverGuide(); return; }  // 框选中不显示悬停线
    const x = e.offsetX;
    hoverLineEl.style.display = 'block';
    hoverLineEl.style.left = x + 'px';
    hoverLineEl.style.top = r.top + 'px';
    hoverLineEl.style.height = (r.bottom - r.top) + 'px';
    // 时间标签常显：位于 grid 上方专用条（不覆盖第一行 task 条）。
    const tm = chart.convertFromPixel({ xAxisIndex: 0 }, x);
    hoverTimeEl.textContent = fmtTimeFull(tm);
    hoverTimeEl.style.display = 'block';
    hoverTimeEl.style.left =
      Math.min(Math.max(x - 60, r.left), r.right - 120) + 'px';
    hoverTimeEl.style.top = '2px';   // grid 上方的时间条区（GRID.top 预留）
  });
  zr.on('mouseout', hideHoverGuide);
}

function hideHoverGuide() {
  if (hoverLineEl) hoverLineEl.style.display = 'none';
  if (hoverTimeEl) hoverTimeEl.style.display = 'none';
}

// ---- 图内框选：起止点时间实时显示（拖动过程中两端各一个标签）----
let brushActive = false;

function showBrushTimes(x0, x1) {
  if (!brushTimeL || !brushTimeR || !chart) return;
  const r = plotRect();
  const t0 = chart.convertFromPixel({ xAxisIndex: 0 }, Math.min(x0, x1));
  const t1 = chart.convertFromPixel({ xAxisIndex: 0 }, Math.max(x0, x1));
  brushTimeL.textContent = fmtTimeFull(t0);
  brushTimeR.textContent = fmtTimeFull(t1);
  brushTimeL.style.display = 'block';
  brushTimeR.style.display = 'block';
  // 左标签锚定起点左侧、右标签锚定终点右侧（都限制在 plot 区内）。
  const w = 130;
  brushTimeL.style.left = Math.max(Math.min(x0, x1) - w / 2, r.left) + 'px';
  brushTimeR.style.left = Math.min(Math.max(x0, x1) - w / 2, r.right - w) + 'px';
  brushTimeL.style.top = brushTimeR.style.top = '2px';
}

function hideBrushTimes() {
  if (brushTimeL) brushTimeL.style.display = 'none';
  if (brushTimeR) brushTimeR.style.display = 'none';
}

// ---- x 轴起止时间（自绘，左右角）：中间刻度隐藏后，视图范围的左右
// 边界时间显示在图表底部两角（静态无动画）。----
let axisTimeL = null, axisTimeR = null;

function attachAxisTimes(ctx) {
  const chartEl = document.getElementById('tl-chart');
  if (!chartEl) return;
  axisTimeL = document.createElement('div');
  axisTimeL.className = 'tl-axis-time tl-axis-time-l';
  axisTimeR = document.createElement('div');
  axisTimeR.className = 'tl-axis-time tl-axis-time-r';
  chartEl.appendChild(axisTimeL);
  chartEl.appendChild(axisTimeR);
  updateAxisTimes();
}

function updateAxisTimes() {
  if (!axisTimeL || !axisTimeR || !chart) return;
  const z = currentZoom();
  const span = extent.hi - extent.lo || 1;
  axisTimeL.textContent = fmtTime(extent.lo + span * z.start / 100);
  axisTimeR.textContent = fmtTime(extent.lo + span * z.end / 100);
}

// 泳道标签 hover 浮动提示（绝对定位于图表容器内）。
let laneTipEl = null;
function showLaneTip(chartEl, x, y, text) {
  if (!laneTipEl) {
    laneTipEl = document.createElement('div');
    laneTipEl.className = 'tl-lane-tip';
    chartEl.appendChild(laneTipEl);
  }
  laneTipEl.textContent = text;
  laneTipEl.style.display = 'block';
  laneTipEl.style.left = Math.min(x + 10, chartEl.clientWidth - 150) + 'px';
  laneTipEl.style.top = (y - 26) + 'px';
}
function hideLaneTip() {
  if (laneTipEl) laneTipEl.style.display = 'none';
}

// ---- 任意起止时间段刷选（slider 已取消，用户裁定保留选取高亮）----
// plot 区域内按下并拖动：实时显示半透明蓝色选区；松开后缩放到该时间
// 范围，高亮矩形持续标示当前视图范围（滚轮缩放联动更新；全程时隐藏）。
// 拖动距离 >5px 视为刷选，抑制随后的 click（不误触条形/泳道跳转）。
let suppressClick = false;
let brushOverlay = null;   // 高亮矩形元素（pointer-events:none）

function plotRect() {
  const el = document.getElementById('tl-chart');
  return { left: GRID.left, right: el.clientWidth - 40,
           top: GRID.top, bottom: el.clientHeight - GRID.bottom };
}

function attachBrushSelect(ctx) {
  const chartEl = document.getElementById('tl-chart');
  if (!chartEl) return;
  brushOverlay = document.createElement('div');
  brushOverlay.className = 'tl-brush-sel';
  brushOverlay.style.display = 'none';
  chartEl.appendChild(brushOverlay);

  let dragging = false, x0 = 0;

  const zr = chart.getZr();
  zr.on('mousedown', (e) => {
    const r = plotRect();
    if (e.offsetX < r.left || e.offsetX > r.right ||
        e.offsetY < r.top || e.offsetY > r.bottom) return;
    dragging = true; brushActive = true; x0 = e.offsetX;
    hideHoverGuide();
    brushOverlay.style.left = x0 + 'px';
    brushOverlay.style.top = r.top + 'px';
    brushOverlay.style.height = (r.bottom - r.top) + 'px';
    brushOverlay.style.width = '0px';
    brushOverlay.style.display = 'block';
    showBrushTimes(x0, x0);
  });
  zr.on('mousemove', (e) => {
    if (!dragging) return;
    const r = plotRect();
    const x = Math.max(r.left, Math.min(e.offsetX, r.right));
    brushOverlay.style.left = Math.min(x0, x) + 'px';
    brushOverlay.style.width = Math.abs(x - x0) + 'px';
    showBrushTimes(x0, x);
  });
  const finish = (e) => {
    if (!dragging) return;
    dragging = false;
    // 图内选区仅作拖动过程的实时反馈（用户裁定：缩放后 task 背景不高亮）。
    brushOverlay.style.display = 'none';
    hideBrushTimes();
    brushActive = false;
    const r = plotRect();
    const x1 = Math.max(r.left, Math.min(e.offsetX ?? x0, r.right));
    if (Math.abs(x1 - x0) > 5) {
      suppressClick = true;
      setTimeout(() => { suppressClick = false; }, 0);
      const t0 = chart.convertFromPixel({ xAxisIndex: 0 }, Math.min(x0, x1));
      const t1 = chart.convertFromPixel({ xAxisIndex: 0 }, Math.max(x0, x1));
      const span = extent.hi - extent.lo || 1;
      chart.dispatchAction({ type: 'dataZoom',
        start: (t0 - extent.lo) / span * 100,
        end: (t1 - extent.lo) / span * 100 });
    }
  };
  zr.on('mouseup', finish);
  zr.on('mouseleave', finish);
}

// ---- 自绘底部时间范围选取条（用户裁定：三个交互分区明确分离）----
//   · 选区内（中间 70%）按下拖动 = 平移当前视图（保持宽度）
//   · 选区左右边缘（各 15%）按下拖动 = 调整该侧边界
//   · 选区外/全程时任意位置按下拖动 = 框选新范围
// 全程（未选范围）时不显示选区高亮（仅暗色底条）。光标随分区变化。
function attachSlider(ctx) {
  const bar = document.getElementById('tl-slider');
  const sel = document.getElementById('tl-slider-sel');
  if (!bar || !sel) return;

  const EDGE = 0.15;   // 选区两侧 15% 宽度为边缘调整热区
  let mode = null;     // 'pan' | 'left' | 'right' | 'new'
  let startX = 0, origStart = 0, origEnd = 0;

  const apply = (start, end) => {
    const s = Math.max(0, Math.min(100, start));
    const e = Math.max(s + 0.5, Math.min(100, end));
    chart.dispatchAction({ type: 'dataZoom', start: s, end: e });
  };

  bar.addEventListener('mousedown', (e) => {
    if (!chart) return;
    const rect = bar.getBoundingClientRect();
    startX = e.clientX - rect.left;
    const w = rect.width || 1;
    const z = currentZoom();
    origStart = z.start; origEnd = z.end;
    const isFull = z.start <= 0.5 && z.end >= 99.5;
    if (!isFull) {
      const sPx = z.start / 100 * w, ePx = z.end / 100 * w;
      const width = ePx - sPx;
      if (startX >= sPx && startX <= ePx) {
        if (startX <= sPx + width * EDGE) mode = 'left';
        else if (startX >= ePx - width * EDGE) mode = 'right';
        else mode = 'pan';
        e.preventDefault();
        return;
      }
    }
    mode = 'new';
    // 框选起点即当前按下点，拖动过程实时预览。
    origStart = startX / w * 100; origEnd = origStart;
    e.preventDefault();
  });

  document.addEventListener('mousemove', (e) => {
    if (!mode || !chart) return;
    const rect = bar.getBoundingClientRect();
    const x = Math.max(0, Math.min(e.clientX - rect.left, rect.width));
    const w = rect.width || 1;
    const dxPct = (x - startX) / w * 100;
    const curPct = x / w * 100;
    if (mode === 'pan') {
      const width = origEnd - origStart;
      let s = origStart + dxPct;
      s = Math.max(0, Math.min(100 - width, s));
      apply(s, s + width);
    } else if (mode === 'left') {
      apply(Math.min(origStart + dxPct, origEnd - 0.5), origEnd);
    } else if (mode === 'right') {
      apply(origStart, Math.max(origEnd + dxPct, origStart + 0.5));
    } else {  // new
      apply(Math.min(origStart, curPct), Math.max(origStart, curPct));
    }
  });
  document.addEventListener('mouseup', () => { mode = null; });

  // 分区光标提示（非拖动状态）。
  bar.addEventListener('mousemove', (e) => {
    if (mode) return;
    const rect = bar.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const w = rect.width || 1;
    const z = currentZoom();
    if (z.start <= 0.5 && z.end >= 99.5) {
      bar.style.cursor = 'crosshair';
      return;
    }
    const sPx = z.start / 100 * w, ePx = z.end / 100 * w;
    const width = ePx - sPx;
    if (x < sPx || x > ePx) bar.style.cursor = 'crosshair';
    else if (x <= sPx + width * EDGE || x >= ePx - width * EDGE) {
      bar.style.cursor = 'ew-resize';
    } else bar.style.cursor = 'grab';
  });
  bar.addEventListener('mouseleave', () => { if (!mode) bar.style.cursor = 'default'; });
}

// 选取条视觉同步当前视图范围（全程时隐藏选区高亮——用户裁定）。
function syncSliderVisual() {
  const sel = document.getElementById('tl-slider-sel');
  if (!sel) return;
  const z = currentZoom();
  if (z.start <= 0.5 && z.end >= 99.5) {
    sel.style.display = 'none';
    return;
  }
  sel.style.left = z.start + '%';
  sel.style.width = (z.end - z.start) + '%';
  sel.style.display = 'block';
}

function syncControls(ctx) {
  const sortSel = document.getElementById('tl-sort');
  if (sortSel) sortSel.value = ctx.tlSort;
  const dimSel = document.getElementById('tl-dim');
  if (dimSel) dimSel.value = ctx.tlColorDim;
  const stripeBtn = document.getElementById('tl-stripe');
  if (stripeBtn) {
    stripeBtn.textContent = t(ctx.tlStripe ? 'tl.stripeOn' : 'tl.stripeOff');
    stripeBtn.classList.toggle('active', ctx.tlStripe);
  }
  renderLegend(ctx);
}

function renderLegend(ctx) {
  const el = document.getElementById('tl-legend');
  if (!el) return;
  const dim = ctx.tlColorDim;
  const c = dimColors();
  const neutral = neutralColor();
  el.innerHTML = `
    <span class="lg"><i style="background:${c[dim]}"></i>${t('tl.legendHigh', DIM_LABEL[dim])}</span>
    <span class="lg"><i style="background:${c[dim]}80"></i>${t('tl.legendMid')}</span>
    <span class="lg"><i style="background:${neutral}"></i>${t('tl.legendLow')}</span>
    <span class="lg"><i class="striped" style="background-color:${c[dim]}"></i>${t('tl.legendCompound')}</span>
    <span class="lg"><i style="background:${fastColor()}"></i>${t('tl.legendFast')}</span>
    <span class="lg"><i style="background:${neutral};border:2px solid ${failStroke()}"></i>${t('tl.legendFailed')}</span>`;
}

// y 轴标签区命中：x 在 [0, GRID.left)，y 落在 plot 区按泳道等分判定。
function laneAt(x, y) {
  const el = document.getElementById('tl-chart');
  if (!el || !lanes.length) return -1;
  const chartH = el.clientHeight;
  const plotTop = GRID.top, plotBottom = chartH - GRID.bottom;
  if (x < 0 || x >= GRID.left || y < plotTop || y >= plotBottom) return -1;
  const idx = Math.floor((y - plotTop) / ((plotBottom - plotTop) / lanes.length));
  return Math.min(Math.max(idx, 0), lanes.length - 1);
}

// Timeline task 增量缓存（增强刷新）：首轮全量建缓存，之后每轮只拉
// 「新增 + 刚完成（窗口更新）」并按 task_id merge；游标取本轮见过的
// 最大 completed_ms（RUNNING 的无 completed 不推进游标）。run 切换由
// app.js 清（resetSamplesCache 同时机）。
let tlCache = { tasks: new Map(), cursor: 0, runKey: null };
export function resetTimelineCache() {
  tlCache = { tasks: new Map(), cursor: 0, runKey: null };
}

async function fetchTimelineIncremental(runKey) {
  if (tlCache.runKey !== runKey) {
    tlCache = { tasks: new Map(), cursor: 0, runKey };
  }
  const url = tlCache.tasks.size === 0
    ? '/api/timeline'
    : `/api/timeline?changed_since_ms=${tlCache.cursor}`;
  const data = await getJson(url);
  if (!data) return [...tlCache.tasks.values()];
  for (const t of (data.tasks || [])) {
    tlCache.tasks.set(t.task_id, t);  // 新增或替换（窗口更新）
    // 游标只由 completed 推进：新 RUNNING task（exec_start>游标）每轮重传
    // 一次直至完成——数量 = 当前并发运行数，可接受；若用 exec_start 推进
    // 会漏掉"较早派发、稍后才完成"的旧 RUNNING task 的窗口更新。
    if (t.completed_ms && t.completed_ms > tlCache.cursor) {
      tlCache.cursor = t.completed_ms;
    }
  }
  return [...tlCache.tasks.values()];
}

export async function update(ctx) {
  if (!chart) return;
  const [runKey, workers, meta] = await Promise.all([
    getJson('/api/meta').then(m => m ? m.meta.run_start_ms : null),
    getJson('/api/workers'),
    getJson('/api/meta'),
  ]);
  const tasks = await fetchTimelineIncremental(runKey);
  lastTasks = tasks;
  hostMap = {};
  if (workers) {
    for (const w of workers.workers) hostMap[w.worker_id] = w.hostname || '?';
  }
  hostMap[0] = (meta && meta.meta && meta.meta.hostname) || 'master';

  lanes = [...new Set(tasks.map(tk => tk.worker_id))];
  if (ctx.tlSort === 'host') {
    lanes.sort((a, b) => (hostMap[a] || '').localeCompare(hostMap[b] || '') || (a - b));
  } else {
    lanes.sort((a, b) => a - b);
  }

  // 紧凑泳道：每泳道 LANE_H + 顶部图例/底部滑块。
  const chartH = Math.max(GRID.top + GRID.bottom + LANE_H,
                          lanes.length * LANE_H + GRID.top + GRID.bottom);
  document.getElementById('tl-chart').style.height = chartH + 'px';
  chart.resize();

  // 单 series + 逐 datum 着色（主色档位 + Failed 描边 + 条纹 group）。
  const seriesData = tasks.map(tk => {
    const r = ratios(tk);
    const failed = tk.status === 'FAILED';
    const fast = isFast(tk) && !failed;
    return {
      value: [lanes.indexOf(tk.worker_id), tk.exec_start_ms,
              tk.exec_end_ms || tk.exec_start_ms + 50, tk.task_id],
      name: tk.name,
      _r: r, _failed: failed, _fast: fast,
      itemStyle: fast ? { color: fastColor() }
        : { color: dimTierColor(ctx.tlColorDim, r[ctx.tlColorDim]),
            borderColor: failed ? failStroke() : 'transparent',
            borderWidth: failed ? 2 : 0 },
    };
  });

  // notMerge 全量替换会重置 dataZoom 状态——刷新前保存、替换后恢复。
  // · 全程状态（含「复原缩放」后）：保持 0-100 不做时间锚定——否则旧
  //   extent 末端被换算成 <100 的百分比，视图锚死在旧数据末端、无法随
  //   新数据推进（复原后应回归初始跟随状态）。
  // · 子范围（用户已选取）：按绝对时间锚定（非百分比）——运行中新数据
  //   持续扩大 extent，同样百分比对应的时间段会漂移；先换算成绝对时刻，
  //   setOption（新 extent）后再换算回百分比，选取的时间段锚定不变。
  const zPct = ctx._tlRestoreZoom || currentZoom();
  ctx._tlRestoreZoom = null;
  const wasFull = zPct.start <= 0.5 && zPct.end >= 99.5;
  const oldExtent = { ...extent };   // ganttOption 调用前保存（其中会更新 extent）
  const spanOld = oldExtent.hi - oldExtent.lo || 1;
  const t0 = oldExtent.lo + spanOld * zPct.start / 100;
  const t1 = oldExtent.lo + spanOld * zPct.end / 100;
  chart.setOption(ganttOption(lanes, seriesData, ctx), true);
  if (wasFull) {
    chart.dispatchAction({ type: 'dataZoom', start: 0, end: 100 });
  } else {
    const spanNew = extent.hi - extent.lo || 1;
    const newStart = Math.max(0, (t0 - extent.lo) / spanNew * 100);
    const newEnd = Math.min(100, (t1 - extent.lo) / spanNew * 100);
    chart.dispatchAction({ type: 'dataZoom',
      start: newStart, end: Math.max(newStart + 0.5, newEnd) });
  }
  updateRangeHint();
  syncSliderVisual();
  updateAxisTimes();
  if (ctx._tlRestoreScroll != null && ctx.main) {
    ctx.main.scrollTop = ctx._tlRestoreScroll;
    ctx._tlRestoreScroll = null;
  }
}

function updateRangeHint() {
  const el = document.getElementById('tl-range');
  if (!el || !chart) return;
  const z = currentZoom();
  const span = extent.hi - extent.lo || 1;
  const t1 = extent.lo + span * z.start / 100;
  const t2 = extent.lo + span * z.end / 100;
  el.textContent = z.end - z.start < 99.5
    ? t('tl.view', fmtTime(t1), fmtTime(t2), (z.end - z.start).toFixed(0))
    : t('tl.viewFull', fmtTime(t1), fmtTime(t2));
}

// 驻留详情：时间与负载指标逐项独立展示（不聚合，用户裁定）。
function showInfo(tk) {
  const el = document.getElementById('tl-info');
  if (!el) return;
  const dur = tk.exec_end_ms ? tk.exec_end_ms - tk.exec_start_ms : null;
  const r = ratios(tk);
  el.style.display = 'block';
  el.innerHTML = `
    <h3>task ${tk.task_id} · <span class="badge ${tk.status}">${tk.status}</span>${isFast(tk) ? ' · Fast' : ''}</h3>
    <div class="full-name" style="margin-bottom:8px">${escapeHtml(tk.name)}</div>
    <div class="kv">
      <span class="k">worker</span><span class="v">${tk.worker_id} · ${escapeHtml(hostMap[tk.worker_id] || '?')}</span>
      <span class="k">${t('tb.created')}</span><span class="v mono">${fmtTimeFull(tk.created_ms)}</span>
      <span class="k">${t('tl.depsReadyAt')}</span><span class="v mono">${fmtTimeFull(tk.ready_ms)}</span>
      <span class="k">${t('tl.dispatchAt')}</span><span class="v mono">${fmtTimeFull(tk.started_ms)}</span>
      <span class="k">${t('tl.execStartAt')}</span><span class="v mono">${fmtTimeFull(tk.exec_start_ms)}</span>
      <span class="k">${t('tl.execEndAt')}</span><span class="v mono">${fmtTimeFull(tk.exec_end_ms)}</span>
      <span class="k">${t('tl.completedAt')}</span><span class="v mono">${fmtTimeFull(tk.completed_ms)}</span>
      <span class="k">${t('tb.duration')}</span><span class="v mono">${fmtMs(dur)}</span>
      <span class="k">${t('tl.queueWaitMs')}</span><span class="v">${fmtMs(r.queueMs)}</span>
      <span class="k">${t('tl.queueLifeShare')}</span><span class="v">${fmtPct(r.queue)}</span>
      <span class="k">${t('tb.cpuTime')}</span><span class="v">${fmtMs(tk.cpu_time_ms)}</span>
      <span class="k">${t('kv.cpuShare')}</span><span class="v">${fmtPct(r.cpu)}</span>
      <span class="k">${t('kv.readTime')}</span><span class="v">${fmtMs(tk.read_time_ms)}</span>
      <span class="k">${t('kv.writeTime')}</span><span class="v">${fmtMs(tk.write_time_ms)}</span>
      <span class="k">${t('tl.ioShare')}</span><span class="v">${fmtPct(r.io)}</span>
      <span class="k">${t('tl.idleShare')}</span><span class="v">${fmtPct(r.wait)}${t('tl.idleShareNote')}</span>
    </div>`;
  el.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
}

function hideInfo() {
  const el = document.getElementById('tl-info');
  if (el) el.style.display = 'none';
}

function ganttOption(wids, seriesData, ctx) {
  let lo = Infinity, hi = -Infinity;
  for (const d of seriesData) {
    lo = Math.min(lo, d.value[1]);
    hi = Math.max(hi, d.value[2]);
  }
  if (!isFinite(lo)) { lo = 0; hi = 1; }
  extent = { lo, hi };

  return {
    grid: { left: GRID.left, right: 40, top: GRID.top, bottom: GRID.bottom },
    tooltip: {
      trigger: 'item',
      extraCssText: 'max-width:380px; white-space:normal; word-break:break-all;',
      backgroundColor: chartColors().panel2,
      borderColor: chartColors().axis,
      textStyle: { color: chartColors().text, fontSize: 12 },
      formatter: p => {
        const d = seriesData.find(x => x.value[3] === p.value[3]) || {};
        const [, s, e, tid] = p.value;
        const dur = ((e - s) / 1000).toFixed(2);
        const r = d._r || {};
        const load = d._fast ? t('tl.fastTask')
          : t('tl.ttLoad', fmtPct(r.queue), fmtPct(r.cpu), fmtPct(r.io), fmtPct(r.wait));
        return `<b>#${tid}</b> ${shortName(p.name, 12, 8)}<br>${load}` +
               `<br>${new Date(s).toLocaleTimeString()} → ` +
               `${new Date(e).toLocaleTimeString()}（${dur}s）` +
               `<br><span style="color:${chartColors().label}">${escapeHtml(t('tl.ttClickDetail'))}</span>`;
      },
    },
    // animation false：去除缩放/平移时坐标轴标签的动态过渡（拖快时时间
    // 重影、停止时归位动画——用户裁定只要静态）。
    animation: false,
    xAxis: {
      type: 'time',
      axisLine: { lineStyle: { color: chartColors().axis } },
      // 中间刻度标签隐藏（动态重排的来源）；视图起止时间由自绘 DOM 在
      // 底部左右角显示（见 attachAxisTimes），中间时间经悬停竖线查看。
      axisLabel: { show: false },
      axisTick: { show: false },
    },
    yAxis: {
      type: 'category',
      // inverse：category 轴默认从下往上渲染——升序数组视觉上呈降序；
      // 反转后第一个泳道（最小 worker id）显示在顶部。
      inverse: true,
      data: wids.map(w => w === 0 ? `master(0) · ${hostMap[0]}`
                                  : `worker ${w} · ${hostMap[w] || '?'}`),
      axisLine: { lineStyle: { color: chartColors().axis } },
      axisLabel: { color: chartColors().accent },
    },
    // 缩放：仅滚轮缩放（inside 的 moveOnMouseMove/moveOnMouseWheel 默认
    // 开启会导致图内移动/拖动鼠标时平移坐标轴——用户裁定只保留拖动框选
    // 缩放，平移经底部选取条选区拖动完成）。
    // filterMode 'none'：不做数据过滤只缩放坐标轴——默认 'filter' 会把
    // 起点在窗口外的 task 条整条移除（横跨窗口边界的条消失，显示上像
    // 该时段无 task 运行）；'none' + series clip 让与窗口有交集的条都
    // 显示并裁剪越界部分。
    dataZoom: [
      { type: 'inside', xAxisIndex: 0, filterMode: 'none',
        moveOnMouseMove: false, moveOnMouseWheel: false,
        zoomOnMouseWheel: true },
    ],
    series: [{
      type: 'custom',
      // clip：缩放/平移时条形不得越出 grid（否则左侧盖住 worker 名、右侧
      // 超出边界——custom series 默认不裁剪）。
      clip: true,
      renderItem: (params, api) => {
        const lane = api.value(0);
        const start = api.coord([api.value(1), lane]);
        const end = api.coord([api.value(2), lane]);
        const x = start[0], y = start[1] - GRID.barH / 2;
        const w = Math.max(end[0] - start[0], 2);
        const d = seriesData[params.dataIndex];
        const base = {
          type: 'rect',
          shape: { x, y, width: w, height: GRID.barH, r: 3 },
          style: api.style(),
        };
        // 条纹（复合负载）：底条之上叠加斜线（裁剪在条形范围内）。
        if (ctx && ctx.tlStripe && d && !d._fast && !d._failed &&
            isCompound(d, ctx.tlColorDim) && w > 12) {
          const lines = [];
          for (let sx = x - GRID.barH; sx < x + w; sx += STRIPE_GAP) {
            lines.push({
              type: 'line',
              shape: { x1: Math.max(sx, x), y1: y + GRID.barH,
                      x2: Math.min(sx + GRID.barH, x + w), y2: y },
              style: { stroke: 'rgba(13,17,22,.55)', lineWidth: 2 },
              silent: true,
            });
          }
          return { type: 'group', children: [base, ...lines] };
        }
        return base;
      },
      encode: { x: [1, 2], y: 0 },
      data: seriesData,
    }],
  };
}
