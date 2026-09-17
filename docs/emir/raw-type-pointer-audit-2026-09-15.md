# 代码评审报告（纯调研评审：强类型 id / 领域枚举 / 观察指针三类裁定欠账盘点）

> **存档批注（主会话，2026-09-15）**：本报告由只读调研代理产出（agent_59e6b041，
> 90 次工具调用 / 约 17 分钟），供 reviewer 评审 design 重构时对照收敛完成度、
> 供批次 A/B/C 规划引用。
>
> **时效性警告**：「重构在途」清单（B-1~B-13）基于调研时刻的工作区快照，而重构
> 代理（段落②）此后仍在持续推进——主会话已观察到在途 diff 覆盖了报告标记为
> 「未覆盖」的 B-2 枚举字段（DSLayer/DSPin/DSInstance 定型存储）。**一切以重构
> 代理落地提交为准，reviewer 须逐项复核 B 组清单的实际残留**，不得以本报告快照
> 直接断言欠账。
>
> **范围对齐要点**：B-4（DSNameHasherT/DSNameMapperT 裸整型模板实例化）被报告
> 定为 design 模块 id 收敛的「剩余主战场」——重构代理当前段落②/②b/③ 是否覆盖
> 此处，是 reviewer 对照核验的第一优先项；若未覆盖，归入批次 B（design 链顺带）。

**评审结论**：❌ 存量欠账需分批收敛（非本次合入门禁——本报告为裁定 ①②③ 的全量欠账盘点，供批次规划；无正确性缺陷需立即阻断）

**材料完整性**：裁定链（任务书内 2026-09-16 ①②③ + b68fd56 提交体）✅　约束（AGENTS.md + DEVELOPMENT_GUIDELINES.md §16，已核原文）✅　方案文档 ❌（无独立方案件，以裁定链为基准）　测试 ❌（纯调研评审，禁跑测试，无影响）

**在途范围界定（关键前提）**：`git status` 显示并行会话未提交修改仅 `src/emir/design/cpp/ds_types.{h,cpp}`（另新增未跟踪 `docs/emir/timing-db-plan.md`）。该在途 diff 正是 b68fd56 提交信息预告的「首笔消费」——ds_types 的 CM id 迁移 + `net_uses_` 枚举定型。**src/emir/design、src/emir/common、src/common/types 三路径的全部发现按要求单列「重构在途」表，不计入存量主体**；ds_types 行号以已提交状态（HEAD）为准，已验证在途工作区状态的项单独注明。

---

## 总量统计

| 维度 | 存量主体（非在途路径） | 在途路径（design/emir-common/types） |
|---|---|---|
| 1. 裸整型承载 id | 1 项 3 处（emir/timing clock id 族） | 12 项约 60+ 处（design 全模块） |
| 2. 裸整型承载领域枚举 | 10 项约 18 处 | 5 项约 12 处 |
| 3. 裸指针可智能化 | 4 项 6 处 + 2 条绑定面建议 | 3 组约 15 处 |

按模块分布（存量主体）：network 7 项、storage 4 项、agent 3 项、task 2 项、common 2 项、emir/timing 1 项、solver 1 条建议。emir/lib 零命中（设计裁定：lib 阶段以 cell name 区分、不分配 id，见 `src/emir/lib/cpp/lib_types.h:6`）。geometry 零命中（GEOOrientation 已 enum class 定型存储）。

---

## 存量主体问题清单（P1-P3）

### 🔴 P1（跨模块 API 面、语义混淆已实际易错）

1. **[实现质量/裁定②] CompressionType 值经裸整型贯穿四层，边界重构无值域校验**
   - 位置与现状摘句：
     - `/root/fly/src/common/serialization/cpp/object_header.h:18`：`uint8_t compression_type_ = 0;`（持久化对象头，跨进程落盘）
     - `/root/fly/src/common/io/cpp/chunk_source.h:29`：`virtual int compression_type() const = 0;`（公共层接口以 `int` 承载枚举）
     - `/root/fly/src/network/cpp/message_types.h:282`：`uint8_t chunk_compression_type_ = 0;`；`:1058`：`uint8_t compression_type_ = 1;  // CompressionType::LZ4（数值——message_types 不依赖 storage/compressor 定义）`
     - `/root/fly/src/storage/cpp/fly_stream.h:66,80`：`uint8_t sink_effective_compression() const { return sink_comp_; }` / `uint8_t sink_comp_ = 0;`
     - 无校验重构点：`/root/fly/src/storage/cpp/decompressing_streambuf.cpp:14,23` 与 `/root/fly/src/agent/cpp/peer_rpc_server.cpp:151` 均为裸 `static_cast<CompressionType>(...)`——线上/落盘值一旦越界即未定义行为枚举值，对标 `is_valid_message_type`（message_types.h:107 对 MessageType 有校验）此处缺失。
   - 依据：裁定②「领域枚举一律 enum class 定型存储，禁止裸整型承载」；同仓已有边界校验先例（MessageType）。
   - 建议（批次 C，独立收敛）：将 `CompressionType` 枚举定义下沉至 `common/types`（或 common/buffer），storage 侧 `using` 别名保持兼容；`ObjectHeader::compression_type_`、`ChunkSource::compression_type()`、`PeerStreamStartMessage::compression_type_`、`FlyStream::sink_comp_` 及三个 ChunkSource 实现类（disk:37/network:63/memory:21,39 的 `int` 成员）全链定型为该枚举；同时在 `MessageProtocol::decode` 与 trailer 解析处补 `is_valid_compression_type` 值域校验（对齐 MessageType 口径）。序列化字节不变（enum class 底层 uint8_t）。

2. **[实现质量/裁定②] PeerRpcWireStatus 同文件已有 enum class，仍以 uint8_t 传参**
   - 位置与现状摘句：`/root/fly/src/agent/cpp/peer_rpc_server.h:20-35` 定义 `enum class PeerRpcWireStatus : uint8_t { OK=0, NOTIFY_FAILURE=1, RESPOND_FAILURE=2, BYE=3, ... }`；但 `:175` `bool send_response(uint64_t conn_id, uint64_t rpc_id, uint8_t status, ...)`、`:140` `ResponseHandler` 回调签名 `void(... uint8_t status ...)` 均裸整型承载。
   - 依据：裁定②；同文件同模块无任何层级障碍，属最直接的违例。
   - 建议（批次 C）：签名改 `PeerRpcWireStatus status`；调用点（peer_rpc_server.cpp 内多处 `static_cast<uint8_t>(PeerRpcWireStatus::OK)`，如 ：157）反向消除。

### 🟠 P2（模块内部 API/存储面）

3. **[裁定②] `/root/fly/src/network/cpp/message_types.h:566`** — `uint8_t exit_reason_ = 0;  // worker ExitReason 值`。ExitReason 枚举在 agent 层（worker_agent.h），network 不能反向依赖。建议（批次 C）：枚举定义下沉 common/runtime（与 error_types.h 同层），network 与 agent 共用；或 network 内定义镜像枚举 + static_assert 值对齐。诊断字段暂无消费方逻辑，风险为语义漂移。
4. **[裁定②] `/root/fly/src/network/cpp/message_types.h:129`** — `uint8_t role_ = 0; // 0=hybrid, 1=storage_only`，承载 task 层 WorkerRole。同上分层下沉方案（批次 C）。
5. **[裁定②] `/root/fly/src/network/cpp/message_types.h:1057`** — `uint8_t direction_ = 0; // 0=请求流, 1=响应流`，无既有枚举、无分层障碍。建议 network 模块内定义 `enum class PeerStreamDirection : uint8_t { REQUEST, RESPONSE }`；消费点 `peer_rpc_server.cpp:157` 的 `m.direction_ == 0` 魔法比较与 `peer_rpc_server.h:52` `PeerStreamRxState::direction`（uint8_t）一并定型（批次 C）。
6. **[裁定②] `/root/fly/src/network/cpp/message_types.h:187`** — `uint8_t kind_ = 0; // 0=周期采样；1=事件驱动采样`，monitor 域概念无枚举。建议定义 `MonitorSampleKind`（monitor 或 network 侧，批次 C）。
7. **[裁定②] `/root/fly/src/storage/cpp/fly_stream.h:66,80`** — `sink_comp_` 与访问器（并入 P1-1 一并处理；单列因同模块即 CompressionType 定义处，无任何障碍，是 P1-1 的最小独立切口）。
8. **[裁定②] `/root/fly/src/agent/cpp/peer_rpc_server.h:181-192,304`** — `send_stream_start(..., uint8_t direction, uint8_t compression_type)`；`:304` `send_stream_payload(..., uint8_t direction, ..., CompressionType comp, ...)` 同一签名内 typed/裸整型混用——正是裁定②所指混淆的现场证据（并入 P1-1/P2-5 改法，批次 C）。
9. **[裁定①] `/root/fly/src/emir/timing/cpp/tm_types.h:37,70,118`** — `inline constexpr uint32_t kTMNoClock = UINT32_MAX;` / `uint32_t clock_id_ = kTMNoClock;` / `CMUnorderedMap<CMString, uint32_t> clock_index_;`。依据：emir_ids.h:9 明示「新增实体 id（如将来 timing 的 CMClockId）也在本处新增」。建议（批次 A，timing 模块重启批次顺带——在途立项文档 docs/emir/timing-db-plan.md 已就位，正好同批）：`emir_ids.h` 增 `CMClockId`（32 位族），哨兵 = kInvalid 与 kTMNoClock 同值，`clock_index_` 值类型同步。
10. **[裁定③] `/root/fly/src/task/cpp/task_scheduler.h:19,38-39`** — `TaskScheduler(DependencyGraph* graph, WorkerManager* manager)` 构造参数与存储成员 `DependencyGraph* graph_; WorkerManager* manager_;`。宿主 `master_agent.h:572-573` 以 `CMUniquePtr` 持有，MasterAgent.cpp:362 注入 `.get()`；正常析构序安全（成员逆序销毁），但运行期 reset/重建（solver restart 同类场景，§16 判据原文：「一个 reset()/析构能让另一处持有的指针悬垂，即属必须场景」）即悬垂。建议（批次 C）：宿主改 `CMSharedPtr`，scheduler 持 `CMWeakPtr` 并在使用点 lock 判空。
11. **[裁定③] `/root/fly/src/task/cpp/heartbeat_monitor.h:11,22`** — `HeartbeatMonitor(WorkerManager* manager, ...)` + 成员 `WorkerManager* manager_;`，同上（批次 C，与 P2-10 同一批改造）。

### 🟡 P3 / 💬 Suggestion

12. **[裁定②] `/root/fly/src/network/cpp/message_types.h:217,353`** — `uint8_t is_write_ = 0;`、`uint8_t storage_only = 0;` 均为 bool 语义存 uint8_t。建议直接 `bool`（线上字节不变，FLY_SERIALIZE 对 bool/uint8_t 同 1 字节——落地时需以 record_format 测试确认，零容忍条款下不得有字节级漂移）。
13. **[绑定面一致性] `/root/fly/src/agent/export/agent_export.cpp:51-63,625-637` 与 `/root/fly/src/solver/export/solver_export.cpp:147`** — `new` 产物裸指针经 `FLY_EXPORT_METHOD`（无显式 rv_policy）交 Python。已核实 nanobind `infer_policy`（nb_cast.h:437-440）：指针返回 + automatic → take_ownership，内存安全成立，属裁定③豁免边界。建议对齐 storage 先例（storage_export.cpp:42-79 显式 `rv_policy::take_ownership` + 注释说明原因），消除对 nanobind 默认策略的隐式依赖；备选形态：C++ 侧改返 `CMUniquePtr`，导出层 `std::move` 交接管。
14. **[裁定③备选] `/root/fly/src/storage/cpp/database.h:50-56`** — `FlyStream* open_write_stream(...)` 注释已自证「返回裸指针（export 层 take_ownership 接管）」，属文档化豁免；同上可选 `CMUniquePtr` 返回 + 导出层释放（nanobind unique_ptr caster 对 FlyStream 不生效已有注释，改动前需先验证该限制是否已解除）。

---

## 重构在途——落地后复核（单列，不计入存量主体）

**范围**：src/emir/design、src/emir/common、src/common/types。基线 = 已提交状态；已核实工作区在途 diff 的覆盖面如下。

### A. 在途 diff 已覆盖（落地后仅需复核编译贯通 + 序列化字节级一致 + 测试全绿）
`ds_types.{h,cpp}` 的全部 id 字段/函数签名/容器键（含树区间起始 `instance_start_/net_start_/via_start_`、`self_global_id_`、`block_cell_id_`、via cell 三层 id、`fake_cell_ids_`、`get_cell/pin_name_of/cell_pin_geometries/init_placeholder/is_pg_net` 等）与 `net_uses_` → `CMUnorderedMap<CMNetId, DSNetUse>` 枚举定型。区间 count 保持裸型，符合 emir_ids.h 裁定口径。

### B. 在途 diff 未覆盖的残留（工作区状态已逐项核实，落地后仍为欠账）

1. **ds_types.h 自身残留**（工作区行号）：
   - `:155` `static constexpr uint32_t kNoLayer = DSLayerNameHasher::kInvalidId;` —— 哨兵常量仍裸 uint32_t，而 `:161 find_layer` 已返 `CMLayerId`，同文件类型口径不一致（在途会话应在落地前处理）；
   - `:425-433` `metal_layer_counts_/via_layer_counts_` 键仍 `uint32_t`（应为 CMLayerId），`accumulate_layer_shape(uint32_t layer_id,...)`、`layer_total(uint32_t layer_id,...)` 同；
   - `:455` `per_cell_counts_` 键（cell id → CMCellId）；`:1045` `lib_link_` 键（cell id → CMCellId）。
2. **裁定②枚举字段（ds_types.h，HEAD 行号约 100-104/220-227/350-357）**：`DSLayer::type_/direction_`、`DSPin::type_/direction_/placement_status_`、`DSInstance::placement_status_` 均为 `uint8_t` + 赋值处 static_cast（文件头注释「枚举字段按设计以 uint8_t 存储」为裁定②之前的旧设计，已被推翻）。在途 diff 仅改了 `net_uses_`，其余未动。落定为 enum class 直接存储（序列化走整型路径字节不变）。
3. **ds_flatten.h**（未修改，HEAD 行号）：`DSNetConnEntry::inst_id_/pin_id_`（110,112）、`DSPartConnection` 三 id（127-131）、`DSGeomEntry::layer_id_/via_cell_id_ + kNoViaCell`（148,154-145）、`DSPartitionGeometry::nets_/crossing_nets_` 键（175,181）、`DSPartInstances/DSPartInstConnections::items_` 键（232,247）、`DSNet::net_id_ + use_ uint8_t`（260-261，use_ 是裁定②同款）、`DSPartitionNets::part_id_ + nets_ + net_of`（282-290）、`DSPgNetSlice/DSPgNetSet` 的 id 向量/集合与判定口（301-302,315-326）、`ds_flatten_block` 返回 `CMVector<std::pair<uint32_t, DSPartitionProduct>>`（363，first 应为 CMPartitionId）。
4. **ds_name_hasher.h——P1 级结构性缺口**：`DSNameHasherT<IdT>` 模板与六个实体别名全部以裸整型实例化（750-764：`DSCellNameHasher = DSNameHasherT<uint32_t,...>` 等）。hasher 是 id 的生产者（`emplace` 返回 id、`kInvalidId = numeric_limits<IdT>::max()`），不迁移则强类型链路在源头断链。StrongIdT 已提供 std::hash 与显式值构造，backends（`CMUnorderedMap<CMString, IdT>` / `tsl::htrie_map<char, IdT>`）可无缝换型；`ds_name_hasher.h:551 lcp_form_`（uint8 状态标记）顺带评估枚举化。**这是 design 模块 id 收敛的剩余主战场**（批次 B）。
5. **ds_name_mapper.h**：`DSNameMapperT<uint64_t>` 双维度共用裸整型实例化 + 运行时 `DSNameMapperKind` 分派（59,139-140）——强类型化后应拆 `DSNameMapperT<CMInstanceId>` / `DSNameMapperT<CMNetId>` 双实例化，Kind 枚举随之由类型系统编码（可删）；`set_block_hasher(uint32_t cell_id, ...)` 与 `injected_` 键（99,129）→ CMCellId；`:73,80,125` `const DSHierTree* tree_` 存储型观察裸指针成员——**违反 DEVELOPMENT_GUIDELINES §16「业务层全禁裸指针」（2026-09-11 裁定，emir 业务模块连非拥有观察也不用裸指针）**，先于 2026-09-16 裁定③即已违规，应改 CMSharedPtr<const DSHierTree>（宿主 DSDesign 的 hier_tree_ 为内联成员，需以 aliasing shared_ptr 或提升树为 CMSharedPtr 持有，落地时定形态）。
6. **ds_id_map.h**：`DSIdPartitionSlice::ids_/pids_`（52-53）、`DSIdPartitionSegment::id_start_/pids_/partition_of`（69-85）、`kIdMapNoPartition`（43）→ CMInstanceId/CMNetId（按 INST/NET 分表）+ CMPartitionId；`:133` `ds_collect_partition_id_slice(..., bool instance_kind, uint32_t partition_id)` 的 instance/net id 混流同一容器 + bool 分型——强类型化天然消除混流（批次 B）。
7. **ds_partition.h**：`DSSubPartition::partition_id_`（55）→ CMPartitionId；`DSDensityWeights::layer_factors_` 键 + `layer_factor(uint32_t)`（82-88）→ CMLayerId。xp_/yp_ 为网格坐标豁免。
8. **ds_union.h**：`net_a_/net_b_`（45-46）、`port_net_ids_`（62）、`root_of_/members_of_`（74,77）、`find/members`（82,84）→ CMNetId（批次 B）。
9. **ds_verify.h**：`partition_id_`（81）与 `primary_instance_ids_/instance_ids_/net_ids_/crossing_net_ids_`（100-105）→ 对应 CM id（批次 B）。
10. **ds_instance_pipeline.h / ds_net_pipeline.h**：ctx 产出字段 `cell_id/instance_id`（88,95）、`local_net_id`（net_pipeline:118）、`ds_fake_cell_id_base(uint32_t max_cell_id)`（159-160）、`ds_resolve_via_cell` 返回（net_pipeline:176）→ CM 类；`DSInstanceContext::placement_status` uint8（73）→ DSPlacementStatus；**两 ctx 的环境引用裸指针**（instance:78-84 design/block_data/fake_cells；net: stack/design/block_data/net_data）——业务层禁裸指针违规（§16），改 CMSharedPtr/CMSharedPtr<const T>；`:111 ds_instance_pipeline.cpp` `static_cast<int>(ctx.orient)` 把已定型枚举转回 int 喂 `place_from_def`（ds_transform_util.h:36,55 `int orient_int`）——typed→int→typed 往返，应改 GEOOrientation 直传，适配层（ds_def_adapter.cpp:540-542）在 Si2 回调边界一次转换（批次 B）。
11. **design_export.cpp**（:116,208-209）：`FLY_EXPORT_READONLY_ATTR("placement_status", &DSPin::placement_status_)` 直绑 uint8 字段——枚举定型后需改 caster（FLY_EXPORT_ENUM 注册或 getter static_cast<int>）；`:205-206` orient 以 int 透出可顺带评估枚举导出。id 字段经 export 保持 int 属既定裁定④，不算发现，但 ds_types 字段类型变更会使 `def_ro` 直接绑成员的编译断裂，落地时逐项核对（批次 B）。
12. **借用指针集参数（8 处，业务层违规）**：`CMVector<const T*>&` 形态——ds_merge.h:70-71（ds_build_hier_tree）、ds_partition.h:99-100（ds_merge_global_density）、ds_union.h:100,111、ds_flatten.h:320（finalize_from_flatten）、ds_id_map.h:127、ds_verify.h:187-190、ds_name_mapper.h:148（ds_make_name_mapper）。均系同步调用期借用（注释自证「观察指针集/借引用不拷贝」），按 §16 业务层口径仍应清退——改 `CMVector<CMSharedPtr<const T>>` 或调用侧持 shared 的集合形态（批次 B，与 5/10 同一注入语义改造）。
13. **容器视图返回指针（口径问题，低危）**：design 模块内 `find_cell/find_via_cell/connections_of/wires_of/rects_of/via_ids_of/via_instance_at/net_of/entries_of` 等「未命中 nullptr」返回——指向本对象容器内部，CMWeakPtr 不适用于子对象视图。建议维持现状并在 §16 补一条「容器内部视图返回」豁免口径，或改 `std::optional<std::reference_wrapper<const T>>`（后者侵入面大，性价比低，倾向前者）。

---

## 豁免清单（判定合理，附理由）

| 项 | 位置 | 理由 |
|---|---|---|
| CM_FLAGS 位组 | 全仓（DSPin/DSCell/DSNetConnection/DSGeomEntry/TMNameTiming 等） | 裁定③任务书明示「CM_FLAGS 宏生成的位组属既有框架件豁免」；位标志非枚举语义 |
| FLY_SERIALIZE / StrongIdT 模板内部机制 | serialization_macros.h / strong_id.h | 序列化宏内部与框架模板机器，任务书明示豁免；strong_id 手写直通已按字节级兼容设计 |
| Python 绑定 new + take_ownership | storage_export.cpp:42-79（有文档+显式策略）、agent_export.cpp:51-63、solver_export.cpp:147、database.h:50 | 裁定③明示豁免边界；nanobind automatic→take_ownership 已核实（nb_cast.h infer_policy），内存安全成立 |
| sqlite3* db_ | metrics_db.h:99 | 第三方 C 库不透明句柄，C API 边界 |
| IoEvent* / iovec* / defiUserData / defiBox* 等 | epoll_multiplexer.h:28、tcp_socket.h:25、ds_def_adapter.cpp 全部回调 | 系统调用出参缓冲 / 第三方 C 回调契约 |
| std::ostream* dest_ | compressing_streambuf.h:56 | streambuf sink 标准库惯用法，同步栈内借用（§16 场景①，基础设施层适用） |
| 测试访问器 .get() | master_agent.h:179,181 | 测试专用、生命周期同宿主 CMUniquePtr |
| task_id/worker_id/conn_id/rpc_id/writer_id 等 | network/agent/task/storage 各处 | 非EDA实体编号，不在 CM id 体系裁定范围（裁定①枚举的实体清单为 cell/pin/instance/net/via/layer/partition/树区间/clock 族） |
| 计数/坐标/网格下标 | ds_verify.h 各 count、DSDensityGrid col/row、xp_/yp_、entry_index_(size_t)、区间 count | 物理量与计数非编号；emir_ids.h 明示「区间长度是计数保持裸型」 |
| emir/lib 全 name-keyed | lib_types.h:6 | 设计裁定：lib 阶段不分配 id，cell id 由 design 阶段分配 |
| GEOOrientation / LogLevel / MessageType / TaskStatus 等已定型枚举 | transform.h:55、logger.h:16、message_types.h 等 | 已 enum class 定型存储，无欠账；MessageType 另有线上值域校验先例 |
| 消息级别（任务书点名核查项） | message_registry.h:96 `CMUnorderedMap<CMString, LogLevel>` | 全链 LogLevel 定型传递，未发现裸整型承载点（全 src grep static_cast<LogLevel> 零命中） |

## 各维度小结

| 维度 | 评价 |
|---|---|
| 维度1（id 强类型） | 存量主体仅 timing clock id 一项；主欠账集中在 design 模块（在途），其中 **DSNameHasherT/DSNameMapperT 裸整型模板是结构性源头**，ds_types 字段迁移（在途）不触此处则链路在 id 生产端断链 |
| 维度2（枚举定型） | 存量主体最实的一条线：**CompressionType 跨层裸整型 + 边界无校验**（P1）与 PeerRpcWireStatus 同文件违例（P1）；design 侧枚举字段 uint8 存储在途 diff 未覆盖，落地后仍欠 |
| 维度3（指针智能化） | 基础设施层整体干净（PeerStreamWriter::srv_ CMSharedPtr 化的既往修复在位）；存量主体剩 task 模块两个存储型观察成员；design 侧 tree_/ctx/借用集违反 2026-09-11 业务层全禁裸指针先行裁定，量最大 |
| 绑定面 | id 保持 int 属既定裁定；三处 new+裸指针返回内存安全成立，建议对齐 storage 的显式 take_ownership 先例 |

## 亮点

- b68fd56 的 StrongIdT 设计（显式构造、减法特例、std::hash、手写序列化直通保字节级兼容、跨类运算编译期拒绝）为后续收敛提供了完整机器，在途 ds_types 迁移与其衔接顺畅。
- 线上 MessageType 的 `is_valid_message_type` 值域校验先例，可直接复刻为 CompressionType 收敛的边界校验模板。
- DEVELOPMENT_GUIDELINES §16 判据（“一个 reset 能让另一处悬垂即必须场景”）+ PeerStreamWriter 修复案例，使裁定③的豁免/违例边界在仓内有可执行先例可依。

**批次归组建议汇总**：批次 A（timing 重启）= P2-9；批次 B（三套输入件/design 链）= 在途清单 B-2 至 B-13 全部；批次 C（独立收敛）= P1-1、P1-2、P2-3~8、P2-10~11、P3-12~14（CompressionType 下沉可与 B 并行，无文件交叉）。