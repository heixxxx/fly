"""build_design_db flow 的并行任务 + 全局汇总装配（裁定 ①：无业务合并
语义的解析不采用 MapReduceJob——阶段链以 as_task 直书）。

阶段链（实施计划 §4；阶段名为业务语汇，括注方案 design-db-phase2-plan
.md 的阶段编号交叉引用）：
  tech lef（单 task：Stack 基准 + tech via 集合含 VIARULE 展开模板，㉚）
  → 每 cell lef 一 task（局部 id 中间产物）
  → cell lef 汇总 task（ds_merge_cell_lef 逐个合入：统一 cell id /
    pin hasher 重挂 + pin_id 回填 / via 合入 / 几何重挂 + DSGN::0001/
    0005）→ 写 DSDesign 中间态 + DSPinGeometry 正式对象
  → 分叉：lib merge task（读 lib db，DSDesign::merge_lib → DSGN::0002/
    0003/0004 + DSPinTables 正式对象）∥ DEF 头扫描每 DEF 一 task（头扫
    描，大段真跳过）
  → DEF 头汇总 task（ds_merge_def_header → cell 全集快照 DSDesign：
    block cell 进 cell hasher + port pin id 分配回填 + port 几何重挂；
    写临时对象——正式 DSDesign 由实例解析汇总写定，正式对象禁止二次
    覆盖写）
  → COMPONENTS 解析每 DEF 一 task（实例责任链 ∥ 网名扫描，同一遍 DEF
    读取：实例表/密度通道/统计 + local 网名空间 → 临时产物；undefined
    cell → fake cell 兜底 DSGN::0007，⑲/⑳。R7 ㊵② 落盘拆分：名字伴生
    对象 DSBlockNames_<i>（instance/net 两 hasher，解析期即终态——fake
    引用重映射不触及 local id 与名字）由本任务唯一写定正式对象）
  → 网内容解析每 DEF 一 task（网内容责任链 ∥ 分批多阶段，裁定 ③：批
    大小 alpha 键 net_batch_size；读 DSBlockNames_<i> 注入临时产物的网
    名空间（⑨ 对齐 local net id）+ via cell 权威表快照 ⑪ → 正式
    DSNet_<def 序号> 产物对象唯一写定；undefined via 兜底 DSGN::0008）
  → 网内容汇总 task（统计合并 → DSGN::0009 网数/几何/via instance 数）
  → 层级树 task（层级树构建 + 起始编号分配，⑧⑨⑮：消费实例解析临时产
    物 + cell 全集快照 + 网内容正式产物的 via 计数一次建全三类区间——
    via 计数物理依赖要求网内容解析先行；主 DEF = 唯一无父者，多根/
    零根/环 → raise（D22）→ 层级树临时对象）
  → 实例解析汇总 task（ds_merge_block_build 逐 DEF 并入：fake cell 进
    全局 cell 表（id 保持任务内分配值 ⑳，冲突顺延）+ 层级树嵌容器（正
    式 DSDesign 写定前完成树构建，容器唯一写定原则）+ 统计日志 → 正式
    DSBlock_<def 序号> 产物对象唯一写定（不含名字，名字在伴生对象）→
    写正式 DSDesign）
  → 全局密度合并 + 分区决策 task（2026-09-12/13 裁定：层级树自底
    向上 + 格值面积比例分摊 D10 A → global_density 独立对象；行列前缀
    和切分 → DSDesign 补 partitions_ 重写。依赖层级树 + 全部 per-DEF
    正式产物 + stack + alpha_settings 对象。alpha 四键 target_partitions/
    partition_count/partition_target_density/density_channel_weights 任务
    内 read_object 读回（2026-09-13 裁定：声明式 DSAlphaSettings，非法值
    WARN 回退不 raise））
  → 跨块连接归并任务组（并查集，2026-09-13 裁定：仅 port 相连网、
    两层树、单对象——与全局密度任务同级并行，依赖同为层级树 + 网内容
    正式产物）：
    block 名清单小任务（DSBlockNames_<i> 的 block 名 → def 序号，slice
    任务定位子定义网产物用）→ per-DEF slice 任务并行收集局部 (父网, 子
    网) 边（对接键 = 同一块实例 + 同名 port）→ 汇总任务（两层化 + root
    规范化 → net_union 正式对象 + slice 清理 + 悬空 port DSGN::0018）
  → 展平分区任务组（flatten 展平 + 分区保存，两级任务 + 小 DEF 聚合，
    2026-09-13 裁定补记①-⑤ + D26 + 同日 partition 网数据结构重组终态
    + 2026-09-14 拆分裁定与 net id 0 专属 OBS 裁定；依赖分区表 + 层级
    树 + 全部 per-DEF 产物 + DSDesign）：分区编排任务（分组编排：预估
    = DEF 文件大小 × 树上实例化次数，≥ alpha def_aggregate_threshold
    独占任务、低于阈值贪心聚合；重名定义保留首份；worker 上动态提交下
    游任务——同 solver kickoff 先例）→ per-组展开任务并行（每组只读本
    组 def 产物——每份 DEF 数据只读一次；ds_flatten_block 全位置展开 +
    分区分流，每任务对全部分区各写一份分片临时对象）→ 每分区一合并任
    务（真实合并语义：merge 全部相关分片 → 六类正式对象
    PART_{xp}_{yp}.{GEOMETRY,GEOMETRY_PG,INSTANCES,INST_CONNECTIONS,
    NETS,NETS_PG} 唯一写定 + id 映射片段 + pg 网片段临时对象）→ 全局
    pg 网 id 集汇总任务（2026-09-13 重组裁定：读全部分区 pg 片段 → 两
    set 去重合并 → "pg_nets" 正式对象——debug API is_pg O(1) 判定数据
    源）
  → 汇总校验任务组（校验 + 冻结前置，由分区编排任务动态提交排在
    freeze 之前，2026-09-13 校验分级裁定）：每分区一校验任务（并行读
    单分区六类正式产物——红线：不跨区读）→ 全局汇总校验任务（读全部
    校验结果 + 树 + DSDesign + stack + global_density + net_union +
    per-DEF 产物与伴生名；损坏类——并查集不自洽/分区覆盖断裂/namemap
    双向不一致——经 ds_verify_report_or_fatal fatal 退出（码 80 +
    master 联动），report 不落盘、freeze 依赖缺失——损坏库不冻结；
    观测类——id 连续性空洞/重复 DSGN::0022、密度守恒 primary 口径偏差
    DSGN::0023——user warn 不阻断；统计汇总 DSGN::0024 INFO；报告写
    verify_report 正式对象）
  → freeze task（依赖 DSDesign/DSStack/DSPinTables/DSPinGeometry/
    DSBlock_*/DSBlockNames_*/DSNet_*/global_density/net_union/全部分区对
    象/pg_nets/verify_report 写完 + 中间对象清理（分片 + pg 片段 + 校验
    结果临时对象）——由分区编排任务动态提交）

流程入口 build_design_db 在 ds_db.py（UserDoc + Schema + @register_flow）。
约定（择简，UserDoc 同步注明）：lef_paths[0] 为 tech lef（确立 DBU 基准
与层堆叠），其余为 cell lef。
"""

from uuid import uuid4

from fly import as_task

from .ds_db import DesignDb
from .ds_export import (
    EXDSBlockBuildData,
    EXDSDesign,
    EXDSHierTree,
    EXDSNetBuildData,
    EXDSNetUnionSlice,
    EXDSPartitionProduct,
    EXDSPinGeometry,
    ds_build_hier_tree,
    ds_build_net_union,
    ds_build_pg_net_set,
    ds_collect_net_union_slice,
    ds_collect_inst_id_slice,
    ds_collect_net_id_slice,
    ds_collect_pg_net_slice,
    ds_decide_partitions,
    ds_flatten_block,
    ds_merge_block_build,
    ds_merge_cell_lef,
    ds_merge_def_header,
    ds_merge_global_density,
    ds_merge_inst_id_partition_slices,
    ds_merge_net_id_partition_slices,
    ds_net_union_child_indexes,
    ds_verify_design,
    ds_verify_partition,
    ds_verify_report_or_fatal,
)
from .ds_utils import (
    ds_parse_cell_one,
    ds_parse_def_components_one,
    ds_parse_def_nets_one,
    ds_parse_def_one,
    ds_parse_tech_one,
)


def _tmp_key(uid: str, name: str) -> str:
    return f"__dsn__{uid}__{name}"


# ── tech lef（单 task，确立 DBU 基准 + 层堆叠）──────────────────────

@as_task(inputs=lambda db, path, stack_key, vias_key: [])
def _tech_lef_task(db, path, stack_key, vias_key):
    from .ds_export import EXDSStack
    stack = EXDSStack()
    tech_vias = ds_parse_tech_one(path, stack)
    db.write_object(stack_key, stack, save_to_db=True)
    db.write_object(vias_key, tech_vias, save_to_db=False)


# ── 每 cell lef 一 task（局部 id 中间产物 + 失败标记）─────────────────

@as_task(inputs=lambda db, path, stack_key, design_key, geoms_key, vias_key,
         failed_key: [
    db.get_full_name(stack_key)
])
def _cell_lef_task(db, path, stack_key, design_key, geoms_key, vias_key,
                   failed_key):
    stack = db.read_object(stack_key)
    part_design, part_geoms, part_vias, stats = ds_parse_cell_one(path, stack)
    db.write_object(design_key, part_design, save_to_db=False)
    db.write_object(geoms_key, part_geoms, save_to_db=False)
    db.write_object(vias_key, part_vias, save_to_db=False)
    # 失败标记随产物传递（流程错误处理范式 2026-09-13：单文件语法错误不
    # FAILED，产物已被 adapter 清空；汇总层据此发 DSGN::0014 / fatal 0015）
    db.write_object(failed_key,
                    [path] if stats.parse_failed_count else [],
                    save_to_db=False)


# ── cell lef 汇总：统一 cell id 分配 + pin hasher 重挂 + via/几何合入 ─

@as_task(inputs=lambda db, part_tuples, stack_key, tech_vias_key, merged_key,
         geoms_key: [
    db.get_full_name(stack_key), db.get_full_name(tech_vias_key)
] + [db.get_full_name(k) for tup in part_tuples for k in tup])
def _cell_lef_merge_task(db, part_tuples, stack_key, tech_vias_key,
                         merged_key, geoms_key):
    global_design = EXDSDesign()
    global_geoms = EXDSPinGeometry()

    conflicts = 0
    failed_paths = []
    for design_key_i, geoms_key_i, vias_key_i, failed_key_i in part_tuples:
        part_design = db.read_object(design_key_i)
        part_geoms = db.read_object(geoms_key_i)
        part_vias = db.read_object(vias_key_i)
        failed_paths.extend(db.read_object(failed_key_i))
        for via in part_vias:
            part_design.add_via_cell(via)  # 局部 via 并入 part design
        conflicts += ds_merge_cell_lef(global_design, part_design,
                                       global_geoms, part_geoms)
    tech_vias = db.read_object(tech_vias_key)
    conflicts += ds_merge_def_header(global_design, [], [], global_geoms,
                                     EXDSPinGeometry(), tech_vias)
    if conflicts:
        from log import INFO
        INFO(f"cell lef merge: {conflicts} duplicate macro/via dropped "
             f"(keep first)")

    # 流程错误处理范式（2026-09-13 裁定）：部分文件失败 → DSGN::0014
    # （ERROR，列文件；cell 缺失由 fake cell 承接引用，DSGN::0007）+ 空产
    # 物照常汇总（依赖链满足）；全部失败 → DSGN::0015 fatal（与 lib 全败
    # 同口径，码 80 退出 + master 联动）。
    if failed_paths:
        total = len(part_tuples)
        if len(failed_paths) >= total:
            from fly import fatal_message
            fatal_message("DSGN::0015", 0,
                          f"all {total} cell lef file(s) failed to parse, "
                          f"downstream data cannot be produced: "
                          f"{', '.join(failed_paths)}")
            # fatal_message 不返回（_exit(80)）——下方 DSGN::0014 仅部分
            # 失败路径可达（review 2026-09-13：显式化防误读）
        from fly import message
        message("DSGN::0014", 0,
                f"{len(failed_paths)}/{total} cell lef file(s) failed to "
                f"parse (skipped, undefined references become fake cells): "
                f"{', '.join(failed_paths)}")

    db.write_object(merged_key, global_design, save_to_db=False)
    # macro pin 几何中间产物（临时对象——正式 DSPinGeometry 由 DEF 头汇
    # 总合并 port 几何后唯一写定，正式对象禁止二次覆盖写）
    db.write_object(geoms_key, global_geoms, save_to_db=False)


# ── lib merge：lib cell ↔ lef cell 结构 merge（与 DEF 头扫描并行）────

@as_task(inputs=lambda db, merged_key, lib_obj_name, tables_key, s3_key: [
    db.get_full_name(merged_key),
    db.get_full_name(lib_obj_name),
])
def _merge_lib_task(db, merged_key, lib_obj_name, tables_key, s3_key):
    design = db.read_object(merged_key)
    lib = db.read_object(lib_obj_name)
    matched = design.merge_lib(lib)
    from log import INFO
    INFO(f"merge_lib: {matched} cells matched with lib")
    db.write_object(s3_key, design, save_to_db=False)
    db.write_object(tables_key, design.get_pin_tables(), save_to_db=True)


# ── DEF 头扫描：每 DEF 一 task（头部扫描，大段真跳过）────────────────

@as_task(inputs=lambda db, path, stack_key, cells_key, geoms_key, vias_key,
         obs_key: [
    db.get_full_name(stack_key)
])
def _def_header_task(db, path, stack_key, cells_key, geoms_key, vias_key,
                     obs_key):
    stack = db.read_object(stack_key)
    block_cells, port_names, port_geoms, vias, obstructions = \
        ds_parse_def_one(path, stack)
    db.write_object(cells_key, (block_cells, port_names), save_to_db=False)
    db.write_object(geoms_key, port_geoms, save_to_db=False)
    db.write_object(vias_key, vias, save_to_db=False)
    # obstruction 元组列表（S4 收录，2026-09-13 D17 修订）——S5a 任务回填
    # per-DEF 产物（正式对象随实例解析汇总唯一写定，本产物为临时）
    db.write_object(obs_key, obstructions, save_to_db=False)


# ── DEF 头汇总：block cell / port pin id / via 权威表 → cell 全集快照 ─

@as_task(inputs=lambda db, s3_key, part_keys, snapshot_key, geoms_key,
         macro_geoms_key: [
    db.get_full_name(s3_key),
    db.get_full_name(macro_geoms_key),
] + [db.get_full_name(k) for tup in part_keys for k in tup])
def _def_header_merge_task(db, s3_key, part_keys, snapshot_key, geoms_key,
                           macro_geoms_key):
    design = db.read_object(s3_key)
    global_geoms = db.read_object(macro_geoms_key)  # cell lef 汇总的 pin 几何
    for part_keys_i in part_keys:
        cells_key, geoms_key_i, vias_key = part_keys_i[:3]
        block_cells, port_names = db.read_object(cells_key)
        port_geoms = db.read_object(geoms_key_i)
        vias = db.read_object(vias_key)
        ds_merge_def_header(design, block_cells, port_names, global_geoms,
                            port_geoms, vias)
    # 正式 DSPinGeometry（macro + port 几何全集）唯一写定；写入顺序红线：
    # 先几何后 DSDesign——freeze 以正式对象齐备为依赖，DSDesign 最后
    # 落地，规避 freeze 先行冻结 db 使几何写失败
    db.write_object(geoms_key, global_geoms, save_to_db=True)
    # cell 全集快照写临时对象（实例解析每 DEF 任务的 fake id 基址输入 =
    # 该快照的 cell 总数；正式 DSDesign 由实例解析汇总唯一写定）
    db.write_object(snapshot_key, design, save_to_db=False)


# ── COMPONENTS 解析：每 DEF 一 task（实例责任链 ∥ 网名扫描，同一遍读取）─

@as_task(inputs=lambda db, path, stack_key, snapshot_key, block_key,
         names_key, obs_key, fake_key, bin_dbu, lcp_name_arena: [
    db.get_full_name(stack_key),
    db.get_full_name(snapshot_key),
    db.get_full_name(obs_key),
])
def _components_def_task(db, path, stack_key, snapshot_key, block_key,
                         names_key, obs_key, fake_key, bin_dbu,
                         lcp_name_arena):
    stack = db.read_object(stack_key)
    design = db.read_object(snapshot_key)
    block_data = EXDSBlockBuildData()
    # S4 收录的 obstruction 回填（2026-09-13 D17 修订；inputs 声明依赖
    # 保证头扫描产物先行）
    block_data.set_obstructions(db.read_object(obs_key))
    # fake cell 数据经独立临时对象传递（2026-09-13 裁定：产物本体不含
    # 副本——汇总任务并入全局表后即与 cells_ 冗余）
    stats, fake_cells = ds_parse_def_components_one(path, stack, design,
                                                    block_data, bin_dbu)
    db.write_object(fake_key, fake_cells, save_to_db=False)
    # R8d 落盘封口（裁定 55：alpha 键 lcp_name_arena——解析完成后、名字
    # 伴生对象落盘前的单一封口点）：true = instance/net 两 hasher id→name
    # 侧 LCP 后缀压缩封口（DSBlockNames_<i> 落盘即封口形态，读回按标记
    # 自识别）；false（缺省）= 形态一默认零变化
    if lcp_name_arena:
        block_data.finalize_names_for_save(True)
    # ㊵② 落盘拆分：名字伴生对象（instance/net 两 hasher，解析期即终态
    # ——fake 引用重映射只改 cell id 引用、不触及 local id 与名字）由本
    # 任务唯一写定正式对象（extract_names 拷出两 hasher + block 名）；
    # 网内容解析与按需名字查询由此独立取用
    db.write_object(names_key, block_data.extract_names(), save_to_db=True)
    # 写临时对象（fake cell 引用可能随汇总顺延重映射——正式对象由实例
    # 解析汇总唯一写定，规避 DUPLICATE_SKIPPED 静默丢弃二次写）
    db.write_object(block_key, block_data, save_to_db=False)
    if block_data.stats.fake_cell_count or stats.skipped_net_count:
        from log import INFO
        INFO(f"components '{path}': {block_data.stats.fake_cell_count} fake "
             f"cells, {stats.skipped_net_count} duplicate nets skipped")


# ── 层级树：构建 + 起始编号分配（⑧⑨⑮；消费实例解析 + 网内容计数）───

@as_task(inputs=lambda db, snapshot_key, temp_block_keys, names_keys,
         formal_net_keys, hier_key: [
    db.get_full_name(snapshot_key),
] + [db.get_full_name(k) for k in temp_block_keys] +
    [db.get_full_name(k) for k in names_keys] +
    [db.get_full_name(k) for k in formal_net_keys])
def _hier_task(db, snapshot_key, temp_block_keys, names_keys, formal_net_keys,
               hier_key):
    # 消费实例解析临时产物（实例计数与 block 引用关系；fake 引用重映射
    # 不触及两者——fake cell 恒非 block cell）+ cell 全集快照（block cell
    # 判别与命名）+ 名字伴生对象（㊵② 临时产物落盘不含 hasher——树构建
    # 反查 block instance 实例名 ⑮ 需注入 DSBlockNames_<i>）+ 网内容正式
    # 产物（per-DEF via instance 统计，via 区间输入）。via 计数物理依赖
    # 要求网内容解析先行：层级树一次建全三类区间，正式 DSDesign 写定前
    # 完成树构建（容器唯一写定）。
    design = db.read_object(snapshot_key)
    blocks = [db.read_object(k) for k in temp_block_keys]
    from log import INFO
    for block_data, names_key in zip(blocks, names_keys):
        # attach 为拷贝注入（hasher 归属留伴生对象持有方，只读副本回填
        # 运行时字段——树构建反查实例名 ⑮ 与 mapper 注入共用同一来源）
        block_data.attach_names(db.read_object(names_key))
    INFO(f"hier attach done: "
         f"net_counts={[b.net_count for b in blocks]}")
    nets = [db.read_object(k) for k in formal_net_keys]
    tree = ds_build_hier_tree(blocks, nets, design)
    INFO(f"hier tree: {tree.node_count} block instances, top="
         f"'{tree.design_name}'")
    db.write_object(hier_key, tree, save_to_db=False)


# ── 实例解析汇总：fake cell 并入全局表（id 保持 ⑳）→ 正式对象唯一写定 ─

@as_task(inputs=lambda db, snapshot_key, temp_block_keys, fake_keys,
         formal_block_keys, design_key, hier_key: [
    db.get_full_name(snapshot_key),
    db.get_full_name(hier_key),
] + [db.get_full_name(k) for k in temp_block_keys]
  + [db.get_full_name(k) for k in fake_keys])
def _components_merge_task(db, snapshot_key, temp_block_keys, fake_keys,
                           formal_block_keys, design_key, hier_key):
    design = db.read_object(snapshot_key)
    # 层级树嵌容器（⑬；层级树产物——在正式 DSDesign 写定前完成树挂载）
    tree = db.read_object(hier_key)
    assert isinstance(tree, EXDSHierTree)
    design.set_hier_tree(tree)
    total_leaf = 0
    total_net = 0
    total_fake = 0
    for temp_key, fake_key, formal_key in zip(temp_block_keys, fake_keys,
                                              formal_block_keys):
        block_data = db.read_object(temp_key)
        # fake cell 数据经独立临时对象传入（2026-09-13 裁定：产物本体
        # 不含副本——并入全局表后即冗余，临时对象由 freeze 清理）
        fake_cells = db.read_object(fake_key)
        total_fake += ds_merge_block_build(design, block_data, fake_cells)
        total_leaf += block_data.stats.instance_count
        total_net += block_data.net_count
        # per-DEF 产物正式写定（实施计划 §3.2「即产即落盘」；fake 引用
        # 重映射已就位，层级树/网内容/下游直接消费：实例计数 → 编号区
        # 间，local net id → 网内容解析）。不含名字——名字在 DSBlockNames_<i>
        # 伴生对象（㊵② 落盘拆分，读取经 load_block_names / mapper）
        db.write_object(formal_key, block_data, save_to_db=True)
    from log import INFO
    INFO(f"components merge: {total_leaf} instances, {total_net} nets, "
         f"{total_fake} fake cells merged")
    # 正式 DSDesign 唯一写定（DEF 头汇总产物为临时快照，正式对象禁止
    # 二次覆盖写——本任务是其唯一写定点）
    db.write_object(design_key, design, save_to_db=True)


# ── 网内容解析：每 DEF 一 task（责任链 ∥ 分批多阶段，同一遍流式读取）──

@as_task(inputs=lambda db, path, stack_key, snapshot_key, block_key,
         names_key, net_key, stats_key, net_batch, bin_dbu: [
    db.get_full_name(stack_key),
    db.get_full_name(snapshot_key),
    db.get_full_name(block_key),
    db.get_full_name(names_key),
])
def _nets_def_task(db, path, stack_key, snapshot_key, block_key, names_key,
                   net_key, stats_key, net_batch, bin_dbu):
    stack = db.read_object(stack_key)
    design = db.read_object(snapshot_key)
    # ⑨ local net id 对齐：消费实例解析临时产物 + 名字伴生对象（㊵②：
    # 网名空间随 DSBlockNames_<i> 落盘，读回 attach_names 注入运行时字
    # 段——临时产物落盘不带 hasher）。via cell 权威表快照 = 正式对象，
    # 实例解析汇总仅增 fake cell。网内容解析先于汇总：其 via instance
    # 计数是层级树 via 区间的输入，且临时产物输入使本任务不依赖正式
    # DSDesign（汇总单点写定时序保持）
    block_data = db.read_object(block_key)
    # attach 为拷贝注入（hasher 归属留伴生对象持有方；网内容对齐 ⑨ 只读）
    block_data.attach_names(db.read_object(names_key))
    net_data = EXDSNetBuildData()
    stats = ds_parse_def_nets_one(path, stack, design, block_data, net_data,
                                  bin_dbu, net_batch)
    # 正式 DSNet_<序号> 唯一写定（产物无全局重排语义，任务直写）
    db.write_object(net_key, net_data, save_to_db=True)
    # 统计经轻量 dict 传汇总 task（EXDSDefNetsStats 不做 pickle 面）
    db.write_object(stats_key, {
        "net": stats.net_count,
        "connection": stats.connection_count,
        "wire": stats.wire_count,
        "rect": stats.rect_count,
        "via_instance": stats.via_instance_count,
        "skipped_via": stats.skipped_via_count,
        "skipped_net": stats.skipped_net_count,
        "skipped_invalid_connection": stats.skipped_invalid_connection_count,
        "batch": stats.batch_count,
    }, save_to_db=False)
    if stats.skipped_via_count or stats.skipped_net_count or \
            stats.skipped_invalid_connection_count:
        from log import INFO
        INFO(f"nets '{path}': {stats.skipped_via_count} undefined via "
             f"references skipped, {stats.skipped_net_count} unknown nets, "
             f"{stats.skipped_invalid_connection_count} invalid connections "
             f"skipped")


# ── 网内容汇总：统计合并 → DSGN::0009（网数/几何/via instance 数）─────

@as_task(inputs=lambda db, stats_keys: [
    db.get_full_name(k) for k in stats_keys
])
def _nets_summary_task(db, stats_keys):
    total = {"net": 0, "connection": 0, "wire": 0, "rect": 0,
             "via_instance": 0, "skipped_via": 0, "skipped_net": 0,
             "skipped_invalid_connection": 0, "batch": 0}
    for key in stats_keys:
        stats = db.read_object(key)
        for name in total:
            total[name] += stats.get(name, 0)
    from fly import message
    message("DSGN::0009", 0,
            f"net content summary: {total['net']} nets "
            f"({total['connection']} connections, {total['wire']} wires, "
            f"{total['rect']} rects, {total['via_instance']} via instances "
            f"in {total['batch']} batches), "
            f"{total['skipped_via']} undefined vias skipped, "
            f"{total['skipped_invalid_connection']} invalid connections "
            f"skipped")


# ── S8：全局密度合并 + 分区决策（core/extend 双区域，2026-09-12/13 裁定）─

@as_task(inputs=lambda db, stack_key, hier_key, block_keys, net_keys,
         design_key, density_key, settings_key: [
    db.get_full_name(stack_key),
    db.get_full_name(hier_key),
    db.get_full_name(design_key),
    db.get_full_name(settings_key),
] + [db.get_full_name(k) for k in block_keys] +
    [db.get_full_name(k) for k in net_keys])
def _partition_task(db, stack_key, hier_key, block_keys, net_keys, design_key,
                    density_key, settings_key):
    from log import INFO
    # 依赖 S6 树（临时产物）+ 全部 per-DEF 正式产物（S5a 实例密度 +
    # S5b 网侧逐层密度）+ stack（w_eff 判定）+ alpha_settings 对象（建库
    # 时写定）。S7 与本任务同级并行、互不依赖（红线：S8 只依赖 S6+S5b
    # 产物）。
    stack = db.read_object(stack_key)
    tree = db.read_object(hier_key)
    blocks = [db.read_object(k) for k in block_keys]
    nets = [db.read_object(k) for k in net_keys]
    design = db.read_object(design_key)
    # alpha 四键（2026-09-13 裁定：任务侧统一读回，inputs 声明依赖，与
    # wait_obj/判死语义一致；normalize 兜底旧对象缺键/未知属性）。C++
    # ds_decide_partitions 签名不变——任务内解析后散参传入；'{x}x{y}'
    # 解析失败 / 非正 target_density 仍由 C++ 侧 DSGN::0013 提醒回退
    settings = db.read_object(settings_key)
    settings.normalize()
    weights = settings.density_channel_weights
    target_partitions = settings.target_partitions or ""  # None/空串 = 未设置
    partition_count = settings.partition_count
    target_density = settings.partition_target_density

    global_density = ds_merge_global_density(tree, blocks, nets)
    partitions = ds_decide_partitions(global_density, stack,
                                      weights["instance"], weights["metal"],
                                      weights["via"], target_partitions,
                                      partition_count, target_density)
    design.set_partitions(partitions)
    # DSDesign 补分区字段重写（S8 按方案重写落盘）：正式对象已在汇总任务
    # 写定——先 remove 规避 DUPLICATE_SKIPPED 静默丢弃二次写；freeze 依赖
    # 本任务末尾才写的 global_density 正式对象（重写先于该写完成），不会
    # 先行冻结
    db.remove_object(design_key)
    db.write_object(design_key, design, save_to_db=True)
    db.write_object(density_key, global_density, save_to_db=True)
    INFO(f"partition: {len(partitions)} partitions, global density "
         f"total={global_density.total_count} "
         f"metal={global_density.metal_total} via={global_density.via_total}")


# ── S7：跨块连接归并（并查集；2026-09-13 裁定：id 域 = global net id、
# 仅 port 相连网参与（internal net 绝不入表）、两层树 root = 层级最高的
# 网/同级最小 global id、悬空 port 照常入表 + 计数、单对象不分块。两级
# 任务：per-DEF slice 并行收集 + 单任务汇总——与 S8 同级并行，依赖同为
# S6 树 + S5b 正式产物）──────────────────────────────────────────────

@as_task(inputs=lambda db, names_keys, block_names_key: [
    db.get_full_name(k) for k in names_keys
])
def _net_union_names_task(db, names_keys, block_names_key):
    # block 名清单（def_paths 序）：slice 任务把树上 children 的 block
    # cell 名映射回 def 序号（定位子定义网产物）。名字伴生对象轻量，
    # N 次读仅此一遭——slice 任务据此只读本 def 引用的子定义网产物
    #（避免每任务全量重复读，同 S9 按定义切分的 I/O 精神）
    db.write_object(block_names_key,
                    [db.read_object(k).block_name for k in names_keys],
                    save_to_db=False)


@as_task(inputs=lambda db, hier_key, block_names_key, net_keys, index,
         slice_key: [
    db.get_full_name(hier_key), db.get_full_name(block_names_key)
] + [db.get_full_name(k) for k in net_keys])
def _net_union_slice_task(db, hier_key, block_names_key, net_keys, index,
                          slice_key):
    # per-DEF 局部收集（每父块 DEF 一任务）：本 def 网产物 + 树 + 本 def
    # 引用的各子定义网产物 → (父网, 子网) union 边 + 本 def port 网 local
    # id 集（悬空判定素材）。对接键 = 同一块实例 + 同名 port（S5b 连接表
    # 名字形态：父侧 (子实例名, port 名) × 子侧 ("PIN", port 名)，两侧都
    # 是字符串）。slice 为临时对象，汇总合并后 remove
    tree = db.read_object(hier_key)
    block_names = db.read_object(block_names_key)
    # 重名 def（已被 S4 DSGN::0001 / S6 emplace 保留首份 + WARN）：非首份
    # 序号不收集——review 2026-09-13：其连接表会按 block 名反查命中首份
    # 的实例化位置，产生首份定义中不存在的边造成错误归并；空 slice 在
    # 汇总侧天然安全跳过（无 block_name 无边无 port 网）
    if block_names.index(block_names[index]) != index:
        db.write_object(slice_key, EXDSNetUnionSlice(), save_to_db=False)
        return
    own = db.read_object(net_keys[index])
    child_nets = [db.read_object(net_keys[j]) for j in
                  ds_net_union_child_indexes(tree, block_names, index)]
    slice_obj = ds_collect_net_union_slice(tree, own, child_nets)
    db.write_object(slice_key, slice_obj, save_to_db=False)


@as_task(inputs=lambda db, hier_key, slice_keys, union_key: [
    db.get_full_name(hier_key)
] + [db.get_full_name(k) for k in slice_keys])
def _net_union_summary_task(db, hier_key, slice_keys, union_key):
    # 全局汇总（单任务）：合并全部局部边集 → 两层化 + root 规范化（层级
    # 最高、同级最小 global id）→ net_union 正式对象唯一写定 + slice 清
    # 理；悬空 port（root = 自身）DSGN::0018 计数提醒（不 raise，dev-rules
    # §7）
    tree = db.read_object(hier_key)
    slices = [db.read_object(k) for k in slice_keys]
    union = ds_build_net_union(tree, slices)
    db.write_object(union_key, union, save_to_db=True)
    for key in slice_keys:
        db.remove_object(key)
    from log import INFO
    INFO(f"net union: {union.class_count} classes, "
         f"{union.dangling_count} dangling port nets")
    if union.dangling_count:
        from fly import message
        message("DSGN::0018", 0,
                f"{union.dangling_count} dangling port net(s) not connected "
                f"by any parent net (root = itself)")


# ── 展平分区：flatten 展平 + 分区保存（两级任务 + 小 DEF 聚合；
#    2026-09-13 裁定补记①-⑤ + D26）──────────────────────────────────

@as_task(inputs=lambda db, design_key, global_density_key, hier_key,
         block_names_key, alpha_key, block_keys, net_keys, names_keys,
         def_paths, slice_prefix, verify_prefix, stack_key,
         net_union_key, id_slice_prefix, pg_slice_prefix, formal_keys,
         temp_keys: [
    db.get_full_name(design_key), db.get_full_name(global_density_key),
    db.get_full_name(hier_key), db.get_full_name(block_names_key),
    db.get_full_name(alpha_key),
])
def _partition_plan_task(db, design_key, global_density_key, hier_key,
                         block_names_key, alpha_key, block_keys,
                         net_keys, names_keys, def_paths, slice_prefix,
                         verify_prefix, stack_key, net_union_key,
                         id_slice_prefix, pg_slice_prefix, formal_keys,
                         temp_keys):
    """分区编排计划（依赖分区表（全局密度任务写定）+ 层级树 + block 名
    清单 + alpha 设置；worker 上动态提交展开/合并/freeze 链——同 solver
    kickoff 动态提交先例；分组信息依赖树运行时数据，无法静态提交）。

    分组（D26）：预估 = DEF 文件大小 × 树上实例化次数；≥ 阈值独占任务，
    < 阈值按 def_paths 序贪心聚合（累计预估不超阈值）；重名定义保留首份
    （跨块连接归并/层级树同语义——非首份序号不展开，其产物被首份遮蔽）。
    分区数来自全局密度任务写定的 DSDesign.partitions_（含 (xp, yp) 网格
    坐标）；每展开任务对全部分区各写一份分片临时对象（未触达分区写空产
    物——合并任务依赖恒可解），合并任务按 pid 汇聚本分区的全部分片。

    全局密度完成锚点 = global_density（全局密度任务末尾写定）：DSDesign
    首写（实例解析汇总）先于补分区重写，仅依赖 DSDesign 会在重写前
    被满足而读到无分区表的旧版本（竞态，2026-09-13 QA 实证）——依赖
    global_density 保证读到的是补齐 partitions_ 之后的 DSDesign。

    本任务同时动态提交校验链（每分区一校验任务 + 全局汇总校验任务，
    2026-09-13 校验分级裁定）——校验必须排在 merge 之后、freeze 之前：
    损坏类 fatal 阻断冻结、verify_report 正式对象挂 freeze 依赖。
    """
    import os
    design = db.read_object(design_key)
    tree = db.read_object(hier_key)
    block_names = db.read_object(block_names_key)
    settings = db.read_object(alpha_key)
    settings.normalize()
    threshold = settings.def_aggregate_threshold
    # names_keys 不被展开消费（连接 id 化后展开零名字查询，2026-09-13
    # 裁定）——仅透传给下方动态提交的全局校验任务（namemap 全查）

    # 树上实例化计数（block cell 名 → 出现次数；root 含其定义自身）
    inst_count = {}
    for i in range(tree.node_count):
        name = tree.node(i).block_cell_name
        inst_count[name] = inst_count.get(name, 0) + 1

    # 展开分组：重名定义跳过（保留首份）；大定义独占、小定义贪心聚合
    groups = []
    current = []
    acc = 0
    for i, path in enumerate(def_paths):
        if block_names.index(block_names[i]) != i:
            continue
        estimate = os.path.getsize(path) * inst_count.get(block_names[i], 0)
        if estimate >= threshold:
            groups.append([i])
            continue
        if current and acc + estimate > threshold:
            groups.append(current)
            current = []
            acc = 0
        current.append(i)
        acc += estimate
    if current:
        groups.append(current)

    partitions = [(design.partition_at(i).partition_id,
                   design.partition_at(i).xp, design.partition_at(i).yp)
                  for i in range(design.partition_count)]

    from log import INFO
    INFO(f"partition plan: {len(groups)} expand task(s) over "
         f"{len(def_paths)} def(s), {len(partitions)} partition(s), "
         f"threshold={threshold}")

    # 展开任务（每组一任务，只读本组 def 产物——每份 DEF 数据只读一次）
    for g, group in enumerate(groups):
        _partition_expand_task(db, design_key, hier_key, block_names_key,
                               group,
                               [block_keys[i] for i in group],
                               [net_keys[i] for i in group],
                               f"{slice_prefix}{g}_", len(partitions))
    # 每分区一合并任务（真实合并语义）：merge 全部相关分片 → 六类正式
    # 对象唯一写定 + 本区 id→partition 片段临时对象（debug 定位裁定）+
    # 本区 pg 网片段临时对象（2026-09-13 重组裁定：pg 全局集汇总素材）
    for pid, xp, yp in partitions:
        _partition_product_task(db, slice_prefix, len(groups), pid, xp, yp,
                                id_slice_prefix, pg_slice_prefix)
    # id→partition 反向映射汇总（2026-09-13 debug 定位裁定：INST/NET 各
    # 一任务，读全部分区片段 → 全空间分段正式对象 + 段表）
    for kind in DesignDb.ID_MAP_KINDS:
        _id_map_merge_task(db, id_slice_prefix, len(partitions), kind)
    # 全局 pg 网 id 集汇总（2026-09-13 重组裁定：读全部分区 pg 片段 →
    # 两 set 去重合并 → "pg_nets" 正式对象；依赖全部分区 merge 产出片段）
    _pg_nets_merge_task(db, pg_slice_prefix, len(partitions))
    # 校验链（每分区一校验任务并行 + 全局汇总校验；2026-09-13 校验
    # 分级裁定——损坏类 fatal 阻断冻结，观测类 warn 不阻断）
    verify_keys = []
    for pid, xp, yp in partitions:
        verify_key = f"{verify_prefix}{pid}"
        verify_keys.append(verify_key)
        _partition_verify_task(db, pid, xp, yp, verify_key)
    report_key = DesignDb.VERIFY_REPORT_OBJ
    _design_verify_task(db, verify_keys, design_key, stack_key,
                        global_density_key, net_union_key, hier_key,
                        block_keys, net_keys, names_keys, report_key)
    # freeze：正式对象集（静态 + 全部分区对象 + id→partition 段表 ×2 +
    # pg 网 id 集 + 校验报告）+ 中间对象清理（分片 + pg 片段 + 校验结果
    # 临时对象）——verify_report 在 final_keys 中，校验未完成（或 fatal
    # 未产出报告）不冻结。段对象与段表同任务写定（段表在即全部段对象已
    # 在），依赖段表即足。
    partition_keys = [DesignDb.partition_obj_name(xp, yp, kind)
                      for _, xp, yp in partitions
                      for kind in DesignDb.PARTITION_KINDS]
    index_keys = [DesignDb.id_map_index_obj_name(kind)
                  for kind in DesignDb.ID_MAP_KINDS]
    slice_keys = [f"{slice_prefix}{g}_{pid}"
                  for g in range(len(groups))
                  for pid, _, _ in partitions]
    pg_slice_keys = [f"{pg_slice_prefix}{pid}" for pid, _, _ in partitions]
    _freeze_design_task(db, formal_keys + partition_keys + index_keys +
                        [DesignDb.PG_NETS_OBJ, report_key],
                        temp_keys + slice_keys + pg_slice_keys + verify_keys)


@as_task(inputs=lambda db, design_key, hier_key, block_names_key,
         group, block_keys, net_keys, slice_prefix, n_parts: (
    [db.get_full_name(k) for k in (design_key, hier_key, block_names_key)]
    + [db.get_full_name(k) for k in block_keys]
    + [db.get_full_name(k) for k in net_keys]))
def _partition_expand_task(db, design_key, hier_key, block_names_key,
                           group, block_keys, net_keys, slice_prefix,
                           n_parts):
    """per-组展开任务：读本组各 def 的单份解析产物（实例表/网内容）→
    ds_flatten_block 全部出现位置展开（复合变换取树节点、三类 id 换算、
    放置点归属、几何副本、连接补全）→ 按分区累积分片，对全部分区各写一
    份（未触达分区写空产物）。连接项 id + flags 六位已在网内容解析边界
    换算填写（2026-09-13 裁定），本任务零名字查询、零 pin 几何依赖（电源
    引脚预展开 D18 删除——归 ④ 提取自取）。"""
    design = db.read_object(design_key)
    tree = db.read_object(hier_key)
    block_names = db.read_object(block_names_key)
    products = {}
    for i, block_key, net_key in zip(group, block_keys, net_keys):
        if block_names.index(block_names[i]) != i:
            continue  # 重名保留首份（plan 分组已排除，防御再判）
        block = db.read_object(block_key)
        nets = db.read_object(net_key)
        for pid, product in ds_flatten_block(tree, block, nets, design):
            if pid in products:
                products[pid].merge_from(product)
            else:
                products[pid] = product
    for pid in range(n_parts):
        db.write_object(f"{slice_prefix}{pid}",
                        products.get(pid, EXDSPartitionProduct()),
                        save_to_db=False)


@as_task(inputs=lambda db, slice_prefix, n_groups, pid, xp, yp,
         id_slice_prefix, pg_slice_prefix: [
    db.get_full_name(f"{slice_prefix}{g}_{pid}") for g in range(n_groups)
])
def _partition_product_task(db, slice_prefix, n_groups, pid, xp, yp,
                            id_slice_prefix, pg_slice_prefix):
    """每分区一合并任务（分区侧真实合并语义，裁定 ⑤）：merge 来自不同
    展开任务的同分区分片 → 六类正式对象
    PART_{xp}_{yp}.{GEOMETRY,GEOMETRY_PG,INSTANCES,INST_CONNECTIONS,
    NETS,NETS_PG} 唯一写定（2026-09-14 拆分裁定：GEOMETRY/NETS 各按
    pg/信号拆两对象——信号网大文件与 pg 小文件物理分离，④ 提取首期
    专注电源网络时只加载 GEOMETRY_PG + NETS_PG 两个小对象）。随后提取
    本区 id→partition 片段（2026-09-13 debug 定位裁定：INST = primary
    副本 id 集、NET = 两几何对象键集并集——net 副本 id 集；临时对象由
    id→partition 汇总任务 merge 后清理）+ 本区 pg 网片段（拆分裁定：
    NETS_PG 侧对象表键按 use 分流，临时对象由 pg 全局集汇总任务合并后
    清理）。"""
    from log import INFO
    product = EXDSPartitionProduct()
    for g in range(n_groups):
        product.merge_from(db.read_object(f"{slice_prefix}{g}_{pid}"))
    db.write_object(DesignDb.partition_obj_name(xp, yp, "GEOMETRY"),
                    product.geometry(), save_to_db=True)
    db.write_object(DesignDb.partition_obj_name(xp, yp, "GEOMETRY_PG"),
                    product.geometry_pg(), save_to_db=True)
    db.write_object(DesignDb.partition_obj_name(xp, yp, "INSTANCES"),
                    product.instances(), save_to_db=True)
    db.write_object(DesignDb.partition_obj_name(xp, yp, "INST_CONNECTIONS"),
                    product.inst_connections(), save_to_db=True)
    db.write_object(DesignDb.partition_obj_name(xp, yp, "NETS"),
                    product.nets(), save_to_db=True)
    db.write_object(DesignDb.partition_obj_name(xp, yp, "NETS_PG"),
                    product.nets_pg(), save_to_db=True)
    # 本区映射片段（临时对象；merge 幂等键覆盖后提取——恰一 primary /
    # net 副本口径见 ds_collect_inst_id_slice / ds_collect_net_id_slice，
    # NET = 两几何键集并集）
    db.write_object(
        DesignDb.id_slice_obj_name(id_slice_prefix, pid, "INST"),
        ds_collect_inst_id_slice(product.instances(), pid),
        save_to_db=False)
    db.write_object(
        DesignDb.id_slice_obj_name(id_slice_prefix, pid, "NET"),
        ds_collect_net_id_slice(product.geometry(), product.geometry_pg(),
                                pid),
        save_to_db=False)
    # 本区 pg 网片段（临时对象：NETS_PG 侧表键按 use 分流）
    db.write_object(f"{pg_slice_prefix}{pid}",
                    ds_collect_pg_net_slice(product.nets_pg()),
                    save_to_db=False)
    INFO(f"partition product ({xp},{yp}): {product.instance_count} "
         f"instances, geometry {product.geometry().net_count} bucket(s) / "
         f"pg {product.geometry_pg().net_count}, nets {product.nets().size} "
         f"/ pg {product.nets_pg().size}")


# ── 全局 pg 网 id 集汇总（2026-09-13 重组裁定：多分区 pg 片段 → 两 set
# 去重合并 → "pg_nets" 正式对象；由分区编排任务动态提交在分区 merge 之
# 后、freeze 之前——消费 = debug API is_pg 快速判定 + Python 查询口）────

@as_task(inputs=lambda db, pg_slice_prefix, n_parts: [
    db.get_full_name(f"{pg_slice_prefix}{pid}") for pid in range(n_parts)
])
def _pg_nets_merge_task(db, pg_slice_prefix, n_parts):
    """pg 网 id 集汇总任务（单任务）：读全部分区 pg 片段 →
    ds_build_pg_net_set 两 set 去重合并（同 pg 网跨分区副本只此一条）→
    PG_NETS_OBJ 正式对象唯一写定 → 片段清理（freeze temp_keys 兜底）。"""
    slices = [db.read_object(f"{pg_slice_prefix}{pid}")
              for pid in range(n_parts)]
    pg_set = ds_build_pg_net_set(slices)
    db.write_object(DesignDb.PG_NETS_OBJ, pg_set, save_to_db=True)
    # 片段显式清理（review 2026-09-13：与 _id_map_merge_task 同族任务
    # 风格一致——早释放；freeze temp_keys 兜底双保险）
    for pid in range(n_parts):
        db.remove_object(f"{pg_slice_prefix}{pid}")
    from log import INFO
    INFO(f"pg net set: power={pg_set.power_count} "
         f"ground={pg_set.ground_count} merged from {n_parts} slice(s)")


# ── id → partition 反向映射汇总（2026-09-13 debug 定位裁定：多分区片段
# → 全空间分段正式对象；由分区编排任务动态提交在分区 merge 之后、freeze
# 之前——段对象与段表同任务写定，freeze 依赖段表即依赖全部段对象）───────

@as_task(inputs=lambda db, id_slice_prefix, n_parts, kind: [
    db.get_full_name(DesignDb.id_slice_obj_name(id_slice_prefix, pid, kind))
    for pid in range(n_parts)
])
def _id_map_merge_task(db, id_slice_prefix, n_parts, kind):
    """id→partition 映射汇总任务（INST/NET 各一）：读全部分区片段 →
    ds_merge_id_partition_slices 全空间分段（空洞段跳过）→ 段对象逐段
    写正式对象 + 段表最后写定（段表在即全部段对象已在——freeze 依赖段
    表）→ 片段清理。"""
    slices = [db.read_object(DesignDb.id_slice_obj_name(id_slice_prefix,
                                                        pid, kind))
              for pid in range(n_parts)]
    merge = (ds_merge_inst_id_partition_slices if kind == "INST"
             else ds_merge_net_id_partition_slices)
    index, segments = merge(slices)
    for seg_start, segment in segments:
        db.write_object(
            DesignDb.id_map_segment_obj_name(
                kind, seg_start >> DesignDb.ID_MAP_SEGMENT_BITS),
            segment, save_to_db=True)
    db.write_object(DesignDb.id_map_index_obj_name(kind), index,
                    save_to_db=True)
    for pid in range(n_parts):
        db.remove_object(
            DesignDb.id_slice_obj_name(id_slice_prefix, pid, kind))
    from log import INFO
    INFO(f"id partition map '{kind}': {index.segment_count} segment(s) "
         f"merged from {n_parts} slice(s)")


# ── 汇总校验 + 冻结前置（2026-09-13 校验分级裁定：损坏类 fatal /
#    观测类 warn；由分区编排任务动态提交排在 merge 之后、freeze 之前）──

@as_task(inputs=lambda db, pid, xp, yp, result_key: [
    db.get_full_name(DesignDb.partition_obj_name(xp, yp, kind))
    for kind in DesignDb.PARTITION_KINDS
])
def _partition_verify_task(db, pid, xp, yp, result_key):
    """每分区一校验任务（并行读单分区产物——红线：不跨区读）：分区级计
    数 + 全局校验素材 id 集提取；损坏类判定集中在全局汇总任务（fatal 单
    点退出）。结果为临时对象，全局校验合并后由 freeze 清理。六类对象按
    2026-09-14 拆分裁定读取（geometry/nets 各信号与 pg 两侧）。"""
    geometry = db.read_object(DesignDb.partition_obj_name(xp, yp, "GEOMETRY"))
    geometry_pg = db.read_object(
        DesignDb.partition_obj_name(xp, yp, "GEOMETRY_PG"))
    instances = db.read_object(
        DesignDb.partition_obj_name(xp, yp, "INSTANCES"))
    inst_connections = db.read_object(
        DesignDb.partition_obj_name(xp, yp, "INST_CONNECTIONS"))
    nets = db.read_object(DesignDb.partition_obj_name(xp, yp, "NETS"))
    nets_pg = db.read_object(DesignDb.partition_obj_name(xp, yp, "NETS_PG"))
    result = ds_verify_partition(pid, xp, yp, geometry, geometry_pg,
                                 instances, inst_connections, nets, nets_pg)
    db.write_object(result_key, result, save_to_db=False)


@as_task(inputs=lambda db, verify_keys, design_key, stack_key, density_key,
         union_key, hier_key, block_keys, net_keys, names_keys,
         report_key: (
    [db.get_full_name(k) for k in verify_keys]
    + [db.get_full_name(k)
       for k in (design_key, stack_key, density_key, union_key, hier_key)]
    + [db.get_full_name(k) for k in block_keys]
    + [db.get_full_name(k) for k in net_keys]
    + [db.get_full_name(k) for k in names_keys]))
def _design_verify_task(db, verify_keys, design_key, stack_key,
                        density_key, union_key, hier_key, block_keys,
                        net_keys, names_keys, report_key):
    """全局汇总校验任务（单任务）：树 + 全部分区校验结果 + net_union +
    DSDesign（分区表/hashers）+ stack + global_density（覆盖域基准）+
    per-DEF 产物（via id 域与 UNPLACED 计数）+ 伴生名（namemap 全查）。
    分级处置：损坏类（并查集/分区覆盖/namemap）→ fatal 退出（码 80 +
    master 联动）——report 不落盘、freeze 依赖缺失，损坏库不冻结；观测类
    （id 连续性 DSGN::0022 / 密度守恒 primary 口径 DSGN::0023）→ user
    warn 不阻断；统计汇总 DSGN::0024 INFO；报告写 verify_report 正式对象
    （freeze final_keys 依赖——校验未完成不冻结）。"""
    checks = [db.read_object(k) for k in verify_keys]
    design = db.read_object(design_key)
    stack = db.read_object(stack_key)
    density = db.read_object(density_key)
    union = db.read_object(union_key)
    tree = db.read_object(hier_key)
    blocks = [db.read_object(k) for k in block_keys]
    nets = [db.read_object(k) for k in net_keys]
    names = [db.read_object(k) for k in names_keys]
    report = ds_verify_design(tree, design, stack, density, union, blocks,
                              nets, names, checks)
    # 损坏类逐项 fatal（首个非空项触发退出，损坏库到此终止）
    ds_verify_report_or_fatal(report)
    # 观测类（不阻断冻结）：id 连续性——空洞含 UNPLACED 实例/空网/root
    # 自身等合法形态，重复 = instance 多 primary 超量
    from fly import message
    domains = []
    for label, dom in (("instance", report.instance_ids),
                       ("net", report.net_ids),
                       ("via", report.via_ids)):
        if dom.holes or dom.duplicates:
            domains.append(
                f"{label}: expected={dom.expected} actual={dom.actual} "
                f"holes={dom.holes} duplicates={dom.duplicates}")
    if domains:
        message("DSGN::0022", 0,
                "global id continuity (legal hole sources differ per "
                "domain — instance: UNPLACED; net: empty net, plus one "
                "local-0 hole slot per block which is by-design and "
                "excluded from expected; via: none): "
                + "; ".join(domains))
    if report.density_variance:
        message("DSGN::0023", 0,
                f"density conservation: {report.density_variance}")
    # 全局统计汇总（INFO）
    message("DSGN::0024", 0,
            f"design verify summary: partitions={report.partition_count}, "
            f"instances {report.total_primary} primary / "
            f"{report.total_instances} copies, nets "
            f"{report.total_nets}/{report.expected_nets}, connections "
            f"{report.total_connections}, geometry entries "
            f"{report.total_geometry_entries}, crossing nets "
            f"{report.total_crossing_nets}, density totals "
            f"inst={report.density_instance_total} "
            f"metal={report.density_metal_total} "
            f"via={report.density_via_total}")
    from log import INFO
    INFO(f"design verify: partitions={report.partition_count}, "
         f"primary={report.total_primary}, copies={report.total_instances}, "
         f"nets={report.total_nets}, connections={report.total_connections}")
    # 校验报告正式落盘（freeze final_keys 依赖本对象——校验未完成不冻结）
    db.write_object(report_key, report, save_to_db=True)


# ── freeze：依赖正式对象写完 + 中间对象清理（由分区编排任务动态提交，
#    使 final_keys 能携带运行时确定的全部分区对象名）────────────────────

@as_task(inputs=lambda db, final_keys, temp_keys: [
    db.get_full_name(k) for k in final_keys
])
def _freeze_design_task(db, final_keys, temp_keys):
    for key in temp_keys:
        try:
            db.remove_object(key)
        except Exception:
            pass
    db.freeze()


def run_design_flow(db, lef_paths, def_paths, lib_db):
    """提交全部阶段任务（非阻塞）。alpha 提供建库未稳定配置（⑦）：八键
    经 DSAlphaSettings 声明式定义（2026-09-13 裁定，src/emir/design/py/
    alpha_settings.py；校验与 DSGN::0013 汇总在 build_design_db 接线处），
    settings 对象以对象名 "alpha_settings" 随建库写入 db——本函数从 db
    读回（normalize 兜底：旧对象缺键补默认、未知属性丢弃）取提交侧三键：
    density_bin_size（µm，缺省 10）、net_batch_size（网内容批界网数，缺
    省 1000，裁定 ③）、lcp_name_arena（R8d 裁定 55，缺省 False 形态一零
    变化）；分区决策四键（target_partitions/partition_count/
    partition_target_density/density_channel_weights）与小 DEF 聚合阈值
    def_aggregate_threshold 由消费任务内读回（inputs 声明依赖）。"""
    settings = db.read_object(DesignDb.ALPHA_SETTINGS_OBJ)
    settings.normalize()
    bin_um = settings.density_bin_size
    net_batch = settings.net_batch_size
    lcp_name_arena = settings.lcp_name_arena
    uid = uuid4().hex[:8]
    stack_key = DesignDb.STACK_OBJ
    design_key = DesignDb.DESIGN_OBJ
    geoms_key = DesignDb.PIN_GEOMETRY_OBJ
    tables_key = DesignDb.PIN_TABLES_OBJ

    tech_vias_key = _tmp_key(uid, "tech_vias")
    # macro pin 几何中间产物 key（cell lef 汇总写、DEF 头汇总读后清理；
    # 正式 DSPinGeometry 由 DEF 头汇总唯一写定）
    macro_geoms_key = _tmp_key(uid, "macro_geoms")
    # lib 库容器快照：master 侧读 lib db 写入 design db 临时对象——lib
    # merge task 全程在 design db 内，规避 worker 端跨 db 读取
    lib_snapshot_key = _tmp_key(uid, "lib_library")
    db.write_object(lib_snapshot_key,
                    lib_db.read_object(lib_db.LIBRARY_OBJ),
                    save_to_db=False)

    temp_keys = [tech_vias_key, macro_geoms_key, lib_snapshot_key]

    # tech lef（lef_paths[0]，约定见模块 docstring）
    _tech_lef_task(db, lef_paths[0], stack_key, tech_vias_key)

    # 每 cell lef 一 task（产物四元组：design/geoms/vias + 失败标记）
    part_tuples = []
    for i, path in enumerate(lef_paths[1:]):
        keys = (_tmp_key(uid, f"cell_lef_{i}_design"),
                _tmp_key(uid, f"cell_lef_{i}_geoms"),
                _tmp_key(uid, f"cell_lef_{i}_vias"),
                _tmp_key(uid, f"cell_lef_{i}_failed"))
        temp_keys.extend(keys)
        part_tuples.append(keys)
        _cell_lef_task(db, path, stack_key, *keys)

    # cell lef 汇总 → DSDesign 中间态 + macro pin 几何临时对象（正式
    # DSPinGeometry 由 DEF 头汇总合并 port 几何后唯一写定）
    merged_key = _tmp_key(uid, "merged")
    temp_keys.append(merged_key)
    _cell_lef_merge_task(db, part_tuples, stack_key, tech_vias_key,
                         merged_key, macro_geoms_key)

    # lib merge（快照对象已在 design db 内）
    s3_key = _tmp_key(uid, "lib_merged_design")
    temp_keys.append(s3_key)
    _merge_lib_task(db, merged_key, lib_snapshot_key, tables_key, s3_key)

    # DEF 头扫描：每 DEF 一 task
    header_part_keys = []
    for i, path in enumerate(def_paths):
        keys = (_tmp_key(uid, f"header_{i}_cells"),
                _tmp_key(uid, f"header_{i}_geoms"),
                _tmp_key(uid, f"header_{i}_vias"),
                _tmp_key(uid, f"header_{i}_obs"))
        temp_keys.extend(keys)
        header_part_keys.append(keys)
        _def_header_task(db, path, stack_key, *keys)

    # DEF 头汇总 → cell 全集快照（临时）+ 正式 DSPinGeometry；正式
    # DSDesign 由实例解析汇总写定
    snapshot_key = _tmp_key(uid, "snapshot")
    temp_keys.append(snapshot_key)
    _def_header_merge_task(db, s3_key, header_part_keys, snapshot_key,
                           geoms_key, macro_geoms_key)

    # COMPONENTS 解析：每 DEF 一 task（实例 ∥ 网名，同一遍读取）→ 临时
    # 产物 + 正式名字伴生对象 DSBlockNames_<序号>（对象名按 def_paths 序，
    # block 名冗余在伴生对象 block_name 字段可校验）；正式 DSBlock_<序号>
    # 由汇总唯一写定
    temp_block_keys = []
    names_keys = []
    fake_keys = []
    for i, path in enumerate(def_paths):
        block_key = _tmp_key(uid, f"components_{i}_block")
        names_key = DesignDb.names_obj_name(i)
        obs_key = header_part_keys[i][3]  # S4 obstruction 临时对象
        fake_key = _tmp_key(uid, f"fake_cells_{i}")  # fake cell 中转
        temp_keys.append(block_key)
        temp_keys.append(fake_key)
        temp_block_keys.append(block_key)
        names_keys.append(names_key)
        fake_keys.append(fake_key)
        _components_def_task(db, path, stack_key, snapshot_key, block_key,
                             names_key, obs_key, fake_key,
                             bin_um * 1000,  # µm → DBU（全局基准 ㉝）
                             lcp_name_arena)

    # 网内容解析：每 DEF 一 task（分批解析；⑨ local net id 经名字伴生对
    # 象注入对齐、⑪ via cell 权威表快照就绪）→ 正式 DSNet_<序号> 对象 +
    # 统计临时对象；正式对象由 per-DEF 任务唯一写定（无全局重排语义）。
    # 网内容解析先于汇总：其 via instance 计数是层级树 via 区间的输入，
    # 且临时产物输入使其不依赖正式 DSDesign（汇总单点写定时序保持）
    formal_net_keys = [DesignDb.net_obj_name(i) for i in range(len(def_paths))]
    nets_stats_keys = []
    for i, path in enumerate(def_paths):
        stats_key = _tmp_key(uid, f"nets_{i}_stats")
        temp_keys.append(stats_key)
        nets_stats_keys.append(stats_key)
        _nets_def_task(db, path, stack_key, snapshot_key, temp_block_keys[i],
                       names_keys[i], formal_net_keys[i], stats_key,
                       net_batch, bin_um * 1000)

    # 网内容汇总：统计合并 → DSGN::0009
    _nets_summary_task(db, nets_stats_keys)

    # 层级树：构建 + 起始编号分配（消费实例解析临时产物 + 网内容正式产
    # 物的 via 计数——via 计数物理依赖已消解为串行，见 phase2 方案备注；
    # 串行便宜——元数据级操作）
    hier_key = _tmp_key(uid, "hier")
    temp_keys.append(hier_key)
    _hier_task(db, snapshot_key, temp_block_keys, names_keys,
               formal_net_keys, hier_key)

    # 实例解析汇总：层级树嵌容器 + fake cell 并入 + 统计合并 → 正式对象
    # 写定
    formal_block_keys = [DesignDb.block_obj_name(i)
                         for i in range(len(def_paths))]
    _components_merge_task(db, snapshot_key, temp_block_keys, fake_keys,
                           formal_block_keys, design_key, hier_key)

    # S8：全局密度合并 + 分区决策（依赖 S6 树 + 全部 per-DEF 正式产物 +
    # stack + alpha_settings 对象；DSDesign 补 partitions_ 重写 + global_
    # density 独立对象写定。与 S7 同级并行、互不依赖——见下方 S7 任务组。
    # alpha 四键任务内读回，2026-09-13 裁定）
    global_density_key = DesignDb.GLOBAL_DENSITY_OBJ
    _partition_task(db, stack_key, hier_key, formal_block_keys,
                    formal_net_keys, design_key, global_density_key,
                    DesignDb.ALPHA_SETTINGS_OBJ)

    # S7：跨块连接归并（与 S8 同级并行——依赖同为 S6 树 + S5b 正式产物；
    # 两级任务：per-DEF slice 并行收集 + 单任务汇总，2026-09-13 裁定）。
    # slice 为临时对象（汇总任务合并后自行 remove）；net_union 正式对象
    # 挂 freeze final_keys
    net_union_key = DesignDb.NET_UNION_OBJ
    union_slice_keys = [_tmp_key(uid, f"net_union_slice_{i}")
                        for i in range(len(def_paths))]
    block_names_key = _tmp_key(uid, "block_names")
    temp_keys.append(block_names_key)
    _net_union_names_task(db, names_keys, block_names_key)
    for i in range(len(def_paths)):
        _net_union_slice_task(db, hier_key, block_names_key,
                              formal_net_keys, i, union_slice_keys[i])
    _net_union_summary_task(db, hier_key, union_slice_keys, net_union_key)

    # S9：flatten 展平 + 分区保存（依赖 S8 分区表 + S6 树 + 全部 per-DEF
    # 产物；连接项 id + flags 位在 S5b 解析边界完成换算，展开零名字查询
    # 零 pin 几何依赖。两级任务 + 小 DEF 聚合，plan 任务在 worker 上动态
    # 提交展开/合并任务并收尾 freeze——final_keys 需携带运行时确定的全
    # 部分区对象名，故 freeze 由 plan 动态提交而非本函数静态提交）。
    # S10 校验链同由 plan 动态提交（merge 之后、freeze 之前；freeze 依赖
    # verify_report 正式对象——校验未完成不冻结）
    slice_prefix = _tmp_key(uid, "s9_slice_")
    verify_prefix = _tmp_key(uid, "s10_verify_")
    id_slice_prefix = _tmp_key(uid, "id_slice_")
    pg_slice_prefix = _tmp_key(uid, "pg_slice_")
    _partition_plan_task(
        db, design_key, global_density_key, hier_key, block_names_key,
        DesignDb.ALPHA_SETTINGS_OBJ, formal_block_keys,
        formal_net_keys, names_keys, def_paths, slice_prefix, verify_prefix,
        stack_key, net_union_key, id_slice_prefix, pg_slice_prefix,
        [design_key, stack_key, tables_key, geoms_key] + formal_block_keys +
        names_keys + formal_net_keys + [global_density_key, net_union_key],
        temp_keys)
