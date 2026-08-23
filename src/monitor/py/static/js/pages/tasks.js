// Tasks：搜索/过滤/分页的任务表 + task 详情。
// mount 建列表/详情双容器（控件事件绑一次，切换仅显隐——详情不得覆盖
// main.innerHTML，否则返回时列表结构与绑定已销毁、update 写不回）。
// update 仅填数据——过滤条件与分页状态存活于 ctx.taskFilter。
import { getJson, fmtGB, fmtBytes, fmtMs, fmtPct, displayModule, fmtTimeFull, escapeHtml, shortName, expandoHtml, errorBriefHtml } from '../api.js';
import { t } from '../i18n.js';
import { navigate } from '../app.js';

const PAGE_SIZE = 50;

export function destroy() {
  detailTid = -1;
}

export function mount(ctx) {
  const f = ctx.taskFilter;
  ctx.main.innerHTML = `
    <div id="t-list-view">
      <div class="panel">
        <div class="controls">
          <input id="t-q" placeholder="${t('t.searchPh')}" value="${f.q || ''}" style="width:220px">
          <select id="t-status">
            <option value="">${t('t.allStatus')}</option>
            ${['PENDING', 'RUNNING', 'COMPLETED', 'FAILED', 'CANCELLED'].map(s =>
              `<option value="${s}" ${f.status === s ? 'selected' : ''}>${s}</option>`).join('')}
          </select>
          <select id="t-worker"><option value="0">${t('t.allWorkers')}</option></select>
          <span class="muted" id="t-total"></span>
        </div>
        <div class="table-wrap"><table>
          <thead><tr>
            <th>${t('tb.id')}</th><th>${t('tb.name')}</th><th>${t('tb.status')}</th><th>worker</th>
            <th>${t('tb.created')}</th><th>${t('tb.started')}</th><th>${t('tb.finished')}</th><th>${t('tb.queueWait')}</th><th>${t('tb.duration')}</th><th>${t('tb.cpuTime')}</th>
            <th title="${t('t.cpuIoShareTitle')}">${t('t.cpuIoShare')}</th>
            <th>${t('t.readBT')}</th><th>${t('t.writeBT')}</th>
            <th>${t('t.memAvgPeakCol')}</th><th>${t('t.dbsCol')}</th>
          </tr></thead>
          <tbody id="t-body"></tbody>
        </table></div>
        <div class="pager">
          <button id="t-prev">${t('t.prev')}</button>
          <span id="t-range"></span>
          <button id="t-next">${t('t.next')}</button>
        </div>
      </div>
    </div>
    <div id="t-detail-view" style="display:none">
      <span class="back-link" id="t-back">${t('t.back')}</span>
      <div id="t-detail"></div>
    </div>`;

  document.getElementById('t-q').onchange = e => { f.q = e.target.value; f.offset = 0; navigate(); };
  document.getElementById('t-status').onchange = e => { f.status = e.target.value; f.offset = 0; navigate(); };
  document.getElementById('t-worker').onchange = e => { f.worker = e.target.value; f.offset = 0; navigate(); };
  document.getElementById('t-prev').onclick = () => {
    f.offset = Math.max(0, (f.offset || 0) - PAGE_SIZE); navigate();
  };
  document.getElementById('t-next').onclick = () => {
    f.offset = (f.offset || 0) + PAGE_SIZE; navigate();
  };
  // 返回按钮：清详情状态回列表（双容器切换，绑定不因视图切换丢失）。
  document.getElementById('t-back').onclick = () => { ctx.taskId = null; navigate(); };
  // 行点击进详情：事件委托（tbody 每轮重建，委托在稳定父节点上）。
  document.getElementById('t-body').parentElement.addEventListener('click', (e) => {
    if (e.target.closest('.expando')) return;  // 展开名称不触发进详情
    const tr = e.target.closest('tr[data-tid]');
    if (tr) { ctx.taskId = +tr.dataset.tid; navigate(); }
  });
  // worker 下拉选项一次填充（worker 集合 run 内基本不变；详情返回后仍可用）。
  getJson('/api/workers').then(ws => {
    if (!ws) return;
    const sel = document.getElementById('t-worker');
    if (!sel) return;
    for (const w of ws.workers) {
      const o = document.createElement('option');
      o.value = w.worker_id;
      o.textContent = w.worker_id === 0 ? 'master' : `worker ${w.worker_id}`;
      if (+ (ctx.taskFilter.worker || 0) === w.worker_id) o.selected = true;
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
  detailTid = -1;  // 离开详情：下次进入重建

  const f = ctx.taskFilter;
  const offset = f.offset || 0;
  const qs = new URLSearchParams({
    worker: f.worker || 0, status: f.status || '', q: f.q || '',
    limit: PAGE_SIZE, offset,
  });
  const data = await getJson('/api/tasks?' + qs);
  if (!data) return;

  // 表格滚动位置保留（tbody 重建后恢复）。
  const wrap = document.querySelector('#t-body').closest('.table-wrap');
  const scrollTop = wrap ? wrap.scrollTop : 0;

  document.getElementById('t-body').innerHTML = data.tasks.map(row).join('');
  document.getElementById('t-total').textContent = t('t.totalN', data.total);
  document.getElementById('t-range').textContent =
    `${offset + 1} - ${Math.min(offset + PAGE_SIZE, data.total)}`;
  document.getElementById('t-prev').disabled = offset === 0;
  document.getElementById('t-next').disabled = offset + PAGE_SIZE >= data.total;
  if (wrap) wrap.scrollTop = scrollTop;
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
  return `<tr data-tid="${tk.task_id}">
    <td>${tk.task_id}</td>
    <td>${expandoHtml(tk.name)}</td>
    <td><span class="badge ${tk.status}">${tk.status}</span></td>
    <td>${tk.worker_id || '-'}</td>
    <td class="mono">${fmtTimeFull(tk.created_ms)}</td>
    <td class="mono">${fmtTimeFull(tk.started_ms)}</td>
    <td class="mono">${fmtTimeFull(tk.completed_ms)}</td>
    <td>${queue}</td>
    <td>${fmtMs(execMs)}</td>
    <td>${fmtMs(tk.cpu_time_ms)}</td>
    <td>${bar}</td>
    <td>${fmtBytes(tk.read_bytes)} / ${fmtMs(tk.read_time_ms)}</td>
    <td>${fmtBytes(tk.write_bytes)} / ${fmtMs(tk.write_time_ms)}</td>
    <td>${fmtGB(tk.mem_avg_bytes)} / ${fmtGB(tk.mem_peak_bytes)}</td>
    <td class="mono">${tk.dbs ? expandoHtml(tk.dbs, 14, 8) : '-'}</td>
  </tr>`;
}

// 详情：ctx.taskId 变化时整块重建（纯文本无图表；task 详情数据多为终态，
// 指纹机制已挡掉绝大多数刷新）。只写 #t-detail 容器——外壳与返回按钮由
// mount 一次建成（双容器切换），绑定不因视图切换丢失。
let detailTid = -1;

export async function renderDetail(ctx) {
  detailTid = ctx.taskId;
  const d = await getJson(`/api/tasks/${ctx.taskId}`);
  const el = document.getElementById('t-detail');
  if (!d || d.error || !el) { if (el) el.innerHTML = `<div class="panel">${t('t.notFound')}</div>`; return; }
  const tk = d.task;
  const execMs = tk.exec_end_ms ? tk.exec_end_ms - tk.exec_start_ms : null;
  const dbs = (tk.dbs || '').split(',').filter(Boolean);
  el.innerHTML = `
    <div class="detail-grid">
      <div>
        <div class="panel"><h3>task ${tk.task_id} · ${shortName(tk.name, 14, 10)}</h3>
          <div class="full-name" style="margin-bottom:8px">${escapeHtml(tk.name)}</div>
          <div class="kv">
            <span class="k">${t('tb.status')}</span><span class="v"><span class="badge ${tk.status}">${tk.status}</span></span>
            <span class="k">${t('kv.module')}</span><span class="v mono" title="${escapeHtml(tk.module)}">${escapeHtml(displayModule(tk.module))}</span>
            <span class="k">worker</span><span class="v">${tk.worker_id || '-'}</span>
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
          <thead><tr><th>${t('tb.time')}</th><th>${t('tb.event')}</th><th>worker</th><th>${t('tb.detail')}</th></tr></thead>
          <tbody>${(d.events || []).map(e => `<tr>
            <td class="mono">${fmtTimeFull(e.epoch_ms)}</td>
            <td><span class="badge ${e.event}">${e.event}</span></td>
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

export function resetDetail() { detailTid = -1; }
