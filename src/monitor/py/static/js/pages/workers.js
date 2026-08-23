// Workers：卡片列表 + 详情（四图 + 该 worker 的 task 表）。
// mount 建外层容器；update 按 ctx.workerId 填列表或详情——详情骨架/图表
// 实例只在进入时建一次，后续 update 仅 setOption（缩放/hover 保留）。
import { getJson, fmtGB, fmtBytes, fmtMs, escapeHtml, expandoHtml } from '../api.js';
import { makeChart, line, rateSeries } from '../charts.js';
import { navigate } from '../app.js';

let charts = [];
let detailBuilt = false;   // 详情骨架与图表实例是否已建
let detailWid = -1;        // 骨架对应的 worker（切换时重建）

export function destroy() {
  charts.forEach(c => c.dispose());
  charts = [];
  detailBuilt = false;
  detailWid = -1;
}

export function mount(ctx) {
  ctx.main.innerHTML = `<div id="w-body"></div>`;
}

export async function update(ctx) {
  const body = document.getElementById('w-body');
  if (!body) return;

  // 子视图切换（列表 ↔ 详情 / 详情换 worker）：销毁旧图表、重建骨架。
  if (ctx.workerId !== detailWid) {
    charts.forEach(c => c.dispose());
    charts = [];
    detailBuilt = false;
    detailWid = ctx.workerId;
  }

  if (ctx.workerId == null) {
    const data = await getJson('/api/workers');
    if (!data) return;
    body.innerHTML = `<div class="worker-cards">${data.workers.map(card).join('')}</div>`;
    body.onclick = (e) => {
      const el = e.target.closest('.worker-card');
      if (el) { ctx.workerId = +el.dataset.wid; navigate(); }
    };
    return;
  }
  await fillDetail(body, ctx.workerId);
}

function card(w) {
  const l = w.latest;
  const rss = l ? fmtGB(l.proc_rss_bytes) : '-';
  const cpu = l ? (l.proc_cpu_bps / 100).toFixed(1) + '%' : '-';
  const hcpu = l ? (l.host_cpu_bps / 100).toFixed(1) + '%' : '-';
  return `<div class="worker-card" data-wid="${w.worker_id}">
    <div class="title">worker ${w.worker_id}
      <span class="muted">${w.hostname}${w.ip ? ':' + w.ip : ''}</span>
      <span class="badge ${w.last_event}" style="float:right">${w.last_event || '-'}</span>
    </div>
    <div class="row"><span>角色</span><b>${w.role || '-'}</b></div>
    <div class="row"><span>进程 RSS / CPU</span><b>${rss} · ${cpu}</b></div>
    <div class="row"><span>机器 CPU</span><b>${hcpu}</b></div>
    <div class="row"><span>属性</span><b>${w.attributes || '-'}</b></div>
  </div>`;
}

async function fillDetail(body, wid) {
  const [s, tasks] = await Promise.all([
    getJson(`/api/workers/${wid}/samples`),
    getJson(`/api/tasks?worker=${wid}&limit=300`),
  ]);
  if (!s) return;
  const sp = s.samples || [];
  const times = sp.map(x => x.epoch_ms);

  if (!detailBuilt) {
    body.innerHTML = `
      <span class="back-link" id="w-back">← 返回 worker 列表</span>
      <div class="kpi-row" id="w-kpi"></div>
      <div class="grid cols-2">
        <div class="panel"><h3>CPU（进程 vs 机器）</h3><div id="w-cpu" class="chart"></div></div>
        <div class="panel"><h3>内存（进程 vs 机器）</h3><div id="w-mem" class="chart"></div></div>
        <div class="panel"><h3>网络 IO 速率（读/写）</h3><div id="w-net" class="chart"></div></div>
        <div class="panel"><h3>机器平均负载（Load1）</h3>
        <div class="muted" style="font-size:12px;margin:-6px 0 6px">Load1：1 分钟平均可运行任务数（含等 IO），健康参考值 ≈ CPU 核数</div><div id="w-load" class="chart"></div></div>
      </div>
      <div class="panel"><h3 id="w-task-title"></h3>
        <div class="table-wrap"><table>
          <thead><tr><th>ID</th><th>名称</th><th>状态</th><th>运行时长</th><th>CPU</th><th>读/写时间</th><th>avg/peak 内存</th></tr></thead>
          <tbody id="w-tasks"></tbody>
        </table></div>
      </div>`;
    document.getElementById('w-back').onclick = () => { ctx.workerId = null; navigate(); };
    charts = [
      makeChart(document.getElementById('w-cpu'), {}),
      makeChart(document.getElementById('w-mem'), {}),
      makeChart(document.getElementById('w-net'), {}),
      makeChart(document.getElementById('w-load'), {}),
    ];
    detailBuilt = true;
  }

  document.getElementById('w-kpi').innerHTML = `
    <div class="kpi"><div class="label">样本数</div><div class="value">${sp.length}</div></div>
    <div class="kpi"><div class="label">最新进程 RSS</div><div class="value">${sp.length ? fmtGB(sp[sp.length - 1].proc_rss_bytes) : '-'}</div></div>
    <div class="kpi"><div class="label">最新进程 CPU</div><div class="value">${sp.length ? (sp[sp.length - 1].proc_cpu_bps / 100).toFixed(1) + '%' : '-'}</div></div>
    <div class="kpi"><div class="label">网络累计 读/写</div><div class="value" style="font-size:15px">${sp.length ? fmtBytes(sp[sp.length - 1].net_read_bytes) + ' / ' + fmtBytes(sp[sp.length - 1].net_write_bytes) : '-'}</div></div>`;

  charts[0].setOption({
    series: [
      line('Proc CPU (%)', times.map((t, i) => [t, sp[i].proc_cpu_bps / 100]), '#4aa8ff'),
      line('Host CPU (%)', times.map((t, i) => [t, sp[i].host_cpu_bps / 100]), '#e8b339'),
    ],
    yAxis: [{ type: 'value', axisLabel: { color: '#7a8a9c', formatter: '{value}%' } }],
  });
  charts[1].setOption({
    series: [
      line('Proc RSS', times.map((t, i) => [t, sp[i].proc_rss_bytes]), '#4aa8ff', 0, true),
      line('Host Available', times.map((t, i) => [t, sp[i].host_mem_avail_bytes]), '#3fb972'),
      // 总量是参考线：亮色虚线（原 #37424f 与深色背景几乎不可见）。
      { name: 'Host Total', type: 'line',
        data: times.map((t, i) => [t, sp[i].host_mem_total_bytes]),
        showSymbol: false, lineStyle: { width: 1.5, color: '#5f7385', type: 'dashed' },
        itemStyle: { color: '#5f7385' } },
    ],
    yAxis: [{ type: 'value', axisLabel: { color: '#7a8a9c', formatter: v => fmtGB(v) } }],
  });
  charts[2].setOption({
    series: [
      line('Read B/s', rateSeries(times, sp.map(x => x.net_read_bytes)), '#6fd3e8'),
      line('Write B/s', rateSeries(times, sp.map(x => x.net_write_bytes)), '#ff9d5c'),
    ],
    yAxis: [{ type: 'value', axisLabel: { color: '#7a8a9c', formatter: v => fmtBytes(v) } }],
  });
  charts[3].setOption({
    series: [line('Load1', times.map((t, i) => [t, sp[i].host_load1_x100 / 100]), '#c79bf2', 0, true)],
  });

  document.getElementById('w-task-title').textContent =
    `worker ${wid} 的 tasks（${tasks ? tasks.total : 0}）`;
  document.getElementById('w-tasks').innerHTML =
    (tasks ? tasks.tasks : []).map(taskRow).join('');
}

function taskRow(t) {
  const dur = t.exec_end_ms ? t.exec_end_ms - t.exec_start_ms : null;
  return `<tr>
    <td>${t.task_id}</td><td>${expandoHtml(t.name)}</td>
    <td><span class="badge ${t.status}">${t.status}</span></td>
    <td>${fmtMs(dur)}</td>
    <td>${fmtMs(t.cpu_time_ms)}</td>
    <td>${fmtMs(t.read_time_ms)} / ${fmtMs(t.write_time_ms)}</td>
    <td>${fmtGB(t.mem_avg_bytes)} / ${fmtGB(t.mem_peak_bytes)}</td>
  </tr>`;
}

