# UserDoc — Help 与参数校验系统

Fly 为用户面向的 API 提供 **Help 系统**（运行时查询 API 文档与示例）和 **参数校验器**
（调用入口快速确认输入无误）。两者由 `UserDoc` 类统一收集，经 `@document` 装饰器接入。

## 快速上手

**查询 API 文档**——无需任何 import，在 fly 脚本或交互 shell 中直接调用：

```python
help()                    # 列出所有已注册 API（名称 + owner + 一句话描述）
help("solve")             # 默认精简：描述 + prototype（快速浏览）
help("solve", detail=True)  # 完整文档：追加 Parameters / Examples / Keywords
help(all=True)            # 一次性输出全部 API 的完整详情（整体导览）
```

**默认精简输出**（`help("solve")`）——只给描述与 prototype，快速确认 API 用途与签名：
```
══════════════════════════════════════════════════
solve — SolverProject
══════════════════════════════════════════════════
求解 matrix_db 中的矩阵，结果存入 name 的 db，异步返回。

Prototype:
  solve(name: str, matrix_db, nsd, overlap_ratio=0.5, max_iter=100, tol=1e-08, omega=1.0)

(use detail=True for parameters & examples)
```

**完整文档输出**（`help("solve", detail=True)`）——追加参数、示例、关键词：
```
══════════════════════════════════════════════════
solve — SolverProject
══════════════════════════════════════════════════
求解 matrix_db 中的矩阵，结果存入 name 的 db，异步返回。

Prototype:
  solve(name: str, matrix_db, nsd, overlap_ratio=0.5, max_iter=100, tol=1e-08, omega=1.0)

Parameters:
    name           : str              [required]    求解结果 db 的子目录名
    matrix_db      : _Database        [required]    含 read_object('matrix') 的数据源 db
    nsd            : int              [required]    子域数（须有 >= nsd 个带 sd_i 的 worker）
    overlap_ratio  : float            [default 0.5] 子域重叠比例
    max_iter       : int              [default 100] 最大迭代数
    tol            : float            [default 1e-8] 收敛阈值
    omega          : (int | float) | (str) [default 1.0] 松弛策略（数值或 'coarse'/'adaptive'）

Examples:
  [基础求解]  单矩阵异步求解，返回 db 后等待冻结
    matrix_db = proj.build_matrix(name="matrix", matrix_path="poisson.npz")
    result_db = proj.solve(name="solve", matrix_db=matrix_db, nsd=4)
    proj.wait_frozen("solve", timeout=120)
  [adaptive 松弛]
    result_db = proj.solve(name="solve", matrix_db=matrix_db, nsd=4, omega="adaptive")

Keywords: ras, solver, linear, iterative, solve
```

**参数校验**——调用 API 时自动校验，失败抛聚合 `ValueError`：

```python
proj.solve(name="solve", matrix_db=None, nsd=0, overlap_ratio=2.0)
```
```
ValueError: solve: parameter validation failed:
  - matrix_db: expected _Database, got NoneType
  - nsd: must be >= 1, got 0
  - overlap_ratio: must be in [0,1], got 2.0
```

---

## 为自定义 API 添加 Help + 校验

三步：构造 `UserDoc` → 用 `@document` 装饰 → 与 `@register_flow` 组合。

```python
from fly import UserDoc, Schema, document, register_flow

# 1. 构造 UserDoc（收集描述 / 参数校验 / 示例 / 关键词）
solve_doc = UserDoc("求解 matrix_db 中的矩阵，结果存入 name 的 db。")
solve_doc.add_param("nsd",
    schema=Schema(int, check=lambda n: n >= 1, error="must be >= 1, got {value}"),
    required=True, desc="子域数")
solve_doc.add_param("overlap_ratio",
    schema=Schema(float, check=lambda x: 0 <= x <= 1, error="must be in [0,1], got {value}"),
    required=False, default=0.5, desc="子域重叠比例")
solve_doc.add_example("基础求解",
    code="result_db = proj.solve(name='solve', matrix_db=db, nsd=4)",
    desc="单矩阵异步求解")
solve_doc.add_keyword(["ras", "solver"])

# 2. 装饰器组合：@register_flow 在外，@document 在内
@register_flow(SolverProject)
@document(solve_doc)
def solve(self, name, matrix_db, nsd, overlap_ratio=0.5):
    ...
```

`@document` 在内层先执行（注入 api_name + signature，注册进 help 系统，owner=None）；
`@register_flow` 在外层后执行（把方法挂到类上，并回填 owner）。两者解耦：仅有 `@document`
的独立 API 也能正常注册（owner 显示为空）。

### 装饰类（校验构造 + 类文档）

`@document` 同样可用于类——校验 `__init__` 参数，api_name 取类名：

```python
proj_doc = UserDoc("RAS solver 的 Project 模板。")
proj_doc.add_param("base_path",
    schema=Schema(str, check=lambda s: len(s) > 0, error="must not be empty"),
    required=True, desc="project 目录路径")

@document(proj_doc)
class SolverProject(Project):
    pass

# SolverProject("")  → ValueError: SolverProject: parameter validation failed:
#                        - base_path: must not be empty
```

类装饰器还会回填类体内方法的 owner：类体内被 `@document` 装饰的方法此时 owner=None，
类装饰器执行时统一回填为该类，使 `help("set_merger")` 能显示归属 `MapReduceJob`。

---

## Schema 校验规则

校验归约为三类正交积木。嵌套位置（dict field、list 元素、分支）统一接受 `Schema` 对象
或裸 `callable`（自动包装）。

### 类型 + 深度校验

```python
Schema(type, *, check=None, error=None, desc=None)
```

- `type`：Python 类型 / 类型元组 / 类名字符串（如 `"_Database"`，规避循环导入）。
- `check`：纯判定函数 `fn(value) -> bool`。`True` 通过，`False` 失败。
- `error`：失败消息（与 check 分离）。模板 str（支持 `{value}` 占位）、`callable(value)->str`、或 `None`（默认消息）。

```python
Schema(int, check=lambda n: n >= 1, error="must be >= 1, got {value}")
Schema(str, check=lambda s: len(s) > 0, error="must not be empty")
Schema("_Database")                       # _Database 实例
Schema((int, float), check=lambda x: x > 0)   # int 或 float
```

类型不匹配时**短路**（不执行 check），避免 check 对错误类型崩溃。

### 容器：dict / list

```python
Schema.dict(*, required=None, optional=None, allow_extra=False, check=None, error=None, desc=None)
Schema.list(items, *, min_len=None, max_len=None, check=None, error=None, desc=None)
```

`required` / `optional` 直接是 `{key: schema}` 字典：

```python
Schema.dict(
    required={
        "host": Schema(str, check=lambda h: len(h) > 0, error="host 不能为空"),
        "port": Schema(int, check=lambda p: 1 <= p <= 65535, error="端口范围 [1,65535], got {value}"),
    },
    optional={
        "attributes": Schema.list(Schema(str), min_len=1),
    },
)
# 缺 required key → "missing required key 'port'"
# 多余 key（allow_extra=False）→ "unexpected key 'x'"
# 整个 dict 的深度校验：check=lambda d: d["host"] != "localhost"
```

### 多分支：any_of

参数可为多种形态时（如单元素 vs list）：

```python
Schema.any_of(
    Schema(float, check=lambda w: 0 < w <= 2, error="数值须在 (0,2]"),
    Schema(str, check=lambda w: w in ("coarse", "adaptive"), error="须为 'coarse'/'adaptive'"),
)
# 任一分支完全通过即通过；全失败则聚合各分支错误
```

> 注：不能用 `Schema.or`——`or` 是 Python 保留关键字。

### 裸函数自动包装

凡是期望 Schema 的位置，传入裸 `callable` 会自动包装成 `Schema(object, check=fn)`：

```python
doc.add_param("x", schema=lambda v: v > 0)   # 等价于 Schema(object, check=lambda v: v>0)
```

---

## UserDoc API

| 方法 | 说明 |
|------|------|
| `UserDoc(desc="")` | 构造，首行描述。api_name / owner 由装饰器注入。 |
| `add_param(name, schema, *, required=True, default=_NO_DEFAULT, desc="", hidden=False, none_ok=False)` | 添加参数校验与文档。`schema` 可为 Schema 或裸 callable。`hidden=True` 时 help prototype 与 Parameters 均隐藏该参数，但校验照常生效。`none_ok=True` 时允许值为 `None`（跳过 schema 校验）；默认 `False`，值为 `None` 报错。 |
| `add_example(title, code, desc="")` | 添加使用示例。 |
| `add_keyword(keywords: list)` | 添加检索关键词，`keywords` 为 `list[str]`。 |
| `register_help(api_name=None)` | 冻结并注册（通常由 `@document` 自动调用，幂等）。 |

冻结后 `add_param` / `add_example` / `add_keyword` 抛 `RuntimeError`；身份注入（api_name / owner）不受冻结限制。

`none_ok` 用于可选的 `param=None` 参数——必须显式声明而非隐式跳过。例如 `MapReduceJob.get(db=None)` 的 `db` 允许 `None`（运行时兜底），需标 `none_ok=True`。

---

## 报错示例

校验失败抛聚合 `ValueError`，列出所有错误参数：

```python
>>> proj.solve(name="s", matrix_db=None, nsd=0, overlap_ratio=2.0)
ValueError: solve: parameter validation failed:
  - matrix_db: expected _Database, got NoneType
  - nsd: must be >= 1, got 0
  - overlap_ratio: must be in [0,1], got 2.0

>>> proj.solve(name="s", matrix_db=db, nsd=4, omega="bad")
ValueError: solve: parameter validation failed:
  - omega: expected one of: (int | float) | (str), got 'bad'
    [int | float]: number must be in (0,2]
    [str]: must be 'coarse' or 'adaptive'

>>> proj.solve(name="s", matrix_db=db)        # 缺必填参数
ValueError: solve: parameter binding failed: missing a required argument: 'nsd'
```

嵌套结构错误带路径：

```python
>>> proj.launch_workers(configs=[{"host": "h", "port": 70000}])
ValueError: launch_workers: parameter validation failed:
  - configs: [0].port: 端口范围 [1,65535], got 70000
```

---

## 开箱即用机制

Fly 启动时由 C++ 入口预执行 `import fly.bootstrap`，为用户脚本构建含全部公共 API 的
命名空间。因此用户脚本中**零 import** 即可直接使用：

```python
# 用户脚本，无需任何 import
help()
proj = SolverProject("./my_project")
db = open_db("./my_db")
```

### 惰性加载（零启动开销）

重业务模块（如 solver，import 耗 ~244ms 的 numpy/scipy）**不在启动时加载**，而是：
- 入口类（`SolverProject`）以惰性代理注入命名空间，**首次实例化时**才 import solver
- help 系统**首次查询时**才触发 solver 的 `@document` 注册（延迟钩子）

不用 solver 的脚本/进程：零 solver 开销，启动耗时与无业务模块时持平。

### 接入新业务模块（1 行声明）

新增业务模块的入口类要开箱即用，只需在 `src/fly/bootstrap.py` 的 `_LAZY_MODULES`
声明表加一行：

```python
# src/fly/bootstrap.py
_LAZY_MODULES = {
    "SolverProject": "solver:SolverProject",
    "Pipeline": "pipeline:Pipeline",   # ← 新模块，仅此一行
}
```

框架自动完成：惰性代理注入命名空间 + help 查询时触发模块加载 + `@document` 注册。
**无需写代理类、无需写 help 钩子、无需改其他代码。**

模块自身的 API（flow/方法）用 `@document` 装饰即可（见前文「为自定义 API 添加」），
模块首次被访问时这些装饰器才执行，自动注册进 help。
