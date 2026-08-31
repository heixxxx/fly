// Tasks：搜索/过滤/分页的任务表 + task 详情。
// mount 建列表/详情双容器（控件事件绑一次，切换仅显隐——详情不得覆盖
// main.innerHTML，否则返回时列表结构与绑定已销毁、update 写不回）。
// update 仅填数据——过滤条件与分页状态存活于 ctx.taskFilter。
import { getJson, fmtGB, fmtBytes, fmtMs, fmtPct, displayModule, fmtTime, fmtTimeFull, escapeHtml, shortName, expandoHtml, errorBriefHtml, statusLabel, evLabel, bindPageJump, getPageSize, setPageSize, PAGE_SIZE_OPTIONS } from '../api.js';
import { createTopFloat } from '../floatbar.js';
import { t } from '../i18n.js';
import { navigate } from '../app.js';

// 每页行数：用户可调（全局设置，localStorage 持久化），限制单页数量、
// 翻页查看，不用表内滚动。
const pageSize = () => getPageSize();

// 详情按需重建：同一 task 且已终态 → 跳过重拉重渲（保住展开的名称/驻留
// 的错误信息等交互状态）；RUNNING task 每轮刷新直至终态。语言/主题切换
// 走整页重建（mount 重置），详情模板文案随新语言。
const TERMINAL_STATUS = new Set(['COMPLETED', 'FAILED', 'CANCELLED']);
let renderedDetail = null;   // { tid, terminal } | null
let tFloat = null;           // 筛选栏智能浮窗（createTopFloat 句柄）
let syncTControlsFloat = null;   // 列表显隐切换后手动重估浮窗状态

export function destroy() {
  renderedDetail = null;
  if (tFloat) { tFloat.destroy(); tFloat = null; }
  syncTControlsFloat = null;
}

export function mount(ctx) {
  renderedDetail = null;
  const f = ctx.taskFilter;
  if (!f.sort) f.sort = { key: 'id', desc: true };   // 默认：ID 降序（现状序）
  ctx.main.innerHTML = `
    <div id="t-list-view">
      <div class="panel">
        <div class="controls" id="t-controls-wrap">
          <input id="t-q" placeholder="${t('t.searchPh')}" value="${escapeHtml(f.q || '')}" style="width:230px">
          <select id="t-status">
            <option value="">${t('t.allStatus')}</option>
            ${['PENDING', 'RUNNING', 'COMPLETED', 'FAILED', 'CANCELLED'].map(s =>
              `<option value="${s}" ${f.status === s ? 'selected' : ''}>${s}</option>`).join('')}
          </select>
          <select id="t-worker"><option value="">${t('t.allWorkers')}</option></select>
        </div>
        <div class="table-x"><table>
          <thead id="t-head"><tr></tr></thead>
          <tbody id="t-body"></tbody>
        </table></div>
        <div class="pager">
          <button id="t-prev">${t('t.prev')}</button>
          <span id="t-range"></span>
          <button id="t-next">${t('t.next')}</button>
          <span class="pg-jump">
            <span id="t-pageinfo"></span>
            <input id="t-page" type="number" min="1" title="${t('t.pageTitle')}">
            <button id="t-go">${t('t.jump')}</button>
          </span>
          <span class="pg-size">
            ${t('t.perPage')}
            <select id="t-psize">${PAGE_SIZE_OPTIONS.map(n =>
              `<option value="${n}">${n}</option>`).join('')}</select>
          </span>
        </div>
      </div>
    </div>
    <div id="t-detail-view" style="display:none">
      <span class="back-link" id="t-back">${t('t.back')}</span>
      <div id="t-detail"></div>
    </div>`;
  document.getElementById('t-psize').value = getPageSize();

  document.getElementById('t-q').onchange = e => { f.q = e.target.value; f.offset = 0; navigate(); };
  document.getElementById('t-status').onchange = e => { f.status = e.target.value; f.offset = 0; navigate(); };
  document.getElementById('t-worker').onchange = e => { f.worker = e.target.value; f.offset = 0; navigate(); };
  document.getElementById('t-psize').onchange = e => {
    setPageSize(e.target.value);   // 全局共享：worker 详情列表同步生效
    f.offset = 0;
    navigate({ keepScroll: true });
  };
  document.getElementById('t-prev').onclick = () => {
    f.offset = Math.max(0, (f.offset || 0) - pageSize()); navigate();
  };
  document.getElementById('t-next').onclick = () => {
    f.offset = (f.offset || 0) + pageSize(); navigate();
  };
  // 列排序：点击循环 该列降序 → 升序；换列重置为降序（数值列惯例）。
  // 点击激活列头上的 ✕ 清除排序（恢复默认 ID 降序）。
  // 委托绑定在稳定父节点（thead 骨架 mount 建一次，内容由 update 重建）。
  document.getElementById('t-head').addEventListener('click', (e) => {
    const clear = e.target.closest('.sort-clear');
    const th = e.target.closest('th[data-sort]');
    if (!th) return;
    if (clear) {
      f.sort = { key: 'id', desc: true };
    } else {
      const key = th.dataset.sort;
      if (f.sort.key === key) f.sort.desc = !f.sort.desc;
      else f.sort = { key, desc: true };
    }
    f.offset = 0;
    navigate();
  });
  // 跳页：输入页码（回车/按钮）→ clamp → 翻页。total 由 update 保存。
  bindPageJump(
    document.getElementById('t-page'), document.getElementById('t-go'),
    () => Math.max(1, Math.ceil((f.total || 0) / pageSize())),
    p => { f.offset = (p - 1) * pageSize(); navigate(); });
  // ---- 筛选栏智能浮窗：滚出视口顶部后整体搬入固定浮窗，回滚归位
  //（与 Timeline 工具栏同机制，公共实现见 floatbar.js；详情视图下筛选
  // 栏隐藏，不触发浮窗——canFloat 门条件）。
  if (tFloat) { tFloat.destroy(); tFloat = null; }
  document.getElementById('t-controls-float')?.remove();
  tFloat = createTopFloat({
    bar: document.getElementById('t-controls-wrap'),
    floatId: 't-controls-float',
    threshold: 70,
    moveSelf: true,
    canFloat: () => document.getElementById('t-list-view')?.style.display !== 'none',
  });
  syncTControlsFloat = () => { if (tFloat) tFloat.sync(); };
  // 返回按钮：清详情状态回列表（双容器切换，绑定不因视图切换丢失）。
  document.getElementById('t-back').onclick = () => { ctx.taskId = null; navigate(); };
  // 行点击进详情：事件委托（tbody 每轮重建，委托在稳定父节点上）。
  document.getElementById('t-body').parentElement.addEventListener('click', (e) => {
    if (e.target.closest('.expando')) return;  // 展开名称不触发进详情
    const tr = e.target.closest('tr[data-tid]');
    if (tr) { ctx.taskId = +tr.dataset.tid; navigate(); }
  });
  // worker 下拉选项一次填充（worker 集合 run 内基本不变；详情返回后仍可用）。
  // 仅当用户显式选过 worker（非空过滤值）才回填选中态——否则 master
  // （wid=0）会因「0 === 0」误显示为当前筛选。
  getJson('/api/workers').then(ws => {
    if (!ws) return;
    const sel = document.getElementById('t-worker');
    if (!sel) return;
    const cur = ctx.taskFilter.worker;
    for (const w of ws.workers) {
      if (w.worker_id === 0) continue;   // master 不执行 task，不入筛选
      const o = document.createElement('option');
      o.value = w.worker_id;
      o.textContent = `worker ${w.worker_id}`;
      if (cur !== undefined && cur !== '' && +cur === w.worker_id) o.selected = true;
      sel.appendChild(o);
    }
  });
}

export async function update(ctx) {
  const listView = document.getElementById('t-list-view');
  const detailView = document.getElementById('t-detail-view');
  if (!listView || !detailView) return;

  if (ctx.taskId != null) {
    listView.style.display = 'none';
    detailView.style.display = '';
    await renderDetail(ctx);
    return;
  }
  listView.style.display = '';
  detailView.style.display = 'none';
  renderedDetail = null;  // 离开详情：下次进入重建

  const f = ctx.taskFilter;
  const offset = f.offset || 0;
  const ps = pageSize();
  const qs = new URLSearchParams({
    worker: f.worker || 0, status: f.status || '', q: f.q || '',
    limit: ps, offset,
    order: f.sort.key, desc: f.sort.desc ? 1 : 0,
  });
  const data = await getJson('/api/tasks?' + qs);
  if (!data) return;
  if (!document.getElementById('t-head')) return;   // 页面已切走/重建
  // 列表↔详情显隐切换后重估筛选栏浮窗（详情视图下不悬浮）。
  if (syncTControlsFloat) syncTControlsFloat();

  // thead 每轮重建：排序指示箭头随状态刷新（点击委托绑在 thead 骨架上，
  // 重建 innerHTML 不丢绑定；语言切换走整页重建，文案随新语言）。
  // 列序显式声明：名称/状态保持在 ID 之后，可排序数值列各归其位。
  const sortTh = (key, labelKey, extra = '') => {
    const active = f.sort.key === key;
    // 激活列的箭头旁常显 ✕（取消排序恢复默认）；默认排序（ID 降序）无
    // 可取消态，不显示。委托 handler 内先判 .sort-clear 分支，命中即不
    // 再走列切换。
    const isDefault = key === 'id' && f.sort.desc;
    const ind = active
      ? `<span class="sort-ind">${f.sort.desc ? '▼' : '▲'}</span>` +
        (isDefault ? '' : `<span class="sort-clear" title="${t('t.sortClear')}">×</span>`)
      : '';
    return `<th data-sort="${key}" class="sortable${active ? ' sorted' : ''}" ` +
           `title="${t('t.sortHint')}" ${extra}>${t(labelKey)}${ind}</th>`;
  };
  document.getElementById('t-head').innerHTML = `<tr>` +
    sortTh('id', 'tb.id') +
    `<th style="width:30%">${t('tb.name')}</th>` +
    `<th>${t('tb.status')}</th>` +
    sortTh('worker', 'tb.worker') +
    sortTh('started', 'tb.startEnd') +
    sortTh('queue', 'tb.queueWait') +
    sortTh('duration', 'tb.duration') +
    sortTh('cpu', 'tb.cpuTime') +
    `<th title="${t('t.cpuIoShareTitle')}">${t('t.cpuIoShare')}</th>` +
    sortTh('read', 'tb.readCol') +
    sortTh('write', 'tb.writeCol') +
    sortTh('mem', 't.memAvgPeakCol') +
    `<th>${t('t.dbsCol')}</th>` +
    `</tr>`;

  document.getElementById('t-body').innerHTML = data.tasks.map(row).join('');
  f.total = data.total;
  const pages = Math.max(1, Math.ceil(data.total / ps));
  const page = Math.floor(offset / ps) + 1;
  document.getElementById('t-pageinfo').textContent = t('t.pageOf', page, pages);
  const pageInput = document.getElementById('t-page');
  pageInput.max = pages;
  if (document.activeElement !== pageInput) pageInput.value = page;
  const end = Math.min(offset + ps, data.total);
  document.getElementById('t-range').innerHTML =
    `${data.total ? offset + 1 : 0}–${end} <span class="pg-total">${t('t.totalN', data.total)}</span>`;
  document.getElementById('t-prev').disabled = offset === 0;
  document.getElementById('t-next').disabled = offset + ps >= data.total;
}

function row(tk) {
  const execMs = tk.exec_end_ms ? tk.exec_end_ms - tk.exec_start_ms : null;
  let bar = '<span class="muted">-</span>';
  if (execMs && execMs > 0) {
    const cpu = Math.min(100, Math.round(tk.cpu_time_ms / execMs * 100));
    const io = Math.min(100 - cpu, Math.round((tk.read_time_ms + tk.write_time_ms) / execMs * 100));
    bar = `<div title="CPU ${cpu}% / IO ${io}%">
      <span class="mono">${cpu}% / ${io}%</span>
      <div class="io-bar" style="width:80px">
        <div class="cpu" style="width:${cpu}%"></div>
        <div class="io" style="width:${io}%"></div>
        <div class="other" style="width:${100 - cpu - io}%"></div>
      </div></div>`;
  }
  const queue = tk.started_ms ? fmtMs(tk.started_ms - (tk.ready_ms || tk.created_ms)) : '-';
  // 起止合并为双行短时间（悬停 title 看全量毫秒级）——省两列横向空间。
  const spanCell = tk.started_ms
    ? `<span class="mono">${fmtTime(tk.started_ms)}</span><br>` +
      `<span class="muted mono">→ ${tk.completed_ms ? fmtTime(tk.completed_ms) : '-'}</span>`
    : '<span class="muted">-</span>';
  const spanTitle = `${t('tb.started')} ${fmtTimeFull(tk.started_ms)} · ` +
    `${t('tb.finished')} ${tk.completed_ms ? fmtTimeFull(tk.completed_ms) : '-'}`;
  return `<tr data-tid="${tk.task_id}" title="${escapeHtml(spanTitle)}">
    <td>${tk.task_id}</td>
    <td>${expandoHtml(tk.name)}</td>
    <td><span class="badge ${tk.status}">${statusLabel(tk.status)}</span></td>
    <td>${tk.worker_id || '-'}</td>
    <td>${spanCell}</td>
    <td>${queue}</td>
    <td>${fmtMs(execMs)}</td>
    <td>${fmtMs(tk.cpu_time_ms)}</td>
    <td>${bar}</td>
    <td class="mono">${fmtBytes(tk.read_bytes)} · ${fmtMs(tk.read_time_ms)}</td>
    <td class="mono">${fmtBytes(tk.write_bytes)} · ${fmtMs(tk.write_time_ms)}</td>
    <td>${fmtGB(tk.mem_avg_bytes)} / ${fmtGB(tk.mem_peak_bytes)}</td>
    <td class="mono">${tk.dbs ? expandoHtml(tk.dbs, 14, 8) : '-'}</td>
  </tr>`;
}

// 详情：ctx.taskId 变化时整块重建；同 task 已终态则跳过（见
// renderedDetail 注）。只写 #t-detail 容器——外壳与返回按钮由 mount 一次
// 建成（双容器切换），绑定不因视图切换丢失。

export async function renderDetail(ctx) {
  // 终态 task 的详情不可再变——已渲染过就直接跳过（不重拉不重渲）。
  if (renderedDetail && renderedDetail.tid === ctx.taskId &&
      renderedDetail.terminal) return;
  const d = await getJson(`/api/tasks/${ctx.taskId}`);
  const el = document.getElementById('t-detail');
  if (!d || d.error || !el) {
    if (el) el.innerHTML = `<div class="panel">${t('t.notFound')}</div>`;
    renderedDetail = null;
    return;
  }
  const tk = d.task;
  renderedDetail = { tid: tk.task_id, terminal: TERMINAL_STATUS.has(tk.status) };
  const execMs = tk.exec_end_ms ? tk.exec_end_ms - tk.exec_start_ms : null;
  const dbs = (tk.dbs || '').split(',').filter(Boolean);
  el.innerHTML = `
    <div class="detail-grid">
      <div>
        <div class="panel"><h3>${t('t.taskTitle', tk.task_id)} · ${shortName(tk.name, 14, 10)}</h3>
          <div class="full-name" style="margin-bottom:8px">${escapeHtml(tk.name)}</div>
          <div class="kv">
            <span class="k">${t('tb.status')}</span><span class="v"><span class="badge ${tk.status}">${statusLabel(tk.status)}</span></span>
            <span class="k">${t('kv.module')}</span><span class="v mono" title="${escapeHtml(tk.module)}">${escapeHtml(displayModule(tk.module))}</span>
            <span class="k">${t('tb.worker')}</span><span class="v">${tk.worker_id || '-'}</span>
            <span class="k">${t('kv.priority')}</span><span class="v">${tk.priority}</span>
            <span class="k">${t('tb.created')}</span><span class="v mono">${fmtTimeFull(tk.created_ms)}</span>
            <span class="k">${t('kv.depsReady')}</span><span class="v mono">${fmtTimeFull(tk.ready_ms)}</span>
            <span class="k">${t('tb.started')}</span><span class="v mono">${fmtTimeFull(tk.started_ms)}</span>
            <span class="k">${t('tb.finished')}</span><span class="v mono">${fmtTimeFull(tk.completed_ms)}</span>
            <span class="k">${t('tb.duration')}</span><span class="v mono">${fmtMs(execMs)}</span>
            ${tk.error ? `<span class="k">${t('kv.error')}</span><span class="v">${errorBriefHtml(tk.error)}<button class="pin-btn" data-pin-err title="${escapeHtml(t('err.pin'))}">📌</button><pre class="err-full" style="display:none"></pre></span>` : ''}
          </div>
        </div>
        <div class="panel"><h3>${t('t.resIo')}</h3>
          <div class="kv">
            <span class="k">${t('tb.cpuTime')}</span><span class="v">${fmtMs(tk.cpu_time_ms)}</span>
            <span class="k">${t('kv.cpuShare')}</span><span class="v">${execMs ? fmtPct(tk.cpu_time_ms / execMs) : '-'}</span>
            <span class="k">${t('kv.readTime')}</span><span class="v">${fmtMs(tk.read_time_ms)}</span>
            <span class="k">${t('kv.readBytes')}</span><span class="v">${fmtBytes(tk.read_bytes)}</span>
            <span class="k">${t('kv.writeTime')}</span><span class="v">${fmtMs(tk.write_time_ms)}</span>
            <span class="k">${t('kv.writeBytes')}</span><span class="v">${fmtBytes(tk.write_bytes)}</span>
            <span class="k">${t('kv.memBase')}</span><span class="v">${fmtGB(tk.mem_baseline_bytes)}</span>
            <span class="k">${t('kv.memAvg')}</span><span class="v">${fmtGB(tk.mem_avg_bytes)}</span>
            <span class="k">${t('kv.memAvgDelta')}</span><span class="v">${fmtGB(Math.max(0, tk.mem_avg_bytes - tk.mem_baseline_bytes))}</span>
            <span class="k">${t('kv.memPeak')}</span><span class="v">${fmtGB(tk.mem_peak_bytes)}</span>
            <span class="k">${t('kv.memPeakDelta')}</span><span class="v">${fmtGB(Math.max(0, tk.mem_peak_bytes - tk.mem_baseline_bytes))}</span>
            <span class="k">${t('kv.dbs')}</span><span class="v mono">${dbs.map(escapeHtml).join('<br>') || '-'}</span>
          </div>
        </div>
      </div>
      <div>
        <div class="panel"><h3>${t('t.eventStream')}</h3><div class="table-wrap"><table>
          <thead><tr><th>${t('tb.time')}</th><th>${t('tb.event')}</th><th>${t('tb.worker')}</th><th>${t('tb.detail')}</th></tr></thead>
          <tbody>${(d.events || []).map(e => `<tr>
            <td class="mono">${fmtTimeFull(e.epoch_ms)}</td>
            <td><span class="badge ${e.event}">${evLabel(e.event)}</span></td>
            <td>${e.worker_id || '-'}</td>
            <td class="muted mono">${escapeHtml(String(e.detail || '').slice(0, 60))}</td>
          </tr>`).join('')}</tbody>
        </table></div></div>
        <div class="panel"><h3>${t('t.objIo', (d.io || []).length)}</h3><div class="table-wrap"><table>
          <thead><tr><th>${t('tb.direction')}</th><th>${t('tb.object')}</th><th>${t('tb.bytes')}</th><th>${t('tb.elapsed')}</th></tr></thead>
          <tbody>${(d.io || []).map(r => `<tr>
            <td>${t(r.direction === 'w' ? 'd.write' : 'd.read')}</td>
            <td class="mono">${expandoHtml(r.object_name, 16, 10)}</td>
            <td>${fmtBytes(r.bytes)}</td>
            <td>${fmtMs(r.duration_ms)}</td>
          </tr>`).join('')}</tbody>
        </table></div></div>
      </div>
    </div>`;
}
