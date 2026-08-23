// 前端 app 启动冒烟（Node，最小 DOM/fetch/echarts stub）：
// 验证模块图无重复声明/缺导出、app.js 顶层（switchPage+mount）在浏览器
// 语义下可完整执行、五个页面 mount/update 不抛。抓「页面空白」类回归
// （escapeHtml 重复声明事故的防复发）。
// 另覆盖 i18n/主题：中英字典 key 完整一致（防漏译）、语言/主题切换触发
// 整页重渲染（rerender→switchPage→mount）不抛且文案随语言更新。
// 运行：node src/monitor/tests/app_smoke_test.mjs
import { pathToFileURL } from 'node:url';
import { join } from 'node:path';
import { readFileSync, readdirSync } from 'node:fs';

const failures = [];

function makeElement(id) {
  const el = {
    id, innerHTML: '', textContent: '', value: '', scrollTop: 0, scrollLeft: 0,
    style: {}, dataset: {}, disabled: false, checked: true, children: [],
    classList: { toggle() {}, add() {}, remove() {}, contains: () => false },
    addEventListener() {}, dispatchEvent() {},
    appendChild() {}, closest: () => null, querySelector: () => null,
    scrollIntoView() {}, onclick: null, onchange: null,
    parentElement: null,
  };
  el.parentElement = { addEventListener() {} };
  return el;
}

const elements = new Map();
globalThis.document = {
  documentElement: { dataset: {}, lang: '' },
  getElementById: (id) => {
    if (!elements.has(id)) elements.set(id, makeElement(id));
    return elements.get(id);
  },
  querySelector: (sel) => elements.get(sel.replace(/^#/, '')) || null,
  addEventListener() {},
  createElement: (tag) => makeElement(tag),
};
globalThis.window = {
  addEventListener() {},
  matchMedia: () => ({ matches: false, addEventListener() {} }),
};
// localStorage/getComputedStyle stub（i18n/theme 持久化与 CSS 变量读取）。
globalThis.localStorage = {
  store: new Map(),
  getItem(k) { return this.store.has(k) ? this.store.get(k) : null; },
  setItem(k, v) { this.store.set(k, v); },
};
globalThis.getComputedStyle = () => ({ getPropertyValue: () => '' });

// fetch stub：按 URL 返回各 API 的最小合法 JSON。
const apiData = {
  '/api/meta': { meta: { run_start_ms: 1, run_end_ms: 2, hostname: 'h' },
                 task_counts: { COMPLETED: 1 }, workers: 1, sample_lo: 1, sample_hi: 2 },
  '/api/events': { events: [{ epoch_ms: 1, category: 'run', event: 'DRAIN_DONE',
                              worker_id: 0, task_id: 0, detail: '' }] },
  '/api/workers': { workers: [{ worker_id: 1, hostname: 'h1', ip: '1.2.3.4',
                                role: 'hybrid', attributes: '', first_seen_ms: 1,
                                last_event_ms: 2, last_event: 'REGISTER',
                                latest: { epoch_ms: 2, proc_rss_bytes: 1,
                                          proc_cpu_bps: 0, host_cpu_bps: 0,
                                          net_read_bytes: 0, net_write_bytes: 0 } }] },
  '/api/workers/1/samples': { worker_id: 1, samples: [
    { epoch_ms: 1000, proc_rss_bytes: 1, proc_cpu_bps: 0, host_cpu_bps: 0,
      host_mem_total_bytes: 1, host_mem_avail_bytes: 1, host_load1_x100: 0,
      net_read_bytes: 0, net_write_bytes: 0 },
    { epoch_ms: 2000, proc_rss_bytes: 1, proc_cpu_bps: 0, host_cpu_bps: 0,
      host_mem_total_bytes: 1, host_mem_avail_bytes: 1, host_load1_x100: 0,
      net_read_bytes: 5, net_write_bytes: 5 }] },
  '/api/tasks': { total: 1, tasks: [{ task_id: 1, name: 'n', status: 'COMPLETED',
      module: 'm', worker_id: 1, priority: 10, error: '', created_ms: 1,
      ready_ms: 1, started_ms: 1, completed_ms: 2, exec_start_ms: 1,
      exec_end_ms: 2, cpu_time_ms: 1, read_time_ms: 0, write_time_ms: 0,
      read_bytes: 0, write_bytes: 0, mem_baseline_bytes: 1, mem_avg_bytes: 1,
      mem_peak_bytes: 1, dbs: '' }] },
  '/api/tasks/1': { task: { task_id: 1, name: 'n', status: 'COMPLETED', module: 'm',
      worker_id: 1, priority: 10, error: '', created_ms: 1, ready_ms: 1,
      started_ms: 1, completed_ms: 2, exec_start_ms: 1, exec_end_ms: 2,
      cpu_time_ms: 1, read_time_ms: 0, write_time_ms: 0, read_bytes: 0,
      write_bytes: 0, mem_baseline_bytes: 1, mem_avg_bytes: 1,
      mem_peak_bytes: 1, dbs: '' }, events: [], io: [] },
  '/api/timeline': { tasks: [{ task_id: 1, name: 'n', status: 'COMPLETED',
      worker_id: 1, exec_start_ms: 1, exec_end_ms: 2, started_ms: 1,
      completed_ms: 2 }] },
  '/api/dbs': { dbs: [{ db: '/tmp/x.db', created_ms: 1, frozen_ms: null,
                        disk_bytes: 1024 }] },
};
globalThis.fetch = async (url) => {
  const key = String(url).split('?')[0].replace(/\/\d+\/samples$/, '/1/samples');
  const data = apiData[key] ?? {};
  return { ok: true, json: async () => data };
};

globalThis.echarts = {
  init: () => ({ setOption() {}, dispose() {}, on() {}, getZr: () => ({ on() {} }),
                 getOption: () => ({ dataZoom: [{}] }),
                 dispatchAction() {}, resize() {} }),
};

const staticRoot = join(import.meta.dirname, '..', 'py', 'static');

// ---- 静态完整性：CSS 双主题变量集合一致（漏定义浅色值会回退深色）----
{
  const css = readFileSync(join(staticRoot, 'css', 'app.css'), 'utf8');
  const varsOf = (block) =>
    [...block.matchAll(/--([a-z0-9-]+)\s*:/gi)].map(m => m[1]).sort();
  const dark = varsOf(css.slice(0, css.indexOf('[data-theme="light"]')));
  const light = varsOf(css.slice(css.indexOf('[data-theme="light"]')));
  const missLight = dark.filter(v => !light.includes(v));
  const missDark = light.filter(v => !dark.includes(v));
  if (missLight.length) failures.push(`浅色主题缺变量: ${missLight.join(',')}`);
  if (missDark.length) failures.push(`深色主题缺变量: ${missDark.join(',')}`);
}

// ---- 静态完整性：全部 t('字面量') 调用的 key 双语字典可查 ----
{
  const { dictKeys } = await import(pathToFileURL(join(staticRoot, 'js', 'i18n.js')));
  const { zh } = dictKeys();
  const used = new Set();
  for (const dir of ['.', 'pages']) {
    const dirPath = join(staticRoot, 'js', dir);
    for (const f of readdirSync(dirPath).filter(f => f.endsWith('.js') && f !== 'i18n.js')) {
      const src = readFileSync(join(dirPath, f), 'utf8');
      for (const m of src.matchAll(/\bt\('([^']+)'/g)) {
        if (!m[1].endsWith('.')) used.add(m[1]);  // 'theme.'+m 为动态前缀拼接
      }
    }
  }
  const missing = [...used].filter(k => !zh.includes(k));
  if (missing.length) failures.push(`t() 引用了字典不存在的 key: ${missing.join(',')}`);
}


// ---- i18n：双语字典 key 完整一致（防漏译）----
const i18n = await import(pathToFileURL(join(staticRoot, 'js', 'i18n.js')));
{
  const { zh, en } = i18n.dictKeys();
  const zhOnly = zh.filter(k => !en.includes(k));
  const enOnly = en.filter(k => !zh.includes(k));
  if (zhOnly.length) failures.push(`字典缺英文: ${zhOnly.join(',')}`);
  if (enOnly.length) failures.push(`字典缺中文: ${enOnly.join(',')}`);
}

// 加载被测模块（app.js 顶层会执行 switchPage('overview') + startPolling）。
const mod = await import(pathToFileURL(join(staticRoot, 'js', 'app.js')))
  .catch(e => { failures.push(`app.js import: ${e.message}`); return null; });

if (mod) {
  // 等待 mount/update 的异步链（fetch stub 立即返回；两轮微任务足够）。
  await new Promise(r => setImmediate(r));
  await new Promise(r => setImmediate(r));
  // 主容器应已填充（非空骨架 = mount 成功），默认中文。
  const main = elements.get('main');
  if (!main || main.innerHTML.length < 100) {
    failures.push(`#main 未渲染（长度 ${main ? main.innerHTML.length : -1}）`);
  }
  if (main && !main.innerHTML.includes('集群聚合 RSS')) {
    failures.push('默认语言应为中文（未找到中文面板标题）');
  }

  // ---- 语言切换：rerender 整页重建，文案变英文 ----
  i18n.setLang('en');
  await new Promise(r => setImmediate(r));
  if (main && !main.innerHTML.includes('Cluster Aggregate RSS')) {
    failures.push('切英文后未找到英文面板标题');
  }
  if (i18n.getLang() !== 'en') failures.push('getLang 应为 en');
  if (localStorage.getItem('fly-monitor-lang') !== 'en') {
    failures.push('语言未持久化到 localStorage');
  }
  i18n.setLang('zh');
  await new Promise(r => setImmediate(r));
  if (main && !main.innerHTML.includes('集群聚合 RSS')) {
    failures.push('切回中文后未找到中文面板标题');
  }
}

// ---- 主题：setTheme 三态 + data-theme 落地 + 持久化 ----
{
  const theme = await import(pathToFileURL(join(staticRoot, 'js', 'theme.js')));
  theme.setTheme('light');
  if (document.documentElement.dataset.theme !== 'light') {
    failures.push(`light 模式下 data-theme 应为 light（实际 ${document.documentElement.dataset.theme}）`);
  }
  if (localStorage.getItem('fly-monitor-theme') !== 'light') {
    failures.push('主题未持久化到 localStorage');
  }
  // 跟随系统：stub matches=false → light。
  theme.setTheme('system');
  if (theme.resolvedTheme() !== 'light') {
    failures.push(`system 模式应解析为 light（实际 ${theme.resolvedTheme()}）`);
  }
  theme.setTheme('dark');
  if (document.documentElement.dataset.theme !== 'dark') {
    failures.push('dark 模式下 data-theme 应为 dark');
  }
}

if (failures.length) {
  console.error('SMOKE FAIL:', failures.join('; '));
  process.exit(1);
}

// ---- 五页面 × 双语：直接 mount/update 断言关键文案随语言切换 ----
{
  const pages = {
    overview: { mod: await import(pathToFileURL(join(staticRoot, 'js', 'pages', 'overview.js'))),
                probe: ['集群聚合 RSS', 'Cluster Aggregate RSS'] },
    workers: { mod: await import(pathToFileURL(join(staticRoot, 'js', 'pages', 'workers.js'))),
               probe: null },   // 列表视图无静态标题，详情标题经 update 填充
    tasks: { mod: await import(pathToFileURL(join(staticRoot, 'js', 'pages', 'tasks.js'))),
             probe: ['上一页', 'Prev'] },
    timeline: { mod: await import(pathToFileURL(join(staticRoot, 'js', 'pages', 'timeline.js'))),
                probe: ['复原缩放', 'Reset Zoom'] },
    dbs: { mod: await import(pathToFileURL(join(staticRoot, 'js', 'pages', 'dbs.js'))),
           probe: ['磁盘占用', 'Disk Usage'] },
  };
  const mountAndUpdate = async (spec) => {
    const el = makeElement('page-main-' + Math.random());
    const ctx = { main: el, workerId: null, taskId: null, taskFilter: {} };
    spec.mod.mount(ctx);
    await spec.mod.update(ctx);
    return el.innerHTML;
  };
  i18n.setLang('zh');
  await new Promise(r => setImmediate(r));
  const zhHtml = {};
  for (const [name, spec] of Object.entries(pages)) {
    zhHtml[name] = await mountAndUpdate(spec);
    if (spec.probe && !zhHtml[name].includes(spec.probe[0])) {
      failures.push(`${name} 页中文渲染缺「${spec.probe[0]}」`);
    }
  }
  i18n.setLang('en');
  await new Promise(r => setImmediate(r));
  for (const [name, spec] of Object.entries(pages)) {
    const html = await mountAndUpdate(spec);
    if (spec.probe && !html.includes(spec.probe[1])) {
      failures.push(`${name} 页英文渲染缺「${spec.probe[1]}」`);
    }
  }
}

if (failures.length) {
  console.error('SMOKE FAIL:', failures.join('; '));
  process.exit(1);
}
console.log('app smoke OK: 模块图 + 五页面双语渲染 + i18n 完整性 + 主题切换重渲染通过');
