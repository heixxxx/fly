# EMIR 模块族 — 分布式 EMIR 仿真分析业务层

## 模块概述

**位置**: `src/emir/`（模块族，2026-09-06 启动）

EMIR（电压降 IR 分析 + 电迁移 EM 分析）业务层：`EMIRProject` 工程类型 + 13 个
数据库子模块（每个为 cpp/export/py 三段式，随立项逐个落地）。

**总流程权威文档**：[emir-data-flow.md](../emir-data-flow.md)——13 个数据库的
职责、依赖关系、架构裁定与 API 归属表，本文只记模块结构。

## 子模块

| 子模块 | 状态 | 内容 |
|--------|------|------|
| `project/` | ✅ | EMIRProject（`from emir import EMIRProject`）：13 个数据库创建 API 的归属载体（@register_flow 注册，load_project 按 meta class 动态还原） |
| `lib/` | ✅ | lib 库 db：Liberty 单元库解析入库。`build_lib_db(name, lib_paths)` flow（MapReduce 每文件一解析任务 + 全量合并 LIBLibrary 容器）；`LibDb`（role="lib"）；C++ 侧 `LIBLibrary`/`LIBCell` 数据结构 + `lib_parse_lib_file` 适配层（新思 Open Liberty 参考解析器，见 `third_party/liberty/`，上游方式编译引入，补丁记录 `third_party/liberty/src_local/PATCHES.md`） |
| 其余 11 个 db | 规划 | tech/design/extraction/spef/matrix/timing/vcd/switching/power/current/analysis/em，随立项逐个建立（子模块命名 = 角色名，模块简写见 emir-data-flow.md 裁定 12） |

## 使用方式

```python
from emir import EMIRProject

proj = EMIRProject("./emir_run")
lib_db = proj.build_lib_db(name="lib", lib_paths=["nangate45.lib"])
proj.wait_frozen("lib", timeout=600)
library = lib_db.load_library()   # EXLIBLibrary 整合容器
```

## 命名规范（裁定）

- C++ 类名 = 模块简写大写前缀 + 类名（`LIBCell`/`LIBLibrary`）；
- 独立函数 = 模块简写小写前缀 + 动词短语（`lib_parse_lib_file`）；
- 建库 API = `build_<db 角色>_db`；直接前驱显式传参建数据库链，间接前置经
  `find_db(role=...)` 链上获取；
- Python 绑定产物：`_fly_emir_<子模块>.so`（如 `_fly_emir_lib.so`）。

## 测试

- 单测：`src/emir/<子模块>/tests/`（bazel cc_test / py_test）；
- QA：`qa/emir/`（runqa case，含双文件分布式解析 e2e）。
