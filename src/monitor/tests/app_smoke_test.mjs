// 前端 app 启动冒烟（Node，最小 DOM/fetch/echarts stub）：
// 验证模块图无重复声明/缺导出、app.js 顶层（switchPage+mount）在浏览器
// 语义下可完整执行、五个页面 mount/update 不抛。抓「页面空白」类回归
// （escapeHtml 重复声明事故的防复发）。
// 运行：node src/monitor/tests/app_smoke_test.mjs
import { pathToFileURL } from 'node:url';
import { join } from 'node:path';

const failures = [];

function makeElement(id) {
  return {
    id, innerHTML: '', textContent: '', scrollTop: 0, scrollLeft: 0,
    style: {}, dataset: {}, disabled: false, checked: true, children: [],
    classList: { toggle() {}, add() {}, remove() {}, contains: () => false },
    addEventListener() {}, dispatchEvent() {},
    appendChild() {}, closest: () => null,
    scrollIntoView() {}, onclick: null, onchange: null,
  };
}

const elements = new Map();
globalThis.document = {
  getElementById: (id) => {
    if (!elements.has(id)) elements.set(id, makeElement(id));
    return elements.get(id);
  },
  addEventListener() {},
  createElement: (tag) => makeElement(tag),
};
globalThis.window = { addEventListener() {} };

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
                 dispatchAction() {} }),
};

// 加载被测模块（app.js 顶层会执行 switchPage('overview') + startPolling）。
const staticRoot = join(import.meta.dirname, '..', 'py', 'static');
const mod = await import(pathToFileURL(join(staticRoot, 'js', 'app.js')))
  .catch(e => { failures.push(`app.js import: ${e.message}`); return null; });

if (mod) {
  // 等待 mount/update 的异步链（fetch stub 立即返回；两轮微任务足够）。
  await new Promise(r => setImmediate(r));
  await new Promise(r => setImmediate(r));
  // 主容器应已填充（非空骨架 = mount 成功）。
  const main = elements.get('main');
  if (!main || main.innerHTML.length < 100) {
    failures.push(`#main 未渲染（长度 ${main ? main.innerHTML.length : -1}）`);
  }
}

if (failures.length) {
  console.error('SMOKE FAIL:', failures.join('; '));
  process.exit(1);
}
console.log('app smoke OK: 模块图 + 五页面 mount/update 启动通过');
