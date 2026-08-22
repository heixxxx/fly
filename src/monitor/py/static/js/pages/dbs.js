// DBs：db 生命周期时间线 + 关联 task 反查 + 读写汇总。
import { getJson, fmtBytes, fmtMs, fmtTimeFull } from '../api.js';

export function destroy() {}

export async function render(ctx) {
  const main = ctx.main;
  const data = await getJson('/api/dbs');
  if (!data) { main.innerHTML = '<div class="panel">API 不可达</div>'; return; }

  const panels = (data.dbs || []).map(d => {
    const evs = d.events || [];
    const created = evs.find(e => e.event === 'DB_CREATED');
    const frozen = evs.find(e => e.event === 'DB_FROZEN');
    return `<div class="panel">
      <h3 class="mono">${escapeHtml(d.db)}</h3>
      <div class="kv">
        <span class="k">首见</span><span class="v mono">${created ? fmtTimeFull(created.epoch_ms) : '-'}</span>
        <span class="k">freeze</span><span class="v mono">${frozen ? fmtTimeFull(frozen.epoch_ms) : '未冻结'}</span>
        <span class="k">关联 task 数</span><span class="v">${d.task_count}</span>
        <span class="k">merge</span><span class="v">${evs.filter(e => e.event.startsWith('DB_MERGE')).map(e => e.event).join(', ') || '-'}</span>
      </div>
      <div class="table-wrap" style="max-height:220px"><table>
        <thead><tr><th>时间</th><th>事件</th><th>详情</th></tr></thead>
        <tbody>${evs.map(e => `<tr>
          <td class="mono">${fmtTimeFull(e.epoch_ms)}</td>
          <td><span class="badge ${e.event}">${e.event}</span></td>
          <td class="muted mono" style="white-space:normal">${escapeHtml(String(e.detail || ''))}</td>
        </tr>`).join('')}</tbody>
      </table></div>
    </div>`;
  });

  main.innerHTML = panels.length ? panels.join('')
    : '<div class="panel">无 db 事件（run 中未使用 Database）</div>';
}

function escapeHtml(s) {
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}
