// Timeline：按 worker 分泳道的 task 执行窗口 Gantt。
// - 泳道标签含 host（"worker N · host"），支持按 worker id（默认增序）/
//   按 host（同 host 相邻，host 内按 id）排序切换。
// - 缩放：滚轮 + 底部滑块拖选范围；滑块上方实时显示当前视图时间范围
//   （初次使用即可见的提示）；「复原缩放」一键回到全程。
// - 点击条形 → 底部驻留信息面板（全名折行 + 时间窗/状态）。
import { getJson, escapeHtml, shortName, fmtTimeFull, fmtMs, fmtTime } from '../api.js';
import { makeChart } from '../charts.js';
import { navigate } from '../app.js';

const STATUS_COLOR = {
  COMPLETED: '#3fb972', FAILED: '#e0564f', RUNNING: '#4aa8ff', PENDING: '#8a7ca8',
  CANCELLED: '#7a8a9c',
};

let chart = null;
let lastTasks = [];      // update 缓存（点击面板按 task_id 查全量字段）
let hostMap = {};        // wid → hostname（/api/workers；wid 0 用 meta.hostname）
let extent = { lo: 0, hi: 1 };  // 数据全范围（dataZoom 百分比 → 时间换算）

export function destroy() {
  if (chart) { chart.dispose(); chart = null; }
  lastTasks = [];
  hostMap = {};
}

export function mount(ctx) {
  ctx.tlSort = ctx.tlSort || 'id';  // 'id' | 'host'（默认按 worker id 增序）
  ctx.main.innerHTML = `
    <div class="panel">
      <div class="controls">
        <button id="tl-sort-id" class="active">按 worker id 排序</button>
        <button id="tl-sort-host">按 host 排序</button>
        <button id="tl-reset-zoom">↺ 复原缩放</button>
        <span class="tl-hint">滚轮缩放 · 底部滑块拖选范围 · 点击条形查看驻留详情</span>
        <span class="tl-range" id="tl-range"></span>
      </div>
      <div id="tl-chart" style="width:100%;height:360px"></div>
    </div>
    <div class="panel" id="tl-info" style="display:none"></div>`;
  chart = makeChart(document.getElementById('tl-chart'), ganttOption([], []));

  document.getElementById('tl-sort-id').onclick = () => {
    ctx.tlSort = 'id'; toggleSortButtons(); navigate();
  };
  document.getElementById('tl-sort-host').onclick = () => {
    ctx.tlSort = 'host'; toggleSortButtons(); navigate();
  };
  document.getElementById('tl-reset-zoom').onclick = () => {
    if (!chart) return;
    chart.dispatchAction({ type: 'dataZoom', start: 0, end: 100 });
  };

  function toggleSortButtons() {
    document.getElementById('tl-sort-id').classList.toggle(
      'active', ctx.tlSort === 'id');
    document.getElementById('tl-sort-host').classList.toggle(
      'active', ctx.tlSort === 'host');
  }

  // 点击条形：驻留信息面板（数据来自 update 缓存）。
  chart.on('click', (params) => {
    if (params.seriesType !== 'custom') return;
    const tid = params.value[3];
    const t = lastTasks.find(x => x.task_id === tid);
    if (t) showInfo(t);
  });
  // 点击图表空白处：取消驻留。
  chart.getZr().on('click', (e) => {
    if (!e.target) hideInfo();
  });
  // 缩放变化：更新当前视图范围提示（滑块拖选的即时反馈）。
  chart.on('dataZoom', updateRangeHint);
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

  // 泳道：按当前排序模式排列（host 模式：host 字典序，同 host 内按 id）。
  const wids = [...new Set(tasks.map(t => t.worker_id))];
  if (ctx.tlSort === 'host') {
    wids.sort((a, b) => (hostMap[a] || '').localeCompare(hostMap[b] || '') || (a - b));
  } else {
    wids.sort((a, b) => a - b);
  }

  const seriesData = tasks.map(t => ({
    value: [wids.indexOf(t.worker_id), t.exec_start_ms,
            t.exec_end_ms || t.exec_start_ms + 50, t.task_id],
    name: t.name,
    itemStyle: { color: STATUS_COLOR[t.status] || '#4aa8ff' },
  }));
  chart.setOption(ganttOption(wids, seriesData));
  updateRangeHint();
}

// dataZoom 百分比 → 时间范围提示（所见即所得的拖选反馈）。
function updateRangeHint() {
  const el = document.getElementById('tl-range');
  if (!el || !chart) return;
  const opt = chart.getOption();
  const dz = (opt.dataZoom || [{}])[0];
  const startPct = dz.start ?? 0, endPct = dz.end ?? 100;
  const span = extent.hi - extent.lo || 1;
  const t1 = extent.lo + span * startPct / 100;
  const t2 = extent.lo + span * endPct / 100;
  el.textContent = `视图：${fmtTime(t1)} → ${fmtTime(t2)}` +
                   (endPct - startPct < 99.5 ? `（${(endPct - startPct).toFixed(0)}%）` : '（全程）');
}

function showInfo(t) {
  const el = document.getElementById('tl-info');
  if (!el) return;
  const dur = t.exec_end_ms ? t.exec_end_ms - t.exec_start_ms : null;
  el.style.display = 'block';
  el.innerHTML = `
    <h3>task ${t.task_id}</h3>
    <div class="full-name" style="margin-bottom:8px">${escapeHtml(t.name)}</div>
    <div class="kv">
      <span class="k">状态</span><span class="v"><span class="badge ${t.status}">${t.status}</span></span>
      <span class="k">worker</span><span class="v">${t.worker_id} · ${escapeHtml(hostMap[t.worker_id] || '?')}</span>
      <span class="k">执行窗口</span><span class="v mono">${fmtTimeFull(t.exec_start_ms)} → ${fmtTimeFull(t.exec_end_ms)}</span>
      <span class="k">时长</span><span class="v">${fmtMs(dur)}</span>
      <span class="k">调度</span><span class="v mono">派发 ${fmtTimeFull(t.started_ms)} · 完成 ${fmtTimeFull(t.completed_ms)}</span>
    </div>`;
  el.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
}

function hideInfo() {
  const el = document.getElementById('tl-info');
  if (el) el.style.display = 'none';
}

function ganttOption(wids, seriesData) {
  // 数据全范围（换算 dataZoom 百分比用）。
  let lo = Infinity, hi = -Infinity;
  for (const d of seriesData) {
    lo = Math.min(lo, d.value[1]);
    hi = Math.max(hi, d.value[2]);
  }
  if (!isFinite(lo)) { lo = 0; hi = 1; }
  extent = { lo, hi };

  return {
    grid: { left: 170, right: 40, top: 20, bottom: 60 },
    tooltip: {
      trigger: 'item',
      extraCssText: 'max-width:360px; white-space:normal; word-break:break-all;',
      formatter: p => {
        const [, s, e, tid] = p.value;
        const dur = ((e - s) / 1000).toFixed(2);
        return `<b>#${tid}</b> ${shortName(p.name, 12, 8)}<br>` +
               `${new Date(s).toLocaleTimeString()} → ` +
               `${new Date(e).toLocaleTimeString()}（${dur}s）` +
               `<br><span style="color:#7a8a9c">点击条形查看驻留详情</span>`;
      },
    },
    xAxis: {
      type: 'time',
      axisLine: { lineStyle: { color: '#37424f' } },
      axisLabel: { color: '#7a8a9c' },
    },
    yAxis: {
      type: 'category',
      data: wids.map(w => w === 0 ? `master(0) · ${hostMap[0]}`
                                  : `worker ${w} · ${hostMap[w] || '?'}`),
      axisLine: { lineStyle: { color: '#37424f' } },
      axisLabel: { color: '#d7e0ea' },
    },
    dataZoom: [
      { type: 'inside', xAxisIndex: 0 },
      { type: 'slider', xAxisIndex: 0, height: 18, bottom: 12,
        borderColor: '#2a3542', backgroundColor: '#161d26',
        fillerColor: 'rgba(74,168,255,.15)', textStyle: { color: '#7a8a9c' } },
    ],
    series: [{
      type: 'custom',
      renderItem: (params, api) => {
        const lane = api.value(0);
        const start = api.coord([api.value(1), lane]);
        const end = api.coord([api.value(2), lane]);
        const height = 14;
        return {
          type: 'rect',
          shape: { x: start[0], y: start[1] - height / 2,
                  width: Math.max(end[0] - start[0], 2), height, r: 3 },
          style: api.style(),
        };
      },
      encode: { x: [1, 2], y: 0 },
      data: seriesData,
    }],
  };
}
