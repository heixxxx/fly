// DBs：简化视图（用户裁定）——db 列表 + 创建时间 + 冻结时间 + 磁盘占用(GB)。
// 不再展示 db 的事件明细（事件流可在总览页查）。
import { getJson, fmtGB, fmtTimeFull, expandoHtml } from '../api.js';
import { t } from '../i18n.js';

export function destroy() {}

export function mount(ctx) {
  ctx.main.innerHTML = `
    <div class="panel">
      <h3>Databases</h3>
      <div class="table-wrap"><table>
        <thead><tr>
          <th>${t('db.path')}</th><th>${t('tb.created')}</th><th>${t('tb.frozen')}</th><th>${t('tb.diskUsage')}</th>
        </tr></thead>
        <tbody id="dbs-body"></tbody>
      </table></div>
      <div class="muted" style="margin-top:8px; font-size:12px">
        ${t('db.hint')}
      </div>
    </div>`;
}

export async function update(ctx) {
  const el = document.getElementById('dbs-body');
  if (!el) return;
  const data = await getJson('/api/dbs');
  if (!data) return;

  el.innerHTML = (data.dbs || []).map(d => `<tr>
    <td>${expandoHtml(d.db, 20, 14)}</td>
    <td class="mono">${fmtTimeFull(d.created_ms)}</td>
    <td class="mono">${d.frozen_ms ? fmtTimeFull(d.frozen_ms)
      : `<span class="muted">${t('db.notFrozen')}</span>`}</td>
    <td>${d.disk_bytes != null && d.disk_bytes >= 0 ? fmtGB(d.disk_bytes)
      : '<span class="muted">-</span>'}</td>
  </tr>`).join('') ||
    `<tr><td colspan="4" class="muted">${t('db.empty')}</td></tr>`;
}
