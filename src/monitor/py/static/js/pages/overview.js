// Overview：run 概况 KPI + 集群聚合曲线（RSS/CPU/网络速率）+ 磁盘 IO 占位 + 事件流。
//
// 聚合口径（对齐 bug 根治）：各 worker 的样本 epoch 是独立毫秒时间戳，
// 按精确毫秒分组几乎永不重合——旧实现"Σ"曲线每个点只含单个 worker
// （实测 ΣRSS 峰值=单 worker 峰值）。现按 1s 时间桶聚合：桶内每 worker
// 取最新样本（事件+周期混合密度下同 worker 可能多条），跨 host 求和
// （多机时进程 Σ 可 >100%，机器取各 host 最大值；单机即全部 fly 进程合计，
// 与机器 CPU 直接可比）。
// mount 建骨架与图表实例；update 仅 setOption/innerHTML（缩放/hover 保留）。
import { getJson, fetchSamplesIncremental, fmtGB, fmtBytes, fmtTime, fmtTimeFull, escapeHtml } from '../api.js';
import { makeChart, line } from '../charts.js';

let charts = [];

export function destroy() {
  charts.forEach(c => c.dispose());
  charts = [];
}

export function mount(ctx) {
  ctx.main.innerHTML = `
    <div class="kpi-row" id="ov-kpi"></div>
    <div class="grid cols-2">
      <div class="panel"><h3>集群聚合 RSS</h3><div id="ov-rss" class="chart"></div></div>
      <div class="panel"><h3>集群聚合 CPU（进程 vs 机器）</h3><div id="ov-cpu" class="chart"></div></div>
      <div class="panel"><h3>集群聚合网络 IO 速率（读/写）</h3><div id="ov-net" class="chart"></div></div>
      <div class="panel"><h3>磁盘 IO 速率</h3>
        <div class="empty-chart">磁盘 IO 监控暂未支持（待后续增强）</div>
      </div>
    </div>
    <div class="panel"><h3>最近事件</h3><div class="table-wrap"><table>
      <thead><tr><th>时间</th><th>类别</th><th>事件</th><th>worker</th><th>task</th><th>详情</th></tr></thead>
      <tbody id="ov-events"></tbody>
    </table></div></div>`;
  charts = [
    makeChart(document.getElementById('ov-rss'), {}),
    makeChart(document.getElementById('ov-cpu'), {}),
    makeChart(document.getElementById('ov-net'), {}),
  ];
}

export async function update(ctx) {
  const [meta, events, workers] = await Promise.all([
    getJson('/api/meta'),
    getJson('/api/events?limit=30'),
    getJson('/api/workers'),
  ]);
  if (!meta) return;

  const m = meta.meta || {};
  // 运行中实时计时（run_end 未落盘时用当前时刻），结束后为终值。
  const endTime = m.run_end_ms ? +m.run_end_ms : Date.now();
  const durS = m.run_start_ms
    ? ((endTime - +m.run_start_ms) / 1000).toFixed(1) + ' s' : '-';
  const tc = meta.task_counts || {};
  const total = Object.values(tc).reduce((a, b) => a + b, 0);

  document.getElementById('ov-kpi').innerHTML = `
    <div class="kpi"><div class="label">运行时长${m.run_end_ms ? '' : '（进行中）'}</div><div class="value">${durS}</div><div class="sub">${m.hostname || ''} · ${fmtTime(+m.run_start_ms)} → ${m.run_end_ms ? fmtTime(+m.run_end_ms) : '现在'}</div></div>
    <div class="kpi"><div class="label">Tasks 总数</div><div class="value">${total}</div><div class="sub">完成 ${tc.COMPLETED || 0} · 失败 ${tc.FAILED || 0} · 运行 ${tc.RUNNING || 0}</div></div>
    <div class="kpi"><div class="label">Workers</div><div class="value">${meta.workers}</div></div>
    <div class="kpi"><div class="label">样本数</div><div class="value" style="font-size:16px">${meta.sample_lo ? '采样中' : '-'}</div><div class="sub">${fmtTime(meta.sample_lo)} → ${fmtTime(meta.sample_hi)}</div></div>`;

  // ---- 1s 桶聚合（含 master wid=0 的样本——同样是 fly 进程负载）----
  // 样本经增量缓存拉取（每轮只传新增，见 fetchSamplesIncremental）。
  // 前值填充（forward-fill）：桶内无样本的 worker 用其 ≤ 该桶的最后已知
  // 样本参与聚合——否则末尾桶只含已上报的部分 worker（10s 成组上报有
  // 延迟），Σ 曲线尾部塌陷（与 RunMetrics 最近邻合成同理）。
  const seenWorkers = new Set();
  for (const w of (workers ? workers.workers : [])) seenWorkers.add(w.worker_id);
  const buckets = new Map();   // bucketSec → { wid → latest sample }
  const latest = new Map();    // wid → 该桶时刻的最后已知样本（跨桶携带）
  for (const w of (workers ? workers.workers : [])) {
    const samples = await fetchSamplesIncremental(w.worker_id);
    for (const sp of samples) {
      const k = Math.floor(sp.epoch_ms / 1000);
      let b = buckets.get(k);
      if (!b) { b = {}; buckets.set(k, b); }
      b[w.worker_id] = sp;  // 同桶多条取最新（时间升序遍历天然覆盖）
    }
  }
  const series = [];  // {t, rss, pcpu, hcpu, nr, nw}
  for (const k of [...buckets.keys()].sort((a, b) => a - b)) {
    const b = buckets.get(k);
    for (const [wid, sp] of Object.entries(b)) latest.set(+wid, sp);
    let rss = 0, pcpu = 0, hcpu = null, nr = 0, nw = 0;
    for (const wid of seenWorkers) {
      const sp = b[wid] || latest.get(wid);   // 桶内或前值
      if (!sp) continue;                      // 该 worker 尚无任何样本
      rss += sp.proc_rss_bytes;
      pcpu += sp.proc_cpu_bps / 100;
      const h = sp.host_cpu_bps / 100;
      hcpu = hcpu == null ? h : Math.max(hcpu, h);  // 单机多 worker 同值；多机取最热
      nr += sp.net_read_bytes;
      nw += sp.net_write_bytes;
    }
    series.push({ t: k * 1000, rss, pcpu, hcpu, nr, nw });
  }
  // 网络速率：累计值（桶内已 Σ）在相邻桶间差分（计数器回绕/重启记 0）。
  const netR = [], netW = [];
  for (let i = 1; i < series.length; i++) {
    const dt = (series[i].t - series[i - 1].t) / 1000;
    if (dt <= 0) continue;
    const dr = series[i].nr - series[i - 1].nr;
    const dw = series[i].nw - series[i - 1].nw;
    netR.push([series[i].t, dr >= 0 ? +(dr / dt).toFixed(1) : 0]);
    netW.push([series[i].t, dw >= 0 ? +(dw / dt).toFixed(1) : 0]);
  }

  charts[0].setOption({
    yAxis: [{ type: 'value', axisLabel: { color: '#7a8a9c', formatter: v => fmtGB(v) } }],
    series: [line('Total Proc RSS', series.map(s => [s.t, s.rss]), '#4aa8ff', 0, true)],
  });
  charts[1].setOption({
    yAxis: [{ type: 'value', axisLabel: { color: '#7a8a9c', formatter: '{value}%' } }],
    series: [
      line('Total Proc CPU', series.map(s => [s.t, +s.pcpu.toFixed(1)]), '#4aa8ff'),
      line('Host CPU', series.map(s => [s.t, s.hcpu ?? 0]), '#e8b339'),
    ],
  });
  charts[2].setOption({
    yAxis: [{ type: 'value', axisLabel: { color: '#7a8a9c', formatter: v => fmtBytes(v) + '/s' } }],
    series: [
      line('Read', netR, '#6fd3e8'),
      line('Write', netW, '#ff9d5c'),
    ],
  });

  document.getElementById('ov-events').innerHTML =
    (events ? events.events : []).map(evRow).join('');
}

function evRow(e) {
  // worker DEAD：按关停指令推导显示「正常退出」（绿）或「异常死亡」（红）。
  let badgeEvent = e.event;
  let badgeText = e.event;
  let badgeTitle = '';
  if (e.category === 'worker' && e.event === 'DEAD' && e.exit_kind) {
    badgeEvent = e.exit_kind;
    badgeText = e.exit_kind === 'EXITED' ? '正常退出' : '异常死亡';
    badgeTitle = e.exit_kind === 'EXITED'
      ? '收到关停指令后正常退出' : '无关停指令先行：心跳超时/宽限耗尽判死';
  }
  return `<tr>
    <td class="mono">${fmtTimeFull(e.epoch_ms)}</td>
    <td class="cat-${e.category}">${e.category}</td>
    <td><span class="badge ${badgeEvent}" ${badgeTitle ? `title="${badgeTitle}"` : ''}>${badgeText}</span></td>
    <td>${e.worker_id || '-'}</td>
    <td>${e.task_id || '-'}</td>
    <td class="muted mono">${escapeHtml(String(e.detail || '')).slice(0, 80)}</td>
  </tr>`;
}
