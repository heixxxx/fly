// Tasks：可搜索/过滤的任务表（IO/CPU 密集分析列）+ task 详情。
import { getJson, fmtBytes, fmtMs, fmtTimeFull } from '../api.js';
import { rerender } from '../app.js';

const PAGE_SIZE = 50;

export function destroy() {}

export async function render(ctx) {
  const main = ctx.main;
  if (ctx.taskId != null) {
    await renderDetail(ctx, ctx.taskId);
    return;
  }
  const f = ctx.taskFilter;
  const offset = f.offset || 0;
  const qs = new URLSearchParams({
    worker: f.worker || 0, status: f.status || '', q: f.q || '',
    limit: PAGE_SIZE, offset,
  });
  const data = await getJson('/api/tasks?' + qs);
  if (!data) { main.innerHTML = '<div class="panel">API 不可达</div>'; return; }

  main.innerHTML = `
    <div class="panel">
      <div class="controls">
        <input id="t-q" placeholder="搜索 task 名称…" value="${f.q || ''}" style="width:220px">
        <select id="t-status">
          <option value="">全部状态</option>
          ${['PENDING', 'RUNNING', 'COMPLETED', 'FAILED', 'CANCELLED'].map(s =>
            `<option value="${s}" ${f.status === s ? 'selected' : ''}>${s}</option>`).join('')}
        </select>
        <select id="t-worker">
          <option value="0">全部 worker</option>
        </select>
        <span class="muted">共 ${data.total} 条</span>
      </div>
      <div class="table-wrap"><table>
        <thead><tr>
          <th>ID</th><th>名称</th><th>状态</th><th>worker</th>
          <th>创建/派发/完成</th><th>排队→执行</th><th>执行时长</th><th>CPU time</th>
          <th title="exec 时长中 CPU/IO 占比">CPU / IO 占比</th>
          <th>读(时间/字节)</th><th>写(时间/字节)</th>
          <th>内存 avg / peak</th><th>关联 db</th>
        </tr></thead>
        <tbody>${data.tasks.map(row).join('')}</tbody>
      </table></div>
      <div class="pager">
        <button id="t-prev" ${offset === 0 ? 'disabled' : ''}>上一页</button>
        <span>${offset + 1} - ${Math.min(offset + PAGE_SIZE, data.total)}</span>
        <button id="t-next" ${offset + PAGE_SIZE >= data.total ? 'disabled' : ''}>下一页</button>
      </div>
    </div>`;

  const workers = await getJson('/api/workers');
  if (workers) {
    const sel = document.getElementById('t-worker');
    for (const w of workers.workers) {
      const o = document.createElement('option');
      o.value = w.worker_id; o.textContent = `worker ${w.worker_id}`;
      if (+f.worker === w.worker_id) o.selected = true;
      sel.appendChild(o);
    }
  }

  document.getElementById('t-q').onchange = e => { f.q = e.target.value; f.offset = 0; rerender(); };
  document.getElementById('t-status').onchange = e => { f.status = e.target.value; f.offset = 0; rerender(); };
  document.getElementById('t-worker').onchange = e => { f.worker = e.target.value; f.offset = 0; rerender(); };
  document.getElementById('t-prev').onclick = () => { f.offset = Math.max(0, offset - PAGE_SIZE); rerender(); };
  document.getElementById('t-next').onclick = () => { f.offset = offset + PAGE_SIZE; rerender(); };
  for (const tr of main.querySelectorAll('tbody tr')) {
    tr.onclick = () => { ctx.taskId = +tr.dataset.tid; rerender(); };
  }
}

function row(t) {
  const execMs = t.exec_end_ms ? t.exec_end_ms - t.exec_start_ms : null;
  // CPU/IO 密集判别：cpu_time 与 (read+write) 时间相对执行窗口的占比条。
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
    <td>${escapeHtml(t.name)}</td>
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
    <td>${fmtBytes(t.mem_avg_bytes)} / ${fmtBytes(t.mem_peak_bytes)}</td>
    <td class="mono" style="white-space:normal">${escapeHtml(t.dbs || '')}</td>
  </tr>`;
}

async function renderDetail(ctx, tid) {
  const main = ctx.main;
  const d = await getJson(`/api/tasks/${tid}`);
  if (!d || d.error) { main.innerHTML = '<div class="panel">task 不存在</div>'; return; }
  const t = d.task;
  const execMs = t.exec_end_ms ? t.exec_end_ms - t.exec_start_ms : null;
  const dbs = (t.dbs || '').split(',').filter(Boolean);

  main.innerHTML = `
    <span class="back-link" id="t-back">← 返回任务列表</span>
    <div class="detail-grid">
      <div>
        <div class="panel"><h3>task ${t.task_id} · ${escapeHtml(t.name)}</h3>
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
            <span class="k">内存 baseline</span><span class="v">${fmtBytes(t.mem_baseline_bytes)}</span>
            <span class="k">内存 avg</span><span class="v">${fmtBytes(t.mem_avg_bytes)}（delta ${fmtBytes(Math.max(0, t.mem_avg_bytes - t.mem_baseline_bytes))}）</span>
            <span class="k">内存 peak</span><span class="v">${fmtBytes(t.mem_peak_bytes)}（delta ${fmtBytes(Math.max(0, t.mem_peak_bytes - t.mem_baseline_bytes))}）</span>
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
            <td class="muted mono" style="white-space:normal">${escapeHtml(String(e.detail || ''))}</td>
          </tr>`).join('')}</tbody>
        </table></div></div>
        <div class="panel"><h3>对象 IO 明细（${(d.io || []).length}）</h3><div class="table-wrap"><table>
          <thead><tr><th>方向</th><th>对象</th><th>字节</th><th>耗时</th></tr></thead>
          <tbody>${(d.io || []).map(r => `<tr>
            <td>${r.direction === 'w' ? '写' : '读'}</td>
            <td class="mono" style="white-space:normal">${escapeHtml(r.object_name)}</td>
            <td>${fmtBytes(r.bytes)}</td>
            <td>${fmtMs(r.duration_ms)}</td>
          </tr>`).join('')}</tbody>
        </table></div></div>
      </div>
    </div>`;
  document.getElementById('t-back').onclick = () => { ctx.taskId = null; rerender(); };
}

function escapeHtml(s) {
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}
