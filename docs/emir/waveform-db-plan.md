# waveform db 立项方案（⑧ 波形数据库——设计稿待裁定）

> **定位**：EMIR 库体系 waveform db 的实现方案——原规划 ⑧ vcd db 与
> ⑨ switching db **合并**（2026-10-12 用户裁定）并**更名 waveform db**
> （库体系 13 库 → 12 库，编号全重排——2026-10-12 第二轮裁定）：同一
> db 同时承载波形容器（VCD 解析的翻转事件序列，**按分区去重存储 +
> 分区局部波形 id 引用**——第二轮裁定 4）与统计容器（逐实例引脚活动
> 统计）；用户不给 VCD 文件、仅经 settings 设置统计值时同样产出逐分区
> 对象（波形容器为空）。方案先行，**本文待逐节裁定后实施**；实施按
> 分工协议交 coder 子代理、评审交 code-reviewer。
> 总流程依赖见 [emir-data-flow.md](../emir-data-flow.md)；开发规则见
> [dev-rules.md](emir/dev-rules.md)。
> 创建：2026-10-12；裁定五轮（§13 裁定记录 I/II/IV/V，2026-10-12
> 三轮 + 2026-10-13 两轮）。**全链实施已落地**：首轮（解析层/换算合并
> 层/导出与流程层/QA，按裁定 I/II/III 口径，评审通过 + P1 修复完毕）+
> **裁定 IV/V 重构批次已实施**（gzip 支持、混合模式、轨道归并、
> 四级回落、统计与过滤挪合并、快照删除、变量表入库、词法公共化、
> 初值事件修复、标量速记修复；QA 补混合模式、gzip 压缩输入、轨道
> 归并与四级回落场景）。实施分工（用户裁定 2026-10-12）：
> 主 agent 仅任务分配与测试设计；输入文件生成、代码编写交 coder
> 子代理、评审交 code-reviewer（串行派发）；测试使用开源工具
> （Icarus Verilog）仿真生成真实 VCD。调研底稿：`.work/pdf-research/`。

---

## 1. 定位与依赖

- **职责**：解析 VCD（Value Change Dump，值变化转储文件，IEEE 1364
  标准 ASCII 格式，四态 0/1/x/z），把信号翻转事件与活动统计换算为全局
  id（挂 (instance id, pin id)——裁定 3，net 名推导与 inst pin 名直配
  两形态），**按 design db 分区结构落库**；波形（事件，去重）与统计
  （活动值，逐引脚物化）双容器**全量入库**（裁定 1）。支持
  settings-only 模式（无 VCD 输入，统计容器物化用户设置、波形容器
  空——消费接口恒定）。
- **直接前驱**（显式传参）：design db（名字映射器 + 分区 NETS 连接表 +
  id 反向映射）。
- **下游消费者**：power db（统计容器：翻转率 = (rise_count +
  fall_count) / DURATION、静态概率按 x/z 口径换算、方向分解喂 Liberty
  rise/fall 能量分表；波形容器：演进期动态电流合成 + 时间窗分析）。
- **不依赖**：lib/tech/extraction——纯右端项数据链（活动传播引擎
  亦不在首期——裁定 6）。

## 2. 输入与格式

- **格式锁定**：IEEE 1364 标准 VCD（四态）。头部声明段
  （`$date`/`$version`/`$timescale`/`$scope module` 嵌套/`$var`
  类型+位宽+id 码+名〔可带位区间〕/`$upscope`/`$enddefinitions`）+
  事件段（`$dumpvars` 初始快照 → `#时间戳` + 值变化；标量 `0#`、
  向量 `b1010 !`〔最高位在前〕、real `r1.5 \`；`$dumpoff` 后全 x、
  `$dumpon` 恢复、`$dumpall` 全量重置）。**扩展 VCD（EVCD：strength/
  dumpports）不支持**（记录级跳过 + 计数）；**gzip 压缩 VCD 支持**
  （2026-10-13 用户裁定由演进项提前实施：入口魔数嗅探后解压到 db
  临时文件再进常规切块链，临时文件随 freeze 清理责任单点）。
- **时间单位**：`$timescale` 值域 1s~1fs 十的幂；**内部统一飞秒
  int64**（第二轮裁定 9——无损承载全部档位），事件时刻、各态时长、
  DURATION 均以 fs 落库。
- **向量拆位**：向量按位拆解——VCD 向量记录只出现在值变化时，逐位
  拆后仅变化位产生事件条目（无存储放大）；统计逐位累计后并入目标
  pin（位级匹配细则见 §7）。
- **输入形态**：`vcd_files` 列表（单文件亦列表；元素支持 timing db
  名字空间三形态：纯路径全层级名 / `{"file_name", "block_inst"}` 块
  实例绑定 / `{"file_name", "block_cell"}` 块定义绑定 + 可选
  `strip_prefix`，§7 逐条复用）。
- **settings 统计输入**（settings-only 模式的主输入，键形见 §4）。

### 2.1 多文件合并语义（已裁定，2026-10-12 第三轮）

多文件输入（`vcd_files` 列表长度 > 1）按**文件绑定形态精确分派合并语义**（元素形态在建库入口已知，无需运行时猜测）：

| 场景 | 文件形态 | 合并语义 |
|------|---------|---------|
| a：分段仿真转储（主语义） | 纯路径文件（全层级名） | **逐文件独立解析，合并期按分区重组保存**：同一波形身份键跨文件的事件按文件序拼接（时间轴偏移对齐——逐文件首末时间戳嗅探，各文件独立 [0,..] 形态则累计前文件时长作偏移，保全局单调不减）；统计直和（T0/T1/TX/TZ/rise/fall 逐项相加，DURATION 同加——分段仿真下直和严格等价单文件全程统计） |
| b：分块转储（块绑定文件） | block_inst / block_cell 描述符 | 各文件名字空间不同，引脚集合理论上不相交——**暂不考虑多文件实例引脚重合场景**；万一重合（同波形身份键出现在多个块绑定文件），**按文件先后顺序数据覆盖**（后文件统计与波形整体替换先文件 + WFMG::0006 计数——注意与 lib/timing 库「保留首份」相反，本库显式裁定后优先） |
| 混合（纯路径与块绑定文件重合） | — | 按场景 b 规则：覆盖 + 计数 |
| c：多激励/多场景 | — | **明确不支持**（2026-10-13 用户裁定：非「首版与否」，该场景不在本工具支持范围；如未来确需多场景分析，另行立项独立输入面） |

## 3. API（建库 + 读库）

### 3.1 建库（EMIRProject，@register_flow）

```python
build_waveform_db(name, vcd_files=None, design_db, settings=None, alpha=None)
```

- `vcd_files: list | None`——VCD 文件输入列表（元素形态同 §2）；
  **缺省 None = settings-only 模式**（`settings.activity` 必须给出，
  否则入口报错——空输入无意义）。每文件一独立解析任务组。
- `design_db`——直接前驱显式传参。
- 返回 `WaveformDb` 句柄；三段式流程（§6，同 timing db 2026-09-17
  裁定：master 提交 O(1) 任务，切块/嗅探/解析全部 IO 在 worker 任务
  并行）。
- **入口等待语义**（同 timing db）：不等待 design db 冻结——flow 根
  任务经任务依赖只等必要数据对象；入口预处理只查文件存在性 + alpha
  逐键校验 + settings 结构（不读文件内容；VCD 头嗅探〔`$timescale`/
  `$enddefinitions` 关键字 + gzip 魔数检测〕下放文件入口任务，形态错
  任务失败透出）。

### 3.2 读库——模块函数层（wf_functions.py，供其他模块消费，@wait_obj 依赖声明）

调用规范同 timing db 先例：本地直调自动轮询等待；task 内调用须
`api.deps(db, ...) + [自身其他依赖]` 传播依赖 + `run_direct(api, db,
...)` 直跑。

| API | 对象 | 说明 |
|-----|------|------|
| `load_waveform_summary(db)` | `summary` | 全局 DURATION（归一化基准）+ x/z 口径元数据 + 覆盖率/弃收/兜底计数 + 逐来源文件统计（起止时刻/事件数/命中数）+ settings 默认值元数据（消费侧展开依据） |
| `iter_waveform_partition(db)` | DESIGN_OBJ 锚 | 枚举全部分区 (xp, yp)（行主序——空分区亦产出空对象，与 design/timing 先例同构）。下游逐分区并行消费的枚举口 |
| `load_partition_activity(db, xp, yp)` | `PART_{xp}_{yp}.ACTIVITY` | **统计容器（power db 主输入）**：本区逐实例逐引脚 {波形 id 引用 + 四态时长 + rise/fall 计数} |
| `load_partition_waveform(db, xp, yp)` | `PART_{xp}_{yp}.WAVEFORM` | **波形容器（演进期动态电流合成输入）**：本区波形实体表（分区局部波形 id → 事件序列）。统计消费者无需加载——双对象独立加载（裁定：双容器） |

**下游换算口径**（power db 侧，本库不预除）：

```
toggle_rate(pin) = (rise_count + fall_count) / (total_duration_fs × 1e-15)
P1(pin) = t1 / (t0+t1+tx+tz)          # x_z_handling = denominator_include（默认）
P1(pin) = t1 / (t0+t1)                # x_z_handling = exclude
方向占比 = rise_count / (rise_count + fall_count)   # 喂 Liberty rise/fall 能量分表
```

### 3.3 读库——debug 用户查询层（WaveformDb 句柄方法，交互定位面）

| 方法 | 入参 | 返回 |
|------|------|------|
| `get_activity(inst_id_or_name)` | 实例全局 id（int）或层级全路径名（str） | dict：实例 id + 逐引脚 {pin id、pin 名、waveform id、T0/T1/TX/TZ（fs）、rise/fall 计数、**导出视图**（toggle_rate / P1 / 方向占比，按 summary 元数据口径现算）}；未命中 None |
| `get_waveform(inst_id_or_name, pin_name=None)` | 实例 id 或名（+ 可选 pin 名过滤） | dict：实例 id + 逐引脚 {pin 名、waveform id、事件序列 [(time_fs, value_str)]}（value_str ∈ "0"/"1"/"x"/"z"）；settings-only 模式或无事件命中 → 每引脚空序列；未命中 None |
| `load_waveform_summary_obj()` | — | EXWFSummary（§3.2 函数的句柄形态） |
| `load_partition_activity_obj(xp, yp)` | 分区坐标 | EXWFPartitionActivity（句柄形态） |
| `load_partition_waveform_obj(xp, yp)` | 分区坐标 | EXWFPartitionWaveform（句柄形态） |

- 定位机制同 design/timing debug 先例：INST 反向映射段表定位分区 →
  整区对象按需加载 + 进程内 LRU（容量 8）。
- Python 绑定访问面（EXWF* 类）：WFPinActivity（pin_id / waveform_id /
  t0/t1/tx/tz / rise_count / fall_count 只读属性）、WFWaveform
  （waveform_id / t_start / t_end / events 列表 [(time, value)] / size）、
  WFPartitionActivity（part_id / size / instance_at / instance_ids）、
  WFPartitionWaveform（part_id / waveform_count / waveform_at(id) /
  iter）、WFSummary（files 表 + 全局计数 + 元数据）。

## 4. 用户设置（settings / alpha，dev-rules §3 + 裁定 18）

本 db 的 settings **首版即有稳定键**（统计值设置是用户面主功能）：

```python
settings = {
    "activity": {                          # 活动统计设置（settings-only 主输入）
        "default": {"toggle_rate": 2.0e6,        # 全局默认翻转率（次/秒，rise+fall 合计）
                     "static_probability": 0.5}, # 全局默认逻辑 1 概率
        "duration": 1.0,                   # 统计窗口时长（秒，默认换算基准；缺省 1.0）
        "overrides": {                     # 逐实体覆盖（键 = inst pin 层级路径名或 net 名——
            "u_cpu/u_alu/ZN": {"toggle_rate": 5.0e6,   # net 名经 §7 形态 A 推导展开
                                "static_probability": 0.3,
                                "rise_fraction": 0.6}, # 可选：上升沿占比（缺省 0.5）
            ...}
    },
    "x_z_handling": "denominator_include", # denominator_include（默认，裁定 5）
                                           # / exclude / hold_previous（消费侧口径，
                                           #   随 summary 元数据落库）
}
```

- **物化口径**（裁定 6 同意）：`overrides` 显式条目物化到 (inst, pin)
  统计容器；`default` **不逐引脚物化**——作为元数据随 `summary` 落库，
  消费侧展开（与「率由消费侧推导」裁定 2 口径一致）；settings-only
  模式的波形容器照常产出空对象（每引脚 waveform_id = 无波形哨兵）。
- **混合模式**（2026-10-13 用户裁定：直接支持，非演进项）：VCD 模式
  下 `settings.activity.overrides` 同样生效——语义 = 用户对特定实体
  给出**权威统计值**，建库期在每分区合并产物（统计容器）上替换对应
  (inst, pin) 条目的统计值（网名覆盖经 §7 形态①推导展开）；**波形
  引用保留**（事件是实测真实数据，统计被用户值覆盖——消费侧统计走
  覆盖值、演进期动态合成走真实事件，两者不矛盾）；覆盖条目数计入
  summary。优先级：用户覆盖 > VCD 实测。`default` 在 VCD 模式下
  不生效（实测已覆盖全部命中实体，全局默认无意义）。
- 用户统计值换算回原始量入库：`T1 = p × D`、`T0 = (1−p) × D`、
  `rise_count = rate × D × rise_fraction`、`fall_count = rate × D ×
  (1−rise_fraction)`、`TX = TZ = 0`（D = duration 秒换 fs）。
- **alpha（未稳定，`WFAlphaSettings` 声明式）**：

| 键 | 默认 | 类型与约束 | 说明 |
|----|------|-----------|------|
| `chunk_size_mb` | 256 | int ≥ 16 | 单文件字节区间切块大小（逐块解析任务粒度，同 timing db） |

## 5. 数据结构（C++，FLY_SERIALIZE；前缀统一 WF——第二轮裁定 2）

### 5.1 解析边界（`wf_types.h`，不落库）

- `WFVar`——头部 `$var` 声明：id 码（`CMString`）、名字（scope 全
  路径 + 名，层级分隔符归一 `/`）、宽度、位区间、类型枚举
  （wire/reg/...；real/event 不支持功耗语义——跳过 + 计数）。
- `WFVarTable`——单文件 var 表（文件入口任务解析头部产出，**落
  waveform db temp 对象**〔2026-10-13 用户裁定：写入 db、块任务经
  db 按引用共享读取，不再随任务参数逐块复制〕；块任务不重解析头部
  文本）。
- `WFEvent { uint64_t time_fs_; WFValue value_; }`——逐位事件；向量
  记录拆位产物（`WFValue`：四态 2 bit 枚举 ZERO/ONE/X/Z）。**值等价
  过滤（与前一值相同的重复行丢弃）在 T3 合并阶段于完整序列上统一
  做**（2026-10-13 裁定：T2 不做过滤——非首块无块首值可判；首事件
  无前值不过滤，自第二事件起丢弃同值行）。
- ~~`WFChunkSnapshot`~~——**已删除**（2026-10-13 裁定：统计挪到合并
  阶段基于完整序列计算 + 等价过滤同挪合并后，块首快照的两个用途
  均消失；连带 T1 切块扫描退化为纯字节切块，不再维护全 var 当前
  值表，「按可匹配 var 过滤快照」的优化项随之不存在）。
- `WFFileChunkPlan`——切块计划（头段公共前缀区间〔`$enddefinitions`
  前〕+ 块字节区间表〔块界 = `#时间戳` 行边界；**首块起点 =
  min(`$dumpvars` 段起点, 首个 `#` 行起点)**——2026-10-13 裁定修复：
  初值事件段不可丢失〕+ 各块时间范围 + 时间档位字段）。

### 5.2 落库形态（`wf_partition.h`）——波形去重 + 波形 id 引用（第二轮裁定 4）

```cpp
// —— 波形容器（PART_{xp}_{yp}.WAVEFORM 正式对象）——

// 单条波形：一份去重的事件序列。身份 = 源 VCD var 经名字换算的目标
// （形态 A = net id——同网全部连接位共享这一份；形态 B = (inst, pin)）
struct WFWaveform {
    uint32_t waveform_id_ = 0;   // 分区局部 id（T3 合并时分配；= 容器数组下标）
    uint64_t t_start_fs_ = 0, t_end_fs_ = 0;
    CMVector<WFEvent> events_;   // 时间升序；向量拆位后仅变化位有条目
};

class WFPartitionWaveform {
    CMPartitionId part_id_;
    // 波形 id = 数组下标（O(1) 直索引；id 分配序 = 身份键全序——形态①
    // net id 先行 / 形态② (inst, pin) 字典序后行，跨构建可复现）
    CMVector<WFWaveform> waveforms_;   // settings-only 模式 = 空数组
};

// —— 统计容器（PART_{xp}_{yp}.ACTIVITY 正式对象）——

// 逐引脚活动：统计原始量 + 波形引用（引替复制——56 字节/引脚换消费
// 直接性；同网连接位统计值同源相同，不引波形反查）
struct WFPinActivity {
    CMPinId pin_id_;
    uint32_t waveform_id_ = 0;   // 最终波形引用——经 §7.3 四级回落解析
                                 // 后物化（2026-10-13 裁定）：
                                 // 1 引脚自身轨道 → 2 所连网轨道 →
                                 // 3 同网 output pin 轨道（多个取连接表
                                 // 第一个）→ 4 连接表第一个有波形的引脚；
                                 // 四级全无 = 哨兵 UINT32_MAX（settings-only
                                 // 条目 / 完全未覆盖信号——消费侧走 default）
    uint64_t t0_fs_ = 0, t1_fs_ = 0, tx_fs_ = 0, tz_fs_ = 0;
    uint64_t rise_count_ = 0;    // 仅 0→1（x 参与的转换不计——裁定 7）
    uint64_t fall_count_ = 0;    // 仅 1→0
};

struct WFInstanceActivity {
    CMVector<WFPinActivity> pins_;   // 引脚稀疏表（形态对齐 TMInstanceTiming）
};

class WFPartitionActivity {
    CMPartitionId part_id_;
    CMUnorderedMap<CMInstanceId, WFInstanceActivity> items_;  // 键 = 实例全局 id
};

// —— 汇总（"summary" 正式对象）——
class WFSummary {
    struct WFFileStats {             // 逐来源文件（可追溯）
        CMString source_file_;
        double timescale_sec_;       // 头声明原值（fs 已归一）
        uint64_t t_start_fs_, t_end_fs_;   // 文件时间范围（多文件偏移对齐依据）
        uint64_t event_count_;       // 拆位后事件总数
        uint64_t hit_count_;         // 换算命中 (inst, pin) 条目数
        // 各类跳过计数（net 未匹配 / inst-pin 未匹配 / 悬空 / pg 跳过 /
        // 未放置 / strip 未命中 / real-event 跳过 / 位宽失配 / 破损块）
    };
    CMVector<WFFileStats> files_;
    uint64_t total_duration_fs_ = 0; // 全局 DURATION（Σ文件时长——率/概率
                                     // 归一化唯一基准；逐引脚等值不重复存）
    // 全局计数：x/z 观测、多文件时间重叠段、跨文件冲突、多驱动网、
    // 波形数（去重后）、失败块数、无效绑定文件数
    // settings 元数据：default{toggle_rate, static_probability} /
    //   duration / x_z_handling（消费侧展开与换算依据）
};
```

**去重设计语义**（裁定 4 + 2026-10-13 裁定）：同一份翻转数据（同一
标识码轨道的变化序列）在分区内只存一份 `WFWaveform` 实体；引脚统计
条目以 waveform_id 引用。轨道身份归并规则见 §7.2（同轨道双名统一
立网身份）；引脚最终引用经 §7.3 四级回落解析。**引脚身份波形的分区
复制范围 = 自身实例所在分区 + 所连网的全部连接位所在分区**（四级
回落 3/4 级可能被同网其它引脚引用——每区自足原则，回落解析在合并
任务内本地完成，跨分区本地可解）。跨分区不共享 id 空间（分区局部
id 互不相干）。

### 5.3 中间形态（temp 对象，链上任务自清理）

- `WFEntrySlice`——T2 逐块产物分区分片：**纯事件分片**（本块触及各
  分区：身份键〔net id 或 (inst, pin)〕→ 块内事件序列 + 计数 delta）
  ——**不含统计**（2026-10-13 裁定：统计挪到 T3 合并后基于完整序列
  计算，T2 无统计职责）。
- 变量表 temp 对象（每文件一对象，块任务经 db 按引用共享读取——
  2026-10-13 裁定）。
- 块数清单对象与合并链编排消费（依赖全部清单齐备）同 timing db
  §5.3 模式。

## 6. 建库流程（直接任务链 + 三段式，同 timing db §6 全先例）

```
build_waveform_db（master，O(1) 提交，三段式）：
  ① 轻量参数预处理（schema 校验 + 文件存在性 + settings 结构 +
     settings-only 模式判定 + 建库 + alpha 写入；不读文件内容）
  ② 提交唯一 flow 根任务 _waveform_flow_task
  ③ 顶层提交 freeze（依赖固定标记 summary + 全部分区对象标记）

_waveform_flow_task（根任务；依赖 design db 必要对象锚——层级树/
  DSBlockNames 全集/id_partition_map INST 段表/分区表/pg_nets/各分区
  NETS，跨 db 直读同 timing db §21 模式，零快照搬运）：
  VCD 模式：
    绑定校验（块绑定目标存在性，条目级兜底 WFMG::0011）
    → design db 枚举锚
    → 每有效文件一个文件入口任务 _wf_file_entry_task
    → 合并链编排任务 _wf_plan_merge_chain_task（依赖全部块数清单对象；
       体内提交：每分区合并 → 汇总）
  settings-only 模式：
    design db 分区表枚举 → 直接提交每分区一物化任务（空
    WFPartitionWaveform + overrides 换算物化 WFPartitionActivity〔net
    名覆盖经形态 A 推导展开〕）→ 汇总任务（settings 元数据）

_wf_file_entry_task（每文件一任务）：
  VCD 嗅探（gzip 魔数 → 解压到 db 临时文件再进常规链——2026-10-13
  裁定 gzip 支持，临时文件随 freeze 清理责任单点；头关键字形态
  校验——形态错任务失败透出）→ 头部解析 → WFVarTable temp 对象 →
  切块扫描 wf_plan_file_chunks（**纯字节切块**：#时间戳行边界 +
  头段前缀区间 + 首块起点含 $dumpvars 段〔修复初值事件丢失〕+
  时间档位字段——不维护信号当前值表、不产出任何快照〔快照已删，
  2026-10-13 裁定〕）→ 体内提交逐块解析任务 + 块数写清单对象

T2 逐块解析任务 wf_convert_chunk（每块一任务，全并行；inputs 锚
  design db 对象全名 + 本文件 var 表 temp 对象 + 块计划）：
  逐行解析事件（向量拆位、$dumpoff/$dumpon 处理、real/event 跳过
  计数；**不做值等价过滤、不做统计**——均挪 T3）→ 名字换算
  （§7 两形态 + 轨道归并 §7.2 + 块绑定 + strip_prefix）→ 波形身份
  键 + 引脚挂载集合 → 分区路由（INST 段表逐实例 primary 分区 +
  **引脚身份波形按 §8 复制范围路由**）→ 各分区 WFEntrySlice
  （纯事件分片）

T3 每分区一合并任务 wf_merge_partition（每分区一任务）：
  ① 波形拼接：同身份键分片按文件序/时间序拼接（多文件偏移对齐
     §2.1）→ 完整事件序列
  ② 等值过滤：完整序列上统一丢弃与前值相同的重复行（首事件不过滤）
  ③ 统计计算（2026-10-13 裁定：合并后进行——块界概念于统计中
     不存在，无任何缝隙）：序列初值取 $dumpvars（或轨道首事件），
     四态时长 = 前导段〔t_start 至首事件，归属初值〕+ 相邻事件
     间隔 + 尾段〔末事件至 t_end，归属末态〕；rise/fall 沿完整
     序列严格计数（仅 0→1 / 1→0）
  ④ 四级回落解析（§7.3）：本区每引脚按优先级定最终 waveform_id
     （本区波形实体集合 + design db NETS 连接表——本地可解）
  ⑤ waveform_id 分配（身份键全序——跨构建可复现）+ 统计条目绑
     最终引用
  时间重叠段：WFMG::0006/0007 观测计数（场景 a 偏移对齐非单调段 /
   场景 b 重合覆盖——§2.1 已裁定）
  混合模式（2026-10-13 裁定）：VCD 输入 + settings.activity.overrides
   并存时，回落与统计完成后对命中条目替换统计值（波形引用保留），
   覆盖计数入 summary
  产出 PART_{xp}_{yp}.WAVEFORM + PART_{xp}_{yp}.ACTIVITY

T4 汇总任务 wf_merge_summary：逐文件统计归并（多文件 DURATION 直和）
  + 全局计数 + settings 元数据 → 写 summary（freeze 标记）
```

- **单块失败兜底**：块语法破损 → 跳过 + 计数，空分片照常产出（同
  timing db 范式 (b)）；**全部文件失败** → fatal 消息码 80（范式 (a)）。
- 可 raise 场景仅两类（同 timing db）：文件不可读——预处理同步拦截；
  文件形态错（嗅探不通过）——文件入口任务失败透出。

## 7. 名字换算（解析边界一次完成，flow 内 C++；裁定 3 两形态）

| 优先序 | VCD 名字形态 | 换算路径 |
|--------|-------------|---------|
| ① net 名（行业主形态） | scope 全路径名 | `DSNameMapperT(net)` → net global id → 分区 NETS 对象该网连接条目 → 挂全部连接位（driver + receiver）：**事件侧去重为一份波形（身份键 = net id）+ 全部连接位引脚各一条统计引用同一 waveform_id**（裁定 4 与 timing db 裁定 4「多驱动挂全部 driver 位」的推广——receiver 位输入翻转是 power db 输入引脚被动功耗查表与动态电流合成的必需输入）；无连接条目（悬空/仅端口）→ 端口位跳过 + 计数 |
| ② inst pin 名（实践直配形态，裁定 3 新增） | scope 路径 = 实例层级路径、var 名 = pin 名（①未命中时拆最末段尝试） | `DSNameMapperT(instance)` → inst id；pin 名 → 全局 pin 名 id（单哈希查，无 cell 组合键——2026-09-16 命名裁定）；所属 cell 的 pin 存在性校验（同 timing db）。身份键 = (inst_id, pin_id)（单引脚独享波形） |
| 双未命中 | — | 跳过 + 计数（§9 消息族） |

### 7.2 轨道身份归并（同标识码双名——网身份优先，2026-10-13 裁定）

VCD 头部可能对**同一物理信号**登记两个名字且复用同一标识码（典型：
网名 `n_answer` 与其驱动单元端口名 `u_and1/ZN` 共用 id 码）——事件段
该信号只记录一份值变化序列。归并规则：**同一标识码轨道的多个名字
中有网名且网名匹配命中（形态①）时，整条轨道统一立网身份**（一份
波形实体，挂该网全部连接位）；轨道内其它名字（实例引脚名）不再
另立身份、直接挂网身份引用。仅当轨道无可命中的网名时才按实例引脚
身份立波形（形态②）。该规则消除同轨道双名的重复波形实体。边界说
明：同轨道多名**均为网名且各自命中不同网**的极端病态形态（同一 id
码被两个无关网名复用）下各自立波形实体（轨道统一身份只约束引脚名
分支），现状无害放行——两网连接位各自可见该轨道波形，与「整轨道统
一身份」精神存在出入，特此声明（评审 Suggestion 8）。

### 7.3 引脚波形四级回落（不同标识码、波形不一致场景，2026-10-13 裁定）

VCD 中某网有网名轨道（id₁），同时网内某实例引脚另有引脚名轨道
（id₂ ≠ id₁，两份波形内容可能不一致——不同转储源/延迟仿真形态）。
每个引脚的**最终波形引用**按以下优先级解析：

1. **引脚自身轨道**（该 (inst, pin) 名字直接命中的波形）；
2. 自身无轨道 → **所连网的网名轨道**波形；
3. 自身与网均无 → **同网 output 引脚的轨道**波形（多个 output 引脚
   都有波形时取网连接表条目序**第一个**——用户确认推定）；
4. 均无 → **网连接表中第一个有波形的引脚**（按 design db 连接表
   条目序扫描，不区分引脚方向——用户确认推定）的波形；
5. 四级全无 → 无波形（waveform_id 哨兵，统计全零——未覆盖信号，
   消费侧走 default 元数据）。

语义本质：精确覆盖粗略（引脚实测 > 网级合并值 > 驱动源 > 网上任意
可用源）。**统计随最终引用的波形计算**；回落解析在 T3 合并任务内
本地完成（本区波形实体集合 + design db NETS 连接表即可判定，前置
条件 = §8 引脚身份波形的分区复制范围保证候选波形实体必在本区）。

- **块绑定三形态 + strip_prefix**：逐条复用 timing db §7.1（block_inst
  local 名空间 + 偏移换算）/§7.2（block_cell 定义级建库期复制）/
  §7.4（strip_prefix 段级精确剥离）设计——VCD 的 scope 层级与 TWF
  层级路径同构，组件（DSNameMapper / DSBlockNames / 层级树区间表）
  全部现成。
- **向量位级匹配**（裁定 8 同意）：var 拆位后的位名（`scope/Count[2]`）
  优先按完整位名走 ①/②；design db pin 名无位下标（bus pin 单名）
  时该 var 全部位并入同 pin（统计各位直和，事件挂同 pin——同 var
  的全部位共享一份波形，位事件并入同一序列）。位宽失配（design pin
  集合与 var 位集合不一致）→ 计数观测不 fatal（WFMG::0013）。
- pg 网条目（VDD/VSS 等全局 pg 网名命中）→ 跳过 + 计数（同 timing
  db §7.5；VCD 通常不转储 pg，防御性口径）。
- 未放置实例（design db 无 primary 分区副本）→ 跳过 + 计数。

## 8. 分区路由与对象布局

- 路由键 = 实例全局 id → `id_partition_map.INST` 段表 → (xp, yp)。
- 归属 = **primary 恰一**（活动与事件均挂实例，点数据语义，同 timing
  db 裁定）。
- **波形实体的分区复制范围**（2026-10-13 裁定扩展）：
  - 网身份波形 → 该网全部连接位实例所在分区（既有规则）；
  - 引脚身份波形 → **自身实例所在分区 + 所连网全部连接位实例所在
    分区**（§7.3 四级回落 3/4 级可能被同网其它引脚引用——每区自足
    原则，回落本地可解）。
- 对象布局（同一 (xp, yp) 网格坐标系）：`PART_{xp}_{yp}.ACTIVITY` +
  `PART_{xp}_{yp}.WAVEFORM`（**双对象**——裁定：双容器；统计消费者
  不加载事件大对象）+ 全局 `summary` / `alpha_settings`。
- settings-only 模式：每分区两对象照常产出（WAVEFORM 恒空、ACTIVITY
  装 overrides 物化条目、waveform_id 全哨兵）——消费接口恒定。

## 9. 消息族（前缀 WFMG——第二轮裁定 1）与异常语义

| 消息 | 级别 | 场景 |
|------|------|------|
| WFMG::0001 | warn | net 名未匹配（①形态跳过 + 计数，一次汇总） |
| WFMG::0002 | warn | 实例/引脚名未匹配（②形态双未命中） |
| WFMG::0003 | warn | 网条目无连接位/悬空（含 design db NETS 无此网——未布线设计合法兜底，同 TIMG::0003 口径） |
| WFMG::0004 | warn | 全部输入解析成功但 0 有效条目（空结果放行） |
| WFMG::0006 | warn | 多文件实例引脚重合按文件先后顺序覆盖（场景 b/混合——后文件替换先文件，§2.1 已裁定；块绑定文件引脚集合理论不相交，重合即输入异常观测） |
| WFMG::0007 | warn | 场景 a 时间轴观测（文件间时间重叠/回退段计数——偏移对齐后仍非单调的段；主语义 = 时间序衔接直和） |
| WFMG::0008 | warn | 未放置实例跳过 |
| WFMG::0009 | fatal(80) | 全部文件解析失败（流程级范式 (a)） |
| WFMG::0010 | warn | strip_prefix 未命中条目跳过（含剥后余空） |
| WFMG::0011 | error | 绑定目标未命中（block_inst/block_cell；条目级兜底跳过该文件，仅全部跳过才失败——同 TIMG::0011） |
| WFMG::0012 | warn | 不支持构造观测（real/event 类型 var、EVCD 扩展记录；跳过 + 计数） |
| WFMG::0013 | warn | 向量位宽失配观测（design pin 集合与 var 位集合不一致，不 fatal） |

- C++ 侧同族计数器经 WFSummary 落库，消息由 flow 侧读汇总一次发出。

## 10. 模块结构（dev-rules 三段式 + 六文件；简写与类名**统一 WF**——第二轮裁定 2）

```
src/emir/waveform/
├── cpp/            wf_types.h/.cpp + wf_parser.h/.cpp（VCD 解析器：
│                   wf_parse_vcd_file / wf_parse_vcd_text；**时间档位/
│                   标量值/转储参数词法提取至公共头单点维护**——
│                   2026-10-13 裁定 IV-5，块扫描器与解析器共用）
│                   + wf_partition.h/.cpp（换算/路由/合并/物化：
│                   wf_plan_file_chunks / wf_convert_chunk /
│                   wf_merge_partition / wf_merge_summary）
├── export/         nanobind 绑定（_fly_emir_waveform.so；FLY_EXPORT_*；
│                   EXWF* 导出类名）
├── py/             wf_db.py / wf_flow.py / wf_register_msg.py /
│                   wf_utils.py / wf_functions.py / alpha_settings.py
└── tests/          wf_parser_test.cpp + flow error/structure .py
```

- 模块简写 **WF**（类名 `WFxxx`〔WFPartitionWaveform / WFPinActivity /
  WFSummary...〕、独立函数 `wf_动词` 、Python 绑定导出类 `EXWF*`、
  `_fly_emir_waveform.so`）；VCD/SW 两旧简写随合并撤销（dev-rules §5
  简写表同步）。
- 注册接入：emir 聚合 + main.cpp `import _fly_emir_waveform` + fly.sh
  install 循环（AGENTS.md 新模块六步）。

## 11. 测试规划（测试矩阵——2026-10-12 主 agent 设计，随实施细化）

**测试数据生成方式（用户裁定）**：使用开源仿真器 **Icarus Verilog**
（`iverilog` + `vvp`，`$dumpfile`/`$dumpvars` 产标准 VCD）编写结构化
Verilog 设计仿真生成**真实 VCD**——Verilog 网表的实例名/线网名与
design db 测试设计的 DEF 名对齐（门级网表风格），保证 VCD 信号名可
匹配。手写样张仅补特征覆盖（四态/边界构造/破损流）。

### 11.1 测试数据集（`qa/emir/data/waveform/`，生成脚本与 Verilog 源同存）

| 数据 | 生成方式 | 覆盖特征 |
|------|---------|---------|
| `small.vcd` | iverilog 仿真小设计（计数器 + 组合逻辑 + 向量总线 + 两分频时钟） | 基础全断言载体：翻转数/概率可手工推导精确断言；net 名匹配主形态；向量拆位 |
| `part_a1.vcd` + `part_a2.vcd` | 同设计分段仿真（两次 dump，时间轴衔接） | 场景 a 多文件：统计直和 + 事件偏移拼接 |
| `block_b1.vcd` + `block_b2.vcd` | 分块转储（scope 为块内局部名；含同实例引脚重合构造） | 场景 b 块绑定 + 重合覆盖语义（后文件替换） |
| `xz.vcd` | 仿真初始未知段（未初始化寄存器释放前）+ `force` 未知态 | x/z 时长统计（TX/TZ 分列）+ x 参与转换不计 |
| `instpin.vcd` | dump 指定层级引脚（`$dumpvars(1, u_inst)` 形态） | 场景 ②inst pin 直配（net 名未命中路径） |
| `big.vcd` | 计数器/寄存器阵列大规模设计 | 万级实例 + 分区路由（下限断言口径同 timing db 裁定 8） |
| 手写样张（`src/emir/waveform/tests/data/`） | 直接文本 | 四态全构造、位区间、`$dumpoff`/`$dumpon`、real/event 跳过、重复时间戳、破损流（fatal）、gzip 魔数、书籍 Fig 4.9 样张移植 |

### 11.2 C++ 单测

- **`wf_parser_test.cpp`**（解析边界）：头部全构造（timescale 各档位/
  scope 嵌套/var 类型/位区间/id 码复用）；事件段（标量/向量最高位在
  前/real 跳过计数/x/z/$dumpoff 全 x/$dumpon 恢复/$dumpall）；时间戳
  单调/重复；流级破损 raise；时间戳×档位溢出防御；**$dumpvars 先于
  首时间戳形态——首块起点含初始值段、初值事件不丢失（2026-10-13
  裁定修复项，须断言事件非仅快照）**；**多位向量「全位同值」简写行
  的等价判定覆盖全位（构造 [x,0] 后施 x 简写行——2026-10-13 裁定
  修复项）**；块边界切块正确性（纯字节切块区间 + 首块起点 + 档位
  字段）；真实 VCD 样张回归。
- **`wf_partition_test.cpp`**（换算/路由/合并/物化）：
  - 名字换算两形态（net 命中→全部连接位共享同一 waveform_id；inst
    pin 直配；双未命中计数）；
  - **轨道身份归并**（同标识码双名统一立网身份——引脚名不另立
    实体、挂网身份引用）；
  - **四级回落**（构造双轨道波形不一致的网：四级各级命中 + 多
    output 取连接表第一 + 四级全无哨兵；回落目标波形在本区可引用
    〔引脚身份波形复制范围〕）；
  - 块绑定三形态 + strip_prefix（数据名对齐 timing db 测试先例）；
  - 向量位级匹配（位名后缀完整匹配 / 总线 pin 单名各位直和并入 /
    位宽失配观测不 fatal）；
  - **合并后统计口径**（完整序列：前导/事件间隔/尾段四态时长覆盖
    [t_start, t_end] 全程；多块拼接与整读单块统计一致；重复行合并
    阶段过滤）；
  - 合并：场景 a 直和、场景 b 覆盖、波形 id 分配序（身份键全序）；
  - settings-only 物化（空波形容器 + 哨兵 waveform_id + overrides
    展开〔net 名覆盖经形态 A 推导〕+ default 元数据）。

### 11.3 QA e2e（`qa/emir/test_emir_waveform_flow.py` 等）

| case | 断言要点 |
|------|---------|
| 基础建库（small.vcd + design db） | 双容器产出；统计精确断言（翻转数/时长与仿真激励手工推导一致）；**波形去重**（同网连接位共享 waveform_id、事件不复制）；分区路由与 design 分区一致；空分区空对象 |
| 轨道归并与四级回落 | 同标识码双名归并为网身份（无重复实体）；双轨道波形不一致构造下四级回落逐级命中断言 + 多输出引脚取连接表第一 + 四级全无哨兵（2026-10-13 裁定新增） |
| 多文件场景 a（part_a1+a2） | 统计直和 == 两文件单独建库后手工相加；事件时间轴单调；summary 逐文件统计 |
| 多文件场景 b（block_b1+b2 重合） | 后文件覆盖先文件（统计与波形均为后文件值）+ WFMG::0006 计数 |
| x/z 统计（xz.vcd） | TX/TZ 时长精确断言；rise/fall 不含 x 参与转换；概率两口径换算（消费侧 API 验证） |
| inst pin 直配（instpin.vcd） | 形态 ② 命中路径 + 统计挂 (inst, pin) |
| settings-only 模式 | 无 VCD：每分区 WAVEFORM 空对象 + ACTIVITY 物化 overrides + 全哨兵 waveform_id + default 元数据落 summary |
| 大规模（big.vcd） | 万级实例 + 分区数下限断言；建库完成 + summary 覆盖率合理 |
| 错误流 | 破损 VCD → 文件入口任务失败不冻结；`vcd_files=None` 且无 settings.activity → 入口报错；全部文件失败 → fatal 码 80 |
| gzip 压缩输入 | small.vcd.gz 建库全链断言（解压临时文件路径 + 统计与明文版逐项一致）（2026-10-13 裁定新增） |
| 混合模式 | small.vcd + overrides 部分覆盖：覆盖条目统计值 == 用户值换算、未覆盖条目 == 实测值、waveform_id 保留、summary 覆盖计数（2026-10-13 裁定新增） |
| 确定性 | 同输入双跑产物逐字节一致 |

## 12. 演进路径（不在首期）

- **活动传播引擎**（裁定 6 明确首期不做，列未来规划：触发器输出 ≤
  时钟翻转一半、组合输出概率按布尔函数传播〔四门公式表调研已备〕、
  门控调制、缓冲器直传/反相器取补——产出形态与本库统计容器同构）；
- SAIF（Switching Activity Interchange Format）输入格式；
- FSDB 等其他压缩波形格式（gzip 已支持——2026-10-13 裁定提前实施）；
- **多场景输入**：§2.1 场景 c（多激励）**明确不支持**（2026-10-13
  裁定，非首版问题；如未来确需多场景分析另行立项独立输入面）；
- 波形容器全局时间索引（跨波形时间窗扫描加速——动态电流合成期按
  需建）；
- x/z 重分配消费口径扩展；毛刺口径（TG/IG）；EVCD 支持。
  （原「形态②别名身份波形实体归并」演进项已转为正式功能 §7.2
  ——2026-10-13 裁定；原「块首快照过滤」优化项随快照删除而消失。）

## 13. 裁定记录

### I. 2026-10-12 用户裁定（调研六裁定点 + 结构裁定）

1. **全量入库**——事件（波形容器）+ 统计（活动容器）都落库；
2. **统计口径 = SAIF 等价原始量**——{T0, T1, TX, TZ, rise_count,
   fall_count} + DURATION 分列存储，率与概率由消费侧推导；
3. **挂 (instance id, pin id)**——VCD 给 net 名亦推导至实例引脚
   （连接位展开）；同时支持直接给实例引脚波形的输入形态；
4. **名字换算复用 timing db 先例**；
5. **四态时长单独统计**——x/z 默认归入概率分母，settings 控制；
6. **活动传播引擎首期不做**，列未来规划；
- **结构裁定**：⑧ vcd db 与 ⑨ switching db 合并更名 **waveform db**；
  双容器；settings-only 模式逐分区对象照常产出、波形容器空。

### II. 2026-10-12 用户裁定（方案第一轮待裁定点）

1. **消息前缀 WFMG**——确认；
2. **模块简写与类名统一 WF**（含函数 wf_ 前缀、EXWF* 导出名、
   `_fly_emir_waveform.so`）——确认；
3. **双容器 = 双对象**（`PART.ACTIVITY` + `PART.WAVEFORM` 独立对象、
   独立加载）——确认；
4. **波形数据去重**：相同的波形数据统一保存——按分区给独立波形 id，
   实例引脚级别直接保存此 id 引用，避免冗余数据（§5.2/§7 落实：
   同网全部连接位共享一份 WFWaveform，统计条目以 waveform_id 引用）；
5. **多文件合并语义**——已裁定（2026-10-12 第三轮，§2.1）：场景 a
   纯路径文件逐文件独立解析 + 合并期按分区重组（事件按文件序偏移
   拼接、统计直和）；场景 b 块绑定文件暂不考虑实例引脚重合，重合时
   **按文件先后顺序数据覆盖**（后优先——与 lib/timing 保留首份相反，
   本库显式特例）+ WFMG::0006 计数；场景 c 多激励不考虑（演进项）；
6. **settings 键形态与物化范围**（§4 键形；overrides 物化 + default
   元数据不物化）——确认；
7. **翻转计数严格口径**：仅 0→1 / 1→0，x 参与的转换不计（当前略过
   x 统计）——确认；
8. **向量位级匹配**（位名完整匹配优先、无位下标总线引脚各位并入
   同引脚直和；位宽失配观测不 fatal）——确认；
9. **内部时间单位统一 fs int64**——确认；
10. **emir-data-flow.md 库编号全重排**（13 库 → 12 库，⑨ 空缺不留、
    后续库编号前移；流程图/API 表/简写表随 §14 同步）——确认。

### IV. 2026-10-13 用户裁定（遗留问题处置轮）

1. **gzip 压缩 VCD 支持**（由演进项提前实施；§2 更新：魔数嗅探后
   解压到 db 临时文件再进常规切块链）；
2. **混合模式直接支持**（VCD 与 settings.activity.overrides 并存，
   用户覆盖优先——§4 混合模式段：统计值建库期替换、波形引用保留、
   default 在 VCD 模式不生效）；
3. **多激励多场景明确不支持**（措辞修正：非「首版不支持」——不在
   支持范围，未来确需则另行立项独立输入面）；
4. **变量声明表写入 db**：var 表落 waveform db temp 对象，块任务经
   db 按引用共享读取，不再随任务参数逐块复制（§5.1 更新）；
5. **词法双份提取公共化**（时间档位/标量值/转储参数词法单点维护）；
6. **初值事件丢失必须修复**（`$dumpvars` 先于首个时间戳的形态——
   不可丢弃，首块起点须含初始值段）；
7. **标量速记过滤必须修复**（多位向量「全位同值」简写行的等价判断
   不可只查位 0——不可忽略）。

**同轮答疑确认（未构成新裁定）**：形态②别名身份冗余的成因（VCD 同
标识码双名共享值变化序列、换算层双身份各存一份）——演进项修法已列
§12；分块统计块界缝隙（时长归属缺口，事件无损）——**修复与否待裁
定**（修法：块统计窗口改「前块末时间戳至本块末时间戳」）。

### V. 2026-10-13 用户裁定（遗留问题处置轮·答疑后落定）

1. **轨道身份归并——网身份优先**（§7.2 新增）：同一标识码轨道的
   多名中有网名命中时整轨道统一立网身份，引脚名不另立实体（消除
   同轨道双名重复波形实体；原 §12 演进项转正式功能）；
2. **统计与等值过滤挪至合并阶段**（§6 T3 重排）：T2 产出纯事件
   分片（无统计无过滤）；T3 拼接完整序列后统一做等值过滤与统计
   （前导 + 事件间隔 + 尾段覆盖全程）——块界概念于统计中消失，
   「块界缝隙」问题连同其待裁定状态一并作废；
3. **块首快照删除**（§5.1）：统计与过滤均挪合并后，快照两用途
   消失；T1 退化为纯字节切块；「按可匹配 var 过滤快照」优化项
   随之不存在；
4. **引脚波形四级回落**（§7.3 新增）：引脚自身轨道 → 所连网轨道
   → 同网输出引脚轨道（多个取连接表第一）→ 连接表第一个有波形
   引脚；四级全无 = 哨兵。统计随最终引用波形计算。两个推定经
   用户确认：多输出取连接表序第一个；「第一个」按 design db
   连接表条目序不区分方向；
5. **引脚身份波形分区复制范围扩大**（§8）：自身实例分区 + 所连
   网全部连接位分区——回落本地可解、每区自足；
6. **变量声明表写入 db**（维持裁定 IV-4，编号纠正后确认有效：
   「直接忽略」实指 real 类型）；
7. **real 类型直接忽略**（现状维持：跳过 + 计数——用户编号更正
   后明确）。

## 14. 实施期文档同步项（§13-II-5 裁定后执行）

- [emir-data-flow.md](../emir-data-flow.md)：**全重排**——⑧⑨ 合并为
  waveform db、后续库编号前移（power/current/analysis/em），流程图 +
  §3 表 + §5 API 表（`build_power_db` 显式参数 switching_db →
  waveform_db）+ §4 裁定补记（合并与改名 + 本方案裁定链）；
- [dev-rules.md](emir/dev-rules.md) §5 简写分配表：VCD/SW 撤销、WF
  新增；
- [DOC_CHANGELOG.md](../DOC_CHANGELOG.md) 条目。
