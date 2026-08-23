// i18n：中文（默认）/ 英文双语文典。
//   · t(key, ...args)：查当前语言字典；缺 key 回退中文，中文也缺则显示
//     key 本身（开发期肉眼可见漏配）；{0} {1} 位置占位替换。
//   · 语言持久化 localStorage（fly-monitor-lang）；切换经 onLangChange
//     订阅——app.js 联动整页重建（mount 模板文案在重建时取新语言，
//     update 内的动态文案每次渲染都经 t() 实时取）。
//   · <html lang> 同步（zh-CN / en）。
const ZH = {
  // ---- header / 主题语言控件 ----
  'nav.overview': '总览',
  'hdr.autoRefresh': '自动刷新(3s)',
  'lang.zh': '中文',
  'lang.en': 'English',
  'theme.light': '浅色',
  'theme.dark': '深色',
  'theme.system': '跟随系统',

  // ---- app 壳 ----
  'app.running': '进行中',
  'app.now': '现在',
  'app.pollStopped': 'run 已结束 · 轮询已停止',
  'app.back': '返回上一页',
  'app.backTo': '返回{0}',

  // ---- 公共 ----
  'name.expandToggle': '点击展开/收起全名',
  'err.pin': '驻留显示完整错误',
  'tb.id': 'ID',
  'tb.name': '名称',
  'tb.status': '状态',
  'tb.duration': '运行时长',
  'tb.time': '时间',
  'tb.category': '类别',
  'tb.event': '事件',
  'tb.detail': '详情',

  // ---- worker 终态语义 ----
  'ev.exited': '正常退出',
  'ev.died': '异常死亡',
  'ev.exitedTitle': '收到关停指令后正常退出',
  'ev.diedTitle': '无关停指令先行：心跳超时/宽限耗尽判死',

  // ---- Overview ----
  'ov.durationRunning': '运行时长（进行中）',
  'ov.duration': '运行时长',
  'ov.tasksTotal': 'Tasks 总数',
  'ov.tasksSub': '完成 {0} · 失败 {1} · 运行 {2}',
  'ov.samples': '样本数',
  'ov.sampling': '采样中',
  'ov.rss': '集群聚合 RSS',
  'ov.cpu': '集群聚合 CPU（进程 vs 机器）',
  'ov.net': '集群聚合网络 IO 速率（读/写）',
  'ov.disk': '磁盘 IO 速率',
  'ov.diskPending': '磁盘 IO 监控暂未支持（待后续增强）',
  'ov.recentEvents': '最近事件',

  // ---- Workers ----
  'w.role': '角色',
  'w.procRssCpu': '进程 RSS / CPU',
  'w.hostCpu': '机器 CPU',
  'w.attrs': '属性',
  'w.back': '← 返回 worker 列表',
  'w.cpu': 'CPU（进程 vs 机器）',
  'w.mem': '内存（进程 vs 机器）',
  'w.net': '网络 IO 速率（读/写）',
  'w.load': '机器平均负载（Load1）',
  'w.loadHint': 'Load1：1 分钟平均可运行任务数（含等 IO），健康参考值 ≈ CPU 核数',
  'w.latestRss': '最新进程 RSS',
  'w.latestCpu': '最新进程 CPU',
  'w.netTotal': '网络累计 读/写',
  'w.tasksOf': 'worker {0} 的 tasks（{1}）',
  'w.masterTasksOf': 'master 的 tasks（{0}）',
  'tb.cpuCol': 'CPU',
  'tb.rwTime': '读/写时间',
  'tb.memAvgPeak': 'avg/peak 内存',

  // ---- Tasks ----
  't.searchPh': '搜索 task 名称…',
  't.allStatus': '全部状态',
  't.allWorkers': '全部 worker',
  'tb.created': '创建时间',
  'tb.started': '开始时间',
  'tb.finished': '结束时间',
  'tb.queueWait': '排队等待',
  'tb.cpuTime': 'CPU 耗时',
  't.cpuIoShare': 'CPU / IO 占比',
  't.cpuIoShareTitle': '运行时长中 CPU/IO 占比',
  't.readBT': '读(字节/时间)',
  't.writeBT': '写(字节/时间)',
  't.memAvgPeakCol': '内存 avg / peak',
  't.dbsCol': '关联 db',
  't.prev': '上一页',
  't.next': '下一页',
  't.totalN': '共 {0} 条',
  't.back': '← 返回任务列表',
  't.notFound': 'task 不存在',
  'kv.module': '模块',
  'kv.priority': '优先级',
  'kv.depsReady': '依赖就绪',
  'kv.error': '错误',
  't.resIo': '资源 / IO',
  'kv.cpuShare': 'CPU 占比',
  'kv.readTime': 'IO 读耗时',
  'kv.readBytes': 'IO 读字节',
  'kv.writeTime': 'IO 写耗时',
  'kv.writeBytes': 'IO 写字节',
  'kv.memBase': '内存基线',
  'kv.memAvg': '内存平均',
  'kv.memAvgDelta': '内存平均增量',
  'kv.memPeak': '内存峰值',
  'kv.memPeakDelta': '内存峰值增量',
  'kv.dbs': '关联 db',
  't.eventStream': '事件流',
  't.objIo': '对象 IO 明细（{0}）',
  'tb.direction': '方向',
  'tb.object': '对象',
  'tb.bytes': '字节',
  'tb.elapsed': '耗时',
  'd.write': '写',
  'd.read': '读',

  // ---- Timeline ----
  'tl.sortTitle': '泳道排序',
  'tl.sortId': '按 worker id 排序',
  'tl.sortHost': '按 host 排序',
  'tl.dimTitle': '主色维度',
  'tl.dim': '主色维度：{0}',
  'tl.stripeOn': '条纹：开',
  'tl.stripeOff': '条纹：关',
  'tl.resetZoom': '↺ 复原缩放',
  'tl.sliderTitle': '时间范围选取：选区内拖动平移 · 边缘拖动调宽 · 选区外拖动框选',
  'tl.hint': '滚轮缩放 · 图内拖动框选时间段 · 点击条形看负载详情 · 点击泳道标签跳转 Worker',
  'tl.view': '视图：{0} → {1}（{2}%）',
  'tl.viewFull': '视图：{0} → {1}（全程）',
  'tl.laneTip': '点击查看 Worker {0} 详情',
  'tl.legendHigh': '{0} ≥50%',
  'tl.legendMid': '10–50%',
  'tl.legendLow': '<10%',
  'tl.legendCompound': '复合（其它维度 ≥30%，条纹开启时）',
  'tl.legendFast': 'Fast <500ms',
  'tl.legendFailed': 'Failed（红描边）',
  'tl.ttLoad': '排队 {0} · CPU {1} · IO {2} · 空闲 {3}',
  'tl.fastTask': 'Fast Task',
  'tl.ttClickDetail': '点击条形查看负载详情',
  'tl.depsReadyAt': '依赖就绪时间',
  'tl.dispatchAt': '开始调度时间',
  'tl.execStartAt': '执行开始时间',
  'tl.execEndAt': '执行结束时间',
  'tl.completedAt': '完成确认时间',
  'tl.queueWaitMs': '排队等待时长',
  'tl.queueLifeShare': '排队占生命周期比',
  'tl.ioShare': 'IO 占比',
  'tl.idleShare': '空闲占比',
  'tl.idleShareNote': '（执行窗口内非 CPU 非 IO）',

  // ---- DBs ----
  'db.path': 'db 路径',
  'tb.frozen': '冻结时间',
  'tb.diskUsage': '磁盘占用',
  'db.hint': '磁盘占用：freeze 时为终值；未冻结 db 为 run 结束时的占用（- 表示未测得）。',
  'db.notFrozen': '未冻结',
  'db.empty': '无 db（run 中未使用 Database）',
};

const EN = {
  'nav.overview': 'Overview',
  'hdr.autoRefresh': 'Auto refresh (3s)',
  'lang.zh': '中文',
  'lang.en': 'English',
  'theme.light': 'Light',
  'theme.dark': 'Dark',
  'theme.system': 'System',

  'app.running': 'In progress',
  'app.now': 'now',
  'app.pollStopped': 'Run finished · polling stopped',
  'app.back': 'Back',
  'app.backTo': 'Back to {0}',

  'name.expandToggle': 'Click to expand/collapse full name',
  'err.pin': 'Pin to show full error',
  'tb.id': 'ID',
  'tb.name': 'Name',
  'tb.status': 'Status',
  'tb.duration': 'Duration',
  'tb.time': 'Time',
  'tb.category': 'Category',
  'tb.event': 'Event',
  'tb.detail': 'Detail',

  'ev.exited': 'Exited Normally',
  'ev.died': 'Died',
  'ev.exitedTitle': 'Exited normally after shutdown command',
  'ev.diedTitle': 'No shutdown command first: judged dead by heartbeat timeout / grace expiry',

  'ov.durationRunning': 'Duration (running)',
  'ov.duration': 'Duration',
  'ov.tasksTotal': 'Total Tasks',
  'ov.tasksSub': 'Completed {0} · Failed {1} · Running {2}',
  'ov.samples': 'Samples',
  'ov.sampling': 'Sampling',
  'ov.rss': 'Cluster Aggregate RSS',
  'ov.cpu': 'Cluster CPU (Process vs Host)',
  'ov.net': 'Cluster Network IO Rate (Read/Write)',
  'ov.disk': 'Disk IO Rate',
  'ov.diskPending': 'Disk IO monitoring not yet supported (future enhancement)',
  'ov.recentEvents': 'Recent Events',

  'w.role': 'Role',
  'w.procRssCpu': 'Proc RSS / CPU',
  'w.hostCpu': 'Host CPU',
  'w.attrs': 'Attributes',
  'w.back': '← Back to Workers',
  'w.cpu': 'CPU (Process vs Host)',
  'w.mem': 'Memory (Process vs Host)',
  'w.net': 'Network IO Rate (Read/Write)',
  'w.load': 'Host Load Average (Load1)',
  'w.loadHint': 'Load1: average runnable tasks (incl. IO wait) in 1 min; healthy reference ≈ CPU cores',
  'w.latestRss': 'Latest Proc RSS',
  'w.latestCpu': 'Latest Proc CPU',
  'w.netTotal': 'Network Total Read/Write',
  'w.tasksOf': 'Tasks of worker {0} ({1})',
  'w.masterTasksOf': 'Tasks of master ({0})',
  'tb.cpuCol': 'CPU',
  'tb.rwTime': 'Read/Write Time',
  'tb.memAvgPeak': 'Avg/Peak Memory',

  't.searchPh': 'Search task name…',
  't.allStatus': 'All Statuses',
  't.allWorkers': 'All Workers',
  'tb.created': 'Created',
  'tb.started': 'Started',
  'tb.finished': 'Finished',
  'tb.queueWait': 'Queue Wait',
  'tb.cpuTime': 'CPU Time',
  't.cpuIoShare': 'CPU / IO Share',
  't.cpuIoShareTitle': 'CPU/IO share of duration',
  't.readBT': 'Read (Bytes/Time)',
  't.writeBT': 'Write (Bytes/Time)',
  't.memAvgPeakCol': 'Memory Avg / Peak',
  't.dbsCol': 'Related DBs',
  't.prev': 'Prev',
  't.next': 'Next',
  't.totalN': '{0} total',
  't.back': '← Back to Tasks',
  't.notFound': 'Task not found',
  'kv.module': 'Module',
  'kv.priority': 'Priority',
  'kv.depsReady': 'Deps Ready',
  'kv.error': 'Error',
  't.resIo': 'Resources / IO',
  'kv.cpuShare': 'CPU Share',
  'kv.readTime': 'IO Read Time',
  'kv.readBytes': 'IO Read Bytes',
  'kv.writeTime': 'IO Write Time',
  'kv.writeBytes': 'IO Write Bytes',
  'kv.memBase': 'Memory Baseline',
  'kv.memAvg': 'Memory Avg',
  'kv.memAvgDelta': 'Memory Avg Delta',
  'kv.memPeak': 'Memory Peak',
  'kv.memPeakDelta': 'Memory Peak Delta',
  'kv.dbs': 'Related DBs',
  't.eventStream': 'Events',
  't.objIo': 'Object IO Details ({0})',
  'tb.direction': 'Direction',
  'tb.object': 'Object',
  'tb.bytes': 'Bytes',
  'tb.elapsed': 'Duration',
  'd.write': 'Write',
  'd.read': 'Read',

  'tl.sortTitle': 'Sort Lanes',
  'tl.sortId': 'By Worker ID',
  'tl.sortHost': 'By Host',
  'tl.dimTitle': 'Primary Dimension',
  'tl.dim': 'Primary: {0}',
  'tl.stripeOn': 'Stripes: On',
  'tl.stripeOff': 'Stripes: Off',
  'tl.resetZoom': '↺ Reset Zoom',
  'tl.sliderTitle': 'Time range: drag inside selection to pan · drag edges to resize · drag outside to select new range',
  'tl.hint': 'Wheel to zoom · drag in chart to select range · click bar for load details · click lane label to open worker',
  'tl.view': 'View: {0} → {1} ({2}%)',
  'tl.viewFull': 'View: {0} → {1} (full)',
  'tl.laneTip': 'Click to view worker {0} details',
  'tl.legendHigh': '{0} ≥50%',
  'tl.legendMid': '10–50%',
  'tl.legendLow': '<10%',
  'tl.legendCompound': 'Compound (other dims ≥30%, with stripes on)',
  'tl.legendFast': 'Fast <500ms',
  'tl.legendFailed': 'Failed (red border)',
  'tl.ttLoad': 'Queue {0} · CPU {1} · IO {2} · Idle {3}',
  'tl.fastTask': 'Fast Task',
  'tl.ttClickDetail': 'Click bar for load details',
  'tl.depsReadyAt': 'Deps Ready At',
  'tl.dispatchAt': 'Dispatched At',
  'tl.execStartAt': 'Exec Start',
  'tl.execEndAt': 'Exec End',
  'tl.completedAt': 'Completed At',
  'tl.queueWaitMs': 'Queue Wait',
  'tl.queueLifeShare': 'Queue Share of Lifecycle',
  'tl.ioShare': 'IO Share',
  'tl.idleShare': 'Idle Share',
  'tl.idleShareNote': '(non-CPU non-IO within exec window)',

  'db.path': 'DB Path',
  'tb.frozen': 'Frozen',
  'tb.diskUsage': 'Disk Usage',
  'db.hint': "Disk usage: final value at freeze; for unfrozen DBs, measured at run end ('-' = not measured).",
  'db.notFrozen': 'Not Frozen',
  'db.empty': 'No DBs (Database unused in this run)',
};

let lang = 'zh';
if (typeof localStorage !== 'undefined') {
  lang = localStorage.getItem('fly-monitor-lang') === 'en' ? 'en' : 'zh';
}

const listeners = new Set();

export function t(key, ...args) {
  const dict = lang === 'en' ? EN : ZH;
  let s = dict[key] ?? ZH[key] ?? key;
  for (let i = 0; i < args.length; i++) {
    s = s.split(`{${i}}`).join(String(args[i]));
  }
  return s;
}

export function getLang() {
  return lang;
}

export function setLang(l) {
  if (l !== 'zh' && l !== 'en') return;
  if (l === lang) return;
  lang = l;
  if (typeof localStorage !== 'undefined') {
    localStorage.setItem('fly-monitor-lang', l);
  }
  if (typeof document !== 'undefined' && document.documentElement) {
    document.documentElement.lang = l === 'en' ? 'en' : 'zh-CN';
  }
  listeners.forEach(fn => fn());
}

export function onLangChange(fn) {
  listeners.add(fn);
}

// 字典 key 集合（冒烟测试断言双语完整一致，防漏译）。
export function dictKeys() {
  return { zh: Object.keys(ZH).sort(), en: Object.keys(EN).sort() };
}
