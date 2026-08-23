// 主题：浅色 / 深色 / 跟随系统（默认跟随系统）。
//   · 实际渲染主题落在 <html data-theme="light|dark">，CSS 变量双套值
//     （app.css），DOM 侧自动生效。
//   · 跟随系统经 matchMedia(prefers-color-scheme) 解析，系统切换实时生效。
//   · 选择值持久化 localStorage（fly-monitor-theme）。
//   · FOUC 防护：index.html 头部内联脚本在 CSS 渲染前先落 data-theme
//     与 lang（此处 init 幂等重设一次）。
//   · cssVar()：ECharts canvas 不消费 CSS 变量，图表颜色经此实时读取
//     （主题切换后页面重建、图表实例随 mount 新建，自然取到新值）。
const mq = (typeof window !== 'undefined' && window.matchMedia)
  ? window.matchMedia('(prefers-color-scheme: dark)') : null;

let mode = 'system';
if (typeof localStorage !== 'undefined') {
  const saved = localStorage.getItem('fly-monitor-theme');
  if (saved === 'light' || saved === 'dark') mode = saved;
}

const listeners = new Set();

export function resolvedTheme() {
  if (mode === 'system') {
    return (mq && mq.matches) ? 'dark' : 'light';
  }
  return mode;
}

function applyTheme() {
  if (typeof document !== 'undefined' && document.documentElement) {
    document.documentElement.dataset.theme = resolvedTheme();
  }
}

export function getTheme() {
  return mode;
}

export function setTheme(m) {
  if (m !== 'light' && m !== 'dark' && m !== 'system') return;
  if (m === mode) return;
  mode = m;
  if (typeof localStorage !== 'undefined') {
    localStorage.setItem('fly-monitor-theme', m);
  }
  applyTheme();
  listeners.forEach(fn => fn());
}

export function onThemeChange(fn) {
  listeners.add(fn);
}

// 读取根元素 CSS 变量（trim 去掉换行空白；canvas 侧颜色用）。
// fallback：变量缺失/样式表未就绪等异常场景下回落深色默认值——绝不把
// 空色交给 ECharts（空串会被当作无效色，轴标签/线条渲染成默认深灰，
// 深色主题下与背景几乎不可见）。
export function cssVar(name, fallback = '') {
  let v = '';
  if (typeof getComputedStyle !== 'undefined' && document.documentElement) {
    v = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  }
  return v || fallback;
}

// 跟随系统：系统主题切换时实时应用（仅 system 模式下有视觉变化）。
if (mq && typeof mq.addEventListener === 'function') {
  mq.addEventListener('change', () => {
    if (mode !== 'system') return;
    applyTheme();
    listeners.forEach(fn => fn());
  });
}

applyTheme();
