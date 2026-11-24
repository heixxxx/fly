"""E2E test: EMIRProject.build_spef_db — ⑤ spef db 大规模与多块
（plan docs/emir/spef-db-plan.md，2026-11-24 全裁定）。

验证（2 万网规模的 SPEF 程序化生成 → 切块并行解析 → 多分区落库 → 冻
结 → 读库），断言依据主 agent 测试设计稿 §5（期望值已全部对真实实现
核对——探测脚本核对后修正过样张公式一处：行首不小于 7 使对地恒为正，
负的电容记录是破损网；期望值全部写成字面量与公式，不依赖生成脚本）：
  - 设计库：wf_big.def 带布线副本 + partition_count=4（实际 4 个横向
    条带分区，先断言分区列表再断言条目数——这组数依赖设计库分区结果）；
  - 寄生文件在用例内生成到 qa_tmp 下：每个网一段（行首 = (序号 mod
    1000) + 7、耦合 = 序号 mod 7、对地 = 行首 − 耦合——行首恒为正、对
    地恒为正），序号 = 网在 DEF NETS 段的书写顺序；每段后一条约 1.8 KB
    填充注释把文件撑过两个最小切块（38 MB）；生成时二进制方式记下每个
    网段头的字节偏移；
  - 两个库一批提交：big_multi（alpha chunk_size_mb=16 → 切块数 3）与
    big_single（缺省 256 → 切块数 1）——除切块数外逐条相同；
  - 设置对象（read_object("alpha_settings")：chunk_size_mb 16 / 256、
    value_index None）；
  - 逐文件统计（解析网 20002、命中 20002、电容记录 40004、电阻 20002、
    破损 0、未布线 0、未放置实例 0）与全局统计（已布线信号网 20008 =
    20000 局部网 + clk/rn 在 4 分区各计一次、覆盖 20008、不一致 0、多
    成员电气网 0——平展设计）；
  - 分区条目数 (0,0) 20002（502 原始 + 19500 补全）/ (0,1) 3002 /
    (0,2) 4002 / (0,3) 12502，合计 39508；
  - 全量核对（直接读四个分区的网负载对象，不走点查：每条目的网编号、
    三值、来源位图 1、非缩减与公式逐条相同，两库逐条相同）；
  - 点查表（clk/rn 落全部 4 分区、局部网按 placement 行归属条带——
    字面量表 §5.3）+ 切块边界（按用例记下的字节偏移取 16 MB / 32 MB
    边界前后 4 KB 内的全部网——不写死网名）+ 固定步长抽查约 400 网；
  - 确定性：同输入的两次建库（即这两个库）汇总与分区对象逐条一致。
"""
import os
import shutil
import time

from log import INFO

from fly import get_config, launch_workers, wait_tasks
from fly.runtime import get_agent
from emir import EMIRProject

from test import qa_tmp

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DESIGN_DATA = os.path.join(SCRIPT_DIR, "data", "waveform", "design")
TIMING_DATA = os.path.join(SCRIPT_DIR, "data", "timing")
TECH_LEF = os.path.join(TIMING_DATA, "NangateOpenCellLibrary.tech.lef")
MACRO_LEF = os.path.join(TIMING_DATA,
                         "NangateOpenCellLibrary.macro.lef")
LIBERTY = os.path.join(TIMING_DATA,
                       "NangateOpenCellLibrary_typical.lib")

LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "emir_spef_big")
SCRATCH = qa_tmp("spef_big_gen")

from emir.design import iter_design_partition  # noqa: E402  分区表读库
from emir.spef import (  # noqa: E402  spef 读库 API（模块函数层）
    iter_spef_partition,
    load_partition_net_load,
    load_spef_summary,
)

# ── 寄生文件生成（§5.2：公式确定性——每个值都能手推）────────────────

# 文件头照基础样张（qa/emir/data/spef/sp_small.spef 的头部形态）
HEADER = """*SPEF "IEEE 1481-1998"
*DESIGN "wf_big"
*DATE "Tue Nov 24 00:00:00 2026"
*VENDOR "fly QA"
*PROGRAM "test_emir_spef_big.py"
*VERSION "1.0"
*DESIGN_FLOW "PIN_CAP NONE"
*DIVIDER /
*DELIMITER :
*T_UNIT 1 NS
*C_UNIT 1 FF
*R_UNIT 1 OHM

"""
# 每段之后一条约 1.8 KB 的填充注释——总体积约 38 MB，撑过两个最小切块
PAD = b"// " + b"x" * 1800 + b"\n"
CHUNK_BYTES = 16 * 1024 * 1024  # alpha chunk_size_mb=16（切块大小下限）
BOUNDARY_WINDOW = 4096          # 切块边界前后取网段头的窗口


def formula(i):
    """第 i 个网（序号 = DEF NETS 段书写顺序，从 0 起）的三值：
    行首 = (序号 mod 1000) + 7、耦合 = 序号 mod 7、对地 = 行首 − 耦合。
    行首不小于 7、耦合最大 6——对地恒为正（负的电容记录是破损网、整
    网跳过）；行首 = 对地 + 耦合，行首值不一致计数为 0。"""
    total = (i % 1000) + 7
    coupling = i % 7
    return total, total - coupling, coupling


def read_nets(def_path):
    """DEF 网表段里的网名（书写顺序）。"""
    nets = []
    in_nets = False
    with open(def_path) as f:
        for line in f:
            if line.startswith("NETS "):
                in_nets = True
                continue
            if line.startswith("END NETS"):
                break
            if in_nets and line.lstrip().startswith("- "):
                nets.append(line.split()[1])
    return nets


def routed_copy(def_name):
    """带布线几何的副本（qa_tmp scratch；每条网语句补同一段 metal1
    ROUTED——寄生条目按连接位实例归属分区，未布线的网不在分区网表）。"""
    src = os.path.join(DESIGN_DATA, def_name)
    os.makedirs(SCRATCH, exist_ok=True)
    out = os.path.join(SCRATCH, "routed_" + def_name)
    with open(src) as f:
        lines = []
        for line in f.read().splitlines():
            compact = line.strip()
            if (compact.startswith("- ") and compact.endswith(") ;")
                    and "+ ROUTED" not in compact):
                line = compact[:-1].rstrip() + \
                    " + ROUTED metal1 ( 1000 4000 ) ( 2000 4000 ) ;"
            lines.append(line)
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")
    return out


def gen_spef(nets, path):
    """每个网一段 + 一条填充注释；返回每个网段头的字节偏移（二进制方
    式写——偏移即文件字节位置）。"""
    offsets = []
    with open(path, "wb") as f:
        f.write(HEADER.encode())
        for i, name in enumerate(nets):
            offsets.append(f.tell())
            total, ground, coupling = formula(i)
            f.write(("*D_NET %s %d\n*CAP\n1 %s:1 %d\n2 %s:1 %s:2 %d\n"
                     "*RES\n1 %s:1 %s:2 %d\n*END\n"
                     % (name, total, name, ground, name, name, coupling,
                        name, name, 1)).encode())
            f.write(PAD)
    return offsets


# ── 断言辅助（同 test_emir_spef_flow 的口径）────────────────────────

_FILE_FIELDS = (
    "net_count", "rnet_count", "pnet_skip_count", "broken_net_count",
    "name_map_miss_count", "dropped_record_count", "multi_value_net_count",
    "value_count_max", "value_index_fallback_count", "cap_count",
    "coupling_cap_count", "res_count", "chunk_count", "hit_count",
    "net_name_miss_count", "pg_net_skip_count", "unrouted_net_count",
    "strip_miss_count", "unplaced_instance_count", "failed_chunk_count",
)
_SUMMARY_FIELDS = (
    "routed_signal_net_count", "covered_net_count", "uncovered_net_count",
    "uncovered_instance_count", "zero_cap_instance_count", "rnet_count",
    "cap_mismatch_count", "same_scope_overlap_count", "cross_scope_sum_count",
    "source_mask_overflow_count", "invalid_binding_count",
    "electrical_net_count",
)


def _assert_summary(summary, **expect):
    for field in _SUMMARY_FIELDS:
        want = expect.get(field, 0)
        got = getattr(summary, field)
        assert got == want, (field, got, want)


def cleanup():
    if os.path.isdir(PROJ_PATH):
        shutil.rmtree(PROJ_PATH, ignore_errors=True)


cleanup()

T0 = time.time()


def _lap(what):
    INFO("  [TIME] %.2fs: %s" % (time.time() - T0, what))


launch_workers([{}, {}])
assert get_agent().wait_workers_registered(timeout=60), "workers connect"
INFO("  2 workers connected (user-managed)")

proj = EMIRProject(PROJ_PATH)

# ── 前置：lib db + 大设计库（带布线副本，partition_count=4）──────────
lib_db = proj.build_lib_db(name="big_lib", lib_paths=[LIBERTY])
assert proj.wait_frozen("big_lib", timeout=1200), "lib db should freeze"

ROUTED_BIG_DEF = routed_copy("wf_big.def")
big_design = proj.build_design_db(
    name="big_design", def_paths=[ROUTED_BIG_DEF],
    lef_paths=[TECH_LEF, MACRO_LEF], lib_db=lib_db,
    alpha={"partition_count": 4})
assert wait_tasks(timeout=1800), "design flow complete"
assert proj.wait_frozen("big_design", timeout=120), \
    "big_design should freeze"
_lap("lib + design db frozen")

# §5.3 第 4 条前置：先断言分区列表（条目数依赖设计库分区结果）
PARTS = [(0, 0), (0, 1), (0, 2), (0, 3)]
assert list(iter_design_partition(big_design)) == PARTS
INFO("[OK] big design: 4 horizontal-band partitions")

# ── 寄生文件生成（qa_tmp 下，约 38 MB）───────────────────────────────

nets = read_nets(ROUTED_BIG_DEF)
assert len(nets) == 20002, len(nets)
assert nets[0] == "n_d_0_0" and nets[1] == "clk" and nets[2] == "rn", \
    nets[:3]
assert nets[3] == "n_q_0_0" and nets[-1] == "n_q_99_99", (nets[3],
                                                          nets[-1])
SPEF_PATH = os.path.join(SCRATCH, "big.spef")
offsets = gen_spef(nets, SPEF_PATH)
_lap("spef file generated (%d bytes, %d net(s))"
     % (os.path.getsize(SPEF_PATH), len(nets)))

# ── 两个寄生库一批提交（chunk 16 MB → 3 块；缺省 → 1 块）─────────────

big_multi = proj.build_spef_db(
    name="big_multi", spef_files=[SPEF_PATH], design_db=big_design,
    alpha={"chunk_size_mb": 16})
big_single = proj.build_spef_db(
    name="big_single", spef_files=[SPEF_PATH], design_db=big_design)
assert wait_tasks(timeout=1800), "spef flows complete"
assert proj.wait_frozen("big_multi", timeout=120), \
    "big_multi should freeze"
assert proj.wait_frozen("big_single", timeout=120), \
    "big_single should freeze"
_lap("both spef dbs frozen")

# ═══ §5.3 第 1 条：设置对象 ══════════════════════════════════════════

settings_multi = big_multi.read_object("alpha_settings")
settings_single = big_single.read_object("alpha_settings")
assert settings_multi.chunk_size_mb == 16, settings_multi.chunk_size_mb
assert settings_single.chunk_size_mb == 256, settings_single.chunk_size_mb
assert settings_multi.value_index is None, settings_multi.value_index
assert settings_single.value_index is None, settings_single.value_index
INFO("[OK] alpha_settings: chunk_size_mb 16 / 256, value_index None")

# ═══ §5.3 第 2 条：逐文件统计（两库相同，只有切块数不同）═════════════

s_multi = load_spef_summary(big_multi)
s_single = load_spef_summary(big_single)
assert len(s_multi.files) == len(s_single.files) == 1
f_multi, f_single = s_multi.files[0], s_single.files[0]
assert f_multi.source_file == f_single.source_file
for field in _FILE_FIELDS:
    if field == "chunk_count":
        continue
    assert getattr(f_multi, field) == getattr(f_single, field), field
    want = {"net_count": 20002, "hit_count": 20002, "cap_count": 40004,
            "coupling_cap_count": 20002, "res_count": 20002,
            "value_count_max": 1}.get(field, 0)
    assert getattr(f_multi, field) == want, (field, getattr(f_multi, field),
                                             want)
# 切块数不在上面的字段循环里断言（循环跳过 chunk_count）——单独断言
assert f_multi.chunk_count == 3, f_multi.chunk_count
assert f_single.chunk_count == 1, f_single.chunk_count
INFO("[OK] per-file stats: 20002 nets parsed and hit, chunk count 3 vs 1")

# ═══ §5.3 第 3 条：全局统计（两库相同）═══════════════════════════════

for s in (s_multi, s_single):
    _assert_summary(s,
                    routed_signal_net_count=20008,  # 20000 局部网 + clk/
                    covered_net_count=20008,        # rn 在 4 区各计一次
                    cap_mismatch_max_rel=0.0)
INFO("[OK] global stats: routed 20008 = covered 20008, flat design "
     "(0 electrical groups), no mismatch")

# ═══ §5.3 第 4 条：分区列表与条目数 ══════════════════════════════════

PART_SIZES = {(0, 0): 20002, (0, 1): 3002, (0, 2): 4002, (0, 3): 12502}
assert list(iter_spef_partition(big_multi)) == PARTS
assert list(iter_spef_partition(big_single)) == PARTS
total_entries = 0
for xp, yp in PARTS:
    size_a = load_partition_net_load(big_multi, xp, yp).size
    size_b = load_partition_net_load(big_single, xp, yp).size
    assert size_a == size_b == PART_SIZES[(xp, yp)], ((xp, yp), size_a,
                                                      size_b)
    total_entries += size_a
assert total_entries == 39508, total_entries
INFO("[OK] partition entry counts: (0,0) 20002 = 502 attached + 19500 "
     "completed; total 39508")

# ═══ §5.3 第 5 条：全量核对（分区对象逐条目 vs 公式；两库逐条相同）═══

checked = 0
for xp, yp in PARTS:
    obj_a = load_partition_net_load(big_multi, xp, yp)
    obj_b = load_partition_net_load(big_single, xp, yp)
    items_a, items_b = obj_a.items, obj_b.items
    assert len(items_a) == len(items_b)
    prev_id = 0
    for ea, eb in zip(items_a, items_b):
        # 两库逐条相同
        assert ea.net_id == eb.net_id and \
            ea.total_cap_reported_ff == eb.total_cap_reported_ff and \
            ea.ground_cap_ff == eb.ground_cap_ff and \
            ea.coupling_cap_ff == eb.coupling_cap_ff and \
            ea.source_mask == eb.source_mask and ea.flags == eb.flags
        # 条目按 net id 升序（书写序 + 1 → 序号 = net_id − 1）
        assert ea.net_id > prev_id, (ea.net_id, prev_id)
        prev_id = ea.net_id
        total, ground, coupling = formula(ea.net_id - 1)
        assert ea.total_cap_reported_ff == float(total), \
            (xp, yp, ea.net_id, ea.total_cap_reported_ff, total)
        assert ea.ground_cap_ff == float(ground), \
            (xp, yp, ea.net_id, ea.ground_cap_ff, ground)
        assert ea.coupling_cap_ff == float(coupling), \
            (xp, yp, ea.net_id, ea.coupling_cap_ff, coupling)
        assert ea.source_mask == 1, (xp, yp, ea.net_id)
        assert ea.flags & 0x1 == 0, (xp, yp, ea.net_id)
        checked += 1
assert checked == 39508, checked
INFO("[OK] full verification: all %d entries of both dbs match the "
     "formula (net-id ascending, mask 1, not reduced)" % checked)

# ═══ §5.3 第 6 条：点查表（字面量——名字、网编号、三值、落点）════════

# (网名, 网编号, 行首/对地/耦合, 落点分区)——设计稿 §5.3 第 6 条
PROBE_TABLE = [
    ("clk", 2, (8, 7, 1), PARTS),
    ("rn", 3, (9, 7, 2), PARTS),
    ("n_d_0_0", 1, (7, 7, 0), [(0, 0)]),
    ("n_d_2_49", 501, (507, 504, 3), [(0, 0)]),
    ("n_d_5_0", 1003, (9, 8, 1), [(0, 0), (0, 1)]),
    ("n_q_3_0", 604, (610, 609, 1), [(0, 0), (0, 1)]),
    ("n_d_25_0", 5003, (9, 5, 4), [(0, 0), (0, 2)]),
    ("n_q_25_49", 5102, (108, 103, 5), [(0, 0), (0, 2)]),
    ("n_q_49_99", 10002, (8, 3, 5), [(0, 0), (0, 3)]),
    ("n_q_99_99", 20002, (8, 6, 2), [(0, 0), (0, 3)]),
]
for db in (big_multi, big_single):
    for name, nid, vals, parts in PROBE_TABLE:
        rec = db.get_net_load(name)
        assert rec is not None, name
        got = (rec["total_cap_reported_ff"], rec["ground_cap_ff"],
               rec["coupling_cap_ff"])
        assert got == vals, (db is big_multi, name, got, vals)
        assert rec["net_id"] == nid, (name, rec["net_id"], nid)
        assert rec["partitions"] == parts, (name, rec["partitions"])
        assert rec["electrical_members"] == [nid], (name, rec[
            "electrical_members"])
        assert rec["reduced"] is False, name
        assert rec["source_mask"] == 1, name
INFO("[OK] point-query table: 10 nets x 2 dbs (band placement, clk/rn "
     "in all 4 partitions)")

# ═══ §5.3 第 7 条：切块边界（按用例记下的偏移取，不写死网名）═════════

# 两个切块边界分别保证覆盖：各边界的窗口内非空（合计下限挡不住「全部
# 落在同一边界」的漂移——38 MB 填充尺寸下每侧约 4 个网段头）
near_16 = [i for i in range(len(nets))
           if abs(offsets[i] - CHUNK_BYTES) <= BOUNDARY_WINDOW]
near_32 = [i for i in range(len(nets))
           if abs(offsets[i] - 2 * CHUNK_BYTES) <= BOUNDARY_WINDOW]
assert near_16, "no net header near the 16 MB boundary"
assert near_32, "no net header near the 32 MB boundary"
for i in near_16 + near_32:
    rec_a = big_multi.get_net_load(nets[i])
    rec_b = big_single.get_net_load(nets[i])
    total, ground, coupling = formula(i)
    for rec in (rec_a, rec_b):
        assert rec is not None, nets[i]
        got = (rec["total_cap_reported_ff"], rec["ground_cap_ff"],
               rec["coupling_cap_ff"])
        assert got == (total, ground, coupling), (nets[i], got)
    assert rec_a["net_id"] == rec_b["net_id"] == i + 1
INFO("[OK] chunk boundaries: %d net(s) near 16 MB + %d near 32 MB "
     "match the formula in both dbs" % (len(near_16), len(near_32)))

# 固定步长抽查约 400 个网（点查路径——名字换算 + 分区 LRU）
step = max(1, len(nets) // 400)
sample = list(range(0, len(nets), step))
for i in sample:
    rec_a = big_multi.get_net_load(nets[i])
    rec_b = big_single.get_net_load(nets[i])
    total, ground, coupling = formula(i)
    for rec in (rec_a, rec_b):
        assert rec is not None, nets[i]
        got = (rec["total_cap_reported_ff"], rec["ground_cap_ff"],
               rec["coupling_cap_ff"])
        assert got == (total, ground, coupling), (nets[i], got)
INFO("[OK] sampled %d net(s) at fixed step %d: both dbs match the "
     "formula" % (len(sample), step))
_lap("verification done")

# ═══ §5.3 第 8 条：确定性（同输入两库逐条一致）═══════════════════════

for field in _SUMMARY_FIELDS + ("cap_mismatch_max_rel",
                                "internal_cap_unit_ff",
                                "internal_res_unit_ohm"):
    assert getattr(s_multi, field) == getattr(s_single, field), field
INFO("[OK] determinism: big_multi / big_single identical except chunk "
     "count")

shutil.rmtree(SCRATCH, ignore_errors=True)

INFO("[PASS] test_emir_spef_big")
