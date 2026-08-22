// Workers：worker 卡片列表 + 详情（CPU/内存/网络/负载曲线 + 该 worker 的 task）。
import { getJson, fmtBytes, fmtMs, fmtTime } from '../api.js';
import { makeChart, line, rateSeries } from '../charts.js';
import { rerender } from '../app.js';

let charts = [];

export function destroy() {
  charts.forEach(c => c.dispose());
  charts = [];
}

export async function render(ctx) {
  const main = ctx.main;
  if (ctx.workerId != null) {
    await renderDetail(ctx, ctx.workerId);
    return;
  }
  const data = await getJson('/api/workers');
  if (!data) { main.innerHTML = '<div class="panel">API 不可达</div>'; return; }

  main.innerHTML = `
    <div class="worker-cards">
      ${data.workers.map(card).join('')}
    </div>`;
  for (const el of main.querySelectorAll('.worker-card')) {
    el.onclick = () => { ctx.workerId = +el.dataset.wid; rerender(); };
  }
}

function card(w) {
  const l = w.latest;
  const rss = l ? fmtBytes(l.proc_rss_bytes) : '-';
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

async function renderDetail(ctx, wid) {
  const main = ctx.main;
  const [s, tasks] = await Promise.all([
    getJson(`/api/workers/${wid}/samples`),
    getJson(`/api/tasks?worker=${wid}&limit=300`),
  ]);
  if (!s) { main.innerHTML = '<div class="panel">API 不可达</div>'; return; }
  const sp = s.samples || [];
  const times = sp.map(x => x.epoch_ms);

  main.innerHTML = `
    <span class="back-link" id="w-back">← 返回 worker 列表</span>
    <div class="kpi-row">
      <div class="kpi"><div class="label">样本数</div><div class="value">${sp.length}</div></div>
      <div class="kpi"><div class="label">最新进程 RSS</div><div class="value">${sp.length ? fmtBytes(sp[sp.length - 1].proc_rss_bytes) : '-'}</div></div>
      <div class="kpi"><div class="label">最新进程 CPU</div><div class="value">${sp.length ? (sp[sp.length - 1].proc_cpu_bps / 100).toFixed(1) + '%' : '-'}</div></div>
      <div class="kpi"><div class="label">网络累计 读/写</div><div class="value" style="font-size:15px">${sp.length ? fmtBytes(sp[sp.length - 1].net_read_bytes) + ' / ' + fmtBytes(sp[sp.length - 1].net_write_bytes) : '-'}</div></div>
    </div>
    <div class="grid cols-2">
      <div class="panel"><h3>CPU%（本进程 vs 机器）</h3><div id="w-cpu" class="chart"></div></div>
      <div class="panel"><h3>内存（进程 RSS vs 机器可用）</h3><div id="w-mem" class="chart"></div></div>
      <div class="panel"><h3>网络 IO 速率（读/写 B/s）</h3><div id="w-net" class="chart"></div></div>
      <div class="panel"><h3>机器 load1</h3><div id="w-load" class="chart"></div></div>
    </div>
    <div class="panel"><h3>worker ${wid} 的 tasks（${tasks ? tasks.total : 0}）</h3>
      <div class="table-wrap"><table>
        <thead><tr><th>ID</th><th>名称</th><th>状态</th><th>执行时长</th><th>CPU</th><th>读/写时间</th><th>avg/peak 内存</th></tr></thead>
        <tbody>${(tasks ? tasks.tasks : []).map(taskRow).join('')}</tbody>
      </table></div>
    </div>`;
  document.getElementById('w-back').onclick = () => { ctx.workerId = null; rerender(); };

  charts.push(makeChart(document.getElementById('w-cpu'), {
    series: [
      line('本进程', times.map((t, i) => [t, sp[i].proc_cpu_bps / 100]), '#4aa8ff'),
      line('机器', times.map((t, i) => [t, sp[i].host_cpu_bps / 100]), '#e8b339'),
    ],
    yAxis: [{ type: 'value', axisLabel: { color: '#7a8a9c', formatter: '{value}%' } }],
  }));
  charts.push(makeChart(document.getElementById('w-mem'), {
    series: [
      line('proc RSS', times.map((t, i) => [t, sp[i].proc_rss_bytes]), '#4aa8ff', 0, true),
      line('host 可用', times.map((t, i) => [t, sp[i].host_mem_avail_bytes]), '#3fb972'),
      line('host 总量', times.map((t, i) => [t, sp[i].host_mem_total_bytes]), '#37424f'),
    ],
    yAxis: [{ type: 'value', axisLabel: { color: '#7a8a9c', formatter: v => fmtBytes(v) } }],
  }));
  charts.push(makeChart(document.getElementById('w-net'), {
    series: [
      line('读 B/s', rateSeries(times, sp.map(x => x.net_read_bytes)), '#6fd3e8'),
      line('写 B/s', rateSeries(times, sp.map(x => x.net_write_bytes)), '#ff9d5c'),
    ],
    yAxis: [{ type: 'value', axisLabel: { color: '#7a8a9c', formatter: v => fmtBytes(v) } }],
  }));
  charts.push(makeChart(document.getElementById('w-load'), {
    series: [line('load1', times.map((t, i) => [t, sp[i].host_load1_x100 / 100]), '#c79bf2', 0, true)],
  }));
}

function taskRow(t) {
  const dur = t.exec_end_ms ? t.exec_end_ms - t.exec_start_ms : null;
  return `<tr>
    <td>${t.task_id}</td><td>${escapeHtml(t.name)}</td>
    <td><span class="badge ${t.status}">${t.status}</span></td>
    <td>${fmtMs(dur)}</td>
    <td>${fmtMs(t.cpu_time_ms)}</td>
    <td>${fmtMs(t.read_time_ms)} / ${fmtMs(t.write_time_ms)}</td>
    <td>${fmtBytes(t.mem_avg_bytes)} / ${fmtBytes(t.mem_peak_bytes)}</td>
  </tr>`;
}

function escapeHtml(s) {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}
