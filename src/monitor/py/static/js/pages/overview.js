// Overview：run 概况 KPI + 集群聚合曲线 + 统一事件流。
import { getJson, fmtBytes, fmtTime, fmtTimeFull } from '../api.js';
import { makeChart, line } from '../charts.js';

let charts = [];

export function destroy() {
  charts.forEach(c => c.dispose());
  charts = [];
}

export async function render(ctx) {
  const main = ctx.main;
  const [meta, events, workers] = await Promise.all([
    getJson('/api/meta'),
    getJson('/api/events?limit=30'),
    getJson('/api/workers'),
  ]);
  if (!meta) { main.innerHTML = '<div class="panel">API 不可达（master 未启动或 db 无数据）</div>'; return; }

  const m = meta.meta || {};
  const durS = m.run_start_ms && m.run_end_ms ? ((+m.run_end_ms - +m.run_start_ms) / 1000).toFixed(1) + ' s' : '-';
  const tc = meta.task_counts || {};
  const total = Object.values(tc).reduce((a, b) => a + b, 0);

  main.innerHTML = `
    <div class="kpi-row">
      <div class="kpi"><div class="label">运行时长</div><div class="value">${durS}</div><div class="sub">${m.hostname || ''}</div></div>
      <div class="kpi"><div class="label">Tasks 总数</div><div class="value">${total}</div><div class="sub">完成 ${tc.COMPLETED || 0} · 失败 ${tc.FAILED || 0} · 运行 ${tc.RUNNING || 0}</div></div>
      <div class="kpi"><div class="label">Workers</div><div class="value">${meta.workers}</div><div class="sub">含 master 自监控(wid=0)</div></div>
      <div class="kpi"><div class="label">样本区间</div><div class="value" style="font-size:15px">${fmtTime(meta.sample_lo)} → ${fmtTime(meta.sample_hi)}</div><div class="sub">monitor.db</div></div>
    </div>
    <div class="grid cols-2">
      <div class="panel"><h3>集群聚合进程 RSS</h3><div id="ov-rss" class="chart"></div></div>
      <div class="panel"><h3>集群聚合 CPU%（进程 / 机器）</h3><div id="ov-cpu" class="chart"></div></div>
    </div>
    <div class="panel"><h3>最近事件</h3><div class="table-wrap"><table>
      <thead><tr><th>时间</th><th>类别</th><th>事件</th><th>worker</th><th>task</th><th>详情</th></tr></thead>
      <tbody>${(events ? events.events : []).map(evRow).join('')}</tbody>
    </table></div></div>
  `;

  // 集群聚合：逐 epoch 汇总各 worker 的 RSS/CPU。
  const byEpoch = new Map();
  for (const w of (workers ? workers.workers : [])) {
    const s = await getJson(`/api/workers/${w.worker_id}/samples`);
    if (!s) continue;
    for (const sp of s.samples) {
      let acc = byEpoch.get(sp.epoch_ms);
      if (!acc) { acc = { rss: 0, pcpu: 0, hcpu: null }; byEpoch.set(sp.epoch_ms, acc); }
      acc.rss += sp.proc_rss_bytes;
      acc.pcpu += sp.proc_cpu_bps / 100;
      // host 指标各 worker 同机重复（单机多 worker）——取 max 更真实。
      const h = sp.host_cpu_bps / 100;
      acc.hcpu = acc.hcpu == null ? h : Math.max(acc.hcpu, h);
    }
  }
  const times = [...byEpoch.keys()].sort((a, b) => a - b);
  charts.push(makeChart(document.getElementById('ov-rss'), {
    series: [line('Σ proc RSS', times.map(t => [t, byEpoch.get(t).rss]), '#4aa8ff', 0, true)],
    yAxis: [{ type: 'value', axisLabel: { color: '#7a8a9c', formatter: v => fmtBytes(v) } }],
  }));
  charts.push(makeChart(document.getElementById('ov-cpu'), {
    series: [
      line('Σ 进程 CPU%', times.map(t => [t, +byEpoch.get(t).pcpu.toFixed(1)]), '#4aa8ff'),
      line('机器 CPU% (max)', times.map(t => [t, byEpoch.get(t).hcpu ?? 0]), '#e8b339'),
    ],
    yAxis: [{ type: 'value', axisLabel: { color: '#7a8a9c', formatter: '{value}%' } }],
  }));
  charts = charts.filter(c => !c.isDisposed());
}

function evRow(e) {
  return `<tr>
    <td class="mono">${fmtTimeFull(e.epoch_ms)}</td>
    <td class="cat-${e.category}">${e.category}</td>
    <td><span class="badge ${e.event}">${e.event}</span></td>
    <td>${e.worker_id || '-'}</td>
    <td>${e.task_id || '-'}</td>
    <td class="muted mono">${escapeHtml(String(e.detail || '')).slice(0, 80)}</td>
  </tr>`;
}

function escapeHtml(s) {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}
