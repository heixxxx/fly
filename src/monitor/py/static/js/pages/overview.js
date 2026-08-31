// Overview：run 概况 KPI + 集群聚合曲线（RSS/CPU/网络速率）+ 磁盘 IO 占位 + 事件流。
//
// 聚合口径（对齐 bug 根治）：各 worker 的样本 epoch 是独立毫秒时间戳，
// 按精确毫秒分组几乎永不重合——旧实现"Σ"曲线每个点只含单个 worker
// （实测 ΣRSS 峰值=单 worker 峰值）。现按 1s 时间桶聚合：桶内每 worker
// 取最新样本（事件+周期混合密度下同 worker 可能多条），跨 host 求和
// （多机时进程 Σ 可 >100%，机器取各 host 最大值；单机即全部 fly 进程合计，
// 与机器 CPU 直接可比）。
// mount 建骨架与图表实例；update 仅 setOption/innerHTML（缩放/hover 保留）。
import { getJson, fetchAllSamplesIncremental, fmtGB, fmtBytes, fmtTime, fmtTimeFull, escapeHtml, catLabel, evLabel } from '../api.js';
import { makeChart, line, chartColors, fmtMb, fmtMbPerS, fmtPctVal } from '../charts.js';
import { t } from '../i18n.js';

let charts = [];

export function destroy() {
  charts.forEach(c => c.dispose());
  charts = [];
}

export function mount(ctx) {
  ctx.main.innerHTML = `
    <div class="kpi-row" id="ov-kpi"></div>
    <div class="grid cols-2">
      <div class="panel"><h3>${t('ov.rss')}</h3><div id="ov-rss" class="chart"></div></div>
      <div class="panel"><h3>${t('ov.cpu')}</h3><div id="ov-cpu" class="chart"></div></div>
      <div class="panel"><h3>${t('ov.net')}</h3><div id="ov-net" class="chart"></div></div>
      <div class="panel"><h3>${t('ov.disk')}</h3>
        <div class="empty-chart">${t('ov.diskPending')}</div>
      </div>
    </div>
    <div class="panel"><h3>${t('ov.recentEvents')}</h3><div style="max-height:360px; overflow:auto"><table>
      <thead><tr><th>${t('tb.time')}</th><th>${t('tb.category')}</th><th>${t('tb.event')}</th><th>${t('tb.worker')}</th><th>${t('tb.taskCol')}</th><th>${t('tb.detail')}</th></tr></thead>
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
  if (!meta || !document.getElementById('ov-kpi')) return;   // 后者：页面已切走

  const m = meta.meta || {};
  // 运行中实时计时（run_end 未落盘时用当前时刻），结束后为终值。
  const endTime = m.run_end_ms ? +m.run_end_ms : Date.now();
  const durS = m.run_start_ms
    ? ((endTime - +m.run_start_ms) / 1000).toFixed(1) + ' s' : '-';
  const tc = meta.task_counts || {};
  const total = Object.values(tc).reduce((a, b) => a + b, 0);

  const hosts = new Set((workers ? workers.workers : [])
    .map(w => w.hostname).filter(Boolean)).size;
  document.getElementById('ov-kpi').innerHTML = `
    <div class="kpi"><div class="label">${t(m.run_end_ms ? 'ov.duration' : 'ov.durationRunning')}</div><div class="value">${durS}</div><div class="sub">${m.hostname || ''} · ${fmtTime(+m.run_start_ms)} → ${m.run_end_ms ? fmtTime(+m.run_end_ms) : t('app.now')}</div></div>
    <div class="kpi"><div class="label">${t('ov.tasksTotal')}</div><div class="value">${total}</div><div class="sub">${t('ov.subDone')} <span class="c-ok">${tc.COMPLETED || 0}</span> · ${t('ov.subFail')} <span class="c-fail">${tc.FAILED || 0}</span> · ${t('ov.subRun')} <span class="c-run">${tc.RUNNING || 0}</span></div></div>
    <div class="kpi"><div class="label">${t('ov.workersLabel')}</div><div class="value">${meta.workers}</div></div>
    <div class="kpi"><div class="label">${t('ov.hostsLabel')}</div><div class="value">${hosts}</div></div>`;

  // ---- 1s 桶聚合（含 master wid=0 的样本——同样是 fly 进程负载）----
  // 样本经批量增量缓存拉取（全部 worker 一次请求，每轮只传新增；
  // fetchAllSamplesIncremental 返回 Map wid → 累计样本数组）。
  const seenWorkers = new Set();
  for (const w of (workers ? workers.workers : [])) seenWorkers.add(w.worker_id);
  const widList = [...seenWorkers];
  const samplesByWid = await fetchAllSamplesIncremental(widList);
  const buckets = new Map();   // bucketSec → { wid → latest sample }
  const latest = new Map();    // wid → 该桶时刻的最后已知样本（跨桶携带）
  for (const wid of widList) {
    for (const sp of (samplesByWid.get(wid) || [])) {
      const k = Math.floor(sp.epoch_ms / 1000);
      let b = buckets.get(k);
      if (!b) { b = {}; buckets.set(k, b); }
      b[wid] = sp;  // 同桶多条取最新（时间升序遍历天然覆盖）
    }
  }
  // 前值填充（forward-fill）：桶内无样本的 worker 用其 ≤ 该桶的最后已知
  // 样本参与聚合——否则末尾桶只含已上报的部分 worker（10s 成组上报有
  // 延迟），Σ 曲线尾部塌陷（与 RunMetrics 最近邻合成同理）。
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

  const c = chartColors();
  const withFmt = (f) => ({ valueFormatter: f });
  charts[0].setOption({
    tooltip: withFmt(fmtMb),
    yAxis: [{ type: 'value', axisLabel: { color: c.label, formatter: v => fmtGB(v) } }],
    series: [line(t('ser.totalRss'), series.map(s => [s.t, s.rss]), c.blue, 0, true)],
  });
  charts[1].setOption({
    tooltip: withFmt(fmtPctVal),
    yAxis: [{ type: 'value', axisLabel: { color: c.label, formatter: '{value}%' } }],
    series: [
      line(t('ser.totalCpu'), series.map(s => [s.t, +s.pcpu.toFixed(1)]), c.blue),
      line(t('ser.hostCpu'), series.map(s => [s.t, s.hcpu ?? 0]), c.yellow),
    ],
  });
  charts[2].setOption({
    tooltip: withFmt(fmtMbPerS),
    yAxis: [{ type: 'value', axisLabel: { color: c.label, formatter: v => fmtBytes(v) + '/s' } }],
    series: [
      line(t('ser.read'), netR, c.cyan),
      line(t('ser.write'), netW, c.orange),
    ],
  });

  document.getElementById('ov-events').innerHTML =
    (events ? events.events : []).map(evRow).join('');
}

function evRow(e) {
  // worker DEAD：按关停指令推导显示「正常退出」（绿）或「异常死亡」（红）。
  let badgeEvent = e.event;
  let badgeText = evLabel(e.event);
  let badgeTitle = '';
  if (e.category === 'worker' && e.event === 'DEAD' && e.exit_kind) {
    badgeEvent = e.exit_kind;
    const exited = e.exit_kind === 'EXITED';
    badgeText = t(exited ? 'ev.exited' : 'ev.died');
    badgeTitle = t(exited ? 'ev.exitedTitle' : 'ev.diedTitle');
  }
  return `<tr>
    <td class="mono">${fmtTimeFull(e.epoch_ms)}</td>
    <td class="cat-${e.category}">${catLabel(e.category)}</td>
    <td><span class="badge ${badgeEvent}" ${badgeTitle ? `title="${badgeTitle}"` : ''}>${badgeText}</span></td>
    <td>${e.worker_id || '-'}</td>
    <td>${e.task_id || '-'}</td>
    <td class="muted mono">${escapeHtml(String(e.detail || '').slice(0, 80))}</td>
  </tr>`;
}
