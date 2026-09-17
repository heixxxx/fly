# timing db 立项方案（⑦ 时序数据库——设计稿待裁定）

> **定位**：EMIR 十三库体系 ⑦ timing db 的实现方案——API、用户设置、内部
> 流程、数据结构与测试规划。方案先行（用户裁定 2026-09-06 起），**本文
> 待逐节裁定后实施**；实施按分工协议交 coder 子代理、评审交 code-reviewer。
> 总流程依赖见 [emir-data-flow.md](../emir-data-flow.md)；开发规则见
> [dev-rules.md](dev-rules.md)；测试数据链见 `qa/emir/data/timing/`。
> 创建：2026-09-16；**全部裁定点于 2026-09-16 裁定完毕（§13），进入
> 可实施状态**。
> **实施状态（2026-09-17）：已全量落地**（类型族奠基 → tm_partition 换算
> 与合并 → export/Python flow → QA e2e；实施口径与本文的衔接点及偏差
> 见 docs/DOC_CHANGELOG.md 2026-09-17 条目与 git 提交说明）。

---

## 1. 定位与依赖

- **职责**：解析 TWF（Timing Window File，时序窗口文件，楷登 Innovus
  `write_timing_windows` 格式——解析器 `src/emir/timing/cpp/` 已立项落地，
  含网络/引脚/混合三维度支持），将逐条目时序数据（到达窗口 + 翻转时间 +
  时钟归属 + 常量标记）换算为全局 id，**按 design db 分区结构落库**。
- **直接前驱**（显式传参）：design db（名字映射器 + 分区表 + id 反向映射）。
- **下游消费者**：⑩ power db（翻转时间 → 功耗查找表输入翻转维度、时钟
  周期/频率）；演进期 dynamic current db / 动态压降（到达窗口对齐电流
  波形）与 em db（方均根电流场景窗口）。
- **不依赖**：lib/tech/extraction 等——纯右端项数据链。

## 2. 输入与格式

- **格式锁定**（已裁定 2026-09-15）：楷登 Innovus `write_timing_windows`
  25.10 版手册格式（S 表达式；八对字段 RTW/Rt/RDr/RSlk + FTW/Ft/FDr/FSlk；
  HEADER/WAVEFORM/CAUSED_BY/CONSTANT；`-pin` 风味引脚条目与混合维度已
  由解析器支持并有真实数据覆盖）。
- **输入形态**（修订原裁定 7「单文件」→ **文件列表**，2026-09-16 用户
  裁定新增分块 TWF 测试需求）：单文件（单 DEF 设计）或按 block 分块的
  多文件（层级设计每子 block 一份 + 顶层一份）。
- **名字空间三形态**（2026-09-16 补设计——分块 TWF 的真实形态是块内
  局部名，需要显式块上下文绑定；`timing_files` 列表元素支持。键名
  裁定 2026-09-16：`timing_files` / `file_name` / `block_inst`）：
  1. 纯路径字符串 = 条目名为**全层级路径**（顶层视图/全芯片展平 TWF）；
  2. `{"file_name": ..., "block_inst": <块实例层级路径名>}` = 条目名为
     该块内**局部名**，按块实例绑定换算（见 §7.1）；
  3. `{"file_name": ..., "block_cell": <块 cell 名>}` = 条目名为该 cell
     定义内局部名，**定义级时序对该 cell 全部实例成立**（类比 Liberty
     单元时序），建库期复制到全部实例（见 §7.2）；
  4. 以上 2/3 形态可附加 `"strip_prefix": <外层前缀>`——TWF 若产自
     包装顶层仿真（测试壳），条目名携带与当前设计不匹配的外层层级
     前缀，先段级剥离再进入块绑定换算（见 §7.4；同族业界先例 =
     Cadence read_twf 的 -strip_prefix/-scope/-cell 选项族）。
- **弃收字段**（解析器已实现）：源电阻（RDr/FDr）、富余量（RSlk/FSlk）
  ——当前无消费者，条目级计数；C/D 结尾标记计数不入库。
- **单位**：解析边界统一换算 ns（TIME_SCALE）。

## 3. API（建库 + 读库）

### 3.1 建库（EMIRProject，@register_flow）

```python
build_timing_db(name, timing_files, design_db, settings=None, alpha=None)
```

- `timing_files: list`——TWF 文件输入列表（元素 = 纯路径字符串或
  §2 绑定描述符 dict；单文件亦列表；分块 TWF 传全部块文件）。每文件
  一独立解析任务组（天然分布式点）。
- `design_db`——直接前驱显式传参（裁定 13）。
- 返回 `TimingDb` 句柄；异步 4 步范式（检查 → 建库 → 入口任务链 →
  freeze 任务），提交后立即返回（评审 P2-3：T1 master 侧同步毫秒级、
  design 快照为异步任务并在其 worker 执行体上动态提交下游全链——同
  design flow 分区编排任务先例）。
- **入口等待语义**（用户裁定 2026-09-15）：不等待 design db 冻结
  （`wait_frozen`）——design 快照任务经任务依赖只等**必要数据对象**
  （层级树、各 DEF 名字伴生对象 hasher 集、id_partition_map INST 段表、
  分区表随 DESIGN_OBJ）。入口校验（master 同步）只查：文件可读 + TWF
  头嗅探（`TIMING_WINDOWS` 关键字，误传秒级 ValueError）+ alpha 逐键
  校验 + 绑定描述结构；绑定**目标存在性**（块实例路径 / 块 cell 名在
  design db 命中）随 design 快照任务异步校验——2026-09-17 条目级兜底
  裁定：单文件未命中 → TIMG::0011 error + 跳过该文件（零条目、计数入
  summary.invalid_binding_count）；仅全部文件被跳过才任务内 ValueError
  （评审 P2-3 后不阻塞 master 提交线程）。

### 3.2 读库（tm_functions.py，供其他模块消费）

| API | 对象 | 说明 |
|-----|------|------|
| `load_timing_clocks(db)` | `clocks` | TMClockTable：时钟 id → {名, 周期, 上升/下降沿时刻}（周期 → 频率 = 1/period 由消费侧推导） |
| `load_timing_summary(db)` | `summary` | 覆盖率/弃收/兜底计数 + 逐来源文件统计 |
| `iter_timing_partition(db)` | DESIGN_OBJ 锚 | 枚举全部分区 (xp, yp)（行主序——T3 对每分区写 TIMING 对象，空分区亦产出空对象，与 design 先例同构） |
| `load_partition_timing(db, xp, yp)` | `PART_{xp}_{yp}.TIMING` | TMPartitionTiming：本区逐实例逐引脚时序 |
| debug：`get_timing(db, inst_id)` | 同上 + id_partition_map | 单实例点查（经 INST 反向映射定位分区 → 整区加载进程内 LRU，容量 8 同 design debug API 先例） |

## 4. 用户设置（settings / alpha，dev-rules §3 + 裁定 18）

- **settings（稳定，首版空表）**：无——首版无成熟到收纳的稳定配置；
  键表随成熟从 alpha 迁入。
- **alpha（未稳定，声明式五要素 `TMAlphaSettings` + `alpha_settings.py`）**：

| 键 | 默认 | 类型与约束 | 说明 |
|----|------|-----------|------|
| `chunk_size_mb` | 256 | int ≥ 16 | 单文件字节区间切块大小（逐块解析任务粒度）；后续单文件流式分布式增强的调节钮 |
| `format` | `auto` | {auto, innovus} | 方言覆盖；auto = 头嗅探（首版仅 innovus 一种，键为后续方言扩展预留） |

- 未知键/非法值 → TIMG 一次汇总提醒（不 raise），settings 对象以
  `"alpha_settings"` 随建库写入 db（基座约定）。

## 5. 数据结构（C++，FLY_SERIALIZE）

### 5.1 解析边界（已落地 `tm_types.h`）

`TMRange` / `TMClock` / `TMNameTiming`（名字原文形态、四组值 + 六标记位
pin_kind/multi_source/constant/四字段存在位）/ `TMTimingFile`（头部元
信息 + 时钟表 + 条目表 + 计数器）——名字换算前的中间形态，不落库。

### 5.2 落库形态（本方案新增 `tm_partition.h`）

```cpp
// 逐引脚时序（id 化终态；值 = 解析边界 TMRange 原样搬运）
struct TMPinTiming {
    uint32_t pin_id_;            // 全局平铺 pin id
    TMRange rise_arrival_, fall_arrival_, rise_slew_, fall_slew_;
    CM_FLAGS(uint8_t, rise_arrival, fall_arrival,
             rise_slew, fall_slew, constant, multi_source)
};

// 逐实例时序（时钟归属 + 引脚稀疏表）
struct TMInstanceTiming {
    uint32_t clock_id_;          // 时钟表键（kTMNoClock = 无归属）
    CMVector<TMPinTiming> pins_; // 实例引脚少，紧凑 vector 优于 map
};

// 分区正式对象 PART_{xp}_{yp}.TIMING（primary 恰一，无 extend 副本——
// 时序为点数据，无空间延展语义）
class TMPartitionTiming {
    uint32_t part_id_;
    CMUnorderedMap<uint64_t, TMInstanceTiming> items_;  // 键 = 实例全局 id
};

// 时钟表（"clocks" 正式对象；跨文件按名合并保留首份）
class TMClockTable {
    struct Entry { CMString name_; double period_, posedge_, negedge_; };
    CMVector<Entry> clocks_;     // 下标即 clock id
};

// 汇总（"summary" 正式对象）：逐文件条目数/命中数、未匹配实例/引脚/网、
// 悬空网、未放置实例、CONST/NO_TW 覆盖、弃收计数（源电阻/富余量）、
// C/D 观测、跨文件冲突数、单位与来源文件清单（可追溯）
class TMSummary { /* 计数器 + 来源文件表 */ };
```

**裁定修订（原裁定点 4）**：Innovus 格式**无实例级频率**字段（那是
RedHawk sta.timing 方言概念）——频率经时钟表周期推导（1/period），不
单独存储实例级覆盖字段。

### 5.3 中间形态（temp 对象，freeze 清理）

- `TMChunkPlan`——切块清单（文件 → [(字节起点, 字节终点)]）；
- `TMEntrySlice`——逐块解析产物分区分片（块 → 各分区 (inst_id,
  TMInstanceTiming) 片段 + 统计片段）。

## 6. 建库流程（直接任务链，**不用 MapReduce**——用户裁定 2026-09-15）

```
入口校验（master 同步：文件可读 + 头嗅探 + alpha + 绑定描述结构）
→ T1 切块扫描（master 侧单任务，同步执行——毫秒级 I/O；逐文件按字节
   偏移行对齐 + 记录括号边界切块，块大小 alpha chunk_size_mb）→
   TMChunkPlan
→ design 快照任务（master 提交、异步执行——评审 P2-3：依赖系统等
   design db 必要数据对象，快照组装 + 绑定目标校验 + 落临时对象；随后
   在 worker 上动态提交下游全链——快照键集/分区清单依赖 design db 运
   行时数据，无法静态提交，同 design flow 分区编排任务先例）
→ T2 逐块解析任务（每块一任务，全并行；块自包含 = 头段公共前缀拼块，
   评审 P1-1；[inputs 注入快照临时对象]）
   每任务：tm_parse_twf_text(块)（复用已落地解析器）
   → 名字换算（§7）→ inst_id 经 id_partition_map.INST 段表路由
   → 本块所涉各分区的 TMEntrySlice 片段 + 统计片段
→ T3 每分区一合并任务 → PART_{xp}_{yp}.TIMING 正式对象
→ T4 汇总任务（时钟表跨文件合并 + summary 聚合）
   时钟合并优先级（2026-09-16 裁定）：**顶层文件（无绑定、纯路径）定义
   优先**，其余（块绑定文件）按文件序首份兜底；任何周期/沿时刻差异经
   TIMG::0007 提醒（不 raise）
→ freeze 任务（依赖全部正式对象写完；中间临时对象严格清理——键固定
   名 __tmg__{name}〔uid 已删，2026-09-17 裁定〕，清理责任单点）
```

- 任务依赖经 R9 `api.deps(db)` 传播 + task 内 `run_direct` 直跑。
- **单块失败兜底**（§7.2 范式 (b)）：块语法破损 → 跳过 + 计数，本块空
  分片照常产出（依赖链保持满足）；**全部文件失败** → fatal message
  码 80（范式 (a)，master 联动）。

## 7. 名字换算（解析边界一次完成，flow 内 C++）

| 条目维度 | TWF 名字形态 | 换算路径 |
|----------|-------------|----------|
| 网络（NET，缺省风味） | 网名（层级路径，**不含设计名前缀**——2026-09-16 命名裁定） | DSNameMapperT(net) → net global id → 分区 NETS 对象该网连接条目中 **driver 位**条目 → (inst_id, pin_id)；无 driver（悬空网/仅端口）→ 端口条目 port 位 → 跳过计数；**design db NETS 只收有布线几何的网（「NETS 跟随网副本」既定语义——连接在 INST_CONNECTIONS 的网不落 NETS 表）——未布线设计的网条目全部计 TIMG::0003 悬空（合法兜底口径，见 §9）**；**多驱动网（2026-09-16 裁定：需处理，不取首个）→ 该网时序值挂全部 driver 位条目**——每个 (inst_id, pin_id) 各存一份、值同源该网条目（Innovus 网级窗口本就是多驱动合并值）+ summary 多驱动网计数（时钟网格即此形态） |
| 引脚（PIN 风味） | `实例层级路径/引脚名` | DSNameMapperT(instance) → inst global id；**pin 名 → 全局 pin 名 id（单哈希查，无 cell 组合键——2026-09-16 裁定：pin 同名同 id）** |
| CONSTANT | 上述任一 | 同上映射；constant 位置位、无值搬运 |

### 7.1 块实例绑定（block，局部名 → 全局 id）

局部名换算不走字符串拼前缀，走 design db 的 **local 名空间 + 偏移**同源
路径（与 S5b/S9 的 id 组装一致，精确且更便宜）：

- 实体条目：该块定义的 `DSBlockNames_<i>` 伴生哈希器（实例/网两 local
  名空间）→ local id → **+ 该块实例的 inst_start/net_start 偏移**（S6
  层级树区间表）→ 全局 id；
- 引脚条目：local 实例 id + pin 名 → 全局 pin 名 id（同上表，无 cell 组合键）；
- 块端口条目：归属键 = **块实例自身全局 id**（对应「local 0 = 块实例
  自身占位」语义，与 INST_CONNECTIONS 键 0 = 根端口先例同构）。

### 7.2 块定义绑定（block_cell，定义级时序全实例复制）

- 语义类比 Liberty 单元时序：该 cell 每个实例共享同一份局部时序；
- 换算：层级树枚举该 block cell 的全部实例节点（树按 cell 查询）→ 逐
  实例经 7.1 偏移路径展开 → 各实例独立分区路由；
- **建库期复制**（非消费期引用展开）——保持「分区对象按实例自足」原
  则；块被实例化 N 次即 N 份数据，规模成本与几何展开同构；
- 块内跨实例可见的端口条目同样归块实例自身。

### 7.4 包装前缀剥离（strip_prefix，2026-09-16 补设计）

场景：TWF 针对某 block 生成，但经包装顶层（如测试壳 `tb_top/u_dut`）
仿真产出——条目名 = 外层前缀 + 块内名字，外层前缀与当前设计层级不匹配。

- **段级剥离**：条目名按文件头部 `DELIMITERS` 声明的分隔符拆段，起始
  段序列与 strip_prefix **精确段匹配**（非裸字符串前缀——防 `u_dut`
  误配 `u_dutx`）则剥去，余部进入 §7.1/§7.2 块绑定换算；
- **未命中前缀的条目**：跳过 + TIMG::0010 计数提醒（余部必然换算失败，
  显式计数保覆盖率可见）；剥后余空（条目名恰为前缀）同此处置；
- **整文件零命中**：空结果放行 + 提醒（不 fatal）；
- **匹配口径**：作用于解析边界反斜杠清理后的名字；分隔符经文件头
  DELIMITERS 归一（解析器需增记录 `delimiters_` 字段——实施注记）；
- **不做**：包装壳对块内名字的重命名（非前缀性改名）不覆盖——需用户
  名字映射表，列演进项（业界无标准格式，Cadence 亦仅前缀级）。

### 7.5 通用规则

- 换算在 T2 任务内（worker 加载 mapper/块哈希器一次，逐条目换算——
  mapper 微秒级 × 数十万条目为分钟级，可接受；性能不达预期时优化点在
  批量前缀换算，预留接口）。
- pg 网条目（VDD/VSS，若上游未滤）→ 跳过 + 计数（时序无 pg 语义）。
- 未放置实例（design db 不入分区）→ 跳过 + 计数（与 D14 口径一致）。
- 绑定目标不存在（块实例路径未命中 / cell 名未命中）→ 条目级兜底
  （2026-09-17 裁定）：该文件 TIMG::0011 error + 跳过（零条目入库、
  计数入 summary.invalid_binding_count）；仅全部文件被跳过才任务内
  ValueError（输入整体无意义；评审 P2-3 后随 design 快照任务异步执行
  ——不阻塞 master 提交线程）。

## 8. 分区路由与对象布局

- 路由键 = 实例全局 id → `id_partition_map.INST` 段表（段粒度 2^20，
  按需加载段对象）→ partition id → (xp, yp)。
- 归属 = **primary 恰一**（时序点数据无 extend 副本语义——与几何/网
  对象的 extend 副本不同，2026-09-15 已裁定）。
- 对象命名沿用 design db 分区产物惯例（同一 (xp, yp) 网格坐标系）：
  `PART_{xp}_{yp}.TIMING` + 全局 `clocks` / `summary` / `alpha_settings`。

## 9. 消息族（前缀 TIMG，待裁定点 1）与异常语义

| 消息 | 级别 | 场景 |
|------|------|------|
| TIMG::0001 | warn | 实例名未匹配（跳过 + 计数，一次汇总；**网名未命中并入本族文案**——`net_name_miss_count` 字段随 0001 一并透出） |
| TIMG::0002 | warn | 引脚名未匹配（跳过 + 计数） |
| TIMG::0003 | warn | 网条目无驱动/悬空（跳过 + 计数；**成因含 design db NETS 表无此网**——NETS 只收有布线几何的网（§7），未布线设计的网条目全部计此，合法兜底） |
| TIMG::0004 | warn | 全部输入解析成功但 0 有效条目（空结果放行；触发条件 = `total_hit == 0`，两种成因：① 全部条目名字未命中 design db（0001/0002 家族覆盖全部条目）、② 条目命中但全部被跳过计数（悬空/pg/未放置/strip 未命中）——两种均非建库失败） |
| TIMG::0005 | （已删）曾用于 alpha 未知/非法键一次汇总；2026-09-17 裁定入口校验改 header 直接 raise 后注册删除 |
| TIMG::0006 | warn | 跨文件同名条目冲突（保留首份，沿用裁定 15） |
| TIMG::0007 | warn | 时钟名跨文件周期/沿不一致（保留首份） |
| TIMG::0008 | warn | 未放置实例跳过（无分区归属） |
| TIMG::0009 | fatal(80) | 全部文件解析失败（流程级范式 (a)） |
| TIMG::0010 | warn | strip_prefix 未命中条目跳过（含剥后余空；一次汇总） |
| TIMG::0011 | error | 绑定目标未命中（block_inst/block_cell 不在 design db——2026-09-17 条目级兜底裁定：该文件跳过零条目、计数入 summary.invalid_binding_count；仅全部文件被跳过才任务失败） |

- 可 raise 场景仅两类（dev-rules §7）：文件不可读、头嗅探不通过——
  入口同步拦截（不建库、不起任务）。
- C++ 侧同族计数器经 TMSummary 落库，消息由 flow 侧读汇总一次发出。

## 10. 模块结构（dev-rules 三段式 + 六文件）

```
src/emir/timing/
├── cpp/            tm_types.h/.cpp ✅ + tm_parser.h/.cpp ✅
│                   + tm_partition.h/.cpp（本方案 §5.2/§7 换算与合并）
├── export/         nanobind 绑定（_fly_emir_timing.so；FLY_EXPORT_* 宏）
├── py/             tm_db.py / tm_flow.py / tm_register_msg.py /
│                   tm_utils.py / tm_functions.py / __init__.py
└── tests/          tm_parser_test.cpp ✅ + tm_flow_error.py（新）
```

- 注册接入：emir 聚合 + main.cpp `import _fly_emir_timing` + fly.sh
  install 循环（AGENTS.md 新模块六步）。
- 加载语义：`import emir` 全功能可用（dev-rules §8）。

## 11. 测试规划

- **单测**：解析器 15+ 例已落地（tm_design 三维度真实文件 + pg_grid）；
  新增 tm_flow_error.py（嗅探/alpha/空结果/冲突合并/fatal 子进程，同
  lib/design flow error 先例）；tm_partition 单测（名字换算 + 路由 +
  合并语义，用 design tests 合成数据）。
- **QA e2e**（qa/emir/）：tm_design 三维度（小规模全断言）→ pg_grid
  （1.2 万实例规模 + 分区路由）→ 三套新设计输入件（含**分块 TWF 多文件
  读取**——2026-09-15 新需求，最大 10 万实例层级设计）。
- **确定性标准**：同输入双跑产物逐字节一致（数据链先例延续）。

## 12. 演进路径（不在首期）

- **单文件流式分布式解析增强**（用户裁定预留）：切块协议已按字节区间
  自包含设计（T1/T2 形态即流式任务的退化形式），增强只动切块器与任务
  派发，不动数据结构；
- **T3 按需派发**（评审 P3-9）：当前每分区一合并任务读**全部**块分片
  （分片含各分区片段，任务内按 pid 取本区片段）；块分片可二级索引到
  「本块触及分区」后，T3 任务 inputs 只声明触及本分区的分片，读放大
  降为按需；
- **网条目定位索引**（评审 P3-9）：ctx 组装期建 net → 首个命中分区
  NETS 对象的索引（当前 T2 逐网条目线性扫全部分区 NETS 对象，
  O(分区数 × 网条目数)；任一分区副本都有该网完整连接表，索引取首个
  命中即正确）；
- 多时钟源分窗口存储（跨时钟域分析，现合并最宽窗口 + multi_source 位）；
- 新方言接入（format alpha 键预留：RedHawk sta.timing / 新思 twf——
  待真实样例）。

## 13. 裁定记录（2026-09-16 全部裁定完毕，方案进入可实施状态）

1. **消息前缀 TIMG**（10 条消息族如 §9 表）——✅ 裁定：确认；
2. **timing_files 列表化**（原单文件裁定修订为列表，承载分块 TWF；
   参数名 `timing_files` / 描述符键 `file_name`）——✅ 裁定：确认（键
   名 2026-09-16 早前裁定）；
3. **实例级频率不存储**（Innovus 格式无此字段，频率经时钟表周期推导）
   ——✅ 裁定：确认；
4. **网络维度条目锚定驱动引脚**（接收实例输入翻转由消费侧取所在网驱
   动值推导）——✅ 裁定：确认，且**多驱动网需处理**：时序值挂全部
   driver 位条目（每 (inst_id, pin_id) 一份、值同源）+ summary 多驱动
   网计数——不取首个（见 §7 表；时钟网格即此形态）；
5. **时钟表跨文件合并**——✅ 裁定：**优先保留顶层**（顶层/无绑定文件
   定义优先，其余按文件序首份兜底）+ TIMG::0007 差异提醒（见 §6 T4）；
6. **读库 API 面**（§3.2 五接口 + debug LRU 点查；首版不加全量加载口）
   ——✅ 裁定：确认；
7. **alpha 两键**（chunk_size_mb=256 / format=auto）——✅ 裁定：确认；
8. **qa/emir e2e 分区断言口径**——✅ 裁定：认可下限断言倾向（pg_grid
   断言 ≥2 分区不锁死数量，三套新数据落定后收紧）；
9. **块上下文绑定三形态**（纯路径全路径名 + `block_inst` local 名偏移
   换算 + `block_cell` 全实例建库期复制；绑定目标未命中入口
   ValueError）——✅ 裁定：确认；**三套新设计输入件须同时覆盖
   block_inst 与 block_cell 两种绑定形态的真实数据**（2026-09-16
   裁定）；
10. **包装前缀剥离 strip_prefix**（段级精确匹配 + 块绑定组合；未命中
    跳过计数 TIMG::0010；DELIMITERS 感知；非前缀性改名列演进）——✅
    裁定：确认。
