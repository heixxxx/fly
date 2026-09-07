# lib db 改造与增强实施计划（含全仓库 export 导入层）

> **状态**：已实施完成（2026-09-07）——A1-A7 与 B 全部落地；单测 83/83、
> 冒烟（export 层 import/emir 幂等/LIBR 注册）3/3、qa（emir/mapreduce/
> solver/project/storage）全绿。实施结果与计划的差异见文末「实施记录」。
> 本文档为实施依据与进度锚点；完成后更新各节状态并记入 DOC_CHANGELOG。

## A 部分：lib db 改造

### A1. Python 目录七文件重组（`src/emir/lib/py/`）

现 `db.py`/`flow.py` 重排为七文件（`__init__.py` 加载顺序：export 最先 → register_msg → db/flow/functions；utils 不导出）：

| 文件 | 内容 |
|------|------|
| `lib_export.py` | `_fly_emir_lib.so` 符号唯一导入点：`from _fly_emir_lib import (EXLIBCell, EXLIBHeaderAttr, EXLIBInternalPower, EXLIBLibrary, EXLIBPin, EXLIBTimingArc, lib_parse_lib_file)` |
| `lib_db.py` | `LibDb`（role="lib"）+ `load_library()` + `build_lib_db` 入口（UserDoc header + Schema 校验器 + `@register_flow(EMIRProject)`）；内部调 `lib_flow.run_lib_flow(db, lib_paths)` |
| `lib_flow.py` | `run_lib_flow`（MapReduce 四阶段装配：partitioner 每文件一分区、processor=lib_utils.lib_parse_one、merger=`a.merge(b); return a`、freeze task 依赖 LIBLibrary） |
| `lib_register_msg.py` | 全局区直书：`fly.register_message_id("LIBR::0001", "WARN")`（merge 抛弃重复 cell）、`("LIBR::0002", "WARN")`（空库兜底） |
| `lib_utils.py` | `lib_parse_one(path)`（worker 解析任务，经 lib_export 取 `lib_parse_lib_file`；文件缺失抛 FileNotFoundError——文件不可读属可 raise 场景） |
| `lib_functions.py` | `load_lib_library(db)`（供其他模块读容器）；不含解析入口 |
| `__init__.py` | 聚合导出（LibDb/build_lib_db/load_lib_library/lib_parse 相关仅内部）+ `from . import lib_register_msg` 副作用 |

### A2. C++ merge 下沉

- `LIBLibrary::merge_from(const LIBLibrary& src)` 成员方法（lib_types.h/.cpp，覆盖现工作区残留的自由函数 lib_merge_library）：
  - cell 冲突：**保留当前（首次出现）、抛弃后续重复，不抛异常**；
  - 抛弃时 `MSG("LIBR::0001", WARN)`：cell 名 + 两处 source_file_ 完整路径 + 抛弃计数；
  - 模板集并入（dst 缺失名才并入，重名先入优先）；skipped_group 统计合并；重建 cell 索引；
  - BUILD：fly_emir_lib deps 加 `//src/log/cpp:fly_log`（MSG/WARN 宏）与 message 依赖（查 log 头是否已含 MSG——MSG 宏在 `src/message/cpp/message_macros.h`，需 include + deps `//src/message/cpp:fly_message`）。
- `lib_export.cpp`：`EXLIBLibrary` 增 `merge` 方法绑定。

### A3. 来源可追溯字段

- `LIBCell` 增 `library_name_`（CMString）+ `source_file_`（CMString，完整路径），进 FLY_SERIALIZE 字段表；
- `lib_parser.cpp` `collect_cell` 链路填充（library 组名 + lib_parse_lib_file 的 path）；
- `lib_export.cpp` `EXLIBCell` 增只读属性 `library_name`/`source_file`。

### A4. 删除 LIBLibrary.name_

字段 / FLY_SERIALIZE 表 / lib_parser 填充 / lib_export 绑定 / 测试断言全清。

### A5. 解析健壮性

- 维持 raise：文件不可读（INVALID_NAME）、语法错误（SYNTAX_ERROR）；
- 「解析成功但 0 cell」不再抛：`MSG("LIBR::0002", WARN)`（含文件路径）+ 返回空容器；
- 既有兜底维持：未知组跳过计数、模板不完整跳过、表数据不校验。

### A6. 加载语义

- `emir/__init__.py` 聚合全部子模块（`from emir.project import *` + `from emir.lib import *`）；
- `emir/project/py/project.py` 尾部 `from emir.lib import *` 移除（消除迂回）；
- `main.cpp` setup_sys_path：`import _fly_emir_lib` 之后加 `ps += "import emir\n";`（master/worker 启动即完成 flow 与消息注册）。

### A7. 测试与文档

- `lib_parser_test.cpp` 增：merge 保留当前抛弃后续（含 MSG 副作用不崩）、library_name_/source_file_ 断言、空库不抛、name_ 移除回归；
- `qa/emir/test_emir_project_lib.py`：重复 cell 场景断言改为「合并成功 + INV_X1 单份保留 + DFF_X1 存在（抛弃后计数）」；load_project 断言保留；
- `docs/emir-data-flow.md` §4 裁定补充（merge 语义 / 来源字段 / LIBR 前缀）+ DOC_CHANGELOG。

## B 部分：全仓库 export 导入层

每个含 C++ 绑定 Python 包：新建 `<模块>_export.py`（原 `from _fly_<模块> import ...` 整体迁入），`__init__.py` 改经 export 层引回；业务文件直连 `.so` 的 import 一并改经 export 层。

覆盖（实施时先精确盘点各包 `_fly_*` import 位置与别名特例）：solver、storage、network、agent、task、core、test、monitor、fly（聚合包）、message（无独立 py 包则并入 fly 聚合层处理）；log 无 Python 源码跳过。

语义等价改造：符号集不变，仅 import 组织变化。

## 实施顺序

1. B 部分（机械，先平地基）→ 构建 + 全部单测验证；
2. A 部分（lib db 业务）→ 构建 + emir 单测；
3. install + 冒烟（启动无 unregistered 警告、import emir 幂等、build_lib_db 全链）；
4. qa 回归：qa/emir、qa/mapreduce、qa/solver、qa/project、qa/storage；
5. 文档收尾 + 全量校验提交推送（pre-push 兜底）。

## 已知风险与注意

- fix 脚本曾两次破坏 BUILD（教训）：本计划 BUILD 改动一律 Edit 手工或恢复 HEAD 后重做，不再写自动改写脚本；
- nanobind def_rw 的 vector 属性 Python 侧读取为拷贝副本——merge 已下沉 C++，规避该类问题；
- qa/emir 的 test_emir_project_lib.py 重复场景断言必须同步更新，否则 qa 失败；
- main.cpp `import emir` 依赖 build/python/emir 包树（fly.sh emir 小节已就绪）。

## 实施记录（2026-09-07 完成）

- **B 部分落地**：新建 export 层文件 10 个（log/container/core/network/solver/storage/agent/test 各 `<模块>_export.py`，fly 聚合 `fly_export.py` 承载 `_fly_message`，message 无独立 py 包并入聚合层）；业务文件直连 `.so` 的 import 全部改道（同包相对引 export 层，跨模块走包根 `from module import ...`）。log 无 py/ 子包，export 层放模块根（fly.sh 安装循环改为复制模块根全部 .py）。task/monitor 无自有 `.so` 的 Python 消费，无 export 文件。
- **stub 测试同步**：业务代码改道包根后，纯 Python 测试的 stub 拦截点从 `_fly_storage`/`_fly_log`/`_fly_core` 换成包根模块名（`storage`/`log`/`core`）。
- **A1-A7 落地**：七文件重组（db.py/flow.py 删除）；`merge_from` 成员 + `EXLIBLibrary.merge` 绑定 + `LIBR::0001/0002`（BUILD 增 log/message 依赖）；`library_name_`/`source_file_` 进序列化；`LIBLibrary.name_` 删除；emir 聚合加载（`emir/__init__.py` project→lib 顺序）+ project.py 尾部迂回移除 + main.cpp `import emir`。
- **计划外修正 1**：原 `network/py/__init__.py` 三个符号（`ex_net_create_transport` 等）在 `_fly_network.so` 中不存在（真实符号为 `ex_net_create_connection_manager`），且 network 包根此前从未被任何 Python 代码成功加载（死符号必 ImportError）——export 层按真实符号表修正。
- **计划外既有缺陷（实锤非本次引入，待 C++ 构建层专项排查）**：fly 进程内加载 `_fly_network.so` 后，进程退出期触发 libfly_core 静态配置表（`Config::INT_DEFAULTS`/`STR_DEFAULTS`）double free。已在 HEAD 干净工作树复现（未改造代码同样崩溃）——network 包根因上述死符号从未被加载，缺陷此前不可达。处置：main.cpp 有意不预加载 `_fly_network`（注释说明），network Python 包根零消费者不受影响。
- **计划外演进**：log 从「跳过」改为建最小 export 层——log 的 INFO/DBG 等是被六个模块直连最多的 `.so` 符号，不建导入点则 B 目标（业务文件不直连 `.so`）残缺；`log_export.py` + 包根 re-export（含 init_log/shutdown_log 等全量绑定符号）。
