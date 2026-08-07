# DB Chain 机制设计与实现方案

> 状态：**设计定稿**（2026-08-08）
> 制定日期：2026-08-08
> 关联：`docs/project-design.md`（Project 容器）、`docs/db-merge-design.md`（merge 机制）、`docs/adr/0002-deprecate-db-id.md`（uid 历史背景）
> 前置依赖：本方案**取代** ADR 0002 的 `_MIGRATED_TO` 源路径遗留机制

---

## 0. 背景与动机

### 0.1 当前痛点

当前 solver 分两步流程：`build_matrix` 创建矩阵 db，`solve` 读取矩阵 db 求解。两个 db 之间的关联**仅体现在 API 层**——用户显式把 `matrix_db` 句柄透传给 `solve`，kickoff task 的 `@as_task(inputs=[matrix_db.get_full_name("matrix")])` 声明跨 db 对象依赖。

这种设计对长程、复杂业务流程很不健壮：

- 若新增流程中分了更多 db，且靠后的 db 依赖很靠前的 db，当前方式只能**把所有依赖的 db 透传进 API，并一致透传到对应 job**——对开发者和使用者都过于繁杂。
- solver db 本身无法主动找到矩阵 db，依赖关系是隐式的、靠人维护的。

### 0.2 目标

引入 **db chain（DAG）机制**：把 db 自然组织成有向无环图。靠后的 db 只需直接依赖少数前置 db；当需要更靠前的 db 数据时，沿 DAG 向前查找即可。

```
                    matrix_db (role=matrix)
                          │
                    solve_db (role=solve)         reference_db (role=reference)
                          │                         │
                          └────────┬────────────────┘
                              analysis_db (role=analysis)
```

### 0.3 需要同时解决的遗留问题

实现 db chain 的过程中，发现当前 merge db 的 `_MIGRATED_TO` 源路径遗留机制是一个不健壮的过渡实现：

- merge 后源路径**残留数据**（`_DB_META`/`_FROZEN`/`_MIGRATED_TO`/孤儿 `.idx`），用户不希望数据遗留。
- 源路径寄生性地充当"重定向锚点"，把迁移指针存在被清理对象身上，构成**自举困境**：删了源目录，重定向信息也跟着没了。
- 当用户在原地址复用创建同名 db 时（merge 迁走后又 `open_db` 同一 path），path 既是物理位置又是逻辑身份的耦合直接导致**别名冲突**——两个不同 db 共享同一 path 字符串，旧引用被静默错误解析。

因此本方案**同时解决** db chain 与 merge 遗留问题，两者共享同一套 uid + `_DB_CHAIN` 机制。

---

## 1. 核心原则

**逻辑身份（uid）与物理路径（path）彻底解耦。**

- `uid` = db 的"我是谁"，创建时生成，merge 后**不变**（merge = db 搬家，不是销毁重建）。
- `path` = db 的"现在在哪"，merge 会变，由 master 内存映射维护。
- **身份匹配永远靠 uid，不靠 path。**
- **uid 的正确性范围：单次 run 内。** 跨 run 靠 `load_db` 加载新 db、建立新 uid 体系。跨 run 做破坏性操作（如新起 run 做 merge）允许失败并报错。

### 1.1 历史背景：为什么重新引入 uid

ADR 0002 废弃了 db_id，理由是 idx 改存 short_name 后 db_id 在 idx 里 100% 冗余。但 ADR 0002 用"源 path 永久存在"这个假设回避了 db_id 当年解决的真正问题——**当同一物理路径被复用（merge 迁走后又新建同名 db），path 作为标识符产生别名冲突**。

db_id 当年错在**载体**（冗余存进每条 idx entry）。本方案的 uid 存在 `_DB_CHAIN`（每个 db 一份，不冗余）和 master 内存（一份），既不寄生（在 db 自身目录内）又不冗余。

---

## 2. uid 机制

### 2.1 生成

```python
def _generate_uid(db_path, role):
    """综合 目录 + 纳秒创建时间 + role 生成 uid，单次 run 内极低重复率。"""
    raw = f"{db_path}:{time.time_ns()}:{role or 'none'}"
    return hashlib.sha256(raw).hexdigest()[:12]   # 12 字符 hex
```

- `time.time_ns()` 纳秒精度 → 单次 run 内即使瞬时创建多个 db 也不会撞。
- 跨 run 不保证唯一（符合约束：load_db 加载新 db，不依赖跨 run 唯一性）。

### 2.2 作用范围

uid **仅用于 db chain 与迁移追踪**，不扩展到对象全名：

- `_DB_CHAIN` 内部的链身份匹配（防 path 复用错配）。
- merge 迁移追踪（target 继承 source uid，`absorbed_from` 记旧 path）。
- master 内存 `uid_to_path_` 映射（find_db/迁移解析前驱物理位置）。
- task 参数序列化 `__fly_db__:{uid}:{db_path}:{data_path}`（restart 靠 uid 找 db）。

对象全名仍是 `db_path:short_name`（保留 ADR 0002 的 db_path 全名体系，最小侵入）。

### 2.3 merge 后 uid 不变

**merge 的语义是"搬运同一个 db"，不是"销毁旧 db、创建新 db"。** 身份没变，只是换了物理位置。target **继承** source 的 uid。这是关键设计决策，带来三个连带简化：

1. **下游链引用零更新**：下游 `prev[].uid = A`，merge 后 uid 仍是 A，master 只更新 `uid_to_path_[A]` 的值。
2. **master 映射是单点覆盖**：`uid_to_path_[A] = target_path`，同一 key 覆盖 value，无需废弃/转译逻辑。
3. **absorbed_from 退化为"路径历史"**：只记曾经住过的旧 path，不记外部 uid（就是自身 uid）。

---

## 3. `_DB_CHAIN` 文件格式

位置：`{db_path}/_DB_CHAIN`，与 `_DB_META`/`_FROZEN`/`_VARS` 同级。

格式：**纯 JSON**（人类可读、Python 原生）。

```json
{
  "version": 1,
  "uid": "a3f8c2e109d4",
  "role": "solve",
  "logical_name": "solve",
  "created_at": 1723084800.123456789,
  "prev": [
    {
      "uid": "7e1b09c4aa30",
      "role": "matrix",
      "logical_name": "matrix",
      "db_path": "/abs/path/to/project/matrix"
    }
  ],
  "next": [
    {
      "uid": "cc4410ee8f2a",
      "role": "analysis",
      "logical_name": "analysis",
      "db_path": "/abs/path/to/project/analysis"
    }
  ],
  "absorbed_from": []
}
```

### 3.1 字段说明

| 字段 | 含义 | 可变性 |
|------|------|--------|
| `version` | 格式版本号 | 不变 |
| `uid` | 自身逻辑身份（创建时生成，merge 后不变）| 不变 |
| `role` | db 角色（由子类类属性决定，见 §4）| 不变 |
| `logical_name` | 建库时的 name（Project 内部 key）| 不变 |
| `created_at` | 创建时间（浮点秒，纳秒精度）| 不变 |
| `prev[]` | 前驱 db 列表（DAG 前向边），每项 `{uid, role, logical_name, db_path}` | `db_path` 在邻居 merge 时更新 |
| `next[]` | 后继 db 列表（DAG 反向边），每项 `{uid, role, logical_name, db_path}` | `db_path` 在邻居 merge 时更新；建链时追加 |
| `absorbed_from[]` | merge 吸收的源 path（迁移历史），字符串列表 | merge 时追加 |

### 3.2 设计要点

1. **双向边，各记一次对端位置**：下游在 `prev[].db_path` 存上游位置，上游在 `next[].db_path` 存下游位置。
2. **prev 是权威边，next 是加速索引**：next 丢失可自愈（load/merge 时校验双向一致性，发现 prev 有对端而 next 缺，自动补齐回填）。
3. **uid 是身份，path 是定位**：边的身份匹配靠 `uid`（不变），物理定位靠 `db_path`（可变）。
4. **旧 db（无 `_DB_CHAIN`）兼容**：视为 `role=None`、无前驱的叶子节点，不报错。

---

## 4. `_Database` 子类机制

### 4.1 role 类属性 + 自动注册

```python
class _Database:
    role = None  # 基类无 role（裸 db / 旧 db）
    _ROLE_REGISTRY = {}  # role -> cls，子类定义时自动注册

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        if cls.role is not None:
            _Database._ROLE_REGISTRY[cls.role] = cls
```

### 4.2 领域子类（solver 示例）

```python
# src/solver/py/dbs.py（新文件）
from storage.py.database import _Database

class MatrixDb(_Database):
    """存储输入矩阵的 db。role=matrix。"""
    role = "matrix"
    def load_matrix(self):
        return self.read_object("matrix")

class SolveDb(_Database):
    """存储求解过程与结果的 db。role=solve。"""
    role = "solve"
    def load_solution(self):
        return self.read_object("__rasg__sol")
```

子类定义时（`import solver.dbs`）自动注册到 `_ROLE_REGISTRY`。worker 进程 import 流程模块即完成注册（与 `Project.load()` 动态还原子类的 import 触发机制一致）。

### 4.3 构造拆分：`_wrap` 类方法

为支持 find_db 返回子类实例，把"获取 C++ Database 指针"的逻辑抽成 `_wrap` 类方法：

```python
@classmethod
def _wrap(cls, db_path, data_path=""):
    """获取/复用 C++ Database 指针，包装成 cls 实例。

    master: 走 MasterAgent::get_or_create_database（权威 map 复用同一 C++ 对象）。
    worker: 走 ex_stg_create_database。
    不重复建库——同 db_path 的 C++ Database 全进程唯一。
    """
    instance = cls.__new__(cls)   # 不调 __init__
    from fly.runtime import _mode
    if _mode == "master":
        from fly.runtime import get_agent
        instance._db = get_agent()._agent.get_or_create_database(db_path, data_path, 0)
    else:
        from _fly_storage import ex_stg_create_database
        instance._db = ex_stg_create_database(db_path, data_path, 0)
    return instance
```

`__init__` 改为调用 `_wrap` + 写 `_DB_CHAIN`（仅新建时）。`find_db` 直接用 `_wrap` 构造前驱子类实例。

---

## 5. API 设计

### 5.1 建库侧（Project / flow 内部）

```python
def _create_db(self, name, data_path="", db_cls=None, prev=None):
    """flow 内部建库。

    Args:
        name: db 子目录名 + logical_name。
        db_cls: _Database 子类（决定 role）。默认 _Database（无 role）。
        prev: 前驱 db 句柄列表（DAG 边）。默认 []（无前驱）。
    """
```

建链三步（见 §7 建链流程）：
1. 生成 uid，写自身 `_DB_CHAIN`（含 `prev[]`）。
2. 遍历 `prev[]`，回填每个前驱的 `next[]`。
3. master 内存注册 `uid_to_path_[uid] = db_path`。

`open_db` 同样扩展 `db_cls` / `prev` 参数（供裸 db 场景）。

### 5.2 查询侧（task 内）

```python
def find_db(self, role=None, logical_name=None, uid=None):
    """沿自身 DAG 向前（BFS），返回距离最近的一个匹配前驱。

    匹配条件：uid 精确相等，或 role/logical_name 任一非 None 即参与匹配（AND 组合）。
    多匹配：返回跳数最少者；同跳数按 prev 声明顺序取第一个。
    找不到返回 None。

    返回对应 role 子类实例（按 _DB_CHAIN 记录的 role 查 _ROLE_REGISTRY 重建）。
    """

def find_all_dbs(self, role=None, logical_name=None):
    """返回 DAG 中所有匹配前驱列表（按 BFS 距离排序）。"""

def prevs(self):
    """返回直接前驱列表（仅一层，不递归）。"""

def nexts(self):
    """返回直接后继列表（仅一层，不递归）。"""
```

### 5.3 BFS 匹配算法

```
find_db(role, logical_name, uid):
  queue = [(self.db_path, self.role, self.logical_name, depth=0)]
  visited = {self.db_path}
  while queue:
    (cur_path, cur_role, cur_lname, depth) = queue.pop_front()
    chain = read_chain(cur_path)          # LOCK_SH 并发读，轻量 JSON
    if chain is None: continue
    for prev_node in chain.prev:          # [{uid, role, logical_name, db_path}]
      if prev_node.uid in visited: continue   # DAG 防环（靠 uid）
      visited.add(prev_node.uid)
      if match(prev_node, role, logical_name, uid):
        # 通过 master uid_to_path_ 解析前驱当前物理位置
        actual_path = master.resolve_uid(prev_node.uid) or prev_node.db_path
        return reconstruct(prev_node, actual_path)  # 构造对应子类实例
      queue.append((prev_node.db_path, prev_node.role, prev_node.logical_name, depth+1))
  return None
```

`match(node, role, lname, uid)`：uid 非 None 需相等；role 非 None 需相等；logical_name 非 None 需相等。

`reconstruct(node, path)`：
1. `cls = _ROLE_REGISTRY.get(node.role, _Database)`
2. `return cls._wrap(path)` —— 构造时 C++ 获取 Database 指针

### 5.4 仅沿自身 DAG 搜索

find_db 的搜索范围**仅限当前 db 的 DAG 前驱**，不扩展到整个 Project。前驱若是外部 Project 的 db，链清单照常记录其 uid/path，照样能找到。语义清晰、边界明确。

---

## 6. 并发安全：readers-writer 文件锁

### 6.1 问题

`_DB_CHAIN` 的访问存在两类并发：

- **单 run 内**：master Python 主线程建链 vs master C++ 线程 merge cleanup，跨语言并发对同一文件 read-modify-write。
- **多 run 间**：多个 run 同时打开同一 db（并发读已 freeze 的数据源是合理场景）。

### 6.2 readers-writer 语义

`_DB_CHAIN` 的访问分两类，用 `fcntl.flock` 区分：

| 操作 | 并发性 | 锁 |
|------|--------|-----|
| **读**（load/find_db 读拓扑） | 多 run 并发 | `LOCK_SH` 共享锁 |
| **写**（建链/merge 更新邻居） | 独占 | `LOCK_EX` 排他锁 |

多 run 并发读同一 db 的 `_DB_CHAIN` 时全部拿 `LOCK_SH`，互不阻塞；只有 merge/建链写时升级为 `LOCK_EX`，读进程短暂等待，写完（`os.replace` 原子落盘）即放行。

### 6.3 实现

```python
import fcntl, json, os

class DbChainFile:
    def __init__(self, db_path):
        self.path = os.path.join(db_path, "_DB_CHAIN")
        self.lock_path = self.path + ".lock"

    def read(self):
        """多 run 并发读。LOCK_SH，不阻塞其他读者。"""
        with open(self.path) as f:
            fcntl.flock(f.fileno(), fcntl.LOCK_SH)
            return json.load(f)              # 读一致快照

    def update(self, mutator):
        """排写。LOCK_EX，阻塞所有读者直到写完。mutator(dict)->dict。"""
        with open(self.lock_path, "w") as lf:
            fcntl.flock(lf.fileno(), fcntl.LOCK_EX)     # 跨进程阻塞
            data = self._read_locked()                   # 持锁读全量
            new = mutator(data)                           # 改内存
            self._write_locked(new)                       # os.replace 原子落盘
        # 退出 with 释放锁（进程 crash 也释放）

    def _write_locked(self, data):
        tmp = self.path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
        os.replace(tmp, self.path)                        # 原子替换
```

### 6.4 正确性保证

- **读永远一致**：`LOCK_SH` 期间若有写者，读者阻塞到写完，读到最新全量快照，不读到半个文件。
- **写不丢更新**：`LOCK_EX` 串行化所有写者，read-modify-write 全程持锁。
- **进程 crash 自动释放锁**：`flock` 绑定 fd，进程退出即释放，不会死锁。

### 6.5 适用边界

`flock` 的 readers-writer 在**同机同文件系统**有效。"多个 run 并发读同一 db"的前提是 db_path 是共享目录、各 run 在同一机器（或同一共享 FS）——这与 db 的物理模型一致。跨机同时操作同一本地目录的 `_DB_CHAIN` 不是合理用法（本地路径不跨机共享）。

### 6.6 写时整文件替换（无读改写交叉）

关键设计：`update` 内部是"读当前全量 → 改 → 写全量"，全程持锁。不存在"另一个写者读半个旧文件"。这把 read-modify-write 的 lost update 彻底消除——进得了锁，看到的永远是最新全量。

---

## 7. 关键流程

### 7.1 建链流程（`_create_db` with prev）

```
_create_db(name, db_cls=XxxDb, prev=[A, B]):
  1. open_db 建库（自动递增避重名，见 §8）
  2. 生成 uid = _generate_uid(db_path, role)
  3. 写自身 _DB_CHAIN：
       { uid, role, logical_name, created_at,
         prev: [{A.uid, A.role, A.logical_name, A.db_path},
                {B.uid, B.role, B.logical_name, B.db_path}],
         next: [], absorbed_from: [] }
  4. 遍历 prev，对每个前驱 P（LOCK_EX 更新）：
       P._DB_CHAIN.next += [{self.uid, self.role, self.logical_name, self.db_path}]
  5. master 内存注册 uid_to_path_[uid] = self.db_path
```

**两步原子性**：建一条边要改两个文件（自己 prev + 上游 next），中间 crash 留下单向边。prev 是权威边（先写，单文件原子）；next 丢失可自愈（load/merge 校验双向一致性，发现 prev 有对端而 next 缺，自动补齐回填）。

### 7.2 find_db 流程

见 §5.3 BFS 算法。读 `_DB_CHAIN` 用 `LOCK_SH`（并发读不阻塞），物理定位优先查 master `uid_to_path_`（本 run 内有效），miss 则用 prev 记录的 `db_path`。

### 7.3 merge 流程（彻底删源 + uid 不变 + 更新邻居）

```
merge_db(source_path → target_path):
  ── Phase 1-4：数据迁移（既有机制，target worker 拉源 .dat）──
  
  ── Phase 5：链与迁移更新（master Python 主线程执行，非 C++ cleanup）──
  5a. 读 source._DB_CHAIN（拿 uid, role, prev, next）
  5b. target._DB_CHAIN 继承 source 身份：
        { uid: source.uid（不变）,          ← 继承
          role: source.role,                ← 继承
          logical_name: source.logical_name,← 继承
          prev: source.prev,                ← 继承（链身份完整）
          next: source.next,                ← 继承
          absorbed_from: source.absorbed_from + [source_path] }  ← 追加旧 path
  5c. master 内存更新：
        uid_to_path_[source.uid] = target_path   ← 单点覆盖（key 不变）
  5d. 靠 source.next[] 找下游 S（LOCK_EX 更新每个 S）：
        S._DB_CHAIN.prev[uid==source.uid].db_path = target_path
  5e. 靠 source.prev[] 找上游 P（LOCK_EX 更新每个 P）：
        P._DB_CHAIN.next[uid==source.uid].db_path = target_path
  5f. _PROJECT_META 更新：该 db 的 db_path → target_path
  5g. 彻底删除 source_path 目录（含 _DB_META/_FROZEN/_DB_CHAIN/.idx，全部）
       ← 必须在 5d/5e 邻居更新之后，保证"邻居读到旧 path 时源还在"
  5h. 不再写 _MIGRATED_TO（机制废弃）
```

**关键不变式**：merge 一个 db 要更新的是**直接邻居两侧**（下游 prev + 上游 next），都靠自己的反向边定向找，O(邻居数) 完成，不扫描全部 db。

### 7.4 失败 task 信息的完整设计

#### 7.4.1 背景：failed_tasks.bin 携带了哪些 db 引用

task 失败时，`FailedTaskRecord`（内嵌 `TaskSubmissionSpec`，`master_agent.h:31`）整体 append 到 `{log_dir}/failed_tasks.bin`（`master_agent.cpp:1517`）。`TaskSubmissionSpec` 携带三处 db 引用：

| 字段 | 内容 | db 引用形态 |
|------|------|------------|
| `args_` | task 参数序列化 | `__fly_db__:{...}` 协议串（db 句柄）|
| `inputs_` | task 依赖的对象全名列表 | `db_path:short_name` |
| `outputs_` | task 产出的对象全名列表 | `db_path:short_name` |
| `vars_` | 声明的 var 全名列表 | `db_path:short_name` |

restart 时（`restart_failed_tasks`，`master_agent.cpp:1603`）整体读回 records，逐个用 `record.submission_` 调 `submit_task` 重投。**这些 db 引用里的 path 会随 merge 失效**，是失败重启闭环的关键难点。

#### 7.4.2 序列化格式升级（uid 化）

**db 句柄参数（`args_`）**——升级 `__fly_db__` 协议串，加入 uid：

```
旧：__fly_db__:{db_path}:{data_path}
新：__fly_db__:{uid}:{db_path}:{data_path}
```

序列化端 `_serialize_args`（`task.py:258`）：db 句柄有 uid 属性时写入新格式。

反序列化端 `_deserialize_args`（`executor.py:51`）：按 `:` split 后识别新格式，拿到 uid + db_path + data_path。

**对象全名（`inputs_`/`outputs_`/`vars_`）**——保持 `db_path:short_name` 不变。

原因：对象全名是依赖图（DependencyGraph）和索引的 key，改动面巨大（网络消息、idx、所有 map key）。对象全名里的 path 通过 **restart 时重映射**（见 §7.4.4）解决，不需要在格式里内嵌 uid。

#### 7.4.3 persist 时机：失败即落盘（既有机制，不动）

三个失败触发点都经 `make_failed_record` + `persist_failed_task` 整体落盘（`master_agent.cpp:567/602/931`）：

- 调度时数据依赖不可解
- 调度时属性死锁
- 运行时失败

落盘的是**失败时刻的 submission 快照**，含当时的 db_path。merge 后这些 path 可能失效，靠 §7.4.4 的 restart 重映射修复。

优雅停机时 PENDING task 也写成 FailedTaskRecord（`master_agent.cpp:2004`），语义相同。

#### 7.4.4 restart 流程：自动 load + 重映射

restart 接收用户传入的 bin 文件路径，**自动恢复**未在 master 内存注册的 db，然后重映射重投。整个流程对用户而言是一个调用，不需要手动 load。

```
restart_failed_tasks(file_path):
  records = read_failed_records(file_path)
  
  # ── Phase 1：收集所有涉及的 db 引用 ──
  db_refs = {}   # uid -> {old_path, data_path}
  for record in records:
    spec = record.submission_
    for arg in spec.args_:
      if arg.startswith("__fly_db__:"):
        uid, old_path, data_path = parse(arg)
        db_refs[uid] = {old_path, data_path}
  
  # ── Phase 2：自动 load 未注册的 db ──
  missing = []
  for uid, ref in db_refs.items():
    if master.resolve_uid(uid) is not None:
      continue                          # 已注册（同 run 或已 load），跳过
    if os.path.isdir(ref.old_path):
      try:
        load_db(ref.old_path)           # 自动 load → 注册 uid_to_path_
        continue
      except: pass
    # path 不存在或 load 失败 → 记入 missing
    missing.append({uid, ref.old_path, ref.data_path})
  
  if missing:
    # ── 显式报错，列出所有无法自动恢复的 db ──
    raise RestartError(
      "无法自动恢复以下 db，请手动 load 后重试 restart：\n"
      + "\n".join(f"  uid={m.uid}, 原路径={m.old_path}" for m in missing)
      + "\n提示：若 db 已被 merge 迁移，请 load 迁移后的目标路径。"
    )
    # 注意：不删 bin 文件，用户 load 后可重试
  
  # ── Phase 3：重映射 + 重投（所有 uid 已注册）──
  remove(file_path)
  for record in records:
    spec = record.submission_
    
    # 重映射 args_ 的 db 句柄 path
    for i, arg in enumerate(spec.args_):
      if arg.startswith("__fly_db__:"):
        uid, old_path, data_path = parse(arg)
        actual_path = master.resolve_uid(uid)   # 此时必定命中
        spec.args_[i] = f"__fly_db__:{uid}:{actual_path}:{data_path}"
    
    # 重映射 inputs_/outputs_/vars_ 的对象全名 path 前缀
    # （path→uid 映射从 args_ 已知；无 uid 的全名靠 path 反查 _DB_CHAIN）
    for field in (spec.inputs_, spec.outputs_, spec.vars_):
      for i, full_name in enumerate(field):
        old_path, short = split_full_name(full_name)
        uid = path_to_uid.get(old_path) or read_chain(old_path).uid
        actual_path = master.resolve_uid(uid)
        if actual_path != old_path:
          field[i] = f"{actual_path}:{short}"
    
    submit_task(record.task_id_, spec)
```

#### 7.4.5 自动 load 的三级解析

restart Phase 2 对每个未注册的 uid，按以下顺序尝试恢复：

| 级别 | 方法 | 适用场景 |
|------|------|---------|
| 1 | `master.resolve_uid(uid)` 命中 | 同 run（内存有效）或当前 run 已 load |
| 2 | record 里的 `old_path` 目录存在 → `load_db(old_path)` | 跨 run 但 db 未被 merge（path 未变）|
| 3 | 以上都失败 → 报错 | db 已被 merge 迁移，record 里的 path 失效 |

**级别 3 的报错是可恢复的**：报错信息给出 uid 和原 path，提示用户手动 load 迁移后的目标路径。用户 load 后重试 `restart_failed_tasks(file_path)` 即可（bin 文件未被删除）。

**为什么不自动查 `_PROJECT_META`**：failed task 不含 project 信息（§7.4.1），restart 无法确定去哪个 Project 查 meta。让用户手动 load 失效的 db 是最简洁、无歧义的恢复路径。

#### 7.4.6 merge 与 failed_tasks.bin 的时序关系

**merge 时不碰 `failed_tasks.bin`**（restart 时惰性重映射 + 自动 load）：

- merge 发生在 task 运行期间，`failed_tasks.bin` 可能仍被并发 append，merge 时 rewrite 有竞态。
- restart 是用户显式调用的串行操作，此时无并发写，自动 load + 重映射安全。
- master `uid_to_path_` 在同 run 内始终有效（merge 时已更新），restart 时级别 1 直接命中。

**worker 反序列化的兜底**：即使 restart 重映射后 path 正确，worker 侧反序列化也要处理"目录不存在"的极端时序（merge 发生在 restart 之后）：

```
worker._deserialize_args:
  解析 __fly_db__:{uid}:{db_path}:{data_path}
  if not os.path.isdir(db_path):
    resolved = worker.request_db_path(db_path, data_path, uid)  # 带 uid 查 master
    if resolved and resolved.uid == uid:
      db_path, data_path = resolved.db_path, resolved.data_path
    else:
      raise Error("db uid=X not found at path=Y")
  构造 db（按 _DB_CHAIN 的 role 选子类）
```

#### 7.4.7 用户操作总结

| 场景 | 用户操作 | 系统行为 |
|------|---------|---------|
| 同 run restart | `restart_failed_tasks(bin_path)` | 级别 1 命中，自动重映射重投 |
| 跨 run，db 未 merge | `restart_failed_tasks(bin_path)` | 级别 2 自动 load，重映射重投 |
| 跨 run，db 已 merge | `restart_failed_tasks(bin_path)` → 报错 → 手动 load → 重试 | 级别 3 报错提示 uid+原路径；用户 `load_db(target_path)` 后重试，级别 1 命中 |

**跨 run merge 的典型恢复流程**：

```python
# run1: merge 后 task 失败，master 崩溃
# run2: 重启

try:
    restart_failed_tasks("fly_log/failed_tasks.bin")
except RestartError as e:
    print(e)
    # 输出示例：
    # 无法自动恢复以下 db，请手动 load 后重试 restart：
    #   uid=a3f8c2e1, 原路径=/proj/matrix
    # 提示：若 db 已被 merge 迁移，请 load 迁移后的目标路径。
    
    # 用户知道 matrix 被迁到了 /shared/matrix
    load_db("/shared/matrix")     # 注册 uid_to_path_[a3f8c2e1]="/shared/matrix"
    restart_failed_tasks("fly_log/failed_tasks.bin")  # 重试，级别 1 命中，成功
```

**设计原则**：尽力自动恢复（级别 1-2 覆盖绝大多数场景），恢复不了的明确报错（级别 3），绝不静默 fallback 到 path 猜测。

### 7.5 load_db 流程（新 run）

```
load_db(path):
  1. 读 path/_DB_CHAIN（LOCK_SH）
  2. 若有 _DB_CHAIN：按 role 查 _ROLE_REGISTRY 选子类，_wrap 构造
  3. master 内存注册 uid_to_path_[chain.uid] = path
  4. 新 run 建立自己的 uid 体系
```

Project.load 遍历所有 db，对每个调 load_db，建立完整的 uid→path 映射。

### 7.6 next 自愈（load/merge 时校验）

```
load/merge 时：
  for each db_dir:
    chain = read(db_dir/_DB_CHAIN)
    for prev_node in chain.prev:
      读 prev_node.db_path/_DB_CHAIN
      若 prev 的 next 不含 chain.uid：
        补齐回填 prev.next += [{chain.uid, ...}]
```

next 是可重建的缓存，丢一条不影响正确性，只影响 merge 一次定位速度。

---

## 8. path 复用与别名冲突的解决

### 8.1 问题场景

```
t0: open_db("/proj/matrix", role=matrix) → matrix_db_A（uid=A，含数据 D_A）
t1: merge_db("/proj/matrix" → "/shared/matrix") → 数据迁到 /shared/matrix
    → /proj/matrix 彻底删除
    → master: uid_to_path_[A] = "/shared/matrix"
t2: open_db("/proj/matrix", role=matrix) → matrix_db_B（uid=B，新数据 D_B）
t3: 下游 db 的 prev = [{uid:A, ...}]
    → find_db(uid=A) → master uid_to_path_[A] = "/shared/matrix"
    → 正确定位到 D_A，不会错配到 D_B（uid=B）
```

**uid 彻底阻断了 path 复用的别名冲突。**

### 8.2 open_db 自动递增避让

`open_db(path)` 检测目录已有 `_DB_CHAIN`（或 `_DB_META`）时，自动递增（`/proj/matrix.1`/`matrix.2`），与现有行为一致。源头避免覆盖。每个新 db 有不同 uid（纳秒时间戳差异）。

---

## 9. 场景覆盖验证

| 场景 | 靠什么闭合 |
|------|-----------|
| 运行中迁移 | master `uid_to_path_` map |
| path 复用 | 新 db 新 uid，旧引用靠 uid 不错配 |
| open_db 重名 | 自动递增避让 |
| 重启 load_db | 读 target `_DB_CHAIN`，新 uid 体系 |
| failed_tasks.bin 同 run restart | 自动重映射，用户只传 bin 路径（§7.4.4 级别 1）|
| failed_tasks.bin 跨 run restart（db 未 merge）| 自动 load 未注册 db，重映射重投（§7.4.4 级别 2）|
| failed_tasks.bin 跨 run restart（db 已 merge）| 报错提示 uid+原路径，用户手动 load 后重试（§7.4.4 级别 3）|
| failed_tasks.bin 并发写 | merge 时不 rewrite，restart 时串行自动 load + 重映射（§7.4.6）|
| merge 彻底删源 | 迁移信息在 target `_DB_CHAIN.absorbed_from`，不寄生源 |
| db_chain find_db 前驱被 merge | prev 记录 uid，master 解析 uid→path |
| 旧引用（目录已删）| worker查 master → uid 匹配 → target |
| 多 run 并发读同一 db | `LOCK_SH` 共享锁，零阻塞 |
| merge/建链写 `_DB_CHAIN` | `LOCK_EX` 排他锁，串行化 |
| 建链中途 crash（单向边）| next 自愈（load/merge 校验补齐）|
| 旧 db 无 `_DB_CHAIN` | 视为 role=None 叶子，跳过 |

---

## 10. 实现范围

### 10.1 改动清单

| 层 | 改动 | 文件 | 语言 |
|----|------|------|------|
| **存储** | `_DB_CHAIN` 读写工具（含 readers-writer 锁）| `src/storage/py/db_chain.py`（新）| Python |
| **存储** | `_Database` 子类机制 + `_wrap` + `find_db`/`find_all_dbs`/`prevs`/`nexts` | `src/storage/py/database.py` | Python |
| **存储** | Database 构造不再读 `_MIGRATED_TO`；`resolve_migrated_path` 改查 master 内存 map | `src/storage/cpp/database.cpp`、`data_service.cpp` | C++ |
| **Agent** | `db_instances_` key 改 uid；`cleanup_after_merge` 改为只迁数据，链更新回 Python 编排 | `src/agent/cpp/master_agent.cpp` | C++ |
| **Agent** | worker `request_db_path` 支持 uid 查询（目录不存在时）；executor 反序列化按 uid+role 选子类 | `src/agent/py/executor.py`、`src/agent/cpp/worker_agent.cpp` | Py+C++ |
| **Task** | `_serialize_args` 加 uid（`__fly_db__:{uid}:{path}:{data_path}`）；`_deserialize_args` 解析 uid + 目录不存在时查 master；`restart_failed_tasks` 重映射 args_/inputs_/outputs_ 靠 uid | `src/task/py/task.py`、`src/agent/py/executor.py`、`src/agent/cpp/master_agent.cpp` | Py+C++ |
| **Project** | `_create_db` 扩展 db_cls/prev；`merge_db` 更新 `_PROJECT_META` db_path + 链更新 + 彻底删源；open_db 自动递增 | `src/fly/project.py`、`src/fly/__init__.py`、`src/agent/py/agent.py` | Python |
| **Solver** | MatrixDb/SolveDb 子类；flows.py 连链 | `src/solver/py/dbs.py`（新）、`src/solver/py/flows.py` | Python |
| **QA** | path 复用场景、merge+find_db、跨 run 报错、并发读、建链 crash 自愈 | `qa/storage/test_db_chain.py`（新）| Python |

### 10.2 废弃的机制

| 废弃项 | 替代 |
|--------|------|
| `_MIGRATED_TO` 文件（`db_meta.h:MigrationHeader`）| `_DB_CHAIN.absorbed_from` + master `uid_to_path_` |
| `DataService::resolve_migrated_path` 读文件 | 改为只查 master 内存 map |
| `DataService::write_migration_marker` | 写 target `_DB_CHAIN.absorbed_from` |
| ADR 0002 的"源 path 永久保留"假设 | 源目录彻底删除 |

### 10.3 核心是纯 Python 为主

C++ 改动集中在 merge cleanup 路径（链更新移交 Python 编排）和 `db_instances_` key 迁移、`resolve_migrated_path` 改内存查询。链机制、uid、find_db、文件锁全部在 Python。

### 10.4 TDD 实现顺序

1. **`_DB_CHAIN` 工具 + readers-writer 锁 + uid 生成**（纯 Python，可独立测试）
2. **`_Database` 子类机制 + `_wrap`**（纯 Python）
3. **`find_db` / `find_all_dbs` / `prevs` / `nexts` + BFS 匹配**（纯 Python）
4. **open_db/_create_db 扩展 + 自动递增 + 建链（含 next 回填）**（Python）
5. **task 序列化加 uid + worker 反序列化按 uid 选子类 + restart 重映射**（Py+C++）
6. **merge 改造：彻底删源 + target `_DB_CHAIN` 继承 + 邻居更新（靠 next/prev）**（C++ 数据迁移 + Python 链更新）
7. **废弃 `_MIGRATED_TO` + `resolve_migrated_path` 改内存查询 + `db_instances_` key 改 uid**（C++）
8. **next 自愈（load/merge 校验补齐）**（Python）
9. **SolverProject 改造为 db_chain 范式**（Python）
10. **QA：path 复用场景、merge+find_db、跨 run 报错、并发读、建链 crash 自愈**

---

## 11. 边界情况处理

| 场景 | 处理 |
|------|------|
| 旧 db 无 `_DB_CHAIN` | 视为 `role=None` 叶子，find_db 跳过 |
| `_DB_CHAIN` 损坏 | WARN，视为叶子，不崩溃 |
| DAG 环（误建） | BFS `visited` 集合（靠 uid）防环 |
| 前驱未 freeze / 数据未就绪 | find_db 返回句柄，read_object 时由依赖图 mark_data_ready 把关（既有机制）|
| 前驱被 merge | find_db 通过 master uid_to_path_ 解析新 path |
| 裸 db（脱离 Project）| open_db(path, db_cls=X, prev=[...]) 直接用，链照样落盘 |
| role 未注册子类 | 回退基类 `_Database`（`_ROLE_REGISTRY.get(role, _Database)`）|
| 建链中途 crash | next 自愈（load/merge 校验补齐）|
| 跨 run restart（db 已 merge，path 失效）| 自动 load 失败 → 报错提示 uid+原路径，用户 load target 后重试（§7.4.7）|
| 多 run 并发读 | `LOCK_SH` 共享锁 |
| 多 run 并发写 | `LOCK_EX` 排他锁，串行化 |

---

## 附录 A：与现有机制的关系

| 现有机制 | 本方案关系 |
|---------|-----------|
| `_DB_META`（created_at + WorkerInfo）| 不动，继续记录写入者拓扑 |
| `_FROZEN` / `_VARS` | 不动 |
| `_MIGRATED_TO` | **废弃**，由 `_DB_CHAIN.absorbed_from` + master uid map 取代 |
| `DataService::resolve_migrated_path` | 改为只查内存 map，不再 stat 源文件 |
| ADR 0002（源 path 永久保留）| **取代**，源目录彻底删除 |
| `Project` / `register_flow` | 扩展 `_create_db` 参数，不动核心机制 |
| `open_db` 自动递增 | 扩展检测 `_DB_CHAIN`，行为不变 |
| 对象全名 `db_path:short_name` | **不动**，保留 ADR 0002 体系 |
| merge 数据迁移（Phase 1-4）| **不动**，只改 Phase 5（链更新 + 删源）|

---

*决策日期：2026-08-08*
