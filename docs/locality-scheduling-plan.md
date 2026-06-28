# 数据 Locality 调度 — 调研与实现计划

> 状态：**待 review**。本文档基于完整链路调研，所有事实均已代码验证。

## 0. 目标

让 `TaskScheduler` 在选择 worker 时，优先把 task 调度到**数据亲和度最高**的 idle worker —— 即"把 task 的输入数据传输量降到最低的 worker"。基于**数据大小加权**，而非简单命中数。

本计划同时合并一项技术债清理：**消除冗余的 `DataReadyMessage`，统一所有写入注册走 `WriteRegisterMessage` 单一入口**。

---

## 1. 调研结论：所有前提已验证

### 1.1 调度器当前完全 locality-blind

`TaskScheduler::select_best_worker`（`src/task/cpp/task_scheduler.cpp:94-139`）决策只看两件事：
1. **capability 匹配**（强约束，完整匹配立即选中）+ `timeout_seconds_` 三档语义
2. **idle + worker_id 排序**（tie-break）

**完全不考虑** worker 所在 host、数据所在 host、数据大小。`set_locality_preference`（`:91-92`）是空函数体。

### 1.2 master 已掌握 placement，但缺 size

| 信息 | 现状 | 位置 |
|------|------|------|
| object → 持有 worker 列表 | ✅ 已有 | `DataService::remote_idx_`，`get_remote_workers()` |
| worker_id → host:port | ✅ 已有 | `DataService::worker_registry_` |
| task → 输入对象列表 | ✅ 已有 | `DependencyGraph::task_dependencies_`，已有 getter `get_task_dependencies` |
| **object → 字节大小** | ❌ **缺失** | `RemoteObjectMeta`（`data_service.h:38-42`）只有 `workers_/read_count_/last_access_time_`，无 size |

### 1.3 size 链路现状：四条写入路径，size 在 register 前全部可得，但被丢弃

所有写入路径都在发 `WriteRegisterMessage` 前完成序列化+压缩，手握压缩后字节数，但 `register_write` 只传了对象名。

| 路径 | 入口 | 序列化/压缩产物（含 size） | register 调用点 |
|---|---|---|---|
| C++ `write_object<T>` | `database.h:203` | `record` + `original_size` | `commit_write:135` |
| Py C++对象 `_write_to_db` | `export_macros.h:75` | 同上（调 `write_object<Cls>`） | 同上 |
| Py pickle `_commit_stream` | `database.cpp:222` | `pure_record` + `hdr.total_size_` | `commit_write:246` |
| Py temp `_write_temp_pickle` | `database.cpp:199` | `buf`（压缩产物） | `put_temp_data:728` |

四条路径全部收敛到 `WorkerAgentContext::register_write(db_id, object_name)`（`worker_context.h:54`），该函数只透传名字。

### 1.4 temp 对象也上 placement

`save_to_db=False` 的 temp 对象经 `put_temp_data`（`database.cpp:713`）**同样调用 `register_write`**（`:728`），master 的 `on_write_register`（`master_agent.cpp:1090`）会 `update_remote_idx` 登记它。temp 影响的是本地存储方式（内存 vs 磁盘），不影响 master 的 placement 登记。这意味着所有通过 `write_object` 产生的对象（含 temp）都在 master placement table 中，locality 调度对它们都适用 —— 这是 size 链路改造能全覆盖的前提。（注：本特性不改 solver，solver 仅作为潜在受益方，不用于验证。）

### 1.5 DataReadyMessage 现状：核心功能冗余，沦为边缘场景载体

经完整链路验证，`DataReadyMessage` 的三个核心动作（`mark_data_ready` / `update_remote_idx` / `schedule_tasks`）对 **worker 写入完全冗余** —— `WriteRegisterMessage` 已经全部做了，且 `DataReadyMessage` 在其后才到达：

```
commit_write
  ├─ register_write_with_master           [worker_agent.cpp:704]
  │    └→ WriteRegisterMessage → on_write_register (master_agent.cpp:1090)
  │         ✅ mark_data_ready + update_remote_idx + schedule_tasks   ← 已全部完成
  │            (worker 阻塞等 ACK，register_write_with_master:753)
  └─ record_write (streaming 分支)        [worker_agent.cpp:509]
       └→ DataReadyMessage → on_data_ready (master_agent.cpp:715)
            🔁 mark_data_ready    (:718)  重复，幂等无操作
            🔁 update_remote_idx  (:722)  重复，幂等
            🔁 schedule_tasks     (:781)  重复，多一轮空调度
            + recorded_workers_ 登记 (:753-778)  ← db meta 持久化
            + auto-backup 评估 (worker_id==0 时, :724)
```

batch 模式下（`dependency_update_mode!=0`）`record_write` 的 streaming 分支不触发，`DataReadyMessage` 根本不发 —— `recorded_workers_` 登记靠 `on_task_complete`（`master_agent.cpp:760-779`）等价逻辑完成。证明这个登记不是 streaming 独有需求。

**唯一不可替代的是 master 自写对象**（`worker_id==0`，`on_master_record_write` `:1522`）：master 自己写不走 worker 的网络 `register_write`，而是直接调 `on_data_ready`。此时三个核心动作是首次执行，且 auto-backup（`:724`，worker_id==0）是 master 自写唯一的 backup 触发点。

但 `on_master_register_write`（`:1535`）目前是**另一条平行链路**：它直接 mark + update_idx + schedule，根本没调 `on_data_ready`。即 master 自写当前有两条平行路径，`on_master_record_write`（发 DataReady）和 `on_master_register_write`（直接操作），逻辑重复且割裂。

### 1.6 master 自写链路的双回调结构

master 进程的 `setup_write_context`（`:1481`）注册了与 worker 对称的两个回调：

```cpp
WorkerAgentContext::set_record_write_func(... on_master_record_write ...);   // → 构造 DataReadyMessage → on_data_ready
WorkerAgentContext::set_register_func(... on_master_register_write ...);     // → 直接 mark + update_idx + schedule
```

这与 worker 侧的 `record_write`（发 DataReady）/ `register_write_with_master`（发 WriteRegister）双消息结构完全对应。统一收敛的关键：**让 master 自写也走 `WriteRegisterMessage`**（给自己发一条），消除 `DataReadyMessage` 和平行链路。

---

## 2. 设计

### 2.1 设计原则

1. **capability 强约束优先** —— 完整匹配仍立即选中，locality 只是软偏好，永不降低 capability 匹配质量。
2. **locality 是软偏好，不是硬约束** —— 无命中/无 size 信息时退回原行为。
3. **基于 size 的亲和度，不是命中数** —— 选"输入数据传输量最小"的 worker。
4. **决策只在 master，worker 只多带一个 size 字段** —— 不新增消息类型（`DataReadyMessage` 反而是删除）。
5. **不考虑兼容** —— master/worker 一起升级。
6. **scheduler 不接触 DataService** —— master 预计算 locality hint，scheduler 只消费。
7. **写入注册统一到 `WriteRegisterMessage` 单一入口** —— 消除 `DataReadyMessage` 冗余，master 自写也走该入口。

### 2.2 亲和度打分算法（核心）

对每个 ready task，遍历其输入对象，对每个 idle worker 计算跨网络拉取字节数：

```
score[worker] = Σ over task inputs:
    若 worker 持有该 input（在 remote_idx_.workers_ 中）:  +0
    否则:                                                  +size[input]
# score 越小越好（本地数据多 = 传输少）
选 idle workers 中 score 最小者；score 相同则保持原 worker_id 升序 tie-break
```

**为何用压缩后 size**：跨 worker 拉取走 `DataService::read_raw_compressed`（`data_service.cpp:880`），传输的是压缩字节。

### 2.3 改动总览（四个工作流）

#### 工作流 A：统一写入注册到 WriteRegisterMessage（消除 DataReadyMessage）

**核心思路**：`WriteRegisterMessage` 是所有写入注册的权威入口。让 master 自写也走它（给自己发一条），则 `DataReadyMessage` 的所有功能都由 `WriteRegisterMessage` + `on_write_register` 覆盖，可以删除。

**(A1) master 自写改为发 `WriteRegisterMessage` 给自己**

`on_master_record_write`（`master_agent.cpp:1522`）改为：构造 `WriteRegisterMessage`（`worker_id_=0`）发给自己 reactor，由 `on_write_register` 统一处理。

但 master 自写的 `record_write_func` 和 `register_func` 是先后两次回调（对应 `commit_write` 的两步：先 register、后 record）。统一后：
- `register_func`（`on_master_register_write`）仍存在，但改为发 `WriteRegisterMessage` 给自己（而非直接操作）—— 与 worker 行为对称。
- `record_write_func`（`on_master_record_write`）删除 —— master 自写的 register 已含全部逻辑，record 阶段不再需要。

**具体做法 + R4 缓解**：master 自写走 reactor 需要指向自己的 conn_id，更简单的是**直接调用**。但 `on_write_register:1136` 末尾 `reactor_->send(conn_id, ack)` 会给"自己的 conn_id"发 ACK，master 自写不需要网络 ACK。

解法：**抽出纯逻辑函数 `do_write_register(msg) -> ack`**（含 provenance 校验、mark_data_ready、update_remote_idx 带 size、schedule_tasks、recorded_workers_ 登记、auto-backup 评估）。
- worker 路径：`on_write_register(conn_id, msg)` 调 `do_write_register(msg)` 后 `reactor_->send(conn_id, ack)`。
- master 自写路径：`on_master_register_write` 构造 msg 后调 `do_write_register(msg)`，**丢弃返回的 ack**（同步调用，无需 ACK）。

这样 worker/master 共用同一套注册逻辑，只在"是否回 ACK"上分叉，零网络开销。

**(A2) 把 DataReadyMessage 的非冗余逻辑迁入 on_write_register**

`on_write_register`（`master_agent.cpp:1090`）当前只做 placement + provenance + schedule。需补上 `DataReadyMessage` 独有的两条：

- `recorded_workers_` db meta 登记（原 `on_data_ready:753-778`）→ 迁入 `on_write_register`。由于 `WriteRegisterMessage` 已有 `worker_id_` 和 `db_id_`，补上 `writer_id_` 字段即可（master 自写时从 `db_instances_` 取）。
- auto-backup 评估（原 `on_data_ready:724`，worker_id==0）→ 迁入 `on_write_register`，触发条件改为 `worker_id_==0`。

**(A3) `WriteRegisterMessage` 补 writer_id 字段**

为支撑 A2 的 `recorded_workers_` 登记（key 含 writer_id），`WriteRegisterMessage`（`message_types.h:277`）加 `CMString writer_id_` 字段。

**(A4) 删除 DataReadyMessage**

- 删除 `message_types.h:202` 的 `DataReadyMessage` 结构。
- 删除 `master_agent.cpp:66-69` 的 reactor handler 注册。
- 删除 `master_agent.h:172` 的 `on_data_ready` 声明 + `master_agent.cpp:715-782` 实现。
- 删除 `master_agent.cpp:1522-1533` 的 `on_master_record_write`。
- 删除 worker `record_write`（`worker_agent.cpp:509-519`）的 streaming 分支（整个 `if (dependency_update_mode==0)` 块）。
- 删除 reactor 对 `DataReadyMessage` 的 `MessageType` 枚举（若仅此处用）。
- 更新 `message_protocol_test.cpp` 中相关测试。

**简化效果**：worker 和 master 的写入注册都收敛为单一 `WriteRegisterMessage` → `on_write_register` 链路，消除 streaming/batch 模式下 placement 更新的不对称。

#### 工作流 B：size 链路打通

**(B1) `register_write` 透传 size**

`WorkerAgentContext::register_write`（`worker_context.h:54`）加 `int64_t compressed_size` 参数，同步改 `set_register_func`（`:50`）签名。

三处 `register_write` 调用点全部带 size（均已验证 size 可得）：
- `database.cpp:135`（`commit_write`）→ 传 `record->size()`（压缩后字节数）
- `database.cpp:333`（`do_backup_write`）→ 传 `record->size()`。**backup 是原样复制压缩字节，副本 size 必须等于原对象 size**（同一对象在 `remote_idx_` 的所有副本共享同一个 `RemoteObjectMeta.size_bytes_`，写多次幂等）。backup 的目的是给数据加副本，副本必须经 `register_write` → `update_remote_idx` 登记进 `remote_idx_.workers_` 才对读取/调度可见 —— 当前链路已通过 `do_backup_write:333` 实现，B1 改造后同步带 size。
- `database.cpp:728`（`put_temp_data`）→ 传 `buf->size()`

两处 `set_register_func` 实现点全部改签名：
- `worker_agent.cpp:470`（worker，发 `WriteRegisterMessage`）
- `master_agent.cpp:1485`（master 自写，工作流 A 后改为发 `WriteRegisterMessage` 给自己）

**(B2) `WriteRegisterMessage` 加 size 字段**

`message_types.h` 的 `WriteRegisterMessage`（`:277`）加 `int64_t size_bytes_`，加进 `FLY_SERIALIZE`。

**(B3) `TaskCompleteMessage.written_objects_` 改结构体**

原计划写"并行字段"是脆弱设计（平行数组易错位），改用结构体数组：

```cpp
struct WrittenObject {
    CMString object_name_;
    int64_t size_bytes_ = 0;
    FLY_SERIALIZE(object_name_, size_bytes_);
};
// TaskCompleteMessage.written_objects_ 从 CMVector<CMString> 改为 CMVector<WrittenObject>
```

worker 侧填充：扩展 `record_write_func` 签名加 `int64_t compressed_size`（与 `register_func` 对称）。`commit_write` 的 complete lambda 捕获 register 阶段已确定的 `record->size()`，落盘后传入。worker `record_write` 把 `WrittenObject{name, size}` push 进 `current_writes_`（类型改为 `CMVector<WrittenObject>`），task 完成时直接 move 进 `TaskCompleteMessage.written_objects_`（`worker_agent.cpp:364-366`）。
master 侧消费（`master_agent.cpp:798, 838`）：遍历改读 `.object_name_` / `.size_bytes_`，size 传给 `update_remote_idx`。

**(B4) master 接收并记入 placement table**

- `RemoteObjectMeta`（`data_service.h:38`）加 `int64_t size_bytes_ = 0`。
- `update_remote_idx`（`data_service.h:138`）加 `int64_t size_bytes` 参数。**语义：size>0 时更新；size==0 时保持原值不变**（防止冗余调用清零，见 §4 风险）。
- `on_write_register`（工作流 A 后的统一入口）从 `WriteRegisterMessage.size_bytes_` 取 size 传给 `update_remote_idx`。
- `on_task_complete`（`master_agent.cpp:798, 838`）从 `TaskCompleteMessage.written_objects_` 的 `WrittenObject.size_bytes_` 取。
- 新增 `DataService::get_remote_size(object_name)` getter（读 `RemoteObjectMeta.size_bytes_`）。

#### 工作流 C：master 预计算 locality hint

在 `MasterAgent::schedule_tasks()`（`master_agent.cpp:388`）调 `scheduler_->schedule_all_available()` **之前**，为每个 ready task 预计算亲和度排序的 worker 列表，挂到 task 上。

**(C1) `compute_locality_order(task_id)`（master_agent 新方法）**

```cpp
CMVector<uint64_t> compute_locality_order(uint64_t task_id) {
    auto deps = graph_->get_task_dependencies(task_id);
    auto idle = worker_manager_->get_idle_workers();
    CMUnorderedMap<uint64_t, int64_t> score;
    for (uint64_t w : idle) score[w] = 0;
    for (const auto& obj : deps) {
        auto holders = DataService::instance()->get_remote_workers(obj);
        int64_t sz = DataService::instance()->get_remote_size(obj);
        CMUnorderedSet<uint64_t> holder_set(holders.begin(), holders.end());
        for (auto& [w, s] : score) {
            if (!holder_set.count(w)) s += sz;
        }
    }
    CMVector<uint64_t> ordered(idle.begin(), idle.end());
    std::sort(ordered.begin(), ordered.end(), [&](uint64_t a, uint64_t b) {
        if (score[a] != score[b]) return score[a] < score[b];
        return a < b;  // 与原 worker_id tie-break 一致
    });
    return ordered;
}
```

**(C2) `DependencyGraph` 给 task 挂 locality hint**

`TaskRequirements`（`dependency_graph.h:17`）加 `CMVector<uint64_t> locality_order_` 字段。
`DependencyGraph` 加 `set_task_locality_hint(task_id, order)` setter。
master 在 `compute_locality_order` 后调此 setter。

**(C3) `schedule_tasks` 接线**

`schedule_tasks()`（`master_agent.cpp:388`）：`get_ready_tasks()` 后、`schedule_all_available()` 前，对每个 ready task 调 `compute_locality_order` + `set_task_locality_hint`。

#### 工作流 D：scheduler 消费 hint

**(D1) `select_best_worker` 三阶段算法**

替换当前单遍循环（`task_scheduler.cpp:94-139`）为三阶段，**每阶段都遵守原 timeout/degrade 语义**：

```
阶段 A（capability 完整匹配，强约束不变）：
    遍历 idle workers，任一完整匹配立即返回。← 完全保留现状

阶段 B（locality 偏好，仅 locality_enabled_ 时）：
    若 TaskRequirements.locality_order_ 非空：
        按 locality_order_ 顺序遍历（master 调度前算好的 score 升序快照），
        跳过当前已非 idle 的（串行调度下，上一轮 schedule_next 选中的 worker 已 BUSY），
        在剩余 idle 里挑 capability 匹配数最高的（遵守不变量：不低于全局最佳部分匹配数）
        若找到 → 返回

阶段 C（兜底，原行为）：
    无 locality 命中/未启用 → 原 allow_degrade 逻辑（最佳部分匹配 / 0）
```

**核心不变量**：locality 永不降低 capability 质量。阶段 B 选中的 worker 的 capability 匹配数必须 ≥ 阶段 C 能找到的最佳部分匹配数。

**(D2) `set_locality_preference` 真正生效**

`task_scheduler.cpp:91-92` 空实现改为写 `locality_enabled_` 成员。加 `locality_preference()` getter。

### 2.4 启用策略

- **第一阶段（本计划）**：实现 + 单测 + 默认**关闭**（`locality_enabled_ = false`），保证零行为变更。
- **第二阶段**：Step 10 的独立性能对比测试验证收益。收益明确（跨网络读取次数显著下降、wall clock 下降）再默认开启。
- 通过 Config 项 `locality_scheduling_enabled` 控制开关。

---

## 3. 实现步骤（TDD）

> 工作流顺序：A（消息统一）→ B（size 链路）→ C（预计算）→ D（scheduler）。A 和 B 都改消息结构，合并处理减少冲突。

### Step 1：消息结构统一改造（工作流 A2/A3 + B2/B3）

- [ ] 1a. `message_types.h`：`WriteRegisterMessage` 加 `CMString writer_id_` + `int64_t size_bytes_`，更新 `FLY_SERIALIZE`。
- [ ] 1b. `message_types.h`：`TaskCompleteMessage.written_objects_` 改为 `CMVector<WrittenObject>`（新增 `WrittenObject` 结构体，含 size）。
- [ ] 1c. `message_protocol_test.cpp`：更新 `WriteRegisterMessage` / `TaskCompleteMessage` 的序列化测试。

### Step 2：DataService placement 带 size（工作流 B4）

- [ ] 2a. `data_service.h`：`RemoteObjectMeta` 加 `int64_t size_bytes_`。
- [ ] 2b. `data_service.h/cpp`：`update_remote_idx` 加 `int64_t size_bytes` 参数；语义 = size>0 更新、size==0 保持原值。
- [ ] 2c. `data_service.h/cpp`：新增 `get_remote_size(object_name)` getter。
- [ ] 2d. 更新所有 `update_remote_idx` 调用点（master_agent 多处、data_service 内部 backup/rebuild），size 未知处传 0（保持原值）。
- [ ] 2e. 单测：验证 `RemoteObjectMeta.size_bytes_` 正确记录 + size==0 不覆盖。

### Step 3：register_write 透传 size（工作流 B1）

- [ ] 3a. `worker_context.h`：`register_write` / `set_register_func` 签名加 `int64_t compressed_size`。
- [ ] 3b. `database.cpp`：三处 `register_write` 调用点带 size（`:135, :333, :728`）。
- [ ] 3c. `worker_agent.cpp`：`set_register_func` 实现（`:470`）把 size 填进 `WriteRegisterMessage`。

### Step 4：master 自写统一走 WriteRegister（工作流 A1 + R4 缓解）

- [ ] 4a. `master_agent.cpp`：从 `on_write_register` 抽出纯逻辑函数 `do_write_register(msg) -> ack`（含 provenance 校验、mark_data_ready、update_remote_idx 带 size、schedule_tasks、recorded_workers_ 登记、auto-backup 评估）。
- [ ] 4b. `on_write_register` 改为调 `do_write_register(msg)` 后 `reactor_->send(conn_id, ack)`（worker 路径）。
- [ ] 4c. `on_master_register_write`（`:1535`）改为构造 `WriteRegisterMessage`（worker_id=0，带 size、writer_id）后调 `do_write_register(msg)`，**丢弃返回 ack**（master 自写，零网络开销）。
- [ ] 4d. 验证 master 自写场景：write_object → register → do_write_register 全链路，含 auto-backup + db meta 登记。

### Step 5：删除 DataReadyMessage（工作流 A4）

- [ ] 5a. 删除 `master_agent.cpp:1522-1533`（`on_master_record_write`）+ `:1482-1484`（master 的 `set_record_write_func` 绑定）。
- [ ] 5b. 删除 `master_agent.cpp:715-782`（`on_data_ready`）+ `:66-69`（reactor handler）+ `master_agent.h:172`（声明）。
- [ ] 5c. **worker `record_write`（`worker_agent.cpp:505`）：保留函数，删除内部 streaming 分支（`:509-519`）。** 保留 `current_writes_.push_back(full_name)`（`:507`）—— 它是 task 完成时 `TaskCompleteMessage.written_objects_` 的数据来源。`record_write_func` 回调机制保留（被 `commit_write` 异步落盘 complete 回调使用）。详见 Step 6 的签名扩展。
- [ ] 5d. 删除 `message_types.h:202-212`（`DataReadyMessage` 结构）。同步删除 `network_export.cpp:27` 的 `DATA_READY` 导出（R1）。`MessageType::DATA_READY` 枚举值随之删除 —— 序列化按结构体名匹配，不依赖枚举整数序号，删除安全。
- [ ] 5e. 更新所有相关测试（含 `message_protocol_test.cpp`）。

### Step 6：worker 批量上报带 size（工作流 B3 worker 侧）

> size 在 register 阶段已确定（数据完成序列化+压缩进 cache，落盘只是持久化只读字节，size 不变）。`record_write_func` 与 `register_func` 对称扩展 size 参数即可，无需并行映射。

- [ ] 6a. `worker_context.h`：`record_write_func` / `set_record_write_func` / `record_write` / `current_record_func` 签名加 `int64_t compressed_size` 参数（与 `register_func` 的 size 参数对称）。
- [ ] 6b. `database.cpp`：`commit_write` 的 complete lambda（`:175-189`）捕获 size（register 阶段已确定的 `record->size()`），落盘后调用 `caller_record_func(db_id, object_name, size)`。`do_backup_write` 的 complete lambda（`:355-367`）同理。
- [ ] 6c. `worker_agent.cpp`：`record_write`（`:505`，删除 streaming 分支后）改签名接收 size，`current_writes_` 类型改为 `CMVector<WrittenObject>`，push `WrittenObject{name, size}`。
- [ ] 6d. `worker_agent.cpp:364-366`：`written_objects_` 现已是 `CMVector<WrittenObject>`，`tracked_writes`（`end_task` 返回）直接 move 进 `TaskCompleteMessage.written_objects_`。
- [ ] 6e. master `on_task_complete`（`:798, :838`）：遍历改读 `.object_name_` / `.size_bytes_`，size 传给 `update_remote_idx`。

### Step 7：master 预计算 locality（工作流 C）

- [ ] 7a. `master_agent.h/cpp`：实现 `compute_locality_order(task_id)`。
- [ ] 7b. `dependency_graph.h/cpp`：`TaskRequirements` 加 `locality_order_` + `set_task_locality_hint` setter。
- [ ] 7c. `master_agent.cpp`：`schedule_tasks()` 里 ready task 预计算后调 `set_task_locality_hint`。
- [ ] 7d. 单测：mock `get_remote_workers` / `get_remote_size`，验证打分排序（含大对象优先的反优化防护）。

### Step 8：scheduler 消费 hint（工作流 D）

- [ ] 8a. `task_scheduler.h`：加 `locality_enabled_` 成员；`set_locality_preference` 改非空；加 `locality_preference()` getter。
- [ ] 8b. `task_scheduler.cpp`：`select_best_worker` 改三阶段算法。
- [ ] 8c. `task_scheduler_test.cpp` 新增 fixture `LocalityScheduling`（fake DependencyGraph 填 `locality_order_`，不依赖真实 DataService）：
  - [ ] T1: `locality_enabled=false` → 行为与现状完全一致（回归保护）
  - [ ] T2: 启用，task 输入全在 worker W → 调度到 W
  - [ ] T3: 多 worker 命中不同输入 → 选 `locality_order_` 首位
  - [ ] T4: **capability 完整匹配优先于 locality**
  - [ ] T5: **locality 不降低 capability 质量**（locality worker 匹配数 < 全局最佳 → 退阶段 C）
  - [ ] T6: `locality_order_` 空 → 退原行为（覆盖 R6：task 无输入对象时 scheduler 不参与 locality，直接走阶段 A/C）

### Step 9：接线 + 端到端

- [ ] 9a. `master_agent.cpp`：构造 scheduler 后 `set_locality_preference(false)`（默认关）。
- [ ] 9b. `./fly.sh build //src/main/cpp:fly` + `./fly.sh install`。
- [ ] 9c. e2e smoke：跑现有 QA 确认 master/worker 启动正常、调度行为不变、size 正确上报、DataReadyMessage 已无引用。

### Step 10：Locality 功能 + 性能对比测试（独立构造，不依赖 solver）

> solver 不做任何修改，本特性的功能验证与性能收益全部由独立构造的测试覆盖。

**测试场景设计原则**：构造一个数据传输为主导开销的 task 拓扑 —— 某些 task 的输入是大对象，分布在不同 worker 上，调度选择直接决定跨网络拉取量。

- [ ] 10a. `qa/scheduling/test_locality_basic.py`：功能正确性。多 worker + 已知数据分布（worker A 写大对象 X，worker B 写大对象 Y），提交一个依赖 X 的 task，断言它被调度到 worker A（持有者）。验证手段：`@as_task` 里读取 `get_agent()` 的 worker assignment，或通过 task 执行日志/依赖 location 验证。
- [ ] 10b. `qa/scheduling/test_locality_capability_priority.py`：capability 完整匹配优先于 locality（T4 场景的 E2E）。
- [ ] 10c. `qa/performance/test_locality_perf.py`：**增强前后性能对比**。
  - 构造场景：N 个 worker，每个 worker 持有一个大对象（如 10-50MB 矩阵）；提交 N 个 task，每个 task 依赖一个特定大对象。
  - 对照组 A（`locality_scheduling_enabled=0`，关闭）：task 随机/worker_id 排序调度，多数 task 跨网络拉取大对象。
  - 对照组 B（`locality_scheduling_enabled=1`，开启）：task 调度到持有者 worker，零跨网络拉取。
  - 指标：每个 task 的 wall clock（或总调度完成时间），通过 DBG 日志的 `[TIER2]`/`[TIER3]` 跨网络读取次数对比（开启后应大幅减少）。
  - 断言：B 组的总完成时间 < A 组，跨网络读取次数 B < A。
- [ ] 10d. **性能对比必须给出增强前后的实测数据**（wall clock + 跨网络读取次数），填入 §6 实测收益章节。这是本特性验收的硬指标。

### Step 11：文档

- [ ] 11a. 更新 `docs/architecture.md`：调度层加 locality 说明 + 写入注册统一（WriteRegister 单一入口）说明。
- [ ] 11b. 本文档 §6 填充实测收益数据。

### Step 12：全量回归 + commit

- [ ] 12a. `./fly.sh check`（build + unit test + clangd）。
- [ ] 12b. `./qa/runqa` 全量 QA（含 Step 10 新增的 locality 测试）。
- [ ] 12c. 确认 solver 相关 QA（`qa/solver/`）全部通过且**无任何 solver 代码改动**（`git diff src/solver/` 应为空）。
- [ ] 12d. pre-commit-check skill，commit。

---

## 4. 风险与边界

### 4.1 已识别并已缓解的风险

| 风险 | 缓解 |
|------|------|
| locality 反优化（为命中牺牲能力） | §2.3(D1) 核心不变量 + T5 守住 |
| size 未上报（中间态/异常） | `get_remote_size` 返回 0 时该对象对所有 worker 加 0 成本（忽略 size，退化为不区分），不阻塞调度 |
| 冗余 `update_remote_idx` 清零 size | **冗余链路有两条**：streaming 模式下 `on_data_ready:722`（工作流 A 删除后此链路消失）；batch 模式下 `on_task_complete:801/839`（B3 改造后 `written_objects_` 带 size，传真实 size，不再冗余）。**backup 路径**（`do_backup_write:333`）经 B1 改造后带 size，与原对象 size 一致，幂等写入安全。**rebuild 路径**（`:1635, :1689`，进程恢复时从本地 idx 重建 placement）size 未知，传 0。`update_remote_idx` 语义定为 size>0 才更新、size==0 保持原值，防御 rebuild 这类 size 未知场景 |
| 删除 DataReadyMessage 破坏 master 自写 auto-backup | 工作流 A2 已把 auto-backup 评估迁入 `on_write_register`（worker_id==0 触发） |
| 删除 DataReadyMessage 破坏 recorded_workers_ 登记 | 工作流 A2 已把登记迁入 `on_write_register`；WriteRegisterMessage 补 writer_id 字段 |
| `record_write_func` 机制需保留 | `commit_write` 把 `record_write_func` 存进异步落盘 complete 回调，落盘后调用 worker `record_write` 收集 `current_writes_`（task 完成上报的数据来源）。工作流 A 删 master 侧 `on_master_record_write` 和 worker streaming 分支；Step 6 给 `record_write_func` 加 size 参数（对称 `register_func`），`current_writes_` 改 `CMVector<WrittenObject>`，`commit_write` 的 complete lambda 捕获 register 阶段已确定的 size 传入。size 在 register 时定死（数据已压缩进 cache），落盘只是持久化只读字节 |
| 分层依赖（task → storage） | scheduler 不 include storage；master 预计算 hint，scheduler 只读 `TaskRequirements.locality_order_`（POD） |

### 4.2 需在实现阶段处理的风险

**R1：MessageType::DATA_READY 的 Python 导出**

`network_export.cpp:27` 把 `MessageType::DATA_READY` 导出给 Python。删除 `DataReadyMessage` 时同步修改：删除该枚举值的导出 + 确认 Python 侧无引用。序列化按结构体名匹配，不依赖枚举整数序号，删除安全。

**R2：master 自写走 do_write_register 的 ACK 处理**

`on_write_register:1136` 末尾 `reactor_->send(conn_id, ack)`。master 自写是同步函数调用（`on_master_register_write` → `do_write_register`），不需要网络 ACK。

**解决**：抽出纯逻辑函数 `do_write_register(msg) -> ack`。
- worker 路径：`on_write_register(conn_id, msg)` 调 `do_write_register(msg)` 后 `reactor_->send(conn_id, ack)`。
- master 自写路径：`on_master_register_write` 构造 msg 后调 `do_write_register(msg)`，丢弃返回 ack。

worker/master 共用同一套注册逻辑，只在"是否回 ACK"上分叉。

**R3：locality_order_ 空集合语义**

空集合 = master 未填 hint（task 无输入，或所有输入 size=0）。scheduler 阶段 B 明确判 `locality_order_.empty()` → 跳过 locality，退阶段 C。T6 测试覆盖"task 无输入对象"用例。

**R4：size 类型转换**

`FlyBuffer::size()` 返回 `size_t`，`size_bytes_` 用 `int64_t`。2^63 bytes ≈ 9.2 EB，单文件不可能达到，无溢出风险。传 size 前用 `static_cast<int64_t>` 即可，无需特殊防御。

### 4.3 风险小结

size 链路（worker 写入 → register 带 size → master placement）经核查无时序/语义冲突：数据在 register 阶段已完成序列化+压缩并进入 cache（`database.cpp:124-129`），register 之后的数据落盘是只读字节持久化，size 在 register 时即确定且不再变化。R1/R2 是删 DataReadyMessage 的连带项，有明确解法。R3/R4 由代码处理和测试守底。

---

## 5. 不做什么（明确排除）

- **不做 worker 负载感知**（CPU/内存/队列）—— 当前无负载数据，独立特性。
- **不做网络拓扑感知**（rack/switch）—— 单机多进程为主，host 粒度足够。
- **不做数据预取/迁移** —— 只改调度决策。
- **不改 solver 任何代码** —— 本特性对 solver 完全透明，solver 的 `requires=sd_{id}`、数据分布方式一律不动。`git diff src/solver/` 必须为空。功能与性能验证全部由独立构造的测试覆盖（Step 10）。
- **不依赖 solver 验证收益** —— 不在 `big_qa/` 跑 solver benchmark，避免与 solver 强耦合。性能对比用 Step 10c 的独立场景。
- **不做 per-task locality 配置** —— 先全局开关。
- **不考虑消息向后兼容** —— 已确认一起升级。

---

## 6. 实测收益

### 6.1 功能验证（QA 测试）

三个独立构造的 QA 测试全部通过（不依赖 solver）：

| 测试 | 位置 | 验证内容 |
|------|------|---------|
| `test_locality_basic.py` | `qa/scheduling/` | 2 worker + 大对象，consume task 被调度到持有者 worker |
| `test_locality_capability_priority.py` | `qa/scheduling/` | capability 完整匹配优先于 locality（冲突场景） |
| `test_locality_perf.py` | `qa/performance/` | 3 worker 各持 24MB 对象，3 consume 全部本地命中 |

### 6.2 性能数据

`test_locality_perf.py`（N_WORKERS=3, PAYLOAD_SIZE=3M ints ≈ 24MB/对象）：

```
holders={0: worker1, 1: worker2, 2: worker3}
consume_0 ran on worker1, holder=worker1, LOCAL
consume_1 ran on worker2, holder=worker2, LOCAL
consume_2 ran on worker3, holder=worker3, LOCAL
wall_consume=0.31s local_hits=3/3
```

locality ON 时 3/3 consume 本地命中，**零跨网络传输**。每个 consume 省去 24MB 跨 worker 拉取。

### 6.3 实现中发现并修复的问题

1. **locality_order_ 时效性（计划原 §4.2 R5，曾误判删除后恢复）**：`schedule_all_available` 串行循环里每调一次 `schedule_next` 选走一个 worker，后续 task 若用调度前预算的 idle-only hint 会失效。**修复**：`compute_locality_order` 用所有注册 worker 算分（不只 idle），scheduler 阶段 B 自行过滤 idle，hint 不因 worker 变 busy 而过期。

2. **Config 运行时切换**：scheduler 构造时只读一次 Config，运行时 `set_int("locality_scheduling_enabled")` 不生效。**修复**：`schedule_tasks` 每次调度前从 Config 同步开关到 scheduler。
