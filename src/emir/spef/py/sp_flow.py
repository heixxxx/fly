"""build_spef_db flow 任务链（直接任务链，同 timing/waveform 先例全同
构；plan docs/emir/spef-db-plan.md §6）。

三段式（dev-rules §3「建库 API 流程标准」）：入口 sp_db.build_spef_db
①轻量参数预处理（schema 校验 + 文件存在性 + 建库 + alpha_settings 写入
——不读文件内容）②提交唯一根任务 ③顶层提交 freeze。master 提交 O(1)
个任务，与输入规模无关（§20 编排判据）。

根任务 _spef_flow_task 体内目录（依赖系统等 design db 必要对象——
plan §3.1 入口等待语义，不等待 design db freeze）：
  绑定校验（条目级兜底：单文件无效跳过 + SPMG::0009；全无效
     任务内 ValueError）
  → 作用域编号（裁定 8 判定材料——assign_scope_indexes 纯函数）+
    有效文件序（从 0 递增，同时是贡献来源位图的位下标）
  → 每有效文件一个文件入口任务 _sp_file_entry_task（gzip 魔数嗅探
     → 解压副本进常规链〔waveform 裁定 IV-1 同款〕+ 切块扫描
     sp_plan_file_chunks〔文件头随切块计划一次产出，免二次整读〕+
     文件头写 db temp 对象〔逐块任务按引用共享〕+ 体内提交该文件逐
     块解析任务 + 块数/文件头键/原始路径/解压目录写清单对象——文件
     内容 IO 全在 worker 并行，文件级错误由本任务失败透出）
  → 合并链编排任务 _sp_plan_merge_chain_task（依赖全部清单对象——齐
     备即全部文件入口完成；体内提交：每分区合并 → 汇总）

块解析/合并任务跨 db 直读 design db（§21「db 数据所有权：禁跨 db 数据
搬运」）：inputs 锚 design db 对象全名，任务体内经 design_db 句柄直读
组装 EXSPDesignContext。文件头/块对象（temp）落 spef db 按引用共享。

gzip 压缩 SPEF（waveform 裁定 IV-1 同款）：文件入口任务读头两字节检
测魔数（0x1f 0x8b）→ Python 标准库 gzip 流式解压到 db 临时目录
（<db_path>/sp_scratch/，副本名带文件序防同名冲突）→ 切块/块任务按
解压副本普通处理；明文文件零开销直通。解压目录由汇总任务删除（freeze
清理链上的单点清理——汇总为全部块任务后的最后读者；建库失败残留随 db
目录生命周期归零）。

每分区合并任务（第 H 节第 4 条覆盖率材料）：组装 EXSPDesignContext 后
另读本区 INST_CONNECTIONS 对象注入（键 = 本区 pid）——覆盖率分母 =
本区实例连接 + 已布线信号网 id 集合（全部 NETS 对象），不是本区 NETS
（NETS 按布线几何归属分区而寄生条目按连接位实例归属分区，两者可以不
同）。合并分片读取顺序 = 「文件序 × 块序」（slice_keys 序——「同作用
域保留首份」依赖它）。

freeze _freeze_spef_task（build_spef_db 第③段顶层提交）：依赖固定标记
summary（蕴含链：summary ⟸ 汇总 inputs 含全部切片/清单/文件头/分区计
数 ⟹ 分区正式对象先写定——freeze 不可能早于任一正式产物）。运行时规
模临时对象（文件头/切片/分区计数/清单）与解压副本目录由汇总任务自清
理——清理责任单点（§19 批次口径）。
"""

from fly import as_task, fatal_message, message

from emir.design import DesignDb, ds_make_instance_name_mapper

from .sp_db import SpefDb
from .sp_export import (
    EXSPDesignContext,
    EXSPEntrySlice,
    EXSPFileBinding,
    sp_convert_chunk,
    sp_merge_partition,
    sp_merge_summary,
    sp_parse_chunk_text,
    sp_plan_file_chunks,
    sp_read_byte_range,
)


def _tmp_key(name: str) -> str:
    """编排临时对象键（固定名 __spg__{name}——同 timing/waveform 裁定：
    一 db 一 flow，task 重放参数原样同键覆盖幂等。键内的内容索引
    （chunk_{fi}_{cj}/header_{fi}/manifest_{fi}/counts_{pid}）是业务寻
    址保留）。"""
    return f"__spg__{name}"


SCRATCH_DIR_NAME = "sp_scratch"
# gzip 魔数（waveform 裁定 IV-1 同款：文件入口任务读头两字节嗅探——
# IEEE 1952 gzip 固定首两字节）
GZIP_MAGIC = b"\x1f\x8b"
# 解压流式拷贝块大小（1 MiB——大 SPEF 常量内存）
_DECOMP_CHUNK_BYTES = 1024 * 1024


def assign_scope_indexes(files):
    """作用域编号（裁定 8 的判定材料）：全部纯路径文件共用同一个作用
    域；绑同一个块实例的文件共用一个作用域；绑同一个块定义的文件共用
    一个作用域；三者互不相同。返回逐文件作用域序号列表（编号 = 作用域
    键的首次出现序）。独立纯函数——便于单测钉住判定材料。"""
    scopes = {}
    result = []
    for f in files:
        if f["kind"] == 0:
            key = (0, "")
        elif f["kind"] == 1:
            key = (1, f["block_inst"])
        else:
            key = (2, f["block_cell"])
        if key not in scopes:
            scopes[key] = len(scopes)
        result.append(scopes[key])
    return result


def _decompress_if_gzip(file_name, file_index, scratch_dir):
    """gzip 魔数嗅探 → 解压副本（waveform 裁定 IV-1 同款）；明文文件
    零开销直通。

    返回 (切块/块任务实际读的路径, 解压目录 or "")。副本名带文件序——
    多文件同名 basename 不冲突；流式解压常量内存。副本落 db 目录下
    SCRATCH_DIR_NAME（spef db 临时目录），删除责任单点在汇总任务。"""
    with open(file_name, "rb") as f:
        magic = f.read(2)
    if magic != GZIP_MAGIC:
        return file_name, ""
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
    """worker 端跨 db 直读组装 EXSPDesignContext（共享注入零拷贝——
    §21 禁跨 db 数据搬运，spef db 无 design 数据副本；names_count/
    段号/分区表由根任务读 design db 得出后透传）。覆盖率材料的分区实
    例连接对象由每分区合并任务另行读本区对象注入（见
    _sp_merge_partition_task）。"""
    ctx = EXSPDesignContext()
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

@as_task(inputs=lambda db, design_db, files, chunk_size, value_index,
         value_index_given: [
    design_db.get_full_name(DesignDb.DESIGN_OBJ),
    design_db.get_full_name(DesignDb.GLOBAL_DENSITY_OBJ),
    design_db.get_full_name(DesignDb.PG_NETS_OBJ),
    design_db.get_full_name(DesignDb.id_map_index_obj_name("INST")),
    design_db.get_full_name(DesignDb.BUILD_META_OBJ),
    db.get_full_name(SpefDb.ALPHA_SETTINGS_OBJ),
])
def _spef_flow_task(db, design_db, files, chunk_size, value_index,
                    value_index_given):
    """绑定校验 → 文件入口任务族 + 合并链编排任务；体内目录见模块
    docstring。锚集证明见 design-db 文档——DESIGN_OBJ 锚即伴生对象全
    集，GLOBAL_DENSITY 锚即分区表，INST 段表锚即分区正式对象。design
    db 数据零搬运（§21）。"""
    design = design_db.read_object(DesignDb.DESIGN_OBJ)
    # 分区表（行主序，DESIGN_OBJ 锚即分区表——§21 跨 db 直读，无快照搬运）
    partitions = []
    for p in range(design.partition_count):
        part = design.partition_at(p)
        partitions.append((part.partition_id, part.xp, part.yp))

    # ── 名字伴生对象全集（数量锚 build_meta.def_count——确定性循环
    #    读取）+ 绑定校验（条目级兜底 timing/waveform 同款：单文件无
    #    效 → SPMG::0009 + 跳过该文件〔不进切块链、零条目〕；仅全部
    #    文件无效才 ValueError——输入整体无意义）──
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
    # 逐文件先发 SPMG::0009 再判全部未命中——同一输入错误在两种情况
    # 下表现一致，message.log 里都有解释（全部未命中随后任务内抛
    # ValueError 终止建库）
    for fi, field, value in invalid:
        message("SPMG::0009", 0,
                f"binding target {field} '{value}' not found in design db "
                f"(file '{files[fi]['file_name']}') — file skipped, "
                f"0 entries stored")
    if len(invalid) == len(files):
        listing = "; ".join(
            f"{field} '{value}' (file '{files[fi]['file_name']}')"
            for fi, field, value in invalid)
        raise ValueError(
            f"spef_files: no binding target resolved in design db — "
            f"all {len(files)} file(s) skipped: {listing}")
    invalid_indexes = sorted(fi for fi, _, _ in invalid)

    # ── 作用域编号（裁定 8 判定材料——纯函数 assign_scope_indexes）
    #    + 有效文件序（按输入顺序从 0 递增——贡献来源位图的位下标）──
    scopes = assign_scope_indexes(files)
    invalid_set = set(invalid_indexes)
    # (原输入序, 重编文件序, 作用域序) 三元组——编排提交按此序
    valid_entries = []
    for fi, f in enumerate(files):
        if fi in invalid_set:
            continue
        valid_entries.append((fi, len(valid_entries), scopes[fi]))

    # ── 块解析任务的 design db 枚举锚（本任务执行时刻确定性枚举）：
    #    INST 段号（段表 id_starts 升序，段表锚即全部段对象已写定）──
    index = design_db.read_object(DesignDb.id_map_index_obj_name("INST"))
    inst_seg_indexes = [seg_start >> DesignDb.ID_MAP_SEGMENT_BITS
                        for seg_start in index.id_starts]

    # ── 文件入口任务（每有效文件一个）──
    manifest_keys = [_tmp_key(f"manifest_{file_index}")
                     for _, file_index, _ in valid_entries]
    names_count = len(names_list)
    for (orig_fi, file_index, scope_index), manifest_key in zip(
            valid_entries, manifest_keys):
        f = files[orig_fi]
        # 绑定描述以散字段传参（pickle 友好）
        _sp_file_entry_task(db, design_db,
                            file_name=f["file_name"],
                            binding_kind=f["kind"],
                            block_inst=f["block_inst"],
                            block_cell=f["block_cell"],
                            strip_prefix=f["strip_prefix"],
                            scope_index=scope_index,
                            file_index=file_index,
                            chunk_size=chunk_size,
                            value_index=value_index,
                            value_index_given=value_index_given,
                            names_count=names_count,
                            inst_seg_indexes=inst_seg_indexes,
                            partitions=partitions,
                            manifest_key=manifest_key)

    # ── 合并链编排任务（体内提交每分区合并 → 汇总；ctx 组装材料随参
    #    透传）──
    _sp_plan_merge_chain_task(db, design_db, files,
                              [entry[0] for entry in valid_entries],
                              [entry[2] for entry in valid_entries],
                              partitions, manifest_keys, invalid_indexes,
                              names_count, inst_seg_indexes)

    from log import INFO
    INFO(f"spef flow: {len(valid_entries)}/{len(files)} file(s) valid, "
         f"{len(partitions)} partition(s)")


# ── 文件入口任务（每文件一个：gzip 嗅探解压 + 切块扫描 + 提交逐块
#    解析）──────────────────────────────────────────────────────────────

@as_task(inputs=lambda db, design_db, file_name, binding_kind, block_inst,
         block_cell, strip_prefix, scope_index, file_index, chunk_size,
         value_index, value_index_given, names_count, inst_seg_indexes,
         partitions, manifest_key: [])
def _sp_file_entry_task(db, design_db, file_name, binding_kind, block_inst,
                        block_cell, strip_prefix, scope_index, file_index,
                        chunk_size, value_index, value_index_given,
                        names_count, inst_seg_indexes, partitions,
                        manifest_key):
    """文件入口任务：gzip 魔数嗅探（压缩 → db 临时目录解压副本；明文
    直通）→ sp_plan_file_chunks 切块扫描（缺失单位头等文件形态错在此
    失败透出，plan §3.1 入口只查存在性不读内容——文件头随切块计划一次
    产出，免二次整读）→ 文件头写 db temp 对象（逐块任务按引用共享）
    → 体内提交该文件逐块解析任务 + 块数/文件头键/原始路径/解压目录写
    清单对象（合并链编排与汇总的消费锚；解压目录删除责任单点在汇总
    任务）。source_name（统计的逐文件归并键）= 用户输入路径——解压副
    本路径不进 summary（可读性）。"""
    import os
    scratch_dir = os.path.join(db.get_db_path(), SCRATCH_DIR_NAME)
    parse_path, decompressed_dir = _decompress_if_gzip(
        file_name, file_index, scratch_dir)
    plan, header = sp_plan_file_chunks(parse_path, chunk_size)
    header_key = _tmp_key(f"header_{file_index}")
    db.write_object(header_key, header, save_to_db=False)
    for cj in range(len(plan.chunks)):
        _sp_convert_chunk_task(db, design_db,
                               parse_path=parse_path,
                               chunk=plan.chunks[cj],
                               header_key=header_key,
                               binding_kind=binding_kind,
                               block_inst=block_inst,
                               block_cell=block_cell,
                               strip_prefix=strip_prefix,
                               value_index=value_index,
                               value_index_given=value_index_given,
                               scope_index=scope_index,
                               file_index=file_index,
                               chunk_index=cj,
                               source_name=file_name,
                               names_count=names_count,
                               inst_seg_indexes=inst_seg_indexes,
                               partitions=partitions,
                               slice_key=_tmp_key(
                                   f"chunk_{file_index}_{cj}"))
    db.write_object(manifest_key,
                    {"chunk_count": len(plan.chunks),
                     "header_key": header_key,
                     "source_name": file_name,
                     "scratch_dir": decompressed_dir},
                    save_to_db=False)
    from log import INFO
    gz = " (gzip decompressed)" if decompressed_dir else ""
    INFO(f"spef file {file_index}: planned into "
         f"{len(plan.chunks)} chunk(s){gz}")


# ── 逐块解析任务（每块一任务，全并行）────────────────────────────────

@as_task(inputs=lambda db, design_db, parse_path, chunk, header_key,
         binding_kind, block_inst, block_cell, strip_prefix, value_index,
         value_index_given, scope_index, file_index, chunk_index,
         source_name, names_count, inst_seg_indexes, partitions,
         slice_key: (
    [db.get_full_name(header_key)]
    + _design_enum_anchors(design_db, names_count, inst_seg_indexes,
                           partitions)))
def _sp_convert_chunk_task(db, design_db, parse_path, chunk, header_key,
                           binding_kind, block_inst, block_cell,
                           strip_prefix, value_index, value_index_given,
                           scope_index, file_index, chunk_index,
                           source_name, names_count, inst_seg_indexes,
                           partitions, slice_key):
    """单块解析：文件头经 db temp 对象按引用读取（任务参数只携带键全
    名，inputs 锚定读取时序）→ 字节区间读文本（sp_read_byte_range——
    切块计划的字节区间）→ 逐段解析（sp_parse_chunk_text；段级破损跳过
    + 计数不抛出）→ worker 端跨 db 直读组装 EXSPDesignContext（见
    _assemble_design_context）→ sp_convert_chunk（名字换算 + 分区路
    由）→ SPEntrySlice 分片。绑定未命中 = C++ 侧兜底（空分片 +
    failed_chunk 计数——SPMG::0009 条目级兜底的运行期防御，依赖链保持
    满足）。"""
    ctx = _assemble_design_context(design_db, names_count,
                                   inst_seg_indexes, partitions)
    binding = EXSPFileBinding()
    binding.set_kind(binding_kind)
    binding.block_inst = block_inst
    binding.block_cell = block_cell
    binding.strip_prefix = strip_prefix
    header = db.read_object(header_key)
    text = sp_read_byte_range(parse_path, chunk.start, chunk.end)
    parsed = sp_parse_chunk_text(text, header, value_index,
                                 value_index_given, source_name)
    slice_obj = sp_convert_chunk(ctx, header, parsed, file_index,
                                 scope_index, binding, source_name)
    from log import INFO
    if slice_obj.stats.files:
        fs = slice_obj.stats.files[0]
        INFO(f"chunk parse {file_index}/{chunk_index}: "
             f"net={fs.net_count} hit={fs.hit_count} "
             f"net_miss={fs.net_name_miss_count} pg={fs.pg_net_skip_count} "
             f"unrouted={fs.unrouted_net_count} broken={fs.broken_net_count} "
             f"strip={fs.strip_miss_count} failed={fs.failed_chunk_count} "
             f"parts={list(slice_obj.partition_ids)}")
    db.write_object(slice_key, slice_obj, save_to_db=False)


# ── 合并链编排任务（清单对象齐备即全部文件入口完成）──────────────────

@as_task(inputs=lambda db, design_db, files, valid_orig_indexes,
         valid_scopes, partitions, manifest_keys, invalid_indexes,
         names_count, inst_seg_indexes: [
             db.get_full_name(k) for k in manifest_keys])
def _sp_plan_merge_chain_task(db, design_db, files, valid_orig_indexes,
                              valid_scopes, partitions, manifest_keys,
                              invalid_indexes, names_count,
                              inst_seg_indexes):
    """合并链编排：读全部清单对象推导切片键集与文件头键集（序 = 文件
    序 × 块序——sp_merge_partition 入参约定，「同作用域保留首份」依此
    序；manifest_keys 序 = 有效文件重编号序 0..k-1，切片键按 (文件序,
    块序) 展开）+ 收集解压目录（清理单点在汇总）→ 提交每分区合并
    （design_db 句柄随参——合并任务体内跨 db 直读组装 ctx）→ 汇总。
    切片键集（块数）运行时才知——动态提交（「形状运行时才知」场景，
    dev-rules §3，注释一行声明）。"""
    slice_keys = []
    header_keys = []
    scratch_dirs = []
    for fi, manifest_key in enumerate(manifest_keys):
        manifest = db.read_object(manifest_key)
        header_keys.append(manifest["header_key"])
        if manifest["scratch_dir"]:
            scratch_dirs.append(manifest["scratch_dir"])
        for cj in range(manifest["chunk_count"]):
            slice_keys.append(_tmp_key(f"chunk_{fi}_{cj}"))
    counts_keys = []
    for pid, xp, yp in partitions:
        counts_key = _tmp_key(f"counts_{pid}")
        counts_keys.append(counts_key)
        _sp_merge_partition_task(db, design_db,
                                 slice_keys=slice_keys,
                                 pid=pid,
                                 xp=xp,
                                 yp=yp,
                                 net_load_key=SpefDb.partition_obj_name(
                                     xp, yp),
                                 counts_key=counts_key,
                                 names_count=names_count,
                                 inst_seg_indexes=inst_seg_indexes,
                                 partitions=partitions)

    _sp_merge_summary_task(db, files, valid_orig_indexes,
                           slice_keys=slice_keys,
                           counts_keys=counts_keys,
                           manifest_keys=manifest_keys,
                           header_keys=header_keys,
                           scratch_dirs=scratch_dirs,
                           invalid_file_indexes=invalid_indexes,
                           summary_key=SpefDb.SUMMARY_OBJ)
    from log import INFO
    INFO(f"spef merge chain: {len(slice_keys)} slice(s), "
         f"{len(partitions)} partition(s) submitted")


# ── 每分区一合并任务 ─────────────────────────────────────────────────

@as_task(inputs=lambda db, design_db, slice_keys, pid, xp, yp,
         net_load_key, counts_key, names_count, inst_seg_indexes,
         partitions: (
    [db.get_full_name(k) for k in slice_keys]
    + _design_enum_anchors(design_db, names_count, inst_seg_indexes,
                           partitions)
    + [design_db.get_full_name(
        DesignDb.partition_obj_name(xp, yp, "INST_CONNECTIONS"))]))
def _sp_merge_partition_task(db, design_db, slice_keys, pid, xp, yp,
                             net_load_key, counts_key, names_count,
                             inst_seg_indexes, partitions):
    """每分区合并：收集各块分片（文件序 × 块序——slice_keys 序，「同
    作用域保留首份」依赖此序）→ worker 端跨 db 直读组装 EXSPDesign-
    Context（覆盖率判定材料，见 _assemble_design_context）+ 本区实
    例连接对象注入（PART_{xp}_{yp}.INST_CONNECTIONS——覆盖率统计材料，
    第 H 节第 4 条；取法同 waveform 每分区合并任务读本区 INSTANCES）
    → sp_merge_partition（按来源作用域合并 + 覆盖率统计 + 行首值不一
    致观测）→ PART_{xp}_{yp}.NET_LOAD 正式对象唯一写定 + 合并统计
    temp（SPMG::0007/0013 由汇总聚合）。"""
    slices = [db.read_object(k) for k in slice_keys]
    ctx = _assemble_design_context(design_db, names_count,
                                   inst_seg_indexes, partitions)
    from emir.design import DesignDb
    ctx.add_partition_inst_connections(
        pid, design_db.read_object(
            DesignDb.partition_obj_name(xp, yp, "INST_CONNECTIONS")))
    obj, stats = sp_merge_partition(ctx, slices, pid)
    db.write_object(net_load_key, obj, save_to_db=True)
    db.write_object(counts_key, stats, save_to_db=False)
    from log import INFO
    INFO(f"spef merge partition ({xp},{yp}): {obj.size} net(s), "
         f"routed={stats.routed_signal_net_count} "
         f"covered={stats.covered_net_count} "
         f"uncovered={stats.uncovered_net_count}")


# ── 汇总任务（summary + 消息族 + fatal 判定 + 切块数 + 运行时键自清理）──

@as_task(inputs=lambda db, files, valid_orig_indexes, slice_keys,
         counts_keys, manifest_keys, header_keys, scratch_dirs,
         invalid_file_indexes, summary_key: (
    [db.get_full_name(k) for k in slice_keys + counts_keys]
    + [db.get_full_name(k) for k in manifest_keys + header_keys]))
def _sp_merge_summary_task(db, files, valid_orig_indexes, slice_keys,
                           counts_keys, manifest_keys, header_keys,
                           scratch_dirs, invalid_file_indexes, summary_key):
    """汇总：summary 聚合 + 逐文件切块数按清单填入（第 H 节第 5 条——
    多块路径的可观测量）+ SPMG 消息族一次汇总透出（每个族只在计数大
    于 0 时发一次）+ 全部文件失败 fatal（SPMG::0008，范式 (a) 防御面
    ——绑定校验后正常情况下到不了）+ 运行时规模临时对象与 gzip 解压副
    本目录自清理（文件头/切片/分区计数/清单/解压目录——本任务为全链
    最后读者，清理责任单点）。"""
    import shutil
    from log import INFO
    invalid_set = set(invalid_file_indexes)
    deltas = [db.read_object(k).stats for k in slice_keys]
    merge_stats = [db.read_object(k) for k in counts_keys]

    summary = sp_merge_summary(deltas, len(invalid_set), merge_stats)

    # 逐文件切块数按文件清单填入（第 H 节第 5 条）：清单 source_name =
    # 块任务统计的归并键（summary.files.source_file 同源；按名显式配
    # 对——不用 zip 位置配对，文件缺逐文件表条目时 zip 截断会静默错位）
    chunk_count_by_name = {}
    for k in manifest_keys:
        manifest = db.read_object(k)
        chunk_count_by_name[manifest["source_name"]] = \
            manifest["chunk_count"]
    valid_count = len(files) - len(invalid_set)
    for f in summary.files:
        f.chunk_count = chunk_count_by_name.get(f.source_file, 0)

    # fatal 判定（plan §6 范式 (a) 防御面）：每个有效文件的失败块数都
    # 等于它的块数且块数大于 0（绑定校验后正常情况下到不了——空分片
    # 兜底与清单齐全使该路径仅剩环境漂移防御）
    failed = []
    for f in summary.files:
        n = chunk_count_by_name.get(f.source_file, 0)
        if n > 0 and f.failed_chunk_count >= n:
            failed.append((f, n))
    all_failed = bool(failed) and len(failed) == valid_count
    if all_failed:
        listing = ", ".join(f.source_file for f, _ in failed)
        fatal_message(
            "SPMG::0008", 0,
            f"all {valid_count} spef file(s) failed to parse, downstream "
            f"data cannot be produced: {listing}")
        # fatal_message 不返回（_exit(80) + master 联动）

    # 逐文件计数求和（消息族条件的「汇总里的合计」——逐文件字段在
    # summary 上无全局列，按有效文件表求和）
    def file_total(attr):
        return sum(getattr(f, attr) for f in summary.files)

    total_hit = file_total("hit_count")
    if total_hit == 0 and not all_failed:
        message("SPMG::0004", 0,
                f"all {valid_count} spef file(s) parsed but 0 valid nets "
                f"matched the design db (empty result)")
    if file_total("net_name_miss_count"):
        message("SPMG::0001", 0,
                f"{file_total('net_name_miss_count')} net name(s) not "
                f"matched in design db (skipped)")
    if file_total("strip_miss_count"):
        message("SPMG::0002", 0,
                f"{file_total('strip_miss_count')} entr(y/ies) skipped: "
                f"strip_prefix not matched (including fully-stripped "
                f"names)")
    if file_total("pg_net_skip_count"):
        message("SPMG::0003", 0,
                f"{file_total('pg_net_skip_count')} power/ground net "
                f"entr(y/ies) skipped (no parasitic semantics)")
    if summary.rnet_count:
        message("SPMG::0005", 0,
                f"{summary.rnet_count} reduced net(s) (R_NET) stored in "
                f"reduced form (total cap only)")
    if file_total("multi_value_net_count") or \
            file_total("value_index_fallback_count"):
        message("SPMG::0006", 0,
                f"{file_total('multi_value_net_count')} multi-value "
                f"net(s), {file_total('value_index_fallback_count')} "
                f"value-index fallback(s) (one value taken per rule)")
    if summary.same_scope_overlap_count:
        message("SPMG::0007", 0,
                f"{summary.same_scope_overlap_count} same-scope overlap "
                f"entr(y/ies) (first file kept)")
    if file_total("broken_net_count"):
        message("SPMG::0010", 0,
                f"{file_total('broken_net_count')} broken net section(s) "
                f"skipped (truncated/invalid records)")
    if file_total("unrouted_net_count"):
        message("SPMG::0011", 0,
                f"{file_total('unrouted_net_count')} net(s) without "
                f"routed geometry or attachable connections in design db "
                f"(skipped, uncovered downstream)")
    if file_total("name_map_miss_count"):
        message("SPMG::0012", 0,
                f"{file_total('name_map_miss_count')} unregistered "
                f"name-map index reference(s) (net/record skipped)")
    if summary.cap_mismatch_count:
        message("SPMG::0013", 0,
                f"{summary.cap_mismatch_count} net(s) with reported total "
                f"cap inconsistent with detail sum (max relative "
                f"deviation {summary.cap_mismatch_max_rel:.3g}; observed "
                f"only)")
    if file_total("pnet_skip_count"):
        message("SPMG::0014", 0,
                f"{file_total('pnet_skip_count')} physical net section(s) "
                f"skipped")

    # 部分文件失败：空分片已照常产出，失败清单可追溯
    if failed and len(failed) < valid_count:
        listing = ", ".join(f.source_file for f, _ in failed)
        INFO(f"spef flow: {len(failed)}/{valid_count} file(s) fully "
             f"failed to parse: {listing}")

    # 运行时规模临时对象自清理（键均为本任务 inputs，必然存在——严格
    # remove，缺失即 KeyError 暴露清理责任错位）+ gzip 解压副本目录
    # 删除（freeze 清理链单点；目录缺失容忍——明文库 scratch_dirs 为空）
    for key in slice_keys + counts_keys + manifest_keys + header_keys:
        db.remove_object(key)
    for d in scratch_dirs:
        shutil.rmtree(d, ignore_errors=True)
    db.write_object(summary_key, summary, save_to_db=True)
    INFO(f"spef summary: {total_hit} hit(s), "
         f"{file_total('net_count')} net(s), {valid_count} file(s) "
         f"({len(invalid_set)} skipped for invalid binding), "
         f"routed={summary.routed_signal_net_count} "
         f"covered={summary.covered_net_count} "
         f"uncovered={summary.uncovered_net_count}, "
         f"{summary.rnet_count} reduced net(s)")


# ── freeze（build_spef_db 第③段顶层提交）────────────────────────────

@as_task(inputs=lambda db: [
    db.get_full_name(SpefDb.SUMMARY_OBJ)])
def _freeze_spef_task(db):
    """freeze：依赖固定标记 summary（蕴含链：summary ⟸ 汇总 inputs 含
    全部切片 + 各分区计数 + 清单 + 文件头 ⟹ 分区 NET_LOAD 对象先写定
    ——freeze 不可能早于任一正式产物）。运行时规模临时对象由汇总任务
    自清理（§19 清理责任单点）。"""
    db.freeze()
