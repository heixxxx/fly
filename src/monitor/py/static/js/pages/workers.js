// Workers：卡片列表 + 详情（四图 + 该 worker 的 task 表）。
// mount 建外层容器；update 按 ctx.workerId 填列表或详情——详情骨架/图表
// 实例只在进入时建一次，后续 update 仅 setOption（缩放/hover 保留）。
import { getJson, fetchSamplesIncremental, fmtGB, fmtBytes, fmtMs, escapeHtml, expandoHtml, statusLabel, evLabel, bindPageJump, getPageSize, setPageSize, PAGE_SIZE_OPTIONS, mappedLabel } from '../api.js';
import { makeChart, line, rateSeries, chartColors, fmtMb, fmtMbPerS, fmtPctVal } from '../charts.js';
import { t } from '../i18n.js';
import { navigate } from '../app.js';

// 详情 task 表每页行数：全局分页大小（翻页替代表内滚动）。

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

  // 子视图切换（列表 ↔ 详情 / 详情换 worker）：销毁旧图表、重建骨架，
  // 翻页状态随 worker 重置。
  if (ctx.workerId !== detailWid) {
    charts.forEach(c => c.dispose());
    charts = [];
    detailBuilt = false;
    detailWid = ctx.workerId;
    ctx.wTaskOffset = 0;
  }

  if (ctx.workerId == null) {
    const data = await getJson('/api/workers');
    if (!data || !document.getElementById('w-body')) return;   // 后者：页面已切走
    document.getElementById('w-body').innerHTML =
      `<div class="worker-cards">${data.workers.map(card).join('')}</div>`;
    document.getElementById('w-body').onclick = (e) => {
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
    stateBadge = `<span class="badge MASTER">${t('evN.MASTER')}</span>`;
  } else if (w.exit_kind) {
    const exited = w.exit_kind === 'EXITED';
    stateBadge = `<span class="badge ${w.exit_kind}" title="${escapeHtml(t(exited ? 'ev.exitedTitle' : 'ev.diedTitle'))}">${t(exited ? 'ev.exited' : 'ev.died')}</span>`;
  } else {
    stateBadge = `<span class="badge ${w.last_event}">${w.last_event ? evLabel(w.last_event) : '-'}</span>`;
  }
  // 角色值映射（compute→计算）；未知角色回退原值。注册侧半可信字符串
  // （attributes/role）一律 escapeHtml 后入 HTML。
  const role = w.role ? escapeHtml(mappedLabel('role.', w.role)) : '-';
  const hostText = `${w.hostname}${w.ip ? ':' + w.ip : ''}`;
  return `<div class="worker-card" data-wid="${w.worker_id}">
    <div class="title">
      <span class="w-name">${isMaster ? 'master' : `worker ${w.worker_id}`}</span>
      <span class="w-host muted" title="${escapeHtml(hostText)}">${escapeHtml(hostText)}</span>
      ${stateBadge}
    </div>
    <div class="row"><span>${t('w.role')}</span><b>${role}</b></div>
    <div class="row"><span>${t('w.procRssCpu')}</span><b>${rss} · ${cpu}</b></div>
    <div class="row"><span>${t('w.hostCpu')}</span><b>${hcpu}</b></div>
    <div class="row"><span>${t('w.attrs')}</span><b>${escapeHtml(w.attributes || '-')}</b></div>
  </div>`;
}

async function fillDetail(body, ctx) {
  const wid = ctx.workerId;
  const off = ctx.wTaskOffset || 0;
  // 样本经增量缓存拉取（每轮只传新增）；task 表分页（20/页）。
  const [samples, tasks] = await Promise.all([
    fetchSamplesIncremental(wid),
    getJson(`/api/tasks?worker=${wid}&limit=${getPageSize()}&offset=${off}`),
  ]);
  if (!document.getElementById('w-title')) return;   // 请求期间页面已切走
  const times = samples.map(x => x.epoch_ms);

  if (!detailBuilt) {
    body.innerHTML = `
      <div class="detail-head">
        <span class="back-link" id="w-back">${t('w.back')}</span>
        <h2 id="w-title"></h2>
      </div>
      <div class="kpi-row" id="w-kpi"></div>
      <div class="grid cols-2">
        <div class="panel"><h3>${t('w.cpu')}</h3><div id="w-cpu" class="chart"></div></div>
        <div class="panel"><h3>${t('w.mem')}</h3><div id="w-mem" class="chart"></div></div>
        <div class="panel"><h3>${t('w.net')}</h3><div id="w-net" class="chart"></div></div>
        <div class="panel"><h3>${t('w.load')}</h3>
        <div class="muted" style="font-size:12px;margin:-6px 0 6px">${t('w.loadHint')}</div><div id="w-load" class="chart"></div></div>
      </div>
      <div class="panel"><h3 id="w-task-title"></h3>
        <div class="table-x"><table>
          <thead><tr><th>${t('tb.id')}</th><th style="width:30%">${t('tb.name')}</th><th>${t('tb.status')}</th><th>${t('tb.duration')}</th><th>${t('tb.cpuCol')}</th><th>${t('tb.rwTime')}</th><th>${t('tb.memAvgPeak')}</th></tr></thead>
          <tbody id="w-tasks"></tbody>
        </table></div>
        <div class="pager">
          <button id="w-prev">${t('t.prev')}</button>
          <span id="w-range"></span>
          <button id="w-next">${t('t.next')}</button>
          <span class="pg-jump">
            <span id="w-pageinfo"></span>
            <input id="w-page" type="number" min="1" title="${t('t.pageTitle')}">
            <button id="w-go">${t('t.jump')}</button>
          </span>
          <span class="pg-size">
            ${t('t.perPage')}
            <select id="w-psize">${PAGE_SIZE_OPTIONS.map(n =>
              `<option value="${n}">${n}</option>`).join('')}</select>
          </span>
        </div>
      </div>`;
    document.getElementById('w-back').onclick = () => { ctx.workerId = null; navigate(); };
    document.getElementById('w-prev').onclick = () => {
      ctx.wTaskOffset = Math.max(0, (ctx.wTaskOffset || 0) - getPageSize());
      navigate({ keepScroll: true });   // 详情页翻页：留在 task 表位置
    };
    document.getElementById('w-next').onclick = () => {
      ctx.wTaskOffset = (ctx.wTaskOffset || 0) + getPageSize();
      navigate({ keepScroll: true });
    };
    // 跳页：与翻页一致保留滚动位置（表格在图表区下方）。
    bindPageJump(
      document.getElementById('w-page'), document.getElementById('w-go'),
      () => Math.max(1, Math.ceil((ctx.wTotal || 0) / getPageSize())),
      p => { ctx.wTaskOffset = (p - 1) * getPageSize(); navigate({ keepScroll: true }); });
    // 每页条数（全局设置）：变更后回第一页。
    const wpsize = document.getElementById('w-psize');
    wpsize.value = getPageSize();
    wpsize.onchange = e => {
      setPageSize(e.target.value);
      ctx.wTaskOffset = 0;
      navigate({ keepScroll: true });
    };
    charts = [
      makeChart(document.getElementById('w-cpu'), {}),
      makeChart(document.getElementById('w-mem'), {}),
      makeChart(document.getElementById('w-net'), {}),
      makeChart(document.getElementById('w-load'), {}),
    ];
    detailBuilt = true;
  }

  document.getElementById('w-title').textContent =
    wid === 0 ? 'master' : `worker ${wid}`;
  document.getElementById('w-kpi').innerHTML = `
    <div class="kpi"><div class="label">${t('w.latestRss')}</div><div class="value">${samples.length ? fmtGB(samples[samples.length - 1].proc_rss_bytes) : '-'}</div></div>
    <div class="kpi"><div class="label">${t('w.latestCpu')}</div><div class="value">${samples.length ? (samples[samples.length - 1].proc_cpu_bps / 100).toFixed(1) + '%' : '-'}</div></div>
    <div class="kpi"><div class="label">${t('w.netTotal')}</div><div class="value" style="font-size:15px; line-height:34px">${samples.length ? fmtBytes(samples[samples.length - 1].net_read_bytes) + ' / ' + fmtBytes(samples[samples.length - 1].net_write_bytes) : '-'}</div></div>`;

  const c = chartColors();
  charts[0].setOption({
    tooltip: { valueFormatter: fmtPctVal },
    series: [
      line(t('ser.procCpu'), times.map((tm, i) => [tm, samples[i].proc_cpu_bps / 100]), c.blue),
      line(t('ser.hostCpu'), times.map((tm, i) => [tm, samples[i].host_cpu_bps / 100]), c.yellow),
    ],
    yAxis: [{ type: 'value', axisLabel: { color: c.label, formatter: '{value}%' } }],
  });
  charts[1].setOption({
    // 双 y 轴：进程与机器内存差两个数量级，单轴会让进程 RSS 贴底不可读；
    // 右侧标签（"65 GB"）需要额外空间，全局 grid 的 right:22 会裁掉。
    grid: { right: 66 },
    tooltip: { valueFormatter: fmtMb },
    series: [
      line(t('ser.procRss'), times.map((tm, i) => [tm, samples[i].proc_rss_bytes]), c.blue, 0, true),
      line(t('ser.hostAvail'), times.map((tm, i) => [tm, samples[i].host_mem_avail_bytes]), c.green, 1),
      // 总量是参考线：亮/中灰虚线（深浅主题下均与背景拉开对比度）。
      { name: t('ser.hostTotal'), type: 'line', yAxisIndex: 1,
        data: times.map((tm, i) => [tm, samples[i].host_mem_total_bytes]),
        showSymbol: false, lineStyle: { width: 1.5, color: c.hostTotal, type: 'dashed' },
        itemStyle: { color: c.hostTotal } },
    ],
    yAxis: [
      { type: 'value', axisLine: { show: false },
        axisLabel: { color: c.label, formatter: v => fmtGB(v) },
        splitLine: { show: true, lineStyle: { color: c.split } } },
      { type: 'value', axisLine: { show: false },
        axisLabel: { color: c.label, formatter: v => fmtGB(v) },
        splitLine: { show: false } },
    ],
  });
  charts[2].setOption({
    tooltip: { valueFormatter: fmtMbPerS },
    series: [
      line(t('ser.read'), rateSeries(times, samples.map(x => x.net_read_bytes)), c.cyan),
      line(t('ser.write'), rateSeries(times, samples.map(x => x.net_write_bytes)), c.orange),
    ],
    yAxis: [{ type: 'value', axisLabel: { color: c.label, formatter: v => fmtBytes(v) } }],
  });
  charts[3].setOption({
    tooltip: { valueFormatter: v => (+v).toFixed(2) },
    series: [line(t('ser.load1'), times.map((tm, i) => [tm, samples[i].host_load1_x100 / 100]), c.purple, 0, true)],
  });

  document.getElementById('w-task-title').textContent = wid === 0
    ? t('w.masterTasksOf', tasks ? tasks.total : 0)
    : t('w.tasksOf', wid, tasks ? tasks.total : 0);
  document.getElementById('w-tasks').innerHTML =
    (tasks ? tasks.tasks : []).map(taskRow).join('');
  const total = tasks ? tasks.total : 0;
  ctx.wTotal = total;
  const ps = getPageSize();
  const pages = Math.max(1, Math.ceil(total / ps));
  const page = Math.floor(off / ps) + 1;
  document.getElementById('w-pageinfo').textContent = t('t.pageOf', page, pages);
  const pageInput = document.getElementById('w-page');
  pageInput.max = pages;
  if (document.activeElement !== pageInput) pageInput.value = page;
  const end = Math.min(off + ps, total);
  document.getElementById('w-range').innerHTML =
    `${total ? off + 1 : 0}–${end} <span class="pg-total">${t('t.totalN', total)}</span>`;
  document.getElementById('w-prev').disabled = off === 0;
  document.getElementById('w-next').disabled = off + ps >= total;
}

function taskRow(tk) {
  const dur = tk.exec_end_ms ? tk.exec_end_ms - tk.exec_start_ms : null;
  return `<tr>
    <td>${tk.task_id}</td><td>${expandoHtml(tk.name)}</td>
    <td><span class="badge ${tk.status}">${statusLabel(tk.status)}</span></td>
    <td>${fmtMs(dur)}</td>
    <td>${fmtMs(tk.cpu_time_ms)}</td>
    <td>${fmtMs(tk.read_time_ms)} / ${fmtMs(tk.write_time_ms)}</td>
    <td>${fmtGB(tk.mem_avg_bytes)} / ${fmtGB(tk.mem_peak_bytes)}</td>
  </tr>`;
}

