// DBs：简化视图（用户裁定）——db 列表 + 创建时间 + 冻结时间 + 磁盘占用(GB)。
// 不再展示 db 的事件明细（事件流可在总览页查）。
import { getJson, fmtGB, fmtTimeFull, expandoHtml } from '../api.js';

export function destroy() {}

export function mount(ctx) {
  ctx.main.innerHTML = `
    <div class="panel">
      <h3>Databases</h3>
      <div class="table-wrap"><table>
        <thead><tr>
          <th>db 路径</th><th>创建时间</th><th>冻结时间</th><th>磁盘占用</th>
        </tr></thead>
        <tbody id="dbs-body"></tbody>
      </table></div>
      <div class="muted" style="margin-top:8px; font-size:12px">
        磁盘占用：freeze 时为终值；未冻结 db 为 run 结束时的占用（- 表示未测得）。
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
      : '<span class="muted">未冻结</span>'}</td>
    <td>${d.disk_bytes != null && d.disk_bytes >= 0 ? fmtGB(d.disk_bytes)
      : '<span class="muted">-</span>'}</td>
  </tr>`).join('') ||
    '<tr><td colspan="4" class="muted">无 db（run 中未使用 Database）</td></tr>';
}
