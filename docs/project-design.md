# Project 机制设计与实现方案

> 状态：**已实现**（TDD，2026-07-29）
> 制定日期：2026-07-29
> 关联：`docs/architecture.md` §3.3（双路径契约）、§5.3（freeze）；`docs/adr/0001-db-meta-and-load-db.md`；`docs/db-merge-design.md`（master-only 编排范式）

---

## 0. 摘要

提供 **`fly.Project` 高层管理对象**：把一整条业务流程打包为一个 project，由 project 统一管理流程各步骤产生的 db。Project 是比 db 更高一级的管理单元，提供固定业务流程的启动 API，每个流程 API 内部创建存储数据的 db 并返回。

**核心定位**：
- db = 单个数据集合（原子存储单元）
- Project = 业务流程容器（管理多个 db + 提供流程 API）

**三个核心设计决策**：
1. **流程 API 注册制**：Project 基类只提供机制（建库/取库/冻结/持久化/load），不含业务流程；每个流程 API 是普通函数，通过 `@register_flow(子类)` 注册到指定 Project 子类，实现拆分到不同模块，避免基类臃肿。
2. **流程 API 自己建库返回**：不暴露通用 `create_db`；每个 flow 内部用 `self._create_db(name)` 建库并 `return db`。`name` 既是 db 子目录名（actual_name），也是 Project 内部 key。重名 → WARN log 提醒 + 自动递增（`name.1`、`name.2`）。
3. **跨流程数据依赖显式传入**：flow 间不默认传递数据；若某 flow 需用另一 db 的数据，由用户**显式传 db 对象**（如 `solve(name, matrix_db, ...)`），支持使用 project 内或 project 外的 db 作输入。
4. **flow 异步范式（核心原则）**：master 侧 flow 只做 4 件轻量事——①检查输入 ②建库
   ③调用 flow 的入口 task 函数（`@as_task` 提交，非阻塞）④调用 db 的 freeze task 函数
   （`@as_task` 提交，inputs 依赖入口 task 写的数据）。提交后**立即返回 db**，不做同步等待。
   实际计算（写数据、求解迭代）在 worker task 上异步执行，进度由 master 依赖图调度推进。
   freeze 本身是 task：依赖上游数据写完后由 master 调度执行，task 内 `db.freeze()` 通知
   master 更新 db frozen 状态。**flow 不负责 worker 池管理**——用户脚本预先唤起必要 worker。

**纯 Python**：零 C++，复用现有 `fly.open_db` / `fly.load_db` / `@as_task` / `db.freeze()` / freeze-as-task 机制。

**首个模板**：`SolverProject` 把现有 RAS solver 包装成两个流程 API（`build_matrix` / `solve`），作为用户二次开发的参考。

---

## 1. 存储模型（复用现有 db 契约）

Project 自身只存**必要元信息**（下属 db 的路径列表 + 状态），db 的实质数据仍由各 db 自管。

### 1.1 磁盘布局

```
my_project/                              ← Project 主目录（project base_path）
├── _PROJECT_META.json                   ← Project 元信息（纯 JSON，见 §1.2）
├── matrix/                              ← build_matrix(name="matrix") 产出
│   ├── _DB_META  _DB_META.lock  _FROZEN  _VARS  *.idx
│   │       ← _DB_META 为 JSON version 2 统一元信息（2026-08-26 合并
│   │         _DB_CHAIN + bitsery _DB_META：uid/role/prev/next/data_path/
│   │         workers，见 docs/db-chain-design.md §3）
│   ├── failed_tasks.bin                 ← 归属本 db 的失败 task 断点记录
│   └── data_*.dat                       ← 矩阵数据（write_object pickle）
├── matrix.1/                            ← 第二次 build_matrix("matrix")（WARN + 自动递增）
├── solve/                               ← solve(name="solve") 产出
│   ├── _DB_META  ... *.idx  failed_tasks.bin
│   ├── data_*.dat                       ← 求解过程 + 结果
│   └── matrix.npz                       ← API2 从 matrix_db 还原的工作 npz
└── ...
```

`_create_db(name)` → `base_path = os.path.join(project_dir, name)`，内部调 `fly.open_db(base_path)`（已存在则自动递增 `name.1`），记入 meta。

### 1.2 `_PROJECT_META.json` 结构

```json
{
  "class": "solver.project.SolverProject",
  "project_id": "a3f2b1",
  "created_at": 1789000000,
  "dbs": {
    "matrix": {
      "logical_name": "matrix",
      "base_path": "my_project/matrix",
      "db_id": "a3f2c9d0",
      "created_at": 1789000010,
      "frozen": true
    },
    "matrix.1": {
      "logical_name": "matrix",
      "base_path": "my_project/matrix.1",
      ...
    },
    "solve": { "logical_name": "solve", ... }
  }
}
```

- **顶层 key = actual_name**（磁盘子目录名，唯一，对应 load 目标）。
- **logical_name** = 用户传入的 `name`（可能重复，用于 `get_db(name)` 查找）。
- **class** = `f"{module}.{qualname}"`，load 时按此动态还原子类（见 §6）。
- **project_id** = 6 位随机 hex（构造时生成，持久化后稳定，便于日志追踪）。
- **frozen**：建库/freeze 时用 `db.is_frozen()` 刷新，便于 load 前预判。

每次 `_create_db` / `freeze_db` 后增量更新（重写整个 JSON；低频操作，可接受）。

---

## 2. API 设计

### 2.1 门面签名（`fly.*`）

```python
fly.open_project(path: str) -> "Project"       # 新建/绑定（基类 Project，纯机制壳）
fly.load_project(path: str) -> "Project"        # 全量恢复（master-only，还原真实子类）
fly.register_flow(target_cls)                    # 装饰器：注册 flow 到子类
fly.Project                                      # 基类（供继承）
```

新建带 flow 的 project 用具体子类构造（如 `SolverProject(path)`），最直接；`fly.open_project(path)` 返回基类 Project（无 flow），供纯机制场景或二次开发者。

### 2.2 流程 API 约定（文档约定，不强校验，保持灵活）

注册为 flow 的函数遵循 **异步 4 步原则**（master 侧只做轻量事）：
- 首参 `self`（自动绑定，描述符协议）；
- 第二个位置参数 **`name`**（db 子目录名 actual_name + Project 内部 key）；
- **Step 1** 检查输入参数；**Step 2** `self._create_db(name)` 建库；
  **Step 3** 调用 flow 的入口 task 函数（`@as_task` 提交，非阻塞）；
  **Step 4** 调用 freeze task 函数（`@as_task` 提交，inputs 依赖入口 task 写的数据）；
- `return db`（提交后立即返回，不等计算完成）；
- 跨流程数据依赖由用户显式传 db 参数（该 db 作为入口 task 的 inputs 依赖源）。

### 2.3 Project 基类 API（`src/fly/project.py`）

| 方法 | 可见性 | 说明 |
|------|--------|------|
| `_create_db(name, data_path="")` | protected | flow 内部建库；重名 WARN + 递增；记 meta；缓存句柄；返回 db |
| `_freeze_task_deps(db, depends_on)` | protected | 构造 freeze task 的 inputs（依赖对象 full_name）；freeze 作 task 由 master 调度 |
| `get_db(name, latest=False)` | public | 取 db：默认按 actual_name 精确匹配；latest=True 按 logical_name 取最新版；缓存命中返回，否则 load_db 恢复（master-only） |
| `is_db_frozen(name, latest=False)` | public | 懒查询 master 权威 frozen 状态（confirmed ∪ pending），master-only |
| `wait_frozen(name, timeout, latest=False)` | public | 阻塞等异步 freeze task 完成（轮询 db 句柄 frozen 标志） |
| `freeze_db(name, latest=False)` | public | 同步冻结（master 本地，阻塞；非 flow 异步路径） |
| `freeze_all()` | public | 同步冻结所有未 frozen 的 db |
| `list_dbs()` | public | 返回所有 actual_name（get_db 的精确匹配键） |
| `list_flows()` | public | 返回已注册的 flow 名（内省） |
| `save()` | public | dump meta → _PROJECT_META.json |
| `load(path)` | classmethod | 读 meta + 动态还原子类 + 对每个 db 调 fly.load_db（master-only） |

**get_db 语义**（精确匹配优先）：
- `get_db("matrix")` → 精确匹配 actual_name "matrix"。
- 重名递增后，`get_db("matrix.1")` 取递增产物；`get_db("matrix", latest=True)` 取该 logical_name 最新版。

---

## 3. 流程注册机制（核心）

Project 基类**只提供机制**，不含业务流程。每个业务流程 API 是普通函数，通过 `@register_flow(target_cls)` 注入到指定 Project 子类。

```python
def register_flow(target_cls):
    """把函数注册为 target_cls 的流程方法。"""
    def decorator(func):
        existing = target_cls.__dict__.get(func.__name__)
        if existing is not None:
            WARN(f"register_flow: overriding existing flow '{func.__name__}' "
                 f"on {target_cls.__name__}")
        setattr(target_cls, func.__name__, func)   # 注入为类方法
        target_cls._flows[func.__name__] = func     # 注册表（内省用）
        return func
    return decorator
```

**为什么可行**：`setattr(cls, name, func)` 注入普通函数后，Python 描述符协议让实例访问时自动绑定 `self`，与类体内定义的方法行为完全一致。函数体内可自由访问 `self._create_db()`、`self.base_path` 等。

**为什么这样设计**：让流程实现分离到独立模块（如 `solver/flows.py`），基类不臃肿；不同业务模块各自定义子类 + flow 模块。若流程增多，可再拆 `flows/build.py`、`flows/solve.py` 等。

---

## 4. SolverProject 模板（业务示例）

把现有 RAS solver 包装成两个流程 API，作为二次开发参考。**不改动现有 solver 代码**。

### 4.1 文件组织

```
src/solver/py/
├── project.py      # class SolverProject(fly.Project): pass  + import solver.flows（触发注册）
├── flows.py        # @register_flow(SolverProject) 的 build_matrix / solve 实现
└── __init__.py     # 导出 SolverProject（import 时触发 flows 注册）
```

### 4.2 build_matrix（流程 API 1，异步）

```python
@register_flow(SolverProject)
def build_matrix(self, name: str, matrix_path: str):
    # Step 1: 检查输入
    if not os.path.isfile(matrix_path): raise ValueError(...)
    # Step 2: 建库
    db = self._create_db(name)
    # Step 3: 提交入口 task（写 matrix，worker 执行，非阻塞）
    m = _load_matrix(matrix_path)
    _write_matrix_task(db, m)
    # Step 4: 提交 freeze task（inputs 依赖 matrix 写完）
    _freeze_db_task(db, self._freeze_task_deps(db, ["matrix"]))
    return db          # 立即返回，不等
```

### 4.3 solve（流程 API 2，异步 + kickoff task）

solve 需读 `matrix_db` 的矩阵启动求解，但 matrix 是 build_matrix 异步写的。通过 **kickoff
task**（inputs 依赖 `matrix_db` 的 matrix）让 master 在 matrix ready 后调度，完全异步：

```python
@as_task(inputs=lambda db, matrix_db, nsd, ...:
         [matrix_db.get_full_name("matrix")])
def _solve_kickoff_task(db, matrix_db, nsd, ...):
    m = matrix_db.read_object("matrix")      # matrix ready 后才执行
    work_npz = os.path.join(db.get_base_path(), "matrix.npz")
    np.savez(work_npz, ...)                   # 适配层：还原工作 npz
    ras_graph_coord(db, work_npz, nsd, ...)   # coord 非阻塞，自驱动迭代链

@register_flow(SolverProject)
def solve(self, name, matrix_db, nsd, overlap_ratio=0.50, ...):
    # Step 1: 检查输入
    if nsd < 1: raise ValueError(...)
    # Step 2: 建库
    db = self._create_db(name)
    # Step 3: 提交入口 task（kickoff：依赖 matrix_db 的 matrix）
    _solve_kickoff_task(db, matrix_db, nsd, overlap_ratio, ...)
    # Step 4: 提交 freeze task（依赖 __rasg__sol 求解完成）
    _freeze_db_task(db, self._freeze_task_deps(db, ["__rasg__sol"]))
    return db          # 立即返回，求解进度由 master 调度推进
```

**关键设计点**：
- kickoff task 的 inputs 是 `[matrix_db.get_full_name("matrix")]`（跨 db 间接依赖，master 依赖
  图按 `db_path:short` 全名索引，跨 db 成立）。matrix_db 作为 task 参数经 `__fly_db2__:{uid}:{db_path}`
  协议序列化传给 worker，worker 按 db_path 重建句柄（uid 校验，编码详见 db-chain-design.md）。
- `ras_graph_coord` 在 kickoff task（worker）内执行：非阻塞提交第一轮 compute/check，
  check 在 worker 内提交下一轮，整个迭代链由 master 调度自驱动；收敛时 assemble 写 `__rasg__sol`。
- freeze task 依赖 `__rasg__sol`，求解完成才 freeze。
- **用户须预先唤起足够 worker**（带 `sd_{i}` attributes，>= nsd）。flow 不碰 worker 池。

**适配层说明**：现有 `solve_ras_graph` 的 coord 需要 `matrix_path`（文件，worker 进程级缓存读
矩阵）。kickoff task 从 `matrix_db` 读矩阵后还原成 solve db 目录下的工作 `matrix.npz` 再调 coord。
临时 npz 放 solve db 的 base_path（同机/共享 FS 假设，与 solver 既有假设一致）。

### 4.4 使用范式（异步：用户唤起 worker + 等结果）

```python
import fly
from solver import SolverProject

fly.launch_workers([{"attributes": [f"sd_{i}"]} for i in range(4)])  # 用户唤起 worker
proj = SolverProject("./my_project")
matrix_db = proj.build_matrix(name="matrix", matrix_path="poisson_n20.npz")  # 异步返回
result_db = proj.solve(name="solve", matrix_db=matrix_db, nsd=4, omega=1.0)  # 异步返回

# 等待全流程完成（freeze 依赖 __rasg__sol，故 frozen 即求解完成）
assert proj.wait_frozen("matrix") and proj.wait_frozen("solve")
result = result_db.read_object("__rasg__sol")   # 读对外结果对象（save_to_db=True）
```

**temp 对象语义**：solver 内部迭代用的 temp 对象（`__rasg__iters`/`__rasg__converged`/
`__rasg__x_*` 等，`save_to_db=False`）仅限流程内部使用，freeze 时会被清理——外部流程/用户
**不该读 temp 对象**。对外结果用持久化对象（`__rasg__sol`，`save_to_db=True`）。

---

## 5. load 流程（master-only，全量恢复）

`fly.load_project(path)` → `Project.load(path)`：

1. 读 `_PROJECT_META.json`；
2. 按 `meta["class"]` 用 `importlib.import_module` + `getattr` 动态还原真实子类（SolverProject 等）；import 失败回退基类 Project + WARN（flow 不可用但 db 数据仍可读）；
3. 用真实子类构造实例（触发子类模块 import，flow 注册随之完成）；
4. 对每个 db 调 `fly.load_db(base_path)`（全量恢复索引 + 按需拉起 worker，master-only）；
5. 缓存所有句柄；返回实例。

这样 `fly.load_project(path)` 恢复出的实例是 `SolverProject`，注册的 `build_matrix` / `solve` 随之可用。

---

## 6. 关键设计权衡记录

### 6.1 为什么用注册制而非类体继承写 flow

类体继承会让基类膨胀，且不同业务流程混在一起难管理。注册制让每个业务模块**自带一个子类 + 一个 flow 模块**，互不干扰，新增业务模块零触碰基类。

### 6.2 为什么不暴露通用 `create_db`

通用 `create_db` 让"建库"和"业务流程"脱钩，用户会绕过 flow 直接建一堆无语义的 db。改为"flow 自己建库返回"，强制每个 db 都绑定一个明确的业务语义。

### 6.3 为什么跨流程数据依赖要显式传

显式传入让数据流可见、可追溯；且支持用 project 外的 db 作输入（如用户外部预处理的矩阵 db）。默认传递会形成隐式耦合，破坏可复用性。

### 6.4 为什么重名 WARN 而非报错

报错会中断断点重启场景（用户重跑同名流程）。WARN + 自动递增保留历史运行（`name.1`）。
递增产物是独立 actual_name，`get_db` 精确匹配不会误取；需要最新版用 `get_db(name, latest=True)`。
与现有 `fly.open_db` 行为一致（`__init__.py:60-68`）。

### 6.5 为什么 meta 存 class 字段

load 时需还原真实子类，否则注册的 flow 方法不可用。存 `module.qualname` 用 importlib 动态还原是标准做法。回退基类保证 meta 损坏/class 路径失效时数据仍可读。

### 6.6 为什么 get_db 默认精确匹配而非取最新版

精确匹配让重名产物（`db.1`/`db.2`）成为独立、可寻址的实体——用户能精确取到任意一次运行
的 db，不会因"同名最新"而意外拿到非预期的版本。需要最新版时显式 `latest=True`。这是"可
预测优先"的原则：默认行为不留惊喜。

### 6.7 为什么 flow 是异步 4 步（freeze 作为 task）

master 侧 flow 只做检查输入/建库/提交入口 task/提交 freeze task 四件轻量事，提交后立即返回 db。
重计算在 worker task 异步执行，进度由 master 依赖图调度推进。这样：
- flow 调用是非阻塞的，用户可连续提交多个 flow，由 master 统一调度；
- freeze 作为 task（inputs 依赖入口 task 写的数据），在数据就绪后由 master 调度执行，
  task 内 `db.freeze()` 通知 master 更新 frozen 状态（stream 模式即时确认+广播；非 stream
  模式 task 成功时 `commit_pending_frozen` + 广播，见 `master_agent.cpp:1091`）；
- 跨流程数据依赖（如 solve 的 kickoff task 依赖 matrix_db 的 matrix）通过 task 的 inputs
  间接依赖实现，master 自动在数据 ready 后调度下游，无需用户手动同步。

**flow 不负责 worker 池管理**：拉 worker 是系统级操作，属用户/部署层职责。flow 提交的 task
需要 worker 才能执行，故用户脚本须预先唤起足够 worker（如 solver 需带 `sd_{i}` attributes）。

---

## 7. 复用清单（避免造轮子）

| 需求 | 复用 | 位置 |
|------|------|------|
| db 创建 + 已存在递增 | `fly.open_db` | `src/fly/__init__.py:47` |
| db 恢复（索引+worker） | `fly.load_db` → `Master.load_db` | `src/agent/py/agent.py:277` |
| 冻结语义 | `db.freeze()` / `db.is_frozen()` | `src/storage/py/database.py:128` |
| 从 npz 读矩阵 | `_load_matrix` | `src/solver/py/ras_graph.py:116` |
| RAS 求解（自动拉 worker） | `solve_ras_graph` | `src/solver/py/ras_graph.py:1001` |
| 方法注册 | `setattr(cls, name, func)`（描述符自动绑 self） | Python 标准做法 |
| pickle 支持范式 | `MapReduceJob.__getstate__` | `src/fly/mapreduce.py:403` |
| 双 import 兼容 | `try: ... except ImportError:` | 全代码库惯例 |

---

## 8. 潜在风险与对策

| 风险 | 对策 |
|------|------|
| load_project 对大量 db 全量 load 慢 | meta 记 frozen；本次先全量，未来可加懒加载选项 |
| solve 工作 npz 跨机 worker 读不到 | 模板假设同机/共享 FS（与 solver 既有假设一致）；跨机由用户把 matrix_path 指向共享盘 |
| get_db 重复 load 同一 db | `_db_cache` dict 缓存句柄 |
| 重名丢失旧 db 引用 | meta 的 `dbs` 保留所有 actual_name（含 matrix.1）；get_db 取最新版，旧版仍可 load_project 扫回 |
| 子类 load 还原失败（class 路径错） | load 时 try/except，import 失败回退基类 Project + WARN |
| 注册模块未 import 导致 flow 缺失 | 子类模块顶层 import 自己的 flows（如 `solver/project.py` import `solver.flows`）；文档强调 |

---

## 9. 设计约束呼应

- **"纯管理类，纯 Python"**：零 C++，复用现有 db/solver API。✅
- **"流程启动 api 返回 db，name 为内部 key"**：每个 flow 自建库返回，name = 子目录名 = meta key。✅
- **"重名 log 显式提醒"**：WARN + 自动递增。✅
- **"跨流程显式传 db"**：solve(name, matrix_db, ...)，用户显式传。✅
- **"注册 api 到 project 类，分离模块"**：`register_flow(target_cls)` 装饰器，flow 拆到独立模块。✅
- **"独立目录，子 db 自动子目录"**：base_path = project_dir/name。✅
- **"像 db 一样 load，只存必要元信息"**：_PROJECT_META.json 仅存路径+状态+class；load 全量恢复。✅
- **"包装现有 solver 为模板"**：SolverProject + flows，复用 solve_ras_graph。✅

---

## 10. task 级断点重跑（resume）

> 2026-08-25 增补。推翻 roadmap 决策④「F6 stage checkpoint 不做」中的粒度
> 选择：断点粒度 = **task 级**（db 级 checkpoint 的粗粒度语义由 frozen db
> 继续承担，task 级补齐中断恢复）。

### 10.1 核心设计：不记录、不检测已完成 task

已完成 task 的**输出对象**就是完成证据——调度本身是对象驱动的
（`DependencyGraph::mark_data_ready`），恢复时只需让全部已完成输出（正式
+ temp）重新 ready，未完成 task 重投后依赖图自然收敛。**无需 task 日志、
无需 COMPLETE 记录、无需跳过判定**。

### 10.2 temp 落盘（前置基建）

`save_to_db=False` 的 temp 对象从纯内存（TempStore LRU）改为「内存 LRU
（TIER1 读缓存）+ db 目录专用文件落盘」：

```
db_path/
├── temp_data_{writer_id}_{NNN}.dat   # temp 数据专用（与正式 data_*.dat 隔离）
├── {writer_id}.temp.idx              # temp 专用 idx（op-log 事务段，格式同正式 idx）
```

- **write-through**：`put_temp_data` 同步落盘 + idx ADD 立即 save（idx 的
  ADD 是内存 pending、save 才写文件，不 flush 则 task END 先于 ADD 落盘，
  恢复时段内无记录）；
- **事务段**：temp 写入纳入 task 写追踪（`tracked_writes`），失败
  ABORT 同段回滚（idx 段丢弃 + 数据 truncate）；
- **恢复**：`load_db` 的 IdxLoad 同批加载 `.temp.idx` → worker
  `restore_temp_entries` + master `rebuild_remote_idx_for_worker`
  `mark_data_ready`——下游 task 输入就绪；
- **freeze 清理**：db freeze 确认后删除全部 temp 文件（用户裁定语义：
  temp=中间态，阶段完成即作废）。frozen db 的 Database 不再建 temp writer。

### 10.3 failed_tasks.bin 按归属 db 落盘

失败记录按 task 归属 db 落盘：`{owner_db_path}/failed_tasks.bin`（Task db
归属规则，见 DEVELOPMENT_GUIDELINES §15）。归属由 `TaskSubmissionSpec.
owner_db_path_` 携带（显式 `@as_task(owner=)` 或 master 从第一个 db 参数
推导，随 FailedTaskRecord 持久化、随 restart 重投还原）。project 场景
db 目录在 project 下，断点 bin 天然随 db 目录迁移/自包含，且天然支持多
project（无归属 task fallback `{log_dir}/failed_tasks.bin`）。

恢复入口两形态：

- `fly.restart_failed_tasks(dbs)`：传 db 对象 / db_path / list，自动在各
  db 目录搜索 bin 重投（无 bin 的 db 静默跳过，返回重投总数）；
- `Project.resume()`：内部遍历 project 全部 db 目录，同一机制。

**位置即归属**：bin 所在目录是归属的运行时权威，`restart_failed_tasks`
读取时把记录内 owner（提交时路径快照）归一化为 bin 父目录——db/project
目录迁移后读取天然自愈。**uid 解析（2026-08-26）**：记录的 `args_`/
`inputs_`/`vars_` 路径快照由 restart 按运行时 uid 索引统一解析替换
（uid 迁移/merge 不变；args 重编码 `__fly_db2__`、inputs/vars 前缀替换），
文件级原子——任一 db 引用无法解析则整 bin 保留待重试。

旧的 `set_failed_tasks_file` 路径覆盖机制已废弃。bitsery 非版本化——旧
格式 bin 新版本不读（解码失败静默丢弃；早期无存量不做迁移）。

### 10.4 恢复入口与语义边界

```python
proj = fly.load_project(path, resume=True)   # 恢复全部 db + 重投未完成 task
```

- **优雅中断**（stop / SIGTERM / drain 超时）：FAILED 实时 persist +
  PENDING/RUNNING 由 stop 路径 persist → bin 完整 → resume 完整恢复；
- **SIGKILL/OOM**：RUNNING/PENDING 的 spec 丢失（无 persist 时机）→
  resume 只恢复此前 FAILED 记录，其余靠 **flow 重放兜底**（同
  `write_context_hash` 幂等重写 + ready 对象下游直通，收敛不重算持久
  边界内的工作）；
- 正确性根基 = write 幂等（provenance 同 hash 允许重写）+ idx 事务段
  （崩溃段 load 时丢弃）。

---

## 11. project 整体迁移（migrate_project）

```python
fly.migrate_project(path, new_path="", *, consolidate=False, local_workers=4)
```

**全程无超时**（数据量与集群 IO 速度不可预估——EDA 数 T 级 db 常见，
等待只被显式失败信号终结）。

### 11.1 目录搬迁（new_path 非空，离线）

不要求 frozen（半成品 db 靠 idx 事务段 + ABORT truncate 保证搬后可恢复）：

1. 校验：db 目录存在；data 自包含（meta 的 `data_path` 在 project 外 →
   拒绝并提示先 consolidate）；
2. `shutil.move`（含 failed_tasks.bin，断点能力随迁）；
3. meta db_path/data_path 改写（tmp + `os.replace` 原子）；
4. `_DB_CHAIN` 邻居边改写：prev/next 中指向 project 内 db 的 db_path 按
   旧→新映射更新（uid 不变，project 外的边不动）。

中断重入安全：目录已搬而 meta 未改时，`load_project` 的 stale db_path
fallback（`{project_dir}/{actual_name}` 相对定位，actual 本为 relpath）
兜底恢复。

### 11.2 数据集中（consolidate=True，在线，跨主机）

逐 db frozen 校验（merge 前提，未 frozen 报错列出）→ 逐 db `load_db`
（跨进程先恢复源对象索引）→ `merge_db`（产物 data 落 `{db_path}.merged_data`，
在 project 目录内，自包含）→ 可接搬迁。半成品 project 跨主机不支持
（merge 前提 frozen）。

### 11.3 辅助 API

- `Project.status()`：每 db `{actual, logical, db_path, frozen}`（断点导航）；
- `_create_db` meta 补记 `data_path`（自包含校验依据，旧 meta 视为 ""）。
