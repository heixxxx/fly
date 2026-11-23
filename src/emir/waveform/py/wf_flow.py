"""build_waveform_db flow 任务链（直接任务链，同 timing db §6 全先例；
plan docs/emir/waveform-db-plan.md §6）。

三段式（dev-rules §3「建库 API 流程标准」）：入口 wf_db.build_waveform_db
①轻量参数预处理（schema 校验 + 文件存在性 + settings 结构 + settings-only
模式判定 + 建库 + alpha_settings 写入——不读文件内容）②提交唯一根任务
③顶层提交 freeze。master 提交 O(1) 个任务，与输入规模无关（§20 编排判
据）。

根任务 _waveform_flow_task 体内目录（依赖系统等 design db 必要对象——
plan §3.1 入口等待语义，不等待 design db freeze）：
  VCD 模式（vcd_files 非空）：
    绑定校验（条目级兜底：单文件无效跳过 + WFMG::0011；全无效
       任务内 ValueError）
    → 每有效文件一个文件入口任务 _wf_file_entry_task（gzip 魔数嗅探
       → 解压副本进常规链〔裁定 IV-1〕+ 切块扫描〔档位随头部解析产
       出 + 缺失 $timescale 形态错在此透出〕+ var 表写 db temp 对象
       〔裁定 IV-4：逐块任务按引用共享，不随任务参数复制〕+ 体内提
       交该文件逐块解析任务 + 块数/var 键/解压目录写清单对象——文件
       内容 IO 全在 worker 并行，文件级错误由本任务失败透出）
    → 合并链编排任务 _wf_plan_merge_chain_task（依赖全部清单对象——齐
       备即全部文件入口完成；体内提交：每分区合并 → 汇总）
  settings-only 模式（vcd_files=None，settings.activity 必给）：
    design db 分区表枚举 → 每分区一物化任务 _wf_materialize_task（空
       WFPartitionWaveform + overrides 换算物化 WFPartitionActivity〔net
       名覆盖经形态①推导展开；实例枚举 = design db 分区 INSTANCES 对
       象〕）→ settings 汇总任务 _wf_settings_summary_task（settings
       元数据 → summary）

混合模式（裁定 IV-2：VCD 输入 + settings.activity.overrides 并存）：
  每分区合并任务体内合并产物落库前 wf_apply_overrides 替换命中条目统
  计值（换算同 settings-only；waveform_id 引用保留；default 在 VCD 模
  式不生效）；覆盖条目计数经 counts temp 入 summary，未解析条目按全
  分区聚合口径一次透出（WFMG::0001 家族——全部分区都未命中的条目才
  计，同 settings-only 聚合裁定）。

块解析/合并任务跨 db 直读 design db（§21「db 数据所有权：禁跨 db 数
据搬运」）：inputs 锚 design db 对象全名，任务体内经 design_db 句柄
直读组装 EXWFDesignContext。var 表/块对象（temp）落 waveform db 按
引用共享（裁定 IV-4；EXWF* pickle 落库同款协议）；var 表为反序列化
产物，块任务体内 wf_convert_chunk 前必须 rebuild_indexes()（运行时
索引不序列化）。

gzip 压缩 VCD（裁定 IV-1）：文件入口任务读头两字节检测魔数（0x1f
0x8b）→ Python 标准库 gzip 流式解压到 db 临时目录
（<db_path>/wf_scratch/，副本名带文件序防同名冲突）→ 切块/块任务按
解压副本普通处理（头关键字校验对副本执行）；明文文件零开销直通。
解压目录由汇总任务删除（freeze 清理链上的单点清理——汇总为全部块任
务后的最后读者；建库失败残留随 db 目录生命周期归零）。

freeze _freeze_waveform_task（build_waveform_db 第③段顶层提交）：依赖
固定标记 summary（蕴含链：summary ⟸ 汇总 inputs 含全部切片/清单/分
区计数 ⟹ 分区两正式对象先写定——freeze 不可能早于任一正式产物）。运
行时规模临时对象（切片/var 表/分区计数/清单）与解压副本目录由汇总
任务自清理——清理责任单点（§19 批次口径）。
"""

from fly import as_task, fatal_message, message

from emir.design import DesignDb, ds_make_instance_name_mapper

from .wf_db import WaveformDb
from .wf_export import (
    EXWFActivityOverride,
    EXWFDesignContext,
    EXWFFileBinding,
    EXWFSettingsMeta,
    wf_apply_overrides,
    wf_convert_chunk,
    wf_empty_partition_waveform,
    wf_materialize_activity,
    wf_merge_partition,
    wf_merge_summary,
    wf_plan_file_chunks,
)


def _tmp_key(name: str) -> str:
    """编排临时对象键（固定名 __wfg__{name}——同 timing db 裁定：一 db
    一 flow，task 重放参数原样同键覆盖幂等。键内的内容索引
    （chunk_{fi}_{cj}/vars_{fi}/counts_{pid}）是业务寻址保留）。"""
    return f"__wfg__{name}"


SCRATCH_DIR_NAME = "wf_scratch"
# gzip 魔数（裁定 IV-1：文件入口任务读头两字节嗅探——IEEE 1952 gzip
# 固定首两字节）
GZIP_MAGIC = b"\x1f\x8b"
# 解压流式拷贝块大小（1 MiB——大 VCD 常量内存）
_DECOMP_CHUNK_BYTES = 1024 * 1024


def _decompress_if_gzip(file_name, file_index, scratch_dir):
    """gzip 魔数嗅探 → 解压副本（裁定 IV-1）；明文文件零开销直通。

    返回 (切块/块任务实际读的路径, 解压目录 or None)。副本名带文件序
    ——多文件同名 basename 不冲突；流式解压常量内存。副本落 db 目录
    下 SCRATCH_DIR_NAME（waveform db 临时目录），删除责任单点在汇总
    任务（freeze 清理链）。"""
    with open(file_name, "rb") as f:
        magic = f.read(2)
    if magic != GZIP_MAGIC:
        return file_name, None
    import gzip
    import os
    import shutil
    os.makedirs(scratch_dir, exist_ok=True)
    out_path = os.path.join(
        scratch_dir, f"{file_index}_{os.path.basename(file_name)}")
    with gzip.open(file_name, "rb") as src, \
            open(out_path, "wb") as dst:
        shutil.copyfileobj(src, dst, length=_DECOMP_CHUNK_BYTES)
    return out_path, scratch_dir


def _override_objects(overrides):
    """settings overrides 参数（dict 列表）→ EXWFActivityOverride 列表
    （缺省键与 C++ 侧 WFActivityOverride 缺省一致——物化任务与混合模
    式合并任务共用，§4 换算口径同源）。"""
    ovs = []
    for item in overrides:
        ov = EXWFActivityOverride()
        ov.name = item["name"]
        ov.toggle_rate = item["toggle_rate"]
        ov.static_probability = item["static_probability"]
        ov.rise_fraction = item["rise_fraction"]
        ovs.append(ov)
    return ovs


def _design_enum_anchors(design_db, names_count, inst_seg_indexes,
                         partitions):
    """块任务的 design db 依赖锚名列表（跨 db 直读——§21；枚举锚与读取
    源同源：names_count/INST 段号/分区表由根任务读 design db 得出）。"""
    return (
        [design_db.get_full_name(DesignDb.DESIGN_OBJ),
         design_db.get_full_name(DesignDb.id_map_index_obj_name("INST")),
         design_db.get_full_name(DesignDb.PG_NETS_OBJ)]
        + [design_db.get_full_name(DesignDb.names_obj_name(j))
           for j in range(names_count)]
        + [design_db.get_full_name(
            DesignDb.id_map_segment_obj_name("INST", s))
           for s in inst_seg_indexes]
        + [design_db.get_full_name(
            DesignDb.partition_obj_name(xp, yp, "NETS"))
           for _, xp, yp in partitions])


def _assemble_design_context(design_db, names_count, inst_seg_indexes,
                             partitions):
    """worker 端跨 db 直读组装 EXWFDesignContext（共享注入零拷贝——
    §21 禁跨 db 数据搬运，waveform db 无 design 数据副本；names_count/
    段号/分区表由根任务读 design db 得出后透传）。"""
    ctx = EXWFDesignContext()
    ctx.set_design(design_db.read_object(DesignDb.DESIGN_OBJ))
    index = design_db.read_object(DesignDb.id_map_index_obj_name("INST"))
    ctx.set_inst_id_map(index)
    ctx.set_pg_nets(design_db.read_object(DesignDb.PG_NETS_OBJ))
    # names_count 确定性循环（规模随参数传递，禁键集试探——§19 批次）
    for j in range(names_count):
        ctx.add_block_names(design_db.read_object(
            DesignDb.names_obj_name(j)))
    for seg_index in inst_seg_indexes:
        ctx.add_inst_segment(design_db.read_object(
            DesignDb.id_map_segment_obj_name("INST", seg_index)))
    for _, xp, yp in partitions:
        ctx.add_partition_nets(design_db.read_object(
            DesignDb.partition_obj_name(xp, yp, "NETS")))
    return ctx


# ── flow 根任务（体内目录见模块 docstring）──────────────────────────

@as_task(inputs=lambda db, design_db, files, meta_params, overrides,
         duration_fs: [
    design_db.get_full_name(DesignDb.DESIGN_OBJ),
    design_db.get_full_name(DesignDb.GLOBAL_DENSITY_OBJ),
    design_db.get_full_name(DesignDb.PG_NETS_OBJ),
    design_db.get_full_name(DesignDb.id_map_index_obj_name("INST")),
    design_db.get_full_name(DesignDb.BUILD_META_OBJ),
    db.get_full_name(WaveformDb.ALPHA_SETTINGS_OBJ),
])
def _waveform_flow_task(db, design_db, files, meta_params, overrides,
                        duration_fs):
    """绑定校验 → 文件入口任务族 + 合并链编排任务（VCD 模式）/ 物化链
    （settings-only 模式）；体内目录见模块 docstring。锚集证明见
    design-db 文档——DESIGN_OBJ 锚即伴生对象全集，GLOBAL_DENSITY 锚即
    分区表，INST 段表锚即分区正式对象。design db 数据零搬运（§21）。"""
    design = design_db.read_object(DesignDb.DESIGN_OBJ)
    # 分区表（行主序，DESIGN_OBJ 锚即分区表——§21 跨 db 直读，无快照搬运）
    partitions = []
    for p in range(design.partition_count):
        part = design.partition_at(p)
        partitions.append((part.partition_id, part.xp, part.yp))

    if not files:
        # ── settings-only 模式（vcd_files=None）：每分区一物化任务 →
        #    settings 汇总（overrides/duration_fs 为入口已校验参数）──
        # 名字伴生对象全集（数量锚 build_meta.def_count——overrides 的
        # 实例名直配经 inst mapper 解析，names 集必须注入）
        meta = design_db.load_build_meta()
        names_count = meta["def_count"]
        counts_keys = []
        for pid, xp, yp in partitions:
            counts_key = _tmp_key(f"counts_{pid}")
            counts_keys.append(counts_key)
            _wf_materialize_task(db, design_db, pid=pid, xp=xp, yp=yp,
                                 names_count=names_count,
                                 partitions=partitions, overrides=overrides,
                                 duration_fs=duration_fs,
                                 counts_key=counts_key)
        _wf_settings_summary_task(db, overrides=overrides,
                                  meta_params=meta_params,
                                  counts_keys=counts_keys)
        from log import INFO
        INFO(f"waveform flow: settings-only mode, "
             f"{len(partitions)} partition(s) materialized")
        return

    # ── VCD 模式：名字伴生对象全集（数量锚 build_meta.def_count——确
    #    定性循环读取）+ 绑定校验（条目级兜底 2026-09-17 timing 同款：
    #    单文件无效 → WFMG::0011 + 跳过该文件〔不进切块链、零条目〕；
    #    仅全部文件无效才 ValueError——输入整体无意义）──
    meta = design_db.load_build_meta()
    names_list = [design_db.read_object(DesignDb.names_obj_name(i))
                  for i in range(meta["def_count"])]
    mapper = ds_make_instance_name_mapper(design, names_list)

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
            f"vcd_files: no binding target resolved in design db — "
            f"all {len(files)} file(s) skipped: {listing}")
    invalid_indexes = sorted(fi for fi, _, _ in invalid)
    for fi, field, value in invalid:
        message("WFMG::0011", 0,
                f"binding target {field} '{value}' not found in design db "
                f"(file '{files[fi]['file_name']}') — file skipped, "
                f"0 entries stored")

    # ── 块解析任务的 design db 枚举锚（本任务执行时刻确定性枚举）：
    #    INST 段号（段表 id_starts 升序，段表锚即全部段对象已写定）──
    index = design_db.read_object(DesignDb.id_map_index_obj_name("INST"))
    inst_seg_indexes = [seg_start >> DesignDb.ID_MAP_SEGMENT_BITS
                        for seg_start in index.id_starts]

    # ── 文件入口任务（每有效文件一个；chunk_size 经 alpha_settings
    #    读回）──
    settings = db.read_object(WaveformDb.ALPHA_SETTINGS_OBJ)
    settings.normalize()
    chunk_size = settings.chunk_size_mb * 1024 * 1024
    invalid_set = set(invalid_indexes)
    valid_file_indexes = [fi for fi in range(len(files))
                          if fi not in invalid_set]
    manifest_keys = [_tmp_key(f"manifest_{fi}")
                     for fi in valid_file_indexes]
    names_count = len(names_list)
    for fi, manifest_key in zip(valid_file_indexes, manifest_keys):
        f = files[fi]
        # 绑定描述以散字段传参（pickle 友好）
        _wf_file_entry_task(db, design_db,
                            file_name=f["file_name"],
                            binding_kind=f["kind"],
                            block_inst=f["block_inst"],
                            block_cell=f["block_cell"],
                            strip_prefix=f["strip_prefix"],
                            file_index=fi,
                            chunk_size=chunk_size,
                            names_count=names_count,
                            inst_seg_indexes=inst_seg_indexes,
                            partitions=partitions,
                            manifest_key=manifest_key)

    # ── 合并链编排任务（体内提交每分区合并 → 汇总；ctx 组装材料与混
    #    合模式 overrides/duration 随参透传）──
    _wf_plan_merge_chain_task(db, design_db, files, valid_file_indexes,
                              partitions, manifest_keys, invalid_indexes,
                              meta_params, names_count, inst_seg_indexes,
                              overrides, duration_fs)

    from log import INFO
    INFO(f"waveform flow: {len(valid_file_indexes)}/{len(files)} file(s) "
         f"valid, {len(partitions)} partition(s)")


# ── 文件入口任务（每文件一个：gzip 嗅探解压 + 切块扫描 + 提交逐块
#    解析）──────────────────────────────────────────────────────────────

@as_task(inputs=lambda db, design_db, file_name, binding_kind, block_inst,
         block_cell, strip_prefix, file_index, chunk_size, names_count,
         inst_seg_indexes, partitions, manifest_key: [])
def _wf_file_entry_task(db, design_db, file_name, binding_kind, block_inst,
                        block_cell, strip_prefix, file_index, chunk_size,
                        names_count, inst_seg_indexes, partitions,
                        manifest_key):
    """文件入口任务：gzip 魔数嗅探（压缩 → db 临时目录解压副本，裁定
    IV-1；明文直通）→ wf_plan_file_chunks 切块扫描（缺失 $timescale 等
    文件形态错在此失败透出，plan §3.1 入口只查存在性不读内容——头关
    键字校验对解压副本执行；档位随切块计划的头部解析结果一次产出，免
    二次整读嗅探）→ var 表写 db temp 对象（裁定 IV-4：逐块任务经 db 按
    引用共享读取，不随任务参数逐块复制）→ 体内提交该文件逐块解析任务
    + 块数/var 键/解压目录写清单对象（合并链编排与汇总的消费锚；解压
    目录删除责任单点在汇总任务）。"""
    import os
    scratch_dir = os.path.join(db.get_db_path(), SCRATCH_DIR_NAME)
    parse_path, decompressed_dir = _decompress_if_gzip(
        file_name, file_index, scratch_dir)
    plan, vars_table = wf_plan_file_chunks(parse_path, chunk_size)
    vars_key = _tmp_key(f"vars_{file_index}")
    db.write_object(vars_key, vars_table, save_to_db=False)
    for cj in range(len(plan.chunks)):
        _wf_convert_chunk_task(db, design_db,
                               file_name=parse_path,
                               chunk=plan.chunks[cj],
                               vars_key=vars_key,
                               timescale_fs=plan.timescale_fs,
                               binding_kind=binding_kind,
                               block_inst=block_inst,
                               block_cell=block_cell,
                               strip_prefix=strip_prefix,
                               file_index=file_index,
                               chunk_index=cj,
                               names_count=names_count,
                               inst_seg_indexes=inst_seg_indexes,
                               partitions=partitions,
                               slice_key=_tmp_key(f"chunk_{file_index}_{cj}"))
    db.write_object(manifest_key,
                    {"chunk_count": len(plan.chunks),
                     "parse_name": parse_path,
                     "vars_key": vars_key,
                     "scratch_dir": decompressed_dir},
                    save_to_db=False)
    from log import INFO
    gz = " (gzip decompressed)" if decompressed_dir else ""
    INFO(f"waveform file {file_index}: planned into "
         f"{len(plan.chunks)} chunk(s) (timescale {plan.timescale_sec}s)"
         f"{gz}")


# ── 逐块解析任务（每块一任务，全并行）────────────────────────────────

@as_task(inputs=lambda db, design_db, file_name, chunk, vars_key,
         timescale_fs, binding_kind, block_inst, block_cell, strip_prefix,
         file_index, chunk_index, names_count, inst_seg_indexes,
         partitions, slice_key: (
    [db.get_full_name(vars_key)]
    + _design_enum_anchors(design_db, names_count, inst_seg_indexes,
                           partitions)))
def _wf_convert_chunk_task(db, design_db, file_name, chunk, vars_key,
                           timescale_fs, binding_kind, block_inst,
                           block_cell, strip_prefix, file_index, chunk_index,
                           names_count, inst_seg_indexes, partitions,
                           slice_key):
    """单块解析：var 表经 db temp 对象按引用读取（裁定 IV-4——任务参数
    只携带键全名，inputs 锚定读取时序）→ worker 端跨 db 直读组装
    EXWFDesignContext（见 _assemble_design_context）→ wf_convert_chunk
    （事件解析 + 名字换算 + 分区路由）→ WFEntrySlice 分片。var 表为反
    序列化产物——消费前 rebuild_indexes 重建运行时索引（索引不序列化）。
    绑定未命中/块不可读 = C++ 侧兜底（空分片 + failed_chunk 计数——
    WFMG::0011 条目级兜底的运行期防御，依赖链保持满足）。"""
    ctx = _assemble_design_context(design_db, names_count,
                                   inst_seg_indexes, partitions)
    binding = EXWFFileBinding()
    binding.set_kind(binding_kind)
    binding.block_inst = block_inst
    binding.block_cell = block_cell
    binding.strip_prefix = strip_prefix
    vars_table = db.read_object(vars_key)
    vars_table.rebuild_indexes()
    slice_obj = wf_convert_chunk(ctx, file_name, chunk, vars_table,
                                 timescale_fs, binding, file_index)
    from log import INFO
    if slice_obj.stats.files:
        fs = slice_obj.stats.files[0]
        bi = f" bi='{block_inst}'" if binding_kind else ""
        INFO(f"chunk parse {file_index}/{chunk_index}: "
             f"kind={binding_kind}{bi} event={fs.event_count} "
             f"hit={fs.hit_count} net_miss={fs.net_name_miss_count} "
             f"pin_miss={fs.inst_pin_miss_count} "
             f"dangling={fs.dangling_net_count} "
             f"unplaced={fs.unplaced_instance_count} "
             f"strip={fs.strip_miss_count} failed={fs.failed_chunk_count} "
             f"parts={list(slice_obj.partition_ids)}")
    else:
        INFO(f"chunk parse {file_index}/{chunk_index}: NO stats files")
    db.write_object(slice_key, slice_obj, save_to_db=False)


# ── 合并链编排任务（清单对象齐备即全部文件入口完成）──────────────────

@as_task(inputs=lambda db, design_db, files, valid_file_indexes,
         partitions, manifest_keys, invalid_indexes, meta_params,
         names_count, inst_seg_indexes, overrides, duration_fs: [
             db.get_full_name(k) for k in manifest_keys])
def _wf_plan_merge_chain_task(db, design_db, files, valid_file_indexes,
                              partitions, manifest_keys, invalid_indexes,
                              meta_params, names_count, inst_seg_indexes,
                              overrides, duration_fs):
    """合并链编排：读全部清单对象推导切片键集与 var 表键集（序 = 文件
    序 × 块序——wf_merge_partition 入参约定，上游接口备注③）+ 收集解压
    目录（清理单点在汇总）→ 提交每分区合并（design_db 句柄随参——合并
    任务体内跨 db 直读组装 ctx；ctx 组装材料 + 混合模式 overrides/
    duration 透传）→ 汇总。切片键集（块数）运行时才知——动态提交
    （「形状运行时才知」场景，dev-rules §3）。"""
    slice_keys = []
    vars_keys = []
    scratch_dirs = []
    for fi, manifest_key in zip(valid_file_indexes, manifest_keys):
        manifest = db.read_object(manifest_key)
        vars_keys.append(manifest["vars_key"])
        if manifest["scratch_dir"]:
            scratch_dirs.append(manifest["scratch_dir"])
        for cj in range(manifest["chunk_count"]):
            slice_keys.append(_tmp_key(f"chunk_{fi}_{cj}"))
    counts_keys = []
    for pid, xp, yp in partitions:
        counts_key = _tmp_key(f"counts_{pid}")
        counts_keys.append(counts_key)
        _wf_merge_partition_task(db, design_db,
                                 slice_keys=slice_keys,
                                 pid=pid,
                                 xp=xp,
                                 yp=yp,
                                 waveform_key=WaveformDb.partition_obj_name(
                                     xp, yp),
                                 activity_key=WaveformDb.activity_obj_name(
                                     xp, yp),
                                 counts_key=counts_key,
                                 names_count=names_count,
                                 inst_seg_indexes=inst_seg_indexes,
                                 partitions=partitions,
                                 overrides=overrides,
                                 duration_fs=duration_fs)

    _wf_merge_summary_task(db, files,
                           slice_keys=slice_keys,
                           counts_keys=counts_keys,
                           manifest_keys=manifest_keys,
                           vars_keys=vars_keys,
                           scratch_dirs=scratch_dirs,
                           invalid_file_indexes=invalid_indexes,
                           meta_params=meta_params,
                           overrides=overrides,
                           summary_key=WaveformDb.SUMMARY_OBJ)
    from log import INFO
    INFO(f"waveform merge chain: {len(slice_keys)} slice(s), "
         f"{len(partitions)} partition(s) submitted")


# ── 每分区一合并任务 ─────────────────────────────────────────────────

@as_task(inputs=lambda db, design_db, slice_keys, pid, xp, yp, waveform_key,
         activity_key, counts_key, names_count, inst_seg_indexes,
         partitions, overrides, duration_fs: (
    [db.get_full_name(k) for k in slice_keys]
    + _design_enum_anchors(design_db, names_count, inst_seg_indexes,
                           partitions)
    + [design_db.get_full_name(
        DesignDb.partition_obj_name(xp, yp, "INSTANCES"))]))
def _wf_merge_partition_task(db, design_db, slice_keys, pid, xp, yp,
                             waveform_key, activity_key, counts_key,
                             names_count, inst_seg_indexes, partitions,
                             overrides, duration_fs):
    """每分区合并：收集各块分片（文件序 × 块序——slice_keys 序）→
    worker 端跨 db 直读组装 EXWFDesignContext（四级回落判定材料，见
    _assemble_design_context）→ wf_merge_partition（波形同身份键按文件
    序/时间序拼接〔场景 a 偏移对齐〕或后文件覆盖〔场景 b/混合〕+ 统计
    直和 + 四级回落 + 绑 waveform_id）→ 混合模式覆盖（裁定 IV-2：
    overrides 非空时落库前 wf_apply_overrides 替换命中条目统计值——换
    算同 settings-only、waveform_id 引用保留、default 不经此口；实例枚
    举 = design db 本分区 INSTANCES 对象）→ PART_{xp}_{yp}.WAVEFORM +
    PART_{xp}_{yp}.ACTIVITY 正式对象唯一写定（overlap/anomaly/波形数/
    覆盖条目计数 temp——WFMG::0006/0007 由汇总聚合，覆盖条目未解析的
    权威判定同样在汇总聚合）。"""
    slices = [db.read_object(k) for k in slice_keys]
    ctx = _assemble_design_context(design_db, names_count,
                                   inst_seg_indexes, partitions)
    waveform, activity, overlap, anomaly = wf_merge_partition(ctx, slices,
                                                              pid)
    override_entries = 0
    applied_names = []
    if overrides:
        instances = design_db.read_object(
            DesignDb.partition_obj_name(xp, yp, "INSTANCES"))
        inst_ids = sorted(instances.ids())
        override_entries, applied_names = wf_apply_overrides(
            ctx, pid, inst_ids, activity, _override_objects(overrides),
            duration_fs)
    db.write_object(waveform_key, waveform, save_to_db=True)
    db.write_object(activity_key, activity, save_to_db=True)
    db.write_object(counts_key,
                    {"overlap_override_count": overlap,
                     "time_anomaly_count": anomaly,
                     "waveform_count": waveform.waveform_count,
                     "override_entry_count": override_entries,
                     "applied_names": list(applied_names)},
                    save_to_db=False)


# ── 汇总任务（summary + 消息族 + fatal 判定 + 运行时键自清理）──────────

@as_task(inputs=lambda db, files, slice_keys, counts_keys, manifest_keys,
         vars_keys, scratch_dirs, invalid_file_indexes, meta_params,
         overrides, summary_key: (
    [db.get_full_name(k) for k in slice_keys + counts_keys]
    + [db.get_full_name(k) for k in manifest_keys + vars_keys]))
def _wf_merge_summary_task(db, files, slice_keys, counts_keys,
                           manifest_keys, vars_keys, scratch_dirs,
                           invalid_file_indexes, meta_params, overrides,
                           summary_key):
    """汇总：summary 聚合 + WFMG 消息族一次汇总透出 + 全部文件失败
    fatal（WFMG::0009）+ 混合模式覆盖条目聚合（override_entry_count 入
    summary；未解析覆盖条目按全分区聚合口径一次透出——全部分区都未命
    中的条目才计 WFMG::0001，同 settings-only 聚合裁定）+ 运行时规模
    临时对象与 gzip 解压副本目录自清理（切片/var 表/分区计数/清单/解
    压目录——本任务为全链最后读者，清理责任单点：counts ⟸ 分区合并、
    切片 ⟸ 块解析、var 表 ⟸ 块解析、清单 ⟸ 文件入口均经本任务 inputs
    传递依赖先行完成）。"""
    import shutil
    from log import INFO
    invalid_set = set(invalid_file_indexes)
    slices = [db.read_object(k) for k in slice_keys]
    counts = [db.read_object(k) for k in counts_keys]
    wave_count = sum(c["waveform_count"] for c in counts)
    overlap = sum(c["overlap_override_count"] for c in counts)
    anomaly = sum(c["time_anomaly_count"] for c in counts)
    # 混合模式覆盖聚合（裁定 IV-2）：覆盖条目计数直和 + 命中条目名并集
    # （分区侧事实源）——overrides 为空（纯 VCD/settings-only）时全零
    override_entries = sum(c["override_entry_count"] for c in counts)
    applied = set()
    for c in counts:
        applied.update(c["applied_names"])
    unresolved_overrides = [ov["name"] for ov in overrides
                            if ov["name"] not in applied]
    # 逐文件块数表按键配对（清单 parse_name = 块任务实际解析路径，与
    # summary.files.source_file 同源；不用 zip 位置配对——文件缺逐文件
    # 表条目时 zip 截断会静默错位）
    chunk_count_by_name = {}
    for k in manifest_keys:
        manifest = db.read_object(k)
        chunk_count_by_name[manifest["parse_name"]] = \
            manifest["chunk_count"]
    valid_count = len(files) - len(invalid_set)

    meta = EXWFSettingsMeta()
    meta.default_toggle_rate = meta_params["default_toggle_rate"]
    meta.default_static_probability = \
        meta_params["default_static_probability"]
    meta.duration_sec = meta_params["duration_sec"]
    meta.x_z_handling = meta_params["x_z_handling"]

    summary = wf_merge_summary([s.stats for s in slices], wave_count,
                               overlap, anomaly, len(invalid_set),
                               override_entries, meta)

    # fatal 判定（plan §6 范式 (a)）：全部文件全部块失败（逐文件表条目
    # 按解析路径显式查块数；查无键 = 无块记录，按 0 块保守计入 failed）
    failed = []
    for f in summary.files:
        n = chunk_count_by_name.get(f.source_file, 0)
        if f.failed_chunk_count >= n:
            failed.append((f, n))
    if failed and len(failed) == valid_count:
        listing = ", ".join(f.source_file for f, _ in failed)
        fatal_message(
            "WFMG::0009", 0,
            f"all {valid_count} vcd file(s) failed to parse, downstream "
            f"data cannot be produced: {listing}")
        # fatal_message 不返回（_exit(80) + master 联动）

    if summary.total_hit_count == 0:
        message("WFMG::0004", 0,
                f"all {valid_count} vcd file(s) parsed but 0 valid "
                f"entries matched the design db (empty result)")
    if summary.net_name_miss_count:
        message("WFMG::0001", 0,
                f"{summary.net_name_miss_count} net name(s) not matched in "
                f"design db (skipped)")
    if unresolved_overrides:
        # 混合模式覆盖条目未解析（WFMG::0001 家族聚合口径——全部分区
        # 都未命中的条目才计；文案与 settings-only 同族）
        message("WFMG::0001", 0,
                f"{len(unresolved_overrides)} settings activity override "
                f"entr(y/ies) unresolved against design db (skipped)")
    if summary.inst_pin_miss_count:
        message("WFMG::0002", 0,
                f"{summary.inst_pin_miss_count} instance/pin name(s) not "
                f"matched (skipped)")
    if summary.dangling_net_count:
        message("WFMG::0003", 0,
                f"{summary.dangling_net_count} net entr(y/ies) without a "
                f"connection or dangling (skipped)")
    if overlap:
        message("WFMG::0006", 0,
                f"{overlap} waveform identit(y/ies) overridden by a later "
                f"file (last file wins)")
    if anomaly:
        message("WFMG::0007", 0,
                f"{anomaly} event(s) non-monotonic after cross-file time "
                f"alignment (observed)")
    if summary.unplaced_instance_count:
        message("WFMG::0008", 0,
                f"{summary.unplaced_instance_count} entr(y/ies) skipped for "
                f"unplaced instances (no primary partition copy)")
    if summary.strip_miss_count:
        message("WFMG::0010", 0,
                f"{summary.strip_miss_count} entr(y/ies) skipped: "
                f"strip_prefix not matched (including fully-stripped names)")
    if summary.unsupported_count:
        message("WFMG::0012", 0,
                f"{summary.unsupported_count} unsupported record(s) "
                f"observed (real/event value rows; skipped)")
    if summary.width_mismatch_count:
        message("WFMG::0013", 0,
                f"{summary.width_mismatch_count} vector width mismatch "
                f"observ(ations) (design pin set vs var bits; not fatal)")
    if failed and len(failed) < valid_count:
        # 部分文件失败：空分片已照常产出，失败清单可追溯
        listing = ", ".join(f.source_file for f, _ in failed)
        INFO(f"waveform flow: {len(failed)}/{valid_count} file(s) fully "
             f"failed to parse: {listing}")

    # 运行时规模临时对象自清理（键均为本任务 inputs，必然存在——严格
    # remove，缺失即 KeyError 暴露清理责任错位）+ gzip 解压副本目录
    # 删除（freeze 清理链单点；目录缺失容忍——明文库 scratch_dirs 为空）
    for key in slice_keys + counts_keys + manifest_keys + vars_keys:
        db.remove_object(key)
    for d in scratch_dirs:
        shutil.rmtree(d, ignore_errors=True)
    db.write_object(summary_key, summary, save_to_db=True)
    INFO(f"waveform summary: {summary.total_hit_count} hit(s), "
         f"{summary.total_event_count} event(s), "
         f"{summary.waveform_count} waveform(s), {valid_count} file(s) "
         f"({len(invalid_set)} skipped for invalid binding), "
         f"duration {summary.total_duration_fs} fs, "
         f"{override_entries} override entr(y/ies) applied")


# ── settings-only 模式：每分区一物化任务 ─────────────────────────────

@as_task(inputs=lambda db, design_db, pid, xp, yp, names_count,
         partitions, overrides, duration_fs, counts_key: (
    [design_db.get_full_name(DesignDb.DESIGN_OBJ),
     design_db.get_full_name(DesignDb.PG_NETS_OBJ)]
    + [design_db.get_full_name(DesignDb.names_obj_name(j))
       for j in range(names_count)]
    + [design_db.get_full_name(
        DesignDb.partition_obj_name(x, y, "NETS")) for _, x, y in partitions]
    + [design_db.get_full_name(
        DesignDb.partition_obj_name(xp, yp, "INSTANCES"))]))
def _wf_materialize_task(db, design_db, pid, xp, yp, names_count,
                         partitions, overrides, duration_fs, counts_key):
    """settings-only 物化（plan §6）：worker 端组装 EXWFDesignContext
    （design + 块名字全集〔overrides 的 inst pin 层级路径名经 inst
    mapper 直配〕+ pg_nets + 全部分区 NETS——net 名覆盖经形态①推导展
    开需查网连接表，任一分区副本都有该网完整连接表）→ 实例枚举 =
    design db 本分区 INSTANCES 对象（升序——物化容器序列化序确定性）→
    wf_empty_partition_waveform（空波形容器照常产出——消费接口恒定）+
    wf_materialize_activity（overrides 换算物化；波形引用全哨兵）→
    双对象唯一写定。物化成功条目名集 temp（settings 汇总聚合一次透出
    ——全部分区都未物化的条目才计未解析）。"""
    ctx = EXWFDesignContext()
    ctx.set_design(design_db.read_object(DesignDb.DESIGN_OBJ))
    ctx.set_pg_nets(design_db.read_object(DesignDb.PG_NETS_OBJ))
    # names_count 确定性循环（规模随参数传递，禁键集试探——§19 批次）
    for j in range(names_count):
        ctx.add_block_names(design_db.read_object(
            DesignDb.names_obj_name(j)))
    for _, x, y in partitions:
        ctx.add_partition_nets(design_db.read_object(
            DesignDb.partition_obj_name(x, y, "NETS")))
    instances = design_db.read_object(
        DesignDb.partition_obj_name(xp, yp, "INSTANCES"))
    inst_ids = sorted(instances.ids())
    waveform = wf_empty_partition_waveform(pid)
    ovs = _override_objects(overrides)
    activity, unresolved, resolved_names = wf_materialize_activity(
        ctx, pid, inst_ids, ovs, duration_fs)
    db.write_object(WaveformDb.partition_obj_name(xp, yp), waveform,
                    save_to_db=True)
    db.write_object(WaveformDb.activity_obj_name(xp, yp), activity,
                    save_to_db=True)
    # 分区侧事实源 = 本区物化成功的条目名（未解析的权威判定在汇总任务
    # 聚合——全部分区都未物化的条目才计，避免逐区虚计）
    db.write_object(counts_key, {"resolved_names": list(resolved_names)},
                    save_to_db=False)
    from log import INFO
    INFO(f"waveform materialize partition ({xp},{yp}): "
         f"{activity.size} instance(s), {len(resolved_names)}/{len(ovs)} "
         f"override(s) resolved in this partition")


# ── settings-only 汇总任务（summary = settings 元数据）────────────────

@as_task(inputs=lambda db, overrides, meta_params, counts_keys: (
    [db.get_full_name(k) for k in counts_keys]))
def _wf_settings_summary_task(db, overrides, meta_params, counts_keys):
    """settings-only 汇总：settings 元数据 → summary 唯一写定（波形容
    器空、统计容器 overrides 物化——全库计数为零）；overrides 未解析条
    目按聚合判定一次透出（全部分区都未物化的条目才计——逐区各自跑全
    量 overrides，实例不属本区的条目在本区未命中属正常形态，按区求和
    会虚计）+ counts temp 自清理（清理责任单点）。"""
    from log import INFO
    resolved = set()
    for k in counts_keys:
        resolved.update(db.read_object(k)["resolved_names"])
    unresolved = sum(1 for ov in overrides if ov["name"] not in resolved)
    meta = EXWFSettingsMeta()
    meta.default_toggle_rate = meta_params["default_toggle_rate"]
    meta.default_static_probability = \
        meta_params["default_static_probability"]
    meta.duration_sec = meta_params["duration_sec"]
    meta.x_z_handling = meta_params["x_z_handling"]
    summary = wf_merge_summary([], 0, 0, 0, 0, 0, meta)
    if unresolved:
        message("WFMG::0001", 0,
                f"{unresolved} settings activity override entr(y/ies) "
                f"unresolved against design db (skipped)")
    for key in counts_keys:
        db.remove_object(key)
    db.write_object(WaveformDb.SUMMARY_OBJ, summary, save_to_db=True)
    INFO(f"waveform summary: settings-only mode "
         f"(default toggle_rate={meta.default_toggle_rate}, "
         f"static_probability={meta.default_static_probability}, "
         f"duration={meta.duration_sec}s, x_z={meta.x_z_handling})")


# ── freeze（build_waveform_db 第③段顶层提交）────────────────────────

@as_task(inputs=lambda db: [
    db.get_full_name(WaveformDb.SUMMARY_OBJ)])
def _freeze_waveform_task(db):
    """freeze：依赖固定标记 summary（蕴含链：summary ⟸ 汇总 inputs 含
    全部切片 + 各分区计数 + 清单 ⟹ 分区 WAVEFORM/ACTIVITY 对象先写定
    ——freeze 不可能早于任一正式产物；settings-only 模式 summary ⟸
    counts ⟸ 物化双对象同理）。运行时规模临时对象由汇总任务自清理
    （§19 清理责任单点）。"""
    db.freeze()
