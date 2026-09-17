"""build_timing_db flow 的任务链装配（plan §6：直接任务链，不用
MapReduce——用户裁定 2026-09-15）。

阶段链（plan §6；design db 为直接前驱，快照消费形态同 lib merge 先例
——跨 db 读取在 master 侧完成，worker 只读本 db）：
  design 快照组装（master 侧 @wait_obj 阻塞等 design db 必要数据对象——
    层级树随 DSDesign、DSBlockNames_<i> 伴生对象全集、id_partition_map
    段表、pg_nets、global_density[S8 分区表锚]；名字伴生对象/段对象/
    分区 NETS 对象均在锚后写定，函数体内直读。plan §3.1 入口等待语义：
    不等待 design db freeze，只等必要数据对象）→ 绑定目标校验（块实例
    路径/块 cell 名未命中 → 入口 ValueError，plan §7.5 可 raise）→ 快照
    逐对象落 timing db 临时对象（全部已有序列化类型）
  → T1 切块扫描（master 侧单任务，同步执行——文件已入口校验可读，字节
    流扫顶层构造边界为毫秒级 I/O；块大小 = alpha chunk_size_mb）→
    TMChunkPlan 临时对象（块数就此确定，T2 静态提交）
  → T2 逐块解析任务（每块一任务，全并行；块自包含 = 头段公共前缀拼块，
    评审 P1-1）：
    [inputs 注入快照临时对象] → worker 端组装 EXTMDesignContext（共享
    注入零拷贝）→ tm_convert_chunk（块解析 + §7 名字换算 + 分区路由）→
    TMEntrySlice 分片临时对象（单块语法破损 → 空分片照常产出，依赖链
    保持满足——plan §6 范式 (b)）
  → 时钟表合并任务（独立任务，依赖全部 slices——评审 P1-1：提前至 T3
    之前，产出 clocks 正式对象 + EXTMClockRemap 重映射桥 + TIMG::0007
    冲突计数 temp）
  → T3 每分区一合并任务（依赖 slices + remap——时钟归属经 remap 重写
    为最终表 id）→ PART_{xp}_{yp}.TIMING 正式对象唯一写定 + 跨文件冲突
    数临时对象
  → T4 汇总任务（summary 聚合 + TIMG 消息族一次汇总透出 + 全部文件失
    败 → TIMG::0009 fatal 码 80，范式 (a)）→ summary 正式对象唯一写定
  → freeze 任务（依赖全部分区对象 + clocks + summary 写完 + 中间对象
    清理）

流程入口 build_timing_db 在 tm_db.py（UserDoc + Schema + @register_flow）。
"""

from uuid import uuid4

from fly import as_task, fatal_message, message
from task import wait_obj

from emir.design import DesignDb, ds_make_name_mapper

from .tm_db import TimingDb
from .tm_export import (
    EXTMClockTable,
    EXTMChunkPlan,
    EXTMDesignContext,
    EXTMFileBinding,
    tm_convert_chunk,
    tm_merge_clocks,
    tm_merge_partition,
    tm_merge_summary,
    tm_plan_file_chunks,
)


def _tmp_key(uid: str, name: str) -> str:
    return f"__tmg__{uid}__{name}"


# ── design db 快照组装（master 侧同步；wait_obj 阻塞等必要对象）──────

@wait_obj(inputs=lambda db, design_db, files: [
    design_db.get_full_name(DesignDb.DESIGN_OBJ),
    design_db.get_full_name(DesignDb.GLOBAL_DENSITY_OBJ),
    design_db.get_full_name(DesignDb.PG_NETS_OBJ),
    design_db.get_full_name(DesignDb.id_map_index_obj_name("INST")),
    design_db.get_full_name(DesignDb.id_map_index_obj_name("NET")),
])
def snapshot_design_context(db, design_db, files):
    """组装 design db 快照并落 timing db 临时对象（plan §3.1 入口等待
    语义的执行点；返回 (分区清单 [(pid, xp, yp)], 快照键表)。

    锚集证明（与 design flow 写序对照）：DESIGN_OBJ 首写于 S5a 汇总 ⟹
    DSBlockNames_<i> 全集已写；GLOBAL_DENSITY_OBJ 写于 S8 ⟹ 分区表已填
    （S8 补分区重写后锚——同 _s9_plan_task 口径）；id map 段表写于 S9
    末段 ⟹ 分区六类正式对象已写（段表最后写定 ⟹ 段对象已写）；pg_nets
    同为 S9 产物。函数体内直读的其余对象均在锚后。
    """
    design = design_db.read_object(DesignDb.DESIGN_OBJ)
    # 名字伴生对象全集（S5a 落盘序 = def 序；逐个读到缺）
    names_list = []
    i = 0
    while True:
        try:
            names_list.append(
                design_db.read_object(DesignDb.names_obj_name(i)))
        except Exception:
            break
        i += 1

    # 绑定目标校验（plan §7.5：块实例路径 / 块 cell 名未命中 → 该文件
    # 入口 ValueError——输入语义错误，可 raise 两类之一）
    mapper = ds_make_name_mapper(design, names_list, 0)  # instance 维度
    for f in files:
        if f["kind"] == 1 and mapper.get_global_id(f["block_inst"]) is None:
            raise ValueError(
                f"timing_files: block_inst '{f['block_inst']}' not found "
                f"in design db (file '{f['file_name']}')")
        if f["kind"] == 2 and design.find_cell(f["block_cell"]) is None:
            raise ValueError(
                f"timing_files: block_cell '{f['block_cell']}' not found "
                f"in design db (file '{f['file_name']}')")

    # 快照逐对象落临时对象（全部已有序列化类型；T2 任务在 worker 端
    # read_object 后组装 EXTMDesignContext）
    uid = uuid4().hex[:8]
    keys = {}

    def _snap(name, obj):
        key = _tmp_key(uid, name)
        db.write_object(key, obj, save_to_db=False)
        keys[name] = key

    _snap("design", design)
    _snap("inst_index",
          design_db.read_object(DesignDb.id_map_index_obj_name("INST")))
    _snap("net_index",
          design_db.read_object(DesignDb.id_map_index_obj_name("NET")))
    _snap("pg_nets", design_db.read_object(DesignDb.PG_NETS_OBJ))
    for j, names in enumerate(names_list):
        _snap(f"names_{j}", names)
    # id map 段对象（段表 id_starts 升序；段粒度与 design db 同值口径）
    for map_kind, seg_prefix in (("INST", "inst_seg"), ("NET", "net_seg")):
        index = design_db.read_object(DesignDb.id_map_index_obj_name(map_kind))
        for seg_start in index.id_starts:
            seg_index = seg_start >> DesignDb.ID_MAP_SEGMENT_BITS
            _snap(f"{seg_prefix}_{seg_index}",
                  design_db.read_object(
                      DesignDb.id_map_segment_obj_name(map_kind, seg_index)))
    # 分区 NETS 对象（网条目 driver 位定位源；分区表行主序）
    partitions = []
    for p in range(design.partition_count):
        part = design.partition_at(p)
        pid, xp, yp = part.partition_id, part.xp, part.yp
        partitions.append((pid, xp, yp))
        _snap(f"part_nets_{pid}",
              design_db.read_object(
                  DesignDb.partition_obj_name(xp, yp, "NETS")))
    return partitions, keys


# ── T2 逐块解析任务（每块一任务，全并行）────────────────────────────

@as_task(inputs=lambda db, snapshot_keys, plan_key, file_index, chunk_index,
         binding_kind, block_inst, block_cell, strip_prefix,
         slice_key: (
    [db.get_full_name(k) for k in snapshot_keys.values()]
    + [db.get_full_name(plan_key)]))
def _t2_chunk_task(db, snapshot_keys, plan_key, file_index, chunk_index,
                   binding_kind, block_inst, block_cell, strip_prefix,
                   slice_key):
    """单块执行：worker 端组装 EXTMDesignContext（共享注入零拷贝）→
    tm_convert_chunk（块解析 + 名字换算 + 分区路由）→ TMEntrySlice 分片。
    绑定描述以散字段传参（pickle 友好），任务内组装 EXTMFileBinding。
    单块语法破损 → C++ 侧空分片 + failed_chunk 计数（依赖链保持满足）。"""
    ctx = EXTMDesignContext()
    ctx.set_design(db.read_object(snapshot_keys["design"]))
    ctx.set_inst_id_map(db.read_object(snapshot_keys["inst_index"]))
    ctx.set_net_id_map(db.read_object(snapshot_keys["net_index"]))
    ctx.set_pg_nets(db.read_object(snapshot_keys["pg_nets"]))
    j = 0
    while f"names_{j}" in snapshot_keys:
        ctx.add_block_names(db.read_object(snapshot_keys[f"names_{j}"]))
        j += 1
    # snapshot_keys = {对象名: 对象键}（段对象/分区 NETS 逐个注入）
    for name, key in snapshot_keys.items():
        if name.startswith("inst_seg_"):
            ctx.add_inst_segment(db.read_object(key))
        elif name.startswith("net_seg_"):
            ctx.add_net_segment(db.read_object(key))
        elif name.startswith("part_nets_"):
            ctx.add_partition_nets(db.read_object(key))

    plan = db.read_object(plan_key)
    file_plan = plan.files[file_index]

    binding = EXTMFileBinding()
    binding.kind = binding_kind
    binding.block_inst = block_inst
    binding.block_cell = block_cell
    binding.strip_prefix = strip_prefix
    slice_obj = tm_convert_chunk(
        ctx, file_plan.file_name, file_plan.prefix_start,
        file_plan.prefix_end, file_plan.chunk_starts[chunk_index],
        file_plan.chunk_ends[chunk_index], binding, file_index)
    from log import INFO
    if slice_obj.stats.files:
        fs = slice_obj.stats.files[0]
        # 绑定形态才有 block_inst 字段（纯路径形态为空串，不拼 bi 段）
        bi = f" bi='{block_inst}'" if binding_kind else ""
        INFO(f"t2 chunk {file_index}/{chunk_index}: kind={binding_kind}"
             f"{bi} entry={fs.entry_count} "
             f"hit={fs.hit_count} skip_inst={fs.skipped_instance_count} "
             f"net_miss={fs.net_name_miss_count} skip_pin={fs.skipped_pin_count} "
             f"dangling={fs.dangling_net_count} unplaced={fs.unplaced_instance_count} "
             f"strip={fs.strip_miss_count} failed={fs.failed_chunk_count} "
             f"parts={list(slice_obj.partition_ids)}")
    else:
        INFO(f"t2 chunk {file_index}/{chunk_index}: NO stats files")
    db.write_object(slice_key, slice_obj, save_to_db=False)


# ── 时钟表合并任务（独立任务，评审 P1-1：提前至 T3 之前执行）──────────

@as_task(inputs=lambda db, files, slice_keys, clocks_key, remap_key,
         clock_conflicts_key: (
    [db.get_full_name(k) for k in slice_keys]))
def _t_clock_merge_task(db, files, slice_keys, clocks_key, remap_key,
                        clock_conflicts_key):
    """时钟表跨文件合并（顶层文件定义优先，裁定 5）→ clocks 正式对象唯一
    写定 + EXTMClockRemap 重映射桥（块内时钟 id → 最终表下标——T3 重写
    时钟归属的依据）+ TIMG::0007 冲突计数 temp（T4 聚合入 summary）。"""
    slices = [db.read_object(k) for k in slice_keys]
    # 时钟表跨文件合并：文件内跨块收集 → 顶层文件（kind 0）定义优先、
    # 块绑定文件按文件序首份兜底；周期/沿差异计数（TIMG::0007）
    per_file_clocks = []
    for fi, f in enumerate(files):
        table = EXTMClockTable()
        for s in slices:
            if s.stats.file_index == fi:
                for c in s.stats.clocks:
                    table.add_clock(c.name, c.period, c.posedge, c.negedge)
        per_file_clocks.append((f["kind"], table))
    clocks_table, clock_conflicts, remap = tm_merge_clocks(per_file_clocks)
    db.write_object(clocks_key, clocks_table, save_to_db=True)
    db.write_object(remap_key, remap, save_to_db=False)
    db.write_object(clock_conflicts_key, clock_conflicts, save_to_db=False)
    from log import INFO
    INFO(f"timing flow: clock table merged, {clocks_table.size} clock(s), "
         f"{clock_conflicts} conflict(s)")


# ── T3 每分区一合并任务 ─────────────────────────────────────────────

@as_task(inputs=lambda db, slice_keys, remap_key, pid, xp, yp, part_key,
         conflicts_key: (
    [db.get_full_name(k) for k in slice_keys]
    + [db.get_full_name(remap_key)]))
def _t3_partition_task(db, slice_keys, remap_key, pid, xp, yp, part_key,
                       conflicts_key):
    """每分区合并：收集各块分片中本分区的片段 → 时钟归属经 remap 重写为
    最终表 id（评审 P1-1）→ merge → 正式对象唯一写定（冲突保留首份计数
    temp——TIMG::0006 由 T4 聚合入 summary）。"""
    slices = [db.read_object(k) for k in slice_keys]
    remap = db.read_object(remap_key)
    part, conflicts = tm_merge_partition(slices, remap, pid)
    db.write_object(part_key, part, save_to_db=True)
    db.write_object(conflicts_key, conflicts, save_to_db=False)


# ── T4 汇总任务（summary + 消息族 + fatal 判定）────────────────────

@as_task(inputs=lambda db, files, slice_keys, conflicts_keys,
         clock_conflicts_key, plan_key, summary_key: (
    [db.get_full_name(k) for k in slice_keys + conflicts_keys]
    + [db.get_full_name(clock_conflicts_key), db.get_full_name(plan_key)]))
def _t4_summary_task(db, files, slice_keys, conflicts_keys,
                     clock_conflicts_key, plan_key, summary_key):
    """汇总：summary 聚合（时钟表与冲突计数由时钟表合并任务产出，本任务
    读入 clock_conflicts）+ TIMG 消息族一次汇总透出 + 全部文件失败 fatal
    （TIMG::0009，范式 (a)）。summary 正式对象唯一写定。"""
    from log import INFO
    slices = [db.read_object(k) for k in slice_keys]
    cross_conflicts = sum(db.read_object(k) for k in conflicts_keys)
    clock_conflicts = db.read_object(clock_conflicts_key)
    plan = db.read_object(plan_key)
    file_chunk_counts = [len(fp.chunk_starts) for fp in plan.files]

    summary = tm_merge_summary([s.stats for s in slices], cross_conflicts,
                               clock_conflicts)

    # ── fatal 判定（plan §6 范式 (a)）：全部文件全部块失败 ──
    failed = [(f, n) for f, n in zip(summary.files, file_chunk_counts)
              if f.failed_chunk_count >= n]
    if failed and len(failed) == len(files):
        listing = ", ".join(f.source_file for f, _ in failed)
        fatal_message(
            "TIMG::0009", 0,
            f"all {len(files)} timing file(s) failed to parse, downstream "
            f"data cannot be produced: {listing}")
        # fatal_message 不返回（_exit(80) + master 联动）

    # ── TIMG 消息族一次汇总透出（C++ 计数器 → flow 侧一次发送）──
    if summary.total_hit_count == 0:
        message("TIMG::0004", 0,
                f"all {len(files)} timing file(s) parsed but 0 valid "
                f"entries matched the design db (empty result)")
    if summary.skipped_instance_count or summary.net_name_miss_count:
        message("TIMG::0001", 0,
                f"{summary.skipped_instance_count} instance name(s) and "
                f"{summary.net_name_miss_count} net name(s) not matched in "
                f"design db (skipped)")
    if summary.skipped_pin_count:
        message("TIMG::0002", 0,
                f"{summary.skipped_pin_count} pin name(s) not matched "
                f"(skipped)")
    if summary.dangling_net_count:
        message("TIMG::0003", 0,
                f"{summary.dangling_net_count} net entr(y/ies) without a "
                f"driver or dangling (skipped)")
    if cross_conflicts:
        message("TIMG::0006", 0,
                f"{cross_conflicts} cross-file duplicate entr(y/ies) "
                f"(first occurrence kept)")
    if clock_conflicts:
        message("TIMG::0007", 0,
                f"{clock_conflicts} clock name(s) with cross-file period/"
                f"edge mismatch (top-level definition kept)")
    if summary.unplaced_instance_count:
        message("TIMG::0008", 0,
                f"{summary.unplaced_instance_count} entr(y/ies) skipped for "
                f"unplaced instances (no primary partition copy)")
    if summary.strip_miss_count:
        message("TIMG::0010", 0,
                f"{summary.strip_miss_count} entr(y/ies) skipped: "
                f"strip_prefix not matched (including fully-stripped names)")
    if failed and len(failed) < len(files):
        # 部分文件失败：空分片已照常产出（依赖链满足），失败清单可追溯
        listing = ", ".join(f.source_file for f, _ in failed)
        INFO(f"timing flow: {len(failed)}/{len(files)} file(s) fully failed "
             f"to parse: {listing}")

    db.write_object(summary_key, summary, save_to_db=True)
    INFO(f"timing summary: {summary.total_hit_count}/"
         f"{summary.total_entry_count} entries matched, {len(files)} "
         f"file(s), {summary.missing_clock_count} missing clock group(s)")


# ── freeze：依赖正式对象写完 + 中间对象清理 ──────────────────────────

@as_task(inputs=lambda db, final_keys, temp_keys: (
    [db.get_full_name(k) for k in final_keys]))
def _freeze_timing_task(db, final_keys, temp_keys):
    for key in temp_keys:
        try:
            db.remove_object(key)
        except Exception:
            pass
    db.freeze()


def run_timing_flow(db, design_db, files, settings):
    """提交全部阶段任务（非阻塞）。files = normalize_timing_files 归一化
    描述列表；settings = TMAlphaSettings（db 读回 normalize 兜底）。"""
    from log import INFO
    chunk_size = settings.chunk_size_mb * 1024 * 1024
    uid = uuid4().hex[:8]

    # design 快照组装（master 阻塞等 design db 必要对象 → 校验绑定 →
    # 快照落临时对象）
    partitions, snapshot_keys = snapshot_design_context(db, design_db, files)
    INFO(f"timing flow: design snapshot done, {len(partitions)} "
         f"partition(s), {len(snapshot_keys)} snapshot object(s)")

    # T1 切块扫描（master 侧单任务，同步执行——plan §6 字面形态；文件已
    # 入口校验可读，扫描为毫秒级 I/O；块数就此确定，T2 静态提交）
    plan = EXTMChunkPlan()
    for f in files:
        plan.add_file(tm_plan_file_chunks(f["file_name"], chunk_size))
    plan_key = _tmp_key(uid, "chunk_plan")
    db.write_object(plan_key, plan, save_to_db=False)
    total_chunks = sum(len(fp.chunk_starts) for fp in plan.files)
    INFO(f"timing flow: {len(files)} file(s) planned into {total_chunks} "
         f"chunk task(s)")

    # T2 每块一任务（全并行；绑定描述以散字段传参——pickle 友好）
    slice_keys = []
    for fi, fp in enumerate(plan.files):
        for cj in range(len(fp.chunk_starts)):
            slice_key = _tmp_key(uid, f"chunk_{fi}_{cj}")
            slice_keys.append(slice_key)
            _t2_chunk_task(db, snapshot_keys, plan_key, fi, cj,
                           files[fi]["kind"], files[fi]["block_inst"],
                           files[fi]["block_cell"],
                           files[fi]["strip_prefix"], slice_key)

    # 时钟表合并任务（独立任务，依赖全部 slices——评审 P1-1：提前至 T3
    # 之前，产出 clocks 正式对象 + remap 桥 + 0007 冲突计数 temp）
    clocks_key = TimingDb.CLOCKS_OBJ
    remap_key = _tmp_key(uid, "clock_remap")
    clock_conflicts_key = _tmp_key(uid, "clock_conflicts")
    _t_clock_merge_task(db, files, slice_keys, clocks_key, remap_key,
                        clock_conflicts_key)

    # T3 每分区一合并任务（依赖 remap——时钟归属重写为最终表 id；正式对
    # 象唯一写定 + 冲突计数 temp）
    conflicts_keys = []
    for pid, xp, yp in partitions:
        conflicts_key = _tmp_key(uid, f"conflicts_{pid}")
        conflicts_keys.append(conflicts_key)
        _t3_partition_task(db, slice_keys, remap_key, pid, xp, yp,
                           TimingDb.partition_obj_name(xp, yp), conflicts_key)

    # T4 汇总（summary 正式对象唯一写定 + 消息族 + fatal 判定）
    summary_key = TimingDb.SUMMARY_OBJ
    _t4_summary_task(db, files, slice_keys, conflicts_keys,
                     clock_conflicts_key, plan_key, summary_key)

    # freeze：正式对象集 + 中间对象清理（alpha_settings 入口已写定）
    final_keys = [TimingDb.partition_obj_name(xp, yp)
                  for _, xp, yp in partitions] + [clocks_key, summary_key]
    temp_keys = [plan_key] + list(snapshot_keys.values()) + slice_keys \
        + conflicts_keys + [remap_key, clock_conflicts_key]
    _freeze_timing_task(db, final_keys, temp_keys)
