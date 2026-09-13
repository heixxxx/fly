# design db（③）立项需求与流程设计（评审稿）

> **定位**：design db（设计数据库，EMIR 13 库中的 ③）立项前的需求整理、流程设计与评审稿——基于原始九条需求描述，结合芯片设计数据形态与 EMIR（电压降 IR 分析 + 电迁移分析）业务流程做补充、串联与扩展。**非终稿，等待审查；裁定通过后条目回迁 [emir-data-flow.md](../emir-data-flow.md) §4。**
> **创建**：2026-09-09；**修订**（同日，用户六条裁定）：① 无业务合并的解析任务不采用 MapReduce——改为「并行任务 + 全局汇总」形态（MapReduceJob 仅用于有合并语义的场景，如 lib 库 db）；② 全实体 name ↔ id 双向映射**全量直接建好**（首版 map + vector，撤销原 D6 建议）；③ DEF 完整解析分两段：第一段并行（COMPONENTS 全量 ∥ 网名扫描建 namemap），第二段分批全量解析网内容（控内存峰值），层级编号分配（S6）前移至两段之间；④ **解析阶段每 DEF 数据保存单份**，block cell 实例化为多个 block instance 只在 S9（flatten 展平 + 分区保存）时处理；⑤ S9 任务组织：以 block 定义（DEF 数据）为展开任务单位（而非按分区发任务——超大 block 横跨多分区时，按分区发任务会导致多个任务重复读同一份超大数据），展开任务产出所涉各分区的分片，**每分区一合并任务**将不同来源分片 merge 为最终分区对象；数据量小且实例化次数少的 DEF 按阈值聚合到同一任务处理以减少任务数。⑥ 密度图固定采样像素统计：每格长宽固定，格子数值 = 格子中出现过的（与其交叠的）图形数量按逐层密度系数加权叠加——低层 layer 电阻更高/线更细/图形更多、对电阻提取负载影响更大，系数自高层向低层逐层递增，第一版全部取 1，代码接口保留系数表输入；权重为**建库时一次性配置，db freeze 后不可变，不存在动态调整权重的需求**（调整即重新建库），密度分类分层保存是数据组织要求而非为运行时调权。⑦ **建库 API 配置参数标准（EMIR 开发标准，落 [dev-rules.md](dev-rules.md) §3 与总流程裁定 18）**：`build_design_db` 统一追加 `settings`（稳定配置 dict）与 `alpha`（未稳定配置 dict，成熟后迁移）——design db 首版全部配置项均未稳定、进 alpha（逐层密度权重即首个 alpha 键）。⑧ instance 编号保留语义：leaf instance 的 local id 从 1 起，local id 0 = 当前 block 自身占位（展开时映射为该 block instance 的 global id）；global id 0 = top cell 实例化出的 top block instance。⑨ S5b 不依赖 S6：per-DEF 解析阶段尚未 flatten，无法得到全局 net id——S5b 产物为 local net id（仅依赖 S5a），net 的 global id 同样按 local id + 起始编号在 flatten（S9）时换算；S6 与 S5b 并行（均仅依赖 S5a），S6 前移无收益亦无必要。⑩ via cell 为独立类型（非 cell 的特殊形态、独立编号空间）；DEF 中解析出的 via 为专用 via instance（独立 id 空间，非 instance），无 name（不入 namemap），仅需 via cell id 与位置（连层关系经 via cell 定义获得）。⑪ via 可定义于 tech lef / cell lef / DEF 三处，DEF 的 via 定义解析为独立阶段（S4b），确保网几何解析（S5b）前 via cell 数据全集就绪。⑫ DEF 来源的 via 登记名 = `design_name::via_name`（避免不同 DEF 同名 via 而实际形状不同导致合并污染——各自独立 via cell id）；**via cell 全集集中保存**为单一权威表，确保后续流程（S5b 引用解析、S9 展开时几何实例化、④ 提取）始终拿到完整 via cell 数据。⑬ **全局轻量数据收纳进独立的 Design 容器类型**（C++ 顶层类，草案名 `DSDesign`，同 lib db 的 LIBLibrary 整合容器模式）：cell、pin、via cell、层表、lib 关联、层级树（含编号区间）等全局且相对轻量的数据统一保存于该类型；**后续许多功能通过增强这个类来完成**（查询、索引、派生数据以方法叠加）。大体量数据（instance/net 的 name 映射、块内原始数据、并查集、分区产物、密度图）不进该容器，为独立对象。⑭ **layer 例外，单独保存为独立的 Stack 类型**（层堆栈结构：层自底向上堆叠顺序、邻接关系、几何属性）——后续 fab 提供的 tech file 解析与 tech db 流程都需要这个独立数据结构（跨 design db / tech db 复用，design db 首个落地）。⑮ **编号区间表保存在 hierarchy tree 结构中**（树的组成部分，不独立成对象），hierarchy tree 提供四类接口：①查询某个 instance/net id 所属的 block instance（区间反查）；②查询某个 block instance 的 instance/net id 范围；③查询某个 block instance 的 parent block instance id 与直系 child block instance id 列表；④打印 design 的 hierarchy tree 结构（**以 name 表示而非 id**——要求 tree node 同时保存 block instance/cell 的 name 与 id）。⑯ **lib cell 结构与 lef cell 结构在正式解析 COMPONENTS 前完成 merge 并检测**（lib cell 与 lef cell 有时不能完全 1:1 对应，提早 merge 检测，发生时及时 user message 提醒用户，不可造成 crash）；**cell 结构始终只有一种**——lib 阶段填来自 lib 的数据、lef 阶段填来自 lef 的数据、merge 时整合进同一 cell 记录。⑰ **cell pin 存储简化（细化并取代 ⑯ 的「双视图两份存法」）**：DSDesign 序列化内容中每个 cell 仅保存**简化 pin 结构**（基础属性：pin 类型 signal/电源地、方向 input/output 等）；**pin 表数据（lib 功耗/时序表）与 pin 几何数据**保存于 cell 的**两个不序列化字段**，持久化为独立对象；读取 DSDesign 后业务方**自由选择**是否额外 load 表数据或几何数据并 set 进 DSDesign 的专用字段；下游获取 cell 一律经 **DSDesign 的 get_cell API**——从容器的专用字段将该 cell 的 pin 表数据与几何数据放入 cell 的专用字段；**相关字段一律为指针引用（ptr），避免拷贝**。⑱ **专用字段一律不可序列化**（含 cell 侧与 DSDesign 容器侧，不进 FLY_SERIALIZE 字段表）——避免 `write_object` 时把运行时注入的数据一并写出；**用户需要什么数据就专门加载什么数据**，design 模块提供专门的 API **统一加载过程**（封装 load + set 两步），方便下游业务获取 DSDesign 数据。⑲ **COMPONENTS 解析遇未定义 cell 时动态创建 fake cell 兜底**：master cell 名不在 S2/S4 表中时，动态创建 fake cell（**标记 cell 属性为 fake**、默认无 pin、图形为**长宽 1 全局最小单位的矩形**），该条 COMPONENTS 记录原地创建 instance、cell id 指向 fake cell id 即可；不 raise 不跳过（user message 提醒），网表与实例清单保持完整。⑳ **fake cell 细化**：名称 = `block_cell_name::cell_name`（引用发生的 block 名前缀，同 via 的 `design_name::via_name` 命名模式）；此时正常 cell 均已创建完毕、id 已固定，fake cell id = **max_cell_id + 简易算法生成的唯一值**（避免并行解析不同 DEF 时产生的 fake cell id 重复；产生概率很小，冲突率不必严格，算法实施期定）；**DSDesign 以单独字段保存 fake cell 集合**，区别于正常 cell。㉑ **图形相关结构保存于独立的 geometry 通用子模块**：Point/Rect/Polygon 模板及几何配套（变换/面积/交叠运算）不属 design 模块内部结构，独立成通用子模块，下游多业务复用（design db、④ 提取、几何分析等）——归属路径与命名前缀见裁定 D28。㉒ **实现期存储形态要求**：具体实现每个结构、每个字段时，仔细分析应使用 ptr 还是直接存对象——目标是**减少 copy** 与**防止不期望的连锁修改**（ptr 共享则一处修改处处可见）；**直接存对象比存 ptr 更快更省内存时（小对象/拷贝便宜/无共享需求）就直接存对象**，否则根据数据使用与搬移情况使用 ptr；**禁止使用裸指针**——业务实现理论上不存在必须使用裸指针的场景（拥有关系用智能指针 CMSharedPtr/CMUniquePtr，观察传参用引用）。㉓ **CM_PROPERTY 宏（公共工具宏，非 container 模块；已实施 2026-09-10）**：`src/common/types/cpp/property_macro.h`，五接口生成（get/get_ref/get_cref/set/set_move），成员 `_` 后缀约束。**㉔-㉜（2026-09-10 第二阶段审阅反馈，方案见 [design-db-phase2-plan.md](design-db-phase2-plan.md)，等待审阅）**：㉔ geometry 升为顶层独立模块 `src/geometry/`（图形处理持续生长）；㉕ CMGeometryRef 含 layer id 属业务结构，迁出公共层为 DSShapeRef；㉖ layer id 化（DSLayer 显式保存 id + stack 按 id 查找接口）；㉗ CM_FLAGS 宏（单 flags_ 按位记录一组 bool，逐 flag is/set/reset + reset_flags）；㉘ Cell/Pin 类型全库唯一（撤销 lib db 的 LIBCell/LIBPin，跨场景填不同数据）；㉙ port 复用 pin、block 复用 cell，CM_FLAGS 标记来源种类（DSPort/DSBlock 删除）；㉚ VIARULE 不单独保存，按描述直接生成形状复用 via cell（DSViaRule 删除）；㉛ DEF 解析业务处理组织为处理链/责任链（instance 数据流经链节点，新功能=加节点）；㉜ DSTransform 结构（anchor=旋转后 origin 点 + 累计 orient；instance 坐标始终存「经过旋转的 origin 点」；公式以 LEF/DEF Reference Manual 核对锚定；使用示例 + 可视化效果图 [design-transform.html](design-transform.html)）。
> **关系**：13 库职责与依赖关系见 [emir-data-flow.md](../emir-data-flow.md)（本文引用其裁定时记作「总流程裁定 N」）；子模块开发规则见 [dev-rules.md](dev-rules.md)；lib 库 db 已按三段式落地，design db 复用同一模式。
> **附属可视化**：[design-db-flow.html](design-db-flow.html)——十阶段流水线、层级树与全局编号、密度图分区、主分区判定规则、并查集归并的交互式总览页（单文件，浏览器直接打开）。

---

## 0. 术语约定

| 术语 | 全称与含义 |
|------|-----------|
| LEF | Library Exchange Format（库交换格式）：描述工艺层信息（tech lef）与单元宏版图抽象（cell lef） |
| DEF | Design Exchange Format（设计交换格式）：描述一个具体设计的布局布线结果 |
| DBU | Database Unit（数据库单位）：LEF/DEF 的整数坐标最小单位，通常 1 微米 = 1000 或 2000 DBU |
| macro（宏单元） | LEF 中描述的单元版图抽象（尺寸/引脚/禁布区），标准单元与大 IP 块统一称 macro |
| block（块，层级子设计） | 一个独立 DEF 对应的子设计；层级设计中每个 block 被父设计的 DEF 引用并放置 |
| block instance（块实例） | block 在父设计中放置一次产生的实例（同一 block 定义可被放置多次） |
| special net（特殊网络） | DEF 中 SPECIALNETS 段的电源/地网络（宽线段，自带线宽），EMIR 首期的主要提取对象（总流程裁定 4） |
| port（端口） | block 对外的引脚，定义于子 block DEF 的 PINS 段；顶层 block 的 port 是整个芯片的对外引脚 |
| orient（朝向） | 实例放置的旋转/翻转组合，共 8 种：N、S、W、E、FN、FS、FW、FE |
| origin（宏原点） | LEF macro 的坐标原点偏移；DEF 放置坐标是 origin 的落点，宏左下角需经 origin 换算 |
| 密度图 | 以固定尺寸正方形网格（bin）统计的布局布线密度分布图，供分区决策 |
| 并查集 | 不相交集集合（union-find）：支持合并（union）与查代表（find）的数据结构 |
| local id / global id | local id 为 block 内局部编号（从 0 起）；global id = 层级起始编号 + local id，全局唯一 |

---

## 1. 定位与输入输出边界

### 1.1 在 13 库全景中的位置

```
第 1 层（基础数据库）：  ① lib 库 db ◄── .lib 文件
                        ③ design db ◄── DEF 文件集 + LEF 文件集（tech lef + cell lef）
                                        ＋ ① lib 库 db（cell 齐全性校验 + cell name → cell id）

第 2 层（左右双轨）：    左端项：④ extraction ◄── ③ 几何 + ② 参数
                                 ⑥ matrix ◄── ④ 分区电阻电容
                        右端项：⑦⑧⑨⑩⑪ ◄── 均依赖 ③ 的 name → id 映射与实例连接

第 3 层（汇合）：        ⑫ analysis ◄── ⑥ + ⑪ + ③ 注入点映射；⑬ em ◄── ⑫ + ⑥ + ②
```

design db 是**全局编号（id）分配的权威源头**（总流程裁定 2/9）与**唯一持有空间几何与层级结构的库**：左端项（电阻提取）消费其几何与分区，右端项（电流构造）消费其编号映射与实例连接，⑫ 分析消费其实例电源引脚位置作注入点。

### 1.2 输入

| 输入 | 形态 | 说明 |
|------|------|------|
| tech lef | 单文件（或多文件取首份） | 工艺层定义：布线层方向/默认线宽/间距/pitch、切割层、通孔定义、通孔规则、单位（DBU）、制造网格 |
| cell lef 文件集 | 多文件 | 单元宏（macro）：尺寸、origin、引脚（含几何与用途）、禁布区 |
| DEF 文件集 | 多文件（层级设计：每 block 一个） | 实例清单（COMPONENTS）、信号网（NETS）、特殊网（SPECIALNETS）、端口（PINS）、设计内通孔定义（VIAS）、芯片外框（DIEAREA）等 |
| ① lib 库 db | 前置数据库（显式传入） | LIBLibrary 整合容器：cell 全集 + 引脚电容 + 能量表 |

### 1.3 输出（design db 内持久化对象）

| 产出 | 内容 | 主要消费者 |
|------|------|-----------|
| **DSDesign 容器（独立 Design 类型，裁定 ⑬/⑭/⑯/⑰/⑳）** | 全局轻量数据统一收纳：cell 表（**始终单一结构：lib 字段 + lef 字段经 S3 merge 整合**；**简化 pin**（基础属性）；**fake cell 单独字段**，裁定 ⑳）、via cell 集中权威表（`design_name::` 前缀命名，裁定 ⑫）、lib 关联、层级树（block 定义 + block instance 树 + 变换链 + **编号区间表**，裁定 ⑮；node 同时存 name 与 id）、轻量 namemap（layer/cell/pin/via cell/block）——**layer 例外单独保存为独立 Stack 类型（裁定 ⑭）**；容器的 pin 表/几何数据**专用字段不序列化**（运行时按需注入，裁定 ⑱）；**get_cell 为下游获取 cell 的统一入口**（指针注入，裁定 ⑰） | 全部下游经 `load_design(db)` + 统一加载 API 取容器；总流程 ④（层宽/通孔）、⑥⑬（电流密度截面积）、⑫ 分析（引脚偏移）、S5b/S9（via 引用与几何实例化） |
| **pin 表数据集 / pin 几何数据集（独立对象，裁定 ⑰/⑱）** | lib 视图表数据（功耗/时序表，按 cell 组织）与 lef 视图几何数据（逐层 pin 几何，按 cell 组织）——**不序列化进 DSDesign**，各自独立对象持久化 | 业务方按需经 design 模块统一加载 API（load + set 封装）注入容器专用字段，get_cell 时指针注入 cell；`write_object` 不写出运行时注入数据 |
| **Stack（独立类型，裁定 ⑭）** | 层堆栈：层自底向上堆叠顺序、邻接关系、几何属性（方向/默认线宽/间距/pitch/最小面积）、layer id ↔ 名映射——**独立保存，不进 DSDesign** | 总流程 ④（层宽）、⑥⑬（电流密度截面积）；**后续 tech db（fab 提供的 tech file 流程）复用同一独立结构** |
| 编号映射（大体量部分） | instance/net 的 name ↔ id 双向映射（键带 block instance 维度，裁定 ②；量级数十 GB，独立对象存储） | ⑦⑧⑨⑩⑪（name → id）、报告输出 |
| 块内原始数据 | 每 block 一份：local instance/net/via instance 表、连接三元组、网几何（含拓扑）、local 密度图、统计 | S9 展开读取（内部）；④ 经分区切分产物消费 |
| 跨块连接归并 | 并查集父表 + 代表表（root）+ root → 成员索引 | ④⑤⑥（物理网聚合）、⑫（电源网整体） |
| 分区表 | (xp, yp) 分区矩形 + 数据 → 分区切分产物（按类型分对象，分片合并后） | ④⑤⑥（逐分区分布式消费） |
| 全局密度图 | 合并后的**分类分层**多通道密度图（合成负载按建库配置的通道比重 + 逐层系数一次性折叠，§4.1）+ 分区决策元数据（含权重 provenance） | 负载观测（db freeze 后定型，不进数值链路；调整权重 = 重新建库） |

**DSDesign 容器边界（裁定 ⑬/⑭/⑯/⑰/⑳）**：判断标准 = 「全局且相对轻量」（十万~百万级、单机内存友好）——cell（单一结构，lib+lef 整合；**简化 pin**；fake cell 单独字段）、via cell、lib 关联、层级树（含编号区间表）、轻量 namemap 入容器；pin 表数据/几何数据为独立对象按需注入（⑰/⑱）；**layer 例外——单独保存为独立 Stack 类型（裁定 ⑭），因 tech db 流程同样需要该结构**；instance/net 的 name 映射、块内原始数据、并查集、分区产物、密度图等大体量或按块/按分区组织的数据为独立对象。**后续功能（查询、索引、派生数据）通过向该类增强方法完成**，不再散建平行对象——同 LIBLibrary 的演进模式（总流程裁定 10）。

**DSDesign 构建与持久化时序（显式化）**：容器实例由各阶段的轻量全局汇总任务**逐步填充字段**——S2 汇总填 cell 表（含简化 pin）→ S3 merge 填 lib 字段与 lib 关联 → S4 汇总填 block cell/port → S4b 汇总 merge via cell 权威表 → S6 填层级树与编号区间表 → S8 填分区矩形表与权重 provenance 元数据（**密度图本体为独立对象，不进容器**）；**S10 冻结前 DSDesign 作为单一对象 `write_object` 一次性持久化**（fill 期间仅在汇总任务链上传递，不重复落盘）。

### 1.4 职责边界（不做什么）

- **不存电气参数**：层电阻/电容/电迁移规则归 ② tech db（工艺厂 tech 文件），design db 只存层**几何**属性；④ 经层名关联 ②（总流程裁定 1/3）。
- **不存电流/功耗/时序**：右端项各库职责（⑦-⑪）。
- **不做寄生提取**：节点打断与电阻网络构建归 ④ extraction db；design db 只保证几何的**拓扑连接性**完整（见裁定 D20 相关细节）。
- **首期只重建电源网络几何**（SPECIALNETS + USE 属性为电源的网），信号网几何范围见裁定 D5；连接关系（网表）则全量保存（右端项必需）。

---

## 2. 数据模型

### 2.1 实体总表（★ = 原始需求已含；☆ = 本文补充）

| 实体 | 键 | 关键属性 | 来源 | 备注 |
|------|----|---------|------|------|
| ★ layer | layer id | 层名、类型（布线/切割）、方向、默认线宽、间距、pitch、最小面积、与上下层的邻接关系 | tech lef | 编号空间独立、数量小（<100） |
| ★ cell（宏单元） | cell id | **始终单一结构（裁定 ⑯）**：lef 字段（尺寸/origin/class/site/引脚索引/禁布区）+ lib 字段（引脚电容、功耗/时序模型引用、电源引脚关联）；**简化 pin 集合**（基础属性，裁定 ⑰）；两个不序列化字段：pin 表数据 / pin 几何数据（指针引用） | cell lef + ① lib db（S3 merge 整合） | **block cell 与 lef macro 同一编号空间**；动态创建的 **fake cell 单独字段保存**（名称 `block_cell_name::cell_name`、id = max_cell_id + 唯一值、无 pin、1×1 最小单位矩形，裁定 ⑲/⑳） |
| ★ cell pin（简化 pin，裁定 ⑰） | pin id | **基础属性**：名、类型（signal/电源地）、方向（input/output/bidirectional）、所属 cell id | cell lef + ① lib db（S3 merge） | DSDesign 序列化内容**仅含简化 pin**；pin 表数据（功耗/时序表）与 pin 几何数据为 cell 的**两个不序列化字段**（独立对象持久化，业务方按需加载，get_cell 指针注入，裁定 ⑰/⑱） |
| ★ port（块端口） | port id（进 pin id 空间） | 同 cell pin + 放置状态 + 所属 block | 子 block DEF 的 PINS 段 | 本质是 block cell 的引脚 |
| ★ via cell（通孔定义） | via cell id | 登记名（DEF 来源带 `design_name::` 前缀，裁定 ⑫）、底层/顶层 layer id、切割矩形集、上下层包围矩形集 | tech lef VIA / cell lef VIA / DEF VIAS（S4b） | **独立类型与独立编号空间，非 cell 特例**（裁定 ⑩）；**全集集中保存为单一权威表**（裁定 ⑫）；生成式通孔规则（VIARULE）见裁定 D13 |
| ☆ via instance（通孔实例） | via instance id | via cell id + 位置（底/顶层经 via cell 定义获得） | DEF 布线/通孔语句 | **专用独立编号空间，非 instance；无 name 不入 namemap**（裁定 ⑩）；local → global 同 instance 方式，S9 展开时换算 |
| ★ instance（实例） | local instance id → global instance id | 所属 cell id、放置状态（PLACED/FIXED/COVER/UNPLACED☆）、位置、朝向、权重☆、所属 block instance | DEF COMPONENTS | 位置为 block 局部坐标 |
| ★ net（网） | local net id → global net id | 名、用途（电源/地/信号）☆、所属 block、连接列表 | DEF NETS / SPECIALNETS | **用途属性是电源网筛选的依据（不可缺）** |
| ★ connection（连接） | 三元组（instance id, pin id, net id） | — | DEF NETS/SPECIALNETS 的引脚列表 | 双索引组织见 §2.3 |
| ★ geometry（图形） | — | layer id + 图形体（点/矩形/多边形） | lef 引脚/禁布区、def 布线展开 | 模板化见 §2.4；**布线段必须保留拓扑连接性☆** |
| ☆ block（块定义） | block id | 对应 DEF 文件路径、DIEAREA（尺寸）、port 集、内部数据引用、统计 | DEF 头部扫描 | 一个 block 定义可被多次实例化 |
| ☆ block instance（块实例） | block instance id | 所属 block 定义、父 block instance、在父中的放置（instance 三元组）、起始编号段（instance/net） | 层级树构建 | 树的节点（见 §2.5） |
| ☆ 密度图 | — | 固定尺寸采样格子网格，分类（金属图形/实例/通孔）分层累计的图形计数 | 解析期统计 | 见 §4 |
| ☆ 并查集表 | net global id → 父 | 路径压缩后的父表 + root 表 | 跨块连接归并 | 见 §2.6 |
| ☆ 单位与坐标换算表 | — | 各 DEF 的 DBU 换算系数、全局 DBU 基准 | UNITS 段 | 见 §8.2（经典坑） |

### 2.2 编号（id）体系

**独立编号空间**（2026-09-09 裁定 ⑩）：layer id、cell id（含 block cell id）、pin id（含 port id）、**via cell id（独立类型与独立编号空间，非 cell 的特殊形态）**、instance id、net id、**via instance id（专用独立编号空间，非 instance）**。其中 layer/cell/pin/via cell 为全局一次分配；instance/net/via instance 为 block 内 local → global。

**编号保留语义（2026-09-09 裁定 ⑧）**：每个 block 的 local instance id 空间中 **local 0 保留给「当前 block 自身」**——块内数据以 local 0 引用本 block 实例化后的 block instance（跨块 port 连接三元组的 inst 端即用 local 0，§2.3/§2.6），展开时 local 0 + 起始编号 = 该 block instance 的 global id；**leaf instance 的 local id 从 1 起**；**global id 0 = top cell 实例化出的 top block instance**（树根，其起始编号 0 + local 0）。

**local → global 换算**（原始需求第 9 条步骤 6 的机制）：

```
global instance id   = 所属 block instance 的 instance 起始编号   + block 内 local instance id
global net id        = 所属 block instance 的 net 起始编号        + block 内 local net id
global via instance id = 所属 block instance 的 via instance 起始编号 + block 内 local via instance id
```

- 起始编号沿层级树自根向下按深度优先序分配（每个 block instance 一段连续区间，区间长度 = 该 block 定义的 instance/net/via instance 计数）。
- **同一 block 定义被实例化 N 次 → 编号段复制 N 份（不复制存储）**：块内原始数据单份保存，展开只发生在分区切分（§5）与编号空间。
- 计数在块解析（S5a）时产生，起始编号在层级树构建（S6）时分配——两阶段解耦，天然支持并行解析时无需协调编号（与总流程「分布式解析按分区预留编号区段」的考量一致，但更优：local id 无需协调，global id 后置统一分配）。
- 编号位宽见裁定 D12。

**name ↔ id 映射（namemap，2026-09-09 裁定 ②/⑩）**：**全部有名实体的双向映射全量直接建好并持久化**——layer/cell/pin/via cell/block 用简单名（via cell 的 DEF 来源名带 `design_name::` 前缀后方为全局唯一简单名，裁定 ⑫），**instance/net 的键必须带 block instance 维度**（同一 block 定义多次实例化时，同一 local 名对应多个 global id，层次全名或等价复合键才唯一，详见 §8.2）；**via instance 无 name（裁定 ⑩），不入 namemap**。首版存储结构：name → id 用 map（哈希表）、id → name 用 vector（下标即 id），随对象序列化入 db；分块、压缩、按需加载等作为**后续持续优化项**（instance 亿级量级下全量映射为数十 GB 级，见 §10——是优化动因，不是裁剪范围的理由）。

### 2.3 connection（连接）的双索引组织

connection 本体是三元组（instance id, pin id, net id），按两种主键各建一份索引（原始需求第 7 条）：

```
InstConnection（instance id 主键）: instance id ──► [(pin id, net id), ...]   // 实例侧聚集：⑩⑪ 逐实例电流
NetConnection（net id 主键）:       net id      ──► [(instance id, pin id), ...] // 网侧聚集：④ 网几何归属、⑫ 注入点
```

补充细节：
- 连接对象还包括「块实例 ↔ 父网」（即 port 连接，见 §2.6）与「顶层引脚连接」（DEF 中 `( PIN portName )` 语法）两种特殊条目，统一入三元组模型——子块内部数据引用本 block 实例用 **local 0 占位**（裁定 ⑧），顶层引脚用保留号。
- 重复连接（同 instance 同 pin 出现在同 net 两次）按抛弃并提醒处理（开发规则 §7 兜底模式）。

### 2.4 geometry（图形）模板设计（归属独立通用子模块，裁定 ㉑）

**图形相关结构保存于独立的 geometry 通用子模块**（Point/Rect/Polygon 模板 + 几何配套运算），不属 design 模块内部结构——下游多业务复用（design db、④ 提取、几何分析等）；归属路径与命名前缀见裁定 D28。三种图形均为类模板，兼容不同基础数据类型（原始需求第 8 条）：

```
Point<T>     { T x, y; }                          // 点
Rect<T>      { T x_low, y_low, x_high, y_high; }  // 矩形：左下（low-left）+ 右上（high-right）
Polygon<T>   { Point<T> points[]; }               // 多边形（顶点环序）
所有图形携带 layer id；实例化类型集合建议：int32_t（DBU 整数坐标，首选）、int64_t（防溢出中间量）、double（浮点扩展）——见裁定 D12
```

必须补充的几何配套（原始需求未显式覆盖，缺则断链）：

| 配套 | 内容 | 用途 |
|------|------|------|
| 布线段拓扑☆ | DEF 布线以路径（path）表达：同一路径语句内的连续线段互相连通、通孔语句连接相邻两层——展开为矩形串时**必须保留「哪些矩形经哪个通孔相连」**，不能降维为平面矩形集合 | ④ 在分支/拐角/交叉处打断建节点的唯一依据 |
| 通孔连层关系☆ | 每个通孔实例 =（via instance id、via cell id、位置），底/顶层经 via cell 定义获得 | ④ 层间电阻支路 |
| 变换（transform）☆ | 朝向 8 种旋转翻转矩阵 + 平移；多级嵌套时复合链；宏 origin 换算 | 层级展开、实例几何/引脚偏移定位（⑫ 注入点） |
| 线宽来源优先级☆ | 网内声明线宽 > 非默认规则（NONDEFAULTRULE）线宽 > tech lef 层默认线宽 | 特殊网每段自带线宽；信号网重建几何必须按此优先级 |
| 面积/交叠运算☆ | 面积、包围盒、与分区矩形的交叠判定（半开区间） | 密度统计、分区归属 |

### 2.5 层级模型：block 与 block instance

```
                     ┌─────────────────────────────┐
                     │ top block instance（根，      │  global id 0（起始编号 0 + local 0）
                     │  instance 起始编号 0          │
                     └──────────┬──────────────────┘
              ┌─────────────────┴─────────────────┐
   ┌──────────▼─────────┐              ┌──────────▼─────────┐
   │ block instance A   │              │ block instance B   │   B 与 A 引用同一个
   │ （引用 block 定义 D1）│              │ （引用 block 定义 D1）│   block 定义 D1（存储单份）
   │ inst 起始 = 100     │              │ inst 起始 = 800     │
   └──────────┬─────────┘              └────────────────────┘
   ┌──────────▼─────────┐
   │ block instance A1  │  ← 树节点是 block instance（每个恰有一个父 → 必为树）
   └──────────┬─────────┘
   ┌──────────▼─────────┐
   │ 叶实例（标准单元/宏）│  global id = 所在块实例起始编号 + local id
   └────────────────────┘
```

关键澄清（原始需求第 6 条的精确化）：
- **block（定义）层面是 DAG**（共享子 block 是常态，同一 block 被多处引用）；**block instance（实例）层面才是树**（每个实例恰有一个父）。层级树以 block instance 为节点，根为顶层 block instance。
- 构树依据：父 DEF 的 COMPONENTS 中，master cell 属于「block cell 集合」（由 §3 的 S4 生成）的实例即块实例，其引用的 block 定义 = 该 block cell 对应的子 DEF。
- 环（A 引用 B、B 引用 A）为非法层级，构建时检测（裁定 D22）。
- block 定义复用是设计常态（一个接口单元 block 放置上百次），**原始数据绝不按实例复制存储**，只在编号空间与分区切分产物中体现多实例。

### 2.6 跨块连接归并（并查集）

原始需求第 9 条步骤 7 的语义展开：

```
top block:     net VDD（global net id = 5）──────────── net CLK（id = 9）
                  ▲        ▲                              ▲
                  │union   │union                         │union   ← union 依据：父网连接了
block A 内:   net VDD(id=50001)  net VDD_A(id=50002)   net CLK_A(id=50003)   子块 port（该
                  ▲                                            ▲             port 又连接子块内部网）
block A1 内:   net VDD(id=500010)                            │
                                                          block A1 的 CLK port

find(500010) = 5   —— 任给 net id 一步查到「最终连接至 top 的 VDD」
root → 成员索引（☆补充）—— 反向枚举：VDD(5) 的全部成员 {5, 50001, 50002, 500010, ...}
```

- union 依据完全来自已有连接数据：父网 NetConnection 中的条目（块实例 id, port pin id, 父网 net id）+ 子网 NetConnection 中的条目（连接该 port 的子网 local net id，其 inst 端为 **local 0**——block 自身占位，裁定 ⑧），两侧经 port pin id 对接。
- 同一子网连接多个 port、且这些 port 在父层连到不同父网 → 两父网亦被 union（电气等价，合法形态）。
- **补充反向索引**（原始需求只有正向 find）：物理网（如 VDD）的全部成员枚举是 ④⑤⑥ 按「物理网」聚合消费的前提——由父表按 root 分组导出并持久化（或按 root 重排存储）。
- 顶层引脚连接（`( PIN portName )`）：顶层网直接挂顶层 port，作为 root 候选；边界条件注入点见 §8.1。

---

## 3. 建库流程：十阶段

### 3.1 流程总图（依赖与并行）

```
S1 tech lef 解析 → layer id（Stack 独立类型，裁定 ⑭）+ tech 级 VIA/VIARULE 定义（串行，快）
 │
 ▼
S2 cell lef 解析（每文件一并行任务，无业务合并）
 │  → 全局汇总任务：cell id / pin id / via cell id 分配 + namemap（map + vector）
 │
 │            ┌─ S4 DEF 头部轻量扫描（每文件一并行任务，无业务合并）
 │            │   → 全局汇总任务：block cell id / port id + namemap
 │            │            │
 │            │            ▼
 │            │  S4b DEF via 定义解析（每 DEF 一任务，裁定 ⑪；可与 S4 同遍回调）
 │            │   → via cell 空间并入（DEF 来源名 design_name::via_name，裁定 ⑫）
 │            │            │
 │            └─ S3 lib cell ↔ lef cell 结构 merge（S5a 前置，裁定 ⑯；
 │                与 S4/S4b 并行）→ 单一 cell 结构整合 + 简化 pin
 │                             │（表/几何数据独立对象，⑰）
 │                             │（非 1:1 对应 → user message 提醒，不 crash）
 └───────────────────────────┴─► S5a DEF 第一段（每 DEF 两任务并行，无业务合并；
                                  依赖 S2/S3/S4/S4b 全部完成——正式解析
                                  components 前 merge 检测与 via 数据均已就绪）：
                                任务一 COMPONENTS 全量 → 实例表（local id 从 1 起，
                                  local 0 = block 自身占位；即产即落盘）
                                任务二 NETS/SPECIALNETS 仅提网名 → net namemap + 计数
                                      │
                     ┌────────────────┴──────────────────┐
                     ▼（均仅依赖 S5a，可并行）             ▼
              S6 层级树构建 + 起始编号分配          S5b DEF 第二段（每 DEF 一任务，
              （依赖 S5a 计数；秒级）              内部分批多阶段；依赖 S5a + S4b：
              树根 = top block instance（id 0）     via cell 数据已就绪；产物为
              编号区间表保存在树结构中（裁定 ⑮）    local net id——per-DEF 解析尚未
                     │                            flatten，无法得全局 id，S6 前移
                     │                            无收益亦无必要，裁定 ⑨）
                     │                                  │
                     └────────────────┬─────────────────┘
                                        ▼
                                  S7 跨块连接归并（并查集） ─┐
                                                           ├（S7 与 S8 并行）
                                  S8 密度图合并 + 分区决策 ─┘
                                        ▼
                                  S9 flatten 展平 + 分区保存（两级任务）：
                                  展开任务按 block 定义切分（小 DEF 按阈值聚合）：
                                  读单份解析数据 → 全部实例位置展开
                                  （instance/net/via instance 三类 local id + 起始编号
                                  = global id，坐标变换复合）→ 分流产出分区分片；
                                  每分区一合并任务：不同来源分片 merge 为最终分区对象
                                        ▼
                                  S10 汇总校验 + 冻结（freeze）
```

> **任务形态裁定（2026-09-09）**：**解析类阶段（S2/S4/S5a/S5b）均无业务合并语义**（产物天然按文件/块分散保存），不采用 MapReduceJob——并行形态 = 独立并行任务产出各自对象 + 轻量全局汇总任务（id 分配 / namemap 构建 / 计数上报，秒级元数据操作）。**S9 例外：分区侧存在真实合并语义**（多个 block 定义的展开分片落到同一分区需 merge），任务形态为「展开（scatter）→ 每分区合并（merge）」两级；MapReduceJob 是否直接适配其多分区多类型输出，实施期按框架能力定（MapReduceJob 面向单一输出容器的合并管道，见 lib 库 db）。

### 3.2 阶段明细

**S1 tech lef 解析 → 层表（Stack 独立类型）+ tech 级通孔定义** —— ✅ 已完成（2026-09-12，随 3588922 落地、3d65020 治理加固）
- 输入 tech lef；产出 layer id 分配 + 层几何属性（方向/默认线宽/间距/pitch/最小面积/类型）+ DBU 基准 + 制造网格，**保存为独立 Stack 类型（层堆栈：自底向上堆叠顺序/邻接关系/几何属性，裁定 ⑭——tech db 流程将复用该结构）**；**tech 级 VIA/VIARULE 定义进 via cell 空间首批条目**（裁定 ⑪：via 可定义于 tech lef / cell lef / DEF 三处，统一入独立 via cell 编号空间）。
- 细节：布线层与切割层统一编号；层的上下邻接关系（stack 顺序）入层表（通孔连层判定用）；**全局 DBU 基准在此确立**，所有 cell lef 的 DBU 必须与之一致（不一致属格式错误，可 raise——裁定 D15）。
- 串行、单任务（层表小）。

**S2 cell lef 解析 → 单元结构（每文件一并行任务 + 全局汇总，无业务合并）** —— ✅ 已完成（2026-09-12，随 3588922 落地、3d65020 治理加固）
- 每文件一并行任务：解析 macro（尺寸/origin/class/site/引脚/禁布区）与 VIA/VIARULE 定义，产出以 name 为键的独立中间结构。
- 随后一次全局汇总任务：统一分配 cell id / pin id / via cell id + 构建双向 namemap（map + vector，2026-09-09 裁定）；跨文件重复 macro 在此暴露——保留首份抛弃后续 + 消息提醒（同总流程裁定 15 语义）。
- 细节：引脚按（方向、用途、逐层几何）全量收录；**用途（USE）属性必须保留**（电源/地引脚是 ⑫ 注入点与电源网筛选依据）；非默认规则（NONDEFAULTRULE）的逐层线宽表入库（网几何重建的线宽来源之一，裁定 D20）。

**S3 lib cell 与 lef cell 结构 merge（S5a 前置；2026-09-09 裁定 ⑯）** —— ✅ 已完成（2026-09-12，随 3588922 落地、3d65020 治理加固）
- 读 lib db 的 LIBLibrary，与 S2 汇总产出的 lef cell 结构**在正式解析 COMPONENTS（S5a）之前完成 merge 并检测**——lib cell 与 lef cell 有时不能完全 1:1 对应（一侧独有 cell、pin 集合不一致等），提早 merge 检测，发生时及时经 user message 提醒用户，**不可造成 crash**（开发规则 §7 兜底模式：提醒不拦截）。
- **cell 结构始终只有一种**：lib 阶段填写来自 lib 的字段（引脚电容、功耗/时序模型引用、电源引脚关联等），lef 阶段填写来自 lef 的字段（尺寸/origin/class/site/禁布区等），merge 将两侧数据整合进**同一 cell 记录**（DSDesign 容器的 cell 表在此成型）。
- **pin 存储简化（裁定 ⑰/⑱，细化 ⑯ 的双视图）**：merge 后每个 cell 的序列化内容仅含**简化 pin**（类型 signal/电源地、方向等基础属性）；lib 表数据（功耗/时序表）与 lef 几何数据（逐层 pin 几何）分别汇入两个**独立对象**持久化——读取后业务方按需经 design 模块统一加载 API 注入容器专用字段，下游经 **get_cell** 以指针注入 cell 的两个不序列化字段（零拷贝；专用字段一律不序列化，`write_object` 不写出）。
- 非对应场景兜底：lef 有 lib 无（EMIR 无电流模型，提醒）/ lib 有 lef 无（未被本次设计文件集覆盖，跳过计数）/ pin 名不对应（逐项提醒）——一律不 crash。
- 依赖：S2 汇总 + lib db；与 S4/S4b 并行；**S5a 等待本阶段完成后才启动**（正式解析 components 前 merge 检测就位）。

**S4 DEF 头部轻量扫描 → block cell / port（每文件一并行任务 + 全局汇总，无业务合并）** —— ✅ 已完成（2026-09-12，随 3588922 落地、3d65020 治理加固）
- 对每个 DEF 做轻量解析：DIEAREA（→ block 尺寸）、PINS 段（→ port 集：名/用途/方向/逐层几何/放置状态）、UNITS（→ DBU 换算系数）、DESIGN 名（→ block 名）。
- 产出：全局汇总任务分配 block cell id（block 名进 cell namemap）+ port id（进 pin namemap）+ block 定义记录（文件路径/尺寸/port 集）。**block cell 与 lef macro 同编号空间**，冲突同保留首份语义；此后 S5a 解析父 DEF 时即可对块实例的 master 统一编号。
- 实现要点：跳过 COMPONENTS/NETS/SPECIALNETS 大段只留头部段——lefdef 已吸收的选择性解析接口（`defrSetSkipComponents` 等 section skip 能力）正为此设计；轻量扫描仍需流过全文件字节（I/O 约束），但跳过大段对象构建，开销大幅降低。
- 来源备选：若层级流程同时提供 block 的抽象 LEF（abstract lef，业界层级流程常见），port 信息可由其获得——两来源策略见裁定 D3。

**S4b DEF via 定义解析（每 DEF 一任务；2026-09-09 裁定 ⑪/⑫）** —— ✅ 已完成（2026-09-12，随 3588922 落地、3d65020 治理加固）
- via 可定义于 tech lef、cell lef 或 DEF（VIAS 段）三处——本阶段解析各 DEF 的 VIAS 段并经全局汇总并入独立 via cell 编号空间 + namemap，**确保 S5b 正式解析网几何前 via cell 数据全集已就绪**（网几何中的通孔引用需可解析）。
- **登记名带来源前缀（裁定 ⑫）**：DEF 来源的 via 登记名 = `<该 DEF 的 DESIGN 名>::<VIAS 段内原名>`——不同 DEF 定义同名 via 而实际形状不同时，各自成为独立 via cell（独立 id、独立几何），**不因同名被合并污染**；同一 DEF 内重名（非法重复定义）保留首份抛弃后续 + 消息提醒。
- tech lef / cell lef 来源的 via 名保持原名（库级全局定义，共享合理；跨 lef 文件同名冲突保留首份 + 消息提醒，同总流程裁定 15 语义）。
- **via cell 全集集中保存**（裁定 ⑫）：全部来源（tech lef + cell lef + 各 DEF）的 via cell 定义汇入 design db 内**单一权威表对象**（不分散在各 block 解析产物中），后续流程（S5b 引用解析、S9 展开时几何实例化、④ 层间电阻）始终从该表取完整数据。
- **汇总机制（显式化）**：per-DEF 解析完成后，由**一个轻量串行全局汇总任务将全部 via cell 定义 merge 回 DSDesign 容器的 via cell 权威表字段**（via cell id 分配 + namemap 同步更新）——虽有合并语义但量级很小（全局数千~数万条、秒级），单任务汇总即可，**不使用 MapReduceJob**（其面向大数据量分布式合并，如 lib 库 db 与 S9 分区合并）。
- 实现上可与 S4 合并为同一遍选择性回调（均为头部区轻量段）或独立并行任务——实施期定，语义不变（S5b 前就绪）。

**S5a DEF 第一段：实例全量解析 ∥ 网名扫描（每 DEF 两任务并行，无业务合并；2026-09-09 裁定）** —— ✅ 已完成（2026-09-12，随 3588922 落地、3d65020 治理加固）
- **任务一（COMPONENTS 全量）**：解析实例清单——master cell id 引用 + 放置状态 + 位置 + 朝向 + 权重；local instance id 按文件内顺序**从 1 起**分配（local 0 = block 自身占位，裁定 ⑧；无需跨任务协调）；跳过 NETS/SPECIALNETS 段；实例计数密度通道在此统计（图形计数口径，§4.1）；实例表对象即产即落盘。
- **任务二（网名扫描）**：NETS/SPECIALNETS **仅提取网名**——lefdef 已吸收的 NetNameOnly 选择性解析正为此设计（跳过路径细节，亦跳过 COMPONENTS 段）；产出网名集合 → local net id 分配 + 网计数上报 → 全局汇总并入 net namemap（map + vector）。跨 block 重名（每块都有 VDD 网名）是正常现象：namemap 键带 block 归属维度（§2.2/§8.2），不视为冲突。
- 两任务各读文件一遍（流式 + 选择性回调，内存峰值低），实例侧与网侧数据正交、无业务合并。
- 引用完整性（裁定 ⑲/⑳）：master cell 名未在 S2/S4 表中 → **动态创建 fake cell 兜底**（名称 `block_cell_name::cell_name`；id = max_cell_id + 简易算法唯一值——正常 cell 此时已全部创建、id 已固定，并行解析不同 DEF 不会重复；产生概率小冲突率不必严格；标记 fake 属性、无 pin、图形 1×1 全局最小单位矩形；DSDesign 单独字段保存区别于正常 cell），该条记录原地创建 instance、cell id 指向 fake cell id——不 raise 不跳过 + user message 提醒，实例清单保持完整；UNPLACED 实例兜底见裁定 D14。

**S5b DEF 第二段：网内容全量分批解析（每 DEF 一任务，内部分批多阶段；依赖 S5a + S4b；2026-09-09 裁定 ③/⑨/⑪）** —— ✅ 已完成（2026-09-12，随 3588922 落地、3d65020 治理加固）
- NETS/SPECIALNETS 全量解析：连接列表、布线路径展开（矩形串 + **拓扑连接性**，线宽来源优先级见 §2.4）、通孔实例（via instance id + via cell id + 位置，连层经 via cell 定义，裁定 ⑩）、金属图形/通孔计数密度通道统计（**逐层分列**，图形计数口径 §4.1）。
- **分批多阶段控内存峰值**：解析器本身流式（峰值与文件体积无关），内存峰值主体是网业务对象的全量积累——按数据量阈值分批（裁定 D24），批内「解析回调积累 → 转换为领域对象 → 写对象入 db → 清缓冲」，单遍流式读取，批界落盘释放；批间并行（需多遍读取）列为优化项。
- **依赖 S5a + S4b，产物为 local net id**（裁定 ⑨/⑪）：per-DEF 解析阶段尚未 flatten，无法得到全局 net id——S6 前移无收益亦无必要；net 的 global id = local id + 起始编号，与 instance/via instance 一致，统一在 S9 flatten 展开时换算。
- 统计：图形计数（网几何/通孔实例）与密度计数总量（S10 守恒校验用）。

**S6 层级树构建 + 起始编号分配** —— ✅ 已完成（2026-09-12，随 3588922 落地、3d65020 治理加固）
- 依赖 S5a 全部完成（instance/net/via instance 计数与放置信息已齐）——**与 S5b 并行**（均仅依赖 S5a，裁定 ⑨）。串行、便宜（元数据级操作）。
- 构树（§2.5）：块实例节点 + 放置变换链（含复合矩阵预计算，int64 中间量）；环检测；根 = 顶层 DEF 对应 block instance（**global id 0**，裁定 ⑧；多根/零根 → 格式错误 raise，裁定 D22）。
- 深度优先序分配每块实例的 instance/net/via instance 三类起始编号；产出编号区间表（block instance id → [start, start+count)）。
- 起始编号统一在 S9 flatten 展开时换算 global id（三类同式）；并行解析任务间无需编号协调（local id 先行、起始编号后置）。

**S7 跨块连接归并（并查集）** —— ✅ 已完成（2026-09-13，按本节裁定补记实施）
- 依赖 S6 与 S5b（跨块 port 连接信息在父块网数据内，S5b 才产出）。可按块分组并行 union（每父块独立合并其子 port），随后全局路径压缩一遍完成。
- 产出：父表（压缩后）+ root 表 + root → 成员索引（§2.6）；悬空 port（未连接任何父网）计数提醒。
- **2026-09-13 裁定补记（实施定稿）**：①**id 域 = global net id**（local + S6 起始编号换算参与 union——同一 block 定义多次实例化的网天然是不同 global id，语义正确）；②**仅 port 相连网参与 union，internal net 不入**（与高层网络无逻辑连接）——规模从全量网降至 port 级（数千级），**单对象存储不分块**（撤销区间分块预案）；③**union 树最终两层**：root = 顶层网 global id、叶子 = 成员网 global id（全路径压缩后 find 恒一步）；root 规范 = 等价类中层级最高（最接近树根）的网、同级取最小 global id——物理网身份锚在 top 层，root 网名即该物理网展示名；④**S9 分区产物不换算 root**——net id 保持 local + offset 形式，后续流程需要最顶层 global id 时自行加载 union 查询换算；⑤悬空 port（root = 自身）照常入表。
- **实施备注（2026-09-13 落地）**：
  - **两级任务形态**（同 S9 两级先例）：block 名清单小任务（读 DSBlockNames_<i> 的 block 名 → def 序号，slice 任务定位子定义网产物用——名字伴生对象轻量，N 次读仅此一遭）→ per-DEF slice 并行任务（读本 def 网产物 + 树 + 本 def 引用的各子定义网产物——经 `ds_net_union_child_indexes` 树扫描定位、只读所需，避免每任务全量重复读）收集 (父网, 子网) 边与本 def port 网 local id 集 → 单汇总任务合并（小规模路径压缩并查集）+ 两层化 + root 规范化（树深度最小优先、同级最小 global id；深度经 `block_of_net` 区间反查 + parent 链上溯，memo 化）+ 悬空计数 → `net_union` 正式对象（slice 临时对象汇总后 remove；freeze final_keys 挂 net_union；与 S8 同级并行，依赖同为 S6 树 + S5b 产物）。
  - **local 0 对接形态（读 ConnectionParseNode 实现确认）**：S5b 连接表为名字形态且**无 local 0 条目**——block 自身占位仅在 instance 表（`init_placeholder`），port 引用按 defi 回调语义 instance_name = `"PIN"`（`DSNetConnection::is_port_ref`）；对接键 = 同一块实例 + 同名 port（父侧 (子实例名, port 名) × 子侧 ("PIN", port 名)，两侧皆字符串，无需 pin id）。
  - **悬空判定**：不在任何边上的非 root 块 port 网（按位逐实例化位置换算 global id）→ 单成员类 root = 自身 + `dangling_count_` 计数 + DSGN::0018（WARN）提醒；root 块 port 网（顶层引脚连接）不入表不入悬空口径；internal net（无 PIN 引用）绝不入表。
  - **结构与 API**：`src/emir/design/cpp/ds_union.h/.cpp`（`DSNetUnion`：root_of_ 成员→root + members_of_ root→成员反向索引（升序含 root 自身）+ find 恒一步/members/class_count；`DSNetUnionSlice` 临时产物）+ 导出面（EXDSNetUnion/EXDSNetUnionSlice + ds_collect_net_union_slice/ds_build_net_union/ds_net_union_child_indexes）+ `load_design_net_union`（R9 wait_obj 形态，ds_functions.py）。
  - **测试**：ds_union_test.cpp 8 用例（局部收集/电气等价 + root 规范/三层嵌套 root=顶层/同定义两次实例化不互并/internal 不入 + 悬空计数/两层不变式含序列化往返/空输入兜底/编排辅助）+ QA S7 段（block_parent.def n_top 增 `( top3 PIN_IN )` 形成真实跨块连接：find/members/悬空 n2/两层不变式）。

**S8 全局密度图合并 + 分区决策** —— ✅ 已完成（2026-09-13 落地；2026-09-12/13 用户裁定定稿）
- 依赖 S5a（实例计数通道）+ S5b（金属/通孔计数通道，逐层分列）+ S6（块实例位置与树）。合并自底向上（树的后续遍历）：块实例的全局密度 = 其子块实例密度图按放置变换平移叠加（格值分摊，裁定 D10）+ 自身 local 密度图；多层嵌套逐级进行。
- 分区决策（§4）：目标分区数（裁定 D11）下按行/列负载前缀和求切线 → (xp, yp) 分区网格；合成负载 = 通道比重与逐层密度系数加权折叠（§4.1，2026-09-09 裁定 ⑥）——两者均为**建库时的一次性配置输入**（建库前调参以影响分区负载均衡；db freeze 后定型，原始需求「自由调整比重」即此建库配置手段）。
- 产出：全局密度图 + 分区矩形表 + 切分元数据。串行决策（全局信息），量小。
- **2026-09-12/13 裁定补记（实施定稿）**：①**分区形态** = 横平竖直矩形网格切分整个 design；每分区 **core_rect**（密度网格切分直接产出）+ **extend_rect**（电阻提取拿完整图形：非边缘方向 core 边界向外扩 **2 × w_eff**，**最外围方向不截断且直接开到 int32 极值**，相邻分区 extend 允许重叠）；w_eff = **最高有效层**（全局合并后金属格值总量 > 0 的 ROUTING 层中，自底向上层表序内位置最高者——即物理最高有效层，非层表登记序的首个有效层）的 default_width，无任何有效层时 w_eff = 0。②**通道比重默认 instance=6 / metal=2 / via=2**；逐层系数接口保留（`DSDensityWeights.layer_factors_`，本期不暴露 alpha 键）。③**alpha 键优先级** target_partitions（'{x}x{y}' 直切）> partition_count > partition_target_density（默认 150000——每分区约 10-20 万 leaf instance）；非法值一律 DSGN::0013 提醒后回退，不 raise。④**block instance 自身 bbox 不计密度**（S5a 前置修正，DSDensityNode 按 cell block_cell 位排除——其密度贡献 = S8 子树叠加，避免双计）。⑤**格值分摊（D10 A）**：块局部格计数值按与全局格交叠面积比例撒入（交叠面积中间量 int64 + 最大余数法保证总量守恒），三通道独立分列叠加。⑥全局格网原点 = 根 DIEAREA 左下角、bin 同 local（alpha density_bin_size）、ceil 覆盖根 DIEAREA bbox。⑦合并实现取后序逐级合并的**线性等价形式**（每 def 局部图按其到根复合变换撒入一次——变换复合结合律保证等价），内存峰值 = 全局图单份。⑧flow 侧 S8 任务依赖只挂 S6 + S5b 产物（S7 未实施不预留挂点）；DSDesign 补 `partitions_` 重写落盘（先 remove 规避 DUPLICATE_SKIPPED），global_density 为独立正式对象（freeze 依赖其就绪，保证重写先于冻结）。

**S9 flatten 展平 + 分区保存（展开任务按 block 定义切分 + 每分区合并任务；2026-09-09 裁定）** —— ✅ 已完成（2026-09-13，按本节裁定补记实施）
- **解析阶段每 DEF 数据保存单份**；block cell 实例化为多个 block instance 的问题**只在本阶段处理**。
- **展开任务以 block 定义（DEF 数据）为单位，不按分区发任务**——若按分区发任务，一个超大规模 block cell（仅实例化一次但横跨非常多分区）会导致多个任务重复读同一份超大数据；按 block 定义发任务则每份 DEF 数据只读一次，展开后生成所涉及各分区的数据，更节省 I/O。
- 展开任务内容：读取该 DEF 的单份解析数据（实例表/网数据/几何/密度统计）→ 收集该 block 定义在层级树上的**全部出现位置**（每个出现位置 = 一个 block instance，携带自根的累计变换链与起始编号段，S6 已备）→ 对每个出现位置做 flatten 展开：**instance/net/via instance 三类的 local id + 起始编号 = global id**（local 0 = block 自身占位 → 该 block instance 的 global id，裁定 ⑧），local 坐标 × 复合变换 = 全局坐标 → 按分区矩形分流产出各分区的**分片**。子 block 的数据由子 block 定义的任务展开（各任务只处理自己 block 的 local 数据）。
- **每分区一合并任务**：将来自不同展开任务的同分区分片 merge 为最终分区对象（按类型分对象，§5.3）——分区侧存在真实合并语义，与解析类阶段的「无业务合并」不同（裁定 ①/⑤）。
- **小 DEF 聚合优化**：数据量小且实例化次数少的 DEF，合并到同一展开任务处理（flatten + 分区一体），以减少任务数量；以 DEF 数据大小预估展开数据规模（数据大小 × 实例化次数），设阈值决定聚合（裁定 D26）。
- 叶实例的电源引脚坐标在展开时预计算☆（裁定 D18，⑫ 注入点直接可用）。
- **2026-09-13 裁定补记（S9 归属与产物组织定稿）**：①**instance/via instance 分区归属** = 放置点（pos）判定——在 core_rect 内 → 该分区 primary=true；不在 core 但在 extend_rect 内 → 副本入区 primary=false（所属权归放置点所在 core 的相邻分区；每个对象恰有一个 primary 副本，core 判定半开区间）；结构增 `CM_FLAGS(int8_t, primary)`（位宽静态检查已内置宏中）。**本口径全面取代 D7 的「bbox 左下角锚点」**（连接与电源引脚坐标一律归 primary 分区，语义单一）。②**分区数据四类**（取代 §5.3 五类草案与 via 独立对象）：`PART_{xp}_{yp}/GEOMETRY`（**以 net id 组织**：net 的 wire/rect 几何 + **via instance 图形**；DEF 中的 OBS 结构 → **net id = 0 + OBS flag** 一并放入；net id 保持 local + offset 形式**不换算 root**——下游需要顶层 global id 时自行加载 union 查询）、`/INSTANCES`（含 primary 位 + 电源引脚预展开坐标）、`/INST_CONNECTIONS`（instance connection 跟随 instance 副本）、`/NET_CONNECTIONS`（net connection 跟随 net 副本，**非 pg net 补全全量 connection**——跨分区连接也保存，本分区自足；pg 网不补全，靠 union + instance 维度拼装——全量复制代价不可接受；pg 判定 = special net / POWER-GROUND use）。③**cell 级 pin/OBS 几何不保存**——下游经 instance + transform 从 cell 数据复现全部 pin 与 macro OBS（撤销「实例图形随副本走」预案）；解析侧前置补充：**DEF obstruction（BLOCKAGE）收录**（net 0 + OBS flag 进分区 geometry——D17「仅密度通道」处置随之修订）。④net 几何（wire/rect）按与 extend_rect 交叠判定副本（无 primary 概念）；跨分区网副本带 is_crossing 标记（S10 统计口径）；撤销「主分区 = 最小分区 id」概念（非 pg 全量补全后无需主分区锚）。⑤首版分区对象不做 id 区间二次分块（每分区约 10-20 万 instance，体量可控）。
- **实施备注（2026-09-13 落地）**：
  - **结构与算法**：`src/emir/design/cpp/ds_flatten.h/.cpp`（`DSPartConnection`/`DSGeomEntry`/`DSPartitionGeometry`/`DSPartInstances`/`DSPartInstConnections`/`DSPartNetConnections` 四类正式对象 + `DSPartitionProduct` 分片聚合容器（merge_from 追加合并）+ `ds_flatten_block` 每定义一调用）+ 导出面（EXDS* + ds_flatten_block）。复合放置变换存入树节点（`DSHierNode.composite_transform_`，S6 DFS 递推回填）——展开任务只读单一 def 产物即可拿到任意出现位置的复合变换，保证「每份 DEF 数据只读一次」。归属判定 `assign_point`：core 半开区间包含 → primary（恰一）；无 core 命中（实例越出 DIEAREA 的输入形态）防御回退距最近 core 分区（tie 取小 id），恰一不变式恒成立；副本集 = extend 包含放置点的全部分区。UNPLACED 实例无物理放置、不入分区产物（D14 同族口径）。
  - **连接副本形态**：`DSPartConnection{instance_global_id, net_global_id, pin_name, port 位}` 双向共用——INST_CONNECTIONS 按 instance global id 组织（端点挂网 global id）、NET_CONNECTIONS 按 net global id 组织；`("PIN", port)` port 引用 → 端点实例 = 所属块实例自身 global id（⑧ local 0 映射）+ port 位。连接跟随全部副本（每持有该副本的分区一份）；pg 网判定 = special net 或 USE POWER/GROUND（S5b 连接解析节点记录进 `DSNetBuildData.pg_nets_`），pg 网 NET_CONNECTIONS 仅收本分区 instance 副本相关条目。
  - **net id 0 语义**：root 块 net_start_ = 0 且 local 从 1 起 → root 首网 global id = 0，与 OBS 桶同键共存（裁定补记③字面口径），obs 位判别（ds_flatten_test 锁定）。
  - **via 条目形态**：经 `DSGeomEntry` 统一入 net 桶（保持「geometry 以 net 组织」总形态）——`{layer_id, 全局 rect, via_cell_id（kNoViaCell = 非 via）, obs 位, primary 位}`；via cell 的 cut/enclosure 矩形平移至放置点后经复合变换展开、逐矩形挂所属 net，primary 位按 via 放置点归属分区单独置位。
  - **电源引脚预展开（D18）**：`DSInstance` 增 `CM_FLAGS(int8_t, primary)` + `CMVector<DSPowerPin> power_pins_`（解析产物恒复位/恒空，仅分区副本填充；序列化直接追加，早期无兼容负担）。锚点口径 = 该 pin 全部几何矩形的聚合包围盒中心（int64 中点——多点 pin 对输入序不敏感），×（复合 ∘ 实例放置）变换；pin 无几何（lib-only 形态）跳过不虚构。
  - **两级任务编排（ds_flow.py）**：plan 任务（依赖 S8 分区表 + S6 树 + block 名清单 + alpha；分组信息依赖树运行时数据，故在 worker 上**动态提交**下游任务——同 solver kickoff 先例）：预估 = DEF 文件大小 × 树上实例化次数，≥ alpha `def_aggregate_threshold` 独占任务、低于阈值按 def_paths 序贪心聚合（D26）→ per-组展开任务并行（每组只读本组 def 产物 + 每任务对全部分区各写一份分片临时对象，未触达分区写空产物保证合并依赖恒可解）→ 每分区一合并任务（merge 本分区全部分片 → 四类正式对象唯一写定）→ freeze（final_keys = 静态正式对象 + 运行时确定的全部分区对象名，由 plan 动态提交）。
  - **前置：DEF obstruction 收录（D17 修订）**：S4 头扫描增 `defrSetBlockageCbk`（LAYER 型逐矩形收录 → `DSBlockBuildData.obstructions_`，block 局部坐标 × def_units 换全局 DBU；未定义层条目级丢弃 + 计数、多边形 BLOCKAGE 首版不收录 + 计数、PLACEMENT 型无几何跳过），flow 侧经临时对象转入 per-DEF 产物；S9 展开逐位置换全局坐标入 net 0 + obs 位。
  - **测试**：ds_flatten_test.cpp 12 用例（primary 恰一 + extend 副本 + 半开区间边界/子定义复合变换/几何副本不裁剪 + is_crossing/via 图形挂 net + 放置点 primary/OBS net 0 桶/非 pg 全量补全/pg 过滤/inst_connections 跟随副本/电源引脚预展开/merge 幂等 + 序列化往返）+ ds_def_adapter_test BLOCKAGE 收录断言 + QA S9 段（test_emir_partition.py '2x1' 双分区 + test_emir_project_design.py 单分区，全部手算锁定）。

**S10 汇总校验 + 冻结** —— ⬜ 待实施
- 全局统计（实例/网/连接/图形计数、分区分布、跨分区网数、密度总量）。
- 完整性校验：global id 无空洞重复；并查集连通性自洽；分区网格无缝覆盖 DIEAREA 包围盒；密度守恒（合并前后/切分前后总量一致）；namemap 双向一致。
- **2026-09-13 裁定补记（校验分级定稿）**：**损坏类 → fatal message**（并查集不自洽 / 分区不无缝覆盖 / namemap 双向不一致——结构破坏，db 不能带病冻结）；**观测类 → user warn message**（id 连续性——空洞/重复可能丢数据但业务数据本身没有问题；密度守恒——仅影响分区结果，不损业务数据）。密度守恒按 **primary 口径**校验（Σ 各分区 primary 计数 = 树展开总数；副本不计——副本语义下分区总和必然大于全局）。任务组织：每分区一校验任务（并行读单分区产物）+ 全局汇总校验任务（id 连续性/覆盖/总量/namemap）。
- 提交冻结任务（freeze task，flow 异步四步范式收尾，开发规则 §3）。

### 3.3 并行机会汇总

| 并行维度 | 内容 | 约束 |
|---------|------|------|
| 阶段级 | S3 ∥ S4 ∥ S4b——三轨在 S2 汇总完成后同时推进，**全部完成后 S5a 才启动** | S3 为 S5a 前置（裁定 ⑯：正式解析 components 前 lib/lef cell merge 检测就位）；S4/S4b 提供 block cell/port 与 via cell；S5a 依赖三者全部汇总 |
| 阶段级 | S6 ∥ S5b —— 均仅依赖 S5a（裁定 ⑨） | S6 需计数与放置（S5a 已齐）；S5b 需 net namemap（S5a）+ via cell 就绪（S4b，裁定 ⑪） |
| 阶段级 | S7 ∥ S8 —— 都只依赖 S6 + S5b | 两者输出互不依赖 |
| 任务级 | S2/S4/S4b/S5a/S5b 为**并行任务（无业务合并，不套 MapReduceJob）**：S2/S4 每文件一任务 + 秒级全局汇总（id 分配/namemap）；S4b 每 DEF 一任务（via 定义解析，可与 S4 同遍回调）；S5a 每 DEF 两任务（COMPONENTS 全量 ∥ 网名扫描）；S5b 每 DEF 一任务（内部分批）；S9 为两级：展开任务按 block 定义切分（小 DEF 按数据量阈值聚合以减少任务数）+ 每分区一合并任务（真实合并语义，MapReduceJob 是否适配实施期定） | 解析类阶段产物天然分散（per-block 对象）；S9 展开任务使每份 DEF 数据只读一次（大 block 横跨多分区时不重复读） |
| 不可并行点 | S1（单文件小任务）；全局汇总类（id 分配、namemap 构建、S6 树与起始编号、S8 密度合并与分区决策、S10 校验）——均为秒级元数据操作 | 单个大 DEF 内部单流解析（语法状态机流式，12 GB 实测 78.6 秒）——多个 DEF 并行摊平；S5b 分批控制的是业务对象内存峰值，不改变单流语法约束 |

---

## 4. 密度图与空间分区

### 4.1 多通道密度图（分类分层保存 + 图形计数加权叠加；2026-09-09 裁定）

```
采样格子（bin）：全局统一固定尺寸的正方形网格（如 10 µm × 10 µm，可配置），每格长宽固定
通道分类：金属图形 / 实例 / 通孔三类各自独立累计；金属图形与通孔再**逐层分列**（每层独立计数）
格子原始数值（分类分层累计）= 该格中出现过的（与格交叠的）图形数量，逐图形累加
合成负载 = Σ_layer k_layer × w_metal × 该层金属图形数 + w_inst × 实例数 + Σ_cutlayer k_cutlayer × w_via × 该层通孔数
           ↑ 通道比重 (w_*) 与层系数 (k_layer) 为**建库时的一次性配置输入**；加权折叠只在 S8 分区决策时做一次
```

- **统计口径（2026-09-09 裁定，取代原 D9）**：固定采样格子上做**图形计数**——与格子交叠出现过的图形即计入该格（一个图形跨多格则在多个格子各计一次）；每个图形叠加的数值按其所属层的系数加权。
- **分类分层保存（2026-09-09 裁定）**：密度按通道分类、按层分列持久化（local 密度图与合并后的全局密度图一致）——这是数据组织与可观测性要求（任一层、任一通道的分布独立可查）；**不是为了运行时调权**：design db 创建流程固定，freeze 后不可修改，**不存在动态调整权重的需求**——权重属建库配置，调整权重即重新建库（新 db）。
- **逐层密度系数（k_layer）的业务依据**：低层 layer 电阻更高、线更细、图形数量更多，对后续电阻提取（④ extraction）的计算负载影响更大——系数自高层 layer 向低层 layer **逐层递增**；第一版全部取 1（等价于不区分），经建库 API 的 `alpha` 参数传入（键表见 §6，裁定 ⑦/总流程裁定 18），取值策略实施期按 ④ 实测负载标定（裁定 D25）。
- 通孔归属：通孔实例按其切割层（连接层对）计入该层列；第一版系数全 1 时归属无差别。
- 分区归属口径（与统计口径分离）：一律左下角定位点（§5），简单确定。
- 尺度：30 mm × 30 mm 芯片、10 µm 格 → 3000 × 3000 = 900 万格 ×（金属/通孔 × 层数列 + 实例列）× 4 字节 ≈ 1.5 GB 级，可控（分块对象存储，S8 按行流式合并）。

### 4.2 分区决策

```
        xp=0     xp=1      xp=2
      ┌───────┬──────────┬───────┐  yp=2
      │  ░░   │  ▓▓▓▓▓   │  ░░   │      ▓ 高密度（电源条带列/宏阵列）
      ├───────┴──┬───────┴───────┤  yp=1   切线位置（虚线）由行/列负载
      │ ▓▓▓▓▓▓▓ │      ░░░      │        前缀和等分决定 → 各分区宽窄
      ├─────────┴───────┬───────┤  yp=0   不均，负载均衡
      └─────────────────┴───────┘
分区编号 (xp, yp)：横向第 xp、纵向第 yp 个分区；分区矩形表持久化
```

- 算法（裁定 D11）：行/列负载前缀和一维等分（简单、分区仍为规则网格、编号 (xp, yp) 成立）；备选递归二分（更灵活但编号不规则，与原始需求 (xp, yp) 表述不符，列为备选）。负载口径为 §4.1 的合成负载（通道比重 + 逐层系数加权折叠后的标量场）。
- 分区数来源（裁定 D11）：用户显式给定 (nx, ny) / 给定目标每分区负载自动 / 两者结合（建议首期用户给定）。
- 分区表设计为**可替换产物**：将来支持用户预分区输入（外部工具划分结果直接注入，对应框架能力差距分析 P0-4）与非均匀分区算法，不改动 S9 之后的消费链路。
- 不规则 DIEAREA：取包围盒网格化，die 外格子负载恒 0（切线自然不落在外面）。

---

## 5. 分区切分与存储组织

### 5.1 归属判定：左下角定位点 + 主分区

```
                  分区切线 x = X0
                       │
      ┌────────────────┼────────────────┐
      │   (0, 0)       │    (1, 0)      │
      │         ┌──────┼─────┐          │
      │         │ 图形A │图形B│          │   图形A 左下角在 (0,0) 内 → 主分区 (0,0)
      │         └──────┼─────┘          │   图形B 左下角恰在切线 x=X0 上 → 归 (1,0)
      │                │                │   （半开区间 [x_min, x_max) 判定，
      └────────────────┴────────────────┘     与「位于 low-left 边界线上归该分区」规则一致）
```

- 统一实现语义：每个分区为半开区间 [x_min, x_max) × [y_min, y_max)；定位点 px ∈ [x_min, x_max) 即归属。位于 die 外边界（首分区 low-left 线）上的点归首分区——与原始需求第 9 条末段的规则一致。
- **主分区 vs 普通分区**：图形左下角所在分区为该数据的主分区；图形实体（未裁剪，裁定 D8）同时登记到所有与其交叠的分区（普通分区），保证逐分区消费时局部视野完整。

### 5.2 各类数据的切分规则

| 数据 | 切分规则 |
|------|---------|
| 叶实例（标准单元/宏） | 逻辑归属（连接、cell 引用、电源引脚坐标）归锚点分区 = 实例左下角主分区；实例图形（禁布区/引脚几何，若入库）按各自定位点散布到交叠分区（裁定 D7） |
| 块实例 | 其展开数据（叶实例/网几何）按上述规则逐项切分；块实例本身不作为切分单位（大 block 的内部数据天然跨分区，正合原始需求「block1 横跨两分区」的场景） |
| 网 | 跟随实例：网连接的实例分布在哪些分区，网即出现在哪些分区（每分区持有本区的连接条目）；网的 root 归属（并查集结果）随网数据携带 |
| 网几何（导线矩形串/通孔） | 按左下角定位点归主分区，交叠分区登记普通分区（不裁剪，裁定 D8）；拓扑连接性条目随矩形携带 |
| 通孔实例 | 同网几何（定位点 + 连层关系入所在分区） |

### 5.3 产出组织（原始需求第 5 条的落地形态）

- **按类型分对象**：每分区内 instance/net/connection/geometry/via 各自成对象（如 `PART_{xp}_{yp}/INSTANCES` 等），单对象体积受控（可再按 id 区间分块）。
- **分片与合并**（2026-09-09 裁定 ⑤）：展开任务（按 block 定义切分）产出各分区分片，**每分区一合并任务**将不同来源分片 merge 为最终分区对象——分区侧存在真实合并语义；合并后分区对象即下游消费形态（无需读时合并）。
- **可组合的读库视图**（查询/遍历接口，见 §6）：分区视图（逐分区迭代，④⑤⑥ 消费形态）、网视图（逐物理网聚合，含跨分区成员）、实例视图（逐实例连接/位置/电源引脚）、层级视图（子树枚举与坐标变换）。
- 全 design db 内以对象名空间组织分区产物，不另建子数据库（下游经数据库链 find_db(role="design") 后按视图消费）。

---

## 6. 查询与遍历接口（读库 API 草案）

按开发规则 §9，对外可见面 = 建库入口 + 读库 API（`ds_functions.py`）。建库入口签名（2026-09-09 裁定 ⑦ / 总流程裁定 18）：

```python
build_design_db(name, def_path, lef_paths, lib_db, settings: dict, alpha: dict)
# settings：稳定配置项（design db 首版无稳定项，空 dict 占位）
# alpha：未稳定配置项（首版全部配置在此，成熟后迁 settings）
```

**design db 配置键表（首版草案，键名实施期可调；模式遵循 dev-rules.md §3。2026-09-13 起全部已实施 alpha 键经 `DSAlphaSettings` 声明式定义（五要素，[src/emir/design/py/alpha_settings.py](../src/emir/design/py/alpha_settings.py)）：validator 校验非法值回退默认、未知键忽略，DSGN::0013 一次汇总提醒；settings 对象以固定对象名 `"alpha_settings"` 随建库写入 db，消费点 `read_object` 读回 + `normalize()` 兜底）**：

| 参数 | 键 | 语义 | 默认 | 关联裁定 |
|------|----|------|------|---------|
| alpha | `layer_density_weights` | 逐层密度系数表（layer 名 → 系数），自高层向低层递增。**未实施**（S8 裁定 ⑥ 本期不暴露 alpha 键，`DSDensityWeights.layer_factors_` 接口已留——设置此键当前静默无效） | 全 1 | D25 / 裁定 ⑥ |
| alpha | `density_channel_weights` | 密度通道比重（instance/metal/via dict） | {instance: 6, metal: 2, via: 2}（2026-09-12 裁定；逐层系数接口保留、本期不暴露） | 裁定 ⑥ |
| alpha | `density_bin_size` | 采样格子边长（µm） | 10 | §4.1 |
| alpha | `net_batch_size` | S5b 网内容解析的批界网数（分批控内存峰值） | 1000 | 裁定 ③ |
| alpha | `lcp_name_arena` | 名字伴生对象 id→name 侧 LCP 后缀压缩封口（容量换内存） | False | R8d 裁定 55 |
| alpha | `target_partitions` | 直切分区数 '{x}x{y}'（跳过分区数计算与行列分布推导，切线仍按前缀和） | 未设置 | 2026-09-12 裁定 4 |
| alpha | `partition_count` | 总分区数 N（行列分布按负载自适应） | 未设置 | 2026-09-12 裁定 4 |
| alpha | `partition_target_density` | 目标每分区合成负载（N = ceil(总负载/目标)） | 150000 | 2026-09-12 裁定 4 |
| alpha | `def_aggregate_threshold` | 小 DEF 聚合阈值（字节；预估展开数据规模 = DEF 文件大小 × 树上实例化次数，低于阈值的多个小 block 定义聚合到同一展开任务以减少任务数）——缺省 64 MiB（2026-09-13 S9 落地标定：单任务内存峰值 = 估算数据数倍、可控于数百 MB，秒级任务调度开销相对 64 MiB I/O 可忽略；原名 s9_def_aggregate_threshold，2026-09-13 按 Section 2.7 命名规范改业务语义名） | 64 MiB（已标定） | D26 |

（S8 键优先级 target_partitions > partition_count > partition_target_density；非法值 DSGN::0013 提醒后回退，不 raise。原草案键 `partition_grid` 由 `target_partitions` 取代。）

**读库 API 草案**（2026-09-09 裁定 ⑬：全局轻量数据以 **DSDesign 容器**为统一入口——细粒度表访问作为容器方法或便捷封装，后续功能向容器增强）：

| 接口（名称为草案） | 语义 |
|------------------|------|
| `load_design(db)` | **顶层容器入口**：返回 DSDesign（cell 单一结构含简化 pin 与 fake cell 单独字段 / via cell 权威表 / lib 关联 / 层级树 + 编号区间 / 轻量 namemap；pin 表/几何数据默认**不含**——需要什么专门加载什么，裁定 ⑰/⑱） |
| `load_design_with(db, pin_tables=…, pin_geometries=…)`（统一加载 API，裁定 ⑱） | **封装 load + set 两步的统一加载过程**：按需加载 pin 表数据集 / pin 几何数据集独立对象并注入容器专用字段（不序列化），下游业务一律经此获取完整 DSDesign 数据 |
| `design.get_cell(cell_id)` | **下游获取 cell 的统一入口（裁定 ⑰）**：从容器专用字段将该 cell 的 pin 表数据与几何数据按**指针**放入 cell 的两个不序列化字段（零拷贝；未加载的部分为空指针） |
| `load_design_stack(db)` | Stack 独立类型（层堆栈，裁定 ⑭——独立保存，tech db 流程将复用） |
| `design.tree_*`（层级树接口，编号区间表保存在树结构中，裁定 ⑮） | ①`belonging_block_inst(id, kind)`：由 instance/net id 反查所属 block instance（编号区间二分）；②`id_range(block_inst, kind)`：查 block instance 的 instance/net id 范围；③`parent(block_inst)` / `children(block_inst)`：parent block instance id 与直系 child block instance id 列表；④`dump_tree()`：以 name 打印层级树（tree node 同时保存 block instance/cell 的 name 与 id） |
| `design_find_id(db, kind, name)` / `design_find_name(db, kind, id)` | name ↔ id 双向查询（cell/pin/net/port 经容器与大体量映射对象；instance/net 键带 block instance 维度） |
| `load_design_net_union(db)` | 并查集（find / root → 成员枚举）——大体量，容器外独立对象 |
| `iter_design_partition(db)` | 分区迭代器：矩形元数据 + 逐类型对象句柄（分区产物，容器外） |
| `iter_design_partition_net(db, xp, yp)` | 分区内网迭代（连接/几何/通孔实例，含拓扑） |
| `iter_design_partition_instance(db, xp, yp)` | 分区内实例迭代（cell id/位置/电源引脚坐标） |

高频结构（几何/连接/分区对象）C++ 实现 + 序列化（开发规则 4.2/4.3），Python 编排层一行调用。

---

## 7. 原始九条需求逐条评审意见

| # | 原始需求 | 评审结论 |
|---|---------|---------|
| 1 | 读入 lef/def 建立完整芯片数据结构 | **合理**。补充：tech lef 与 cell lef 分阶段（S1→S2）；DBU 统一与换算是前置（§8.2）；层电气参数不进 design db（② 职责，按层名在 ④ 对齐） |
| 2 | 分阶段产出，经分布式文件系统传递 | **合理**。映射为框架数据库链 + freeze：每阶段产物 = db 对象，断点续算由 project 机制天然获得。修正：lib 关联（原「步骤 3 并行轨」）重构为 **lib cell ↔ lef cell 结构 merge 且为 S5a 前置**（裁定 ⑯——正式解析 components 前完成 merge 检测，非 1:1 对应及时提醒不 crash），与 S4/S4b 并行 |
| 3 | 核心数据六类 | **合理但不完备**。必须补充：layer、via（含连层）、port、密度图、层级树、并查集、name↔id 映射、网用途（电源/地）标志（§2.1 全表） |
| 4 | 按策略分区，每分区含本区数据 | **合理**。补充：分区表设计为可替换（用户预分区/其他算法可注入）；切线算法与分区数来源为裁定 D11 |
| 5 | 分类型保存 + 组织加载 + 查询遍历接口 | **合理**。落地为按类型分对象 + 四种读库视图（§5.3/§6） |
| 6 | 层级：每 DEF 实例化为 block，组织为树 | **合理，需术语精确化**：block 定义层面是有向无环图（共享子块是常态），block **实例**层面才是树；树以块实例为节点；存储单份、编号段按实例复制（§2.5） |
| 7 | id 表示 + 连接三元组双组织 | **合理**。补充：pin id 编码方式（D1）、instance name 映射规模问题（D6）、连接含块实例 port 与顶层引脚两类特殊条目（§2.3） |
| 8 | geometry 三类型模板 + 矩形 low-left/high-right | **合理**。必须补充：图形带 layer id、布线拓扑连接性保留（不可降维为平面图形集）、变换（朝向 8 种 + origin + 嵌套复合）、线宽来源优先级、序列化与绑定的模板实例化集合（§2.4，D12） |
| 9 | 十步建库流程 | **总体合理**。修正/澄清五点：①步骤 4（block cell/port）依赖 DEF 头部信息，须以轻量扫描在完整解析之前完成；完整解析按裁定分两段（第一段并行：COMPONENTS 全量 + 网名扫描建 namemap；第二段分批全量解析网内容控内存峰值），层级编号分配前移至两段之间（D3/D4 已裁定结构）；②步骤 3 与 4/5 并行成立（依赖仅 S2 汇总）；③步骤 6（层级树）与第二段**并行**（均仅依赖第一段；第二段为 per-DEF 解析、尚未 flatten 拿不到全局 net id，产物即 local id，全局化统一在 flatten 时换算——层级树构建前移无收益，裁定 ⑨；第二段另需 via 数据就绪的前置阶段，裁定 ⑪）；④步骤 7 与 8 可并行；⑤原始步骤「按分区切分数据」的准确定位是 **flatten 展平 + 分区保存**（S9）：解析数据每 DEF 单份，多实例化在此阶段展开；展开任务按 block 定义切分（大 block 横跨多分区时不重复读同一份大数据）+ 每分区合并任务汇成最终分区对象（裁定 ④/⑤）。另建议补充 S10 校验收尾（完整性 + 密度守恒）；分区归属的半开区间语义与「low-left 边界线」规则一致化（§5.1） |

---

## 8. 补充项清单

### 8.1 必须补充的数据（缺失即下游断链）

1. **网用途（USE）标志**（电源/地/信号）——首期电源网筛选的唯一依据（总流程裁定 4）；含 NETS 段内 USE POWER 的网与 SPECIALNETS 全体。
2. **宏引脚用途与电源引脚几何**——⑫ 注入点 = 实例位置 + 宏电源引脚偏移（总流程裁定 6）。
3. **顶层引脚（top PINS）几何与用途**——电源注入的边界条件锚点（电压源接入位置），⑫ 求解边界需要。
4. **通孔连层关系**（每通孔实例的底/顶 layer id）——④ 层间支路。
5. **布线路径的拓扑连接性**——④ 打断建节点依据（重复强调：平面化会破坏网内连通性）。
6. **非默认规则线宽表**——信号网几何重建线宽来源。
7. **各 DEF 的 DBU 换算系数 + 全局 DBU 基准**——坐标一致性。
8. **层级树的变换链**（朝向 + origin + 平移复合）——层级展开与坐标换算。
9. **并查集 root → 成员反向索引**——物理网聚合消费前提。
10. **lib 电源引脚关联**（related_power_pin）——⑪⑫ 电流挂载引脚。
11. **密度分类分层保存、通道比重与逐层密度系数**（2026-09-09 裁定 ⑥）：低层对电阻提取负载影响大、系数逐层递增；固定采样格子按图形计数加权叠加；分类分层持久化为数据组织要求；权重为**建库时一次性配置**（第一版全 1，代码接口保留），db freeze 后不可变——**不存在动态调整权重的需求**，调整即重新建库；权重随分区决策元数据持久化作 provenance。

### 8.2 不可忽略的实现细节（易踩坑清单）

1. **DEF 与 LEF 的 DBU 可能不同**（DEF 的 UNITS DIST MICRONS 独立声明）：坐标统一到全局 DBU——裁定 ㉝ 后全局基准恒 1000 DBU/µm（`DSStack::kGlobalDbuPerMicron`），各文件按自身声明换算入库；lef 间 DBU 声明不一致不再 raise（D15 该子项已由 ㉝ 撤销）。
2. **宏 origin 的放置换算**：DEF 放置坐标是 origin 落点，宏左下角 = 放置点经 origin 与朝向换算；忽略 origin 会导致全部引脚偏移错位。
3. **朝向 8 种的变换矩阵表**（含翻转时以宏宽/高取镜像：x' = W − x）与多级嵌套复合（int64 中间量防溢出）。
4. **转义名（escaped names）与分隔符**：层级引脚引用语法 `实例名/引脚名` 依赖 DIVIDERCHAR；总线位选依赖 BUSBITCHARS；lefdef 已有转义回归测试（test_escape.def），适配层须保留语义。
5. **大小写敏感**：LEF/DEF 名字大小写保留（不折叠），namemap 按精确匹配。
6. **半开区间归属判定**：分区归属实现为 [x_min, x_max) × [y_min, y_max)，与主分区规则一致（§5.1）；代码与文档统一表述，避免边界双归属/零归属。
7. **密度守恒校验**：层级合并与分区切分前后密度总量一致（S10），防叠加/漏计。
8. **同一 block 多实例的编号段复制 vs 存储单份**（2026-09-09 裁定）：解析阶段每 DEF 数据严格单份；flatten 展平只在 S9 进行——展开任务按 block 定义切分，读单份数据、对其全部实例位置展开（global id 各异、坐标变换复合）、分流产出分区分片，再由每分区合并任务汇成最终对象。
9. **悬空 port / 悬空网 / 重复连接**：兜底计数 + 消息提醒，不 raise（开发规则 §7）。
10. **SPECIALNETS 逐段线宽**：特殊网路径每段可自带线宽（NEW 段落），不能假定全网一致。
11. **大文件内存形态**：lefdef 流式回调 + 选择性解析（section skip / NetNameOnly）已验证峰值内存与文件体积无关（由最大单记录决定）——解析器峰值低，**业务对象的全量积累才是内存峰值主体**：S5a 即产即落盘、S5b 分批落盘释放（裁定 ③）压平该峰值；适配层沿用回调式增量构建，勿全量中间态。
12. **不同 DEF 的同名 via 异形污染**（裁定 ⑫）：DEF 来源 via 登记名必须带 `design_name::` 前缀——若按裸名合并进同一 via cell id，同名异形会导致后续几何实例化错误；via cell 全集集中保存，下游只从权威表取数。
13. **运行时注入字段不序列化**（裁定 ⑱）：cell 与 DSDesign 的 pin 表/几何数据专用字段一律不进 FLY_SERIALIZE 字段表——`write_object` 时不得把运行时注入的数据写出（独立对象才是持久化边界）。
14. **fake cell 兜底与 id 生成**（裁定 ⑲/⑳）：未定义 master 引用动态创建 fake cell（名称带 block 前缀、id 从 max_cell_id 之后生成、1×1 最小单位占位）；fake instance 的网连接涉及不存在的 pin 时按 pin 缺失兜底跳过 + 计数。
15. **存储形态逐字段决策**（裁定 ㉒）：ptr vs 直接存对象按「拷贝成本 / 共享需求 / 连锁修改风险」逐字段分析——小对象直接存（更快更省、天然隔离修改），大体量或共享/运行时注入用 ptr；**禁止裸指针**（业务实现无必须场景：拥有用 CMSharedPtr/CMUniquePtr，观察传参用引用）。

### 8.3 业务扩展方向（遵循总流程裁定 5「新功能以新 db 叠加」）

| 方向 | 说明 | 前提 |
|------|------|------|
| 信号网几何入库 | 当前裁定 D5 建议首期仅电源网；信号网几何对后续信号完整性/耦合分析有价值 | design db 重建产物按网用途可扩展，不影响分区链路 |
| 用户预分区注入 | 外部工具（图划分等）的分区结果直接替换 S8 产物 | 分区表可替换设计（§4.2） |
| 增量设计变更（ECO） | 设计小改后局部重解析 | db 链断点续算已有；local id 稳定后可做块级增量 |
| 布线障碍/禁布区密度通道 | 宏禁布区、BLOCKAGE 进入密度图通道（反映布线拥挤） | D17/D19 裁定后加通道即可（密度图通道数可扩） |
| 多设计版轮对比 | 同设计多版 DEF 的 diff 视图 | namemap + 编号区间表可支撑（远期） |

---

## 9. 裁定点清单（等待裁定）

| # | 问题 | 选项与建议 |
|---|------|-----------|
| D1 | pin id 编码方式 | A. 全局平铺单调分配（三元组最紧凑，pin 总量 = cell 数 × 引脚数 ≈ 百万级，可行）；B. (cell id + cell 内序号) 二级编码（省 namemap，但连接与查询多一步解码）。**建议 A** |
| D2 | ~~lib 侧 cell 的 id 语义~~ **已裁定（2026-09-09，裁定 ⑯/⑰）**：cell 始终单一结构，S3 在 S5a 前完成 merge | lib 阶段填 lib 字段、lef 阶段填 lef 字段、merge 整合进同一 cell 记录；id 空间以 lef/def 侧为准，lib 独有 cell 不入 id 空间（跳过计数）；pin 为简化结构（⑰），表/几何数据独立对象按需加载；非 1:1 对应 user message 提醒不 crash |
| D3 | block cell/port 信息来源 | A. 子 block DEF 头部扫描（本文方案，输入只需 DEF）；B. block 抽象 LEF（业界层级流程常见，port 信息更权威，但要求额外输入文件）；C. 双来源兼容（DEF 扫描为准，抽象 LEF 可选校验）。**建议 A 起步，接口留 C** |
| D4 | ~~DEF 两遍解析策略~~ **已裁定（2026-09-09）**：轻量头扫描（S4）+ 完整解析两段式（S5a 并行两任务 / S5b 分批多阶段） | 文件被多遍流式读取（S4 一遍 + S5a 两遍 + S5b 一遍，每遍选择性回调、峰值低）；「减少读取遍数的回调合并」列为后续优化项 |
| D5 | 信号网几何存储范围 | A. 首期仅电源网几何（SPECIALNETS + USE POWER），连接关系（网表）全量（建议，对齐总流程裁定 4 首期专注电源）；B. 全网几何入库（存储与解析成本显著增加）。**建议 A，扩展见 §8.3** |
| D6 | ~~instance name ↔ id 映射存储~~ **已裁定（2026-09-09）**：全实体双向映射直接建好 | 所有实体（layer/cell/pin/via/net/instance/block/port）name ↔ id 全量持久化；首版 map（name → id）+ vector（id → name），分块/压缩/按需加载作为后续持续优化项（§10 规模预估）；instance/net 键带 block instance 维度（§2.2） |
| D7 | 实例跨分区语义（**2026-09-13 修订**：锚点口径由「bbox 左下角」改为 **primary = 放置点 pos 在 core_rect 内**，连接/电源引脚坐标归 primary 分区——见 S9 裁定补记①） | A. 逻辑归属唯一（锚点 = 左下角主分区，连接/电源引脚坐标归锚点分区），图形按定位点散布（建议——下游无重复处理，无双重计数）；B. 实例整体多分区冗余（每分区完整副本）。**建议 A**（原始需求「inst 划分至两个分区」在 A 语义下 = 其图形分布两区 + 逻辑归锚点区） |
| D8 | 跨分区图形是否裁剪 | A. 不裁剪，完整图形登记到交叠分区（主分区 = 定位点区；建议——保拓扑完整，④ 打断建节点自定位）；B. 按分区矩形裁剪成片（几何更紧凑但拓扑割裂，需补片间粘合）。**建议 A** |
| D9 | ~~密度图统计口径~~ **已裁定（2026-09-09）**：固定采样格子图形计数加权叠加 | 与格交叠出现过的图形即计入（跨多格则多格各计一次），逐图形按所属层系数加权；取代「交叠面积分摊」方案 |
| D10 | 层级密度合并的格子对齐 | 块放置位置一般不对齐格子边界。A. 格值分摊叠加（块局部格子的计数值按与全局格子的交叠面积比例撒入，建议）；B. 强制块放置格子对齐（约束输入，不现实）。**建议 A** |
| D11 | 分区数与切线算法 | A. 用户给定 (nx, ny) + 行/列前缀和等分切线（建议首期）；B. 给定目标负载自动定分区数；C. 递归二分（编号不规则，与 (xp, yp) 需求不符）。**建议 A，B 作为自动模式后补** |
| D12 | id 位宽与坐标类型 | A. 各 id 空间独立 uint32（instance/net 各自 42.9 亿上限，够用）+ 坐标 int32（DBU，±21 亿 DBU ≈ ±1000 mm@2000 DBU/µm，足够）+ 中间量/面积 int64；B. 统一 uint64/int64（宽裕但对象膨胀）。**建议 A**；geometry 模板实例化集合 {int32, int64, double} |
| D13 | 生成式通孔规则（VIARULE）支持 | A. 首期支持按参数展开（DEF 引用 VIARULE 名 + CutSize 等参数现场生成几何）；B. 首期仅支持预定义 VIA，VIARULE 引用计数提醒。**建议 A**（电源网大量使用生成式通孔） |
| D14 | UNPLACED 实例兜底 | A. 跳过 + 计数 + 消息提醒（无坐标无法入分区）；B. raise。**建议 A**（设计未完成是业务异常，非格式错误） |
| D15 | ~~引用缺失语义~~ **已裁定（2026-09-09，裁定 ⑲/⑳）**：fake cell 兜底（非 raise） | master cell 未定义 → 动态创建 fake cell（名称 `block_cell_name::cell_name`、id = max_cell_id + 唯一值、无 pin、1×1 矩形、DSDesign 单独字段保存），instance 原地创建指向 fake cell id；user message 提醒不 crash。「lef 间 DBU 不一致 raise」子项**已由裁定 ㉝ 撤销**（恒基准 1000，各自换算）；「层引用未定义 raise」子项已改为条目级丢弃兜底（DSGN::0010 + skipped_layer_ref_count，dev-rules §7） |
| D16 | LEF 层收录范围 | A. 布线层 + 切割层（编号与几何属性；建议）；B. 含 implant/特殊层（掩模分析才需要）。**建议 A** |
| D17 | DEF 附属段收录（**2026-09-13 修订**：DEF obstruction（BLOCKAGE）改为收录进分区 geometry——net id 0 + OBS flag，见 S9 裁定补记③）（ROW/TRACK/BLOCKAGE/REGION/GROUP/SITE） | A. 首期全部不建库（SITE 名随 macro 引用保留字符串）；BLOCKAGE 仅入密度通道（可选）；B. 全量入库。**建议 A + BLOCKAGE 密度通道可选项后补** |
| D18 | 实例电源引脚坐标展开时机 | A. S9 切分时预展开进分区（⑫ 直接消费，建议）；B. ⑫ 消费时现算（每轮重复变换）。**建议 A** |
| D19 | 宏禁布区（OBS）几何入库范围 | A. 入库（密度通道/后续分析可用，宏定义层量小）；B. 首期不收。**建议 A** |
| D20 | 布线语句深度处理（TAPER/TAPERRULE/STYLE/mask/SHIELDNET/NOSHIELD） | A. 首期忽略（几何宽度按线宽优先级规则），未识别属性计数；B. 全量收录。**建议 A**（EMIR 几何精度不受影响） |
| D21 | design 模块消息前缀 | 建议 **DSGN**（避开 LIBR 等已注册前缀；注册遵循开发规则 §6） |
| D22 | 层级环 / 多根零根检测语义 | **建议 raise**（层级非法属格式错误类，无法解析）；**2026-09-12 补注：已从 raise 改为 fatal message**（DSGN::0011，`MSG_FATAL_EXIT` 码 80 退出 + master 联动 fast_exit——机制见 docs/message-system.md §14，处置规则见 dev-rules §7.1） |
| D23 | 通孔实例存储粒度 | A. 紧凑定长数组（via instance id + via cell id + 位置，连层经 via cell 定义查得；建议）；B. 对象化逐实例。**建议 A**（亿级量级下对象开销不可接受；via instance 无 name，天然适合无分支定长记录） |
| D24 | S5b 分批粒度 | 批界按累计对象数/字节数阈值切（如每批 10 万 net 或 512 MB 业务数据）；首版单任务内分批（单遍流式读取 + 批界落盘释放），批间并行（需多遍读取，代价明确）列为优化项。**实施期按实测细化** |
| D25 | 逐层密度系数取值策略（2026-09-09 裁定方向） | 自高层向低层逐层递增已裁定；第一版全部取 1、系数表作为**建库时一次性配置输入**保留接口（db freeze 后不可变，非运行时可调）。具体递增策略（线性/指数/逐层手配表）**实施期按 ④ 提取实测负载标定** |
| D26 | S9 小 DEF 聚合阈值（2026-09-09 裁定方向） | 以 DEF 数据大小 × 实例化次数预估展开数据规模，低于阈值的 DEF 聚合到同一展开任务（flatten + 分区一体）以减少任务数；阈值数值**实施期按集群任务开销实测标定**（经 alpha 参数开放） |
| D27 | ~~建库 API 配置参数~~ **已裁定（2026-09-09，EMIR 开发标准）**：统一 `settings` + `alpha` | 全部 `build_<角色>_db` 追加 `settings`（稳定配置 dict）/ `alpha`（未稳定配置 dict，成熟后迁移）两参数；design db 首版全部配置项进 alpha（键表见 §6）。规则落点 [dev-rules.md](dev-rules.md) §3、总流程裁定 18 |
| D28 | geometry 通用子模块的归属路径与命名前缀（裁定 ㉑ 方向已定：独立通用子模块） | A. `src/container/geometry/` 子模块 + CM 前缀（CMPoint/CMRect/CMPolygon）——与 CMLookupTable 同体系（总流程裁定 11「通用结构属框架层」先例，**建议**）；B. 顶层 `src/geometry/` 独立模块（更高层级，当前仅 design 一个消费者时略重） |

---

## 10. 规模预估与风险

| 项 | 预估/实测 | 说明 |
|----|----------|------|
| 解析吞吐 | 12 GB DEF 实测 78.6 秒（单流，优化版 + tcmalloc），峰值内存 114.3 MB（流式，与文件体积无关） | src/lefdef README 实测数据；多 DEF 并行摊平，单文件不可拆 |
| 典型规模参照 | 12 GB DEF ≈ 2456.8 万实例 / 909.3 万网 | 亿级节点目标对应更大层级设计（多 block 并行） |
| namemap 规模 | 全实体双向映射（含 instance/net，已裁定全量持久化）：亿级展开实体 × 平均 30 B ≈ 数十 GB 级 | 首版 map + vector 直存；分块、压缩、按需加载为持续优化项（用户裁定「先做对，再优化」） |
| 密度图 | 900 万格 ×（金属/通孔两通道 × 层数 + 实例列）× 4 B ≈ 1.5 GB 级（如 10 层） | 分类分层持久化；分块对象存储 + S8 按行流式合并可控 |
| 分区产物 | 每 (xp, yp) 分区逐类型对象，单对象受控可再分块 | 下游逐分区消费即分布式单元 |

**主要风险**：
1. **lefdef 接入 Bazel 是硬前置**：当前未接 Bazel、无 Python 绑定、无 db 映射层（emir-data-flow §5 明示）——design db 立项的第 0 项任务（按其 README 演进三步走的第 1、2 步：接 Bazel + 解析正确性回归基线），引入方式遵循开发规则 §4.3（预编译签入 + PATCHES.md）。
2. **回调式适配层工作量**：lefdef 是 C 回调式接口，S2/S4/S5 三个消费场景（cell lef / DEF 头 / DEF 全量）需各自装配回调集，属一次性投入。
3. **几何模板的序列化与绑定**：DSPoint/DSRect/DSPolygon 模板类需进 FLY_SERIALIZE 体系并导出实例化集合（D12），与 lib db 的 CMLookupTable 模式同构。
4. **分区切分的数据重排量**：S9（flatten 展平 + 分区保存）读全部块数据 + 展开变换 + 分流写出 + 分区合并，是设计库最大的单次数据搬移；展开任务按 block 定义切分（大 block 横跨多分区不重复读，裁定 ⑤）+ 每分区合并任务，两级并行可摊平。
5. **DEF 多遍读取的 I/O 放大**：每文件被读 4 遍（S4 头扫描 + S5a 两任务各一遍 + S5b 一遍），各遍均为选择性回调（工作量大减但字节流相同）；任务并行摊平耗时，遍数合并（回调集裁剪/区间索引）列为后续优化项。

---

## 11. 模块落地映射（立项实施时参照）

```
src/emir/design/                      # 模块简写 DS（总流程裁定 12 已分配）
├── cpp/
│   ├── ds_types.h/.cpp               # **DSDesign 顶层容器（裁定 ⑬：cell 单一结构（简化 pin + fake cell
│   │                                 #  单独字段）/via cell 权威表/lib 关联/层级树+编号区间/轻量 namemap
│   │                                 #  统一收纳，后续功能向此类增强；get_cell 统一入口指针注入，
│   │                                 #  专用字段不序列化，裁定 ⑰/⑱）** + **DSStack 独立类型
│   │                                 #  （裁定 ⑭：层堆栈，tech db 流程复用）**
│   │                                 # + DSLayer/DSCell/DSPin/DSViaCell/DSViaInstance/DSBlock/DSBlockInstance/
│   │                                 #   DSInstance/DSNet/DSConnection/DSUnionFind/
│   │                                 #   DSDensityMap/DSPartition + merge/查询方法（语义下沉 C++，同 lib 模式）
│   │                                 #   ——图形结构（Point/Rect/Polygon 等）不在本模块：
│   │                                 #   独立 geometry 通用子模块（裁定 ㉑，归属见 D28）
```

**前置工具与实现约束（待本文档批准后实施）**：

- **CM_PROPERTY 宏**（公共工具宏，裁定 ㉓；**非 container 模块**）：全仓库经检查不存在，需新增——`CM_PROPERTY(attr_name);` 自动生成 `get_attr_name` / `get_ref_attr_name` / `get_cref_attr_name` / `set_attr_name` / `set_attr_name_move` 五个接口（值返回/可变引用/常量引用/拷贝赋值/移动赋值）；**成员变量必须以 `_` 结尾**。实施要点：**放置 `src/common/types/cpp/property_macro.h`**（公共宏归 common 模块族，同 FLY_SERIALIZE_* 宏族先例；纯头仅 `<utility>`，随 `fly_common_types` 库——container_aliases.h 聚合全部容器头与指针别名，仅为一个宏引入过重的头文件依赖，故不放 container）；测试归 `src/common/types/tests/`（TDD 五接口语义：值拷贝隔离/原地修改/const 对象/拷贝源完整/移动转移）。
- **存储形态逐字段决策**（裁定 ㉒）：ptr vs 直接存对象按「拷贝成本 / 共享需求 / 连锁修改风险」逐字段分析——直接存对象更快更省内存时直接存；大体量、共享、运行时注入（⑰ 专用字段）用 ptr；**禁止裸指针**（拥有用 CMSharedPtr/CMUniquePtr，观察用引用）。
│   ├── ds_lef_adapter / ds_def_adapter # lefdef 回调适配层（S1/S2 与 S4/S5 场景装配）
│   └── ...
├── export/design_export.cpp          # nanobind 绑定（_fly_emir_design.so）
├── py/                               # 七文件（同 lib 实践：ds_export/ds_register_msg/ds_db/ds_flow/
│                                     #          ds_utils/ds_functions/__init__）
├── tests/                            # 单测：小 LEF/DEF 集合的结构化断言 + 序列化往返 + merge 语义
└── (lefdef 经 src/lefdef 模块引入，不经本目录 third_party)
```

- flow 装配：`build_design_db(name, def_path, lef_paths, lib_db, settings, alpha)`（总流程 §5 API 表 + 裁定 18；design db 配置键表见本文 §6）；内部 S2/S4（每文件一并行任务 + 全局汇总）、S5a（每 DEF 两并行任务）、S5b（每 DEF 一任务，内部分批）为**并行任务形态，无业务合并，不使用 MapReduceJob**；S9 为两级任务（展开任务按 block 定义切分、小 DEF 按阈值聚合 + 每分区一合并任务——分区侧有真实合并语义）；S1/S3/S6/S7/S8/S10 为直接任务链；冻结任务依赖全部产物对象。
- 消息注册：`DSGN::NNNN`（D21），典型条目——重复 macro 抛弃 / lef-lib 不匹配 / 悬空 port / UNPLACED 跳过计数 / 引用缺失 raise 前的最后报告。
- 测试策略：单测（结构化断言 + 序列化往返 + 兜底语义，同 lib_parser_test 模式）+ QA e2e（多 block 层级小设计 → 建库 → 分区 → 校验断言，含并查集归并/global id 区间/密度守恒）。

---

## 12. 附属可视化

[design-db-flow.html](design-db-flow.html)——单文件交互页（无外部依赖，浏览器直接打开）：
1. 十阶段流水线依赖图（点击阶段展开：输入/产出/并行度/风险）
2. 层级树与全局编号分配示意
3. 密度图 → 分区切分示意（热力网格 + 切线 + (xp, yp) 编号）
4. 左下角主分区判定规则（跨边界图形归属演示）
5. 并查集跨块归并示意
6. 数据实体关系总览

---

## 13. 维护约定

- 本文是 design db 立项前的**需求与流程评审稿**；审查裁定后：裁定条目（D1-D23 的裁定结果）回迁 [emir-data-flow.md](../emir-data-flow.md) §4（延续其裁定编号），实施细节归 `docs/emir/design/module.md`（立项后建立），本文保留作决策记录。
- 附属可视化与本文内容须同步修改。
