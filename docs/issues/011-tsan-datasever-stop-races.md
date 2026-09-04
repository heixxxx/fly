# 011 — TSAN 首轮扫描发现：DataServer::stop 与内部线程的数据竞争（已根治关闭）

> 状态：**已根治关闭（2026-09-05，M0-M5 六里程碑全部落地）**。
> 本文档 = 双栈取证 + 根因 + 分层修复方案 + fd 所有权改造设计评审稿 + 实施方案。
> 来源：P3-17 批 3 TSAN 工具链落地后的首轮扫描。
> 修复前复现：`./fly.sh test --config=tsan //src/storage/tests:data_service_test`
> 每轮 2 条 ThreadSanitizer 警告（用例本身 104/104 全过）；修复后 TSAN 全
> 并发目标零警告。
> 关联：commit 188d3c7（fd 代际校验，已被 M2 句柄语义替代退役）、
> e9815e9（TSAN 工具链）。
> 实施 commit：25e6c47（M0 stop 时序）/ a5025ad（M1 FdHandle 原语）/
> b12362f（M2 DataServer 迁移）/ 7b4f6a0（M3 TcpConnectionManager 迁移）/
> f8f3fa6（M4 连接池/网络分块数据源迁移）。

---

## 一、取证结论（双栈与代码互证）

ThreadSanitizer 两份报告的读写双方，与源码逐一核对后确认为同一根因的两面：
**`DataServer::stop()` 在内部线程仍存活时，无锁地写入 `listen_fd_` /
`epoll_fd_` 并关闭对应文件描述符，而 epoll 线程同时无锁地读取这两个成员。**

| 竞态 | 写方（主线程） | 读方（epoll 线程 T10） | 竞争字段 |
|---|---|---|---|
| 报告一（8 字节写） | `stop()` → `EpollMultiplexerImpl::destroy(epoll_fd_)` → `close()`（data_server.cpp:106-109） | `epoll_loop()` 读 `epoll_fd_` 作 `epoll_wait` 参数（data_server.cpp:167） | `epoll_fd_` |
| 报告二（4 字节写） | `stop()` 关 `listen_fd_` 并置 -1（data_server.cpp:101-104） | `epoll_loop()` 的 accept 分支读 `listen_fd_`（data_server.cpp:177） | `listen_fd_`（`int`） |

**为什么这不只是形式上的未定义行为，而是真实危险**：`stop()` 的时序是
「先关闭 listen 监听描述符与 epoll 实例描述符 → 再 join 线程」（data_server.cpp:101-119）。
`epoll_wait` 阻塞在正被 `close()` 的 epoll 描述符上时，最好情况是立刻返回
EBADF（当前 100ms 超时轮询掩盖了大部分后果）；最坏情况是该描述符编号在此
瞬间被同进程其它线程新建的文件描述符复用，`epoll_wait` 会阻塞在**别人的**
描述符上——这是与 commit 188d3c7 所修「滞留发送任务误发复用编号连接」同族
的文件描述符编号复用问题，只是发生在 epoll 实例描述符上。

## 二、修复方案（分两层，第 0 层直接关闭本 issue 的检测发现）

### 第 0 层：stop 时序修正（约 30 行，关闭 ThreadSanitizer 发现）

1. **调序——先停线程、后关描述符**：`stop()` 改为
   `running_.exchange(false)` → 持 `send_mutex_` 唤醒 `send_cv_` →
   **join 全部 epoll/send 线程** → 再关 `listen_fd_` → 再
   `epoll_->destroy(epoll_fd_)` → 清 `conns_` 残余。
   可行性已核实：`epoll_loop` 的 `epoll_wait` 为 100ms 超时轮询
   （data_server.cpp:167），`running_` 置假后线程最迟 100ms 自然退出，
   **无需引入 eventfd 自唤醒**。
2. **字段原子化**：`listen_fd_` / `epoll_fd_` 改 `std::atomic<int>`
   （消除数据竞争本身；join 之后写方已不存在，原子化主要为兜底
   `cleanup_fd` 等路径的无锁读，见第三章风险点 1）。
3. **验证口径**：`--config=tsan` 下 `data_service_test` 全绿且零警告
   ×10 轮；非检测构建全量测试与 qa/storage + qa/network 全绿。

### 第 1 层：fd 所有权改造（shared_ptr 包装提案的落地，独立立项，本文档第四章为设计评审稿）

第 0 层只修 stop 时序；第三章风险清单里的「发送在途文件描述符」「重复
关闭」「过期事件」等问题需要所有权模型改造，规模约 2-3 天，单独评审后实施。

---

## 三、fd 生命周期现状与风险清单（调研盘点结论）

fd 四套独立体系：控制面 Reactor（内含 TcpConnectionManager，每个实例自有
epoll）、数据面服务端 DataServer（自有 epoll，N 个 epoll 线程 + M 个发送
线程）、数据面客户端 DataClientPool（无 epoll，纯阻塞 IO + 文件描述符池）、
worker 间 PeerRpcServer（又一个独立 TcpConnectionManager）。

裸文件描述符跨线程在途使用位点（按危险度排序，文件:行证据见调研记录）：

1. **DataServer::stop 与内部线程**（本章取证的两处，第 0 层修复）。
2. **发送在途文件描述符**：`SendTask.fd` 为裸 int 值拷贝入队；现有代际
   校验（发送执行前比对代际号）存在两个缺口——(a) 校验通过到真实发送
   之间仍有时间窗口（检查后另一线程清理连接且新连接复用同号）；(b) 分块
   大传输闭包（可达分钟级）校验通过后连接死亡、编号复用，数据写到错误
   连接。
3. **`cleanup_fd` 重复关闭**：由 epoll 线程（对端关闭/帧错误）与发送线程
   （发送失败）两类线程触发，无存在性或代际防重入；第二次 close 可能落在
   复用后的新连接上。
4. **接收侧过期事件**：epoll 事件只携带裸文件描述符，`on_readable` 按同号
   即匹配——过期事件可读进复用后的新连接（流混淆），甚至误清新连接。
5. **TcpConnectionManager send 与 poll 关闭竞争**：发送线程锁内快照
   fd、锁外发送；poll 线程可并发关闭该连接。
6. **TcpConnectionManager poll 的 drain_socket 与外部关闭竞争**（含
   Reactor 关闭连接路径与 PeerRpc BYE 路径）。
7. **DataClientPool 借出文件描述符靠人工纪律归还**（`RawExchange.fd` 裸
   int 跨三层传递给 NetworkChunkSource，析构依赖人工回调释放）。
8. **EpollMultiplexer 全部方法无内部锁**——以上多条的结构性放大器
   （内核层面 epoll_ctl 并发本身合法，危险在关闭与使用的交错）。

---

## 四、fd 所有权改造设计评审稿（回应「shared_ptr 包装文件描述符」提案）

### 4.1 提案复述与核心结论

提案：把文件描述符包装成对象 `FdHandle`，用 `std::shared_ptr` 管理引用
计数；销毁时只需丢弃指针，由析构统一关闭；在途使用者持有指针即可保活。

**核心结论：提案方向正确，是消除第三章 2-7 号风险的结构性方案；但直接
按「最后一个引用释放即 close」落地会引入一个新问题（见 4.2 场景 B），
必须把「关闭」拆成两层语义。**

### 4.2 提案担忧场景的逐条分析

**场景 A（提案原文担忧）：「某处丢弃指针后，另一个指针还活着在通讯，
对端认为连接仍旧可用，发送了新的信息却无法处理」。**

在标准引用计数语义下**此场景不会发生**：只要通讯方还持有
`shared_ptr<FdHandle>`，引用计数大于零，文件描述符不会被关闭——对端看到
的连接真实有效，新消息会被正常处理。引用计数恰好保证了「有活着的持有者
= 连接活着」。

**场景 B（该担忧的真实形态，提案必须解决的问题）：决策型关闭与引用计数
关闭的语义冲突。**

连接关闭通常是**决策**（协议层因对端关闭/错误/停机而决定断开），而非
「最后一个使用者自然用完」。若由最后一个引用释放触发关闭：服务端已决定
断开某连接，但一个滞留的发送任务仍持有指针——文件描述符被滞留引用续命，
对端看到连接仍然活着，**继续发新请求，而服务端协议层已放弃处理**。这才
是「对端误判连接可用」的真实发生路径——方向与提案担忧相反：不是指针丢
了连接还在，而是决策已放弃、连接却被滞留指针拖着不关。

**解法（FdHandle 的两层关闭语义）**：

- `shutdown()`：**决策层，立即生效、幂等**。置关闭标志 +
  `::shutdown(fd, SHUT_RDWR)`——对端**立刻收到对端关闭指示**，停止发送
  新请求（直接消除场景 B 的「对端误判」）；同时从 epoll 摘除、丢弃接收
  缓冲。协议层决定断开时只调这一个。
- 析构：若未 shutdown 先补 shutdown；然后 `close(fd)`。滞留持有者的后续
  读写拿到 EPIPE/ECONNRESET（而非写到复用后的新连接），发送任务据此丢弃
  并清理——错误方向正确。

### 4.3 落地必须解决的三个硬问题

1. **epoll 不持有引用**（最大的隐性缺口）：`epoll_wait` 返回的事件只带
   裸文件描述符数字。若连接的最后一个引用恰在「事件已返回、尚未处理」
   之间释放，描述符被关闭并复用后，事件处理者拿裸数字打到新连接。
   **设计不变量：裸文件描述符数字绝不允许跨越「无引用」边界——事件批
   必须携带 `shared_ptr<FdHandle>`（入队前从连接表快照），处理者持引用
   工作。** 这是整个改造的核心，也是它优于点状修补的地方。
2. **阻塞调用的引用窗口**：发送路径（含分块大传输的分钟级 writev/
   sendfile）必须在全程持有 `shared_ptr<FdHandle>`。SendTask 由捕获裸
   int 改为捕获指针后：(a) 4.2 场景 B 不可能（shutdown 后写得到
   EPIPE）；(b) 现有代际校验的两个缺口（校验后时间窗、分块长闭包）被
   **结构性消除**——代际机制可退役或保留为防御纵深。
3. **fd 编号复用的最后防线**：改造不可能一夜覆盖全部位点（第三章清单
   12 项）。`fd_generations_` 代际机制在改造完成前保留，作为未改造路径
   的兜底；改造完成后按路径逐个退役。

### 4.4 改造面（与现有机制的衔接）

| 改造点 | 现状 | 改后 |
|---|---|---|
| DataServer 连接表 | `conns_: CMVector<ConnState{int fd,...}>` | 持 `shared_ptr<FdHandle>` |
| SendTask | 裸 int + 代际校验 | 持 `shared_ptr<FdHandle>`，执行全程保活；代际校验退役 |
| epoll 事件批 | 裸 fd 数字 | 携带 `shared_ptr<FdHandle>` |
| cleanup_fd | 删表 + close（双关闭风险） | 删表 + `handle->shutdown()`（close 交最后引用，双关闭结构性消失） |
| TcpConnectionManager 三表 | `conn_to_fd_: int` | 值改 `shared_ptr<FdHandle>`；send/poll 快照拷指针 |
| DataClientPool ↔ NetworkChunkSource | `RawExchange.fd` 裸 int + 人工 release 回调 | 交出 `shared_ptr<FdHandle>`，NCS 析构自然归还（人工纪律消失） |
| EpollMultiplexer | 无锁 | 保持无锁（内核 epoll_ctl 并发合法），契约文档化到头注 |
| 滞留引用堆积 | — | shutdown 后滞留任务不再写盘、快速失败出队——引用最长寿命 = 单个发送任务，无泄漏面 |

先例衔接：Reactor 的 per-connection 发送互斥量 shared_ptr 保活
（commit 8a7e8b8）、DataClientPool 停机排水（`active_count_` 计数）、
NetworkChunkSource 的归还回调——都是本方案的点状前身，FdHandle 将其
统一为单一所有权原语。

### 4.5 明确不做

- 不给 `EpollMultiplexer` 加内部锁（调用方持引用后无新增竞争面，加锁
  纯损耗）；
- 不改 DiskChunkSource / MetadataClient / 网络探测等单线程自包含位点
  （构造开、析构关，单属主，无在途问题）。

### 4.6 测试方案

- 单测：FdHandle 契约（shutdown 幂等 / 析构补关闭 / 引用保活期间不关
  闭 / shutdown 后写返回错误）；
- 并发测试：TSAN 下 `data_service_test` ×10 零警告；发送任务滞留 + 连接
  关闭 + 新连接复用同号的确定性交错用例（替换现代际校验的回归位）；
- 全量：非检测构建全量单测 + 全量 QA + 一轮稳定性脚本；
- 性能：peer_rpc / data_service 基准对比（引用计数为原子操作，预期无损；
  如劣化 >2% 需给出解释）。

### 4.7 工作量与顺序

- 第 0 层：0.5 天（调序 + 原子化 + TSAN 验证），直接关闭本 issue 的
  ThreadSanitizer 发现；
- 第 1 层：2-3 天（FdHandle 原语 + DataServer + TcpConnectionManager +
  DataClientPool/NetworkChunkSource + 测试），建议第 0 层合入并观察一轮
  后单独立项实施。

---

## 五、实施方案（已批准方向，2026-09-04 实施级调研定稿）

### 5.0 实施级调研的三个关键确认

1. **stop() 调序与滞留任务的交互（利好）**：`send_loop` 的退出条件是
   「停机且队列空」（data_server.cpp:400-407）——停机时滞留任务会先执行
   完才退出。调序为「先 join 后关闭」后，滞留任务在文件描述符仍然存活的
   窗口内执行，**行为比现状更正确**（现状下滞留任务在 close 之后执行，
   全靠代际校验丢弃）。
2. **188d3c7 代际校验没有专门单测**：其验证面是 data_service_test +
   database_test + 跨 host 传输 QA + 全量 QA（181/181），回归位缺失。
   代际机制退役时必须补一个 FdHandle 语义的确定性回归测试（见 M2）。
3. **DataClientPool 池化描述符的归还语义复杂**（healthy 判定、反倾斜、
   TTL 收割）：M4 采用最小改法——`RawExchange` 携带 `FdHandle`（默认
   closer = ::close）仅供接收线程在途保活与 shutdown 传播；**池归还机制
   （release_borrowed_fd / release_fn_ 回调）原样保留不动**。

### 5.1 里程碑分解（每个里程碑 = 独立 commit + 验证门禁）

**M0 —— 第 0 层 stop 时序修正（0.5 天）**
- `data_server.cpp stop()`：listen_fd_/epoll_fd_ 的关闭（:101-109）移到
  两组 join（:111-119）之后；`conns_` 兜底清理保持最后。
- `data_server.h`：`listen_fd_`/`epoll_fd_` 改 `std::atomic<int>`；读点
  适配（epoll_loop :167/:177、cleanup_fd :152、发送路径 rearm
  :393/:444/:580/:665/:682——load 语义不变）。
- 验证：`--config=tsan` data_service_test ×10 零警告；全量单测；
  qa/storage + qa/network；全量 QA。

**M1 —— FdHandle 原语（0.5 天）**
- 新文件 `src/common/cpp/fd_handle.h`（header-only，经 fly_common_types
  的 hdrs glob 自动可用，无 BUILD 改动；common 是最底层，storage/network
  均可依赖）。
- 接口：`FdHandle::adopt(int fd, CloseFn closer = ::close)`（返回
  shared_ptr）；`bool shutdown()`（幂等，::shutdown(SHUT_RDWR)，非 socket
  描述符返回 false 无副作用）；`void close_now()`（属主强制提前关闭，
  原子 exchange -1 防双关闭）；`int get()`；`bool is_shutdown()`；拷贝
  禁止、shared_ptr 持有；析构 = 未关闭则 closer(fd)。
- 单测 `src/common/tests/fd_handle_test.cpp`（+ BUILD）：shutdown 幂等 /
  析构补关闭 / 引用保活期间不关闭 / shutdown 后写得到错误 / close_now
  后析构不动 / 并发引用掉落恰关一次。

**M2 —— DataServer 迁移（1 天，最大改造面）**
- `ConnState.fd` → `std::shared_ptr<FdHandle>`；accept（:178-193）、事件
  批快照（:174-197，每事件持 conn_mutex_ 拷指针）、on_readable 全链、
  cleanup_fd（改「代际失效 + epoll del + 摘表 + handle->shutdown()」，
  close 交最后引用——双关闭结构性消失）、5 个发送入队点（:279/:370/
  :506/:633 等）、send_loop（:397-451）、分块闭包（:505-581/:632-666）、
  rearm 读点全链改持 handle。
- `fd_generations_`/`fd_gen_counter_`/grab/invalidate 全套退役
  （data_server.h:58-63）；代际校验块（:412-423）删除。
- **新增回归测试**（补 188d3c7 缺失的回归位）：滞留发送任务持 handle +
  连接 shutdown → 任务写得到错误并被丢弃、绝不写向任何复用编号；健康
  连接的发送不受 shutdown 影响。
- 验证：storage 单测全家 + TSAN ×10 + qa/storage + qa/fault + qa/backup
  + 跨 host 传输 QA + 全量 QA。

**M3 —— TcpConnectionManager 迁移（0.5-1 天）**
- `conn_to_fd_`/`fd_to_conn_` 值改 handle；register_connection/close/
  close_all/send（fd 快照改指针快照）/poll（drain_socket 持指针；
  DISCONNECT 路径 handle->shutdown()）。
- 影响面确认：Reactor 与 PeerRpcServer 经 ConnectionManager 接口使用，
  **零改动**（若 peer_rpc_server_test 暴露直接触 fd 的路径，纳入本里程碑）。
- 验证：tcp_connection_manager_test + reactor_test + peer_rpc_server_test
  + TSAN ×10 + qa/network + qa/solver（PeerRpc 流式面）。

**M4 —— DataClientPool / NetworkChunkSource 迁移（0.5 天，最小改法）**
- `RawExchange` 增 `std::shared_ptr<FdHandle> handle`（closer 默认
  ::close）；`worker_agent.cpp:155-192` / `master_agent.cpp:387-413` 接线
  改传 handle；NetworkChunkSource 成员 `int fd_` → handle（接收线程经
  `get()` 使用）；release_fn_ 回调机制**原样保留**（healthy 判定归池语义
  不动）；连接被池关闭时（TTL 收割/反倾斜/停机），接收线程的后续 IO 得
  EBADF → 流失败语义已有（零容忍）。
- 验证：data_client_pool_test + streaming_source_test + qa/storage 流式
  读 + TSAN ×10。

**M5 —— 收尾（0.5 天）**
- TSAN 跑全部并发目标一轮；全量 QA；peer_rpc / data_service 基准对比
  （引用计数为原子操作，预期无损，劣化 >2% 需解释）。
- 文档：DEVELOPMENT_GUIDELINES §16（所有权）增 FdHandle 条目、§13.4 补
  记、issue 011 关闭、DOC_CHANGELOG。
- push（pre-push 全量门禁）。

### 5.2 风险登记

| 风险 | 缓解 |
|---|---|
| stop 调序后 join 时长受滞留分块任务影响（分钟级） | 与现状语义相同（现状同样先等任务），不改语义；停机耗时告警已有 |
| 引用计数原子操作损耗 | 基准对比验证；原子 refcount 在每连接低频拷贝下可忽略 |
| TCM「透明迁移」假设失效 | M3 第一步先 grep Reactor/PeerRpc 直接触 fd 的位点，命中则纳入 M3 |
| DCP closer 归还语义复杂 | 已裁定最小改法：handle 仅保活 + shutdown 传播，归还机制不动（5.0-3） |
| 代际退役后未覆盖路径回归 | M2 回归测试 + 跨 host 传输 QA 双保险；FdHandle shutdown 后写 EPIPE 的失败方向正确 |

### 5.3 待用户确认后启动

默认顺序 M0 → M1 → M2 → M3 → M4 → M5，每个里程碑独立提交、独立验证
门禁；M0 可先行合入观察。
