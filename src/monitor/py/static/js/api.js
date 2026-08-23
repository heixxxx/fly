// 轻量 fetch 封装（GET JSON，失败静默返回 null 由调用方降级）。
export async function getJson(url) {
  try {
    const r = await fetch(url, { cache: 'no-store' });
    if (!r.ok) return null;
    return await r.json();
  } catch (e) {
    return null;
  }
}

// ---- 格式化工具（全局共享） ----
export function fmtBytes(n) {
  if (n == null) return '-';
  if (n < 1024) return n + ' B';
  if (n < 1024 ** 2) return (n / 1024).toFixed(1) + ' KB';
  if (n < 1024 ** 3) return (n / 1024 ** 2).toFixed(1) + ' MB';
  return (n / 1024 ** 3).toFixed(2) + ' GB';
}

// 内存/磁盘占用统一 GB 单位，保留三位有效数字（用户裁定口径）。
// toPrecision(3) 后经 Number() 归一，避免科学计数；小于 1MB 记 0.00 GB
// 意义有限，仍按三位有效数字呈现（0.00123 GB）。
export function fmtGB(bytes) {
  if (bytes == null || bytes < 0) return '-';
  if (bytes === 0) return '0 GB';
  const gb = bytes / (1024 ** 3);
  return Number(gb.toPrecision(3)) + ' GB';
}

export function fmtMs(ms) {
  if (ms == null) return '-';
  if (ms < 1000) return ms + ' ms';
  if (ms < 60000) return (ms / 1000).toFixed(1) + ' s';
  const m = Math.floor(ms / 60000), s = Math.round((ms % 60000) / 1000);
  return `${m}m${s}s`;
}

export function fmtTime(epochMs) {
  if (!epochMs) return '-';
  const d = new Date(epochMs);
  const p = (x) => String(x).padStart(2, '0');
  return `${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
}

export function fmtTimeFull(epochMs) {
  if (!epochMs) return '-';
  const d = new Date(epochMs);
  const p = (x) => String(x).padStart(2, '0');
  return `${d.getMonth() + 1}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}.${String(d.getMilliseconds()).padStart(3, '0')}`;
}

export function fmtDuration(ms) {
  if (ms == null || ms <= 0) return '-';
  return fmtMs(ms);
}

// ---- 超长名称的缩略/展开（task 名、对象全名、db 路径共用） ----

export function escapeHtml(s) {
  return String(s ?? '').replace(/&/g, '&amp;').replace(/</g, '&lt;')
    .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

// 缩略文本：短名原样；长名首尾各 head/tail 字符 + '....'。
export function shortName(s, head = 10, tail = 10) {
  const f = String(s ?? '');
  if (f.length <= head + tail + 4) return escapeHtml(f);
  return escapeHtml(f.slice(0, head)) + '....' + escapeHtml(f.slice(-tail));
}

// 可点击展开的名称单元格：默认单行缩略，点击切换为折行全名
// （word-break 保证任何长度都在可视宽度内换行，不破坏布局）。
export function expandoHtml(full, head = 10, tail = 10) {
  const f = String(full ?? '');
  const short = f.length <= head + tail + 4
    ? escapeHtml(f)
    : escapeHtml(f.slice(0, head)) + '....' + escapeHtml(f.slice(-tail));
  return `<span class="expando" data-full="${escapeHtml(f)}" ` +
         `title="点击展开/收起全名">${short}</span>`;
}
