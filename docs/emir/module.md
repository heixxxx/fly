# EMIR 模块族 — 分布式 EMIR 仿真分析业务层

## 模块概述

**位置**: `src/emir/`（模块族，2026-09-06 启动）

EMIR（电压降 IR 分析 + 电迁移 EM 分析）业务层：`EMIRProject` 工程类型 + 13 个
数据库子模块（每个为 cpp/export/py 三段式，随立项逐个落地）。

**总流程权威文档**：[emir-data-flow.md](../emir-data-flow.md)——13 个数据库的
职责、依赖关系、架构裁定与 API 归属表；**子模块开发规则**：[dev-rules.md](dev-rules.md)——
lib 立项过程确认的约束提炼（结构/命名/语言边界/异常语义/消息/加载），新子模块立项
必须遵循。本文只记模块结构。

## 子模块

| 子模块 | 状态 | 内容 |
|--------|------|------|
| `common/` | ✅ | EMIR 子模块族公共 Python 基座（非 db 角色子模块，无 `_fly_emir_common.so`）：`AlphaSetting` 单项描述符 + `AlphaSettings` 基类——建库 alpha 配置的声明式五要素定义（setting 名/默认值/值类型及约束介绍/validator/setting 介绍，2026-09-13 裁定；apply 纯逻辑返回 rejected/unknown、normalize 读回兜底、pickle 友好，规范见 dev-rules.md §3） |
| `project/` | ✅ | EMIRProject（`from emir import EMIRProject`）：13 个数据库创建 API 的归属载体（@register_flow 注册，load_project 按 meta class 动态还原） |
| `lib/` | ✅ | lib 库 db：Liberty 单元库解析入库。`build_lib_db(name, lib_paths)` flow（MapReduce 每文件一解析任务 + 全量合并 LIBLibrary 容器；cell 冲突保留首份 + `LIBR::0001` 提醒，语义下沉 `LIBLibrary::merge_from`）；`LibDb`（role="lib"）；py 目录七文件（lib_export/lib_db/lib_flow/lib_register_msg/lib_utils/lib_functions/`__init__`，见 dev-rules.md）；C++ 侧 `LIBLibrary`/`LIBCell`（含来源字段 `library_name_`/`source_file_`）+ `lib_parse_lib_file` 适配层（新思 Open Liberty 参考解析器，见 `third_party/liberty/`，上游方式编译引入，补丁记录 `third_party/liberty/src_local/PATCHES.md`）；流程错误处理（2026-09-13 范式，DEVELOPMENT_GUIDELINES §18）：单文件语法错误兜底跳过 + LIBR::0003（成功部分照常产出），全部失败 LIBR::0004 fatal（码 80）；alpha 设置经 `LIBAlphaSettings` 声明式定义（`lib/py/alpha_settings.py`，首版空字段体系就位；未知键 LIBR::0005 一次汇总提醒忽略，settings 对象以 `"alpha_settings"` 随建库写入 db）；入口嗅探 liberty 头（`library` 关键字，误传秒级 ValueError） |
| `design/` | ✅ | design db（S1-S6 + S8 + R1-R10 全部落地）：`build_design_db(name, def_paths, lef_paths, lib_db, settings, alpha)` flow——S1 lib 入库→S2 cell lef 并行→S3 lib↔lef merge→S4/S4b DEF 头扫描（block cell/port/DEF via）→S5a COMPONENTS 责任链（fake cell/密度/网名扫描）→S5b 网内容责任链（via instance/逐层密度，批处理控峰值）→S6 层级树+三类编号区间→S8 全局密度合并（层级树自底向上+格值面积比例分摊 D10 A+三通道独立分列）+分区决策（core/extend 双区域：非边缘扩 2×最高有效层 default_width、最外围 int32 极值；通道比重 6:2:2；alpha 三键优先级 target_partitions > partition_count > partition_target_density 默认 15 万，非法值 DSGN::0013 提醒回退不 raise；2026-09-13 起 alpha 七键经 `DSAlphaSettings` 声明式定义（`design/py/alpha_settings.py`，validator 校验 + DSGN::0013 一次汇总；settings 对象以 `"alpha_settings"` 随建库写入 db，提交侧三键 flow 边界读回、S8 四键任务内读回 + normalize 兜底））→freeze。C++ 侧 `DSDesign` 容器（cell/via cell/lib 关联/层级树/hash 查询/partitions_ 分区表）+ `DSBlockBuildData`（per-DEF instances/density/stats）+ `DSBlockNames`（伴生对象，instance/net hasher 按需加载）+ `DSNameHasherT`（六实体 hasher：32 位组 Hash backend、64 位组 Hatrie backend——hat-trie 压缩 1.8x/双段制序列化直载 12.5x 提速/LCP 后缀共享 alpha `lcp_name_arena`）+ `DSNameMapperT`（全局组装轻壳：分派索引最长前缀匹配 1.1-1.5µs 与规模解耦）+ `DSSubPartition`/`ds_merge_global_density`/`ds_decide_partitions`（ds_partition.h，S8 算法）；`load_design`/`load_design_stack`/`load_design_with`/`load_block_names`/`load_name_mapper`/`load_global_density` 统一加载 API（⑰/⑱ 按需加载）。健壮性语义（R10，2026-09-12）：层引用未定义条目级兜底（DSGN::0010）+ DBU 恒基准 1000（裁定 ㉝）+ 层级树结构错误/hasher 权威段损坏 fatal message（DSGN::0011/0012，退出码 80 + master 联动，见 dev-rules §7.1）。剩余 S7/S9/S10 待实施。流程错误处理（2026-09-13 范式，DEVELOPMENT_GUIDELINES §18）：单 cell lef 语法错误兜底跳过 + DSGN::0014（fake cell 承接引用），全部 cell lef 失败 DSGN::0015 fatal、DEF 语法错误 DSGN::0016 fatal、tech lef 语法错误 DSGN::0017 fatal（码 80）；入口嗅探 LEF（VERSION）/DEF（VERSION+DESIGN）头，误传秒级 ValueError。方案与裁定全量见 [design-db-plan.md](design-db-plan.md)（十阶段）+ [design-db-phase2-plan.md](design-db-phase2-plan.md)（R1-R9 重构与 name 体系终局 ㊱-55）；业务知识见 [design-knowledge.md](design-knowledge.md) |
| 其余 10 个 db | 规划 | tech/extraction/spef/matrix/timing/vcd/switching/power/current/analysis/em，随立项逐个建立（子模块命名 = 角色名，模块简写见 emir-data-flow.md 裁定 12） |

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
