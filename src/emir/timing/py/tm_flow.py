"""build_timing_db flow 任务链（直接任务链，不用 MapReduce——用户裁定
2026-09-15；plan docs/emir/timing-db-plan.md §6）。

三段式（dev-rules §3「建库 API 流程标准」）：入口 tm_db.build_timing_db
①轻量参数预处理（schema 校验 + 文件存在性 + 建库 + alpha_settings 写入
——不读文件内容）②提交唯一根任务 ③顶层提交 freeze。master 提交 O(1)
个任务，与输入规模无关（§20 编排判据）。

根任务 _timing_flow_task 体内目录（依赖系统等 design db 必要对象——
plan §3.1 入口等待语义，不等待 design db freeze）：
  快照搬运（design db 逐对象 → timing db 临时对象 + 快照键清单对象）
  → 绑定校验（条目级兜底：单文件无效跳过 + TIMG::0011；全无效
     任务内 ValueError）
  → 每有效文件一个文件入口任务 _plan_file_chunks_task（TWF 嗅探 +
     切块扫描 + 体内提交该文件逐块解析任务 + 块数写清单对象——文件
     内容 IO 全在 worker 并行，文件级错误由本任务失败透出）
  → 合并链编排任务 _plan_merge_chain_task（依赖全部清单对象——齐备
     即全部文件入口完成；体内提交：时钟表合并 → 每分区合并 → 汇总。
     切片键集〔块数〕运行时才知，动态提交——形状运行时才知场景）

freeze _freeze_timing_task（build_timing_db 第③段顶层提交）：依赖固定
标记 clocks/summary（summary ⟸ 各分区冲突计数临时对象 ⟸ 分区合并先写
正式对象；clocks ⟸ 时钟合并——freeze 不可能早于任一正式产物）+ 清理
快照键清单展开的快照对象（键集规模运行时，根任务写定清单）。其余运行
时规模临时对象（切片/remap/冲突计数/清单）由汇总任务自清理——清理责任
单点（§19 批次口径）。
"""

from fly import as_task, fatal_message, message

from emir.design import DesignDb, ds_make_instance_name_mapper

from .tm_db import TimingDb
from .tm_export import (
    EXTMClockTable,
    EXTMDesignContext,
    EXTMFileBinding,
    tm_convert_chunk,
    tm_merge_clocks,
    tm_merge_partition,
    tm_merge_summary,
    tm_plan_file_chunks,
)
from .tm_utils import sniff_twf_header


def _tmp_key(name: str) -> str:
    """编排临时对象键（固定名 __tmg__{name}——uid 维度已删，2026-09-17
    用户裁定：一 db 一 flow，task 重放参数原样同键覆盖幂等，uid 不提供
    隔离价值）。键内的内容索引（chunk_{fi}_{cj}/conflicts_{pid}）是业务
    寻址保留。"""
    return f"__tmg__{name}"


# ── flow 根任务（体内目录见模块 docstring）──────────────────────────

@as_task(inputs=lambda db, design_db, files: [
    design_db.get_full_name(DesignDb.DESIGN_OBJ),
    design_db.get_full_name(DesignDb.GLOBAL_DENSITY_OBJ),
    design_db.get_full_name(DesignDb.PG_NETS_OBJ),
    design_db.get_full_name(DesignDb.id_map_index_obj_name("INST")),
    design_db.get_full_name(DesignDb.BUILD_META_OBJ),
    db.get_full_name(TimingDb.ALPHA_SETTINGS_OBJ),
])
def _timing_flow_task(db, design_db, files):
    """快照搬运 → 绑定校验 → 文件入口任务族 + 合并链编排任务（体内
    目录见模块 docstring；锚集证明见 design-db 文档——DESIGN_OBJ 锚
    即伴生对象全集，GLOBAL_DENSITY 锚即分区表，INST 段表锚即分区六类
    正式对象）。"""
    design = design_db.read_object(DesignDb.DESIGN_OBJ)
    # 名字伴生对象全集（数量锚 build_meta.def_count——确定性循环读取）
    meta = design_db.load_build_meta()
    names_list = [design_db.read_object(DesignDb.names_obj_name(i))
                  for i in range(meta["def_count"])]
    mapper = ds_make_instance_name_mapper(design, names_list)

    # ── 绑定校验（条目级兜底 2026-09-17：单文件无效 → TIMG::0011 +
    #    跳过该文件〔不进切块链、零条目〕；仅全部文件无效才 ValueError
    #    ——输入整体无意义。结构错/不可读已在入口拦截）──
    invalid = []  # (文件序, 字段名, 目标值)
    for fi, f in enumerate(files):
        if f["kind"] == 1 and mapper.get_global_id(f["block_inst"]) is None:
            invalid.append((fi, "block_inst", f["block_inst"]))
        elif f["kind"] == 2 and design.find_cell(f["block_cell"]) is None:
            invalid.append((fi, "block_cell", f["block_cell"]))
    if len(invalid) == len(files):
        listing = "; ".join(
            f"{field} '{value}' (file '{files[fi]['file_name']}')"
            for fi, field, value in invalid)
        raise ValueError(
            f"timing_files: no binding target resolved in design db — "
            f"all {len(files)} file(s) skipped: {listing}")
    invalid_indexes = sorted(fi for fi, _, _ in invalid)
    for fi, field, value in invalid:
        message("TIMG::0011", 0,
                f"binding target {field} '{value}' not found in design db "
                f"(file '{files[fi]['file_name']}') — file skipped, "
                f"0 entries stored")

    # ── 快照搬运（逐对象落临时对象；块解析任务 worker 端组装
    #    EXTMDesignContext。键集规模运行时——落快照键清单对象供 freeze
    #    清理展开〔静态依赖仅此一键〕）──
    keys = {}

    def _snap(name, obj):
        key = _tmp_key(name)
        db.write_object(key, obj, save_to_db=False)
        keys[name] = key

    _snap("design", design)
    _snap("inst_index",
          design_db.read_object(DesignDb.id_map_index_obj_name("INST")))
    _snap("pg_nets", design_db.read_object(DesignDb.PG_NETS_OBJ))
    for j, names in enumerate(names_list):
        _snap(f"names_{j}", names)
    # INST 段对象（段表 id_starts 升序）+ 分区 NETS 对象（分区表行主序）
    index = design_db.read_object(DesignDb.id_map_index_obj_name("INST"))
    for seg_start in index.id_starts:
        seg_index = seg_start >> DesignDb.ID_MAP_SEGMENT_BITS
        _snap(f"inst_seg_{seg_index}",
              design_db.read_object(
                  DesignDb.id_map_segment_obj_name("INST", seg_index)))
    partitions = []
    for p in range(design.partition_count):
        part = design.partition_at(p)
        pid, xp, yp = part.partition_id, part.xp, part.yp
        partitions.append((pid, xp, yp))
        _snap(f"part_nets_{pid}",
              design_db.read_object(
                  DesignDb.partition_obj_name(xp, yp, "NETS")))
    snapshot_keys_obj = _tmp_key("snapshot_keys")
    db.write_object(snapshot_keys_obj, sorted(keys.values()),
                    save_to_db=False)

    # ── 文件入口任务（每有效文件一个；chunk_size 经 alpha_settings
    #    读回）──
    settings = db.read_object(TimingDb.ALPHA_SETTINGS_OBJ)
    settings.normalize()
    chunk_size = settings.chunk_size_mb * 1024 * 1024
    invalid_set = set(invalid_indexes)
    valid_file_indexes = [fi for fi in range(len(files))
                          if fi not in invalid_set]
    manifest_keys = [_tmp_key(f"manifest_{fi}")
                     for fi in valid_file_indexes]
    for fi, manifest_key in zip(valid_file_indexes, manifest_keys):
        f = files[fi]
        # 绑定描述以散字段传参（pickle 友好）；全参数位置传递——
        # as_task 序列化仅覆盖位置参数
        _plan_file_chunks_task(db, f["file_name"], f["kind"],
                               f["block_inst"], f["block_cell"],
                               f["strip_prefix"], fi, chunk_size, keys,
                               len(names_list), manifest_key)

    # ── 合并链编排任务（体内提交时钟合并/每分区合并/汇总）──
    _plan_merge_chain_task(db, files, valid_file_indexes, partitions,
                           manifest_keys, invalid_indexes)

    from log import INFO
    INFO(f"timing flow: {len(keys)} snapshot object(s), "
         f"{len(valid_file_indexes)}/{len(files)} file(s) valid, "
         f"{len(partitions)} partition(s)")


# ── 文件入口任务（每文件一个：嗅探 + 切块扫描 + 提交逐块解析）────────

@as_task(inputs=lambda db, file_name, binding_kind, block_inst, block_cell,
         strip_prefix, file_index, chunk_size, snapshot_keys, names_count,
         manifest_key: [])
def _plan_file_chunks_task(db, file_name, binding_kind, block_inst,
                           block_cell, strip_prefix, file_index, chunk_size,
                           snapshot_keys, names_count, manifest_key):
    """文件入口任务：TWF 头嗅探（文件形态错在此失败透出——入口只查
    存在性，不读内容）+ tm_plan_file_chunks 切块扫描 + 体内提交该文件
    逐块解析任务（块区间散标量传参）+ 块数写清单对象（合并链编排与
    汇总的消费锚）。"""
    sniff_twf_header(file_name)
    fp = tm_plan_file_chunks(file_name, chunk_size)
    for cj in range(len(fp.chunk_starts)):
        _parse_chunk_task(db, snapshot_keys, file_name, fp.prefix_start,
                          fp.prefix_end, fp.chunk_starts[cj],
                          fp.chunk_ends[cj], file_index, cj, binding_kind,
                          block_inst, block_cell, strip_prefix,
                          _tmp_key(f"chunk_{file_index}_{cj}"),
                          names_count)
    db.write_object(manifest_key, {"chunk_count": len(fp.chunk_starts)},
                    save_to_db=False)
    from log import INFO
    INFO(f"timing file {file_index}: planned into "
         f"{len(fp.chunk_starts)} chunk(s)")


# ── 逐块解析任务（每块一任务，全并行）────────────────────────────────

@as_task(inputs=lambda db, snapshot_keys, file_name, prefix_start,
         prefix_end, chunk_start, chunk_end, file_index, chunk_index,
         binding_kind, block_inst, block_cell, strip_prefix, slice_key,
         names_count: [db.get_full_name(k) for k in snapshot_keys.values()])
def _parse_chunk_task(db, snapshot_keys, file_name, prefix_start,
                      prefix_end, chunk_start, chunk_end, file_index,
                      chunk_index, binding_kind, block_inst, block_cell,
                      strip_prefix, slice_key, names_count):
    """单块解析：worker 端组装 EXTMDesignContext（共享注入零拷贝）→
    tm_convert_chunk（解析 + 名字换算 + 分区路由）→ TMEntrySlice 分片。
    块自包含 = 头段公共前缀拼块；单块语法破损 → 空分片 + failed_chunk
    计数（依赖链保持满足）。"""
    ctx = EXTMDesignContext()
    ctx.set_design(db.read_object(snapshot_keys["design"]))
    ctx.set_inst_id_map(db.read_object(snapshot_keys["inst_index"]))
    ctx.set_pg_nets(db.read_object(snapshot_keys["pg_nets"]))
    # names_count 确定性循环（规模随参数传递，禁键集试探——§19 批次）
    for j in range(names_count):
        ctx.add_block_names(db.read_object(snapshot_keys[f"names_{j}"]))
    for name, key in snapshot_keys.items():
        if name.startswith("inst_seg_"):
            ctx.add_inst_segment(db.read_object(key))
        elif name.startswith("part_nets_"):
            ctx.add_partition_nets(db.read_object(key))

    binding = EXTMFileBinding()
    binding.set_kind(binding_kind)
    binding.block_inst = block_inst
    binding.block_cell = block_cell
    binding.strip_prefix = strip_prefix
    slice_obj = tm_convert_chunk(ctx, file_name, prefix_start, prefix_end,
                                 chunk_start, chunk_end, binding, file_index)
    from log import INFO
    if slice_obj.stats.files:
        fs = slice_obj.stats.files[0]
        bi = f" bi='{block_inst}'" if binding_kind else ""
        INFO(f"chunk parse {file_index}/{chunk_index}: "
             f"kind={binding_kind}{bi} entry={fs.entry_count} "
             f"hit={fs.hit_count} skip_inst={fs.skipped_instance_count} "
             f"net_miss={fs.net_name_miss_count} "
             f"skip_pin={fs.skipped_pin_count} "
             f"dangling={fs.dangling_net_count} "
             f"unplaced={fs.unplaced_instance_count} "
             f"strip={fs.strip_miss_count} failed={fs.failed_chunk_count} "
             f"parts={list(slice_obj.partition_ids)}")
    else:
        INFO(f"chunk parse {file_index}/{chunk_index}: NO stats files")
    db.write_object(slice_key, slice_obj, save_to_db=False)


# ── 合并链编排任务（清单对象齐备即全部文件入口完成）──────────────────

@as_task(inputs=lambda db, files, valid_file_indexes, partitions,
         manifest_keys, invalid_indexes: [
             db.get_full_name(k) for k in manifest_keys])
def _plan_merge_chain_task(db, files, valid_file_indexes, partitions,
                           manifest_keys, invalid_indexes):
    """合并链编排：读全部清单对象推导切片键集 → 提交时钟表合并 →
    每分区合并 → 汇总（写 clocks/summary 固定标记）。切片键集（块数）
    运行时才知——动态提交（「形状运行时才知」场景，dev-rules §3）。"""
    slice_keys = [
        _tmp_key(f"chunk_{fi}_{cj}")
        for fi, manifest_key in zip(valid_file_indexes, manifest_keys)
        for cj in range(db.read_object(manifest_key)["chunk_count"])
    ]
    clocks_key = TimingDb.CLOCKS_OBJ
    remap_key = _tmp_key("clock_remap")
    clock_conflicts_key = _tmp_key("clock_conflicts")
    _merge_clocks_task(db, files, slice_keys, clocks_key, remap_key,
                       clock_conflicts_key)

    conflicts_keys = []
    for pid, xp, yp in partitions:
        conflicts_key = _tmp_key(f"conflicts_{pid}")
        conflicts_keys.append(conflicts_key)
        _merge_partition_task(db, slice_keys, remap_key, pid, xp, yp,
                              TimingDb.partition_obj_name(xp, yp),
                              conflicts_key)

    _merge_summary_task(db, files, slice_keys, conflicts_keys,
                        clock_conflicts_key, manifest_keys,
                        TimingDb.SUMMARY_OBJ, invalid_indexes, remap_key)
    from log import INFO
    INFO(f"timing merge chain: {len(slice_keys)} slice(s), "
         f"{len(partitions)} partition(s) submitted")


# ── 时钟表合并任务（独立任务，提前至分区合并之前——评审 P1-1）────────

@as_task(inputs=lambda db, files, slice_keys, clocks_key, remap_key,
         clock_conflicts_key: (
    [db.get_full_name(k) for k in slice_keys]))
def _merge_clocks_task(db, files, slice_keys, clocks_key, remap_key,
                       clock_conflicts_key):
    """时钟表跨文件合并（顶层文件定义优先，裁定 5）→ clocks 正式对象
    唯一写定 + EXTMClockRemap 重映射桥（块内时钟 id → 最终表下标）+
    TIMG::0007 冲突计数 temp（汇总任务聚合入 summary）。"""
    slices = [db.read_object(k) for k in slice_keys]
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


# ── 每分区一合并任务 ─────────────────────────────────────────────────

@as_task(inputs=lambda db, slice_keys, remap_key, pid, xp, yp, part_key,
         conflicts_key: (
    [db.get_full_name(k) for k in slice_keys]
    + [db.get_full_name(remap_key)]))
def _merge_partition_task(db, slice_keys, remap_key, pid, xp, yp, part_key,
                          conflicts_key):
    """每分区合并：收集各块分片中本分区的片段 → 时钟归属经 remap 重写
    为最终表 id → merge → PART_{xp}_{yp}.TIMING 正式对象唯一写定（冲突
    保留首份计数 temp——TIMG::0006 由汇总聚合）。"""
    slices = [db.read_object(k) for k in slice_keys]
    remap = db.read_object(remap_key)
    part, conflicts = tm_merge_partition(slices, remap, pid)
    db.write_object(part_key, part, save_to_db=True)
    db.write_object(conflicts_key, conflicts, save_to_db=False)


# ── 汇总任务（summary + 消息族 + fatal 判定 + 运行时键自清理）──────────

@as_task(inputs=lambda db, files, slice_keys, conflicts_keys,
         clock_conflicts_key, manifest_keys, summary_key,
         invalid_file_indexes, remap_key: (
    [db.get_full_name(k) for k in slice_keys + conflicts_keys]
    + [db.get_full_name(clock_conflicts_key)]
    + [db.get_full_name(k) for k in manifest_keys]))
def _merge_summary_task(db, files, slice_keys, conflicts_keys,
                        clock_conflicts_key, manifest_keys, summary_key,
                        invalid_file_indexes, remap_key):
    """汇总：summary 聚合 + TIMG 消息族一次汇总透出 + 全部文件失败
    fatal（TIMG::0009）+ 运行时规模临时对象自清理（切片/remap/冲突
    计数/清单——本任务为全链最后读者，清理责任单点：conflicts ⟸ 分区
    合并、remap ⟸ 时钟合并均经本任务 inputs 传递依赖先行完成）。"""
    from log import INFO
    invalid_set = set(invalid_file_indexes)
    slices = [db.read_object(k) for k in slice_keys]
    cross_conflicts = sum(db.read_object(k) for k in conflicts_keys)
    clock_conflicts = db.read_object(clock_conflicts_key)
    # 逐文件块数表按有效文件序（manifest 键序 = 有效文件序——与
    # summary.files 首现序 zip 配对，被跳过文件不进逐文件表）
    file_chunk_counts = [db.read_object(k)["chunk_count"]
                         for k in manifest_keys]
    valid_count = len(files) - len(invalid_set)

    summary = tm_merge_summary([s.stats for s in slices], cross_conflicts,
                               clock_conflicts, len(invalid_set))

    # fatal 判定（plan §6 范式 (a)）：全部文件全部块失败
    failed = [(f, n) for f, n in zip(summary.files, file_chunk_counts)
              if f.failed_chunk_count >= n]
    if failed and len(failed) == valid_count:
        listing = ", ".join(f.source_file for f, _ in failed)
        fatal_message(
            "TIMG::0009", 0,
            f"all {valid_count} timing file(s) failed to parse, downstream "
            f"data cannot be produced: {listing}")
        # fatal_message 不返回（_exit(80) + master 联动）

    if summary.total_hit_count == 0:
        message("TIMG::0004", 0,
                f"all {valid_count} timing file(s) parsed but 0 valid "
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
    if failed and len(failed) < valid_count:
        # 部分文件失败：空分片已照常产出，失败清单可追溯
        listing = ", ".join(f.source_file for f, _ in failed)
        INFO(f"timing flow: {len(failed)}/{valid_count} file(s) fully "
             f"failed to parse: {listing}")

    # 运行时规模临时对象自清理（均为本任务 inputs，必然存在——严格
    # remove，缺失即 KeyError 暴露清理责任错位）
    for key in slice_keys + conflicts_keys + manifest_keys \
            + [remap_key, clock_conflicts_key]:
        db.remove_object(key)
    db.write_object(summary_key, summary, save_to_db=True)
    INFO(f"timing summary: {summary.total_hit_count}/"
         f"{summary.total_entry_count} entries matched, {valid_count} "
         f"file(s) ({len(invalid_set)} skipped for invalid binding), "
         f"{summary.missing_clock_count} missing clock group(s)")


# ── freeze（build_timing_db 第③段顶层提交）──────────────────────────

@as_task(inputs=lambda db, snapshot_keys_obj: [
    db.get_full_name(TimingDb.CLOCKS_OBJ),
    db.get_full_name(TimingDb.SUMMARY_OBJ)])
def _freeze_timing_task(db, snapshot_keys_obj):
    """freeze：依赖固定标记 clocks/summary（蕴含链：summary ⟸ 汇总
    inputs 含各分区冲突计数与全部切片 ⟹ 分区对象/时钟表先写定——
    freeze 不可能早于任一正式产物）。清理快照键清单展开 + 清单对象
    自身（严格 remove——§19 批次口径）。"""
    for key in db.read_object(snapshot_keys_obj):
        db.remove_object(key)
    db.remove_object(snapshot_keys_obj)
    db.freeze()
