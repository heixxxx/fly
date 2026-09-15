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
| `design/` | ✅ | design db（S1-S10 十阶段 + R1-R10 全部落地）：`build_design_db(name, def_paths, lef_paths, lib_db, settings, alpha)` flow——S1 lib 入库→S2 cell lef 并行→S3 lib↔lef merge→S4/S4b DEF 头扫描（block cell/port/DEF via）→S5a COMPONENTS 责任链（fake cell/密度/网名扫描）→S5b 网内容责任链（via instance/逐层密度，批处理控峰值；2026-09-13 连接项 id 化——连接 = (instance local id, 全局 pin id)，解析边界一次换算 + flags 六位 port/driver/receiver/power/ground/clock，INOUT = driver+receiver 同置显式命名 hybrid；未命中兜底跳过 + skipped_invalid_connection_count + DSGN::0025）→S6 层级树+三类编号区间→S7 跨块连接归并（并查集，2026-09-13 裁定：仅 port 相连网、两层树 find 恒一步、root = 层级最高/同级最小 global id、悬空 port root=自身 + DSGN::0018 计数；两级任务 per-DEF slice 并行收集 + 单任务汇总——2026-09-13 连接 id 化后对接键 = 同一 port 的全局 pin id 相等（S7 内部零字符串匹配）；`ds_union.h/.cpp` + `net_union` 单对象）∥S8 全局密度合并（层级树自底向上+格值面积比例分摊 D10 A+三通道独立分列）+分区决策（core/extend 双区域：非边缘扩 2×最高有效层 default_width、最外围 int32 极值；通道比重 6:2:2；alpha 三键优先级 target_partitions > partition_count > partition_target_density 默认 15 万，非法值 DSGN::0013 提醒回退不 raise；2026-09-13 起 alpha 七键经 `DSAlphaSettings` 声明式定义（`design/py/alpha_settings.py`，validator 校验 + DSGN::0013 一次汇总；settings 对象以 `"alpha_settings"` 随建库写入 db，提交侧三键 flow 边界读回、S8 四键任务内读回 + normalize 兜底））→S9 flatten 展平 + 分区保存（两级任务 + 小 DEF 按阈值聚合：plan 任务 worker 上动态提交（分组信息依赖树运行时数据——同 solver kickoff 先例）→ per-组展开任务（每组只读本组 def 产物——每份 DEF 数据只读一次；复合变换存树节点 `DSHierNode.composite_transform_`）→ 每分区一合并任务 → freeze 携全部对象名。归属 = 放置点 core 半开区间 primary 恰一 + extend 副本（UNPLACED 不入分区）；六类对象 `PART_{xp}_{yp}.{GEOMETRY,GEOMETRY_PG,INSTANCES,INST_CONNECTIONS,NETS,NETS_PG}`（2026-09-14 拆分裁定：GEOMETRY/NETS 各按 pg/信号拆两对象——信号网大文件与 pg 小文件物理分离，④ 提取首期只加载 GEOMETRY_PG + NETS_PG 即电源网络输入全集；`DSPartitionNets` 单表化两侧复用同构实例、net_of 单表查 + 加载侧按全局 pg 集 is_pg 路由；2026-09-13 重组裁定：NET_CONNECTIONS → NETS + 单网聚合 `DSNet{net_id, use, connections}`，信号网全量补全 / pg 网不补全；全局 pg 网 id 集 `"pg_nets"` = `DSPgNetSet` power/ground 两 unordered_set O(1) 判定，S9 汇总任务读各分区 pg 片段去重合并，debug API is_pg 与 `load_design_pg_nets` 查询口）——geometry 以 net global id 组织（不换算 root；同日 net id 0 专属 OBS 裁定：net 区间含 local 0 空洞位、global = start + local 无 −1，键 0 = OBS 专属位 + obs 位防御校验，DEF BLOCKAGE 已由 S4 头扫描收录）、via 图形挂 net + 放置点 primary、非 pg 连接全量补全（入 NETS）/ pg 仅本区 instance 端条目（入 NETS_PG，靠 union + instance 维度拼装）；连接条目 id + flags 位自 S5b 直存直拷（2026-09-13 裁定：电源引脚预展开 D18 翻转删除——坐标归 ④ 提取自取）；`ds_flatten.h/.cpp` + alpha `def_aggregate_threshold`（缺省 64 MiB））→freeze。C++ 侧 `DSDesign` 容器（cell/via cell/lib 关联/层级树/hash 查询/partitions_ 分区表）+ `DSBlockBuildData`（per-DEF instances/density/stats）+ `DSBlockNames`（伴生对象，instance/net hasher 按需加载）+ `DSNameHasherT`（六实体 hasher：32 位组 Hash backend、64 位组 Hatrie backend——hat-trie 压缩 1.8x/双段制序列化直载 12.5x 提速/LCP 后缀共享 alpha `lcp_name_arena`）+ `DSNameMapperT`（全局组装轻壳：分派索引最长前缀匹配 1.1-1.5µs 与规模解耦）+ `DSSubPartition`/`ds_merge_global_density`/`ds_decide_partitions`（ds_partition.h，S8 算法）+ `DSNetUnion`/`ds_collect_net_union_slice`/`ds_build_net_union`（ds_union.h，S7 归并）+ `DSPartitionProduct`/`ds_flatten_block`（ds_flatten.h，S9 展开与分片聚合）；`load_design`/`load_design_stack`/`load_design_with`/`load_block_names`/`load_name_mapper`/`load_global_density`/`load_design_net_union`/`load_partition`/`iter_design_partition` 统一加载 API（⑰/⑱ 按需加载）。健壮性语义（R10，2026-09-12）：层引用未定义条目级兜底（DSGN::0010）+ DBU 恒基准 1000（裁定 ㉝）+ 层级树结构错误/hasher 权威段损坏 fatal message（DSGN::0011/0012，退出码 80 + master 联动，见 dev-rules §7.1）。S10 汇总校验 + 冻结前置（2026-09-13 校验分级裁定：损坏类 fatal——并查集不自洽 DSGN::0019 / 分区网格未无缝覆盖 DSGN::0020 / namemap 双向不一致 DSGN::0021，码 80 退出阻断冻结；观测类 warn——id 连续性空洞/重复 DSGN::0022、密度守恒 primary 口径偏差 DSGN::0023；统计汇总 DSGN::0024 INFO。每分区一校验任务并行读单分区产物（不跨区读）+ 全局汇总校验任务由 S9 plan 动态提交排在 merge 后 freeze 前，校验报告 `verify_report` 正式对象挂 freeze final_keys——校验未完成不冻结；`ds_verify.h/.cpp` + `load_design_verify_report`）。流程错误处理（2026-09-13 范式，DEVELOPMENT_GUIDELINES §18）：单 cell lef 语法错误兜底跳过 + DSGN::0014（fake cell 承接引用），全部 cell lef 失败 DSGN::0015 fatal、DEF 语法错误 DSGN::0016 fatal、tech lef 语法错误 DSGN::0017 fatal（码 80）；入口嗅探 LEF（VERSION）/DEF（VERSION+DESIGN）头，误传秒级 ValueError。debug 读库 API（2026-09-13 裁定，DesignDb 六方法：get_cell/get_instance/get_net/get_layer/convert_to_id/convert_to_name——id↔name 转换（inst/net 层级路径 mapper、其余 hasher）、id→partition 反向映射定位分区（`ds_id_map.h` 分段结构：段粒度 2^20 直接索引 + 空洞段跳过 + 段表轻对象 `id_partition_map.{INST,NET}`，S9 分区合并任务写片段临时对象、映射汇总任务 merge 分段）+ 整分区对象按需加载 + 进程内 LRU（容量定值 8）；pg 统计数 flags 位零推导、pg 网不返回 connections 明细；net USE 全量补收（DSNetUse 八值、缺省 SIGNAL、未知值 DSGN::0026 兜底；分区 DSNet.use_ 随网写入）。方案与裁定全量见 [design-db-plan.md](design-db-plan.md)（十阶段）+ [design-db-phase2-plan.md](design-db-phase2-plan.md)（R1-R9 重构与 name 体系终局 ㊱-55）；业务知识见 [design-knowledge.md](design-knowledge.md) |
| `timing/` | 🔶 | timing db（解析器已立项，db flow 待立项——**待确定性测试数据就绪后启动**，2026-09-15 裁定）：TWF（Timing Window File，时序窗口文件，楷登 Innovus `write_timing_windows` 产物）C++ 解析器——`tm_types.h/.cpp` 解析边界结构（TMRange/TMClock/TMNameTiming/TMTimingFile，名字原文形态、单位统一 ns）+ `tm_parser.h/.cpp`（token 流两遍扫描：先 HEADER+WAVEFORM 建时钟表、后 CAUSED_BY 条目；八对字段 RTW/Rt/RDr/RSlk+FTW/Ft/FDr/FSlk，同名跨分组合并取并集+multi_source；源电阻/富余量弃收计数；条目级破损跳过计数、流级破损抛异常）；格式权威源 = 命令手册 25.10 版公开镜像，开源无完整解析器（2026-09-15 全站检索）。**测试数据三途径**（2026-09-15 裁定：一/三保留、二实施）：一 = CircuitNet-N28 数据集（真实 Innovus 产物 cts.twf + 布线后 DEF 交叉验证；BSD-3-Clause；不含 Liberty 走不通全链）；二 = Nangate45 公开 Liberty/LEF + OpenSTA 引脚属性接口（arrival/slew/slack/clock_domains 实证齐全）dump Innovus 格式 TWF——小型确定性设计自造全链输入件，**已产出**：`qa/emir/data/timing/`（Liberty/tech+macro LEF/tm_design 网表+SDC+DEF/twf_gen.tcl/确定性 TWF **三维度版本**——网络维度 7 条 / 引脚维度 19 条（含未连接引脚 * 缺省形态）/ 混合维度 26 条（同分组 NET+PIN 并存，2026-09-15 裁定解析器必须支持）+ README 含两个上游缺陷绕法与跨维度一致性说明），三份真实文件均已入解析器单测（tm_parser_test GeneratedRealTimingFile / GeneratedPinDimensionFile / GeneratedMixedDimensionFile）；三 = 仓库内合成生成器（现有 LEF/DEF 测试数据 + 确定性窗口值，QA 用）。py/export 与建库 flow 随立项补全（裁定：不等待 design db 冻结、不用 MapReduce——直接任务链 + 字节区间切块为单文件分布式流式解析留路径） |
| 其余 9 个 db | 规划 | tech/extraction/spef/matrix/vcd/switching/power/current/analysis/em，随立项逐个建立（子模块命名 = 角色名，模块简写见 emir-data-flow.md 裁定 12） |

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
