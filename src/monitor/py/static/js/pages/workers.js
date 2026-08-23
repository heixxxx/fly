// Workers：卡片列表 + 详情（四图 + 该 worker 的 task 表）。
// mount 建外层容器；update 按 ctx.workerId 填列表或详情——详情骨架/图表
// 实例只在进入时建一次，后续 update 仅 setOption（缩放/hover 保留）。
import { getJson, fetchSamplesIncremental, fmtGB, fmtBytes, fmtMs, escapeHtml, expandoHtml } from '../api.js';
import { makeChart, line, rateSeries, chartColors } from '../charts.js';
import { t } from '../i18n.js';
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
  await fillDetail(body, ctx);
}

function card(w) {
  const l = w.latest;
  const rss = l ? fmtGB(l.proc_rss_bytes) : '-';
  const cpu = l ? (l.proc_cpu_bps / 100).toFixed(1) + '%' : '-';
  const hcpu = l ? (l.host_cpu_bps / 100).toFixed(1) + '%' : '-';
  const isMaster = w.worker_id === 0;
  // 终态语义：EXITED（正常退出，绿）/ DEAD（异常死亡，红）；其余状态原样。
  // master（wid=0）无注册/退出生命周期，恒显示 MASTER（用户裁定：master
  // 可出现在 Workers 列表，但不可描述为 worker 0）。
  let stateBadge;
  if (isMaster) {
    stateBadge = `<span class="badge MASTER">MASTER</span>`;
  } else if (w.exit_kind) {
    const exited = w.exit_kind === 'EXITED';
    stateBadge = `<span class="badge ${w.exit_kind}" title="${escapeHtml(t(exited ? 'ev.exitedTitle' : 'ev.diedTitle'))}">${t(exited ? 'ev.exited' : 'ev.died')}</span>`;
  } else {
    stateBadge = `<span class="badge ${w.last_event}">${w.last_event || '-'}</span>`;
  }
  const hostText = `${w.hostname}${w.ip ? ':' + w.ip : ''}`;
  return `<div class="worker-card" data-wid="${w.worker_id}">
    <div class="title">
      <span class="w-name">${isMaster ? 'master' : `worker ${w.worker_id}`}</span>
      <span class="w-host muted" title="${escapeHtml(hostText)}">${escapeHtml(hostText)}</span>
      ${stateBadge}
    </div>
    <div class="row"><span>${t('w.role')}</span><b>${w.role || '-'}</b></div>
    <div class="row"><span>${t('w.procRssCpu')}</span><b>${rss} · ${cpu}</b></div>
    <div class="row"><span>${t('w.hostCpu')}</span><b>${hcpu}</b></div>
    <div class="row"><span>${t('w.attrs')}</span><b>${w.attributes || '-'}</b></div>
  </div>`;
}

async function fillDetail(body, ctx) {
  const wid = ctx.workerId;
  // 样本经增量缓存拉取（每轮只传新增）。
  const [samples, tasks] = await Promise.all([
    fetchSamplesIncremental(wid),
    getJson(`/api/tasks?worker=${wid}&limit=300`),
  ]);
  const sp = samples;
  const times = sp.map(x => x.epoch_ms);

  if (!detailBuilt) {
    body.innerHTML = `
      <span class="back-link" id="w-back">${t('w.back')}</span>
      <div class="kpi-row" id="w-kpi"></div>
      <div class="grid cols-2">
        <div class="panel"><h3>${t('w.cpu')}</h3><div id="w-cpu" class="chart"></div></div>
        <div class="panel"><h3>${t('w.mem')}</h3><div id="w-mem" class="chart"></div></div>
        <div class="panel"><h3>${t('w.net')}</h3><div id="w-net" class="chart"></div></div>
        <div class="panel"><h3>${t('w.load')}</h3>
        <div class="muted" style="font-size:12px;margin:-6px 0 6px">${t('w.loadHint')}</div><div id="w-load" class="chart"></div></div>
      </div>
      <div class="panel"><h3 id="w-task-title"></h3>
        <div class="table-wrap"><table>
          <thead><tr><th>${t('tb.id')}</th><th>${t('tb.name')}</th><th>${t('tb.status')}</th><th>${t('tb.duration')}</th><th>${t('tb.cpuCol')}</th><th>${t('tb.rwTime')}</th><th>${t('tb.memAvgPeak')}</th></tr></thead>
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
    <div class="kpi"><div class="label">${t('ov.samples')}</div><div class="value">${sp.length}</div></div>
    <div class="kpi"><div class="label">${t('w.latestRss')}</div><div class="value">${sp.length ? fmtGB(sp[sp.length - 1].proc_rss_bytes) : '-'}</div></div>
    <div class="kpi"><div class="label">${t('w.latestCpu')}</div><div class="value">${sp.length ? (sp[sp.length - 1].proc_cpu_bps / 100).toFixed(1) + '%' : '-'}</div></div>
    <div class="kpi"><div class="label">${t('w.netTotal')}</div><div class="value" style="font-size:15px">${sp.length ? fmtBytes(sp[sp.length - 1].net_read_bytes) + ' / ' + fmtBytes(sp[sp.length - 1].net_write_bytes) : '-'}</div></div>`;

  const c = chartColors();
  charts[0].setOption({
    series: [
      line('Proc CPU', times.map((tm, i) => [tm, sp[i].proc_cpu_bps / 100]), c.blue),
      line('Host CPU', times.map((tm, i) => [tm, sp[i].host_cpu_bps / 100]), c.yellow),
    ],
    yAxis: [{ type: 'value', axisLabel: { color: c.label, formatter: '{value}%' } }],
  });
  charts[1].setOption({
    series: [
      line('Proc RSS', times.map((tm, i) => [tm, sp[i].proc_rss_bytes]), c.blue, 0, true),
      line('Host Available', times.map((tm, i) => [tm, sp[i].host_mem_avail_bytes]), c.green),
      // 总量是参考线：亮/中灰虚线（深浅主题下均与背景拉开对比度）。
      { name: 'Host Total', type: 'line',
        data: times.map((tm, i) => [tm, sp[i].host_mem_total_bytes]),
        showSymbol: false, lineStyle: { width: 1.5, color: c.hostTotal, type: 'dashed' },
        itemStyle: { color: c.hostTotal } },
    ],
    yAxis: [{ type: 'value', axisLabel: { color: c.label, formatter: v => fmtGB(v) } }],
  });
  charts[2].setOption({
    series: [
      line('Read', rateSeries(times, sp.map(x => x.net_read_bytes)), c.cyan),
      line('Write', rateSeries(times, sp.map(x => x.net_write_bytes)), c.orange),
    ],
    yAxis: [{ type: 'value', axisLabel: { color: c.label, formatter: v => fmtBytes(v) } }],
  });
  charts[3].setOption({
    series: [line('Load1', times.map((tm, i) => [tm, sp[i].host_load1_x100 / 100]), c.purple, 0, true)],
  });

  document.getElementById('w-task-title').textContent = wid === 0
    ? t('w.masterTasksOf', tasks ? tasks.total : 0)
    : t('w.tasksOf', wid, tasks ? tasks.total : 0);
  document.getElementById('w-tasks').innerHTML =
    (tasks ? tasks.tasks : []).map(taskRow).join('');
}

function taskRow(tk) {
  const dur = tk.exec_end_ms ? tk.exec_end_ms - tk.exec_start_ms : null;
  return `<tr>
    <td>${tk.task_id}</td><td>${expandoHtml(tk.name)}</td>
    <td><span class="badge ${tk.status}">${tk.status}</span></td>
    <td>${fmtMs(dur)}</td>
    <td>${fmtMs(tk.cpu_time_ms)}</td>
    <td>${fmtMs(tk.read_time_ms)} / ${fmtMs(tk.write_time_ms)}</td>
    <td>${fmtGB(tk.mem_avg_bytes)} / ${fmtGB(tk.mem_peak_bytes)}</td>
  </tr>`;
}

