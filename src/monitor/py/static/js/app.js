// SPA 页签路由 + 3s 轮询调度 + 页面模块生命周期。
import { getJson, fmtTime } from './api.js';
import * as overview from './pages/overview.js';
import * as workers from './pages/workers.js';
import * as tasks from './pages/tasks.js';
import * as timeline from './pages/timeline.js';
import * as dbs from './pages/dbs.js';

const PAGES = { overview, workers, tasks, timeline, dbs };
const POLL_MS = 3000;

const main = document.getElementById('main');
const nav = document.getElementById('nav');
const runInfo = document.getElementById('run-info');
const pollEnabled = document.getElementById('poll-enabled');

let current = null;      // 当前页模块状态 { mod, ctx }
let pollTimer = null;

async function refreshHeader() {
  const meta = await getJson('/api/meta');
  if (!meta) return;
  const m = meta.meta || {};
  const start = m.run_start_ms ? fmtTime(+m.run_start_ms) : '?';
  const end = m.run_end_ms ? fmtTime(+m.run_end_ms) : '进行中';
  runInfo.textContent = `${m.hostname || ''} · ${start} → ${end}`;
}

async function render() {
  if (!current) return;
  await current.mod.render(current.ctx);
}

function switchPage(name) {
  // 页面卸载（销毁 chart 实例防泄漏）。
  if (current && current.mod.destroy) current.mod.destroy(current.ctx);
  for (const b of nav.children) b.classList.toggle('active', b.dataset.page === name);
  main.innerHTML = '';
  current = { mod: PAGES[name], ctx: { main, workerId: null, taskFilter: {} } };
  render();
}

// 页面模块可注册子路由回调（worker 详情/task 详情返回等）。
export function rerender() { render(); }
export function showPage(name, opts) {
  switchPage(name);
  if (opts) Object.assign(current.ctx, opts);
  render();
}

nav.addEventListener('click', (e) => {
  const btn = e.target.closest('button[data-page]');
  if (btn) switchPage(btn.dataset.page);
});

pollEnabled.addEventListener('change', () => {
  if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
  if (pollEnabled.checked) {
    pollTimer = setInterval(() => { refreshHeader(); render(); }, POLL_MS);
  }
});

refreshHeader();
switchPage('overview');
pollTimer = setInterval(() => { refreshHeader(); render(); }, POLL_MS);
