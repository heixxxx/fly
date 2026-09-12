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
  → S8 task（全局密度合并 + 分区决策，2026-09-12/13 裁定：层级树自底
    向上 + 格值面积比例分摊 D10 A → global_density 独立对象；行列前缀
    和切分 → DSDesign 补 partitions_ 重写。依赖 S6 树 + 全部 per-DEF
    正式产物 + stack——S7 未实施不预留挂点。alpha 四键：target_partitions/
    partition_count/partition_target_density/density_channel_weights，
    非法值 WARN 回退不 raise）
  → freeze task（依赖 DSDesign/DSStack/DSPinTables/DSPinGeometry/
    DSBlock_*/DSBlockNames_*/DSNet_*/global_density 对象写完 + 中间对象
    清理）

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
    EXDSPinGeometry,
    EXDSHierTree,
    EXDSNetBuildData,
    ds_build_hier_tree,
    ds_decide_partitions,
    ds_merge_block_build,
    ds_merge_cell_lef,
    ds_merge_def_header,
    ds_merge_global_density,
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


# ── 每 cell lef 一 task（局部 id 中间产物）──────────────────────────

@as_task(inputs=lambda db, path, stack_key, design_key, geoms_key, vias_key: [
    db.get_full_name(stack_key)
])
def _cell_lef_task(db, path, stack_key, design_key, geoms_key, vias_key):
    stack = db.read_object(stack_key)
    part_design, part_geoms, part_vias = ds_parse_cell_one(path, stack)
    db.write_object(design_key, part_design, save_to_db=False)
    db.write_object(geoms_key, part_geoms, save_to_db=False)
    db.write_object(vias_key, part_vias, save_to_db=False)


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
    for design_key_i, geoms_key_i, vias_key_i in part_tuples:
        part_design = db.read_object(design_key_i)
        part_geoms = db.read_object(geoms_key_i)
        part_vias = db.read_object(vias_key_i)
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

@as_task(inputs=lambda db, path, stack_key, cells_key, geoms_key, vias_key: [
    db.get_full_name(stack_key)
])
def _def_header_task(db, path, stack_key, cells_key, geoms_key, vias_key):
    stack = db.read_object(stack_key)
    block_cells, port_names, port_geoms, vias = ds_parse_def_one(path, stack)
    db.write_object(cells_key, (block_cells, port_names), save_to_db=False)
    db.write_object(geoms_key, port_geoms, save_to_db=False)
    db.write_object(vias_key, vias, save_to_db=False)


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
    for (cells_key, geoms_key_i, vias_key) in part_keys:
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
         names_key, bin_dbu, lcp_name_arena: [
    db.get_full_name(stack_key),
    db.get_full_name(snapshot_key),
])
def _components_def_task(db, path, stack_key, snapshot_key, block_key,
                         names_key, bin_dbu, lcp_name_arena):
    stack = db.read_object(stack_key)
    design = db.read_object(snapshot_key)
    block_data = EXDSBlockBuildData()
    stats = ds_parse_def_components_one(path, stack, design, block_data,
                                        bin_dbu)
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

@as_task(inputs=lambda db, snapshot_key, temp_block_keys, formal_block_keys,
         design_key, hier_key: [
    db.get_full_name(snapshot_key),
    db.get_full_name(hier_key),
] + [db.get_full_name(k) for k in temp_block_keys])
def _components_merge_task(db, snapshot_key, temp_block_keys,
                           formal_block_keys, design_key, hier_key):
    design = db.read_object(snapshot_key)
    # 层级树嵌容器（⑬；层级树产物——在正式 DSDesign 写定前完成树挂载）
    tree = db.read_object(hier_key)
    assert isinstance(tree, EXDSHierTree)
    design.set_hier_tree(tree)
    total_leaf = 0
    total_net = 0
    total_fake = 0
    for temp_key, formal_key in zip(temp_block_keys, formal_block_keys):
        block_data = db.read_object(temp_key)
        total_fake += ds_merge_block_build(design, block_data)
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
        "batch": stats.batch_count,
    }, save_to_db=False)
    if stats.skipped_via_count or stats.skipped_net_count:
        from log import INFO
        INFO(f"nets '{path}': {stats.skipped_via_count} undefined via "
             f"references skipped, {stats.skipped_net_count} unknown nets")


# ── 网内容汇总：统计合并 → DSGN::0009（网数/几何/via instance 数）─────

@as_task(inputs=lambda db, stats_keys: [
    db.get_full_name(k) for k in stats_keys
])
def _nets_summary_task(db, stats_keys):
    total = {"net": 0, "connection": 0, "wire": 0, "rect": 0,
             "via_instance": 0, "skipped_via": 0, "skipped_net": 0,
             "batch": 0}
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
            f"{total['skipped_via']} undefined vias skipped")


# ── S8：全局密度合并 + 分区决策（core/extend 双区域，2026-09-12/13 裁定）─

@as_task(inputs=lambda db, stack_key, hier_key, block_keys, net_keys,
         design_key, density_key, w_inst, w_metal, w_via, target_partitions,
         partition_count, target_density: [
    db.get_full_name(stack_key),
    db.get_full_name(hier_key),
    db.get_full_name(design_key),
] + [db.get_full_name(k) for k in block_keys] +
    [db.get_full_name(k) for k in net_keys])
def _partition_task(db, stack_key, hier_key, block_keys, net_keys, design_key,
                    density_key, w_inst, w_metal, w_via, target_partitions,
                    partition_count, target_density):
    from log import INFO
    # 依赖 S6 树（临时产物）+ 全部 per-DEF 正式产物（S5a 实例密度 +
    # S5b 网侧逐层密度）+ stack（w_eff 判定）。S7 未实施、不预留挂点
    # （红线：S8 只依赖 S6+S5b 产物）。
    stack = db.read_object(stack_key)
    tree = db.read_object(hier_key)
    blocks = [db.read_object(k) for k in block_keys]
    nets = [db.read_object(k) for k in net_keys]
    design = db.read_object(design_key)

    global_density = ds_merge_global_density(tree, blocks, nets)
    partitions = ds_decide_partitions(global_density, stack, w_inst, w_metal,
                                      w_via, target_partitions,
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


# ── freeze：依赖正式对象写完 + 中间对象清理 ──────────────────────────

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


def _parse_channel_weights(raw):
    """alpha density_channel_weights 解析（dict；非法值 WARN 回退该键默
    认，不 raise——dev-rules §7）。默认 6/2/2（2026-09-12 裁定 3）。"""
    from log import WARN
    weights = {"instance": 6.0, "metal": 2.0, "via": 2.0}
    if raw is None:
        return weights
    if not isinstance(raw, dict):
        WARN(f"alpha density_channel_weights must be a dict, got "
             f"{type(raw).__name__} — falling back to defaults {weights}")
        return weights
    for key in weights:
        if key not in raw:
            continue
        value = raw[key]
        # NaN/inf 一并拒绝（review 2026-09-13）：NaN 比较恒 False 会放行，
        # 使合成负载全 NaN、切线全失效
        if not isinstance(value, (int, float)) or isinstance(value, bool) \
                or value != value or value in (float("inf"), float("-inf")) \
                or value < 0:
            WARN(f"alpha density_channel_weights['{key}'] must be a "
                 f"non-negative finite number, got {value!r} — falling "
                 f"back to default {weights[key]}")
            continue
        weights[key] = float(value)
    return weights


def run_design_flow(db, lef_paths, def_paths, lib_db, alpha=None):
    """提交全部阶段任务（非阻塞）。alpha 提供建库未稳定配置（⑦）：本
    阶段消费 density_bin_size（µm，缺省 10）、net_batch_size（网内容批
    界网数，缺省 1000，裁定 ③）、lcp_name_arena（R8d 裁定 55：名字伴生
    对象 id→name 侧 LCP 后缀压缩封口，缺省 False 形态一零变化）+ S8 四键
    （2026-09-12/13 裁定）：target_partitions（'{x}x{y}' 直切，缺省未设
    置）、partition_count（总分区数，缺省未设置）、partition_target_density
    （目标合成负载，缺省 150000）、density_channel_weights（通道比重 dict，
    缺省 6/2/2）。非法值一律 WARN 提醒后回退，不 raise。"""
    from uuid import uuid4 as _uuid4

    alpha = alpha or {}
    bin_um = int(alpha.get("density_bin_size", 10))
    net_batch = int(alpha.get("net_batch_size", 1000))
    lcp_name_arena = bool(alpha.get("lcp_name_arena", False))
    # S8 分区决策键（四键；类型检查在 flow 边界，语义解析在 C++——
    # '{x}x{y}' 解析失败 / 非正 target_density 由 DSGN::0013 提醒回退）
    target_partitions_raw = alpha.get("target_partitions", "")
    # None 与非 str 一并 WARN 回退（review 2026-09-13：原 `is not None` 前置
    # 使显式 None 穿透类型检查，C++ 边界 TypeError raise，违背不 raise 裁定）
    if not isinstance(target_partitions_raw, str):
        from log import WARN
        WARN(f"alpha target_partitions must be a str like '4x3', got "
             f"{type(target_partitions_raw).__name__} — ignoring")
        target_partitions_raw = ""
    partition_count_raw = alpha.get("partition_count", 0)
    if not isinstance(partition_count_raw, int) or \
            isinstance(partition_count_raw, bool):
        from log import WARN
        WARN(f"alpha partition_count must be an int, got "
             f"{type(partition_count_raw).__name__} — ignoring")
        partition_count_raw = 0
    target_density_raw = alpha.get("partition_target_density", 150000)
    if not isinstance(target_density_raw, int) or \
            isinstance(target_density_raw, bool):
        from log import WARN
        WARN(f"alpha partition_target_density must be an int, got "
             f"{type(target_density_raw).__name__} — ignoring")
        target_density_raw = 150000
    channel_weights = _parse_channel_weights(alpha.get(
        "density_channel_weights"))
    uid = _uuid4().hex[:8]
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

    # 每 cell lef 一 task
    part_tuples = []
    for i, path in enumerate(lef_paths[1:]):
        keys = (_tmp_key(uid, f"cell_lef_{i}_design"),
                _tmp_key(uid, f"cell_lef_{i}_geoms"),
                _tmp_key(uid, f"cell_lef_{i}_vias"))
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
                _tmp_key(uid, f"header_{i}_vias"))
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
    for i, path in enumerate(def_paths):
        block_key = _tmp_key(uid, f"components_{i}_block")
        names_key = DesignDb.names_obj_name(i)
        temp_keys.append(block_key)
        temp_block_keys.append(block_key)
        names_keys.append(names_key)
        _components_def_task(db, path, stack_key, snapshot_key, block_key,
                             names_key, bin_um * 1000,  # µm → DBU（全局基准 ㉝）
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
    _components_merge_task(db, snapshot_key, temp_block_keys,
                           formal_block_keys, design_key, hier_key)

    # S8：全局密度合并 + 分区决策（依赖 S6 树 + 全部 per-DEF 正式产物 +
    # stack；DSDesign 补 partitions_ 重写 + global_density 独立对象写定。
    # S7 未实施——依赖只挂 S6+S5b 产物，不预留 S7 挂点）
    global_density_key = DesignDb.GLOBAL_DENSITY_OBJ
    _partition_task(db, stack_key, hier_key, formal_block_keys,
                    formal_net_keys, design_key, global_density_key,
                    channel_weights["instance"], channel_weights["metal"],
                    channel_weights["via"], target_partitions_raw,
                    partition_count_raw, target_density_raw)

    # freeze：依赖正式对象集（含 per-DEF 实例/名字/网产物 + S8 全局密度
    # 图——后写，使 freeze 排在 S8 重写 DSDesign 之后）；中间对象清理
    _freeze_design_task(
        db,
        [design_key, stack_key, tables_key, geoms_key] + formal_block_keys +
        names_keys + formal_net_keys + [global_density_key],
        temp_keys)
