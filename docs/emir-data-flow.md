# EMIR 仿真分析数据流转流程

> **定位**：业务层总流程权威文档——EMIR（电压降 IR 分析 + 电迁移 EM 分析）工具 13 个数据库的职责、输入、计算内容、依赖关系与架构裁定。框架能力缺口见 [emir-capability-gap.md](emir-capability-gap.md)，求解内核机制见 [solver/module.md](solver/module.md)，本文只定业务数据流。
> **创建**：2026-09-06，经三轮流程确认收敛（v1 初稿 → v2 输入范围/负载电容/电流模型纠正 → v3 全局 ID 化/首期范围裁定）。
> **编号约定**：①-⑬ 为本文内部约定，与流程讨论顺序一致。
> **实施状态**：EMIRProject 子模块 + common 查找表（CMLookupTable）+ lib 库 db（build_lib_db）已实施（2026-09-06，见 [DOC_CHANGELOG.md](DOC_CHANGELOG.md)）；其余 db 随立项逐个落地。

---

## 1. 总体数据流转图

```
━━━━━━━━━━━━ 第 0 层：外部输入文件 ━━━━━━━━━━━━

  .lib 文件        工艺厂 tech 文件     DEF 文件 + LEF 文件      SPEF 文件       PT timing 文件    VCD 文件
  (Liberty 单元    (各层电阻电容        (LEF 含 tech lef：       (标准寄生       (PrimeTime        (信号翻转
   模型)            计算参数 + EM 规则)   layer 信息 + 默认线宽)   交换格式)        静态时序工具)      波形)

━━━━━ 第 1 层：基础数据库（lib/tech 并行；design 依赖 lib）━━━━━

  ① lib 库 db ◄── .lib 文件（多文件，每文件独立解析后汇整为 LIBLibrary）
  ② tech db   ◄── 工艺厂 tech 文件
  ③ design db ◄── DEF + LEF（以 tech lef 的 layer 信息与各层默认线宽重建
                    net 金属导线几何）+ ① lib 库 db（读入 LIBLibrary：
                    cell 齐全性校验 + cell name → cell id 转换）；
                    为 cell / instance / pin / net 分配全局 id 并
                    持久化 name → id 映射

━━━ 第 2 层：左端项与右端项双轨分布式并行 ━━━

  【左端项：电导矩阵 A（电源网络）】                 【右端项：电流激励 b】
  ④ extraction db ◄── ③ 导线几何 + ② 电阻电容参数     ⑦ timing db ◄── PT timing 文件 + ③ id 映射
  ⑥ matrix db ◄── ④ 分区电阻电容数据                  ⑧ vcd db ◄── VCD 文件 + ③ id 映射
     （逐分区直接生成 sub matrix，                      ⑨ switching db ◄── ⑧ vcd db / VCD 直读(+③) / 用户设置
      分区间以端点机制保存连接性）                      ⑩ power db ◄── ① energy 模型 + 引脚电容
                                                           + ⑨ 翻转率 / one probability + ⑦ 时序窗口
  （⑤ spef db ◄── SPEF 文件 + ③ id 映射，                  + ③ 实例连接 + ⑤ net load
    首期仅服务右端项 net load，不进 ⑥）               ⑪ current db ◄── ⑩ energy 中间数据 + load cap 等
                                                           （首期静态：逐 instance pin 的平均电流值）

━━━━━━━━━━ 第 3 层：仿真分析（汇合）━━━━━━━━━━

  ⑫ analysis db ◄── ⑥ 分区 sub matrix + ⑪ 电流 + ③ 注入点映射
     （以 instance id + pin id 从电流数据构造真实右端项 b，分布式求解 G·v = i）
  ⑬ em db ◄── ⑫ 节点电压 + ⑥ 支路电导 + ② EM 规则
     （支路电流 → 电流密度 → 电迁移违例与寿命分析）
```

左右两端在第 2 层完全并行（互不依赖），在 ⑫ 汇合求解，⑬ 收尾。

---

## 2. 全局 ID 化设计（贯穿约束）

工具内部所有 instance、pin、net 统一转换为整数 id 保存，name（名字）只出现在建库边界：

- **③ design db 是 id 分配的权威源头**：建库时为全部 cell、instance、pin、net 分配全局唯一 id，并持久化 name → id 映射（含 id → name 反查，供报告输出用）。其中 **cell id 的分配需读入 ① 的 LIBLibrary**（cell 全集，以 cell name 组织）：校验设计中引用的 cell 全部齐全，并为每个 cell 分配独占 id；design db 的实例表经 (cell id, instance id) 关联 ① 的 cell 数据。
- **全部下游数据库内部只引用 id，不携带名字**：
  - ⑤ spef db：SPEF net 名 → net id；
  - ⑦ timing db：instance / pin 名 → instance id + pin id；
  - ⑧ vcd db：翻转事件（event）最终挂载在 net id 或 (instance id, pin id) 上，解析时依赖 ③ 的 name → id 映射；
  - ⑨⑩⑪：逐 (instance id, pin id) 组织活性、功耗能量与电流数据；
  - ⑫ analysis db：以 instance id + pin id 从 ⑪ 构造右端项 b。
- **收益**：跨数据库关联稳定（名字映射只在 ③ 做一次）、对象紧凑（整数键，序列化与查找高效）、分布式分块数据可按 id 区间切分。
- **分布式考量**：③ 建库并行解析时需按分区预留 id 区段，保证并行分配不冲突（实现期细化）。

---

## 3. 各数据库明细

| 序号 | 数据库 | 外部文件输入 | 前置数据库 | 核心计算 | 主要产出数据 |
|-----|--------|------------|-----------|---------|-------------|
| ① | lib 库 db | .lib 文件（Liberty，多文件 list） | 无 | 分布式解析（每文件一独立任务）：energy 查找表、引脚电容（receiver pin load）、时序模型等全量数据表；结果汇整为单一容器 | **LIBLibrary** 整合容器（cell 集合，以 cell name 为键 + 库头单位与默认参数 + 模板集） |
| ② | tech db | 工艺厂 tech 文件 | 无 | 解析各层电阻计算参数、电容计算参数、电迁移（EM）规则 | 层电阻电容参数表 + EM 规则表 |
| ③ | design db | DEF 文件 + LEF 文件（含 tech lef） | ① lib 库 db（读入 LIBLibrary：cell 齐全性校验 + cell name → cell id 转换） | 解析实例清单、网表、电源网（SPECIALNETS）；以 tech lef 的 layer 信息与各层默认线宽重建 net 金属导线几何；读入 LIBLibrary 校验 cell 齐全并分配 cell / instance / pin / net 全局 id | 按空间分块的实例表（id 化，经 cell id 关联 lib 数据）、重建的导线几何、网名索引、name → id 映射表 |
| ④ | extraction db | 无 | ③ design db + ② tech db | 按空间分区对电源网导线几何做寄生提取：层电阻、对地电容、通孔电阻；在分支/拐角/交叉处打断建节点；建议同时持久化「instance 电源引脚 ↔ 图节点」绑定 | 天然分区的提取图（节点表 + 电阻电容边表 + 对地电容表） |
| ⑤ | spef db | SPEF 文件 | ③ design db（net 名 → net id 映射） | 解析外部提取器输出的逐网络电阻电容，按 net id 归置 | 分区寄生图对象；**首期仅服务 ⑩ 的 net load，不进 ⑥** |
| ⑥ | matrix db | 无 | ④ extraction db（首期唯一来源） | 逐分区直接生成 sub matrix（电导矩阵）；分区间通过**端点机制**保存连接性数据 | 分区 sub matrix 序列 + 端点连接性数据 |
| ⑦ | timing db | PrimeTime 产生的 timing 格式文件 | ③ design db（id 映射） | 解析逐实例/引脚的到达时间、翻转时间窗口 | 按 instance id 分块的时序窗口表 |
| ⑧ | vcd db | VCD 文件 | ③ design db（id 映射） | 解析翻转波形（大文件按时间片分块），翻转事件挂载到 net id 或 (instance id, pin id) | 按时间片分块的翻转事件数据 |
| ⑨ | switching db | 可选 VCD 文件直读 | ⑧ vcd db 或 VCD 文件直读（均需 ③ id 映射）；亦允许用户直接设置 | 从波形统计（或用户直接给定）逐 instance pin 的翻转率（toggle rate）、one probability（逻辑 1 概率）等活性信息 | 逐 (instance id, pin id) 的活性统计表 |
| ⑩ | power db | 无 | ① lib 库 db + ⑨ switching db + ⑦ timing db + ③ design db + ⑤ spef db（net load） | 逐实例功耗：内部功耗（energy 表 × 翻转率）+ 开关功耗（load cap × 电压平方 × 翻转率）；load cap = net load（⑤，扩展来源 ④）+ receiver pin load（①） | 功耗结果 + **中间数据 energy（逐翻转事件能量，供 ⑪ 使用）** |
| ⑪ | current db | 无 | ⑩ power db（energy 中间数据）+ load cap（⑤ net load、① 引脚电容）| 基于能量数据与负载电容合成电流（非功耗除以电压）；**首期静态：逐 (instance id, pin id) 的平均电流值（单值）** | 逐 instance pin 的电流数据 |
| ⑫ | analysis db | 无 | ⑥ matrix db + ⑪ current db + ③ design db（注入点映射） | 以 instance id + pin id 从 ⑪ 构造真实右端项 b（注入到分区图节点）；分布式求解 G·v = i | 节点电压、电压降（V_ideal − v）、支路电流 |
| ⑬ | em db | 无 | ⑫ analysis db + ⑥ matrix db + ② tech db | 支路电流（电压差 × 支路电导）→ 电流密度（电流 ÷ 层宽 × 厚度截面积）→ 对照 ② 的 EM 规则 | 逐线段电流密度表 + 违例清单 |

---

## 4. 关键架构裁定记录（已确认）

以下裁定经用户确认，为设计与开发的约束前提：

1. **基础三库职责**：② tech db 读工艺厂 tech 文件（电阻电容计算参数 + EM 规则）；③ design db 同时读 DEF 与 LEF（含 tech lef），用 tech lef 的 layer 信息与各层默认线宽重建导线几何；① lib 库 db 读 .lib。
2. **全局 ID 化**：见 §2，③ 为 id 分配源头，下游全 id 引用。
3. **全链路分布式，分区从源头天然形成**：④⑤ 的电阻电容数据按空间分区产出，⑥ 逐分区直接生成 sub matrix（无全局矩阵阶段），分区间以端点机制保存连接性。要求分布式求解器针对该数据形态**专项增强，而非简单复用**；现有 `src/solver/py/dbs.py` 的 MatrixDb（role="matrix"，全局 COO 单对象 + kickoff 单点分区）是测试框架的测试性数据库，不是 EMIR 的 matrix db。
4. **首期专注电源网络**：⑥ 仅依赖 ④，忽略信号网络提取；⑤ spef db 首期仅作为 ⑩ 的 net load 来源。
5. **静态先行**：⑪ 首期为逐 instance pin 的平均电流值；动态增强通过新数据库（如 dynamic current db）叠加实现——**db 模式下新功能通过新 db 实现，不改动已有 db**。
6. **右端项 b 的组装职责在 ⑫**：⑪ 以 (instance id, pin id) 粒度保存电流，与电源网络分区解耦；⑫ 求解前做注入点映射（实例电源引脚位置来自 ③ 的实例位置 + LEF macro 引脚偏移）。
7. **电流模型**：⑪ 基于功耗计算的中间数据 energy 与 load cap 等合成电流，不使用功耗除以电压。
8. **timing 输入格式**：新思 PrimeTime 静态时序分析工具产生的 timing 格式。
9. **design db 依赖 lib db（2026-09-06 补充）**：lib db 阶段以 cell name 区分 cell；design db 建库时读入 lib db（LIBLibrary）确保 cell 数据齐全，并为所有 cell 分配独占 id——cell name → cell id 映射随 design db 持久化。
10. **LIBLibrary 整合职责（2026-09-06 补充）**：lib db 多文件分布式解析的结果最终汇整于单一 C++ 容器 LIBLibrary（cell 集合 + 库头 + 模板），作为 lib db 的顶层产出与下游的统一读取入口。
11. **查找表结构属框架层（2026-09-06 补充）**：Liberty 查找表族（模板 + 表）抽象为 fly 全局通用结构，放 `src/container/` 模块（依赖 common(types) 与 common(serialization)；2026-09-06 模块族重组后归属），C++ 实现（算法引擎使用的结构保持语言间零开销传递），经 FLY_SERIALIZE_* 序列化入 db；后续 timing/power/current/switching 各 db 复用。
12. **C/C++ 解析器与 emir 命名规范（2026-09-06 补充）**：文件解析器一律 C/C++（不使用 Python 解析器），lib 首版采用新思 Open Liberty 参考解析器做流程验证（SYNOPSYS Open Source License v1.0，以独立第三方库方式引入，与 lefdef 同构）；C++ 类名 = 模块简写大写前缀 + 类名（如 LIBCell），独立函数名 = 模块简写小写前缀 + 动词短语（如 lib_parse_lib_file）。模块简写经全库前缀冲突检测（须避开 export 目录、导出符号 EX+模块缩写体系〔EXAgent/EXCore/EXNet/EXPeer/EXSlv/EXStg/EXTask〕、FLY_ 宏、fly_* BUILD 目标）后定为：LIB(lib)、TC(tech)、DS(design)、**PEX(extraction**，寄生提取 parasitic extraction 的行业缩写；EX 撞导出前缀体系、EXT 撞 EXTask*，均排除)、SP(spef)、MX(matrix)、TM(timing)、VCD(vcd)、SW(switching)、PWR(power)、CUR(current)、ANS(analysis)、EM(em)；common 查找表为 CM（CMLookupTable）。
13. **API 命名与前置获取（2026-09-06 补充）**：建库 API 统一 `build_<db 角色>_db`；直接前驱显式传入（建数据库链），间接前置一律经数据库链 `find_db(role=...)` 获取（如 em db 不显式传 matrix db，经 analysis db 链取）。
14. **分布式解析与整合采用 MapReduce（2026-09-06 补充）**：lib db 的多文件解析 + LIBLibrary 整合用 MapReduceJob 实施——构造第一参数即数据保存 db（`MapReduceJob(db, output_name)`，中间对象为 temp、freeze 自动清理，最终输出持久化于该 db）；每文件预分区（`set_pre_partitioned`）为独立解析任务，全量合并（merge_type=full）产出 LIBLibrary。

---

## 5. 与现有代码的关系

- **lefdef（DEF/LEF 解析）**：`src/lefdef` 为纯 C 库（Si2 6.0_62-p004 fork），已有读取路径性能优化成果，但未接入 Bazel、无 Python 绑定、无 db 映射层——③ 的建库需按其 README 演进路线（接 Bazel → 解析正确性回归 → 现代接口 + db 映射）逐项补齐。
- **solver（分布式求解）**：求解内核（RAS 域分解 + 两层 Galerkin 粗校正、动态多时间步三阶段架构）是 ⑫ 的算法基础，但输入形态需从「全局矩阵单对象 + kickoff 分区」改造为「预分区 sub matrix + 端点连接性」；现有子域对象的 ghost 连接三元组与邻居映射是端点连接性的一种表达，可衔接复用（建立时机从求解时前移到 ⑥ 生成时持久化）。
- **流程承载（EMIRProject）**：全部 13 个数据库及其创建 API 归属独立 Project 子类 **EMIRProject**（`src/emir/` 模块族：project 子模块 + 每 db 一个完整子模块（cpp/export/py 三段式），使用方式 `from emir import EMIRProject`）。API 统一 `build_<db 角色>_db`，直接前驱显式传入（建数据库链），间接前置经链 `find_db(role=...)` 取：

  | db | API | 显式参数 | 链上获取（find_db） |
  |----|-----|---------|-------------------|
  | ① lib | `build_lib_db` | name, lib_paths（list，每文件一独立解析任务） | — |
  | ② tech | `build_tech_db` | name, tech_path | — |
  | ③ design | `build_design_db` | name, def_path, lef_paths, **lib_db** | — |
  | ④ extraction | `build_extraction_db` | name, design_db, tech_db | — |
  | ⑤ spef | `build_spef_db` | name, spef_path, design_db | — |
  | ⑥ matrix | `build_matrix_db` | name, extraction_db | — |
  | ⑦ timing | `build_timing_db` | name, timing_path, design_db | — |
  | ⑧ vcd | `build_vcd_db` | name, vcd_path, design_db | — |
  | ⑨ switching | `build_switching_db` | name, 来源三选一（vcd_db / vcd_path / 用户设置） | — |
  | ⑩ power | `build_power_db` | name, lib_db, switching_db, timing_db, design_db, spef_db | — |
  | ⑪ current | `build_current_db` | name, power_db | 负载电容来源（spef net load、lib 引脚电容）经 power db 链取 |
  | ⑫ analysis | `build_analysis_db` | name, matrix_db, current_db | design db（注入点映射）经 matrix db 链取 |
  | ⑬ em | `build_em_db` | name, analysis_db | matrix db、tech db 经 analysis db 链取 |

---

## 6. 演进方向（不在首期，按 §4 裁定 5 的「新 db 叠加」原则）

| 方向 | 说明 |
|------|------|
| dynamic current db | 时间序列电流波形（逐翻转时刻叠加），替代/并列 ⑪ 的平均电流值；配合 ⑧⑨ 的时间粒度数据 |
| 动态 IR 多时间步求解 | 复用 solver dynamic 三阶段机制；含电容后矩阵非对称/不定依赖 Krylov 内核（[emir-capability-gap.md](emir-capability-gap.md) P1-2） |
| 非线性迭代反馈 | 电流随节点电压变化的定点迭代（⑫ → ⑪ 反馈环），对应 P1-3 矩阵在线更新缺口 |
| 信号网络提取并入 ⑥ | 若将来左端项需要 SPEF 信号网寄生，扩展 ⑥ 的输入（当前忽略） |

---

## 7. 维护约定

- 本文是 EMIR 业务数据流的**唯一权威落点**；各数据库立项实施后，其实现细节归各自模块文档（`docs/<module>/module.md`），本文只维护流程与依赖关系，并在对应条目链接实现文档。
- 裁定变更（新增 db、依赖调整）须更新 §1 流程图与 §3 表格，并记入 [DOC_CHANGELOG.md](DOC_CHANGELOG.md)。
