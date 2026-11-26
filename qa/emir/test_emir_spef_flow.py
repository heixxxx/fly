"""E2E test: EMIRProject.build_spef_db — ⑤ spef db 全链（单文件的各种
形态；plan docs/emir/spef-db-plan.md，2026-11-24 全裁定）。

验证（SPEF 解析 → 单位换算为内部飞法 → 名字换算为 design db 全局 id →
分区合并落库 → 冻结 → 读库），断言依据 qa/emir/data/spef/README.md 推
导表与主 agent 测试设计稿 §3（期望值全部写成字面量，与样张生成脚本
gen_spef.py 相互独立）：
  - 基础建库（sp_small.spef，1 FF）：逐网三值（行首/对地/耦合）精确
    断言 + 全量分区条目（net_id 升序 15 条，含缩减网 rn 的缩减标记）+
    逐文件统计全字段 + 全局覆盖率/不一致计数（n_and 行首差 1、最大相
    对偏差 1/46）+ 未命中网（n_xor / ghost_net）为 None；
  - gzip 压缩输入：建库结果与明文逐项一致（分区对象逐字节 + 逐文件
    统计除来源路径外全字段 + 汇总数值字段），解压副本冻结前清理；
  - 方言样张（sp_small_dialect.spef，1.0 PF）：皮法刻度换算（相对误差
    1e-9）+ 名字映射 / 记录不带编号 / 多值形态取第 1 个 / 段级破损 /
    未登记映射索引 / 物理网段各计数；
  - 取值下标覆盖（alpha value_index=2）：多值网 n_b2 取第 2 个值 +
    两值网 n_band 越界回退缺省规则（回退计数 25，按字段计）+ 行首值
    不一致 1（30 ≠ 27，最大相对偏差 0.1）+ alpha_settings 落库；
  - 缩减网样张（sp_small_rnet.spef）：缩减网形态（只有行首）+ 缩减标
    记 + 同文件混合分布式网；
  - 多分区设计（partition_count=16 → 实际 2 个分区）：条目落点 = 本区
    连接位的网 + 本区网表补全条目（(0,0) 15 条 / (2,0) 10 条）+ 同网
    各分区数值相同 + 覆盖率按本区口径（合计 23/22/1）+ 行首不一致逐
    分区计（n_and 两区各计一次 = 2）；
  - 未布线设计（原始 wf_small.def）：全部网未布线跳过（命中 0、未布
    线 15）+ 空分区对象 + 全局覆盖率全 0 + 零有效网消息；
  - 电源网段跳过（sp_pg.spef 配带电源网的派生设计）：VDD/VSS 跳过计
    数 2 + 普通网照常收录 + 前置断言设计库电源地网集合；
  - 确定性：同输入双跑（sp_det_a / sp_det_b）与 sp_basic 逐字节一致
    （summary + 分区对象 __getstate__）；
  - SPMG 消息族按批计数（十个库一批结束后的 message.log 增量，§3.10
    逐条次数）；
  - 破损文件（bad_header.spef 缺 *C_UNIT）：文件入口任务失败、库不冻
    结、失败原因含 C_UNIT，且不影响前面批次的消息计数。
"""
import gzip
import os
import shutil

from log import INFO

from fly import get_config, launch_workers, wait_tasks
from fly.runtime import get_agent
from emir import EMIRProject

from test import qa_tmp, record

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(SCRIPT_DIR, "data", "spef")
DESIGN_DATA = os.path.join(SCRIPT_DIR, "data", "waveform", "design")
TIMING_DATA = os.path.join(SCRIPT_DIR, "data", "timing")
TECH_LEF = os.path.join(TIMING_DATA, "NangateOpenCellLibrary.tech.lef")
MACRO_LEF = os.path.join(TIMING_DATA,
                         "NangateOpenCellLibrary.macro.lef")
LIBERTY = os.path.join(TIMING_DATA,
                       "NangateOpenCellLibrary_typical.lib")

LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "emir_spef_flow")
MSG_LOG = os.path.join(LOG_DIR, "message.log")

from emir.design import (  # noqa: E402  设计库读库（分区表 + 电源地网集）
    iter_design_partition,
    load_design_pg_nets,
)
from emir.spef import (  # noqa: E402  spef 读库 API（模块函数层）
    iter_spef_partition,
    load_partition_net_load,
    load_spef_summary,
)

# ── 期望值（全部字面量——测试设计稿 §1.1 / §2 / §3 与数据推导表）──────

# wf_small 带布线副本的网编号（测试设计稿 §1.1，已探测核实）
NET_ID = {
    "count[0]": 1, "n_cnt0_d": 2, "count[1]": 3, "n_cnt1_d": 4,
    "n_b1": 5, "count[2]": 6, "n_cnt2_d": 7, "n_b2": 8,
    "count[3]": 9, "n_cnt3_d": 10, "clk": 11, "rn": 12,
    "clk_div2": 13, "n_div_d": 14, "n_and": 15, "n_xor": 16,
}

# sp_small.spef 逐网（行首 / 对地 / 耦合，单位飞法——1 FF 精确断言）。
# rn 缩减网只收录行首；clk_div2 没有 *CAP/*RES 段；n_div_d 一条零值记
# 录；n_and 行首比明细合计（45）大 1
SMALL_NETS = {
    "count[0]": (3, 2, 1), "n_cnt0_d": (6, 4, 2),
    "count[1]": (9, 6, 3), "n_cnt1_d": (12, 8, 4),
    "n_b1": (15, 10, 5), "count[2]": (18, 12, 6),
    "n_cnt2_d": (21, 14, 7), "n_b2": (24, 16, 8),
    "count[3]": (27, 18, 9), "n_cnt3_d": (30, 20, 10),
    "clk": (33, 22, 11), "rn": (36, 0, 0),
    "clk_div2": (0, 0, 0), "n_div_d": (0, 0, 0),
    "n_and": (46, 30, 15),
}
# 缩减形态的网（sp_small 里只有 rn）
SMALL_REDUCED = {"rn"}

# sp_basic 的 PART_0_0 全量条目（net_id 升序 15 条；flags 最低位 = 缩减）
_BASIC_VALS = {nid: SMALL_NETS[name] for name, nid in NET_ID.items()
               if name in SMALL_NETS}
_BASIC_PART_0_0 = [
    (nid, vals[0], vals[1], vals[2], 1, 1 if nid == NET_ID["rn"] else 0)
    for nid, vals in sorted(_BASIC_VALS.items())
]

# sp_grid 的 (2,0) 条目 = 连接位在本区的 10 张网（本区网表为空，无补全）
_GRID_PART_2_0 = [
    entry for entry in _BASIC_PART_0_0
    if entry[0] in (NET_ID["count[0]"], NET_ID["count[1]"],
                    NET_ID["count[2]"], NET_ID["count[3]"],
                    NET_ID["n_cnt3_d"], NET_ID["clk"], NET_ID["rn"],
                    NET_ID["clk_div2"], NET_ID["n_div_d"], NET_ID["n_and"])
]

# sp_small_dialect.spef 逐网（换算为飞法后的期望；皮法样张——相对误差
# 比较）。多值形态：n_b2 三值取第 1 个、n_and 两值取第 0 个
DIALECT_NETS = {
    "count[3]": (27, 18, 9), "n_b2": (24, 16, 8),
    "n_and": (45, 30, 15), "clk_div2": (2, 2, 0),
    "n_div_d": (4, 4, 0), "n_cnt0_d": (10, 9, 1),
    "n_b1": (10, 10, 0), "count[1]": (9, 6, 3),
}
# value_index=2 覆盖后与 sp_dialect 的差别：n_b2 取第 2 个值（对地 =
# 10 + 8）；n_and 两值形态下标越界回退缺省规则（值不变）
DIALECT_IDX_N_B2 = (30, 18, 9)

# sp_small_rnet.spef 逐网（行首 / 对地 / 耦合，飞法精确）。缩减网只有
# 行首；n_b1 行首三值形态 15:18:20 取第 1 个
RNET_NETS = {
    "clk": (80, 0, 0), "rn": (90, 0, 0),
    "n_and": (12, 0, 0), "n_b1": (18, 0, 0),
    "count[0]": (3, 2, 1), "n_xor": (8, 5, 3),
    "n_div_d": (0, 0, 0), "clk_div2": (2, 2, 0),
}
RNET_REDUCED = {"clk", "rn", "n_and", "n_b1"}

# sp_pg.spef：VDD（1000）/ VSS（800）电源网段跳过；clk 照常收录
PG_CLK = (33, 22, 11)

# SPMG 消息族按批计数的期望（测试设计稿 §3.10；实现语义——0002 =
# strip 前缀未匹配、0007 = 同作用域重合，本批两者都是 0）
FLOW_MSGS = {1: 6, 3: 1, 4: 1, 5: 7, 6: 3, 10: 2, 11: 1, 12: 2, 13: 6,
             14: 2}


# ── 断言辅助 ─────────────────────────────────────────────────────────

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


def _file_stats(summary, suffix, tag):
    hits = [f for f in summary.files if f.source_file.endswith(suffix)]
    # 原断言：assert len(hits) == 1, (suffix, [f.source_file for f in summary.files])
    record(f"{tag}/file_stats_hit_1", len(hits) == 1,
           detail=[f.source_file for f in summary.files])
    assert len(hits) == 1  # 保留：守卫（后面 hits[0] 按下标取，空列表时会以 IndexError 崩溃）
    return hits[0]


def _assert_file(f, tag, c_unit, **expect):
    """逐文件统计全字段记录（未给的数值字段按 0 期望）；*C_UNIT 换算
    系数是浮点——按相对容差记判定，不进基准。"""
    # 原断言：assert got == want, (field, got, want)
    record(f"{tag}/file_stats",
           {field: getattr(f, field) for field in _FILE_FIELDS})
    # 原断言：assert abs(f.c_unit_ff_per_unit - unit) <= 1e-9 * unit, \
    record(f"{tag}/c_unit_ff_match",
           abs(f.c_unit_ff_per_unit - c_unit) <= 1e-9 * c_unit,
           detail=f"{f.c_unit_ff_per_unit!r}")


def _assert_summary(summary, tag, **expect):
    """全局统计记录（未给的计数按 0 期望；max_rel 是浮点——由调用点
    按相对误差记判定，不进基准）。"""
    # 原断言：assert got == want, (field, got, want)
    record(f"{tag}/summary_stats",
           {field: getattr(summary, field) for field in _SUMMARY_FIELDS})


def _assert_load(rec, total, ground, coupling, tag, rel=0.0):
    """get_net_load 点查记录（rel=0 飞法精确；rel>0 相对误差——皮法刻
    度样张换算后的双精度残差）。三值是浮点——记「满足容差/相等」的判
    定，实际值放 detail。"""
    # 原断言：assert rec is not None
    record(f"{tag}/load_exists", rec is not None)
    assert rec is not None  # 保留：守卫（后面 rec["total_cap_reported_ff"] 等解引用）
    got = (rec["total_cap_reported_ff"], rec["ground_cap_ff"],
           rec["coupling_cap_ff"])
    want = (total, ground, coupling)
    ok = got == want if not rel else \
        all(abs(g - w) <= rel * abs(w) for g, w in zip(got, want))
    # 原断言：assert got == want, (got, want)
    # 原断言：assert abs(g - w) <= rel * abs(w), (got, want)
    record(f"{tag}/loads_match", ok, detail=(got, want))


def _partition_entries(db, xp, yp):
    obj = load_partition_net_load(db, xp, yp)
    return obj, [(e.net_id, e.total_cap_reported_ff, e.ground_cap_ff,
                  e.coupling_cap_ff, e.source_mask, e.flags)
                 for e in obj.items]


def _assert_entries(entries, expect, tag):
    """全量条目记录（net_id 升序 + 逐条精确值——电容三值是浮点，记判定）。"""
    got_ids = [e[0] for e in entries]
    want_ids = [e[0] for e in expect]
    # 原断言：assert got_ids == want_ids, (where, got_ids, want_ids)
    record(f"{tag}/entries_net_id_order", got_ids == want_ids,
           detail=(got_ids, want_ids))
    # 原断言：assert g == w, (where, g, w)
    record(f"{tag}/entries_exact", entries == expect,
           detail=[(g, w) for g, w in zip(entries, expect) if g != w])


def _spmg_counts():
    with open(MSG_LOG, errors="replace") as f:
        text = f.read()
    return {i: text.count("SPMG::%04d" % i) for i in range(1, 15)}


def _assert_msg_delta(before, after, tag, expect):
    delta = {k: after[k] - before[k] for k in after}
    # 原断言：assert delta[i] == want, (f"SPMG::{i:04d}", delta[i], want)
    record(f"{tag}/spmg_msg_deltas", delta)


def cleanup():
    if os.path.isdir(PROJ_PATH):
        shutil.rmtree(PROJ_PATH, ignore_errors=True)


cleanup()

launch_workers([{}, {}])
assert get_agent().wait_workers_registered(timeout=60), "workers connect"
INFO("  2 workers connected (user-managed)")

proj = EMIRProject(PROJ_PATH)
# 原断言：assert "build_spef_db" in proj.list_flows()
record("flows/build_spef_db_registered", "build_spef_db" in proj.list_flows(),
       detail=sorted(proj.list_flows()))
INFO("[DONE] build_spef_db registered on EMIRProject")

# ── 前置：lib db + design db 族（一批提交，一次等待）─────────────────
lib_db = proj.build_lib_db(name="sp_lib", lib_paths=[LIBERTY])
assert proj.wait_frozen("sp_lib", timeout=1200), "lib db should freeze"
INFO("[WAIT] lib db frozen")


def _routed_def(def_name, pg=False):
    """派生 wf_small.def 的带布线副本（qa_tmp scratch；每网补一段
    metal1 布线）。pg=True 时在 NETS 段之前插入两条通配连接的电源网
    （真实布线工具写出的形态——数据推导表「对齐的设计与建库办法」节）。
    """
    src = os.path.join(DESIGN_DATA, def_name)
    out_dir = qa_tmp("spef_flow_def")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, ("pg_" if pg else "") + def_name)
    with open(src) as f:
        lines = []
        for line in f.read().splitlines():
            stripped = line.rstrip()
            compact = stripped.strip()
            if (compact.startswith("- ") and compact.endswith(") ;")
                    and "+ ROUTED" not in compact):
                stripped = compact[:-1].rstrip() + \
                    " + ROUTED metal1 ( 1000 4000 ) ( 2000 4000 ) ;"
            lines.append(stripped)
        if pg:
            idx = lines.index("NETS 16 ;")
            lines[idx:idx] = [
                "SPECIALNETS 2 ;",
                "    - VDD ( * VDD ) + USE POWER",
                "      + ROUTED metal1 340 + SHAPE FOLLOWPIN "
                "( 0 5600 ) ( 40000 5600 ) ;",
                "    - VSS ( * VSS ) + USE GROUND",
                "      + ROUTED metal1 340 + SHAPE FOLLOWPIN "
                "( 0 2800 ) ( 40000 2800 ) ;",
                "END SPECIALNETS",
            ]
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")
    return out


def _submit_design(name, def_paths, **alpha):
    kwargs = {}
    if alpha:
        kwargs["alpha"] = alpha
    return proj.build_design_db(
        name=name, def_paths=def_paths, lef_paths=[TECH_LEF, MACRO_LEF],
        lib_db=lib_db, **kwargs)


ROUTED_SMALL_DEF = _routed_def("wf_small.def")
PG_SMALL_DEF = _routed_def("wf_small.def", pg=True)

d_small = _submit_design("sp_design", [ROUTED_SMALL_DEF])
d_grid = _submit_design("sp_design_grid", [ROUTED_SMALL_DEF],
                        partition_count=16)
d_unrouted = _submit_design(
    "sp_design_unrouted", [os.path.join(DESIGN_DATA, "wf_small.def")])
d_pg = _submit_design("sp_design_pg", [PG_SMALL_DEF])
assert wait_tasks(timeout=1800), "design flows complete"
for n in ("sp_design", "sp_design_grid", "sp_design_unrouted",
          "sp_design_pg"):
    assert proj.wait_frozen(n, timeout=60), f"{n} should freeze"
INFO("[WAIT] design db family frozen (4 db(s), one batch)")

# 设计库前置事实（测试设计稿 §1.1 / §1.2 与 §3 的前提断言）
grid_parts = list(iter_design_partition(d_grid))
# 原断言：assert grid_parts == [(0, 0), (2, 0)], grid_parts
record("design_grid/partition_list", grid_parts)
INFO("[DONE] sp_design_grid: 2 partitions (partition_count is a floor)")

pg_nets = load_design_pg_nets(d_pg)
vdd = d_pg.get_net("VDD")
# 原断言：assert pg_nets.power_count == 1, pg_nets.power_count
record("design_pg/power_count", pg_nets.power_count)
# 原断言：assert pg_nets.ground_count == 1, pg_nets.ground_count
record("design_pg/ground_count", pg_nets.ground_count)
# 原断言：assert vdd is not None, "VDD must be registered as a power net"
record("design_pg/vdd_exists", vdd is not None)
assert vdd is not None  # 保留：守卫（后面 vdd["connection_count"] 按下标取）
# 原断言：assert vdd["connection_count"] == 14, vdd["connection_count"]
record("design_pg/vdd_connection_count", vdd["connection_count"])
INFO("[DONE] sp_design_pg: 1 power + 1 ground net, VDD wildcard-connected")

# ── 寄生库十个（一批提交，一次等待）──────────────────────────────────

GZ_PATH = os.path.join(qa_tmp("spef_flow_gen"), "sp_small.spef.gz")
os.makedirs(os.path.dirname(GZ_PATH), exist_ok=True)
with open(os.path.join(DATA, "sp_small.spef"), "rb") as f:
    _raw = f.read()
with gzip.open(GZ_PATH, "wb") as f:
    f.write(_raw)

msg_before = _spmg_counts()

sp_basic = proj.build_spef_db(
    name="sp_basic", spef_files=[os.path.join(DATA, "sp_small.spef")],
    design_db=d_small)
sp_gzip = proj.build_spef_db(
    name="sp_gzip", spef_files=[GZ_PATH], design_db=d_small)
sp_dialect = proj.build_spef_db(
    name="sp_dialect",
    spef_files=[os.path.join(DATA, "sp_small_dialect.spef")],
    design_db=d_small)
sp_dialect_idx = proj.build_spef_db(
    name="sp_dialect_idx",
    spef_files=[os.path.join(DATA, "sp_small_dialect.spef")],
    design_db=d_small, alpha={"value_index": 2})
sp_rnet = proj.build_spef_db(
    name="sp_rnet", spef_files=[os.path.join(DATA, "sp_small_rnet.spef")],
    design_db=d_small)
sp_grid = proj.build_spef_db(
    name="sp_grid", spef_files=[os.path.join(DATA, "sp_small.spef")],
    design_db=d_grid)
sp_unrouted = proj.build_spef_db(
    name="sp_unrouted", spef_files=[os.path.join(DATA, "sp_small.spef")],
    design_db=d_unrouted)
sp_pg = proj.build_spef_db(
    name="sp_pg", spef_files=[os.path.join(DATA, "sp_pg.spef")],
    design_db=d_pg)
sp_det_a = proj.build_spef_db(
    name="sp_det_a", spef_files=[os.path.join(DATA, "sp_small.spef")],
    design_db=d_small)
sp_det_b = proj.build_spef_db(
    name="sp_det_b", spef_files=[os.path.join(DATA, "sp_small.spef")],
    design_db=d_small)

# wait_tasks 在有任务失败时抛 RuntimeError——坏文件的库（§3.11）不能进
# 本批，单独提交（见文末）
assert wait_tasks(timeout=1800), "spef flows complete"
for n in ("sp_basic", "sp_gzip", "sp_dialect", "sp_dialect_idx",
          "sp_rnet", "sp_grid", "sp_unrouted", "sp_pg", "sp_det_a",
          "sp_det_b"):
    assert proj.wait_frozen(n, timeout=60), f"{n} should freeze"
INFO("[WAIT] spef db batch frozen (10 db(s), one batch)")

# ═══ §3.1 sp_basic：逐网三值 + 全量分区条目 + 统计 ═══════════════════

s_basic = load_spef_summary(sp_basic)
# 原断言：assert len(s_basic.files) == 1, s_basic.files
record("basic/files_count", len(s_basic.files))
assert len(s_basic.files) >= 1  # 保留：守卫（后面 s_basic.files[0] 按下标取，越界在 C++ 侧是未定义行为）
# 原断言：assert s_basic.files[0].source_file.endswith("sp_small.spef")
record("basic/source_suffix",
       s_basic.files[0].source_file.endswith("sp_small.spef"),
       detail=s_basic.files[0].source_file)
for name, (total, ground, coupling) in SMALL_NETS.items():
    rec = sp_basic.get_net_load(name)
    _assert_load(rec, total, ground, coupling, f"basic/{name}")
    # 原断言：assert rec["net_id"] == NET_ID[name], (name, rec["net_id"])
    record(f"basic/{name}/net_id", rec["net_id"])
    # 原断言：assert rec["reduced"] == (name in SMALL_REDUCED), name
    record(f"basic/{name}/reduced_matches",
           rec["reduced"] == (name in SMALL_REDUCED), detail=rec["reduced"])
    # 原断言：assert rec["source_mask"] == 1, name
    record(f"basic/{name}/source_mask", rec["source_mask"])
    # 原断言：assert rec["partitions"] == [(0, 0)], name
    record(f"basic/{name}/partitions", rec["partitions"])
    # 原断言：assert rec["electrical_members"] == [NET_ID[name]], name
    record(f"basic/{name}/electrical_members", rec["electrical_members"])
# 原断言：assert sp_basic.get_net_load("n_xor") is None
record("basic/n_xor_load", sp_basic.get_net_load("n_xor"))
# 非守卫：仅检查未命中网返回空，返回值无后续解引用
# 原断言：assert sp_basic.get_net_load("ghost_net") is None
record("basic/ghost_net_load", sp_basic.get_net_load("ghost_net"))
# 非守卫：仅检查未命中网返回空，返回值无后续解引用
# 点查入参双形态：网编号 int 与名字 str 同值
# 原断言：assert sp_basic.get_net_load(NET_ID["clk"]) == \
record("basic/clk_load_by_id_equals_by_name",
       sp_basic.get_net_load(NET_ID["clk"]) == sp_basic.get_net_load("clk"))
INFO(f"[DONE] sp_basic: {len(SMALL_NETS)} net(s) exact + misses are None")

obj, entries = _partition_entries(sp_basic, 0, 0)
# 原断言：assert obj.size == 15, obj.size
record("basic/part00_size", obj.size)
_assert_entries(entries, _BASIC_PART_0_0, "basic/part00")
# 原断言：assert list(iter_spef_partition(sp_basic)) == [(0, 0)]
record("basic/partition_list", list(iter_spef_partition(sp_basic)))
INFO("[DONE] sp_basic: PART_0_0 = 15 exact entries (net-id ascending)")

_assert_file(_file_stats(s_basic, "sp_small.spef", "basic"), "basic", 1.0,
             net_count=16, rnet_count=1, value_count_max=1,
             cap_count=40, coupling_cap_count=13, res_count=14,
             chunk_count=1, hit_count=15, net_name_miss_count=1)
_assert_summary(s_basic, "basic",
                routed_signal_net_count=16, covered_net_count=15,
                uncovered_net_count=1, uncovered_instance_count=1,
                zero_cap_instance_count=1, rnet_count=1,
                cap_mismatch_count=1, electrical_net_count=0)
# 原断言：assert abs(summary.cap_mismatch_max_rel - want) <= 1e-9 * want, \
record("basic/cap_mismatch_max_rel_match",
       abs(s_basic.cap_mismatch_max_rel - 1 / 46) <= 1e-9 * (1 / 46),
       detail=f"{s_basic.cap_mismatch_max_rel!r}")
# 原断言：assert s_basic.internal_cap_unit_ff == 1.0
record("basic/internal_cap_unit_1ff", s_basic.internal_cap_unit_ff == 1.0,
       detail=f"{s_basic.internal_cap_unit_ff!r}")
# 原断言：assert s_basic.internal_res_unit_ohm == 1.0
record("basic/internal_res_unit_1ohm", s_basic.internal_res_unit_ohm == 1.0,
       detail=f"{s_basic.internal_res_unit_ohm!r}")
INFO("[DONE] sp_basic: file stats + coverage + mismatch 1/46 exact")

# ═══ §3.2 sp_gzip：与明文逐项一致 + 解压副本清理 ═════════════════════

s_gz = load_spef_summary(sp_gzip)
fa, fb = s_gz.files[0], s_basic.files[0]
# 原断言：assert s_gz.files[0].source_file != s_basic.files[0].source_file
record("gzip/source_files_differ",
       s_gz.files[0].source_file != s_basic.files[0].source_file,
       detail=(s_gz.files[0].source_file, s_basic.files[0].source_file))
gzip_partition_diffs = []
for xp, yp in iter_spef_partition(sp_gzip):
    a = load_partition_net_load(sp_gzip, xp, yp).__getstate__()
    b = load_partition_net_load(sp_basic, xp, yp).__getstate__()
    if a != b:
        gzip_partition_diffs.append((xp, yp))
# 原断言：assert a == b, f"partition ({xp},{yp}) bytes differ"
record("gzip/partitions_byte_identical", not gzip_partition_diffs,
       detail=gzip_partition_diffs)
gz_summary_diffs = [field for field in _SUMMARY_FIELDS
                    + ("cap_mismatch_max_rel", "internal_cap_unit_ff",
                       "internal_res_unit_ohm")
                    if getattr(s_gz, field) != getattr(s_basic, field)]
# 原断言：assert getattr(s_gz, field) == getattr(s_basic, field), field
record("gzip/summary_fields_equal", not gz_summary_diffs,
       detail=gz_summary_diffs)
# 原断言：assert len(s_gz.files) == len(s_basic.files) == 1
record("gzip/files_count_matches_basic",
       len(s_gz.files) == len(s_basic.files) == 1,
       detail=(len(s_gz.files), len(s_basic.files)))
gz_file_diffs = [field for field in _FILE_FIELDS
                 if getattr(fa, field) != getattr(fb, field)]
# 原断言：assert getattr(fa, field) == getattr(fb, field), field
record("gzip/file_fields_equal", not gz_file_diffs, detail=gz_file_diffs)
# 原断言：assert abs(fa.c_unit_ff_per_unit - fb.c_unit_ff_per_unit) <= 1e-12
record("gzip/c_unit_ff_close", abs(fa.c_unit_ff_per_unit
                                   - fb.c_unit_ff_per_unit) <= 1e-12,
       detail=f"{fa.c_unit_ff_per_unit!r} vs {fb.c_unit_ff_per_unit!r}")
# 解压副本冻结前清理（汇总任务单点——库冻结时已删除）
# 原断言：assert not os.path.isdir(os.path.join(PROJ_PATH, "sp_gzip",
record("gzip/scratch_dir_cleaned",
       not os.path.isdir(os.path.join(PROJ_PATH, "sp_gzip", "sp_scratch")),
       detail=os.path.join(PROJ_PATH, "sp_gzip", "sp_scratch"))
INFO("[DONE] sp_gzip: byte-identical partitions, stats equal, "
     "scratch cleaned")

# ═══ §3.3 sp_dialect：皮法刻度 + 方言计数 ════════════════════════════

s_dialect = load_spef_summary(sp_dialect)
REL = 1e-9
for name, (total, ground, coupling) in DIALECT_NETS.items():
    rec = sp_dialect.get_net_load(name)
    _assert_load(rec, total, ground, coupling, f"dialect/{name}", rel=REL)
# 原断言：assert sp_dialect.get_net_load("n_xor") is None
record("dialect/n_xor_load", sp_dialect.get_net_load("n_xor"))
# 非守卫：仅检查未命中网返回空，返回值无后续解引用
# 原断言：assert sp_dialect.get_net_load("count[0]") is None
record("dialect/count0_load", sp_dialect.get_net_load("count[0]"))
# 非守卫：仅检查未命中网返回空，返回值无后续解引用
INFO("[DONE] sp_dialect: 8 net(s) converted from PF (rel 1e-9) + misses")

_assert_file(_file_stats(s_dialect, "sp_small_dialect.spef", "dialect"),
             "dialect", 1000.0,
             net_count=8, pnet_skip_count=2, broken_net_count=1,
             name_map_miss_count=1, multi_value_net_count=2,
             value_count_max=3, cap_count=20, coupling_cap_count=5,
             res_count=2, chunk_count=1, hit_count=8)
_assert_summary(s_dialect, "dialect",
                routed_signal_net_count=16, covered_net_count=8,
                uncovered_net_count=8, uncovered_instance_count=13)
INFO("[DONE] sp_dialect: file stats (broken=1, name_map_miss=1, "
     "pnet=2) + coverage")

# ═══ §3.4 sp_dialect_idx：value_index=2 覆盖与越界回退 ═══════════════

s_idx = load_spef_summary(sp_dialect_idx)
rec = sp_dialect_idx.get_net_load("n_b2")
_assert_load(rec, *DIALECT_IDX_N_B2, "idx/n_b2", rel=REL)
rec = sp_dialect_idx.get_net_load("n_and")
_assert_load(rec, 45, 30, 15, "idx/n_and", rel=REL)  # 两值形态下标越界回退缺省规则
settings = sp_dialect_idx.read_object("alpha_settings")
# 原断言：assert settings.chunk_size_mb == 256, settings.chunk_size_mb
record("idx/chunk_size_mb", settings.chunk_size_mb)
# 原断言：assert settings.value_index == 2, settings.value_index
record("idx/value_index", settings.value_index)
_assert_file(_file_stats(s_idx, "sp_small_dialect.spef", "idx"), "idx",
             1000.0,
             net_count=8, pnet_skip_count=2, broken_net_count=1,
             name_map_miss_count=1, multi_value_net_count=2,
             value_count_max=3, value_index_fallback_count=25,
             cap_count=20, coupling_cap_count=5, res_count=2,
             chunk_count=1, hit_count=8)
_assert_summary(s_idx, "idx",
                routed_signal_net_count=16, covered_net_count=8,
                uncovered_net_count=8, uncovered_instance_count=13,
                cap_mismatch_count=1)
# 原断言：assert abs(summary.cap_mismatch_max_rel - want) <= 1e-9 * want, \
record("idx/cap_mismatch_max_rel_match",
       abs(s_idx.cap_mismatch_max_rel - 0.1) <= 1e-9 * 0.1,
       detail=f"{s_idx.cap_mismatch_max_rel!r}")
INFO("[DONE] sp_dialect_idx: n_b2 takes value #2, fallback count 25, "
     "mismatch 0.1, alpha_settings persisted")

# ═══ §3.5 sp_rnet：缩减网形态 ════════════════════════════════════════

s_rnet = load_spef_summary(sp_rnet)
for name, (total, ground, coupling) in RNET_NETS.items():
    rec = sp_rnet.get_net_load(name)
    _assert_load(rec, total, ground, coupling, f"rnet/{name}")
    # 原断言：assert rec["reduced"] == (name in RNET_REDUCED), name
    record(f"rnet/{name}/reduced_matches",
           rec["reduced"] == (name in RNET_REDUCED), detail=rec["reduced"])
INFO("[DONE] sp_rnet: 8 net(s) exact, 4 reduced-flagged")

_assert_file(_file_stats(s_rnet, "sp_small_rnet.spef", "rnet"), "rnet", 1.0,
             net_count=8, rnet_count=4, multi_value_net_count=1,
             value_count_max=3, cap_count=6, coupling_cap_count=2,
             res_count=0, chunk_count=1, hit_count=8)
_assert_summary(s_rnet, "rnet",
                routed_signal_net_count=16, covered_net_count=8,
                uncovered_net_count=8, uncovered_instance_count=12,
                rnet_count=4)
INFO("[DONE] sp_rnet: file stats + coverage (rnet_count 4)")

# ═══ §3.6 sp_grid：多分区落点与覆盖率口径 ════════════════════════════

s_grid = load_spef_summary(sp_grid)
obj00, entries00 = _partition_entries(sp_grid, 0, 0)
obj20, entries20 = _partition_entries(sp_grid, 2, 0)
# 原断言：assert list(iter_spef_partition(sp_grid)) == [(0, 0), (2, 0)]
record("grid/partition_list", list(iter_spef_partition(sp_grid)))
# 原断言：assert obj00.size == 15, obj00.size
record("grid/part00_size", obj00.size)
# 原断言：assert obj20.size == 10, obj20.size
record("grid/part20_size", obj20.size)
_assert_entries(entries00, _BASIC_PART_0_0, "grid/part00")
_assert_entries(entries20, _GRID_PART_2_0, "grid/part20")
INFO("[DONE] sp_grid: PART_0_0 = 15 (12 attached + 3 completed), "
     "PART_2_0 = 10 attached")

ONLY_00 = ("n_cnt0_d", "n_cnt1_d", "n_b1", "n_cnt2_d", "n_b2")
BOTH_PARTS = ("count[0]", "count[1]", "count[2]", "count[3]",
              "n_cnt3_d", "clk", "rn", "clk_div2", "n_div_d", "n_and")
for name in ONLY_00:
    parts = sp_grid.get_net_load(name)["partitions"]
    # 原断言：assert sp_grid.get_net_load(name)["partitions"] == [(0, 0)], name
    record(f"grid/{name}/partitions_only_00", parts == [(0, 0)],
           detail=parts)
for name in BOTH_PARTS:
    rec = sp_grid.get_net_load(name)
    # 原断言：assert rec["partitions"] == [(0, 0), (2, 0)], (name, rec["partitions"])
    record(f"grid/{name}/partitions_both",
           rec["partitions"] == [(0, 0), (2, 0)], detail=rec["partitions"])
# 原断言：assert sp_grid.get_net_load("n_xor") is None
record("grid/n_xor_load", sp_grid.get_net_load("n_xor"))
# 非守卫：仅检查未命中网返回空，返回值无后续解引用
# 同一张网在两个分区里的数值相同（上面 _assert_entries 已逐条精确——
# 此处按网名对照两分区的三值再钉一次口径）
vals00 = {e[0]: e[1:4] for e in entries00}
vals20 = {e[0]: e[1:4] for e in entries20}
for name in BOTH_PARTS:
    nid = NET_ID[name]
    # 原断言：assert vals00[nid] == vals20[nid], (name, vals00[nid], vals20[nid])
    record(f"grid/{name}_values_same_both_parts",
           vals00[nid] == vals20[nid],
           detail=(vals00[nid], vals20[nid]))
INFO("[DONE] sp_grid: per-net partition placement + same values in "
     "both partitions")

_assert_summary(s_grid, "grid",
                routed_signal_net_count=23, covered_net_count=22,
                uncovered_net_count=1, uncovered_instance_count=1,
                zero_cap_instance_count=1, rnet_count=1,
                cap_mismatch_count=2)
# 原断言：assert abs(summary.cap_mismatch_max_rel - want) <= 1e-9 * want, \
record("grid/cap_mismatch_max_rel_match",
       abs(s_grid.cap_mismatch_max_rel - 1 / 46) <= 1e-9 * (1 / 46),
       detail=f"{s_grid.cap_mismatch_max_rel!r}")
INFO("[DONE] sp_grid: coverage 23/22/1 per-partition scope, mismatch "
     "counted per partition (= 2)")

# ═══ §3.7 sp_unrouted：未布线设计 ════════════════════════════════════

s_unrouted = load_spef_summary(sp_unrouted)
_assert_file(_file_stats(s_unrouted, "sp_small.spef", "unrouted"),
             "unrouted", 1.0,
             net_count=16, rnet_count=1, value_count_max=1,
             cap_count=40, coupling_cap_count=13, res_count=14,
             chunk_count=1, hit_count=0, net_name_miss_count=1,
             unrouted_net_count=15)
_assert_summary(s_unrouted, "unrouted", rnet_count=1)
obj, entries = _partition_entries(sp_unrouted, 0, 0)
# 原断言：assert obj.size == 0, obj.size
record("unrouted/part00_size", obj.size)
INFO("[DONE] sp_unrouted: 0 hit / 15 unrouted, empty PART_0_0, coverage 0")

# ═══ §3.8 sp_pg：电源网段跳过 ════════════════════════════════════════

s_pg = load_spef_summary(sp_pg)
_assert_file(_file_stats(s_pg, "sp_pg.spef", "pg"), "pg", 1.0,
             net_count=3, value_count_max=1, pg_net_skip_count=2,
             cap_count=4, coupling_cap_count=1, res_count=0,
             chunk_count=1, hit_count=1)
_assert_summary(s_pg, "pg",
                routed_signal_net_count=16, covered_net_count=1,
                uncovered_net_count=15, uncovered_instance_count=14)
# 原断言：assert sp_pg.get_net_load("VDD") is None
record("pg/vdd_load", sp_pg.get_net_load("VDD"))
# 非守卫：仅检查电源网段不入库，返回值无后续解引用
# 原断言：assert sp_pg.get_net_load("VSS") is None
record("pg/vss_load", sp_pg.get_net_load("VSS"))
# 非守卫：仅检查电源网段不入库，返回值无后续解引用
rec = sp_pg.get_net_load("clk")
_assert_load(rec, *PG_CLK, "pg/clk")
INFO("[DONE] sp_pg: VDD/VSS skipped (2), clk stored 33/22/11")

# ═══ §3.9 确定性：同输入双跑逐字节一致 ═══════════════════════════════


def _db_bytes(db):
    """db 正式对象逐字节指纹（summary + 全部分区对象 __getstate__）。"""
    parts = [load_spef_summary(db).__getstate__()]
    for xp, yp in iter_spef_partition(db):
        parts.append(load_partition_net_load(db, xp, yp).__getstate__())
    return parts


fp_ref = _db_bytes(sp_basic)
for tag, db in (("det_a", sp_det_a), ("det_b", sp_det_b)):
    fp = _db_bytes(db)
    # 原断言：assert len(fp) == len(fp_ref)
    record(f"det/{tag}_object_count", len(fp) == len(fp_ref),
           detail=(len(fp), len(fp_ref)))
    byte_diffs = [i for i, (a, b) in enumerate(zip(fp, fp_ref)) if a != b]
    # 原断言：assert a == b, f"object {i} bytes differ across identical runs"
    record(f"det/{tag}_objects_identical", not byte_diffs,
           detail=byte_diffs)
INFO("[DONE] determinism: sp_det_a / sp_det_b byte-identical to sp_basic")

# ═══ §3.10 SPMG 消息族按批计数（十个库一批的增量）════════════════════

_assert_msg_delta(msg_before, _spmg_counts(), "batch", FLOW_MSGS)
INFO("[DONE] SPMG message counts over the 10-db batch (§3.10)")

# ═══ §3.11 破损文件：库不冻结 + 失败原因 + 不影响消息计数 ════════════

sp_bad = proj.build_spef_db(
    name="sp_bad", spef_files=[os.path.join(DATA, "bad_header.spef")],
    design_db=d_small)
assert not proj.wait_frozen("sp_bad", timeout=300), \
    "broken spef must not freeze"
reason = proj.db_failure_reason("sp_bad")
# db_failure_reason 返回 (task_id, error) 元组或 None——error 是失败文案
# 原断言：assert reason is not None and "C_UNIT" in reason[1], reason
record("bad/reason_has_c_unit",
       reason is not None and "C_UNIT" in reason[1], detail=reason)
# 破损文件那一个不影响前面批次的 SPMG 消息计数
_assert_msg_delta(msg_before, _spmg_counts(), "bad_file", FLOW_MSGS)
INFO("[DONE] broken spef: db not frozen, reason carries C_UNIT, "
     "message counts untouched")

shutil.rmtree(os.path.dirname(GZ_PATH), ignore_errors=True)
shutil.rmtree(os.path.dirname(ROUTED_SMALL_DEF), ignore_errors=True)

INFO("[DONE] test_emir_spef_flow")
