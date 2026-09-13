# design db 第二阶段方案：审阅反馈落实（重构 R 批次）+ S5a/S5b（责任链 + transform）

> **定位**：对已实施 S1-S4b 的九条审阅反馈逐条确认与落实方案（重构批次 R1-R6），及下一阶段 S5a/S5b（instance 解析责任链 + transform 结构）的设计方案。**等待审阅，未动代码。**
> **创建**：2026-09-10。裁定编号延续 [design-db-plan.md](design-db-plan.md) 修订头（㉔-㉜）。

---

## 0. 九条反馈逐条确认（㉔-㉜）

| # | 反馈 | 确认与方案 |
|---|------|-----------|
| ㉔ | geometry 独立模块，不在公共层之下，后续图形处理持续增加 | geometry 从 `src/container/geometry/` 迁出为**顶层独立模块 `src/geometry/`**（R1）。接入方式同 lefdef：纯 C++ 库（cc_library，无 Python 绑定、无模块注册六步），消费方 deps 引用；图形处理（布尔/裁剪/变换等）后续在此模块持续生长 |
| ㉕ | 业务结构不可放公共模块——CMGeometryRef 含 layer id 属业务 | CMGeometryRef 从 geometry 模块**删除**，迁移至 design 业务侧（R1）：更名 **DSShapeRef**（DS 前缀合规，含 layer id + 矩形引用，后续扩展多边形变体）。geometry 模块只保留纯几何（Point/Rect/Polygon，无任何业务语义） |
| ㉖ | layer/stack 创建后 id 化：layer 显式保存 id，stack 提供按 id 查找接口 | DSLayer 增 `id_` 字段（构建期由 stack 分配回填、随序列化持久化——不再依赖 vector 下标隐式约定）；DSStack 提供 `layer_by_id(uint32_t id)`（现 layer_at 语义正名）+ 保留 `find_layer(name)`（R2） |
| ㉗ | CM_FLAGS 宏：单 flags_ 变量按位记录一组 bool，`CM_FLAGS(int, fake_cell, std_cell, lef_cell, lib_cell, macro_cell, block_cell)` 展开为逐 flag 的 is_xxx()/set_xxx()/reset_xxx() + 整体 reset_flags() | 公共工具宏，与 CM_PROPERTY 同库（common/types，纯头）；实现复用 FLY_SERIALIZE 已有的变长参数展开基建（serialization_macros.h 的逐参展开机制，需补带索引版本）——实施细节 R3（含单测：位独立性/底层类型可换 int/uint32/uint64/reset 语义） |
| ㉘ | Cell 与 Pin 类型**全库唯一**——lib db 与 design db 共用同一类型，不同场景填不同数据；否则 merge 难维护、DSDesign 存两份、取数据传两份 | **已被 P1 裁定（2026-09-10）部分撤销**：保持现有结构——lib db（LIBCell/LIBPin）与 design db（DSCell/DSPin）类型不合并；改为 DSCell 的 pin 数据三字段按 pin 维度组织（基础 pin + lib 侧含表 pin + lef 侧含图形 pin，键 = 全局 pin_id，R4 重写见 §1） |
| ㉙ | port 直接复用 pin 结构、block 直接复用 cell 结构；用 CM_FLAGS 标记来源与种类 | DSPort/DSBlock 类型**删除**（R5）：port = DSPin + flags（`is_port()`）；block = DSCell + flags（`is_block_cell()`），block 专属信息挂 cell 字段——DIEAREA → cell.bbox_（GEORect 包围盒，见 ㉞）+ def_path_ 字段、def_units 保留在 block 场景字段；ports 即 cell.pins_。cell flags 集即 ㉗ 示例（fake_cell/std_cell/lef_cell/lib_cell/macro_cell/block_cell）；pin 侧 flags（port/normal）同理 |
| ㉚ | VIARULE 不单独保存——按描述直接生成具体形状，复用 via cell 结构 | DSViaRule 结构与 collector **删除**（R5）：LEF VIARULE（GENERATE 型）解析时按规则默认参数直接展开为中心对齐的模板 DSViaCell（cut/enclosure 几何生成，同 T5 的 DEF 生成式展开逻辑）；DEF 引用处带参数的生成式 via 已按参数直接展开（T5 现状保留）。via cell 权威表成为唯一 via 形态存储 |
| ㉛ | DEF 解析业务处理组织为**处理链/责任链**：一份 instance 数据顺序流经链节点，每节点处理相应部分并收集信息（节点1：cell 存在检测 + name→id + instance 生成；节点2：密度图与统计；新功能=加节点） | S5a/S5b 的 instance/net 处理采用 C++ 责任链（设计见 §2）；已实施的 S4 头扫描（port/via 定义收集）处理量小，**不强制回迁**（S5a 起统一链模式，S4 后续视复杂度决定） |
| ㉜ | transform 结构：cell origin 可能非 (0,0)，instance 旋转后的实际放置点需谨慎处理（flatten 也用）；**instance（std cell 与 block cell）坐标始终保存「经过旋转的 origin 点」**；放置点+旋转方向可直接叠加成新 transform；内部结构（std cell 的 pin 图形、block 内部其他 instance 与自身 port pin 图形）相对位置计算真实全局坐标时，直接用「放置且旋转后的 origin point + 最终旋转方向」生成 transform 处理；结构需仔细设计、结合 lef/def reference 文档、给出使用示例并制作可视化效果图 | **最终形态（2026-09-10 用户裁定）**：instance 存 `pos_`（cell **原始坐标系 (0,0) 点**的全局位置，「经过旋转的 origin 点」的精确化）+ 最终 orient；cell 不归一化（几何原坐标）+ `origin_`（LEF=ORIGIN 值 / block=−diearea_ll，统一语义=放置参考点负偏移）+ box（=[−origin_, −origin_+(W,H)]）；DEF placement t → pos_ 经放置边界换算一次；链式复合纯二元组（pos/orient）、零修正项（结构见 §3.1）；公式已按官方 Reference 5.8 核对（§3.5.2 矢量级验证）；使用示例 §3.3；可视化效果图 [design-transform.html](design-transform.html)（R6 同步更新）+ 两张八方向数值图（§3.5.6，图中红点即 pos_） |
| ㉝ | unit 全局统一为 1000；DEF 解析后坐标信息全部转换为统一 unit | **全局 DBU 基准固定 = 1000 DBU/µm**（不再跟随 tech lef 的 DATABASE MICRONS 声明值）：DSStack.dbu_per_micron_ 恒 1000；各 lef/def 按各自声明的单位换算到统一基准（lef 2000/4000、def 100/200/2000 均换算）；lef 间 DBU 不一致不再 raise（各自换算即可，D15 该子项撤销）；实施变更：R2 批次一并改（换算函数基准参数化） |
| ㉞ | DIEAREA 按 polygon 方式存储；cell 增加 is_polygon flag，提供返回 polygon 图形的 API；正常流程均使用 rectangle 边框做业务处理；DSCell 同时保存两个形状——polygon 真实图形 + bbox 最大外边框 | DSCell 增 `polygon_`（GEOPolygon<int32_t>，DBU）字段与 `is_polygon` flag（CM_FLAGS 位）；`get_polygon()` API 返回真实图形；**bbox 直接以 `GEORect<int32_t>` 存储（含左下角坐标，`bbox_` 字段）——现 width_/height_ 删除**：尺寸字段无法表达左下角非 (0,0) 的外边框（如 die 中心原点 DEF 的 DIEAREA (−2500,−2500)–(2500,2500)），而 bbox 可无歧义反推 width()/height()（GEORect 现成接口）；S4 适配层 DIEAREA 从「仅聚合 bbox」改为「polygon 全点集 + bbox 双存」；密度/分区等业务流程一律用 bbox（polygon 仅供精确几何场景）；**坐标类型裁定（2026-09-10）：业务图形一律 int32 实例化**——全局基准 1000 DBU/µm 下 int32 可表达 ±2.15 m，光罩极限芯片（26×33 mm，坐标 ≤3.3×10⁷）余量 65 倍；换算/面积/复合等**中间量必须 int64/double**（见 design-knowledge.md 坐标范围节） |
| ㉟ | 嵌套 DEF 场景：子 def cell 的 diearea 最大外边框左下角非 (0,0) 时，子 def cell 实例化的 instance 如何放置？结合 lefdef reference 文档给出结论，最好结合工业实际用例，或找开源版图查看工具确认正确行为；若能找到开源版图查看工具，顺带验证 transform 设计正确性 | **调研完成（结论见 §3.5）**：①官方语义 = placement 点对齐「变换后放置包围盒左下角」、宏先 +ORIGIN 平移、DIEAREA 多点即 polygon、Example 4-19 证明 die 中心原点合法形态；②八 orient 公式经官方图示**矢量级核对**逐点确认（FW=先 MX 后逆时针 R90=(y,x)、FE=先 MY 后逆时针 R90=(−y,−x)），OpenDB 读入侧源码数值对账完全一致（命名交叉警示：锚定数学非名字）；③嵌套 DEF（无 LEF 中介）放置 = 子 block 按「(0,0) 起放置包围盒」归一化后走标准 transform——fly 落地为 S4 合成 block cell 的 diearea_ll 归一化（裁定点 P7）；④KLayout 渲染实测未执行，由三重独立证据等效覆盖（§3.5.5） |
| ㊱（2026-09-11，同日分层细化） | **name 存储分层**（用户裁定）：数量较少的 **layer / cell / via cell 可同时保存 name 和 id 于自身结构**（双存允许）；**instance / net 必须全程用 id 处理**（name 仅在 per-DEF local mapper）；**pin 因不同 cell 同名 pin 过多，自身不存 name 仅存 id**（name 仅在全局 mapper） | 分层依据 = 数量与访问模式：①DSLayer/DSCell/DSViaCell 保留 name_ 字段（现状不动，十万级以内、name 直接可得）；②**DSPin 删 name_**（仅全局 pin id + DSDesign pin namemap，键 `cell_name/pin_name`）；③**DSInstance 删 name_** + **DSBlockBuildData 新增 instance 名 ↔ local id 双向 mapper**；net 现状已合规（DSNetBuildData 无 name、local namemap 唯一）；④内部业务处理全程以 id 为键（merge/责任链/汇总/区间分配），name 仅在解析边界（文本名注册进 mapper 得 id）与用户可见输出（DSGN 消息格式化、Python 查询 API）经 mapper 转换；⑤pin/instance 的 get_name() 调用点与导出面属性改经 mapper 查询。实施批次 **R7**（串行于 geometry 别名任务之后） |
| ㊲（2026-09-11，定稿） | **所有 name mapper 共享同一个底层结构，只有保存数据类型不同的区别；且 mapper 必须一律双向**（用户裁定，含㊱执行形态） | **name mapper 仅 design db 存在**（lib db 无 mapper 概念）；**通用模板底座 `DSNameMapperT<IdT>`**（design 模块，㉕ 先例：含业务语义结构不进公共模块）内含 `name_to_id_`（map）+ `id_to_name_`（vector，容忍空洞）成对双向 + emplace/assign 双写登记 + get_id/get_name 查询 + FLY_SERIALIZE；**按实体位宽分组实例化 + 实体语义别名保证可读性**（别名不带位宽标识）：32 位组（uint32_t）`DSCellNameMapper`/`DSPinNameMapper`/`DSViaCellNameMapper`，64 位组（uint64_t）`DSInstanceNameMapper`/`DSNetNameMapper`——差异 = 所存 id 的实体种类与位宽组，由别名表达。迁移面：DSDesign 三套（cell/pin/via cell）+ DSBlockBuildData 两套（instance/**补 id→name 反向——现状单向违规必须修复**/net 双向）全部换实体别名 mapper；fake_name_to_id_ 为 block 级便捷子集索引非完整 mapper，保留并注释。序列化布局无兼容负担（工作区未 commit，P6 一次提交）。与 ㊱ 合并为 R7 实施。**㊸ 放宽**：instance/net 场景区别大，改用专用独立结构、不强求与 32 位组同构（见 ㊸） |
| ㊳（2026-09-11，定稿） | **instance 数量 int32 不够，换 int64；mapper 有通用模板底座，pin/cell/layer 用 int32 实例化、instance/net 用 int64 实例化，均用别名保证可读性**（用户裁定） | **id 位宽按实体数量级分组**：32 位组（uint32_t）——cell id / pin id / via cell id / layer id（十万级以内，现状不动）；64 位组（uint64_t）——**instance id（local/global）、net id（local/global）、via instance id、DSHierNode 的三类区间与 self_global_id、block instance id**（instance 空间，大芯片可达 10⁹ 级超 32 位）；mapper 实例化分组见 ㊲（DSNameMapperT 底座 + 实体别名）。**坐标不受影响**（int32 裁定独立——坐标是物理量受芯片尺寸约束，id 是数量受实例数约束）。并入 R7 实施（与 mapper 改造同片代码） |
| ㊴（2026-09-11） | **提供一组 API 判定 id 是否有效：name mapper 查询不存在的 name 返回无效 id，暂定所有无效 id = id 类型的最大值**（用户裁定） | `DSNameMapperT<IdT>` 底座统一提供：`static constexpr IdT kInvalidId = std::numeric_limits<IdT>::max()`（32 位组 = 0xFFFFFFFF、64 位组 = 0xFFFFFFFFFFFFFFFF——**有效 id 空间因此排除 max 值**，与空洞容忍不冲突：fake cell 稀疏落位远低于 max）+ `get_id(name)` 未命中返回 kInvalidId（返回值哨兵语义，替代迭代器风格）+ `static bool is_valid_id(IdT)`；现有哨兵 `DSDesign::kInvalidId`/`DSStack::kNoLayer`（均已是 UINT32_MAX）收编到底座口径、值不变注释关联。层级树反查未命中（导出面已 None）内部同用哨兵。并入 R7 |
| ㊵（2026-09-11，待确认） | ①flatten 后的全局 instance/net name → 全局 id 转换缺失（当前 mapper 仅 block 内 local 转换），**结合 hierarchy tree 设计组合查询**；②is_valid_id 须在不加载 namemap 时可用 | ①**组合查询、不做全局大表**（百万级 × 长路径名爆炸 + 违背 ④ 单份存储）：DSHierTree 提供 `global_instance_id(hier_path)`/`global_net_id(hier_path)`——拆路径 → 自根按实例名逐层找子节点（DSHierNode 已存实例名）→ 叶层 local mapper 查 local id → + 区间 start（local 0 → self_global_id ⑧）；反向 `hier_path_of(global_id)` 同构（区间反查 + 递归向上拼路径）；代价 O(层级深度)。②**is_valid_id 为 static 纯值哨兵判定**（零状态零依赖，与 mapper 加载正交——需要 mapper 的只有 name↔id 转换）；配套架构要求：**per-DEF local namemap 伴生对象化**——`DSBlock_<i>`（instances/density/stats）与 `DSBlockNames_<i>`（instance/net mapper）分开落盘、业务按需加载其一（对齐 ⑰/⑱ 按需加载原则；当前单对象原子加载使「不拿 name」不可实现）；全局 mapper（cell/pin/via，量小）维持挂 DSDesign 容器。落盘布局与 API 面变化并入 R7 |
| ㊶（2026-09-11，调研结论 + 设计待裁定） | **调研 FST 算法（类似前缀树），尝试前缀树 + 字符池压缩 name mapper 内存**：业务中大量 instance/net 名前缀相似（总线/层次/系统性前缀），牺牲一定 name→id 性能换内存，id→name 几乎不受影响（用户提出） | **选型：radix tree（压缩前缀树）+ char arena，非 FST**——FST 共享前后缀压缩率最高（Lucene 同款）但**构建后不可变**，与我们的增量注册模式冲突（解析回调逐名注册 + merge 并入/重挂，每次增量需全量重建）；radix 可增量、前缀共享（EDA 名字主要冗余：`data_out[0..1023]`、层次前缀、`U123`/`_123_` 系统名）已拿到收益大头，后缀共享占比小。**设计**（接口不变、业务零感知——底座统一红利）：内部 = char arena（连续池消堆分配）+ radix 节点池（下标链接无指针）+ id→name 走 `vector<{池offset,len}>`（O(1) 下标，不受影响 ✓）；name→id 树遍历 O(len) 慢于 hash 2-5 倍（仅解析边界与用户查询，非热路径）；序列化自定义线性落盘（池 + 节点池）。**内存预估**：百万级名双份现状 ≈100-150MB → 3-5 倍压缩。**实施分层**：R7 底座先常规实现（unordered_map+vector）定死接口与迁移；**R8 单独批次树化内部替换**（接口不变 + 真实数据基准：内存/查询对比 + 随机名集与参照实现的语义等价单测），失败不阻塞 R7。FST 留作 S5a 汇总点批量构建的后续备选 |
| ㊷（2026-09-11） | **cell/pin/layer id 通常量很小，保持现在的简单 map+vector 结构即可；树化主要优化 instance/net 场景**（用户裁定，细化 ㊶ 实施范围） | R8 树化范围收窄至 **64 位组（DSInstanceNameMapper/DSNetNameMapper）**；32 位组（cell/pin/via cell mapper）维持 unordered_map+vector 简单实现不树化。~~实现形态：模板特化~~ **㊸ 修正：专用独立结构**（见 ㊸） |
| ㊸（2026-09-11，定稿；命名补裁定×5 同日：实体别名清单含 via cell） | **instance、net 场景区别很大，使用专用的结构即可，不强行要求全部一致**；cell/pin/layer 用当前结构改名 **NameHasher**、instance/net 切换新结构 **NameMapper**；两者均为模板（id 类型参数）+ T 后缀；**实体语义别名清单**（用户钦定，via cell 已补） | `template<IdT> DSNameHasherT`（hash 实现）与 `template<IdT> DSNameMapperT`（新结构，R8 树化）+ 六实体别名：`DSNetNameMapper = DSNameMapperT<uint64_t>`、`DSInstanceNameMapper = DSNameMapperT<uint64_t>`、`DSCellNameHasher = DSNameHasherT<uint32_t>`、`DSLayerNameHasher = DSNameHasherT<uint32_t>`、`DSPinNameHasher = DSNameHasherT<uint32_t>`、`DSViaCellNameHasher = DSNameHasherT<uint32_t>`（用户确认补上）。**DSLayerNameHasher 入列** = layer 的 name→id 查询收敛进 hasher 体系（DSStack.layer_index_ 惰性索引改为序列化的 hasher，layer 量小无负担）；两组接口语义对齐（get_id/get_name/kInvalidId/is_valid_id/双向/空洞容忍）；别名不带类型标识。㊲ 放宽为「接口语义对齐、按场景两种模板 + 实体别名」 |
| ㊹（2026-09-11，架构终局） | **DSInstanceNameMapper/DSNetNameMapper 仍是 block 级查询结构，也改名 hasher；最外层支持 global id 查询的结构叫 `DSNameMapperT`——真正的 name mapper：内部加载全部 block 的 hasher，按 hierarchy tree 找到最底层 block hasher 做 local id ↔ leaf name 转换，与 global id offset / hierarchy block name prefix 组装，得到 global id / full hierarchy name**（用户裁定） | 三层职责：①**Hasher 家族**（block 级 local 查询）——`DSInstanceNameHasher = DSNameHasherT<uint64_t>`、`DSNetNameHasher = DSNameHasherT<uint64_t>`（64 位 local id，R8 树化内部）+ 既有四枚 32 位 hasher（cell/pin/layer/via cell）；②**`DSNameMapperT<IdT>` 全局结构**（真正的 name mapper）——持有全部 block 的 hasher 集 + hierarchy tree 引用：`get_global_id(full_hier_name)` = 拆路径 → 树逐层定位 block instance 节点 → 叶层 hasher.get_id(leaf) → local id + 区间 start；`get_full_name(global_id)` = 区间反查 → 叶层 hasher.get_name → 递归向上拼 block name prefix（= ㊵① 的树组合查询落地为此结构，合并）；③实体别名复用——`DSInstanceNameMapper`/`DSNetNameMapper` = `DSNameMapperT<uint64_t>` 的全局 mapper（按 instance/net 维度构造注入各自 hasher 集）。IdT = uint64（global 空间）。伴生对象化（㊵②）适配：hasher 集随 DSBlockNames_<i> 落盘按需加载，DSNameMapperT 组装时汇总持有。**边界（㊺，用户澄清）**：DSNameMapperT 仅服务 instance/net 两维度（唯它们有 local id 与层级组装需求）——cell/pin/layer/via cell 的 id **天然全局、无 local id 概念**，其 hasher 即完整查询结构，与 DSNameMapper **无任何关联** |
| ㊻（2026-09-11） | **name mapper 本身不保存 hasher，落盘也不序列化 hasher；使用时单独读回 hasher，经接口传入设置——仅需部分 block hasher 的场景极大减小内存**（用户裁定） | DSNameMapperT 为**注入式轻壳**：持有层级树引用 + **注入表**（按 block/def 标识 → hasher，运行时按需注入部分或全部）；`set_block_hasher(block 标识, hasher)` 注入接口——**block 标识 = cell id 或 cell name**（block 与 DSCell 同构，㊙/R5；提供两形态重载或以 cell id 为主 + name 便利口）；查询时叶层 hasher 未注入 → 返回 kInvalidId / 空名（局部注入 = 局部可查）；FLY_SERIALIZE 不含 hasher（mapper 自身状态极轻，甚至可运行时构造不落盘）；hasher 随 `DSBlockNames_<i>` 伴生对象独立落盘（㊵② 定稿）、读回后注入。与 ⑰/⑱「用户需要什么专门加载什么」原则同构——按需加载体系完成形态。**注入语义细化（2026-09-11 用户纠偏×3：审查消除 copy/move）**：注入表为 `block 标识 → CMSharedPtr<const DSBlockNames>` **伴生对象级共享注入**（方案 B）——hasher 保持值成员（序列化零变化），查询经 const 引用访问；全链路（落盘→read_object→注入→查询）**零数据 copy、零 move**（值成员交出 hasher 级所有权在 C++ 不可能不 copy/move——伴生对象级共享是唯一自然闭合，注入粒度 = 按需加载粒度）；业务层**不可使用裸指针**（即使非拥有观察；裁定 ㉒），全程 CMSharedPtr；hasher 全程只读（查询接口全 const），read_object **默认缓存语义**（禁 cache=none 解法） |
| ㊾（2026-09-11，调研修正版） | **shared_ptr 序列化：调研确认 bitsery 原生能力后接线**（用户裁定先调研） | **bitsery 5.2.4 原生支持智能指针序列化**（本地源码核验：`ext/std_smart_ptr.h` 的 `ext::StdSmartPtr`——shared/unique/weak_ptr 通吃、空指针原生处理、SharedOwner 共享所有权、多态支持）——此前失败是 **FLY_FIELD 分派未接线**而非库能力缺失。增强 = 分派链前置 `is_shared_ptr` 分支直接 `s.ext(fly_v_, bitsery::ext::StdSmartPtr{})`（不手写 bool+make_shared）；单测：空/非空 round-trip、嵌套、CMSharedPtr 别名、共享拓扑行为确认（同一对象两字段引用 round-trip 后是否仍共享——实测记录语义边界）。解锁：hasher 字段 CMSharedPtr 化（A）与伴生对象级注入（B）均零 copy 零 move |
| ㊿（2026-09-11） | **三条裁定**：① shared_ptr 序列化必须支持；② 共享所有权场景必须使用 shared_ptr 传入 cpp 侧；③ read_object 无论写出类型是 shared_ptr 还是值，均返回 shared_ptr（使用侧无需知晓写出侧形态） | ①=㊾ 接线（R7 实施中）；②入 DEVELOPMENT_GUIDELINES Section 2.8（「必须 CMSharedPtr 传入，禁值/copy 传递共享对象」）；③**现状已满足**——`Database::read_object` 模板签名即返回 `CMSharedPtr<T>`（database.h），读回统一解到 T 后经缓存/构造以 shared_ptr 返回，形态屏蔽契约已在现网，规范固化之（Section 2.8 同步） |
| 52（2026-09-12） | **R8b 选 A（hat-trie），但先做抽象包装层——未来遇见更佳选择时能快速替换底层实现**（用户裁定） | **编译期模板策略**（非虚函数接口——查询热路径零开销）：`DSNameHasherT<IdT, BackendT = DSHasherBackendHatrie<IdT>>`——Backend 概念 = name→id 索引操作的契约（增量 insert(name,id)/find(name)→id/遍历导出），实现可替换（htrie 首个实现；未来 marisa 等按同一概念新增）；**替换点 = 别名单处**（DSInstanceNameHasher/DSNetNameHasher 定义处换 Backend 参数）。id→name 侧（char arena + {off,len} 偏移表）为通用件放 hasher 本体（不进 backend，换 backend 不动）；**序列化取 backend 无关的通用名集格式**（arena+偏移+计数，读回重建 backend）——换 backend 不破坏既有 db 文件、放弃 backend 原生持久化的极致（可接受）。**替换性验证**进单测（极简 dummy backend 实例化编译+基本断言，证明抽象面完备）。R8a 数据存档：hat-trie 1.98x 压缩/1.08x 查询劣化/增量支持/MIT；自建 FST 否决（0.70x 反超+1.75GB 构建峰值）；报告 .work/bench_name_mapper/REPORT.md。**R8c 定型补记（2026-09-12，双段制定型）：序列化定型为双段制①（权威段 = backend 无关通用名集格式 + 加速段 = backend 原生字节流；读回优先直载，空段/段被拒/计数不符 → 权威段重建兜底——上方「放弃 backend 原生持久化」的暂时形态就此升级）**，`native_cache_on_save_` 默认 true 保持（已裁①）。定形数据 = 客户分布（CUSTOMER_DIST.md）：直载读回 **12.5x 提速**（100 万名 2406→193 ms，重建段 95% 成本消除）、重建成本对名长**超线性敏感**（同 100 万名仿真→客户分布重建 698→2406 ms，长簇触发 burst 重排）；客户 IO 环境（300 MB/s-1 GB/s）持平点模型：直载每名 ≈ 0.19 µs 解析 + 加速段 ≈ 53 B/名磁盘读（0.18 µs@300 MB/s 下限），vs 重建每名 0.65-2.4 µs 且随规模超线性——**≥1 万名直载恒优**，以下两形态差异可忽略；+70% 落盘为一次性明确代价。arena LCP 后缀共享为后续可选项（独立基准已定案，见 54 行——容量压力出现时排期，本批次不实施） |
| 53（2026-09-12） | **DSNameMapperT 的 block 分派加速采方案 B（全局 block 路径前缀索引 + 最长前缀下降），且必须经 backend 封装、与 hasher 复用同一份封装**（用户裁定，前台讨论定案） | 现状热点：find_child_by_instance_name 线性扫 children（O(扇出×段长)/层，顶层扇出数百~数千时反超叶层查询）。方案 B：DSNameMapperT 内建**分派索引** = `DSHasherBackendHatrie<uint32_t>` 直接实例（block 层次全路径 → 树节点 id，万级条目；**复用 backend 概念与实现，不需整只 hasher**——id→name 侧由 DSHierNode 双存承担，分派索引纯 name→id 视图）；**运行时从 DSHierTree 遍历重建、不序列化**（mapper 轻壳构件，㊻ 语义）；查询 = 回退式最长前缀匹配（find(全路径) 不命中逐段去尾——block 深度浅通常 1-2 次命中，全走 backend.find 概念接口）；顺带 split 逐段 CMString 分配无堆化（string_view 段）。**补充测试（用户裁定）：R8c 前后各测一轮完整 namemapper 查询路径性能**——get_global_id 正向全路径（split→分派→叶层 hasher→区间换算）与 get_full_name 反向全路径，指标 = **单次查询耗时（µs）与查询吞吐（QPS）**；场景矩阵 = 层级深度（浅/中/深，如 2/4/8 层）× 扇出（10/100/1000）× 名集规模（10万/50万/100万）；R8c 前后对比量化分派优化收益。实施排 R8 补测后（R8c 小批次，与双段制定型可合并委托）。**R8c 实施结果补记（2026-09-12，已实施）**：①分派索引 = `DSHasherBackendHatrie<uint32_t>` 直接实例（键 = block 层次全路径含 root 实例名段、值 = 树节点 id），构建时机对「显式 build」与「首次查询惰性构建」二选一取**构造/set_tree 即建**（最简：无需 mutable、无首次查询并发构建窗口、无换树忘重建静默错；`rebuild_dispatch_index()` 公开兜底树原地修改场景）；②查询收编为「剥叶段后前缀一次 find」——原语义固定最后一段必经叶层 hasher，裁定中「逐段去尾回退」在「尾段数 >1 不交 hasher」的语义保持规则下恒等于未命中，故省去回退循环（全走 backend.find 概念接口 ✓）；③split 无堆化落地为 find_last_of 分段（零逐段分配，至多两次短串构造）。**前后基准（18 格 = 深度 2/4/8 × 扇出 10/100/1000 × 名数 10 万/100 万，装置 src/emir/design/tests/ds_name_mapper_bench_test.cpp，正确性断言先行全 PASS）**：get_global_id 正向命中 p50 提速**最小 2.24x / 中位 4.57x / 最大 11.82x**（p99 最大 16.4x；扇出 1000 格 9-12x；未命中口径相当、F=1000 格 QPS 16-21x），优化后单次 1.1-1.5 µs 与深度/扇出/树规模解耦；get_full_name 反向零改动零回归（±5% 噪音带）。报告 .work/bench_name_mapper/DISPATCH_BENCH.md（原始数据 results/r8c_before.txt / r8c_after.txt）；单测 = 原 8 个全量回归 + 新增 5 个分派正确性（多层多扇出/同名兄弟防御保留首个/未注入叶层/非法路径/set_tree 换树重建） |
| 54（2026-09-12，基准定案） | **arena LCP 后缀共享独立基准**（.work/bench_lcp/，裸 g++ 原型三变体 × 三档 × 双 seed 全 PASS） | 数据（1M 名客户分布）：arena 实省 **63.7%**（59→21.4MB，超原估算 50-60%）、总结构省 52%、RSS 省 44.6%；**get_name 劣化**：随机 4.1x（672ns/次，亚微秒非热路径可接受）、**顺序（注册序）访问 27.8x**（id→rank 乱序跳转 cache 失效——实施时须配套 rank 序批量遍历路径）；checkpoint=64 定案（128 仅再省 0.5MB 换 36% 劣化）；封顶实测 max 63/均值 31.5 与理论一致；构建 +993ms@1M（排序主导，可与 trie 构建重叠）。**定案：值得实施、容量压力（10M 名级）出现时排期**；外推总驻留 150.8→~111MB@1M（再降 26%，接近 marisa 而无其查询劣化）；实施注意：回退拼接须逐级截断（尾部残留坑，报告 §4）、id→rank 与 checkpoint 纳入双段制直载格式 |
| 55（2026-09-12） | **LCP 后缀共享直接加入实现列表，经 alpha 设置做路径控制——由用户自行裁决性能/内存/磁盘取舍，并方便后续真实场景测试**（用户裁定，取代 54 的「容量压力触发」排期） | 实施批次 **R8d**（排 R8c 后，同片文件串行）：①DSNameHasherT 的 id→name 侧双形态（全名 arena 现状 / LCP 压缩：后缀 arena + 4B id→rank + 2B lcp + checkpoint=64）——**alpha 键 `lcp_name_arena`**（bool，默认 false 保守）经 build_design_db 传入、构建期决定形态；②落盘格式自识别（标记位区分两形态，读回按格式重建，双段制加速段兼容——id→rank 与 checkpoint 进直载格式）；③配套 rank 序批量遍历路径（按 sorted rank 序——避免注册序访问 27.8x 劣化，供批量导出/消息格式化使用）；④回退拼接逐级截断正确性单测（基准已捕获的坑）+ 两形态回归 |
| 51（2026-09-11，同日撤销） | ~~C++ 侧 read_object 返回 unique_ptr、Python 包装层转 shared~~ | **用户撤销**：C++ read_object 内部即有实例缓存（ObjectCache high 层，命中返回共享实例——T4 保留裁定），与 unique 独占语义不兼容——**维持 `CMSharedPtr<T>` 返回现状**（㊿③ 形态屏蔽契约不变），R10 取消不实施 |
| ㊼（2026-09-11） | **ds_functions 类 API（内部 read_object）必须 wait_obj 包装；调用方为 task 时必须在 as_task 增加相应数据依赖；为防依赖漂移，支持 lambda 依赖声明 + `deps()` 传播 + `run_direct` 剥离直跑**（用户裁定，含代码形态） | 问题：裸 read_object 在调用方漏声明依赖且数据未就绪时直接读取失败、整流程失败。方案：①框架层（src/task/py/task.py）——wait_obj wrapper 增 **`deps(*args, **kwargs)`** 方法（返回 inputs lambda 解析后的依赖列表，inputs 为 None 返回 []）；新增 **`run_direct(func, *args, **kwargs)`** 公共 API（func 有 `_fly_original_func` 则直调原函数、否则直调本身——剥离本地等待避免走 wait_obj 轮询/master 查询的冗余网络 IO；仅适用 wait_obj 包装的本地 API，as_task 任务函数不适用）——wait_obj 的 inputs lambda 与 `_fly_original_func` 引用**现状已具备**，均为小增量；②ds_functions.py 的 load_design/load_design_stack/load_design_with（及 R7 伴生加载新 API）一律 wait_obj 包装：`@wait_obj(inputs=lambda db: [db.get_full_name(...)])`；③调用规范（**已入 DEVELOPMENT_GUIDELINES.md Section 17**，全仓库业务代码编写规范）：task 内调用这类 API 用 `as_task(inputs=lambda db: api.deps(db) + [...自身依赖])` + 函数体 `run_direct(api, db)`；④框架单测：deps 解析正确、run_direct 不触发等待（monkeypatch _wait_for_objects 断言零调用）、返回值与直调一致。实施批次 **R9**（串行于 R7 后——ds_functions 正是 R7 伴生加载 API 改造对象，避免同文件并发） |
| ㊽（2026-09-11） | **FST 批量构建可行（用户提出）：解析阶段先收集全部 name（id 正常递增），per-DEF 的 instance/net name 解析完毕后统一建 FST；需确认收益——测试查询速度与内存占用** | **流程自洽性已论证**：S5a COMPONENTS 是登记不反查（阶段末建 FST 无障碍）；S5b 的全部名查询（连接查 instance、网对齐查 net）查的是 S5a 已完成产物（跨阶段查上游 FST）；同任务内自查未建成 FST 无场景（via instance 无名、S5b 按 S5a 已分配 local id 对齐）。**R8 拆两步**：R8a 基准实验（原型对比三方案——①现状 unordered_map+vector+string 堆 ②radix tree+char arena ③FST+arena（自实现最小构建原型）；数据 = 仿真 EDA 名集百万级（层次前缀/总线 `[0..1023]`/系统名 `U123`/`_123_` 模式）+ 真实 def 名提取；指标 = 构建时间/内存占用/name→id 查询速度（随机命中+未命中）/id→name 速度；纯 .work 原型不进 src、不走 bazel（可与 R7 并行））→ 依数据由用户裁定 R8b 落地形态（radix 或 FST） |

---

## 1. 重构批次（R1-R6，作用于已实施代码）

### R1 geometry 独立模块 + DSShapeRef 业务化（㉔/㉕）

```
src/geometry/                     # 顶层独立模块（同 lefdef 接入方式：纯 C++ 库）
│                                 #   前缀独立（2026-09-10 用户裁定，前缀=所属模块标识、全大写）：GEO，与 common 的 CM / design 的 DS / lib 的 LIB 并列
├── BUILD                         # cc_library fly_geometry（纯头；无绑定无注册六步）
├── cpp/geometry_types.h          # GEOPoint/GEORect/GEOPolygon（纯几何，删 CMGeometryRef；迁移时 CMPoint 等同步改名）
└── cpp/transform.h               # GEOOrientation（8 方向，值=Si2 DEF_ORIENT_*）+ GEOTransform<T>{point+rotation} 模板
```
- 迁移：container/geometry → src/geometry（git mv 保历史）+ **类型改名 CMPoint/CMRect/CMPolygon → GEOPoint/GEORect/GEOPolygon**（全量引用点：ds_types.h/适配层/单测/QA）；design 的 deps 改 `//src/geometry:fly_geometry`。
- GEOTransform 新增（P5 已裁定下沉，2026-09-10）：**模板类型**（T ∈ {int32_t,int64_t,double}）+ apply/apply_box/**apply_polygon**（多边形逐顶点变换，用户裁定必须支持）/compose + 序列化；单测锚定 §3.1 纯旋转表 + §3.5.2 八方向官方图示数值 + compose 复合表（OpenDB orientMul 同源）+ 两张八方向配图的 pos/框角点全量期望值（§3.5.6）。
- DSShapeRef 落 design（ds_types.h）：`{ uint32_t layer_id_; GEORect<int32_t> rect_; }` + 预留多边形变体扩展点；全部原 CMGeometryRef 引用点（DSPin 几何/obs/port 几何）改 DSShapeRef。
- container/geometry 目录与 BUILD 删除；geometry 单测随迁 src/geometry/tests/。

### R2 layer id 化（㉖）

- `DSLayer` 增 `uint32_t id_`（DSStack::add_layer 分配回填；FLY_SERIALIZE 增列）。
- `DSStack::layer_by_id(uint32_t)`（断言越界，正名现 layer_at）；`find_layer(name)` 保留。注释更新：id 显式持久化，下标巧合一致但不再作为约定。

### R3 CM_FLAGS 宏（㉗）

- `src/common/types/cpp/flags_macro.h`（与 property_macro.h 同库）：

```cpp
// 用法：CM_FLAGS(int, fake_cell, std_cell, lef_cell) —— 底层 int，按位存储，
// 每个 flag 生成 is_fake_cell()/set_fake_cell()/reset_fake_cell()，
// 另生成 reset_flags() 清空全部。底层类型可换（uint32_t/uint64_t）。
#define CM_FLAGS(UnderlyingT, ...)   \
    UnderlyingT flags_ = 0;          \
    void reset_flags() { flags_ = static_cast<UnderlyingT>(0); } \
    CM_FLAGS_FOR_EACH(UnderlyingT, __VA_ARGS__)
```
- `CM_FLAGS_FOR_EACH`：逐参展开带位号（第 n 个 flag 的 mask = `UnderlyingT(1) << n`）——复用/仿照 serialization_macros.h 的变长参数展开机制（该处 FLY_SERIALIZE(a,b,c,...) 已实现任意字段逐参展开，需扩展为带索引版本；上限 32/64 flag 由底层类型决定，越位编译期报错）。
- 单测：位独立性、is/set/reset 语义、reset_flags 清空、底层类型替换、与 CM_PROPERTY 共存（flags_ 自身经 CM_PROPERTY(flags) 暴露）。

### R4 DSCell pin 三字段按 pin 维度组织（P1 裁定，2026-09-10；原「Cell/Pin 全库统一」撤销）

- **保持现有结构**（P1 裁定）：lib db（LIBLibrary/LIBCell/LIBPin）与 design db（DSDesign/DSCell）类型**不合并**——原 ㉘「全库唯一类型」重构撤销；merge_lib 消费 LIBLibrary 的接口不变。
- **DSCell 的 pin 数据三字段**（逻辑视图，用户裁定）：
  1. `pins_`（基础且公共）：DSPin（name/direction/use + P3 的 placement_status_）**+ 新增全局 `pin_id_` 字段**（与 namemap 的全局平铺 id 同源）；
  2. **lib 侧 pin 信息（含表）**：DSPinTables 索引键从 cell_id 改为**全局 pin_id**——`internal_power_tables_[pin_id]`、`timing_tables_[pin_id]`（条目仅含该 pin 自己的功耗表/时序弧表，不再整 cell 混装一个 vector）；
  3. **lef 侧 pin 信息（含图形）**：DSPinGEOmetry 索引键从 cell_id 改为**全局 pin_id**——`pin_geometry_[pin_id]` = 该 pin 的 DSShapeRef 图形集（block port 几何同样挂 port 的全局 pin id）。
- **检索 API**：`tables_of(pin_id)` / `geometry_of(pin_id)` 直接命中（用户痛点「无法通过 pin id 获取对应的表/图形」的修复）；DSDesign/DSCell 提供便捷视图（cell → 其 pin id 列表 → 各侧数据）。
- 序列化不变：基础 pins_ 随 cell 序列化；表/图形仍为独立对象不序列化（P2=A，⑰/⑱ 维持）。
- 适配面：merge_lib 逐 pin 落位登记（S2 cell lef 几何/S4 port 几何同理）；原 cell 维度聚合接口降级为便利 API（遍历 cell 的 pin id 聚合）。

### R5 port/block 复用 + VIARULE 删除（㉙/㉚）

- DSPort 删除：port = DSPin（flags 增 `port` 位）；placement_status/几何字段——几何本就在 DSPinGEOmetry（按 cell id 组织，port 几何挂 block cell 的 pin 几何），placement_status 进 flags 或保留 pin 字段（P3）。
- DSBlock 删除：block = DSCell（flags 增 `block_cell` 位）；die_area → cell.bbox_（GEORect 直接存包围盒，左下角可非 (0,0)；不再用 width_/height_ 表达——尺寸由 bbox 派生，见 ㉞）；def_path_/def_units_per_micron_ 为 block 场景字段（flags 判别后使用）。DSDesign.blocks_ 表删除，block 查找走 cell namemap；block namemap（design_name→id）由 cell namemap 覆盖。
- fake_cell_ids_ 单独字段（⑳）：flags 有 is_fake_cell() 后，单独集合降级为**可选加速索引**（P4：建议保留，遍历加速零成本）。
- DSViaRule/DSViaRuleCollector 删除：LEF VIARULE GENERATE 解析直接展开模板 DSViaCell（默认参数中心对齐几何）；DEF 生成式展开保留现状；适配层与绑定面同步清理。

### R6 transform 业务接入（㉜ 最终形态；结构本体已随 P5 裁定移入 R1 的 geometry 模块）

- **GEOTransform/GEOOrientation 本体在 geometry（R1 实现）**：point+rotation 构造、apply/compose/transform_box/序列化 + 单测（锚定 §3.1 纯旋转表、§3.5.2 八方向官方图示数值、两张八方向配图的 pos/框角点全量期望值 §3.5.6）。
- R6 剩余为业务接入：defin 回调 orient 整型 `static_cast<GEOOrientation>` 直转（值同源零映射，P5 裁定）+ `t → pos` 放置边界换算（origin_/box 用一次）+ instance 字段（pos/orient）接入 S5a 责任链 + flatten 链式复合调用面。
- 可视化效果图：[design-transform.html](design-transform.html) 定稿更新（ORIGIN 符号、FW/FE 对比图按 §3.5 裁定；主表示改为 pos/orient 二元组）。

---

## 2. S5a/S5b：instance/net 解析责任链（㉛）

### 2.1 链结构（C++，语义下沉）

```cpp
// 处理链节点：一份 instance 上下文顺序流经各节点，节点处理并收集信息。
// 新功能 = 新增节点（在链中登记），不改既有节点。
struct DSInstanceContext {          // 链上传递的可变上下文
    // 阶段1 填充：cell id、local instance id、放置（旋转后 origin 点）、orient、weight、status
    // 后续节点追加收集：密度格命中、统计计数、（未来）几何展开缓存……
};
class DSInstanceHandler {           // 抽象节点
public:
    virtual ~DSInstanceHandler() = default;
    virtual const char* name() const = 0;
    virtual void handle(DSInstanceContext& ctx) = 0;
};
class DSInstancePipeline {          // 链组织：固定顺序执行
    CMVector<CMUniquePtr<DSInstanceHandler>> handlers_;
public:
    void add(CMUniquePtr<DSInstanceHandler> h);
    void run(DSInstanceContext& ctx);   // 顺序执行，节点可提前终止（错误标记）
};
```

### 2.2 S5a COMPONENTS 链（每 DEF 一任务）

链节点（首批，按 ㉛ 示例）：
1. **CellResolveNode**：master cell 名查 namemap——存在 → name→cell id；不存在 → **fake cell 生成（⑲/⑳：block_cell_name::cell_name 命名、max_cell_id+唯一值 id、1×1 矩形、is_fake_cell flag）**，ctx 填 cell id；
2. **InstanceBuildNode**：生成 local instance（id 从 1 起，local 0 = block 自身占位 ⑧）、填写基本信息（放置点存**旋转后 origin 点** ㉜——COMPONENTS 的 (x,y) 即 origin 落点，直接保存；orient、status、weight）；
3. **DensityNode**：按 instance footprint 与格子的交叠做图形计数（实例面积通道），逐格累加；
4. **StatsNode**：统计收集（per-cell 计数、fake/UNPLACED 计数等）。

（并行轨：NETS/SPECIALNETS 网名扫描任务不变——NetNameOnly。）

### 2.3 S5b 网内容链 + 批处理

- 网数据同理可组链（NetContext：连接解析→几何展开→拓扑保留→密度通道→落盘），**分批多阶段机制不变**（裁定 ③）。
- 链节点在批内复用（一次构建多次 run），批界落盘释放不变。

### 2.4 已实施部分的回迁策略

- S4（DEF 头扫描 port/via 收集）：处理量小、非 instance 流——**不强制回迁**（保持现状）；若后续 port 处理复杂化（如 port 几何展开进链）再迁。
- R 批次重构先行，S5a/S5b 在重构后基线上实施（依赖统一 cell/flags/transform）。

---

## 3. Transform 设计（㉜ 最终形态：结构 = geometry 模块的 GEOTransform，point + rotation）

### 3.1 结构与公式（2026-09-10 已按官方 Reference 5.8 完成核对，见 §3.5）

```cpp
// —— geometry 模块（src/geometry，㉔ 独立模块 + 2026-09-10 用户裁定：transform 为纯图形处理归此）——
//   GEOOrientation：8 方向枚举，**值严格对齐 Si2 defi 的 DEF_ORIENT_*（defiNet.h，2026-09-10 用户裁定）**：
//     N=0, W=1, S=2, E=3, FN=4, FW=5, FS=6, FE=7（FW 在 FS 之前——与 Si2 头文件一致）
//     defin 回调的 orient 整型直接 static_cast<GEOOrientation>，零映射零转换；
//     旋转/镜像数学定义见 §3.5.2（FW = 先 MX 后逆时针 R90 等）；OA 系名（R0/MX90/MY90…）不出现在代码，
//     仅作文档对照（与 OpenDB MXR90/MYR90 同值异名警示见 §3.5.3）
//   GEOTransformT<T>：**模板类型**（T ∈ {int32_t, int64_t, double}，与 GEOPointT/GEORectT/GEOPolygonT 实例化集合一致，
//     2026-09-10 用户裁定：以适应不同 geometry 坐标类型），offset 同为 GEOPointT<T>；orient 非模板：
//   GEOTransformT<T> {
//     GEOPointT<T>    offset_;       // 平移分量（业务语境 = cell 原始坐标系 (0,0) 点的全局位置）
//     GEOOrientation  orient_;       // 旋转分量（纯几何，D4 群元素）
//   };
//   业务别名（2026-09-10 用户裁定：模板类 T 后缀 + 无后缀业务别名，命名规则见 DEVELOPMENT_GUIDELINES.md §2.2）：
//     using GEOTransform = GEOTransformT<int32_t>;   // 业务通用默认实例化（int32 坐标，见 design-knowledge.md 坐标范围节）
//     ——使用点一律写无后缀名、禁显式模板参数；位宽/类型变更只改别名定义一处
//   apply(m)          = R(orient_) · m + offset_                  // 点变换（纯旋转矩阵见 §3.1 常量表）
//   apply_box(r)      = R(orient_) 作用于 r 后重排角点            // 矩形变换（min/max 归一化）
//   apply_polygon(p)  = 逐顶点 apply、保持顶点序                  // **多边形变换（用户裁定必须支持）**
//   compose(t_sub)    = { R(orient_)·t_sub.offset_ + offset_, orientMul[orient_][t_sub.orient_] }
//                                                                // D4 群 8×8 复合表（OpenDB orientMul 同源，按 N..FE 值序重排）
//   FLY_SERIALIZE —— instance 持久化直接复用
//
// —— 业务侧（emir/design）仅剩薄适配 ——
//   t → pos 换算：pos = t − transform_box(orient, box).ll()（box = cell 放置边界，一行调用几何原语）
//   DSTransform 别名不再需要——直接使用 GEOTransform；DSOrientation 并入 GEOOrientation（值同源，取消独立枚举）
```

**与两张配图的对应**：图中红点即该模型的 `pos_`（已逐一数值验证）——图一红点 = 子 DEF 设计原点 (0,0) 的全局落点；图二红点 = LEF 宏 (0,0) 的全局落点。归一化表示（p 坐标 + anchor）与本表示数学等价（anchor = pos_ + R_o(origin_)），实现采用本表示。

orient → 归一化变换（**2026-09-10 按 LEF/DEF Reference 5.8 官方语义修正**，源文档 DEFSyntax「Specifying Orientation」与 LEFSyntax「Macro SIZE/ORIGIN」章节）：

**官方语义三要素**（原文摘录）：
- 「**Components are always placed such that the lower left corner of the cell is the origin (0,0) after any orientation**. When a component flips about the y axis, it flips about the component center. When a component rotates, the lower left corner of the bounding box of the component's sites remains at the same placement location.」
- 「After placement, a DEF COMPONENTS placement **pt indicates where the lower-left corner of the placement bounding rectangle is placed after any possible rotations or flips**.」
- ORIGIN：「the macro is **shifted by the ORIGIN x,y values first, before aligning** with the DEF placement point」（例：ORIGIN (0,−1) 时宏内 (0,1) 的几何先移到 (0,0) 再对齐放置点）。

**权威 orient ↔ OpenAccess 映射**（官方表格）：N=R0、W=R90、S=R180、E=R270、FN=MY、FS=MX、FW=MX90、FE=MY90。

**变换公式（修正版）**——设 `p = m + ORIGIN`（**官方符号：宏先整体平移 +ORIGIN 进入放置参考系**「the macro is shifted by the ORIGIN x,y values first」——例：ORIGIN (0,−1) 时宏内 (0,1) 的几何移到 (0,0)；p 坐标系中宏占据 [0,W]×[0,H]，W/H 即 SIZE；DEF 数值先换算统一 DBU ㉝），placement 点 `t`（DEF 原值，= 变换后放置包围盒左下角）：

| orient | p=(x,y) →（归一化变换 T_o） | 变换后包围盒 |
|--------|------------------------------|-------------|
| N (R0)    | ( x , y ) + t         | W×H |
| W (R90)   | ( H−y , x ) + t       | H×W（换轴） |
| S (R180)  | ( W−x , H−y ) + t     | W×H |
| E (R270)  | ( y , W−x ) + t       | H×W（换轴） |
| FN (MY)   | ( W−x , y ) + t       | W×H |
| FS (MX)   | ( x , H−y ) + t       | W×H |
| FW (MX90) | ( y , x ) + t（**已按官方图示矢量级核对确认**，见 §3.5；包围盒左下恰在原点） | H×W（换轴） |
| FE (MY90) | ( H−y , W−x ) + t（**已按官方图示矢量级核对确认**，同上） | H×W（换轴） |

FW/FE 复合次序裁定（2026-09-10，依据与过程见 §3.5）：**MX90 = 先 MX（x 轴镜像）再逆时针 R90**、MY90 = 先 MY（y 轴镜像）再逆时针 R90——与 OpenDB `dbTransform` 实现数值完全一致（OpenDB 命名为 MXR90/MYR90，与官方表格 MX90/MY90 名字交叉但数值相同，命名分歧警示：**跨源核对必须锚定数学而非名字**）。八个归一化公式与官方 orientation 图示（Reference 5.8 PDF 矢量数据提取的 marker 方位）逐点吻合。

**pos_ 换算常量表**（DEF placement t → instance 存储 pos_，设 box 左下 = −origin_，W/H = box 宽高；由 pos_ = t − R_o(box).ll() 展开）：N:(t_x+ox−0, t_y+oy−0)……直接按通用式实现即可，无捷径必要；注意 R_o 为**纯旋转**（N:(x,y)、W:(−y,x)、S:(−x,−y)、E:(y,−x)、FN:(−x,y)、FS:(x,−y)、FW:(y,x)、FE:(−y,−x)——与 OpenDB `dbTransform::apply` 一致），box 变换后取 min 即 ll。

**origin 修正规则**（㉜ 核心语义，2026-09-10 调研修正 + 用户最终裁定，详见 §3.5）：DEF placement (px,py) = **变换后放置边界（原坐标系 box = [−origin_, −origin_+(W,H)] 经纯 orient 旋转）的左下角**。落地方式——**不归一化**：cell 一切几何按原始坐标存储（LEF 宏坐标 / 子 DEF 设计坐标，与输入文件同构）；origin_ 仅参与「t → pos_」一次换算，instance 级 transform（pos_/orient_）与几何应用完全不含 origin_。**origin_ 字段统一语义（2026-09-10 用户确认）**：origin_ = 放置参考点的负偏移（放置参考点 = 原坐标系 −origin_ 点；LEF cell 取 ORIGIN 语句值（参考点 = −ORIGIN 标记）、block cell 取 −diearea_ll（参考点 = diearea 左下角））——两类 cell 同构、box 同一公式。

**变换链叠加（flatten 用，可传递性——2026-09-10 最终形态）**：父 transform T_p = (pos_p, orient_p)；子 instance 在父 block 原坐标系中的数据 (pos_c^p, orient_c)（pos_c^p 即子 cell 原点在父原坐标系的存储值，S5a 生成时同样按「t_子 − R_o(box_子).ll()」从子 DEF 的 COMPONENTS 原值换算一次）：
```
子全局 transform：orient_child = compose(orient_p, orient_c)      // D4 群复合表（8×8，闭包）
                  pos_child^G   = R(orient_p) · pos_c^p + pos_p    // 纯二元组复合，零修正项
子内几何全局      = R(orient_child) · m_子 + pos_child^G
```
每级复合只有一次旋转+加法，无 diearea/ORIGIN/box 参与——block 嵌套展开代价最小（用户裁定的可传递性优势）。宏宽高在 W/E/FW/FE 下 W↔H 交换（包围盒换轴），链上累计尺寸由各层 cell 尺寸逐层换轴计算。

### 3.2 实施验收（R6 批次）

1. ~~官方公式核对~~ **已完成（2026-09-10，过程与依据见 §3.5）**：八 orient 归一化公式与官方图示逐点吻合、FW/FE 复合次序已裁定；R6 实施时以单测锚定本表 + 同步更新可视化（design-transform.html 旧公式符号随之修正——ORIGIN 方向与 FW/FE 对比图第 ④ 节改为已裁定版）；
2. 单测：8 orient × 已知宏（ORIGIN 非 0 + 非对称 pin 布局）断言全局坐标；变换链两层嵌套 block 断言（父 W 子 FN 等）；宽高换轴断言；
3. 使用示例（§3.3）进 ds_transform.h 注释。

### 3.3 使用示例

```cpp
// 例 1：std cell INV（SIZE 0.8×0.4，ORIGIN (0.1,0)），pin Z 图形 LEF 原值 (0.6,0.1)-(0.7,0.3)（不归一化，原样存储）
// DEF：- inst1 INV + (100, 200) W —— placement t=(100,200) 对齐变换后放置边界左下角
// cell：origin_ = (0.1,0)；box = [−origin_, −origin_+(0.8,0.4)] = (−0.1,0)-(0.7,0.4)
// t → pos：R_W(x,y)=(−y,x)；R_W(box) 角点 (−0.1,0)→(0,−0.1)、(0.7,0.4)→(−0.4,0.7)
//          → 变换后边界 x∈[−0.4,0], y∈[−0.1,0.7]，ll=(−0.4,−0.1)
//          → pos = t − ll = (100.4, 200.1)（LEF 宏 (0,0) 点全局位）
// pin 全局 = R_W(pin) + pos：pin (0.6,0.1)→(−0.1,0.6)→(100.3, 200.7)；(0.7,0.3)→(−0.3,0.7)→(100.1, 200.8)
GEOTransform t = place_from_def({100, 200}, orient_int, inv_cell);  // design 适配层：static_cast<GEOOrientation>(orient_int) + t→pos 换算
                                                                    // （orient_int = defin 回调原值，与 GEOOrientation 值同源直转）

// 例 2：block A（diearea 左下角非 (0,0)，origin_A = −diearea_ll），内部含子 instance B，flatten 叠加：
GEOTransform tA = ...;                                              // A 的全局 transform（pos_A, orient_A）
// B 在 A 的子 DEF 中：- b1 CELL + (bx,by) FN —— 先换算成 B 原点在 A 原坐标系存储值 pos_B^A（同例 1 流程）
GEOTransform tB = tA.compose(pos_B^A, GEOOrientation::MY);
//   pos_B^G = R(orient_A)·pos_B^A + pos_A；orient_B^G = compose(orient_A, MY) —— 纯二元组复合，零修正项
// B 内 pin 全局 = tB.apply(pin_B_原坐标)
```

### 3.4 可视化效果图

[design-transform.html](design-transform.html)：①8 orient 的宏变换总览（同一 L 形非对称宏 + ORIGIN 非 0 标记，逐 orient 演示归一化变换三步动画）；②origin 修正对比（忽略 ORIGIN 的错误结果 vs 正确结果高亮）；③两层 block 变换链叠加演示（子实例锚点/累计 orient 逐步计算）；④FW/FE 复合次序两种草案的对比图。**注意：本页按旧草案公式（m−ORIGIN 符号）制作，§3.1 已按官方语义修正（m+ORIGIN）并裁定 FW/FE——R6 实施时同步更新本页**。

### 3.5 ㉟ 调研结论（2026-09-10：官方 Reference 语义 + OpenDB 读入侧佐证 + 嵌套 DEF 放置裁定建议）

调研聚焦（用户澄清后的准确场景）：**父 DEF 的 COMPONENTS 实例化一个由子 DEF 定义的 block cell——子 DEF 没有 LEF 文件中介**，且子 DEF 的 DIEAREA 最大外边框左下角非 (0,0) 时，block instance 如何放置。

#### 3.5.1 官方语义（LEF/DEF Reference 5.8 原文，ISPD 官方 PDF + coriolis 在线版双源）

- **placement 点语义**（Components 章节）：「a DEF COMPONENTS placement pt indicates where the **lower-left corner of the placement bounding rectangle** is placed **after any possible rotations or flips**」+「Components are always placed such that the lower left corner of the cell is the origin (0,0) after any orientation. When a component flips about the y axis, it flips about the component center.」——placement 点 = **变换后**放置包围盒左下角（不是「围绕某点旋转」）。
- **ORIGIN 语义**（Macro 章节）：「the macro is **shifted by the ORIGIN x,y values first**, before aligning with the DEF placement point. For example, if the ORIGIN is 0, -1, then macro geometry at 0, 1 are shifted to 0, 0, and then aligned to the DEF placement point.」——宏先整体平移 **+ORIGIN** 进入放置参考系（包围盒重定为 (0,0)-(W,H)），再参与 orient 变换与放置对齐。
- **pin 坐标基准**（2026-09-10 用户问询补充核验）：PIN/OBS 的 RECT/PATH/POLYGON 坐标**相对宏左下角 (0,0)（SIZE 矩形坐标系）**，与 ORIGIN 无关——SIZE 章节「bounding rectangle always stretches from (0,0) to SIZE」+ Layer GEOmetries 语法无「相对 ORIGIN」表述 + OpenDB lefin 几何原样入 master 三源一致。ORIGIN 不是坐标系定义，仅是放置/FOREIGN(GDS) 对齐修正量（宏整体 +ORIGIN 平移后对齐放置点）。
- **DIEAREA 语义**：「If two points are defined, specifies two corners of the bounding rectangle for the design. If more than two points are defined, **specifies the points of a polygon** that forms the die area.（边必须轴平行）」——多点即 polygon（㉞ 与官方一致）；且「GEOmetric shapes can be outside of the die area, to allow proper modeling of **pushed down routing from top-level designs into sub blocks**」——die 外几何合法（子 block 压入路由场景）。
- **工业用例佐证（Example 4-19）**：block 5000×5000、**原点在 die 中心**——DIEAREA (−2500,−2500)–(2500,2500)、PINS 用负坐标（PLACED (0,−2500) N）。证明 **DEF 坐标系原点不在 diearea 左下角是合法且真实存在的形态**（ bbox 必须存位置而不能只存宽高——㉞ 修正的直接依据）。

#### 3.5.2 八 orient 公式官方图示矢量级核对（FW/FE 裁定依据）

方法：Reference 5.8 PDF（lefdefref.pdf，p250-251）orientation 表 Definition 列为矢量图示（每行 = 外框矩形 + 实心黑方块标记，N 行基准标记在左下角）；用 pymupdf `get_drawings()` 提取图元坐标判定标记方位（**像素/视觉判读不可靠**——视觉模型曾将 W 行标记误判为右上，矢量数据实为右下）。

| orient | 框向 | 标记方位（矢量数据） | 归一化公式角点验证 | 结论 |
|--------|------|---------------------|-------------------|------|
| N/R0 | 纵 | 左下（基准） | (x,y) | ✓ |
| S/R180 | 纵 | 右上 | (W−x,H−y) | ✓ |
| W/R90 | 横 | 右下 | (H−y,x) | ✓ **R90=逆时针 90°** |
| E/R270 | 横 | 左上 | (y,W−x) | ✓ |
| FN/MY | 纵 | 右下 | (W−x,y) | ✓ |
| FS/MX | 纵 | 左上 | (x,H−y) | ✓ |
| FW/MX90 | 横 | 左下 | (y,x) | ✓ **MX90=先 MX 后逆时针 R90（转置）** |
| FE/MY90 | 横 | 右上 | (H−y,W−x) | ✓ **MY90=先 MY 后逆时针 R90** |

八行 100% 自洽闭环，§3.1 表全部公式确认，FW/FE 不再是草案。

#### 3.5.3 OpenDB 读入侧佐证（EMIR 同为读入方，读入规则才是对齐基准）

证据链（OpenROAD master 分支源码，2026-09-10）：

| 源码位置 | 证据 |
|----------|------|
| `src/odb/src/lefin/lefin.cpp:1337` | LEF 读入：MACRO ORIGIN **仅存元数据**（`master->setOrigin(x,y)`），pin/obs 几何坐标**原样**入 master、不平移 |
| `src/odb/src/db/dbMaster.cpp:668` | `getPlacementBoundary()`：放置边界 = **SIZE 矩形 (0,0,W,H) 平移 −ORIGIN**（ORIGIN 只在此参与） |
| `src/odb/src/defin/definComponent.cpp:183` | DEF 读入 COMPONENTS：`boundary` 取放置边界 → 纯 orient 变换 → **锚点 offset = (px,py) − 变换后 boundary 左下角**——官方语义的精确实现 |
| `src/odb/src/db/dbTransform.cpp` | 8×8 orientMul 复合表（D4 群乘法表，㉜「放置点+旋转方向直接叠加」的权威实现）+ 纯旋转定义（**先镜像后旋转**） |

**数值对账结论**：OpenDB 与官方图示八方向数值**完全一致、并无矛盾**——OpenDB `definComponent` 映射 FW→`MXR90`、FE→`MYR90`，其 `apply` 实现（先镜像后逆时针旋转 90°）数值 = 官方 FW (y,x)、FE (−y,−x)。表象的「映射相反」仅是**命名交叉**（OpenDB 的 MXR90 ≡ 官方表格的 MX90，同值异名；同名 MX90 在两侧定义不同）。**警示：跨源核对必须锚定数学公式，不能锚定名字**——这正是 ㉜「公式以官方定义核对」策略的验证。

（写出侧补充参考：`lefout.cpp` abstract LEF 写出 `SIZE = diearea.xMax/yMax`、port/obs 坐标原样、`FOREIGN 0 0`——隐含「block diearea 左下角在 (0,0)」约定，非零起点时不做修正。EMIR 不依赖写出行为，仅作背景。）

#### 3.5.4 嵌套 DEF 放置结论（㉟ 核心答案）与 fly 裁定建议

**语法层**：DEF 的 COMPONENTS 只能引用 LEF MACRO——「DEF 实例化 DEF」在标准语法上不存在直接引用；工业实践两条路径：
1. **abstract LEF 中转**（BLOCK 流程，OpenROAD `write_abstract_lef`）：子 DEF → abstract LEF（SIZE + port 几何）→ 父读入当普通 macro 实例化；
2. **工具内部层级模型**（OpenDB `dbInst::setBlock`/`bindBlock`）：无 LEF 中介直接把子 `dbBlock` 绑为 instance 的 master——`dbInst.cpp` 合成 master 时 `width/height = child_block->getBBox() 的 DX/DY`（**归一化到 (0,0) 起的标准 master**），port 经 `_dbHier` 1:1 映射为 mterm，此后走与普通 master 完全相同的 transform 语义。

**两条路径的共同语义**：子 block 作为 master 参与放置时，其坐标系按「**(0,0) 起的放置包围盒**」归一化——lefout 路径隐含约定 diearea 左下角即 (0,0)（SIZE 数值 = xMax 即宽高），OpenDB 层级路径显式取 bbox DX/DY 归一化。**子 DEF 原点不在 diearea 左下角（如 Example 4-19 的 die 中心原点）的形态，由「相对 diearea（或几何 bbox）左下角的归一化」吸收，无特例。**

**abstract LEF 的 ORIGIN 配套公式（2026-09-10 补充：子 DEF 有对应 LEF、diearea 左下角非 (0,0) 的场景）**：设子 DEF DIEAREA (a,b)–(c,d)，两种**数学等价**的写法——**A 归一化**（业界主流）：几何 = 子 DEF 坐标 −(a,b)、SIZE (c−a) BY (d−b)、`ORIGIN 0 0`；**B 原样坐标**：几何 = 子 DEF 原值、SIZE 同上、**`ORIGIN (−a,−b)`**（由放置边界公式反解：SIZE 矩形 −ORIGIN = (a,b)–(c,d) 恰还原为 diearea 矩形，diearea 左下角经放置对齐 placement 点）。两法放置结果相同（B 的 m+ORIGIN ≡ A 的归一化坐标），但**几何写法与 ORIGIN 必须配套**——OpenROAD `lefout` 几何原样 + SIZE 取 xMax/yMax + 不写 ORIGIN，隐含假定 diearea 左下角 (0,0)，非零起点时即错配（读回放置偏移 diearea 左下角向量）。

**fly 裁定（P7，已随 ㉜ 最终形态裁定）**：S4 头扫描合成 block cell（子 DEF 无 LEF）与 LEF cell 同构统一——**不归一化**：
- block cell 一切几何按子 DEF 原始坐标存储（port 几何、polygon、后续 S5a 的子实例位置均与输入文件同构）；bbox（㉞ 双存的 bbox/polygon）按 diearea 矩形（放置边界）原坐标保存；
- block cell 的 origin_ = **−diearea_ll**（2026-09-10 用户确认统一语义：origin_ 与 LEF cell 的 ORIGIN 语句值同位——放置参考点 = 原坐标系中 −origin_ 点。LEF cell：origin_ = ORIGIN 值、参考点 = −ORIGIN 标记；block cell：origin_ = −diearea_ll、参考点 = diearea 左下角。box 同一公式 [−origin_, −origin_+(W,H)]（block 场景恰为 diearea），transform 对两类 cell 零差别；溯源信息顺带保留——由 origin_ 可还原子 DEF 原坐标系）；
- 此后 block instance 放置 = §3.1 标准公式（placement 点 = 变换后 bbox 左下角），**与 leaf instance 零差别**；
- 子 DEF 内部几何越出 diearea（官方允许的 pushed-down routing 场景）：原样存储即可（越出部分属几何数据正常范围），bbox 仍按 diearea（放置边界语义，不随几何扩大）。

#### 3.5.5 开源工具渲染实测说明

KLayout/OpenROAD GUI 渲染实测未执行（本机无可直接安装源，下载构建成本高）。㉟ 的验证已由「官方 Reference 原文 + 官方图示矢量级核对 + OpenDB 读入侧源码」三重独立证据等效覆盖；如审阅仍需工具级渲染佐证（8 orient 视觉比对 + 非零 diearea 嵌套用例渲染），可在 R6 实施期以 `fly` 自身 transform 单测的数值断言替代（锚定 §3.1 表公式），或后续专项补 KLayout 用例。

#### 3.5.6 放置语义配图（八 orient 全量数值示例）

两张配图把 ㉟/㉜ 的三要素（**实例化给的坐标**、**子坐标系原点在主 DEF 的落点**、**最终图形坐标**）在八种 orient 下全部数值标出，坐标系与姿态箭头（红=子坐标系 x 轴、紫=y 轴）直观呈现：

- [design-nested-def-placement.png](design-nested-def-placement.png)（嵌套 DEF，无 LEF 中介）：子 DEF DIEAREA (100,200)–(500,480)（左下角非 (0,0)），按 P7 归一化为 block cell（放置包围盒 400×280，子 DEF (0,0) 在 cell 坐标 (−100,−200)）；主 DEF `- u1 SUBBLOCK + (1000,600) <orient>`。要点：绿三角（实例化坐标）恒为 (1000,600) 且恒等于变换后包围盒左下角；红点（子 DEF (0,0) 落点）随 orient 移动（N=(900,400)、W=(1480,500)、S=(1500,1080)、E=(800,1100)、FN=(1500,400)、FS=(900,1080)、FW=(800,500)、FE=(1480,1100)）——**给定的坐标与子 DEF 原点落点不同，差异即归一化偏移 (−100,−200) 经 T_o 旋转后的结果**；蓝框（diearea 最终图形）在 W/E/FW/FE 下宽高换轴。
- [design-lef-origin-placement.png](design-lef-origin-placement.png)（LEF ORIGIN≠(0,0)）：MACRO SIZE 800×400 DBU、ORIGIN (200,0)，几何经 +ORIGIN 吸收后相对放置包围盒偏移 (200,0)；主 DEF `- i1 INV + (1000,600) <orient>`。要点：蓝虚线（放置包围盒，左下角恒对齐绿三角）与绿框（几何最终图形）**分离 200**（镜像朝向 S/FN/E/FE 下偏向相应反转）；红点（LEF 宏坐标 (0,0) 即 SIZE 左下角落点，N=(1200,600)、W=(1400,800)、S=(1600,1000)、E=(1000,1200)、FN=(1600,600)、FS=(1200,1000)、FW=(1000,800)、FE=(1400,1200)）。

两图数值均由 §3.1 已核对公式生成，可作为 GEOTransform 单测（R1 geometry）的期望值来源。

---

## 4. 裁定点（**全部已裁定**，2026-09-10）

| # | 问题 | 裁定结果 |
|---|------|---------|
| P1 | 统一 Cell/Pin 类型的归属模块 | **已裁定：保持现有结构**（不新建 core 模块；lib db 的 LIBCell/LIBPin 与 design db 的 DSCell/DSPin 不合并——原 ㉘「全库唯一类型」重构撤销）；**DSCell 的 pin 数据三字段按 pin 维度组织**（R4 重写，2026-09-10）：① pins_ 基础公共 pin（+全局 pin_id 字段）；② lib 侧 pin（含表）——DSPinTables 键 cell_id→**全局 pin_id**；③ lef 侧 pin（含图形）——DSPinGEOmetry 键 cell_id→**全局 pin_id**；`tables_of(pin_id)`/`geometry_of(pin_id)` 直接命中（修复「pin id 查不到自己的表/图形」） |
| P2 | 统一 cell 的表数据（功耗/时序表）存储位置 | **已裁定：A**——维持 ⑰/⑱：cell 序列化内容不含表，表随建库方独立对象 |
| P3 | port 的 placement 状态（FIXED/COVER/PLACED）承载 | **已裁定：A**——DSPin 增 placement_status_ 枚举字段（仅 port 场景有效） |
| P4 | fake_cell_ids_ 单独集合是否保留 | **已裁定：保留**（flags 为权威语义、集合为快速索引） |
| P5 | ~~纯仿射变换（线性部分+平移）是否下沉 geometry~~ **已裁定（2026-09-10 用户确认）：下沉**——最终形态 `GEOTransform{GEOPoint 平移 + GEOOrientation 旋转}`（point+rotation 构造、apply/compose/transform_box/序列化全在 geometry，纯图形处理无业务语义）；**枚举值直接采用 Si2 DEF_ORIENT_*（N=0…FE=7，用户裁定严格对齐、defin 回调整型直转零映射）**；业务侧仅剩 t→pos 一行换算；OA 系名仅文档对照 |
| P6 | 重构的发布方式 | **已裁定：需要重构，一次提交**（P1 撤销类型统一后重构范围 = R1/R2/R3/R4'/R5/R6 + S5a/S5b；全部完成 + 全量回归后统一 commit，工作区 T1-T6 实现一并入库） |
| P7 | ~~S4 合成 block cell 的坐标系~~ **已裁定（2026-09-10，随 ㉜ 最终形态裁定）**：block cell 与 LEF cell 同构统一——cell 一切几何按**原始坐标**存储（不归一化）+ `origin_ = −diearea_ll`（与 LEF ORIGIN 同位：放置参考点 = 原坐标系 −origin_ 点）+ `box = [−origin_, −origin_+(W,H)]`（block 场景恰为 diearea 矩形）；DEF placement t → instance pos 经放置边界换算一次；transform（GEOTransform）对 block/leaf 零差别、链式复合零修正项（§3.1） |
| P8 | LEF ORIGIN≠(0,0) 时 instance 坐标的对齐语义（用户审阅图二提出） | **已裁定：A 官方语义**——实例化坐标对齐「放置边界（SIZE 框 −ORIGIN）」变换后左下角，SIZE 框相对偏移 T_o(ORIGIN)（Reference 5.8 + OpenDB 读入源码双源一致；ORIGIN=(0,0) 库两层框重合无差异；两张配图与 §3.1 公式即按此，无需改动） |

## 5. 批次计划（审阅通过后）

| 批次 | 内容 | 依赖 |
|------|------|------|
| R1 | geometry → src/geometry 顶层模块 + DSShapeRef 业务化 | — | ✅（2026-09-12 完成）
| R2 | layer id 化 | R1 | ✅（2026-09-12 完成）
| R3 | CM_FLAGS 宏 | — | ✅（2026-09-12 完成）
| R4 | pin 三字段按 pin 维度组织（DSPin 增全局 pin_id；DSPinTables/DSPinGEOmetry 键 cell_id→pin_id + 检索 API；类型统一已随 P1 撤销） | R3（flags，仅 flags 位复用） | ✅（2026-09-12 完成）
| R5 | port/block 复用 + VIARULE 删除 | R3/R4 | ✅（2026-09-12 完成）
| R6 | transform 业务接入（orient 整型直转 + t→pos 换算 + instance 字段 + 可视化定稿；结构本体在 R1 geometry） | R1（geometry） | ✅（2026-09-12 完成）
| S5a | COMPONENTS 责任链（CellResolve/InstanceBuild/Density/Stats 节点 + fake cell 机制落地；block cell 按已裁定 P7 同构统一）∥ 网名扫描 | R2-R6 | ✅（2026-09-12 完成）
| S5b | 网内容责任链（分批多阶段） | S5a | ✅（2026-09-12 完成）
| S6 | **层级树构建 + 起始编号分配**（2026-09-10 用户裁定纳入本期）：block instance 树（节点 = block instance，root = top block instance，node 同时存 name 与 id）+ 编号区间表（instance/net/via instance 三类，深度优先序连续区间，区间长度 = 该 block 定义的对应计数）+ 四接口（① id→所属 block instance 区间反查；② block instance→id 范围；③ parent/直系 children；④ 以 name 打印树）；global id = local id + 起始编号，local 0 = block 自身占位→该 block instance 的 global id（⑧）；产物挂 DSDesign 容器（⑬）。**实施备注（2026-09-10 返馈消解）**：via instance 区间长度 = S5b 产物的 per-DEF via 统计计数——「S6 ∥ S5b（均仅依赖 S5a，⑨）」的表述与 via 计数的物理依赖内在矛盾，实施取方案 A 消解：S6 实际消费 S5a+S5b（S5b 消费 S5a 临时产物先行，S6 随其后一次建全三类区间，正式 DSDesign 写定前嵌树，单点写定时序不变）| S5a + S5b（via 区间） | ✅（2026-09-12 完成）
| R7 | **name 体系收敛 + id 位宽分组 + 无效 id 哨兵 + 全局 mapper 组装层（㊱㊳㊴㊸㊹㊻；㊵①并入 ㊹ 结构、㊵②伴生对象化随之定稿）**：①`DSNameHasherT<IdT>`（hash 实现，接口：get_id/get_name、kInvalidId 哨兵、is_valid_id、双向、空洞容忍）+ **六实体 hasher**（DSCellNameHasher/DSPinNameHasher/DSLayerNameHasher/DSViaCellNameHasher = <uint32_t>，DSInstanceNameHasher/DSNetNameHasher = <uint64_t>——instance/net hasher 为简单版内部，树化见 R8）替换全部散装 mapper 字段（DSStack.layer_index_ 惰性索引改 DSLayerNameHasher；instance 顺带补 id→name 反向）；②`DSNameMapperT<IdT>` **注入式轻壳**（层级树引用 + 注入表；`set_block_hasher` 按需注入、不保存不序列化 hasher、局部注入局部可查；get_global_id(full_hier_name)/get_full_name(global_id) 组装逻辑；DSInstanceNameMapper/DSNetNameMapper = <uint64_t> 实例化）；③DSPin 删 name_、DSInstance 删 name_（layer/cell/via cell 双存保留）；④instance/net/via instance id 与层级树区间 64 位化（cell/pin/via cell/layer id 维持 uint32）；⑤内部逻辑全程 id 为键、name 仅边界经 mapper 转换；⑥调用点与导出面迁移 | geometry 别名任务后串行 | ✅（2026-09-12 完成）
| R8a | **hasher 压缩基准实验（㊽，并行可跑）**：三方案原型对比（map+vector 基线 / radix+arena / FST+arena 自实现最小构建）× EDA 模式仿真名集（百万级）+ 真实 def 名提取；指标：构建时间/内存/双向查询速度；产出报告（数据表+结论建议）；纯 .work 原型不进 src 不走 bazel | 与 R7 并行（不碰 src） | ✅（2026-09-12 完成）
| R8b | **hasher 树化落地（裁定 52：hat-trie + 抽象包装先行）**：①Backend 概念（编译期模板策略）+ `DSHasherBackendHatrie` 首实现（tsl::htrie_map）+ dummy backend 替换性单测；②`DSNameHasherT<IdT, BackendT=Hatrie>` 内部 = backend（name→id）+ char arena 偏移表（id→name 通用件）；③序列化 backend 无关通用名集格式（读回重建）+ 双段制加速段（直载优先/重建兜底，格式①定型）；④依赖接入（MODULE.bazel http_archive 或签入 third_party，gh-proxy 可达性实测定）；⑤接口不变业务零感知，R7 hasher 测试全量回归 + QA | R8a + R7 | ✅（2026-09-12 完成）
| R8c | **分派加速（裁定 53：方案 B）+ 前后基准**：分派索引（backend 复用、前缀一次 find、set_tree 即建 + rebuild 兜底）+ split 无堆化；前后 18 格基准（结果见 53 行补记：p50 提速 2.24-11.82x、单次 1.1-1.5µs 与规模解耦、反向零回归） | R8b | ✅（2026-09-12 完成）
| R8d | **LCP 后缀共享（裁定 55：alpha 控制 `lcp_name_arena` 默认 false）**：id→name 侧双形态（全名/LCP 压缩+checkpoint64）+ 落盘格式自识别 + rank 序批量遍历路径 + 截断正确性单测 + 两形态回归 | R8c | ✅（2026-09-12 完成）
| R9 | **wait_obj 依赖传播体系（㊼，规范见 DEVELOPMENT_GUIDELINES Section 17）**：框架层——wait_obj wrapper 增 deps(*args) 方法 + run_direct(func,...) 公共 API（经 _fly_original_func 直调）+ 单测（deps 解析/run_direct 零等待）；ds_functions 全部 read_object 类 API wait_obj 包装（含 R7 伴生加载新 API）；ds_flow 任务链调用点改 deps 传播 + run_direct 形态 | R7 | ✅（2026-09-12 完成）
| R10 | **throw 全仓治理专题（2026-09-12 用户裁定，跨 design/storage/container/message）**：①层引用未定义兜底——两处重复 require_layer_id 收敛为共享 `ds_resolve_layer_id` + 条目级丢弃（rect/wire 逐条、via 整条）+ skipped_layer_ref_count 计数 + DSGN::0010，不再 raise（dev-rules §7）；②DBU 恒基准 ㉝ 落实——`DSStack::kGlobalDbuPerMicron=1000`，tech/cell lef UNITS 声明不写 stack 不参与换算，DBU 不一致不再 raise（R2 残留补完）；③层级树多根/零根/环/对齐 4 处 raise → fatal message DSGN::0011（码 80 退出 + master 联动 fast_exit，框架级 fatal 机制见 docs/message-system.md §14）；④name hasher 权威段损坏 2 处 → DSGN::0012 fatal（rebuild 兜底保留；logic_error 3 处保留为编程错误守卫）；⑤存储数据损坏 5 处 → STOR::0005 fatal；⑥lookup_table 错误处理（resolve_template/interpolate bool 化，零 fatal）；⑦可控退出路径 log flush 全覆盖 | R9 后独立专题 | ✅（2026-09-12 完成，commit 3d65020） |
| S8 | **全局密度合并 + 分区决策（2026-09-12/13 用户裁定定稿）**：①S5a 前置修正——DSDensityNode 排除 block instance 自身 bbox（判定 = cell block_cell 位；block 密度贡献 = S8 子树叠加避免双计）；②`ds_partition.h/.cpp`：`DSSubPartition`（partition_id + core_rect/extend_rect——非边缘方向 core 外扩 2×w_eff、最外围方向 int32 极值不截断允许相邻重叠；w_eff = 全局合并后金属格值 > 0 的最高 ROUTING 层 default_width，无有效层 = 0）+ `DSDensityWeights`（6/2/2 默认 + 逐层系数接口保留不暴露 alpha）+ `ds_merge_global_density`（后序逐级合并的线性等价形式：每 def 局部图按到根复合变换撒入一次；格值分摊 D10 A = 交叠面积比例 + 最大余数法守恒，交叠面积 int64 中间量；三通道独立分列；全局格网 = 根 DIEAREA ceil 覆盖、bin 同 local）+ `ds_decide_partitions`（行列前缀和等分切线吸附格边界、空段跳过；行列分布 = 负载等效宽高比法 nx=clamp(round(√(N·Wq/Hq)),1,N)、ny=ceil(N/nx)，均匀负载还原几何比；空负载兜底单分区；三键优先级 target_partitions > partition_count > partition_target_density 默认 150000，非法值 DSGN::0013 提醒回退不 raise）；③DSDesign 增 `partitions_`（序列化 + partition_count/partition_at/set_partitions）+ 导出面（EXDSSubPartition + 两算法函数）；④flow S8 任务（依赖 S6 树 + S5a/S5b 正式产物 + stack，不预留 S7 挂点）：global_density 独立正式对象 + DSDesign 补分区重写（先 remove 规避 DUPLICATE_SKIPPED；freeze 依赖 global_density 保证重写先于冻结）+ alpha 四键 flow 边界类型检查（语义解析在 C++）；⑤单测 ds_partition_test（合并分摊手算/最大余数/tie/多次实例化/旋转 + 直切/行列分布/目标密度/空负载/非法回退 + 序列化往返）+ QA design case 改 block1 密度断言（2→1）+ S8 段 + 新 case test_emir_partition.py（'2x1' 直切手算切线/坐标） | S6 + S5b | ✅（2026-09-13 完成） |
| S7 | **跨块连接归并（并查集；2026-09-13 裁定定稿，见 design-db-plan.md S7 节补记①-⑤）**：①`ds_union.h/.cpp`：`DSNetUnion`（root_of_ 成员→root 两层树 + members_of_ root→成员反向索引（升序含 root 自身）+ dangling_count_ 悬空计数；find 恒一步（不在表 = 自身）/members/class_count）+ `DSNetUnionSlice`（per-DEF 局部收集临时产物：(父网, 子网) 边 + port 网 local id 集 + block 名）+ `ds_collect_net_union_slice`（每父块 DEF 一调用：树按 block 名反查全部实例化位置；对接键 = 同一块实例 + 同名 port——父连接 (子实例名, port) × 子连接 ("PIN", port) 名字对接，**S5b 连接表无 local 0 条目**（block 自身占位仅在 instance 表，port 引用 instance_name = "PIN" 为 defi 回调语义，读 ConnectionParseNode 实现确认）；顶层引脚连接不产生跨层 union）+ `ds_build_net_union`（合并局部边集 → 路径压缩并查集 → root 规范化（层级最高优先——block_of_net 区间反查 + parent 链深度 memo 化；同级最小 global id）→ 全类重挂两层 + 悬空判定（不在任何边上的非 root 块 port 网 root=自身；root 块 port 网排除；internal net 绝不入表）+ DSGN::0018 提醒）+ `ds_net_union_child_indexes`（编排辅助：def 序号 → 子定义序号集）；②导出面（EXDSNetUnion/EXDSNetUnionSlice + 三函数）+ `load_design_net_union`（R9 wait_obj 形态）+ NET_UNION_OBJ = "net_union"；③flow 两级任务（block 名清单小任务 → per-DEF slice 并行收集（只读本 def 引用的子定义网产物，避免 N×全量重复读）+ 单任务汇总两层化/规范化/悬空计数 + slice remove），与 S8 同级并行（依赖同为 S6 树 + S5b 产物）；freeze final_keys 挂 net_union；④单测 ds_union_test 8 用例（局部收集/电气等价 + root 规范/三层嵌套/两次实例化不互并/internal 不入 + 悬空计数/两层不变式含序列化往返/空输入兜底/编排辅助）+ QA design case 改 block_parent.def（n_top 增 `( top3 PIN_IN )` 真实跨块连接，connection 断言同步 2→3 项）+ S7 段（find/members/悬空 n2 root=自身/两层不变式/DSGN::0018 入消息断言清单） | S6 + S5b | ✅（2026-09-13 完成） |
| S9 | **flatten 展平 + 分区保存（2026-09-13 裁定补记①-⑤ 定稿，见 design-db-plan.md S9 节补记 + 实施备注）**：①`ds_flatten.h/.cpp`：四类正式对象（`DSPartitionGeometry`/`DSPartInstances`/`DSPartInstConnections`/`DSPartNetConnections`）+ `DSPartitionProduct` 分片聚合容器（merge_from 追加合并）+ 连接条目 `DSPartConnection`（instance/net global id + pin 名 + port 位，INST/NET_CONNECTIONS 共用形态）+ 几何条目 `DSGeomEntry`（layer + 全局 rect + via_cell_id 哨兵 + obs/primary 位——via 图形经 via cell cut/enclosure 展开逐矩形挂 net 保持「geometry 以 net 组织」）+ `ds_flatten_block`（每定义一调用；归属 assign_point = 放置点 core 半开区间 primary 恰一 + extend 副本集，无 core 命中防御回退最近 core；UNPLACED 不入分区；非 pg 连接全量补全 / pg 仅本区 instance 副本条目——pg 判定 = special 或 USE POWER/GROUND 记入 `DSNetBuildData.pg_nets_`；is_crossing = 成员散布 >1 分区；OBS → net 0 桶）；②**复合变换存树节点**（`DSHierNode.composite_transform_`，S6 DFS 递推回填）——展开任务只读单一 def 产物保证「每份 DEF 数据只读一次」；③结构扩展：`DSInstance` 增 primary 位 + `power_pins_` 电源引脚预展开（D18，锚点 = pin 几何聚合 bbox 中心 × 复合）、`DSBlockBuildData.obstructions_`（S4 头扫描 `defrSetBlockageCbk` 收录 DEF BLOCKAGE——D17 修订）、`DSSubPartition.xp_/yp_`（分区对象命名用）；④flow 两级任务 + 小 DEF 聚合（plan 任务 worker 动态提交——分组依赖树运行时数据同 solver kickoff 先例；预估 = DEF 文件大小 × 实例化次数，alpha `def_aggregate_threshold` 缺省 64 MiB；per-组展开每任务对全部分区写分片临时对象，未触达写空保合并依赖恒可解；每分区一合并任务拆写四类正式对象 `PART_{xp}_{yp}/{kind}`；freeze 由 plan 动态提交携带全部分区对象名，S8 完成锚点 = global_density 防 DSDesign 读旧版竞态）+ `partition_obj_name`/`load_partition`/`iter_design_partition`（R9 wait_obj 形态）+ alpha 八键（新增 def_aggregate_threshold）；⑤单测 ds_flatten_test 12 用例（归属/边界/子定义复合/几何副本不裁剪 + crossing/via 挂 net + 放置点 primary/OBS net 0/全量补全/pg 过滤/inst 跟随副本/电源引脚/merge 幂等 + 序列化往返）+ ds_def_adapter_test BLOCKAGE 断言 + QA S9 段（test_emir_partition '2x1' 双分区 + test_emir_project_design 单分区，手算锁定） | S6 + S7/S8 产物 + 全部 per-DEF 产物 | ✅（2026-09-13 完成） |


每批 TDD + ./fly.sh 验证 + 不 commit（审查后统一处理）；R 批次完成后全量回归（单测 + QA emir + 真实数据烟测复跑）。
