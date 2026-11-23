# spef db 立项方案（⑤ 寄生数据库——设计稿待裁定）

> **定位**：EMIR 12 库体系 ⑤ spef db 的实现方案——解析 SPEF（Standard
> Parasitic Exchange Format，标准寄生交换格式，IEEE 1481），把逐网
> 寄生数据（总电容 + 完整 RC 明细）换算为全局网 id，**按 design db
> 分区结构落库**。首期消费者 = power db 的 net load（负载电容——
> emir-data-flow 裁定 4：首期仅服务右端项 net load，不进 matrix 端；
> RC 明细是否入库见待裁定点 2）。方案先行，
> 待逐节裁定后实施。上游 lib db 功耗数据补齐已另行实施（2026-10-13
> 任务，与本方案并行）。创建：2026-10-13。格式调研来源：IEEE 1481
> 规范与公开语法文档（Wikipedia/Parser-SPEF 等）。
> **修订：2026-11-23**——对照 RedHawk-SC User Manual（Release 2024 R2）
> 与 IEEE 1481 语法（开源静态时序工具 OpenSTA 的语法文件核实）：勘误
> CONN 段 `*C` / `*L` 语义与 R_NET 结构两处；新增 §2.4 块级 SPEF、§12
> 下游用途参考、§13 文档同步项；待裁定点保留原编号并补修订建议、新增
> 三项（10-12）。

---

## 1. 定位与依赖

- **职责**：解析 SPEF 文件（网表级寄生描述：逐网总电容 + 连接表 +
  对地/耦合电容明细 + 电阻明细），名字换算为 design db 全局网 id，
  按分区落库；供 power db 消费 net load（`C_load = 互连电容〔本库〕
  + Σ 扇出引脚电容〔lib 库 db〕`——emir-data-flow §3 ⑨ 行）。
- **直接前驱**（显式传参）：design db（网名映射器 + 分区 NETS 连接
  表〔连接位分区枚举——路由依据，同 waveform 复制范围机制〕+ id
  反向映射）。
- **下游消费者**：power db（首期：total_cap 的 net load）；演进期
  matrix db / 提取端（RC 明细的下游消费另立项——本库只存不消费）。
- **不依赖**：lib/tech/timing/waveform——独立数据链（寄生与活动/
  时序正交）。

## 2. 输入与格式（IEEE 1481）

### 2.1 文件结构

```
*SPEF "IEEE 1481-1998"               ← 版本声明（嗅探锚点）
*DESIGN "top"  *DATE ...  *VENDOR ...  *PROGRAM ...  *VERSION ...
*DESIGN_FLOW "..."                   ← 流程标记（可多行；观测收集）
*DIVIDER /  *DELIMITER :  *BUS_DELIMITER [ ]
*T_UNIT 1 NS   *C_UNIT 1 PF   *R_UNIT 1 OHM   *L_UNIT 1 HENRY
                                     ← 单位头（内部换算见 §2.3）
*NAME_MAP                            ← 可选段：长名索引别名压缩
*1 top/u_cpu/u_alu                   ←   *<索引> <名字>（后续引用复用）
*PORTS
p_clk I  p_din I  p_dout O           ← 设计端口（方向）
*D_NET n_answer 0.002146             ← 网段头：网名 + 总电容
  *CONN
  *I u_and1:ZN O  *D JFK10 *L 0.0    ← 实例引脚：方向 / 驱动单元 / 引脚负载电容
  *I u_xor:A1 I  *L 0.0012 *C 1500 2400
                                     ← 接收引脚：引脚负载电容 *L / 坐标 *C x y
  *CAP
  1 u_and1:ZN 0.00042                ← 对地电容（编号 节点 值）
  2 u_and1:ZN u_xor:A1 0.00003       ← 耦合电容（编号 节点 节点 值）
  *RES
  1 u_and1:ZN *123:1 1.2             ← 电阻（节点可为网内内部节点 *编号:序号）
*END
```

- **NAME_MAP**：索引别名展开（`*123` 引用 → 头部映射名）——解析
  边界一次展开，落库名字为展开全名；未登记索引 = 流级破损 raise。
- **节点形态**：`实例名:引脚名`（换算连接位）| `*n:m`（网内内部
  节点，保留原文字串）| 端口名（`*P` 声明）。
- **R_NET（缩减网形态）**：`*R_NET net total_cap` + 逐驱动缩减模型——
  `*DRV <驱动引脚> *CELL <单元名> *C2_R1_C1 <c2> <r1> <c1>`（驱动端
  π 模型：负载端电容 / 串联电阻 / 驱动端电容）+ `*LOADS` 下逐负载
  `*RC <负载引脚> <r> <c>`（可带 `*Q` 极点 / `*K` 留数）。行首同样带
  整网总电容——处置见待裁定点 5。
- **min:typ:max 三值形态**（`*DESIGN_FLOW "MIN_MAX"` 下电容/电阻写
  三值）：首版取 typ 单值，min/max 观测计数（待裁定点 6）。
- **多文件**：`spef_files` 列表（单文件亦列表；块绑定三形态 +
  strip_prefix 复用 timing/waveform 先例——SPEF 按 block 提取的
  局部名场景与 TWF/VCD 同构）。

### 2.2 名字换算（复用既有机制，一次完成）

| SPEF 名字形态 | 换算路径 |
|---|---|
| 网名（D_NET 行） | 剥根（设计名段）→ `DSNameMapperT(net)` → net global id → 分区 NETS 连接条目 → **路由 = 该网全部连接位实例的 primary 分区集合**（网跨区时每区各存一份——每区自足，同 waveform 引脚身份波形复制范围机制） |
| 连接位节点（CONN 行） | `实例名:引脚名` → inst id + pin id（形态②直配同款换算）——明细保存用，未命中计数不阻塞（明细完整性与 net id 主链解耦） |
| 内部节点 / 端口 | 原文保留（`*n:m` 字串；端口名经端口位判定） |

- NAME_MAP 展开 + DIVIDER 归一（`/`）先行；pg 网（VDD/VSS 全局 pg
  集命中）→ 跳过 + 计数（寄生无功耗语义，同 waveform 口径）。
- 未命中网名 → 跳过 + 计数（SPEF 与 design 网表失配的兜底观测）。

### 2.3 单位（待裁定点 7）

头声明 `*C_UNIT`/`*R_UNIT`/`*L_UNIT`/`*T_UNIT` 各自独立（1/10/100 ×
单位档）。**建议内部统一：电容飞法（fF，double）、电阻欧姆（ohm，
double）**——换算 = 头档位 ×10 幂，无损且消费面（power/未来 matrix）
口径单一；时间/电感首版不消费、仅记录头原值。

### 2.4 块级 SPEF 与跨块边界的网（2026-11-23 补设计）

块绑定文件（§2「多文件」）描述块内部走线，顶层文件描述顶层走线。
一条跨块边界的网在展平后的 design db 里是**同一个全局网**（S7 跨块
连接归并）——块内段（块绑定文件）与顶层段（顶层文件）是互补的两部
分寄生，整网负载 = 两者相加，而非二选一。按来源文件的绑定作用域区分：

| 来源组合 | 同一展平网上的条目关系 | 合并语义（建议） |
|---|---|---|
| 顶层文件 + 块实例绑定文件 | 互补（各描述不同走线段） | **相加** |
| 同一作用域的两个文件（两个顶层文件 / 绑同一块实例的两个文件） | 重合（输入异常） | 冲突处置——待裁定点 8 |
| 块定义绑定（定义级复制，timing §7.2） | 逐实例各得一份（不同全局网） | 无合并；边界网的负载只含块内部分——固有局限，观测计数 |

- 判定材料在合并任务本地：分片条目携带来源文件序，文件的绑定形态
  在入口已知——同作用域 = 纯路径文件之间，或绑定目标（块实例 / 块
  定义）相同的文件之间。
- RedHawk-SC 的参照：事件回放流程对「块级运行与整芯片运行的负载电容
  差异」做了专门处理——块级边界实例的负载会因顶层连接而变化，工具改
  取整芯片运行的负载电容（RedHawk-SC User Manual 第 276 页）。

## 3. API

### 3.1 建库

```python
build_spef_db(name, spef_files, design_db, settings=None, alpha=None)
```

- `spef_files: list`——SPEF 文件列表（元素 = 纯路径或块绑定描述符，
  同 timing/waveform 形态）。
- 三段式流程（§5）；入口预处理不读文件内容，SPEF 嗅探（`*SPEF` 头
  关键字 + gzip 魔数〔gzip 支持口径同 waveform 裁定 IV-1：流程层
  解压临时文件〕）下放文件入口任务。

### 3.2 读库（sp_functions.py，供 power db 消费）

| API | 对象 | 说明 |
|---|---|---|
| `load_spef_summary(db)` | `summary` | 单位/来源文件/网命中数/未命中计数/耦合与 R_NET 观测 |
| `iter_spef_partition(db)` | DESIGN_OBJ 锚 | 枚举全部分区 (xp, yp)（空分区空对象，先例同构） |
| `load_partition_spef(db, xp, yp)` | `PART_{xp}_{yp}.SPEF` | 本区逐网寄生：net id → {总电容 + 明细}（power db 取 total_cap；明细为未来扩展） |
| debug：`get_net_parasitics(net_id 或 net 名)` | 同上 + 反查 | 单网点查（LRU 8 先例） |

## 4. 用户设置（settings / alpha）

- **settings（首版空表）**：无稳定键。消费口径（total 含/不含耦合、
  min/max 角选择）属 **power db 消费侧**——本库存原始量（total_cap
  文件自报值 + 对地/耦合分列明细），口径由消费侧推导（同 waveform
  「率由消费侧推导」哲学）。
- **alpha**：

| 键 | 默认 | 约束 | 说明 |
|----|------|------|------|
| `chunk_size_mb` | 256 | int ≥ 16 | 单文件字节切块大小（D_NET 段边界切块，同 timing 先例） |

## 5. 数据结构（C++，前缀 SP——既定简写）

### 5.1 解析边界（`sp_types.h`，不落库）

```cpp
struct SPConn { CMString node_; CMString direction_; CMString cell_; 
                double cap_obs_ff_; };          // *I/*P 行（cap_obs = *L 引脚负载电容；*C 为坐标 x y，不入此字段）
struct SPCap  { uint32_t id_; CMString node1_, node2_;  // node2 空 = 对地
                double cap_ff_; };              // *CAP 行（两形态统一）
struct SPRes  { uint32_t id_; CMString node1_, node2_; double res_ohm_; };
struct SPNet  { CMString name_;                 // NAME_MAP 展开全名（原文形态）
                double total_cap_ff_;           // *D_NET 行自报值
                CMVector<SPConn> conns_; CMVector<SPCap> caps_;
                CMVector<SPRes> res_; };
class SPSpefFile { 头元信息（design/divider/units/design_flow 列表）
                   + CMVector<SPNet> nets_ + 解析级计数器 };
// 切块计划 SPFileChunkPlan：D_NET 段边界字节区间 + NAME_MAP/头段前缀区间
```

### 5.2 落库形态（`sp_partition.h`）

```cpp
// 逐网寄生（id 化终态；明细保留解析边界原文形态——未来 matrix 立项
// 时再决定节点 id 化，本库不预支）
struct SPNetParasitics {
    CMNetId net_id_;
    double total_cap_ff_;        // 文件自报（消费口径见 §4——原始量保存）
    CMVector<SPCap> caps_;       // 对地与耦合分列同存（node2 区分）
    CMVector<SPRes> res_;
    // conn 明细：首版不入库（消费面无需求；*L 引脚负载电容观测值计入
    // summary 统计）——待裁定点 3
};
class SPPartitionSpef {          // PART_{xp}_{yp}.SPEF 正式对象
    CMPartitionId part_id_;
    CMUnorderedMap<CMNetId, SPNetParasitics> items_;   // 键 = 网全局 id
};
class SPSummary {                // "summary" 正式对象
    struct SPFileStats { CMString source_file_;
                         double c_unit_ff_per_unit_;    // 单位换算原值（可追溯）
                         uint64_t net_count_, hit_count_, skipped_pg_count_;
                         uint64_t net_name_miss_count_, rnet_obs_count_,
                                  minmax_obs_count_, coupling_cap_count_; }
    CMVector<SPFileStats> files_;
    // 全局计数 + 单位基准声明（内部 fF/ohm）
};
```

### 5.3 中间形态（temp）

`SPEntrySlice`——逐块产物分区分片（本块触及各分区的
`net id → SPNetParasitics 片段` + 统计 delta）。网不跨块（D_NET 段
边界切块）→ 合并 = 分片收集 + 跨文件同网冲突处理。

## 6. 建库流程（三段式 + 直接任务链，全先例复用）

```
build_spef_db（master，O(1) 提交）：
  ① 轻量预处理（schema + 文件存在性 + 建库 + alpha 写入；不读内容）
  ② flow 根任务 _spef_flow_task
  ③ freeze（依赖 summary + 全部分区对象标记）

_spef_flow_task（根任务；依赖 design db 必要对象锚，跨 db 直读）：
  绑定校验（块绑定目标存在性——条目级兜底同 waveform WFMG::0011 口径）
  → design db 枚举锚
  → 每有效文件一个文件入口任务 _sp_file_entry_task
  → 合并链编排任务（依赖全部块数清单）

_sp_file_entry_task（每文件一任务）：
  SPEF 嗅探（gzip 魔数 → 流程层解压临时文件〔同 waveform 裁定 IV-1〕；
  *SPEF 头关键字形态校验——形态错任务失败透出）
  → 头部解析（单位/DIVIDER/NAME_MAP → temp 对象〔键 `__spg__vars_{fi}`
  同 waveform var 表先例——块任务共享读〕）
  → 切块扫描 sp_plan_file_chunks（D_NET 段边界字节区间——段自包含，
  复用 TWF 顶层构造边界切块模式）
  → 体内提交逐块解析任务 + 块数写清单对象

T2 逐块解析任务 sp_convert_chunk（每块一任务，全并行）：
  逐 D_NET 段解析（NAME_MAP 展开 + CONN/CAP/RES 明细 + 单位换算）
  → 网名换算（§2.2）→ net id → 连接位分区枚举路由
  → 各分区 SPEntrySlice

T3 每分区一合并任务 sp_merge_partition：
  分片收集 → 跨文件同网冲突处理（后文件覆盖〔对齐 waveform 场景 b
  裁定〕+ 计数——待裁定点 8）
  → PART_{xp}_{yp}.SPEF

T4 汇总任务：逐文件统计 + 全局计数 → summary → freeze 标记
```

- 单 D_NET 段失败兜底（段级语法破损 → 跳过 + 计数、空分片照常产出
  ——范式 (b)）；全部文件失败 fatal 码 80（范式 (a)）。
- 可 raise 场景两类（先例同构）：文件不可读（预处理拦截）；文件形态
  错（嗅探不通过，入口任务失败透出）。

## 7. 消息族（前缀 SPMG——待裁定点 1）

| 消息 | 级别 | 场景 |
|------|------|------|
| SPMG::0001 | warn | 网名未匹配（跳过 + 计数，一次汇总） |
| SPMG::0002 | warn | 连接位节点未匹配（明细完整性观测，不阻塞） |
| SPMG::0003 | warn | pg 网跳过（VDD/VSS 全局 pg 集命中） |
| SPMG::0004 | warn | 全部输入解析成功但 0 有效网（空结果放行） |
| SPMG::0005 | warn | R_NET 缩减网形态观测（跳过 + 计数——待裁定 5） |
| SPMG::0006 | warn | min:typ:max 多值形态观测（取 typ——待裁定 6） |
| SPMG::0007 | warn | 跨文件同网冲突（后文件覆盖 + 计数——待裁定 8） |
| SPMG::0008 | fatal(80) | 全部文件解析失败 |
| SPMG::0009 | error | 绑定目标未命中（条目级兜底跳过该文件，仅全部跳过才失败） |
| SPMG::0010 | warn | 段级语法破损跳过（含 NAME_MAP 索引未登记——处置口径待裁定点 11） |
| SPMG::0011 | warn | 网名命中但 design db 无布线几何（分区 NETS 无此网——未布线设计的合法兜底，同 WFMG::0003 / TIMG::0003 口径）：跳过 + 计数，下游该网视为未覆盖 |

## 8. 模块结构（简写 SP 既定）

```
src/emir/spef/
├── cpp/      sp_types.h/.cpp + sp_parser.h/.cpp（sp_parse_spef_file /
│             sp_parse_spef_text / sp_plan_file_chunks）
│             + sp_partition.h/.cpp（sp_convert_chunk /
│             sp_merge_partition / sp_merge_summary）
├── export/   _fly_emir_spef.so（EXSP* 导出）
├── py/       sp_export / sp_db / sp_flow / sp_register_msg / sp_utils /
│             sp_functions / alpha_settings（七文件，同 waveform 形态）
└── tests/    sp_parser_test.cpp + flow error/structure .py
```

## 9. 测试规划

- **测试数据（待裁定点 9——生成策略）**：真实 SPEF 优先开源工具产
  出——候选：OpenROAD 寄生提取器（rcx，DEF+LEF → SPEF）或 magic
  提取流（.ext → SPEF）；轻量替代 = Parser-SPEF 开源仓库的真实样张
  + 手写规范样张（全构造覆盖：NAME_MAP/对地+耦合电容/内部节点/
  端口/R_NET 观测/min:max 观测/单位各档/破损段）。**与 design db 测试
  设计名对齐**（既有 qa/emir/data/waveform/design 三件套优先复用）。
- **单测**：解析器全构造；换算与路由（net id + 连接位分区枚举 +
  跨区每区一份）；合并（跨文件覆盖）；单位换算各档。
- **QA e2e**：基础建库（网命中 + total_cap 精确断言〔手工推导〕+
  分区路由 + 空分区）；多文件块绑定；未命中网兜底；错误流（gzip
  已支持口径/破损/全失败 fatal）；确定性双跑。
- **回归锚点**：与 waveform 的统计推导表交叉——同一设计的网集合
  命中数可对齐断言。

## 10. 演进路径（不在首期）

- R_NET 缩减网支持（消费面出现时）；
- min:typ:max 三值全保存（多角分析）；
- CONN 明细入库 + 节点 id 化（matrix 端立项时）；
- 电感（*L_UNIT 消费面）；
- 与 extraction db（④，几何提取链）的数据一致性交叉校验工具。

## 11. 裁定记录与待裁定点

### 已并入本方案的既有裁定（引用）

- 首期仅服务 net load 不进 matrix（emir-data-flow 裁定 4）；
- 块绑定三形态 + strip_prefix + 条目级兜底 + 三段式流程 + 跨 db 直读
  （timing/waveform 全先例）；
- gzip 支持口径（waveform 裁定 IV-1 同款）；
- 「原始量保存、消费口径由消费侧推导」（waveform 裁定 2 哲学推广）。

### 待裁定点（2026-10-13 提出；2026-11-23 调研补充——【原建议】为
### 原文保留，【补充】为对照 RedHawk-SC User Manual 与 IEEE 1481 语法
### 后的修订建议，均待裁定）

1. **消息前缀**——建议 SPMG（全仓检索无冲突）。
2. **RC 明细入库范围**
   - 【原建议】全量保存——为 matrix 扩展留底，存储代价 = SPEF 文件量
     级；备选只存 total_cap，库极简但未来重解析。
   - 【补充】若保存明细，须与总电容**分对象**（沿 waveform「双容器 =
     双对象」先例）：首期消费者 power db 只要整网总电容，单对象会让
     它连带加载全部 RC 明细；且现稿「网跨区时每区各存一份」会把时钟、
     复位等跨区大网的明细复制多份。建议形态：总电容轻对象按连接位
     分区复制（每区自足），RC 明细单一归属分区存一份、独立对象。
     另一端的依据：dev-rules §7「无意义的字段/中间产物直接删除，不以
     『保留以防万一』为由留存」——首期明细没有消费者，是否入库请一并
     裁定（§12 启示 3）。
3. **CONN 明细首版不入库**——建议不入库（`*L` 引脚负载电容观测值只进
   summary 统计）；备选随 SPNetParasitics 全量保存。
4. （并入 3）
5. **R_NET 形态**
   - 【原建议】跳过 + SPMG::0005 观测（不阻塞建库）；备选遇 R_NET
     文件级报错。
   - 【补充】R_NET 行首同样带整网总电容，对首期功耗用途已经够用；
     整网跳过会让这些网在下游成为「未覆盖、负载为零」。建议**至少
     收录行首总电容**（驱动 π 模型明细列演进项），SPMG::0005 改为
     观测「以缩减形态收录的网数」。
6. **min:typ:max**——建议取 typ 单值 + 观测计数；备选三值全存。
7. **内部单位**——建议电容 fF / 电阻 ohm（double，头档位换算）。
8. **跨文件同网冲突**
   - 【原建议】后文件覆盖（对齐 waveform 场景 b 裁定）+ SPMG::0007
     计数；备选保留首份（lib/timing 口径）。
   - 【补充】须按来源作用域区分（§2.4）：顶层文件 + 块绑定文件对同一
     展平网的条目是互补关系，应**相加**；只有同作用域重合才是冲突。
     冲突时取哪一套，建议本次一并定下今后的默认口径（现有两套：
     lib/timing 保留首份、waveform 后文件覆盖）。
9. **测试数据生成策略**——【原建议】优先调研 OpenROAD rcx 可用性
   （真实工具），不可行则 Parser-SPEF 仓库样张 + 手写规范样张。
   【补充】本机未安装 openroad / magic / klayout / yosys（2026-11-23
   核实），且现有测试设计的 DEF 没有布线——真实提取工具在其上跑不出
   有意义的寄生。建议主路线 = 与波形测试设计网表对齐的手写规范样张
   （`qa/emir/data/waveform/design/` 的网名可直接对齐）；开源样张与
   工具安装途径的核实结论见调研报告底稿（`.work/spef_research/`）。
10. **（新增）完整性与覆盖率统计**——建议 summary 增四项：SPEF 命中
    网数 / 设计内有布线信号网数、未覆盖网数、所连网含未覆盖网的实例
    数、全部所连网电容为零的实例数。依据：RedHawk-SC 的 SPEF 数据
    完整性检查就是「未覆盖实例数 + 零电容检查」两项（手册第 38、
    40 页）。材料在每分区合并任务本地（该区 NETS 对象）。
11. **（新增）NAME_MAP 未登记索引的处置**——§2.1 写「流级破损
    raise」，§7 的 SPMG::0010 写「段级跳过」，两处不一致，须定其一。
    建议文件级失败：未登记索引说明头部映射表不完整，属文件形态错，
    由文件入口任务失败透出（同 waveform 破损文件的处置）。
12. **（新增）次要构造的处置**——D_NET / R_NET 行尾的 `*V` 置信度、
    `*CONN` 段的 `*N` 节点坐标记录、`*INDUC` 电感段、`*D_PNET` /
    `*R_PNET` 物理网段（均为 IEEE 1481 语法内的构造）：建议解析器
    容忍并忽略前三者（电感首版不消费，§2.3），物理网段跳过 + 计数。

## 12. 下游用途参考——RedHawk-SC 对照（2026-11-23）

参照 RedHawk-SC User Manual（Release 2024 R2）。该工具把「从版图
提取电源地网寄生」与「从 SPEF 文件导入信号网寄生」做成同一种视图
（ExtractView）的两个来源（第 49 页；生成命令分别为
`create_extract_view` 与 `create_extract_view_from_files`，第 122、
147 页）；fly 对应为两个库：④ extraction db 与 ⑤ spef db。

| 用途 | 手册口径 | 页码 |
|---|---|---|
| 导入后的优先级 | 外部 SPEF 优先；SPEF 未覆盖的网取内部估算的网电容 | 54、202 |
| 数据完整性检查 | 「未覆盖实例数」（组合 / 时序分列）与「零电容检查」两项；缺少 SPEF 导致功耗或电流波形算错 | 38、40 |
| 静态功耗 | 开关功率 = ½ × 翻转率 × 频率 × 负载电容 × 电压²；负载电容 = 接收端输入引脚电容 + 输出所连网的 SPEF 电容 | 163 |
| 动态电流波形选取 | 电流波形与能量表以「输入转换时间 × 输出负载电容」为索引 | 38、512 |
| 动态去耦合电容 | 不翻转实例的负载电容经驱动端导通电阻接到电源与地：电源、地各一半 × 安静时间占比；计入芯片总电容 | 306–308 |
| 网延迟 | 逻辑传播场景读 SPEF 寄生网络算网延迟 | 230 |
| 信号电迁移 | 需每条信号网的电阻网络，算每段线的平均 / 有效值 / 峰值电流 | 346–348 |
| π 模型 | 默认由外部寄生归并出 π 模型；下游查询 π 模型参数与驱动到接收的有效电阻，而非原始 RC 明细 | 125 |
| 芯片电源模型 | 信号负载电容来自 SPEF | 397 |

对本库的启示：

1. 首期只有「静态功耗」一项用途——整网总电容即全部所需（§1）。
2. 未覆盖的网要有明确口径：RedHawk-SC 有内部估算电容兜底，fly 首期
   ④ extraction db 不提取信号网——未被 SPEF 覆盖的网在 power db 里
   负载为零。本库以「无条目」表达未覆盖并提供覆盖率统计（待裁定点
   10）；兜底规则在 power db 立项时裁定。
3. 动态分析与信号电迁移使用的是 π 模型而非原始 RC 明细——明细入库
   与否（待裁定点 2）与 π 模型生成（演进项，§10）分开考虑。
4. 主输出端口的片外负载不在 SPEF 内——RedHawk-SC 以
   `default_output_port_capacitance` 给缺省值（第 231 页）；属 power
   db 的 settings，本库不涉及。

## 13. 实施期文档同步项（裁定后执行）

- [emir-data-flow.md](../emir-data-flow.md) §5 API 表：`build_spef_db`
  显式参数 `spef_path` → `spef_files` 列表（2026-11-23 已随实）；
  实施后「实施状态」行补 spef db；
- [emir/module.md](emir/module.md) 子模块表 spef 行（随实）；
- [DOC_CHANGELOG.md](../DOC_CHANGELOG.md) 条目。
