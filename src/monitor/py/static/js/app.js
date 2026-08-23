// SPA 页签路由 + 数据指纹轮询。
//
// 刷新模型（增强式，用户裁定全量刷新在数据量增大时有明显延迟）：
//   · 页面模块拆 mount（一次建 DOM/绑事件/建图表）+ update（数据变化时
//     setOption/innerHTML 增量填充，图表缩放与 hover 状态保留）。
//   · 轮询先拉 /api/meta 拼数据指纹；指纹不变 → 完全跳过 update（交互
//     零打断）；变化 → 仅数据层刷新。
//   · worker 样本走增量缓存（after_ms 游标，每轮只传新增样本）——
//     传输量从 O(总样本) 降为 O(新增)；run 切换（run_start_ms 变化）
//     自动清缓存。
//   · run 结束（run_end_ms 存在）且指纹连续稳定 5 轮（15s）→ 自动停轮询，
//     header 提示；切页签或勾选自动刷新可恢复。
import { getJson, resetSamplesCache, fmtTime } from './api.js';
import { t, getLang, setLang, onLangChange } from './i18n.js';
import { getTheme, setTheme, onThemeChange } from './theme.js';
import * as overview from './pages/overview.js';
import * as workers from './pages/workers.js';
import * as tasks from './pages/tasks.js';
import * as timeline from './pages/timeline.js';
import * as dbs from './pages/dbs.js';

const PAGES = { overview, workers, tasks, timeline, dbs };
const POLL_MS = 3000;
const STABLE_ROUNDS_TO_STOP = 5;

const main = document.getElementById('main');
const nav = document.getElementById('nav');
const runInfo = document.getElementById('run-info');
const pollState = document.getElementById('poll-state');
const pollEnabled = document.getElementById('poll-enabled');
const langSel = document.getElementById('lang-sel');
const themeSel = document.getElementById('theme-sel');

let current = null;      // 当前页状态 { mod, ctx }
let pollTimer = null;
let lastFp = null;
let stableRounds = 0;
let lastRunStart = null;  // run 切换检测（清样本增量缓存）

function updateHeader(meta) {
  const m = meta.meta || {};
  const start = m.run_start_ms ? fmtTime(+m.run_start_ms) : '?';
  const end = m.run_end_ms ? fmtTime(+m.run_end_ms) : t('app.running');
  runInfo.textContent = `${m.hostname || ''} · ${start} → ${end}`;
}

async function fetchFingerprint() {
  const meta = await getJson('/api/meta');
  if (!meta) return null;
  const m = meta.meta || {};
  return {
    // 任务计数/worker 数/最新样本时刻任一变化即视为数据变化。
    fp: `${m.run_end_ms || ''}|${JSON.stringify(meta.task_counts)}|` +
        `${meta.workers}|${meta.sample_hi}`,
    finished: !!m.run_end_ms,
    meta,
  };
}

async function pollTick(keepScroll = true) {
  const info = await fetchFingerprint();
  if (!info) return;
  updateHeader(info.meta);
  if (info.fp === lastFp) {
    stableRounds++;
    if (info.finished && stableRounds >= STABLE_ROUNDS_TO_STOP) {
      stopPolling(t('app.pollStopped'));
    }
    return;
  }
  // run 切换（新 run 的 run_start_ms 不同，或旧 run 数据被替换）：
  // 样本与 timeline 的增量游标对新数据无意义，清空重建。
  const runStart = info.meta.run_start_ms;
  if (runStart && runStart !== lastRunStart) {
    if (lastRunStart !== null) {
      resetSamplesCache();
      timeline.resetTimelineCache();
    }
    lastRunStart = runStart;
  }
  lastFp = info.fp;
  stableRounds = 0;
  if (!current) return;
  // 数据刷新不重置用户滚动位置（页面/表格拖到哪里就停在哪里）。
  const scroll = keepScroll
    ? { top: main.scrollTop, left: main.scrollLeft } : { top: 0, left: 0 };
  await current.mod.update(current.ctx);
  main.scrollTop = scroll.top;
  main.scrollLeft = scroll.left;
}

function stopPolling(reason) {
  if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
  if (reason) pollState.textContent = reason;
}

function startPolling() {
  if (pollTimer) clearInterval(pollTimer);
  pollState.textContent = '';
  if (pollEnabled.checked) {
    pollTimer = setInterval(pollTick, POLL_MS);
  }
}

function switchPage(name, keepCtx) {
  if (current && current.mod.destroy) current.mod.destroy(current.ctx);
  for (const b of nav.children) b.classList.toggle('active', b.dataset.page === name);
  main.innerHTML = '';
  current = { mod: PAGES[name], ctx: { main, workerId: null, taskId: null,
                                       taskFilter: {}, ...keepCtx } };
  current.mod.mount(current.ctx);   // DOM/事件/图表实例一次建好
  lastFp = null;                    // 新页强制首刷
  stableRounds = 0;
  pollTick();
}

// 页内导航（worker/task 详情、返回、过滤/翻页）：改 ctx 状态后立即刷一次；
// 主动导航不保留滚动（回顶部），轮询刷新才保留。
export function navigate() {
  lastFp = null;
  pollTick(false);
}

// 跨页跳转（带返回）：gotoPage('workers', {workerId: 2}) 从 Timeline 泳道
// 跳入 worker 详情；源页模块在 destroy 时自存状态（缩放/排序/滚动），返回
// 时 mount 恢复。backTarget 驱动页面顶部的「返回上一页」条。
let backTarget = null;   // { page, label }（源页状态由源页模块自存）

export function gotoPage(name, opts, backLabel) {
  if (current && backLabel) {
    backTarget = { page: current.mod === PAGES.timeline ? 'timeline'
                    : current.mod === PAGES.workers ? 'workers'
                    : current.mod === PAGES.tasks ? 'tasks'
                    : current.mod === PAGES.dbs ? 'dbs' : 'overview',
                   label: backLabel };
  }
  switchPage(name);
  if (opts && current) Object.assign(current.ctx, opts);
  renderBackBar();
}

function renderBackBar() {
  document.getElementById('back-bar')?.remove();
  if (!backTarget) return;
  const bar = document.createElement('div');
  bar.id = 'back-bar';
  bar.className = 'back-bar';
  bar.innerHTML = `<span class="back-link">← ${backTarget.label ? t('app.backTo', backTarget.label) : t('app.back')}</span>`;
  bar.querySelector('.back-link').onclick = () => {
    const target = backTarget;
    backTarget = null;
    document.getElementById('back-bar')?.remove();
    if (target) switchPage(target.page);
  };
  main.prepend(bar);
}

nav.addEventListener('click', (e) => {
  const btn = e.target.closest('button[data-page]');
  if (btn) {
    backTarget = null;   // 页签切换清返回栈
    document.getElementById('back-bar')?.remove();
    switchPage(btn.dataset.page);
  }
});

// 错误信息 pin：驻留浮窗显示完整内容（再次点击收起）。
document.addEventListener('click', (e) => {
  const btn = e.target.closest('.pin-btn');
  if (!btn) return;
  e.stopPropagation();
  const box = btn.parentElement?.querySelector('.err-full');
  if (!box) return;
  if (box.style.display === 'none') {
    box.textContent = btn.parentElement?.querySelector('.err-brief')?.title || '';
    box.style.display = '';
  } else {
    box.style.display = 'none';
  }
});

// 超长名称的展开/收起（全局委托一次；缩略/全名切换不触发其它点击逻辑）。
document.addEventListener('click', (e) => {
  const el = e.target.closest('.expando');
  if (!el) return;
  if (el.classList.contains('open')) {
    el.classList.remove('open');
    el.textContent = el.dataset.short || el.textContent;
  } else {
    el.dataset.short = el.textContent;
    el.classList.add('open');
    el.textContent = el.dataset.full;  // textContent 赋值，天然免疫注入
  }
});

pollEnabled.addEventListener('change', startPolling);

// ---- 语言 / 主题（用户裁定：双语可切换默认中文；浅色/深色/跟随系统）----
// header 静态文案（nav 总览按钮、自动刷新 label）与两个下拉由这里统一
// 填充——语言切换后随 rerender 重新填充。语言选项显示语言自身名（中文/
// English，不随 UI 语言变化），主题选项随 UI 语言。
function fillHeaderControls() {
  const ovBtn = nav.querySelector('button[data-page="overview"]');
  if (ovBtn) ovBtn.textContent = t('nav.overview');
  document.getElementById('poll-label').textContent = t('hdr.autoRefresh');
  langSel.innerHTML = `<option value="zh">${t('lang.zh')}</option>` +
                      `<option value="en">${t('lang.en')}</option>`;
  langSel.value = getLang();
  themeSel.innerHTML = ['light', 'dark', 'system']
    .map(m => `<option value="${m}">${t('theme.' + m)}</option>`).join('');
  themeSel.value = getTheme();
}

// 语言/主题切换 → 整页重建：mount 模板文案取新语言、图表实例随 mount
// 新建读取新主题的 CSS 变量。当前页上下文（worker/task 详情、过滤条件）
// 拷贝注入新 ctx——用户停在详情页切换语言后不弹回列表；各页面模块自身
// 的 savedState（timeline 缩放/排序等）由 destroy/mount 机制恢复。
function rerender() {
  fillHeaderControls();
  if (!current) return;
  let name = 'overview';
  for (const [n, m] of Object.entries(PAGES)) {
    if (m === current.mod) { name = n; break; }
  }
  const keep = {
    workerId: current.ctx.workerId,
    taskId: current.ctx.taskId,
    taskFilter: { ...current.ctx.taskFilter },
  };
  switchPage(name, keep);
}

langSel.addEventListener('change', () => setLang(langSel.value));
themeSel.addEventListener('change', () => setTheme(themeSel.value));
onLangChange(rerender);
onThemeChange(rerender);

fillHeaderControls();
switchPage('overview');
startPolling();
