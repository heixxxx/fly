# design db 业务背景知识（实现无关）

> 本文档沉淀 design db 开发所需的**领域背景与知识**（与具体实现无关），供实现者理解业务语义。
> 实现方案与裁定见 [design-db-plan.md](design-db-plan.md)（第一阶段评审稿）与
> [design-db-phase2-plan.md](design-db-phase2-plan.md)（第二阶段重构方案）。
> 创建：2026-09-10。

---

## 1. EMIR 与 design db 的位置

EMIR（Electro-Migration 与 IR-Drop 分析）工具链的输入是芯片版图数据（LEF/DEF）与库数据（Liberty lib），
产出电流密度/压降分析结果。fly 框架的 EMIR 十三库体系中（见 [emir-data-flow.md](../emir-data-flow.md)），
**design db（③）是全局 id 分配的权威源头**：它把分散的 LEF/DEF/lib 文件解析、整合为带全局唯一 id 体系的设计数据库，
供下游提取（④）、矩阵（⑥）、分析（⑦-⑪）等库消费。

## 2. LEF/DEF 文件格式背景

**LEF（Library Exchange Format）**——库交换格式，描述工艺与单元库：
- **tech lef**：工艺信息——层（LAYER：routing/cut 类型、方向、间距、宽度）、过孔定义（VIA）、布线规则（VIARULE）、单位（UNITS DATABASE MICRONS）等；
- **cell lef**：单元库——每个 MACRO 描述尺寸（SIZE）、原点修正（ORIGIN）、类别（CLASS）、引脚（PIN：方向/用途/图形）、阻挡（OBS）、FOREIGN（对应 GDS 结构名）。

**DEF（Design Exchange Format）**——设计交换格式，描述具体设计：
- 头部：DESIGN 名、UNITS、DIEAREA（die 区域，2 点=矩形、>2 点=轴对齐多边形）、
- COMPONENTS（实例放置：引用 LEF MACRO 名 + 放置坐标 + 朝向 + FIXED/PLACED/COVER 状态）、
- PINS（端口：block 级引脚 + 位置）、NETS/SPECIALNETS（网表连接与路由几何）、VIAS（设计内定义的过孔）。

**单位体系（DBU，database unit）**：LEF/DEF 坐标均为整数 DBU，`UNITS DATABASE MICRONS n` 声明 1µm = n DBU。
不同文件声明值不同（lef 常 2000/4000，def 常 100/200/2000）——fly 裁定 ㉝：**全局统一基准 1000 DBU/µm，各文件按自身声明换算后入库**。

### 坐标类型与取值范围（2026-09-10 裁定：业务图形一律 int32）

- int32 @ 全局基准 1000 DBU/µm 可表达 **±2.15×10⁹ DBU = ±2.15 m**；
- 正常芯片（光刻掩膜极限约 26×33 mm）坐标绝对值 ≤ 3.3×10⁷ DBU，int32 余量约 **65 倍**；
- 若跟随文件原始 unit（可高达 100000）直接存原值，33 mm 边 = 3.3×10⁹ **溢出 int32**——统一基准 1000
  正是 int32 可用的前提；
- **中间量必须 int64/double**：换算路径 `double 原值 × (1000/src_unit) → int64 → 范围校验 → int32 存储`；
  面积等平方量 (3.3×10⁷)² ≈ 1.1×10¹⁵ 超 int32、在 int64 内（GEORect::area_int 的 int64 中间量先例）；
- GEOPoint/GEORect/GEOPolygon/DSShapeRef/DSCell bbox 与 polygon、GEOTransform 的业务实例化统一
  **int32_t**；int64_t/double 实例化为模板保留能力（未来封装级超大场景备用）；
- 业务使用一律写**无后缀别名** GEOPoint/GEORect/GEOPolygon/GEOTransform（geometry 模块提供，
  = GEOPointT/GEORectT/GEOPolygonT/GEOTransformT 模板的 int32 业务默认实例化；模板类 T 后缀命名
  + 无后缀业务别名的规则见 DEVELOPMENT_GUIDELINES.md §2.2，位宽切换只改别名定义一处）。

## 3. 核心概念词汇表

| 概念 | 含义 |
|------|------|
| cell（macro） | 单元定义——标准单元/macro/block 的静态描述（尺寸、引脚、图形） |
| pin | cell 的引脚定义；port = block 级引脚（DEF PINS），复用 pin 结构（裁定 ㉙） |
| instance | cell 在设计中的实例化（放置）：引用 cell id + 位置 + 朝向 + 状态 |
| block | 子设计（对应一个子 DEF）作为可实例化的「cell」；block 定义可被多处实例化 |
| block instance | block 的一次实例化——层级树节点；block **定义**是 DAG（共享引用），block **实例**是树 |
| net | 电气连接网（引脚集合 + 路由几何） |
| via / via cell | 过孔定义（层连接图形模板）；可在 tech lef / cell lef / DEF 三处定义 |
| via instance | 过孔的放置实例（专用 id 空间、无 name，仅需 via cell id + 位置，裁定 ⑩） |
| layer / stack | 金属层 / 层堆栈（自底向上顺序、邻接关系）——独立 Stack 类型（裁定 ⑭） |
| fake cell | COMPONENTS 引用了未定义 cell 时的兜底（1×1 矩形、标记 fake，裁定 ⑲/⑳） |
| bbox / polygon | 外接矩形（含左下角坐标的 GEORect） / 真实多边形图形（DIEAREA 双存，裁定 ㉞） |
| namemap | name ↔ id 双向映射（**仅 design db 存在的概念**；模板底座 DSNameMapperT<IdT> + 实体语义别名（32 位组 DSCellNameMapper/DSPinNameMapper/DSViaCellNameMapper、64 位组 DSInstanceNameMapper/DSNetNameMapper）：map name→id + vector id→name 成对、一律双向、空洞容忍；全局挂 DSDesign、per-DEF local 挂 DSBlockBuildData，随宿主序列化；裁定 ②㊱㊲㊳） |

## 4. 放置变换语义（本方案最核心的领域知识）

### 4.1 LEF 宏坐标系

- 宏的唯一坐标系原点 = **宏左下角 (0,0)**，SIZE 矩形从 (0,0) 到 (W,H)；
- **PIN/OBS 几何坐标相对宏 (0,0)**，与 ORIGIN 无关（LEF Reference 5.8 原文 + OpenDB lefin 源码双源确认）；
- **ORIGIN 语句不是几何基准**，是放置对齐修正量：语义 = 「宏先整体平移 +ORIGIN，再对齐 DEF 放置点」，
  即放置参考点在宏坐标系的 **−ORIGIN** 处（无 ORIGIN 时参考点 = (0,0)）；
- ORIGIN 主要用于 FOREIGN（GDS 结构）对齐场景，真实库绝大多数为 (0,0)。

### 4.2 DEF 放置语义（官方 Reference 5.8 + OpenDB 读入源码双源一致）

- `COMPONENTS - instName cellName + (x y) orient` 中的 **(x,y) = 变换后放置边界的左下角**
  （原文「a DEF COMPONENTS placement pt indicates where the lower-left corner of the placement
  bounding rectangle is placed after any possible rotations or flips」）；
- **放置边界（box）= [−origin_, −origin_+(W,H)]**（origin_ = ORIGIN 语句值；无 ORIGIN 时即 SIZE 矩形）；
- 几何不归一化：cell 一切几何按原始坐标存储；placement → instance 存储值的换算只做一次：
  `pos = t − apply_box(orient, box).ll()`（pos = cell 原坐标系 (0,0) 点的全局位置）；
- instance 全局坐标 = `R(orient) · m + pos`（m = cell 原始坐标）。

### 4.3 八种朝向（orient，D4 群）

纯旋转公式（OpenDB dbTransform::apply 同源，已按官方 Reference 5.8 图示矢量级核对）：

| orient | R(x,y) | 说明 |
|--------|--------|------|
| N | (x, y) | 恒等 |
| W | (−y, x) | 逆时针 90° |
| S | (−x, −y) | 180° |
| E | (y, −x) | 逆时针 270° |
| FN | (−x, y) | y 轴镜像 |
| FS | (x, −y) | x 轴镜像 |
| FW | (y, x) | 先 FS 再逆时针 90°（转置） |
| FE | (−y, −x) | 先 FN 再逆时针 90° |

- **命名交叉警示**：官方 Reference 表格的 MX90/MY90（OA 系名）与 OpenDB 源码的 MXR90/MYR90
  同值异名——跨源核对必须锚定数学公式而非名字；代码枚举用 DEF 朝向名（见 4.5）。
- 旋转/镜像后包围盒宽高在 W/E/FW/FE 下交换（换轴）。

### 4.4 嵌套 DEF（block cell，无 LEF 中介）

- DEF 的 COMPONENTS 语法上只能引用 LEF MACRO——「DEF 实例化 DEF」经两条工业路径：
  abstract LEF 中转，或工具内部层级模型（OpenDB dbInst::setBlock）；
- 两条路径共同语义：**block 作为被实例化的 cell 时，放置边界 = 其 diearea 矩形**（归一到 (0,0) 起的语义由 origin_ 表达）；
- fly 裁定（P7）：block cell 与 LEF cell 同构统一——`origin_ = −diearea 左下角`、几何 = 子 DEF 原始坐标、
  `box = [−origin_, −origin_+(W,H)]`（恰为 diearea）；子 DEF 原点不在 diearea 左下角（如 die 中心原点）
  由 origin_ 吸收，无特例；
- **链式复合（可传递性）**：`子 pos_全局 = R(orient_父)·(子原点父坐标) + 父 pos_全局`、
  `子 orient 全局 = compose(orient_父, orient_子)`——纯二元组复合，零修正项，block 嵌套展开代价最小；
- 两张八方向数值图（[design-nested-def-placement.png](design-nested-def-placement.png) /
  [design-lef-origin-placement.png](design-lef-origin-placement.png)）为上述语义的全量数值示例，
  红点 = instance 存储的 pos、绿三角 = DEF 给的放置坐标、蓝/绿框 = 最终图形。

### 4.5 枚举值对齐（fly 裁定）

`GEOOrientation` 枚举值严格对齐 Si2 defi 头文件（src/lefdef/def/include/defiNet.h）的 DEF_ORIENT_*：
**N=0, W=1, S=2, E=3, FN=4, FW=5, FS=6, FE=7**（注意 FW 在 FS 之前）——defin 回调给的 orient 整型
可直接 `static_cast<GEOOrientation>`，零映射零转换。

## 5. id 体系与层级树

- **全局平铺 pin id**：跨 cell 全局单调分配（D1），pin namemap 键 = `cell_name/pin_name`；
- **cell id**：block cell 与 macro cell 同一编号空间（S4 头扫描合成 block cell）；fake cell id = max_cell_id + 唯一值（⑳）；
- **via cell**：独立编号空间、集中权威表；DEF 来源 via 登记名 = `design_name::via_name`（⑫）；
- **instance id**：local id 从 1 起，local 0 = 当前 block 自身占位；global id 0 = top block instance（⑧）；
  net 的 global id 同规则（local id + 起始编号，flatten 时换算，⑨）；
- **层级树（S6）**：以 block instance 为节点（root = top block instance，节点同时存 name 与 id）；
  **编号区间表**是树的组成部分——每个 block instance 分配 instance/net/via instance 三类连续区间
  （深度优先序，区间长度 = 该 block 定义的计数，S5a 产出）；四接口：区间反查（id→所属 block instance）、
  范围查（block instance→id 区间）、parent/直系 children、以 name 打印树（⑮）；
- **DSDesign 容器**（⑬）：全局轻量数据（cell/via cell/lib 关联/层级树/轻量 namemap）统一收纳，
  后续功能以方法增强该类；大体量数据（instance/net 映射、分区产物、密度图）为独立对象。

## 6. 数据组织原则（裁定摘要）

- **表/几何数据不序列化进 cell**（⑰/⑱）：持久化为独立对象，运行时按需加载注入专用字段（指针引用避免拷贝，
  一律禁止裸指针，拥有用智能指针，㉒）；
- **解析阶段每 DEF 数据保存单份**（④）：多实例化在 flatten/分区阶段才展开；
- **异常仅两类可 raise**：文件不可读、语法错误（dev-rules）；其余以 user message 提醒不拦截（如 cell 不 1:1、fake cell）；
- **name mapper 规则**（裁定 ㊱㊲㊳，2026-09-11）：仅 design db 存在（lib db 无 mapper 概念）；
  一律**双向**（单向为缺陷），共享**模板底座 `DSNameMapperT<IdT>`**（design 模块，业务结构
  不进公共模块）+ 实体语义别名（别名不带位宽标识）；**id 位宽按实体数量级分组**——cell/pin/
  via cell/layer id = uint32_t（十万级以内），instance/net/via instance id 与层级树区间 =
  uint64_t（instance 可达 10⁹ 级）；name 存储分层——layer/cell/via cell 双存允许（量少），
  pin 仅 id（跨 cell 同名多），instance/net 仅 id 且内部业务全程以 id 为键（name 仅解析边界
  与用户可见输出经 mapper 转换）；坐标 int32 独立不受影响（物理量 vs 数量）；
  **无效 id 哨兵**（裁定 ㊴）：一律 = id 类型最大值（kInvalidId，get_id 未命中
  返回之；is_valid_id 判定（static 纯值、不依赖 mapper 加载）；有效 id 空间排除 max 值；
  查询接口命名：get_id(name) / get_name(id)——统一 get_ 前缀）；结构形态（裁定 ㊲→㊷→㊸
  演化定稿 + ㊹ 架构终局）：**三层职责**——①Hasher 家族（block 级 local 查询）：
  `DSNameHasherT<IdT>` hash 实现，六实体（DSCellNameHasher/DSPinNameHasher/
  DSLayerNameHasher/DSViaCellNameHasher = <uint32_t>；DSInstanceNameHasher/
  DSNetNameHasher = <uint64_t>，R8 内部 char arena + radix tree 压缩 3-5 倍）；
  ②`DSNameMapperT<IdT>` **全局组装层**（真正的 name mapper：持全部 block hasher 集
  + 层级树；get_global_id(full_hier_name) = 树逐层定位 + 叶层 hasher + 区间 start，
  get_full_name(global_id) 反向拼 hierarchy block name prefix）——**仅服务 instance/net
  两维度**（cell/pin/layer/via cell 的 id 天然全局、无 local 概念，hasher 即完整查询，
  与 DSNameMapper 无关联）——且为**注入式轻壳**（㊻：不保存不序列化 hasher，经
  set_block_hasher 按需注入（**block 标识 = cell id 或 cell name**）、局部注入 = 局部可查；hasher 随 DSBlockNames_<i> 伴生
  对象独立落盘读回注入）；③DSInstanceNameMapper/
  DSNetNameMapper = <uint64_t> 全局 mapper 实例化（按维度注入 hasher 集）；
  **R8 实施加成**（R8a-d 全部落地，2026-09-12）——hasher 内部 = Backend 概念（编译期
  模板策略，替换点 = 六别名单处）+ 64 位组 Hatrie backend（tsl::htrie_map，客户分布
  1.8x 压缩/查询 1.59x）+ 32 位组 Hash backend（不树化）；**序列化双段制**（权威段
  backend 无关名集格式 + htrie 原生加速段经 FLY_SERIALIZE_EXTERNAL 宏桥接，直载
  12.5x 提速、客户 IO 300MB/s-1GB/s 下 ≥1 万名直载恒优）；**LCP 后缀共享**（alpha
  `lcp_name_arena` 默认 false：排序+后缀 arena+checkpoint=64，arena 省 63.7%、随机
  get_name 4.1x 亚微秒、注册序访问 27.8x 劣化须走 for_each_name_by_rank 批量路径）；
  **分派索引**（DSNameMapperT 内建 Hatrie 实例：block 层次路径→树节点 id，剥叶段
  前缀一次 find，get_global_id 1.1-1.5µs 与深度/扇出/规模解耦，p50 提速 2.24-11.82x；
  set_tree 即建、rebuild_dispatch_index 兜底、运行时构件不序列化）；
  **shared_ptr 序列化**（㊾：FLY_FIELD 分派链接 bitsery 原生 ext::StdSmartPtr +
  PointerLinkingContext 会话，同会话保留共享拓扑）——hasher 字段 CMSharedPtr 化
  的框架前提；
- **属性访问宏**：CM_PROPERTY（get/get_ref/get_cref/set/set_move 五件套，成员 `_` 后缀）；
  CM_FLAGS（按位 bool 组：is_xxx/set_xxx/reset_xxx + reset_flags）——见 dev-rules.md；
- **前缀体系**（前缀 = 所属模块的标识，一律**全大写**）：common = CM；design 模块 = DS（同例
  lib 模块 = LIB）；geometry 独立模块 = GEO——规范见 [DEVELOPMENT_GUIDELINES.md](../DEVELOPMENT_GUIDELINES.md)
  Section 2.2 模块类型前缀规范（类型跨模块迁移时前缀随之更换，如 CMGeometryRef 迁入 design 即更名 DSShapeRef）。

## 7. 建库流水线（S1-S10）概览

| 阶段 | 内容 | 状态 |
|------|------|------|
| S1 | lib 库 db 入库 | ✅ |
| S2 | cell lef 并行解析 | ✅ |
| S3 | lib cell ↔ lef cell merge 检测 | ✅ |
| S4/S4b | DEF 头扫描（block/port/DEF via 收集） | ✅ |
| S5a | COMPONENTS 责任链 ∥ 网名扫描 | ✅ |
| S5b | 网内容责任链（分批控内存峰值） | ✅ |
| S6 | 层级树构建 + 起始编号分配 | ✅ |
| S7 | 并查集 port 连接归并（仅 port 相连网、两层树、root = 层级最高/同级最小 global id、悬空 port root=自身 + DSGN::0018 计数；`ds_union.h/.cpp` + `load_design_net_union`） | ✅ |
| S8 | 密度图合并 + 分区决策（core/extend 双区域，非边缘扩 2×最高有效层宽、最外围 int32 极值；通道比重 6:2:2；三键优先级 target_partitions > partition_count > partition_target_density 默认 15 万；block instance bbox 不计局部密度） | ✅ |
| S9 | flatten 展平 + 分区保存（展开任务按 block 定义切分 + 小 DEF 按阈值聚合 + 每分区一合并任务；归属 = 放置点 core 半开区间 primary 恰一 + extend 副本；四类对象 PART_{xp}_{yp}/{GEOMETRY,INSTANCES,INST_CONNECTIONS,NET_CONNECTIONS}——geometry 以 net id 组织（不换算 root）、OBS 入 net 0 + obs 位、非 pg 连接全量补全 / pg 靠 instance 维度拼装；电源引脚预展开 D18；复合变换存树节点使每份 DEF 数据只读一次；`ds_flatten.h/.cpp` + `load_partition`/`iter_design_partition`） | ✅ |
| S10 | 校验 + DSDesign 冻结持久化 | 后续 |

第二阶段重构批次 R1-R9 全部完成（2026-09-12）：R1 geometry 独立模块+GEOTransform、
R2 layer id 化、R3 CM_FLAGS、R4 pin 三字段按 pin 维度、R5 port/block 复用+VIARULE 删除、
R6 transform 接入（place_from_def/DSInstance）、S5a/S5b 责任链、S6 层级树（via 区间依赖消解）、
R7 name 体系终局（六 hasher+DSNameMapperT 注入式轻壳+伴生对象）、R8a-d（hat-trie 选型+
双段制定型+分派索引+LCP alpha）、R9 wait_obj 依赖传播（read_object 类 API 一律 wait_obj
包装、`api.deps(db)` 传播、task 内 `run_direct` 剥离直跑省冗余网络 IO——规范
DEVELOPMENT_GUIDELINES Section 17）。方案与裁定全量见
[design-db-phase2-plan.md](design-db-phase2-plan.md)（㊱-㊿-55）。
见 [design-db-phase2-plan.md](design-db-phase2-plan.md) §5。

**throw 全仓治理（R10，2026-09-12，commit 3d65020）**——解析健壮性语义三条：

- **层引用未定义 = 业务异常兜底**（非格式错误）：几何 rect/wire 段条目级丢弃、
  via 整条放弃 + `skipped_layer_ref_count` 计数 + DSGN::0010 提醒——层表来自
  tech lef（先读已定），引用缺失无后到补全路径，但单条数据错误不作废整个
  建库（与 fake cell DSGN::0007、未定义 via 跳过 DSGN::0008 同族）。
- **DBU 恒基准**（裁定 ㉝）：全局基准恒 1000 DBU/µm（`DSStack::kGlobalDbuPerMicron`），
  不再跟随 tech lef 声明值；LEF 几何为 µm 浮点恒乘基准（与文件 UNITS 声明无关，
  声明仅供工具一致性参考），DEF 坐标按 v × 1000 / def_units 换算。
- **不可恢复结构错误 = fatal message**（dev-rules §7.1 第三类处置）：层级树
  多根/零根/环/产物对齐（DSGN::0011）与 name hasher 权威段损坏（DSGN::0012）
  经 MSG_FATAL_EXIT 以退出码 80 退出 + master 联动 fast_exit——层级树是全部
  全局 id 分配的骨架，其损坏下任何兜底产物不可信；机制详见
  [message-system.md](../message-system.md) §14。

## 8. 外部参考（语义裁决的权威源）

| 资料 | 位置 | 用途 |
|------|------|------|
| LEF/DEF Reference 5.8（官方 PDF） | ISPD 官网 lefdefref.pdf / coriolis 在线版 | 语法与放置语义权威原文 |
| OpenDB（OpenROAD）读入侧源码 | lefin.cpp / definComponent.cpp / dbMaster.cpp / dbTransform.cpp | 工业实现佐证（fly 同为读入方，以 reader 为基准） |
| Si2 lefdef 解析器（魔改 fork） | src/lefdef | fly 的解析底座；DEF_ORIENT_* 枚举即出自 defiNet.h |
| 八方向数值图 | docs/emir/design-*-placement.png | 放置语义全量数值示例（单测期望值来源） |
