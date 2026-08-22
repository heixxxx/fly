// Timeline：按 worker 分泳道的 task 执行窗口 Gantt（ECharts custom series）。
import { getJson } from '../api.js';
import { makeChart } from '../charts.js';

const STATUS_COLOR = {
  COMPLETED: '#3fb972', FAILED: '#e0564f', RUNNING: '#4aa8ff', PENDING: '#8a7ca8',
  CANCELLED: '#7a8a9c',
};

let chart = null;

export function destroy() {
  if (chart) { chart.dispose(); chart = null; }
}

export async function render(ctx) {
  const main = ctx.main;
  const data = await getJson('/api/timeline');
  if (!data) { main.innerHTML = '<div class="panel">API 不可达</div>'; return; }
  const tasks = data.tasks || [];

  // 泳道：worker_id 升序（0=master 在最上）。
  const lanes = [...new Set(tasks.map(t => t.worker_id))].sort((a, b) => a - b);
  const laneIdx = new Map(lanes.map((w, i) => [w, i]));

  main.innerHTML = `
    <div class="panel">
      <h3>task 执行窗口（worker 泳道 · 滚轮缩放 · 拖拽平移）</h3>
      <div id="tl-chart" style="width:100%;height:${Math.max(220, lanes.length * 64 + 120)}px"></div>
    </div>`;

  const seriesData = tasks.map(t => ({
    value: [laneIdx.get(t.worker_id), t.exec_start_ms, t.exec_end_ms || t.exec_start_ms + 50, t.task_id],
    name: t.name,
    itemStyle: { color: STATUS_COLOR[t.status] || '#4aa8ff' },
  }));

  chart = makeChart(document.getElementById('tl-chart'), {
    grid: { left: 90, right: 40, top: 20, bottom: 60 },
    tooltip: {
      trigger: 'item',
      formatter: p => {
        const [lane, s, e, tid] = p.value;
        const t = tasks.find(x => x.task_id === tid) || {};
        const dur = ((e - s) / 1000).toFixed(2);
        return `<b>#${tid} ${t.name || ''}</b><br>` +
          `worker ${t.worker_id} · ${t.status}<br>` +
          `${new Date(s).toLocaleTimeString()} → ${new Date(e).toLocaleTimeString()}（${dur}s）`;
      },
    },
    xAxis: {
      type: 'time',
      axisLine: { lineStyle: { color: '#37424f' } },
      axisLabel: { color: '#7a8a9c' },
    },
    yAxis: {
      type: 'category',
      data: lanes.map(w => w === 0 ? 'master(0)' : `worker ${w}`),
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
        const rect = {
          type: 'rect',
          shape: { x: start[0], y: start[1] - height / 2, width: Math.max(end[0] - start[0], 2), height, r: 3 },
          style: api.style(),
        };
        return rect;
      },
      encode: { x: [1, 2], y: 0 },
      data: seriesData,
    }],
  });
}
