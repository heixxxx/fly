# 任务失败持久化与重启分析

**日期**: 2026-05-27

---

## 1. 任务失败 → 持久化 → 重启链路

### 1.1 失败检测路径

```
Python task execute
  ├── 正常返回 → status=SUCCESS → poll_task 检查 last_error_type_ → 非 UNKNOWN 则覆盖为 FAILED
  ├── raise Exception → executor 捕获 → status=FAILED → error=format_exc()
  └── C++ crash (SIGSEGV) → 进程终止 → Master 心跳超时检测
```

### 1.2 持久化数据

`FailedTaskRecord` 包含：

| 字段 | 来源 | 重启可用？ |
|------|------|:---:|
| `task_id` | Master 分配的 ID | ✅ |
| `name` | 任务函数名 | ✅ |
| `module` | 任务模块名 | ✅ |
| `args` | 序列化参数（含 `__fly_db__:db_id:...` 标记） | ✅ |
| `inputs` | 数据依赖列表 | ✅ |
| `outputs` | 声明输出（当前为空） | — |
| `required_capabilities` | Worker 需求 | ✅ |
| `error_message` | 错误文本 | 仅用于诊断 |

**不持久化**：
- 任务已写入的对象列表（`written_objects`）
- Worker 分配信息（重新调度到任意 Worker）
- 任务执行时间

### 1.3 重启流程

```
restart_failed_tasks(path)
  → read_failed_records(path)     // 读取 FailedTaskRecord 列表
  → 删除 failed_tasks.bin
  → for each record:
      metadata_->remove_task(旧 task_id)  // 清理旧元数据
      submit_task(task_id, name, module, args, inputs, outputs, caps)
         → metadata_->create_task(...)
         → graph_->add_task(inputs, caps)
         → schedule_tasks()
```

重启后的任务与原始任务完全相同：相同的 task_id、相同的参数、相同的依赖。如果任务依赖的其他对象已经就绪（`graph_` 中有 `mark_data_ready` 记录），任务会立即被调度。

### 1.4 评估

| 场景 | 持久化完整？ | 重启正确？ |
|------|:---:|:---:|
| Task 抛出 Python 异常 | ✅ error 文本持久化 | ✅ 重新提交 |
| write_object 注册失败 | ✅ error_type 持久化（via TaskFailedMessage） | ✅ 重新提交 |
| Worker 进程 crash | ✅ Master 标记任务 FAILED | ✅ 重新提交 |
| Master 进程 crash | ❌ `failed_tasks.bin` 丢失（已 fsync 但小概率） | ⚠️ 需外部工具检查 |
| 依赖数据未就绪就重启 | ✅ inputs 持久化 | ✅ graph_ 检查依赖后再调度 |

**结论**：持久化数据充分，重启链路正确。

---

## 2. 部分写入 + crash + 重启场景

### 2.1 问题描述

```
Task: write(A); write(B); write(C)

时刻 T1: write(A) 完成 → register_write 成功 → 数据落盘 → DataReadyMessage 发送
时刻 T2: write(B) 崩溃（write_object 内部或后段代码 crash）
```

重启后 Task 重新执行 `write(A); write(B); write(C)`。

### 2.2 当前机制

| 步骤 | 行为 |
|------|------|
| `write(A)` — 已存在 | `register_write_with_master` → Master ack.success=true（当前不检查重复）→ **A 被覆盖写** |
| `write(B)` — 全新 | 正常写入 |
| `write(C)` — 全新 | 正常写入 |

**Master 的 `on_write_register`**（master_agent.cpp:758-783）仅检查 `is_db_frozen()`，不检查对象是否已存在。对已存在对象的写入会：
1. `mark_data_ready()` — 幂等，无副作用
2. `update_remote_idx()` — 覆盖为新的 Worker 位置
3. `LocalIndex::add_entry()` — **覆盖旧的索引条目**
4. 旧数据文件留在磁盘成为垃圾

### 2.3 数据冲突分析

| 冲突类型 | 具体场景 | 框架表现 | 风险 |
|----------|----------|----------|:---:|
| 同 key 覆盖写 | 重启后重复写相同 key | 旧 entry 被覆盖 | 低（幂等结果一致） |
| 同 key 不同值 | 非幂等任务，第二次产生不同数据 | 旧数据被覆盖，但无版本追踪 | 中（数据正确但无审计） |
| 不同 Worker 写同 key | 重启后调度到不同 Worker | 新 Worker 写入新文件，旧 Worker 文件成为垃圾 | 低 |
| 依赖对象被覆盖 | A 被重新写入 → 下游任务已读过旧 A | 下游任务不会被撤销 | 中（需要显式 version key） |

### 2.4 改进建议

**方案 A：Master 层面拒绝重复写（轻量）**

在 `on_write_register` 中增加对象存在性检查：若对象已在 `remote_idx` 或已被 `mark_data_ready`，返回 `WRITE_REGISTRATION_FAILED`。

优点：
- 避免重复写入，节省磁盘和网络
- 重启任务跳过已完成的写入

缺点：
- 需要额外判断逻辑
- 对于合理覆盖写场景（如 `results/step_N` 需要被最新结果覆盖）会误伤

**方案 B：保留当前行为，提供幂等性标记（推荐）**

- 定义一个新的错误类型 `WRITE_ALREADY_EXISTS`（语义上不同于当前的 `WRITE_REGISTRATION_FAILED`）
- `register_write_with_master` 在检测到重复时返回此类型，而非静默成功
- `poll_task` 将此类型视为可接受（不触发 task failure），但记录警告日志
- 任务可以检查返回值决定是否重写

**方案 C：任务层面显式管理**

- 使用 `db.get_full_name()` + `DataReady` 机制让任务在写之前检查对象是否已存在
- 任务代码自行处理幂等逻辑

### 2.5 当前 `WRITE_REGISTRATION_FAILED` 评估

`WRITE_REGISTRATION_FAILED` 已在 `error_types.h` 定义但**从未被生产代码使用**。`on_write_register` 只返回 `SUCCESS` 或 `WRITE_TO_FROZEN_DB`。该错误类型在 pre-refactor 代码中也未被触发。

### 2.6 结论

| 场景 | 当前状态 | 建议 |
|------|----------|------|
| 重启后全量重新执行 | ✅ 正确 | 保持 |
| 重启后部分重复写 | ⚠️ 重复写入（无冲突但浪费） | 推荐方案 B |
| `WRITE_REGISTRATION_FAILED` | ❌ 定义但未使用 | 实现或删除 |

---

## 3. 写操作 crash 语义

`write_object` 的原子性边界：

```
check_frozen()         ─── 纯检查，无副作用
register_write()       ─── 向 Master 注册（持久化边界）
WBQ enqueue + write    ─── 异步落盘
WBQ completion         ─── record_write() → 标记数据就绪
```

**crash 在 `register_write` 之后、`record_write` 之前**：Master 已知数据即将可用（`mark_data_ready` 已调用），下游任务可能被调度并尝试读取（通过 Worker 的 DataService 或 Master 请求）。但此时数据可能尚未落盘。`DataService::try_read_local_or_wait` 通过 CV 等待落盘完成来保护此窗口。
