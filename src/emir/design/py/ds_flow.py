"""build_design_db flow 任务链（直接任务链不用 MapReduce——用户裁定
①；任务函数名 = <动作>_<对象>[_<粒度>]_task，与根任务目录逐行对应）。

三段式（dev-rules §3「建库 API 流程标准」）：入口 ds_db.build_design_db
①轻量参数预处理（schema 校验 + 文件存在性 + 建库 + alpha_settings 写入
——不读文件内容）②提交唯一根任务 ③顶层提交 freeze。master 提交 O(1)
个任务，与输入规模无关（§20 编排判据）。

根任务 _design_flow_task 体内目录：
  tech lef（层堆叠 + via 权威表）
  → cell lef 族（每文件一任务）+ cell lef 汇总（统一 cell id/pin
     hasher 重挂/via 与几何合入）
  → lib merge（lib 快照搬运 + 按 cell 名并入库信息 → DSPinTables）
  → DEF 头族（每文件一任务：嗅探 + 头扫描）+ DEF 头汇总（cell 全集
     快照 + 正式 DSPinGeometry）
  → COMPONENTS 族（每文件一任务：实例 ∥ 网名扫描，正式名字伴生对象
     DSBlockNames_<i> 唯一写定）
  → 网内容族（每文件一任务：正式 DSNet_<i> 唯一写定）+ 网统计汇总
  → 层级树（正式 build_meta 单点写定——消费侧确定性读取锚）
  → 实例解析汇总（正式 DSDesign 首写 + 正式 DSBlock_<i>）
  → 全局密度合并 + 分区决策（DSDesign 补分区重写 + global_density）
  → 跨块连接归并（block 名清单 + 每文件 slice 收集 + net_union 汇总）
  → 分区链编排任务 _plan_partition_chain_task（体内动态提交：展平族
     → 每分区合并〔六类正式对象〕→ id 映射/pg 汇总 → 分区校验族 →
     全局校验〔写 verify_report 固定标记〕——分区数运行时才知，形状
     运行时才知场景）

freeze _freeze_design_task（build_design_db 第③段顶层提交）：依赖固定
标记 verify_report（权威终态锚——蕴含链见任务 docstring）；清理清单
静态构造（_flow_temp_keys 按输入规模枚举）。运行时规模临时对象由产生
链任务自清理：展开分片（分区合并）、校验结果（全局校验）、net_union
slice / pg 片段 / id 映射片段（各自汇总任务）——清理责任单点（§19 批次
口径）。

约定（择简，UserDoc 同步注明）：lef_paths[0] 为 tech lef（确立 DBU 基准
与层堆叠），其余为 cell lef。
"""

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
    sniff_def_header,
    sniff_lef_header,
)


def _tmp_key(name: str) -> str:
    """编排临时对象键（固定名 __dsn__{name}——uid 维度已删，2026-09-17
    用户裁定：一 db 一 flow，task 重放参数原样同键覆盖幂等，uid 不提供
    隔离价值）。键内的内容索引（分组/分区等后缀）是业务寻址保留。"""
    return f"__dsn__{name}"


def _flow_temp_keys(n_cell_lefs: int, n_defs: int) -> list:
    """freeze 清理清单（build_design_db 提交时静态构造——按输入规模
    枚举，§19 批次）。net_union slice / pg 片段 / id 映射片段 / 展开分
    片 / 校验结果不在本清单——由各自消费任务自清理（责任单点）。"""
    keys = [_tmp_key("tech_vias"), _tmp_key("macro_geoms"),
            _tmp_key("lib_library")]
    for i in range(n_cell_lefs):
        keys += [_tmp_key(f"cell_lef_{i}_design"),
                 _tmp_key(f"cell_lef_{i}_geoms"),
                 _tmp_key(f"cell_lef_{i}_vias"),
                 _tmp_key(f"cell_lef_{i}_failed")]
    keys += [_tmp_key("merged"), _tmp_key("lib_merged_design")]
    for i in range(n_defs):
        keys += [_tmp_key(f"header_{i}_cells"),
                 _tmp_key(f"header_{i}_geoms"),
                 _tmp_key(f"header_{i}_vias"),
                 _tmp_key(f"header_{i}_obs")]
    keys.append(_tmp_key("snapshot"))
    for i in range(n_defs):
        keys += [_tmp_key(f"components_{i}_block"),
                 _tmp_key(f"fake_cells_{i}"),
                 _tmp_key(f"nets_{i}_stats")]
    keys += [_tmp_key("hier"), _tmp_key("block_names")]
    return keys


# ── flow 根任务（体内目录见模块 docstring）──────────────────────────

@as_task(inputs=lambda db, lef_paths, def_paths, lib_db: [
    db.get_full_name(DesignDb.ALPHA_SETTINGS_OBJ),
    lib_db.get_full_name(lib_db.LIBRARY_OBJ),
])
def _design_flow_task(db, lef_paths, def_paths, lib_db):
    """按模块 docstring 目录顺序提交全部阶段任务族（键构造随提交——
    编排调用即流程图）。alpha 三键与 lib 快照经 inputs 锚读回（master
    预处理不读内容——lib 快照搬运在任务内，§20）。"""
    settings = db.read_object(DesignDb.ALPHA_SETTINGS_OBJ)
    settings.normalize()
    bin_um = settings.density_bin_size
    net_batch = settings.net_batch_size
    lcp_name_arena = settings.lcp_name_arena
    stack_key = DesignDb.STACK_OBJ
    design_key = DesignDb.DESIGN_OBJ
    geoms_key = DesignDb.PIN_GEOMETRY_OBJ
    tables_key = DesignDb.PIN_TABLES_OBJ

    # ── tech lef（lef_paths[0]，约定见模块 docstring）──
    tech_vias_key = _tmp_key("tech_vias")
    _parse_tech_lef_task(db, lef_paths[0], stack_key, tech_vias_key)

    # ── cell lef 族（产物四元组：design/geoms/vias + 失败标记）+ 汇总
    #    （正式 DSPinGeometry 由 DEF 头汇总唯一写定，此处为中间产物）──
    part_tuples = []
    for i, path in enumerate(lef_paths[1:]):
        keys = (_tmp_key(f"cell_lef_{i}_design"),
                _tmp_key(f"cell_lef_{i}_geoms"),
                _tmp_key(f"cell_lef_{i}_vias"),
                _tmp_key(f"cell_lef_{i}_failed"))
        part_tuples.append(keys)
        _parse_cell_lef_task(db, path, stack_key, *keys)
    merged_key = _tmp_key("merged")
    macro_geoms_key = _tmp_key("macro_geoms")
    _merge_cell_lefs_task(db, part_tuples, stack_key, tech_vias_key,
                          merged_key, macro_geoms_key)

    # ── lib merge（lib 快照先入 design db——merge 任务全程单 db）──
    lib_snapshot_key = _tmp_key("lib_library")
    db.write_object(lib_snapshot_key,
                    lib_db.read_object(lib_db.LIBRARY_OBJ),
                    save_to_db=False)
    s3_key = _tmp_key("lib_merged_design")
    _merge_lib_task(db, merged_key, lib_snapshot_key, tables_key, s3_key)

    # ── DEF 头族 + 头汇总（cell 全集快照临时 + 正式 DSPinGeometry）──
    header_part_keys = []
    for i, path in enumerate(def_paths):
        keys = (_tmp_key(f"header_{i}_cells"),
                _tmp_key(f"header_{i}_geoms"),
                _tmp_key(f"header_{i}_vias"),
                _tmp_key(f"header_{i}_obs"))
        header_part_keys.append(keys)
        _scan_def_header_task(db, path, stack_key, *keys)
    snapshot_key = _tmp_key("snapshot")
    _merge_def_headers_task(db, s3_key, header_part_keys, snapshot_key,
                            geoms_key, macro_geoms_key)

    # ── COMPONENTS 族（正式名字伴生对象 DSBlockNames_<i> 由本任务唯一
    #    写定；正式 DSBlock_<i> 由实例解析汇总写定）──
    temp_block_keys = []
    names_keys = []
    fake_keys = []
    for i, path in enumerate(def_paths):
        block_key = _tmp_key(f"components_{i}_block")
        names_key = DesignDb.names_obj_name(i)
        obs_key = header_part_keys[i][3]  # DEF 头扫描的 obstruction 产物
        fake_key = _tmp_key(f"fake_cells_{i}")
        temp_block_keys.append(block_key)
        names_keys.append(names_key)
        fake_keys.append(fake_key)
        _parse_components_task(db, path, stack_key, snapshot_key, block_key,
                               names_key, obs_key, fake_key,
                               bin_um * 1000,  # µm → DBU（全局基准 ㉝）
                               lcp_name_arena)

    # ── 网内容族（正式 DSNet_<i> 唯一写定——via 计数是层级树区间输入，
    #    先于汇总；临时产物输入使其不依赖正式 DSDesign）+ 统计汇总 ──
    formal_net_keys = [DesignDb.net_obj_name(i) for i in range(len(def_paths))]
    nets_stats_keys = []
    for i, path in enumerate(def_paths):
        stats_key = _tmp_key(f"nets_{i}_stats")
        nets_stats_keys.append(stats_key)
        _parse_nets_task(db, path, stack_key, snapshot_key,
                         temp_block_keys[i], names_keys[i],
                         formal_net_keys[i], stats_key, net_batch,
                         bin_um * 1000)
    _merge_net_stats_task(db, nets_stats_keys)

    # ── 层级树（build_meta 正式对象单点写定——伴生对象全集锚）──
    hier_key = _tmp_key("hier")
    _build_hier_tree_task(db, snapshot_key, temp_block_keys, names_keys,
                          formal_net_keys, hier_key)

    # ── 实例解析汇总（层级树嵌容器 + fake cell 并入 + 正式 DSDesign
    #    首写 + 正式 DSBlock_<i>）──
    formal_block_keys = [DesignDb.block_obj_name(i)
                         for i in range(len(def_paths))]
    _merge_components_task(db, snapshot_key, temp_block_keys, fake_keys,
                           formal_block_keys, design_key, hier_key)

    # ── 全局密度合并 + 分区决策（DSDesign 补 partitions_ 重写 +
    #    global_density 独立对象；alpha 四键任务内读回）──
    global_density_key = DesignDb.GLOBAL_DENSITY_OBJ
    _decide_partitions_task(db, stack_key, hier_key, formal_block_keys,
                            formal_net_keys, design_key,
                            global_density_key, DesignDb.ALPHA_SETTINGS_OBJ)

    # ── 跨块连接归并（与分区决策同级并行——依赖同为层级树 + 网内容
    #    正式产物；slice 为临时对象，汇总任务自清理）──
    net_union_key = DesignDb.NET_UNION_OBJ
    union_slice_keys = [_tmp_key(f"net_union_slice_{i}")
                        for i in range(len(def_paths))]
    block_names_key = _tmp_key("block_names")
    _collect_block_names_task(db, names_keys, block_names_key)
    for i in range(len(def_paths)):
        _collect_net_union_task(db, hier_key, block_names_key,
                                formal_net_keys, i, union_slice_keys[i])
    _merge_net_union_task(db, hier_key, union_slice_keys, net_union_key)

    # ── 分区链编排任务（体内动态提交分区链 + 校验链——形状运行时才知）
    #    S10 校验链同由其提交（merge 之后；freeze 依赖 verify_report）──
    slice_prefix = _tmp_key("s9_slice_")
    verify_prefix = _tmp_key("s10_verify_")
    id_slice_prefix = _tmp_key("id_slice_")
    pg_slice_prefix = _tmp_key("pg_slice_")
    _plan_partition_chain_task(
        db, design_key, global_density_key, hier_key, block_names_key,
        DesignDb.ALPHA_SETTINGS_OBJ, formal_block_keys,
        formal_net_keys, names_keys, def_paths, slice_prefix, verify_prefix,
        stack_key, net_union_key, id_slice_prefix, pg_slice_prefix,
        len(lef_paths) - 1)

    from log import INFO
    INFO(f"design flow: {len(lef_paths)} lef file(s) [1 tech + "
         f"{len(lef_paths) - 1} cell], {len(def_paths)} def file(s), "
         f"flow tasks submitted")


# ── tech lef 解析（单任务：层堆叠 + DBU 基准 + via 权威表）────────────

@as_task(inputs=lambda db, path, stack_key, vias_key: [])
def _parse_tech_lef_task(db, path, stack_key, vias_key):
    """tech lef 解析（含头嗅探——文件形态错在本任务失败透出，入口只查
    存在性）。"""
    from .ds_export import EXDSStack
    sniff_lef_header(path)
    stack = EXDSStack()
    tech_vias = ds_parse_tech_one(path, stack)
    db.write_object(stack_key, stack, save_to_db=True)
    db.write_object(vias_key, tech_vias, save_to_db=False)


# ── cell lef 解析（每文件一任务：局部 id 中间产物 + 失败标记）─────────

@as_task(inputs=lambda db, path, stack_key, design_key, geoms_key, vias_key,
         failed_key: [
    db.get_full_name(stack_key)
])
def _parse_cell_lef_task(db, path, stack_key, design_key, geoms_key,
                         vias_key, failed_key):
    """单 cell lef 解析（含头嗅探）。失败标记随产物传递：单文件语法
    错误不 FAILED（产物已被 adapter 清空），汇总层据此发 DSGN::0014 /
    fatal 0015（流程错误处理范式 2026-09-13）。"""
    sniff_lef_header(path)
    stack = db.read_object(stack_key)
    part_design, part_geoms, part_vias, stats = ds_parse_cell_one(path, stack)
    db.write_object(design_key, part_design, save_to_db=False)
    db.write_object(geoms_key, part_geoms, save_to_db=False)
    db.write_object(vias_key, part_vias, save_to_db=False)
    db.write_object(failed_key,
                    [path] if stats.parse_failed_count else [],
                    save_to_db=False)


# ── cell lef 汇总（统一 cell id + pin hasher 重挂 + via/几何合入）─────

@as_task(inputs=lambda db, part_tuples, stack_key, tech_vias_key, merged_key,
         geoms_key: [
    db.get_full_name(stack_key), db.get_full_name(tech_vias_key)
] + [db.get_full_name(k) for tup in part_tuples for k in tup])
def _merge_cell_lefs_task(db, part_tuples, stack_key, tech_vias_key,
                          merged_key, geoms_key):
    """cell lef 汇总：部分失败 → DSGN::0014（fake cell 承接引用）+ 空产
    物照常汇总；全部失败 → DSGN::0015 fatal（码 80，与 lib 全败同口径）。"""
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
            part_design.add_via_cell(via)
        conflicts += ds_merge_cell_lef(global_design, part_design,
                                       global_geoms, part_geoms)
    tech_vias = db.read_object(tech_vias_key)
    conflicts += ds_merge_def_header(global_design, [], [], global_geoms,
                                     EXDSPinGeometry(), tech_vias)
    if conflicts:
        from log import INFO
        INFO(f"cell lef merge: {conflicts} duplicate macro/via dropped "
             f"(keep first)")

    if failed_paths:
        total = len(part_tuples)
        if len(failed_paths) >= total:
            from fly import fatal_message
            fatal_message("DSGN::0015", 0,
                          f"all {total} cell lef file(s) failed to parse, "
                          f"downstream data cannot be produced: "
                          f"{', '.join(failed_paths)}")
            # fatal_message 不返回（_exit(80)）——下方 DSGN::0014 仅部分
            # 失败路径可达
        from fly import message
        message("DSGN::0014", 0,
                f"{len(failed_paths)}/{total} cell lef file(s) failed to "
                f"parse (skipped, undefined references become fake cells): "
                f"{', '.join(failed_paths)}")

    db.write_object(merged_key, global_design, save_to_db=False)
    # macro pin 几何中间产物（正式 DSPinGeometry 由 DEF 头汇总唯一写定
    # ——正式对象禁止二次覆盖写）
    db.write_object(geoms_key, global_geoms, save_to_db=False)


# ── lib merge（lib cell ↔ lef cell 结构 merge；与 DEF 头扫描并行）────

@as_task(inputs=lambda db, merged_key, lib_obj_name, tables_key, s3_key: [
    db.get_full_name(merged_key),
    db.get_full_name(lib_obj_name),
])
def _merge_lib_task(db, merged_key, lib_obj_name, tables_key, s3_key):
    """按 cell 名并入 lib 库信息（DSGN::0002/0003/0004 由 C++ 侧发送）→
    DSPinTables 正式对象唯一写定。"""
    design = db.read_object(merged_key)
    lib = db.read_object(lib_obj_name)
    matched = design.merge_lib(lib)
    from log import INFO
    INFO(f"merge_lib: {matched} cells matched with lib")
    db.write_object(s3_key, design, save_to_db=False)
    db.write_object(tables_key, design.get_pin_tables(), save_to_db=True)


# ── DEF 头扫描（每文件一任务：嗅探 + 头扫描，大段真跳过）──────────────

@as_task(inputs=lambda db, path, stack_key, cells_key, geoms_key, vias_key,
         obs_key: [
    db.get_full_name(stack_key)
])
def _scan_def_header_task(db, path, stack_key, cells_key, geoms_key, vias_key,
                          obs_key):
    """DEF 头扫描（含头嗅探——文件形态错在本任务失败透出）。obstruction
    为临时产物，由 COMPONENTS 解析回填 per-DEF 产物。"""
    sniff_def_header(path)
    stack = db.read_object(stack_key)
    block_cells, port_names, port_geoms, vias, obstructions = \
        ds_parse_def_one(path, stack)
    db.write_object(cells_key, (block_cells, port_names), save_to_db=False)
    db.write_object(geoms_key, port_geoms, save_to_db=False)
    db.write_object(vias_key, vias, save_to_db=False)
    db.write_object(obs_key, obstructions, save_to_db=False)


# ── DEF 头汇总（block cell / port pin id / via 权威表 → cell 全集快照）─

@as_task(inputs=lambda db, s3_key, part_keys, snapshot_key, geoms_key,
         macro_geoms_key: [
    db.get_full_name(s3_key),
    db.get_full_name(macro_geoms_key),
] + [db.get_full_name(k) for tup in part_keys for k in tup])
def _merge_def_headers_task(db, s3_key, part_keys, snapshot_key, geoms_key,
                            macro_geoms_key):
    """cell 全集快照写临时对象（fake id 基址输入）；正式 DSPinGeometry
    唯一写定。写入顺序红线：先几何后 DSDesign——DSDesign 最后落地，规避
    freeze 先行冻结使几何写失败。"""
    design = db.read_object(s3_key)
    global_geoms = db.read_object(macro_geoms_key)
    for part_keys_i in part_keys:
        cells_key, geoms_key_i, vias_key = part_keys_i[:3]
        block_cells, port_names = db.read_object(cells_key)
        port_geoms = db.read_object(geoms_key_i)
        vias = db.read_object(vias_key)
        ds_merge_def_header(design, block_cells, port_names, global_geoms,
                            port_geoms, vias)
    db.write_object(geoms_key, global_geoms, save_to_db=True)
    db.write_object(snapshot_key, design, save_to_db=False)


# ── COMPONENTS 解析（每文件一任务：实例责任链 ∥ 网名扫描，同一遍读取）─

@as_task(inputs=lambda db, path, stack_key, snapshot_key, block_key,
         names_key, obs_key, fake_key, bin_dbu, lcp_name_arena: [
    db.get_full_name(stack_key),
    db.get_full_name(snapshot_key),
    db.get_full_name(obs_key),
])
def _parse_components_task(db, path, stack_key, snapshot_key, block_key,
                           names_key, obs_key, fake_key, bin_dbu,
                           lcp_name_arena):
    """单 DEF COMPONENTS 解析：实例表/密度通道/统计 + local 网名空间 →
    临时产物；名字伴生对象 DSBlockNames_<i>（instance/net 两 hasher，
    解析期即终态——fake 引用重映射不触及 local id 与名字）由本任务唯一
    写定正式对象。undefined cell → fake cell 兜底（DSGN::0007）。"""
    stack = db.read_object(stack_key)
    design = db.read_object(snapshot_key)
    block_data = EXDSBlockBuildData()
    block_data.set_obstructions(db.read_object(obs_key))
    stats, fake_cells = ds_parse_def_components_one(path, stack, design,
                                                    block_data, bin_dbu)
    # fake cell 数据经独立临时对象传递（并入全局表后即与 cells_ 冗余）
    db.write_object(fake_key, fake_cells, save_to_db=False)
    # lcp_name_arena（裁定 55）：落盘前单一封口点——true = 两 hasher
    # id→name 侧 LCP 后缀压缩，读回按标记自识别
    if lcp_name_arena:
        block_data.finalize_names_for_save(True)
    db.write_object(names_key, block_data.extract_names(), save_to_db=True)
    # 临时产物（fake 引用可能随汇总顺延重映射——正式对象由汇总唯一写定）
    db.write_object(block_key, block_data, save_to_db=False)
    if block_data.stats.fake_cell_count or stats.skipped_net_count:
        from log import INFO
        INFO(f"components '{path}': {block_data.stats.fake_cell_count} fake "
             f"cells, {stats.skipped_net_count} duplicate nets skipped")


# ── 层级树构建（build_meta 单点写定）─────────────────────────────────

@as_task(inputs=lambda db, snapshot_key, temp_block_keys, names_keys,
         formal_net_keys, hier_key: [
    db.get_full_name(snapshot_key),
] + [db.get_full_name(k) for k in temp_block_keys] +
    [db.get_full_name(k) for k in names_keys] +
    [db.get_full_name(k) for k in formal_net_keys])
def _build_hier_tree_task(db, snapshot_key, temp_block_keys, names_keys,
                          formal_net_keys, hier_key):
    """层级树构建 + 起始编号分配（一次建全三类区间——via 计数物理依赖
    网内容解析先行）；主 DEF = 唯一无父者，多根/零根/环 → raise（D22）。
    build_meta 正式对象单点写定（def_count = 伴生对象全集锚，§19 批次：
    本任务 inputs 含全部 DSBlockNames_<i> ⟹ 写定即全集已写）。"""
    design = db.read_object(snapshot_key)
    blocks = [db.read_object(k) for k in temp_block_keys]
    from log import INFO
    for block_data, names_key in zip(blocks, names_keys):
        # attach 为拷贝注入（hasher 归属留伴生对象持有方）
        block_data.attach_names(db.read_object(names_key))
    INFO(f"hier attach done: "
         f"net_counts={[b.net_count for b in blocks]}")
    nets = [db.read_object(k) for k in formal_net_keys]
    tree = ds_build_hier_tree(blocks, nets, design)
    INFO(f"hier tree: {tree.node_count} block instances, top="
         f"'{tree.design_name}'")
    db.write_object(hier_key, tree, save_to_db=False)
    db.write_object(DesignDb.BUILD_META_OBJ, {"def_count": len(names_keys)},
                    save_to_db=True)


# ── 实例解析汇总（fake cell 并入全局表〔id 保持 ⑳〕→ 正式对象写定）──

@as_task(inputs=lambda db, snapshot_key, temp_block_keys, fake_keys,
         formal_block_keys, design_key, hier_key: [
    db.get_full_name(snapshot_key),
    db.get_full_name(hier_key),
] + [db.get_full_name(k) for k in temp_block_keys]
  + [db.get_full_name(k) for k in fake_keys])
def _merge_components_task(db, snapshot_key, temp_block_keys, fake_keys,
                           formal_block_keys, design_key, hier_key):
    """层级树嵌容器 + fake cell 并入（冲突顺延）+ 统计合并 → 正式
    DSBlock_<i> 唯一写定 + 正式 DSDesign 首写（容器唯一写定原则）。"""
    design = db.read_object(snapshot_key)
    tree = db.read_object(hier_key)
    assert isinstance(tree, EXDSHierTree)
    design.set_hier_tree(tree)
    total_leaf = 0
    total_net = 0
    total_fake = 0
    for temp_key, fake_key, formal_key in zip(temp_block_keys, fake_keys,
                                              formal_block_keys):
        block_data = db.read_object(temp_key)
        fake_cells = db.read_object(fake_key)
        total_fake += ds_merge_block_build(design, block_data, fake_cells)
        total_leaf += block_data.stats.instance_count
        total_net += block_data.net_count
        # per-DEF 产物正式写定（即产即落盘；不含名字——名字在伴生对象）
        db.write_object(formal_key, block_data, save_to_db=True)
    from log import INFO
    INFO(f"components merge: {total_leaf} instances, {total_net} nets, "
         f"{total_fake} fake cells merged")
    db.write_object(design_key, design, save_to_db=True)


# ── 网内容解析（每文件一任务：责任链 ∥ 分批多阶段，同一遍流式读取）───

@as_task(inputs=lambda db, path, stack_key, snapshot_key, block_key,
         names_key, net_key, stats_key, net_batch, bin_dbu: [
    db.get_full_name(stack_key),
    db.get_full_name(snapshot_key),
    db.get_full_name(block_key),
    db.get_full_name(names_key),
])
def _parse_nets_task(db, path, stack_key, snapshot_key, block_key, names_key,
                     net_key, stats_key, net_batch, bin_dbu):
    """单 DEF 网内容解析（批大小 alpha 键 net_batch_size）：local net id
    经名字伴生对象对齐（⑨）+ via cell 权威表快照 → 正式 DSNet_<i> 唯一
    写定（无全局重排语义，任务直写）+ 统计临时对象。undefined via 兜底
    DSGN::0008。"""
    stack = db.read_object(stack_key)
    design = db.read_object(snapshot_key)
    block_data = db.read_object(block_key)
    # attach 为拷贝注入（hasher 归属留伴生对象持有方；对齐只读）
    block_data.attach_names(db.read_object(names_key))
    net_data = EXDSNetBuildData()
    stats = ds_parse_def_nets_one(path, stack, design, block_data, net_data,
                                  bin_dbu, net_batch)
    db.write_object(net_key, net_data, save_to_db=True)
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


# ── 网统计汇总（统计合并 → DSGN::0009）───────────────────────────────

@as_task(inputs=lambda db, stats_keys: [
    db.get_full_name(k) for k in stats_keys
])
def _merge_net_stats_task(db, stats_keys):
    """网内容统计合并 → DSGN::0009（网数/几何/via instance 数）。"""
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


# ── S8：全局密度合并 + 分区决策（core/extend 双区域裁定 2026-09-12/13）─

@as_task(inputs=lambda db, stack_key, hier_key, block_keys, net_keys,
         design_key, density_key, settings_key: [
    db.get_full_name(stack_key),
    db.get_full_name(hier_key),
    db.get_full_name(design_key),
    db.get_full_name(settings_key),
] + [db.get_full_name(k) for k in block_keys] +
    [db.get_full_name(k) for k in net_keys])
def _decide_partitions_task(db, stack_key, hier_key, block_keys, net_keys,
                            design_key, density_key, settings_key):
    """全局密度合并 + 分区决策（行列前缀和切分）：DSDesign 补 partitions_
    重写 + global_density 独立对象写定。与跨块连接归并同级并行、互不依
    赖（红线：只依赖层级树 + per-DEF 正式产物 + stack + alpha_settings）。
    alpha 四键读回（非法值 C++ 侧 DSGN::0013 提醒回退，不 raise）。"""
    from log import INFO
    stack = db.read_object(stack_key)
    tree = db.read_object(hier_key)
    blocks = [db.read_object(k) for k in block_keys]
    nets = [db.read_object(k) for k in net_keys]
    design = db.read_object(design_key)
    settings = db.read_object(settings_key)
    settings.normalize()
    weights = settings.density_channel_weights
    target_partitions = settings.target_partitions or ""  # None/空 = 未设置
    partition_count = settings.partition_count
    target_density = settings.partition_target_density

    global_density = ds_merge_global_density(tree, blocks, nets)
    partitions = ds_decide_partitions(global_density, stack,
                                      weights["instance"], weights["metal"],
                                      weights["via"], target_partitions,
                                      partition_count, target_density)
    design.set_partitions(partitions)
    # 正式对象已写定——先 remove 规避 DUPLICATE_SKIPPED 静默丢弃二次写；
    # freeze 依赖本任务末尾写的 global_density（重写先于该写完成）
    db.remove_object(design_key)
    db.write_object(design_key, design, save_to_db=True)
    db.write_object(density_key, global_density, save_to_db=True)
    INFO(f"partition: {len(partitions)} partitions, global density "
         f"total={global_density.total_count} "
         f"metal={global_density.metal_total} via={global_density.via_total}")


# ── S7：跨块连接归并（并查集，2026-09-13 裁定：id 域 = global net id、
#    仅 port 相连网、两层树 root = 层级最高的网/同级最小 global id、
#    单对象不分块）────────────────────────────────────────────────────

@as_task(inputs=lambda db, names_keys, block_names_key: [
    db.get_full_name(k) for k in names_keys
])
def _collect_block_names_task(db, names_keys, block_names_key):
    """block 名清单（def_paths 序）+ 首份序号表（重名定义保留首份判据
    的 O(1) 查表——D9）。slice 任务据清单只读本 def 引用的子定义网产物。"""
    block_names = [db.read_object(k).block_name for k in names_keys]
    first_seen = {}
    is_first = [first_seen.setdefault(name, i) == i
                for i, name in enumerate(block_names)]
    db.write_object(block_names_key, {"names": block_names,
                                      "is_first": is_first},
                    save_to_db=False)


@as_task(inputs=lambda db, hier_key, block_names_key, net_keys, index,
         slice_key: [
    db.get_full_name(hier_key), db.get_full_name(block_names_key)
] + [db.get_full_name(k) for k in net_keys])
def _collect_net_union_task(db, hier_key, block_names_key, net_keys, index,
                            slice_key):
    """每父块 DEF 一任务：本 def 网产物 + 树 + 引用的子定义网产物 →
    (父网, 子网) union 边 + port 网 local id 集。重名 def 非首份不收集
    （其连接表会按 block 名反查命中首份实例化位置，产生错误归并）。"""
    tree = db.read_object(hier_key)
    block_info = db.read_object(block_names_key)
    block_names = block_info["names"]
    if not block_info["is_first"][index]:
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
def _merge_net_union_task(db, hier_key, slice_keys, union_key):
    """全局汇总（单任务）：合并全部局部边集 → 两层化 + root 规范化 →
    net_union 正式对象唯一写定 + slice 自清理（责任单点）；悬空 port
    DSGN::0018 计数提醒（不 raise）。"""
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


# ── 分区链编排（体内动态提交：展平族 → 每分区合并 → id 映射/pg 汇总
#    → 分区校验族 → 全局校验）─────────────────────────────────────────

@as_task(inputs=lambda db, design_key, global_density_key, hier_key,
         block_names_key, alpha_key, block_keys, net_keys, names_keys,
         def_paths, slice_prefix, verify_prefix, stack_key,
         net_union_key, id_slice_prefix, pg_slice_prefix,
         n_cell_lefs: [
    db.get_full_name(design_key), db.get_full_name(global_density_key),
    db.get_full_name(hier_key), db.get_full_name(block_names_key),
    db.get_full_name(alpha_key),
])
def _plan_partition_chain_task(db, design_key, global_density_key, hier_key,
                               block_names_key, alpha_key, block_keys,
                               net_keys, names_keys, def_paths, slice_prefix,
                               verify_prefix, stack_key, net_union_key,
                               id_slice_prefix, pg_slice_prefix,
                               n_cell_lefs):
    """分区链编排（依赖 global_density 锚——DSDesign 补分区重写完成的
    保证，竞态实证见 design-db 文档）→ worker 上动态提交下游链（分区
    数运行时才知，形状运行时才知场景——同 solver kickoff 先例）：
    分组（D26：预估 = DEF 文件大小 × 树上实例化次数，≥ 阈值独占、低于
    阈值贪心聚合；重名定义保留首份）→ 每组一展平任务 → 每分区一合并
    任务〔六类正式对象〕→ id 映射/pg 汇总 → 每分区一校验任务 → 全局
    校验（写 verify_report 固定标记——freeze 依赖）。"""
    import os
    design = db.read_object(design_key)
    tree = db.read_object(hier_key)
    block_info = db.read_object(block_names_key)
    block_names = block_info["names"]
    is_first = block_info["is_first"]
    settings = db.read_object(alpha_key)
    settings.normalize()
    threshold = settings.def_aggregate_threshold
    # names_keys 不被展开消费（连接 id 化后展开零名字查询）——仅透传给
    # 全局校验任务（namemap 全查）

    # 树上实例化计数（block cell 名 → 出现次数；root 含其定义自身）
    inst_count = {}
    for i in range(tree.node_count):
        name = tree.node(i).block_cell_name
        inst_count[name] = inst_count.get(name, 0) + 1

    groups = []
    current = []
    acc = 0
    for i, path in enumerate(def_paths):
        if not is_first[i]:
            continue  # 重名保留首份（is_first 查表——D9）
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

    # 展平任务（每组一任务，只读本组 def 产物——每份 DEF 数据只读一次）
    for g, group in enumerate(groups):
        _flatten_group_task(db, design_key, hier_key, block_names_key,
                            group,
                            [block_keys[i] for i in group],
                            [net_keys[i] for i in group],
                            f"{slice_prefix}{g}_", len(partitions))
    # 每分区一合并任务（六类正式对象唯一写定 + id/pg 片段临时对象）
    for pid, xp, yp in partitions:
        _merge_partition_task(db, slice_prefix, len(groups), pid, xp, yp,
                              id_slice_prefix, pg_slice_prefix)
    # id→partition 映射汇总（INST/NET 各一）+ 全局 pg 网 id 集汇总
    for kind in DesignDb.ID_MAP_KINDS:
        _merge_id_map_task(db, id_slice_prefix, len(partitions), kind)
    _merge_pg_nets_task(db, pg_slice_prefix, len(partitions))
    # 校验链（每分区一校验任务并行 + 全局汇总校验——损坏类 fatal 阻断
    # 冻结，观测类 warn 不阻断）
    verify_keys = []
    for pid, xp, yp in partitions:
        verify_key = f"{verify_prefix}{pid}"
        verify_keys.append(verify_key)
        _verify_partition_task(db, pid, xp, yp, verify_key)
    _verify_design_task(db, verify_keys, design_key, stack_key,
                        global_density_key, net_union_key, hier_key,
                        block_keys, net_keys, names_keys,
                        DesignDb.VERIFY_REPORT_OBJ)


@as_task(inputs=lambda db, design_key, hier_key, block_names_key,
         group, block_keys, net_keys, slice_prefix, n_parts: (
    [db.get_full_name(k) for k in (design_key, hier_key, block_names_key)]
    + [db.get_full_name(k) for k in block_keys]
    + [db.get_full_name(k) for k in net_keys]))
def _flatten_group_task(db, design_key, hier_key, block_names_key,
                        group, block_keys, net_keys, slice_prefix, n_parts):
    """per-组展平任务：读本组各 def 的单份解析产物 → ds_flatten_block
    全部出现位置展开（复合变换 + 三类 id 换算 + 放置点归属 + 几何副本 +
    连接补全）→ 按分区累积分片，对全部分区各写一份（未触达分区写空产物
    ——合并任务依赖恒可解）。零名字查询、零 pin 几何依赖。"""
    design = db.read_object(design_key)
    tree = db.read_object(hier_key)
    block_info = db.read_object(block_names_key)
    block_names = block_info["names"]
    products = {}
    for i, block_key, net_key in zip(group, block_keys, net_keys):
        if not block_info["is_first"][i]:
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
def _merge_partition_task(db, slice_prefix, n_groups, pid, xp, yp,
                          id_slice_prefix, pg_slice_prefix):
    """每分区一合并任务（分区侧真实合并语义，裁定 ⑤）：merge 全部相关
    分片 → 六类正式对象
    PART_{xp}_{yp}.{GEOMETRY,GEOMETRY_PG,INSTANCES,INST_CONNECTIONS,
    NETS,NETS_PG} 唯一写定（GEOMETRY/NETS 各按 pg/信号拆两对象——
    2026-09-14 拆分裁定）+ 本区 id→partition 片段与 pg 网片段临时对象
    （各自汇总任务清理）。本区全部分片读毕自清理（每分片唯一消费者 =
    本任务——清理责任单点）。"""
    from log import INFO
    product = EXDSPartitionProduct()
    slice_keys = [f"{slice_prefix}{g}_{pid}" for g in range(n_groups)]
    for key in slice_keys:
        product.merge_from(db.read_object(key))
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
    db.write_object(
        DesignDb.id_slice_obj_name(id_slice_prefix, pid, "INST"),
        ds_collect_inst_id_slice(product.instances(), pid),
        save_to_db=False)
    db.write_object(
        DesignDb.id_slice_obj_name(id_slice_prefix, pid, "NET"),
        ds_collect_net_id_slice(product.geometry(), product.geometry_pg(),
                                pid),
        save_to_db=False)
    db.write_object(f"{pg_slice_prefix}{pid}",
                    ds_collect_pg_net_slice(product.nets_pg()),
                    save_to_db=False)
    for key in slice_keys:
        db.remove_object(key)
    INFO(f"partition product ({xp},{yp}): {product.instance_count} "
         f"instances, geometry {product.geometry().net_count} bucket(s) / "
         f"pg {product.geometry_pg().net_count}, nets {product.nets().size} "
         f"/ pg {product.nets_pg().size}")


# ── 全局 pg 网 id 集汇总（多分区 pg 片段 → 两 set 去重合并）──────────

@as_task(inputs=lambda db, pg_slice_prefix, n_parts: [
    db.get_full_name(f"{pg_slice_prefix}{pid}") for pid in range(n_parts)
])
def _merge_pg_nets_task(db, pg_slice_prefix, n_parts):
    """pg 网 id 集汇总（单任务）：ds_build_pg_net_set 两 set 去重合并（同
    pg 网跨分区副本只此一条）→ PG_NETS_OBJ 正式对象唯一写定 + 片段自清
    理（责任单点）。"""
    slices = [db.read_object(f"{pg_slice_prefix}{pid}")
              for pid in range(n_parts)]
    pg_set = ds_build_pg_net_set(slices)
    db.write_object(DesignDb.PG_NETS_OBJ, pg_set, save_to_db=True)
    for pid in range(n_parts):
        db.remove_object(f"{pg_slice_prefix}{pid}")
    from log import INFO
    INFO(f"pg net set: power={pg_set.power_count} "
         f"ground={pg_set.ground_count} merged from {n_parts} slice(s)")


# ── id → partition 反向映射汇总（INST/NET 各一）──────────────────────

@as_task(inputs=lambda db, id_slice_prefix, n_parts, kind: [
    db.get_full_name(DesignDb.id_slice_obj_name(id_slice_prefix, pid, kind))
    for pid in range(n_parts)
])
def _merge_id_map_task(db, id_slice_prefix, n_parts, kind):
    """id→partition 映射汇总任务：读全部分区片段 → 全空间分段（空洞段
    跳过）→ 段对象逐段写正式对象 + 段表最后写定（段表在即全部段对象已
    在——freeze 依赖段表即足）+ 片段自清理（责任单点）。"""
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


# ── 汇总校验 + 冻结前置（2026-09-13 校验分级：损坏类 fatal / 观测类
#    warn）─────────────────────────────────────────────────────────────

@as_task(inputs=lambda db, pid, xp, yp, result_key: [
    db.get_full_name(DesignDb.partition_obj_name(xp, yp, kind))
    for kind in DesignDb.PARTITION_KINDS
])
def _verify_partition_task(db, pid, xp, yp, result_key):
    """每分区一校验任务（并行读单分区六类产物——红线：不跨区读）：分区
    级计数 + 全局校验素材 id 集提取；损坏类判定集中在全局校验任务。"""
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
       for k in (design_key, stack_key, density_key, union_key, hier_key,
                 DesignDb.PG_NETS_OBJ,
                 DesignDb.id_map_index_obj_name("INST"),
                 DesignDb.id_map_index_obj_name("NET"))]
    + [db.get_full_name(k) for k in block_keys]
    + [db.get_full_name(k) for k in net_keys]
    + [db.get_full_name(k) for k in names_keys]))
def _verify_design_task(db, verify_keys, design_key, stack_key,
                        density_key, union_key, hier_key, block_keys,
                        net_keys, names_keys, report_key):
    """全局汇总校验（单任务）：树 + 全部分区校验结果 + net_union +
    DSDesign（分区表/hashers）+ stack + global_density + pg_nets + id
    映射段表（后三者入 inputs 使本任务写定的 verify_report 蕴含全链正式
    对象——freeze 单锚的依据）+ per-DEF 产物与伴生名（namemap 全查）。
    分级处置：损坏类（并查集/分区覆盖/namemap）→ fatal（码 80 + master
    联动）——report 不落盘、freeze 依赖缺失，损坏库不冻结；观测类
    （DSGN::0022/0023）→ user warn 不阻断。校验结果读毕自清理（责任
    单点）。"""
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
    ds_verify_report_or_fatal(report)
    from fly import message
    # 观测类（不阻断冻结）：id 连续性——空洞含 UNPLACED 实例/空网/root
    # 自身等合法形态，重复 = instance 多 primary 超量
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
    for key in verify_keys:
        db.remove_object(key)
    db.write_object(report_key, report, save_to_db=True)


# ── freeze（build_design_db 第③段顶层提交）──────────────────────────

@as_task(inputs=lambda db, n_cell_lefs, n_defs: [
    db.get_full_name(DesignDb.VERIFY_REPORT_OBJ)])
def _freeze_design_task(db, n_cell_lefs, n_defs):
    """freeze：依赖固定标记 verify_report（权威终态锚——蕴含链：全局
    校验任务 inputs 含各分区校验结果〔⟸ 六类分区对象〕+ DSDesign 重写
    版〔global_density 锚〕+ pg_nets + id 映射段表 + per-DEF 正式产物
    与伴生名 + stack ⟹ 全链正式对象先写定，freeze 不可能早于任一正式
    产物）。清理清单静态构造（_flow_temp_keys 按输入规模枚举；每个键
    清理责任唯一归属本任务——严格 remove，缺失即 KeyError 暴露清理责
    任错位）。"""
    for key in _flow_temp_keys(n_cell_lefs, n_defs):
        db.remove_object(key)
    db.freeze()
