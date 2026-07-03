# 第五类问题核实报告（结合代码 + 模块文档 + 设计文档）

> 核实日期：2026-07-03
> 核实对象：`docs/redundancy-audit-report.md` 第五类「架构边界问题」1-4 项
> 方法：逐行读源码 + BUILD 文件 + 四份 module.md（common/serialization/storage/agent）交叉验证

---

## 核实结论速览

| # | 报告标题 | 报告判定 | 实际核实结论 | 严重度 |
|---|---------|---------|------------|--------|
| 1 | common 反向依赖 serialization | 真问题（循环依赖风险） | **部分真**：源码 include 层确实反向引用，但 BUILD 层 common 不依赖 serialization，**当前不存在循环依赖**，是"缺失依赖声明 + 源码跨层引用" | 中 |
| 2 | FlyBuffer 归属错位 | 真问题 | **真问题（且比报告描述更严重）**：通用缓冲区被 5 个模块共用，连 common 模块文档都登记为自有，却物理在 serialization | 高 |
| 3 | worker_context.h 错放 common | 真问题 | **部分真**：仅 agent+storage 用属实，但放 common 是 module.md 明确记录的**有意架构决策**（打破 storage↔agent 循环），非随手乱放 | 低 |
| 4 | Database/DataService TempStore 双持 | 真冗余 | **真冗余（且比报告更严重）**：实际有三层冗余，路径 B 整套（`temp_store_`+`temp_entries_`+4 方法）是死代码 | 高 |

---

## 问题 1：common 反向依赖 serialization

### 报告原文
> common 反向依赖 serialization。`worker_context.h:5` include fly_buffer.h。**判定：真问题（循环依赖风险）**

### 核实证据

**源码层（属报告所讲）**：
- `src/common/cpp/worker_context.h:5`：
  ```cpp
  #include <serialization/cpp/fly_buffer.h>
  ```
  include 路径指向 serialization 模块 ✓（报告事实准确）

**Bazel 模块依赖图（报告未提及，关键反差）**：
- `src/common/cpp/BUILD` 的 `fly_common_types` 目标，`cc_library` 完整内容：
  ```python
  cc_library(
      name = "fly_common_types",
      hdrs = glob(["*.h"]),
      strip_include_prefix = "/src",
      copts = ["-std=c++20"],
  )
  ```
  **`deps` 字段缺失**（为空）。common 不在 BUILD 层依赖 serialization。
- `src/serialization/cpp/BUILD` 的 `fly_serialization` 目标：
  ```python
  deps = [
      "@bitsery//:bitsery",
      "//src/common/cpp:fly_common_types",   # serialization → common
      ...
  ],
  ```
  BUILD 图只有 **serialization → common 单向**。

**头文件展开链**：
```
common/worker_context.h
  → serialization/fly_buffer.h
      → common/common_types.h   （不回引 serialization，链终止）
```
无头文件循环展开，编译安全。

### 判定：部分真（措辞偏重）

| 维度 | 结论 |
|------|------|
| 源码 include 层 common→serialization | **成立** |
| Bazel 模块层 common→serialization | **不成立**（common BUILD 无 deps） |
| 循环依赖风险 | **当前不存在**（serialization→common 单向，头链不回环） |
| 真实工程隐患 | **是**：`fly_common_types` 目标不自洽（include 了未声明的依赖），靠"所有消费者恰好同时 deps serialization"侥幸通过 |

**精确表述**：这是「分层不一致 + 缺失依赖声明」，不是「循环依赖」。当前能编译不代表正确——任何只 deps `fly_common_types` 又 include `worker_context.h` 的新消费者会立刻编译失败。

### 与问题 2 的关系
问题 1 的根源正是问题 2：`worker_context.h` 之所以反向 include serialization，是因为 `FlyBufferPtr`（var 服务回调的参数类型）放在 serialization。**问题 2 解决（FlyBuffer 下沉到 common）后，问题 1 自动消失**。

---

## 问题 2：FlyBuffer 归属错位

### 报告原文
> FlyBuffer 归属错位 —— 跨 storage/network/common 用，却在 serialization。**判定：真问题**

### 核实证据

**物理位置**：`src/serialization/cpp/fly_buffer.h`（header-only，无 .cpp）✓

**FlyBuffer 的类型本质**（读 fly_buffer.h 全文）：
- 封装 `CMString data_` + 读游标 `size_t pos_`
- 接口分两组：
  - **通用缓冲区**（与序列化无关）：`write/take/release/data/size/reserve/begin/end`
  - **pickle file-protocol**：`read/readline/readinto`（注释自承 "for pickle.load(flybuffer)"，fly_buffer.h:29-62）
  - **FlyBufferPtr**（注释自承 "carrier type for compressed bytes throughout the read/serve/cache data flow"，fly_buffer.h:96-100）
- **唯一依赖**：`<common/cpp/common_types.h>` + `<cstring>` + `<iterator>`。**完全不依赖 bitsery 或任何序列化框架**

**跨模块使用统计**（按 include + 类型名双重核实）：

| 模块 | 直接 include 文件数 | 类型名出现文件数 |
|------|------|------|
| storage | 11 | 21 |
| network | 5 | 7 |
| serialization（自身） | 1 | 3 |
| common | 1 | 1 |
| **agent**（报告漏报） | 0（传递） | 5 |
| **export**（报告漏报） | 0（传递） | 1 |

报告称"跨 storage/network/common 用"——属实，但**实际范围更大**（agent/export 也重度用），报告漏报 2 个模块。

**文档自相矛盾（最硬证据）**：
- `docs/common/module.md:19` 核心文件表列了 `fly_buffer.h | FlyBuffer + FlyBufferPtr 定义`
- `docs/common/module.md:5` 写 "位置: src/common/cpp/"
- `docs/common/module.md:70-95` 用整节详解 FlyBuffer
- **但文件物理不在 common，在 serialization**
- `docs/serialization/module.md:13-18` 核心文件表**完全不列** fly_buffer.h（只列 serialization_macros.h、version.h、object_header.h）

即 common 文档认它、serialization 文档不认——文档体系内部矛盾，恰好暴露其"无家可归"。

**serialization 的真实职责**（读 serialization/module.md）：
序列化宏体系（FLY_SERIALIZE/FLY_FIELD/FLY_ENCODE）、FlyTrustedConfig、bitsery adapter 特化。FlyBuffer 仅作为 `FLY_ENCODE_TO_BUFFER` 的载体之一被引用，与序列化核心逻辑无强绑定。

### 判定：真问题（严重度高于报告描述）

FlyBuffer 是 5 模块共用的通用零拷贝缓冲区，被错放 serialization 仅为历史原因（早期名 `FlySerBuf`，后改名统一）。证据链完整：跨 5 模块、内容纯通用、文档体系内部矛盾、上游为用一类型拖整个 serialization+bitsery 栈。

---

## 问题 3：worker_context.h 错放 common

### 报告原文
> worker_context.h 错放 common —— 仅 agent+storage 用，文件名与类名不符。**判定：真问题**

### 核实证据

**类名**：实际是 `WorkerAgentContext`（worker_context.h:12），**不是报告所写的 `WorkerContext`**。全仓无 `WorkerContext` 类。报告此处有事实错误。

**使用范围**（grep 全仓 `#include <common/cpp/worker_context.h>`）：

| 模块 | 文件 |
|------|------|
| agent | master_agent.h、worker_agent.h、master_agent_test.cpp |
| storage | database.h、database.cpp、database_test.cpp |
| 其他所有模块 | **0** |

报告"仅 agent+storage 用"——**完全准确**。

**内容偏向领域对象**（读 worker_context.h 全文）：
- 全 `static inline thread_local` 回调注册中心（无实例数据）
- 字段全偏 agent↔storage 协作：`record_write/register_write/notify_freeze/request_remove/set_var/get_var`
- 与 common 真正的公共类型（`CMString`、`FlyBuffer`、枚举）抽象层次不同

**关键反证 —— 放 common 是有记录的有意决策**：
- `docs/common/module.md:156-159` 设计决策表：
  > "WorkerAgentContext 放在 common 是因为 Database（storage 层）和 WorkerAgent（agent 层）都需要访问。避免循环依赖：common → (无依赖)，storage → common，agent → common"
- `docs/agent/module.md:297` 设计决策表：
  > "std::function + lambda 回调 — WorkerAgentContext 不依赖 Agent 头文件，保持模块独立"

放 common 是为打破 `storage → agent` 或反向的循环依赖——agent 的回调被 storage 的 Database 调用，若放 agent 则 storage 须 deps agent，但 agent 本就要 deps storage，构成环。放 common 是**架构妥协**，不是错放。

### 判定：部分真（措辞偏重）

| 维度 | 结论 |
|------|------|
| 仅 agent+storage 用 | 属实 |
| 文件名/类名与 common 定位不符 | 属实（领域对象 vs 公共类型） |
| "错放 common" | **不准确**——是有意决策，文档明确记录为打破循环依赖 |
| 类名 `WorkerContext` | **错误**——实际是 `WorkerAgentContext` |

真正可改进的瑕疵（报告未提）：
- `docs/common/module.md:17` 把文件名误写为 `writer_context.h`（实际 worker_context.h）
- `docs/common/module.md:134-154` 给出的 WorkerAgentContext 签名**严重过时**（写的是实例方法+4 参数，实际是全静态 thread_local+3 参数）
- `docs/agent/module.md:18` 把它列为 agent 组件并标注 `cpp/worker_context.h`，但该路径在 agent 模块下不存在

理想解：抽独立 `context`/`bridge` 模块（既不污染 common 公共类型层，也不引入循环依赖），但当前态是可接受的妥协。

---

## 问题 4：Database/DataService TempStore 双持

### 报告原文
> Database（门面）vs DataService（单例元数据）—— TempStore 双持是真实冗余，文档未解释为何两层都有。**判定：真冗余**

### 核实证据

**三处 temp 存储**（报告只说两处，实际三处）：

| 位置 | 类型 | 字段 |
|------|------|------|
| `Database::temp_store_`（database.h:197） | `CMUniquePtr<fly::TempStore>` | 独立实例 |
| `DataService::temp_eviction_store_`（data_service.h:303） | `CMUniquePtr<fly::TempStore>` | 独立实例 |
| `DataService::temp_entries_`（data_service.h:298，**报告未提**） | `ConcurrentUnorderedMap<CMString, CMString>` | 第三个 map |

**两条互不相通的写入路径**：

```
路径 A（活跃）：
  database.py:66 _write_temp_pickle
    → Database::put_temp_data (database.cpp:741)
      → DataService::on_temp_write_started / on_temp_write (752)
        → local_idx_[db][short].temp_compressed_data_ = FlyBufferPtr
        → LRU 满 → temp_eviction_store_->put (data_service.cpp:1246)
  读取：DataService::try_read_local* 共 6 处回退到 temp_eviction_store_

路径 B（遗留，死）：
  Database::put_temp (database.cpp:717)
    → temp_store_->put
    → DataService::mark_temp_entry → temp_entries_ (data_service.cpp:1159)
  读取：Database::get_temp / has_temp
```

**两条路径数据互不流动**：A 不碰 `temp_store_`/`temp_entries_`；B 不碰 `temp_eviction_store_`/`local_idx_`。无任何代码在两者间搬运。

**路径 B 是死代码**：
- `storage_export.cpp:305-314` 导出 `_put_temp`/`_get_temp`/`_has_temp`
- 全仓 Python grep（src+qa+big_qa+scripts）：**零调用** `_put_temp`/`_get_temp`/`_has_temp`
- 实际 temp 写入走路径 A（`database.py:66 _write_temp_pickle`）
- 唯一仍被触达的是 `_remove_temp`（database.py:141 调用），但它只是对称删除——无写入方就永远删不到东西

**`temp_entries_` + 4 方法（mark_temp_entry/unmark_temp_entry/is_temp_entry/get_temp_data）也是死的**：
- 仅被路径 B 的 `put_temp`/`remove_temp` 写
- 读取者 `is_temp_entry`/`get_temp_data` 除导出绑定外无消费者

**文档验证**：
- `docs/storage/module.md:69-81`「Temp 写入流程」**只描述路径 A**（write_temp_pickle → DataService）
- 完全未提及 `Database::temp_store_`
- 报告说的"文档未解释为何两层都有"属实

### 判定：真冗余（且比报告描述更严重）

- **路径 A（`put_temp_data`/`temp_eviction_store_`/`local_idx_`）合理且活跃**，保留
- **路径 B（`Database::temp_store_` + `put_temp`/`get_temp`/`has_temp` + 导出）死代码**，可删
- **第三处冗余 `DataService::temp_entries_` + 4 方法 + 导出**死代码，可删
- `_remove_temp`（database.py:141 唯一活跃调用方）的语义需迁移到路径 A 或评估是否仍需要

---

## 解决方案与计划

> 设计原则：问题 1 与问题 2 同源（FlyBuffer 归属），合并处理；问题 3 维持现状补文档；问题 4 独立清理死代码。

### 阶段 0：文档勘误（零代码风险，先做）

| 项 | 文件 | 修复 |
|----|------|------|
| D1 | `docs/common/module.md:17` | `writer_context.h` → `worker_context.h` |
| D2 | `docs/common/module.md:134-154` | WorkerAgentContext 签名同步到实际代码（全静态 thread_local、3 参数 record_write、含 var 服务方法） |
| D3 | `docs/agent/module.md:18` | 删除"`cpp/worker_context.h`"路径标注（该文件不在 agent 模块） |
| D4 | `docs/redundancy-audit-report.md` 第五类 #3 | 类名 `WorkerContext` → `WorkerAgentContext`；判定从"真问题"改为"有意妥协 + 文档过时" |

### 阶段 1：FlyBuffer 下沉（解决问题 1+2）

**目标**：把 `FlyBuffer`/`FlyBufferPtr` 从 `serialization` 迁到 `common`，消除 common→serialization 的源码跨层引用，让上游模块不再为用一类型拖整个 serialization 栈。

**步骤**：
1. `git mv src/serialization/cpp/fly_buffer.h src/common/cpp/fly_buffer.h`
2. 全仓替换 include 路径：
   - `<serialization/cpp/fly_buffer.h>` → `<common/cpp/fly_buffer.h>`
   - 涉及 storage(11)、network(5)、serialization(1)、common(1) 共 18 个直接 include
   - `serialization_macros.h:41` 的 include 改路径
3. `src/common/cpp/BUILD`：`fly_common_types` 的 `hdrs = glob(["*.h"])` 自动覆盖新文件，无需改 BUILD
4. 验证：`./fly.sh build //src/...` 全量编译通过
5. 验证：grep 确认全仓无 `<serialization/cpp/fly_buffer.h>` 残留
6. 文档同步：
   - `docs/common/module.md:19,70-95`：移除"位置 src/common/cpp/" 的矛盾（现在真在 common 了）
   - `docs/serialization/module.md`：在「核心文件」表补注"FlyBuffer 已迁至 common 模块"

**收益**：
- common BUILD 不再"缺失依赖声明"（问题 1 根除）
- FlyBuffer 归属与文档一致（问题 2 解决）
- storage/network 的 BUILD 可评估是否移除对 `//src/serialization/cpp:fly_serialization` 的强制依赖（若它们只用 FlyBuffer 不用序列化宏）—— 此为后续优化项，不在本阶段

**风险**：低。纯 include 路径替换 + 文件移动，不改任何逻辑。

### 阶段 2：TempStore 死代码清理（解决问题 4）

**目标**：删除路径 B 整套死代码，保留活跃的路径 A。

**删除清单**：
| 文件 | 删除内容 |
|------|---------|
| `database.h:84-86` | `put_temp`/`get_temp`/`has_temp` 声明 |
| `database.h:197` | `CMUniquePtr<fly::TempStore> temp_store_` 成员 |
| `database.cpp:63` | `temp_store_ = CMMakeUnique<fly::TempStore>()` 构造 |
| `database.cpp:717-729` | `put_temp`/`get_temp`/`has_temp` 实现 |
| `database.cpp:731-735` | `remove_temp` 实现（见下方说明） |
| `storage_export.cpp:305-314` | `_put_temp`/`_get_temp`/`_has_temp` 导出 |
| `storage_export.cpp:499-505` | `ex_stg_mark_temp_entry`/`ex_stg_unmark_temp_entry` 导出 |
| `data_service.h:211-214` | `mark_temp_entry`/`unmark_temp_entry`/`is_temp_entry`/`get_temp_data` 声明 |
| `data_service.h:298` | `ConcurrentUnorderedMap<CMString, CMString> temp_entries_` 成员 |
| `data_service.cpp:1158-1173` | 4 个方法实现 |

**关于 `remove_temp` / `_remove_temp` 的特殊处理**：
- `database.py:141` 仍调用 `self._db._remove_temp(name)`
- 但路径 B 无写入方，`remove_temp` 实际永远删不到东西
- **决策**：先确认路径 A 是否需要等价的 remove 语义。若路径 A 的 temp 由 LRU 自动淘汰（`temp_eviction_store_` + `on_temp_write` 的 LRU），则 `_remove_temp` 的主动删除语义可能本就未被路径 A 支持——需查 `database.py:141` 的调用上下文（`_write_temp` 失败回滚？显式删 temp？）

**步骤**（TDD）：
1. 先写一个 grep 测试脚本，确认删除前 `_put_temp` 等导出在 Python 侧零调用
2. 删除上述清单
3. `./fly.sh build //src/storage/...` 编译通过
4. `./fly.sh test //src/storage/tests:database_test` + `data_service_test` 通过
5. `./fly.sh install && qa/runqa qa/storage` 通过
6. 文档同步：
   - `docs/storage/module.md`：补注"temp 数据统一由 DataService 管理，Database 不自持 TempStore"
   - 更新 module.md:69-81 流程图移除路径 B 残留描述（如有）

**风险**：中。需确认 `_remove_temp` 的 Python 调用方语义是否丢失。

### 阶段 3：worker_context.h 维持现状（问题 3）

**决策**：不迁移 `worker_context.h`。理由：
- 放 common 是文档明确记录的有意决策（打破循环依赖）
- 迁移到 agent 会引入 storage→agent 依赖（agent 已 deps storage，构成环）
- 迁移到 storage 会让 agent 依赖 storage 的细节
- 抽独立 `context` 模块是理想解但工作量与收益不成比例（仅 1 个文件）

**仅做文档勘误**（已在阶段 0 D1-D3 覆盖）。

---

## 执行顺序与验收

| 顺序 | 阶段 | 工作量 | 风险 | 验收命令 |
|------|------|--------|------|---------|
| 1 | 阶段 0 文档勘误 | 小 | 零 | 文档审阅 |
| 2 | 阶段 1 FlyBuffer 下沉 | 中（18 处 include + 1 次 git mv） | 低 | `./fly.sh build //src/...` + grep 残留 |
| 3 | 阶段 2 TempStore 死代码清理 | 中（删 ~80 行 + 2 处导出） | 中 | `./fly.sh test //src/storage/...` + `qa/runqa qa/storage` |

**全局验收**（所有阶段完成后）：
```bash
./fly.sh check          # build + test + clangd refresh
./fly.sh install
./qa/runqa              # 全量 QA
```

每个阶段独立提交，便于回滚。

---

## 报告准确性总评

| 报告项 | 报告判定 | 实际 | 报告准确性 |
|--------|---------|------|-----------|
| #1 common 反向依赖 serialization | 真问题（循环依赖风险） | 部分真（无循环，是缺失依赖声明） | 事实准但判定偏重 |
| #2 FlyBuffer 归属错位 | 真问题 | 真问题（更严重，跨 5 模块） | 准确，范围低估 |
| #3 worker_context.h 错放 | 真问题 | 有意妥协（类名还写错） | 判定偏重，类名错误 |
| #4 TempStore 双持 | 真冗余 | 真冗余（实际三层） | 准确，范围低估 |

报告整体方向正确（4 项中 2 项完全准确、2 项事实准但措辞偏重），无幻觉或误判。主要不足是低估了 #2/#4 的实际范围，以及 #3 的类名笔误和未识别其有意决策属性。
