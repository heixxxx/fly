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
import { makeChart } from '../charts.js';
import { navigate, gotoPage } from '../app.js';

const DIM_COLOR = { cpu: '#4aa8ff', io: '#e8b339', wait: '#8a7ca8', queue: '#6fd3e8' };
const DIM_LABEL = { cpu: 'CPU', io: 'IO', wait: 'Wait', queue: 'Queue' };
const FAST_COLOR = '#3fb972';
const FAIL_STROKE = '#e0564f';
const NEUTRAL = '#4a5563';

// grid 几何（泳道点击的坐标判定与 ganttOption 配置一致）。
const GRID = { left: 170, top: 34, bottom: 60, barH: 16 };
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
function ratios(t) {
  const exec = Math.max(1, (t.exec_end_ms || t.exec_start_ms + 1) - t.exec_start_ms);
  const cpu = (t.cpu_time_ms || 0) / exec;
  const io = ((t.read_time_ms || 0) + (t.write_time_ms || 0)) / exec;
  const queueMs = Math.max(0, (t.started_ms || 0) - (t.ready_ms || t.created_ms || 0));
  const queue = queueMs / (queueMs + exec);
  return { cpu, io, wait: Math.max(0, 1 - cpu - io), queue, queueMs, execMs: exec };
}

// 主色维度档位色：≥50% 实色，10~50% 半透明，<10% 中性灰。
function dimTierColor(dim, ratio) {
  if (ratio >= 0.5) return DIM_COLOR[dim];
  if (ratio >= 0.1) return DIM_COLOR[dim] + '80';  // 50% 透明度
  return NEUTRAL;
}

function isFast(t) {
  return ((t.exec_end_ms || t.exec_start_ms + 1) - t.exec_start_ms) < 500;
}

// 复合判定：除主色维度外任一维度 ≥ HEAVY。
function isCompound(t, dim) {
  const r = ratios(t);
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
        <select id="tl-sort" title="泳道排序">
          <option value="id">按 worker id 排序</option>
          <option value="host">按 host 排序</option>
        </select>
        <span class="tl-sep"></span>
        <span class="muted">主色维度</span>
        <button id="tl-dim-cpu" class="active">CPU</button>
        <button id="tl-dim-io">IO</button>
        <button id="tl-dim-wait">Wait</button>
        <button id="tl-dim-queue">Queue</button>
        <span class="tl-sep"></span>
        <button id="tl-stripe">条纹：关</button>
        <button id="tl-reset-zoom">↺ 复原缩放</button>
        <span class="tl-range" id="tl-range"></span>
      </div>
      <div class="tl-legend" id="tl-legend"></div>
      <div id="tl-chart" style="width:100%;height:240px"></div>
      <div class="tl-hint" style="margin-top:4px">滚轮缩放 · 滑块拖选 · 点击条形看负载详情 · 点击泳道标签跳转 Worker</div>
    </div>
    <div class="panel" id="tl-info" style="display:none"></div>`;
  chart = makeChart(document.getElementById('tl-chart'), ganttOption([], []));

  document.getElementById('tl-sort').onchange = (e) => {
    ctx.tlSort = e.target.value; syncControls(ctx); navigate();
  };
  for (const dim of ['cpu', 'io', 'wait', 'queue']) {
    document.getElementById(`tl-dim-${dim}`).onclick = () => {
      ctx.tlColorDim = dim; syncControls(ctx); navigate();
    };
  }
  document.getElementById('tl-stripe').onclick = () => {
    ctx.tlStripe = !ctx.tlStripe; syncControls(ctx); navigate();
  };
  document.getElementById('tl-reset-zoom').onclick = () => {
    if (chart) chart.dispatchAction({ type: 'dataZoom', start: 0, end: 100 });
  };

  chart.on('click', (params) => {
    if (params.seriesType !== 'custom') return;
    const t = lastTasks.find(x => x.task_id === params.value[3]);
    if (t) showInfo(t);
  });
  chart.getZr().on('click', (e) => {
    if (!e.target) hideInfo();
    const laneIdx = laneAt(e.offsetX, e.offsetY);
    if (laneIdx >= 0 && lanes[laneIdx] != null) {
      gotoPage('workers', { workerId: lanes[laneIdx] }, 'Timeline');
    }
  });
  chart.on('dataZoom', updateRangeHint);
  syncControls(ctx);
}

function syncControls(ctx) {
  const sortSel = document.getElementById('tl-sort');
  if (sortSel) sortSel.value = ctx.tlSort;
  for (const dim of ['cpu', 'io', 'wait', 'queue']) {
    document.getElementById(`tl-dim-${dim}`)?.classList.toggle(
      'active', ctx.tlColorDim === dim);
  }
  const stripeBtn = document.getElementById('tl-stripe');
  if (stripeBtn) {
    stripeBtn.textContent = ctx.tlStripe ? '条纹：开' : '条纹：关';
    stripeBtn.classList.toggle('active', ctx.tlStripe);
  }
  renderLegend(ctx);
}

function renderLegend(ctx) {
  const el = document.getElementById('tl-legend');
  if (!el) return;
  const c = DIM_COLOR[ctx.tlColorDim];
  el.innerHTML = `
    <span class="lg"><i style="background:${c}"></i>${DIM_LABEL[ctx.tlColorDim]} ≥50%</span>
    <span class="lg"><i style="background:${c}80"></i>10–50%</span>
    <span class="lg"><i style="background:${NEUTRAL}"></i>&lt;10%</span>
    <span class="lg"><i class="striped" style="background:${c}"></i>复合（其它维度 ≥30%，条纹开启时）</span>
    <span class="lg"><i style="background:${FAST_COLOR}"></i>Fast &lt;500ms</span>
    <span class="lg"><i style="background:${NEUTRAL};border:2px solid ${FAIL_STROKE}"></i>Failed（红描边）</span>`;
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

export async function update(ctx) {
  if (!chart) return;
  const [data, workers, meta] = await Promise.all([
    getJson('/api/timeline'),
    getJson('/api/workers'),
    getJson('/api/meta'),
  ]);
  if (!data) return;
  const tasks = data.tasks || [];
  lastTasks = tasks;
  hostMap = {};
  if (workers) {
    for (const w of workers.workers) hostMap[w.worker_id] = w.hostname || '?';
  }
  hostMap[0] = (meta && meta.meta && meta.meta.hostname) || 'master';

  lanes = [...new Set(tasks.map(t => t.worker_id))];
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
  const seriesData = tasks.map(t => {
    const r = ratios(t);
    const failed = t.status === 'FAILED';
    const fast = isFast(t) && !failed;
    return {
      value: [lanes.indexOf(t.worker_id), t.exec_start_ms,
              t.exec_end_ms || t.exec_start_ms + 50, t.task_id],
      name: t.name,
      _r: r, _failed: failed, _fast: fast,
      itemStyle: fast ? { color: FAST_COLOR }
        : { color: dimTierColor(ctx.tlColorDim, r[ctx.tlColorDim]),
            borderColor: failed ? FAIL_STROKE : 'transparent',
            borderWidth: failed ? 2 : 0 },
    };
  });

  // notMerge 全量替换会重置 dataZoom 用户状态——刷新前保存、替换后恢复
  //（跳转返回的 restoreZoom 优先）。
  const zoomToApply = ctx._tlRestoreZoom || currentZoom();
  ctx._tlRestoreZoom = null;
  chart.setOption(ganttOption(lanes, seriesData, ctx), true);
  chart.dispatchAction({ type: 'dataZoom', start: zoomToApply.start, end: zoomToApply.end });
  updateRangeHint();
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
  el.textContent = `视图：${fmtTime(t1)} → ${fmtTime(t2)}` +
                   (z.end - z.start < 99.5 ? `（${(z.end - z.start).toFixed(0)}%）` : '（全程）');
}

// 驻留详情：各类负载分项（CPU/IO/等待/排队 的时长与占比）。
function showInfo(t) {
  const el = document.getElementById('tl-info');
  if (!el) return;
  const dur = t.exec_end_ms ? t.exec_end_ms - t.exec_start_ms : null;
  const r = ratios(t);
  const ioMs = (t.read_time_ms || 0) + (t.write_time_ms || 0);
  el.style.display = 'block';
  el.innerHTML = `
    <h3>task ${t.task_id} · <span class="badge ${t.status}">${t.status}</span>${isFast(t) ? ' · Fast' : ''}</h3>
    <div class="full-name" style="margin-bottom:8px">${escapeHtml(t.name)}</div>
    <div class="kv">
      <span class="k">worker</span><span class="v">${t.worker_id} · ${escapeHtml(hostMap[t.worker_id] || '?')}</span>
      <span class="k">运行时长</span><span class="v mono">${fmtMs(dur)}（${fmtTimeFull(t.exec_start_ms)} → ${fmtTimeFull(t.exec_end_ms)}）</span>
      <span class="k">排队等待</span><span class="v">${fmtMs(r.queueMs)}（占生命周期 ${fmtPct(r.queue)}；就绪 ${fmtTimeFull(t.ready_ms)} → 开始调度 ${fmtTimeFull(t.started_ms)}）</span>
      <span class="k">CPU 负载</span><span class="v">${fmtMs(t.cpu_time_ms)}（${fmtPct(r.cpu)}）</span>
      <span class="k">IO 负载</span><span class="v">${fmtMs(ioMs)}（${fmtPct(r.io)}）—— 读 ${fmtMs(t.read_time_ms)} / 写 ${fmtMs(t.write_time_ms)}</span>
      <span class="k">执行等待占比</span><span class="v">${fmtPct(r.wait)}</span>
      <span class="k">调度</span><span class="v mono">开始时间 ${fmtTimeFull(t.started_ms)} · 结束时间 ${fmtTimeFull(t.completed_ms)}</span>
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
      formatter: p => {
        const d = seriesData.find(x => x.value[3] === p.value[3]) || {};
        const [, s, e, tid] = p.value;
        const dur = ((e - s) / 1000).toFixed(2);
        const r = d._r || {};
        let load = '';
        if (d._fast) load = 'Fast Task';
        else load = `Queue ${Math.round((r.queue || 0) * 100)}% · CPU ${Math.round((r.cpu || 0) * 100)}% · IO ${Math.round((r.io || 0) * 100)}% · Wait ${Math.round((r.wait || 0) * 100)}%`;
        return `<b>#${tid}</b> ${shortName(p.name, 12, 8)}<br>${load}` +
               `<br>${new Date(s).toLocaleTimeString()} → ` +
               `${new Date(e).toLocaleTimeString()}（${dur}s）` +
               `<br><span style="color:#7a8a9c">点击条形查看负载详情</span>`;
      },
    },
    xAxis: {
      type: 'time',
      axisLine: { lineStyle: { color: '#37424f' } },
      axisLabel: { color: '#7a8a9c' },
    },
    yAxis: {
      type: 'category',
      // inverse：category 轴默认从下往上渲染——升序数组视觉上呈降序；
      // 反转后第一个泳道（最小 worker id）显示在顶部。
      inverse: true,
      data: wids.map(w => w === 0 ? `master(0) · ${hostMap[0]}`
                                  : `worker ${w} · ${hostMap[w] || '?'}`),
      axisLine: { lineStyle: { color: '#37424f' } },
      axisLabel: { color: '#4aa8ff' },
    },
    dataZoom: [
      { type: 'inside', xAxisIndex: 0 },
      { type: 'slider', xAxisIndex: 0, height: 20, bottom: 12,
        borderColor: '#3a4a5c', backgroundColor: '#161d26',
        // 选取范围的高亮反馈：填充提亮 + 圆形手柄着色 + 全量数据暗底/
        // 选区蓝色数据背景对比。
        fillerColor: 'rgba(74,168,255,.35)',
        dataBackground: {
          lineStyle: { color: '#37424f', width: 1 },
          areaStyle: { color: 'rgba(55,66,79,.35)' },
        },
        selectedDataBackground: {
          lineStyle: { color: '#4aa8ff', width: 1 },
          areaStyle: { color: 'rgba(74,168,255,.30)' },
        },
        handleIcon: 'circle', handleSize: '120%',
        handleStyle: { color: '#4aa8ff', borderColor: '#1c2530' },
        moveHandleSize: 5,
        moveHandleStyle: { color: '#4aa8ff' },
        emphasis: { handleStyle: { borderColor: '#4aa8ff' },
                    moveHandleStyle: { color: '#6fb9ff' } },
        textStyle: { color: '#7a8a9c' } },
    ],
    series: [{
      type: 'custom',
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
