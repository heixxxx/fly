// Tasks：搜索/过滤/分页的任务表 + task 详情。
// mount 建控件（事件绑一次）与 tbody 容器；update 仅填 tbody——过滤条件与
// 分页状态存活于 ctx.taskFilter，输入框聚焦/滚动不被轮询打断。
import { getJson, fmtGB, fmtBytes, fmtMs, fmtTimeFull, escapeHtml, shortName, expandoHtml } from '../api.js';
import { navigate } from '../app.js';

const PAGE_SIZE = 50;

export function destroy() {
  detailTid = -1;
}

export function mount(ctx) {
  const f = ctx.taskFilter;
  ctx.main.innerHTML = `
    <div class="panel">
      <div class="controls">
        <input id="t-q" placeholder="搜索 task 名称…" value="${f.q || ''}" style="width:220px">
        <select id="t-status">
          <option value="">全部状态</option>
          ${['PENDING', 'RUNNING', 'COMPLETED', 'FAILED', 'CANCELLED'].map(s =>
            `<option value="${s}" ${f.status === s ? 'selected' : ''}>${s}</option>`).join('')}
        </select>
        <select id="t-worker"><option value="0">全部 worker</option></select>
        <span class="muted" id="t-total"></span>
      </div>
      <div class="table-wrap"><table>
        <thead><tr>
          <th>ID</th><th>名称</th><th>状态</th><th>worker</th>
          <th>创建/派发/完成</th><th>排队→执行</th><th>执行时长</th><th>CPU time</th>
          <th title="exec 时长中 CPU/IO 占比">CPU / IO 占比</th>
          <th>读(时间/字节)</th><th>写(时间/字节)</th>
          <th>内存 avg / peak</th><th>关联 db</th>
        </tr></thead>
        <tbody id="t-body"></tbody>
      </table></div>
      <div class="pager">
        <button id="t-prev">上一页</button>
        <span id="t-range"></span>
        <button id="t-next">下一页</button>
      </div>
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
      o.value = w.worker_id; o.textContent = `worker ${w.worker_id}`;
      if (+ (ctx.taskFilter.worker || 0) === w.worker_id) o.selected = true;
      sel.appendChild(o);
    }
  });
}

export async function update(ctx) {
  if (ctx.taskId != null) {
    await renderDetail(ctx);
    return;
  }
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
  document.getElementById('t-total').textContent = `共 ${data.total} 条`;
  document.getElementById('t-range').textContent =
    `${offset + 1} - ${Math.min(offset + PAGE_SIZE, data.total)}`;
  document.getElementById('t-prev').disabled = offset === 0;
  document.getElementById('t-next').disabled = offset + PAGE_SIZE >= data.total;
  if (wrap) wrap.scrollTop = scrollTop;
}

function row(t) {
  const execMs = t.exec_end_ms ? t.exec_end_ms - t.exec_start_ms : null;
  let bar = '<span class="muted">-</span>';
  if (execMs && execMs > 0) {
    const cpu = Math.min(100, Math.round(t.cpu_time_ms / execMs * 100));
    const io = Math.min(100 - cpu, Math.round((t.read_time_ms + t.write_time_ms) / execMs * 100));
    bar = `<div title="CPU ${cpu}% / IO ${io}%">
      <span class="mono">${cpu}% / ${io}%</span>
      <div class="io-bar" style="width:80px">
        <div class="cpu" style="width:${cpu}%"></div>
        <div class="io" style="width:${io}%"></div>
        <div class="other" style="width:${100 - cpu - io}%"></div>
      </div></div>`;
  }
  const queue = t.started_ms ? fmtMs(t.started_ms - (t.ready_ms || t.created_ms)) : '-';
  return `<tr data-tid="${t.task_id}">
    <td>${t.task_id}</td>
    <td>${expandoHtml(t.name)}</td>
    <td><span class="badge ${t.status}">${t.status}</span></td>
    <td>${t.worker_id || '-'}</td>
    <td class="mono" style="white-space:normal">
      ${fmtTimeFull(t.created_ms)}<br>${fmtTimeFull(t.started_ms)} → ${fmtTimeFull(t.completed_ms)}</td>
    <td>${queue}</td>
    <td>${fmtMs(execMs)}</td>
    <td>${fmtMs(t.cpu_time_ms)}</td>
    <td>${bar}</td>
    <td>${fmtMs(t.read_time_ms)} / ${fmtBytes(t.read_bytes)}</td>
    <td>${fmtMs(t.write_time_ms)} / ${fmtBytes(t.write_bytes)}</td>
    <td>${fmtGB(t.mem_avg_bytes)} / ${fmtGB(t.mem_peak_bytes)}</td>
    <td class="mono">${t.dbs ? expandoHtml(t.dbs, 14, 8) : '-'}</td>
  </tr>`;
}

// 详情：ctx.taskId 变化时整块重建（纯文本无图表；task 详情数据多为终态，
// 指纹机制已挡掉绝大多数刷新）。
let detailTid = -1;

export async function renderDetail(ctx) {
  if (ctx.taskId !== detailTid) {
    ctx.main.innerHTML = `
      <span class="back-link" id="t-back">← 返回任务列表</span>
      <div id="t-detail"></div>`;
    document.getElementById('t-back').onclick = () => { ctx.taskId = null; navigate(); };
    detailTid = ctx.taskId;
  }
  const d = await getJson(`/api/tasks/${ctx.taskId}`);
  const el = document.getElementById('t-detail');
  if (!d || d.error || !el) { if (el) el.innerHTML = '<div class="panel">task 不存在</div>'; return; }
  const t = d.task;
  const execMs = t.exec_end_ms ? t.exec_end_ms - t.exec_start_ms : null;
  const dbs = (t.dbs || '').split(',').filter(Boolean);
  el.innerHTML = `
    <div class="detail-grid">
      <div>
        <div class="panel"><h3>task ${t.task_id} · ${shortName(t.name, 14, 10)}</h3>
          <div class="full-name" style="margin-bottom:8px">${escapeHtml(t.name)}</div>
          <div class="kv">
            <span class="k">状态</span><span class="v"><span class="badge ${t.status}">${t.status}</span></span>
            <span class="k">模块</span><span class="v mono">${escapeHtml(t.module)}</span>
            <span class="k">worker</span><span class="v">${t.worker_id || '-'}</span>
            <span class="k">优先级</span><span class="v">${t.priority}</span>
            <span class="k">创建</span><span class="v mono">${fmtTimeFull(t.created_ms)}</span>
            <span class="k">依赖就绪</span><span class="v mono">${fmtTimeFull(t.ready_ms)}</span>
            <span class="k">派发</span><span class="v mono">${fmtTimeFull(t.started_ms)}</span>
            <span class="k">执行窗口</span><span class="v mono">${fmtTimeFull(t.exec_start_ms)} → ${fmtTimeFull(t.exec_end_ms)}（${fmtMs(execMs)}）</span>
            <span class="k">完成</span><span class="v mono">${fmtTimeFull(t.completed_ms)}</span>
            ${t.error ? `<span class="k">错误</span><span class="v mono" style="color:var(--err);white-space:pre-wrap">${escapeHtml(t.error.slice(0, 600))}</span>` : ''}
          </div>
        </div>
        <div class="panel"><h3>资源 / IO</h3>
          <div class="kv">
            <span class="k">CPU time</span><span class="v">${fmtMs(t.cpu_time_ms)}${execMs ? `（${(t.cpu_time_ms / execMs * 100).toFixed(0)}% of exec）` : ''}</span>
            <span class="k">读时间/字节</span><span class="v">${fmtMs(t.read_time_ms)} / ${fmtBytes(t.read_bytes)}</span>
            <span class="k">写时间/字节</span><span class="v">${fmtMs(t.write_time_ms)} / ${fmtBytes(t.write_bytes)}</span>
            <span class="k">内存 baseline</span><span class="v">${fmtGB(t.mem_baseline_bytes)}</span>
            <span class="k">内存 avg</span><span class="v">${fmtGB(t.mem_avg_bytes)}（delta ${fmtGB(Math.max(0, t.mem_avg_bytes - t.mem_baseline_bytes))}）</span>
            <span class="k">内存 peak</span><span class="v">${fmtGB(t.mem_peak_bytes)}（delta ${fmtGB(Math.max(0, t.mem_peak_bytes - t.mem_baseline_bytes))}）</span>
            <span class="k">关联 db</span><span class="v mono">${dbs.map(escapeHtml).join('<br>') || '-'}</span>
          </div>
        </div>
      </div>
      <div>
        <div class="panel"><h3>事件流</h3><div class="table-wrap"><table>
          <thead><tr><th>时间</th><th>事件</th><th>worker</th><th>详情</th></tr></thead>
          <tbody>${(d.events || []).map(e => `<tr>
            <td class="mono">${fmtTimeFull(e.epoch_ms)}</td>
            <td><span class="badge ${e.event}">${e.event}</span></td>
            <td>${e.worker_id || '-'}</td>
            <td class="muted mono">${escapeHtml(String(e.detail || '').slice(0, 60))}</td>
          </tr>`).join('')}</tbody>
        </table></div></div>
        <div class="panel"><h3>对象 IO 明细（${(d.io || []).length}）</h3><div class="table-wrap"><table>
          <thead><tr><th>方向</th><th>对象</th><th>字节</th><th>耗时</th></tr></thead>
          <tbody>${(d.io || []).map(r => `<tr>
            <td>${r.direction === 'w' ? '写' : '读'}</td>
            <td class="mono">${expandoHtml(r.object_name, 16, 10)}</td>
            <td>${fmtBytes(r.bytes)}</td>
            <td>${fmtMs(r.duration_ms)}</td>
          </tr>`).join('')}</tbody>
        </table></div></div>
      </div>
    </div>`;
}

export function resetDetail() { detailTid = -1; }

function escapeHtml(s) {
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}
