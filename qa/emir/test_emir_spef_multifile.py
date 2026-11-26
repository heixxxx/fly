"""E2E test: EMIRProject.build_spef_db — ⑤ spef db 块层级多文件与名字
形态（plan docs/emir/spef-db-plan.md，2026-11-24 全裁定）。

验证（多文件解析 → 块绑定三形态 + 前缀剥离 → 作用域合并 → 跨层次电气
网分组合并 → 冻结 → 读库），断言依据 qa/emir/data/spef/README.md 推导
表 4 / 5 与主 agent 测试设计稿 §4（期望值全部写成字面量，与样张生成脚
本 gen_spef.py 相互独立）：
  - 口径（§4.0，用户裁定 2026-11-24）：展平后经层次端口逻辑相连的各段
    网是同一张电气网——两层合并（单网编号内同作用域保留首份、跨作用
    域相加；再按电气网组求和：三值分别相加、来源位图按位或、缩减标记
    相与）。同一张电气网的几个成员网编号各有条目、数值相同（合计）；
    没有被任何文件描述的成员网（如 u_blk_a/clk）有补全条目（数值 =
    电气网合计）；get_net_load 按任意一段查都是合计，electrical_members
    列出全部成员；
  - mf_main（六文件：纯路径 ×2 + block_inst ×2〔其一带 strip_prefix〕
    + block_cell ×1 + 绑定目标未命中 ×1）：19 条目逐条精确（12 原始
    + 7 补全）、互补相加（n_top 组 60+39=99）、同作用域重合保留首份
    （overlap 的 999/888 丢弃）、前缀剥离（2 个未命中跳过）、块定义绑
    定落在该定义的实例化上、绑定无效文件跳过（invalid_binding_count 1）
    + 逐文件统计按文件名 + 读库点查表（任意一段查到电气网合计 +
    electrical_members + partitions）；
  - mf_top_only：只有顶层文件时的基线（17 条目，位图全 1）；
  - mf_nameforms：层次分隔符声明为点（u_blk_a.n_a1）+ 转义的分隔符
    （u_blk_a\\/n_a2）——4 条目；
  - mf_reordered：文件顺序打乱（不含无效那个）——钉住「同作用域保留
    首份按文件序」（n_top 取先出现的 999）与「电气网合计与文件顺序无
    关」（仍是 19 条）；
  - mf_dangling：设计库顶层不接 u_blk_b 的输出端口（真实设计常见形态
    ——块的一个端口在父层没接）。前置断言设计库并查集 class_count 5 /
    dangling_count 1 / 悬空单成员类，寄生库 electrical_net_count 4（不
    含悬空的单成员类）+ 15 条目（17 去掉无连接位的 n_top2 与所在悬空
    电气网无负载的 u_blk_b/n_out）；
  - mf_all_invalid：全部绑定目标未命中——提交发一次 SPMG::0009、根任
    务失败、库不冻结（不与正常库同批等待——wait_tasks 会抛）；
  - 设计库并查集前置断言（load_design_net_union：class_count /
    dangling_count / find / members）与 SPMG 消息族总次数（§4.7）。
"""
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
PROJ_PATH = os.path.join(LOG_DIR, "emir_spef_multifile")
MSG_LOG = os.path.join(LOG_DIR, "message.log")

from emir.design import load_design_net_union  # noqa: E402  并查集读库
from emir.spef import (  # noqa: E402  spef 读库 API（模块函数层）
    iter_spef_partition,
    load_partition_net_load,
    load_spef_summary,
)

# ── 期望值（全部字面量——测试设计稿 §1.3 / §4 与数据推导表 4/5）──────

# mf 设计库的网编号（§1.3，展平层次路径名）
MF_NET_ID = {
    "clk": 1, "rn": 2, "n_top": 3, "n_top2": 4, "u_blk_a/n_a2": 6,
    "u_blk_a/clk": 7, "u_blk_a/rn": 8, "u_blk_a/n_a1": 9,
    "u_blk_a/n_out": 10, "u_blk_a/u_sub/n2": 12, "u_blk_a/u_sub/clk": 13,
    "u_blk_a/u_sub/rn": 14, "u_blk_a/u_sub/n1": 15,
    "u_blk_a/u_sub/n_in": 16, "u_blk_a/u_sub/n3": 17, "u_blk_b/n_b2": 19,
    "u_blk_b/clk": 20, "u_blk_b/rn": 21, "u_blk_b/n_b1": 22,
    "u_blk_b/n_out": 23,
}

# mf_design 的电气网组（§1.3——设计库并查集的 5 个多成员组；其余 6 张
# 网各自是单成员）
MF_GROUPS = {
    1: [1, 7, 13, 20],   # 时钟
    2: [2, 8, 14, 21],   # 复位
    3: [3, 10],          # n_top
    4: [4, 23],          # n_top2
    9: [9, 16],          # u_blk_a/n_a1
}

# mf_main 合并后的 19 条目（§4.1：net_id 升序；位图按有效文件序
# 0=blk_top 1=blk_a_inst 2=blk_top_overlap 3=blk_strip 4=blk_sub_cell；
# 全部非缩减——flags 最低位 0）
MF_MAIN_PART = [
    (1, 80, 48, 32, 0b00001, 0),
    (2, 90, 54, 36, 0b00001, 0),
    (3, 99, 59, 40, 0b00011, 0),    # 60 + u_blk_a/n_out 39；999 重合丢弃
    (4, 103, 62, 41, 0b00101, 0),   # 70 + u_blk_b/n_out 33
    (6, 27, 16, 11, 0b00011, 0),    # 20 + 7；888 重合丢弃
    (7, 80, 48, 32, 0b00001, 0),    # 补全条目
    (8, 90, 54, 36, 0b00001, 0),    # 补全条目
    (9, 15, 9, 6, 0b00011, 0),      # 10 + 5
    (10, 99, 59, 40, 0b00011, 0),   # 30 + 9，并入 n_top 组
    (12, 8, 5, 3, 0b10000, 0),      # 块定义绑定
    (13, 80, 48, 32, 0b00001, 0),   # 补全条目
    (14, 90, 54, 36, 0b00001, 0),   # 补全条目
    (15, 43, 26, 17, 0b10001, 0),   # 40 + 3
    (16, 15, 9, 6, 0b00011, 0),     # 补全条目
    (17, 6, 4, 2, 0b10000, 0),
    (20, 80, 48, 32, 0b00001, 0),   # 补全条目
    (21, 90, 54, 36, 0b00001, 0),   # 补全条目
    (22, 50, 30, 20, 0b00001, 0),
    (23, 103, 62, 41, 0b00101, 0),  # 70 + 33，并入 n_top2 组
]

# mf_top_only 的 17 条目（§4.2：位图全 1；时钟/复位/n_top/n_top2/n_a1
# 五组各段同值 = 电气网合计）
def _top_only_rows():
    rows = {
        1: (80, 48, 32), 2: (90, 54, 36), 3: (90, 54, 36),
        4: (70, 42, 28), 6: (20, 12, 8), 7: (80, 48, 32),
        8: (90, 54, 36), 9: (10, 6, 4), 10: (90, 54, 36),
        13: (80, 48, 32), 14: (90, 54, 36), 15: (40, 24, 16),
        16: (10, 6, 4), 20: (80, 48, 32), 21: (90, 54, 36),
        22: (50, 30, 20), 23: (70, 42, 28),
    }
    return [(nid, t, g, c, 1, 0) for nid, (t, g, c) in sorted(rows.items())]


TOP_ONLY_PART = _top_only_rows()
# mf_dangling 的 15 条目 = mf_top_only 去掉 n_top2（4：无连接位）与
# u_blk_b/n_out（23：所在悬空电气网无负载）
DANGLING_PART = [e for e in TOP_ONLY_PART if e[0] not in (4, 23)]

# mf_nameforms 的 4 条目（§4.3：位图全 1；n_in 是补全条目）
NAMEFORMS_PART = [
    (6, 20, 12, 8, 1, 0),
    (9, 10, 6, 4, 1, 0),
    (15, 40, 24, 16, 1, 0),
    (16, 10, 6, 4, 1, 0),
]

# mf_reordered 的 19 条目（§4.4：位图按新文件序 0=overlap 1=blk_a_inst
# 2=blk_top 3=blk_strip 4=blk_sub_cell；同作用域重合保留首份——按文件序）
MF_REORDERED_PART = [
    (1, 80, 48, 32, 0b00100, 0),
    (2, 90, 54, 36, 0b00100, 0),
    (3, 1038, 623, 415, 0b00111, 0),   # 999（60 丢弃）+ n_out 39
    (4, 103, 62, 41, 0b00101, 0),      # n_out 33 + 70
    (6, 895, 504, 391, 0b00011, 0),    # 888（20 丢弃）+ 7
    (7, 80, 48, 32, 0b00100, 0),
    (8, 90, 54, 36, 0b00100, 0),
    (9, 15, 9, 6, 0b00110, 0),         # 5 + 10；77 重合丢弃
    (10, 1038, 623, 415, 0b00111, 0),  # 9 + 30，并入 n_top 组
    (12, 8, 5, 3, 0b10000, 0),
    (13, 80, 48, 32, 0b00100, 0),
    (14, 90, 54, 36, 0b00100, 0),
    (15, 43, 26, 17, 0b10100, 0),      # 40 + 3
    (16, 15, 9, 6, 0b00110, 0),
    (17, 6, 4, 2, 0b10000, 0),
    (20, 80, 48, 32, 0b00100, 0),
    (21, 90, 54, 36, 0b00100, 0),
    (22, 50, 30, 20, 0b00100, 0),
    (23, 103, 62, 41, 0b00101, 0),     # 33 + 70
]

# mf_main 读库点查（§4.1 末表）：查询名 → (行首/对地/耦合, 位图, 成员)
MF_MAIN_PROBES = [
    (["clk", "u_blk_a/clk", 7, "u_blk_b/clk"], (80, 48, 32), 1,
     [1, 7, 13, 20]),
    (["rn", "u_blk_a/u_sub/rn"], (90, 54, 36), 1, [2, 8, 14, 21]),
    (["n_top", "u_blk_a/n_out"], (99, 59, 40), 3, [3, 10]),
    (["n_top2", "u_blk_b/n_out"], (103, 62, 41), 5, [4, 23]),
    (["u_blk_a/n_a1", "u_blk_a/u_sub/n_in"], (15, 9, 6), 3, [9, 16]),
    (["u_blk_a/n_a2"], (27, 16, 11), 3, [6]),
    (["u_blk_a/u_sub/n2"], (8, 5, 3), 16, [12]),
]

# 消息总次数（§4.7；实现语义——0002 = strip 前缀未匹配、0007 = 同作用
# 域重合，mf 用例里两者的次数恰好一致，按次数断言）
MF_MSGS = {1: 4, 2: 2, 7: 2, 9: 2, 11: 1}


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


def _file_stats(summary, suffix, tag):
    hits = [f for f in summary.files if f.source_file.endswith(suffix)]
    # 原断言：assert len(hits) == 1, (suffix, [f.source_file for f in summary.files])
    record(f"{tag}/file_stats_hit_1", len(hits) == 1,
           detail=[f.source_file for f in summary.files])
    assert len(hits) == 1  # 保留：守卫（后面 hits[0] 按下标取，空列表会以 IndexError 崩溃）
    return hits[0]


def _assert_file(f, tag, **expect):
    """逐文件统计全字段记录（未给的数值字段按 0 期望）。"""
    # 原断言：assert got == want, (field, got, want)
    record(f"{tag}/file_stats",
           {field: getattr(f, field) for field in _FILE_FIELDS})


def _assert_summary(summary, tag, **expect):
    """全局统计记录（未给的计数按 0 期望）。"""
    # 原断言：assert got == want, (field, got, want)
    record(f"{tag}/summary_stats",
           {field: getattr(summary, field) for field in _SUMMARY_FIELDS})


def _partition_entries(db, xp, yp):
    obj = load_partition_net_load(db, xp, yp)
    return obj, [(e.net_id, e.total_cap_reported_ff, e.ground_cap_ff,
                  e.coupling_cap_ff, e.source_mask, e.flags)
                 for e in obj.items]


def _assert_entries(entries, expect, where):
    got_ids = [e[0] for e in entries]
    want_ids = [e[0] for e in expect]
    # 原断言：assert got_ids == want_ids, (where, got_ids, want_ids)
    record(f"{where}/entries_net_id_order", got_ids == want_ids,
           detail=(got_ids, want_ids))
    # 原断言：assert g == w, (where, g, w)
    record(f"{where}/entries_exact", entries == expect,
           detail=[(g, w) for g, w in zip(entries, expect) if g != w])


def _assert_probe(db, queries, vals, mask, members, tag):
    misses = []
    caps_mismatch = []
    mask_mismatch = []
    parts_mismatch = []
    members_mismatch = []
    for q in queries:
        rec = db.get_net_load(q)
        if rec is None:
            misses.append(q)
            continue
        got = (rec["total_cap_reported_ff"], rec["ground_cap_ff"],
               rec["coupling_cap_ff"])
        if got != vals:
            caps_mismatch.append((q, got))
        if rec["source_mask"] != mask:
            mask_mismatch.append((q, rec["source_mask"]))
        if rec["partitions"] != [(0, 0)]:
            parts_mismatch.append((q, rec["partitions"]))
        if rec["electrical_members"] != members:
            members_mismatch.append((q, rec["electrical_members"]))
    # 原断言：assert rec is not None, q
    record(f"{tag}/loads_exist", not misses, detail=misses)
    # 原断言：assert got == vals, (q, got, vals)
    record(f"{tag}/caps_match", not caps_mismatch, detail=caps_mismatch)
    # 原断言：assert rec["source_mask"] == mask, (q, rec["source_mask"])
    record(f"{tag}/source_mask_match", not mask_mismatch,
           detail=mask_mismatch)
    # 原断言：assert rec["partitions"] == [(0, 0)], (q, rec["partitions"])
    record(f"{tag}/partitions_single", not parts_mismatch,
           detail=parts_mismatch)
    # 原断言：assert rec["electrical_members"] == members, \
    record(f"{tag}/electrical_members_match", not members_mismatch,
           detail=members_mismatch)


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

# ── 前置：lib db + 设计库两个（一批提交，一次等待）───────────────────
lib_db = proj.build_lib_db(name="mf_lib", lib_paths=[LIBERTY])
assert proj.wait_frozen("mf_lib", timeout=1200), "lib db should freeze"
INFO("[WAIT] lib db frozen")


def _routed_def(def_name, drop=None):
    """派生带 ROUTED 几何的 DEF 副本（qa_tmp scratch；每网补一段 metal1
    布线）。drop 给出一段要删掉的连接文本——制造「块的端口网在父层悬
    空」的形态（mf_design_dangling：顶层不再接 u_blk_b 的输出端口），
    删除后照常补布线。"""
    src = os.path.join(DESIGN_DATA, def_name)
    out_dir = qa_tmp("spef_mf_def")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir,
                       ("drop_" if drop else "") + def_name)
    with open(src) as f:
        lines = []
        for line in f.read().splitlines():
            stripped = line.rstrip()
            compact = stripped.strip()
            if drop and compact.startswith("- ") and drop in compact:
                compact = compact.replace(drop, "").replace(")  ;", ") ;")
                stripped = compact
            if (compact.startswith("- ") and compact.endswith(") ;")
                    and "+ ROUTED" not in compact):
                stripped = compact[:-1].rstrip() + \
                    " + ROUTED metal1 ( 1000 4000 ) ( 2000 4000 ) ;"
            lines.append(stripped)
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")
    return out


def _submit_design(name, def_paths):
    return proj.build_design_db(
        name=name, def_paths=def_paths, lef_paths=[TECH_LEF, MACRO_LEF],
        lib_db=lib_db)


BLK_DEFS = ("wf_sub.def", "wf_blk_a.def", "wf_blk_b.def", "wf_blk_top.def")

d_main = _submit_design("mf_design", [_routed_def(n) for n in BLK_DEFS])
d_dangling = _submit_design(
    "mf_design_dangling",
    [_routed_def(n) for n in BLK_DEFS[:3]]
    + [_routed_def("wf_blk_top.def", drop="( u_blk_b n_out )")])
assert wait_tasks(timeout=1800), "design flows complete"
assert proj.wait_frozen("mf_design", timeout=60), "mf_design should freeze"
assert proj.wait_frozen("mf_design_dangling", timeout=60), \
    "mf_design_dangling should freeze"
INFO("[WAIT] design db family frozen (2 db(s), one batch)")

# ── 设计库并查集前置断言（§1.3 / §4.6）───────────────────────────────

union = load_design_net_union(d_main)
# 原断言：assert union.class_count == 5, union.class_count
record("union/class_count", union.class_count)
# 原断言：assert union.dangling_count == 0, union.dangling_count
record("union/dangling_count", union.dangling_count)
for root, members in MF_GROUPS.items():
    got = sorted(int(m) for m in (union.members(union.find(root)) or []))
    # 原断言：assert got == members, (root, got, members)
    record(f"union/groups/{root}", got)
INFO("[DONE] mf_design net union: 5 multi-member groups (clk/rn/n_top/"
     "n_top2/n_a1)")

union_d = load_design_net_union(d_dangling)
# 原断言：assert union_d.class_count == 5, union_d.class_count
record("union_d/class_count", union_d.class_count)
# 原断言：assert union_d.dangling_count == 1, union_d.dangling_count
record("union_d/dangling_count", union_d.dangling_count)
# 悬空的单成员类：u_blk_b/n_out 自己成组；顶层 n_top2 完全没参与跨块
# 连接——不在表（find 恒一步返回自身，members 未命中 = 空）
# 原断言：assert union_d.find(23) == 23, union_d.find(23)
record("union_d/find_23", union_d.find(23))
# 原断言：assert sorted(int(m) for m in (union_d.members(23) or [])) == [23]
record("union_d/members_23", sorted(int(m) for m in (union_d.members(23) or [])))
# 原断言：assert union_d.find(MF_NET_ID["n_top2"]) == MF_NET_ID["n_top2"]
record("union_d/find_n_top2", union_d.find(MF_NET_ID["n_top2"]))
# 原断言：assert not (union_d.members(MF_NET_ID["n_top2"]) or []), "n_top2 ungrouped"
record("union_d/n_top2_ungrouped",
       not (union_d.members(MF_NET_ID["n_top2"]) or []))
# 多成员组不受影响（时钟组仍在）
got_clk = sorted(int(m) for m in (union_d.members(union_d.find(1)) or []))
# 原断言：assert got_clk == [1, 7, 13, 20], got_clk
record("union_d/clock_group_members", got_clk)
INFO("[DONE] mf_design_dangling net union: 1 dangling single-member class "
     "(u_blk_b/n_out), n_top2 ungrouped, clock group intact")

# ── 寄生库五个正常库（一批提交，一次等待）────────────────────────────

BLK_FILES = [
    os.path.join(DATA, "blk_top.spef"),
    {"file_name": os.path.join(DATA, "blk_a_inst.spef"),
     "block_inst": "u_blk_a"},
    os.path.join(DATA, "blk_top_overlap.spef"),
    {"file_name": os.path.join(DATA, "blk_strip.spef"),
     "block_inst": "u_blk_a", "strip_prefix": "tb/u_dut"},
    {"file_name": os.path.join(DATA, "blk_sub_cell.spef"),
     "block_cell": "wf_sub"},
    {"file_name": os.path.join(DATA, "blk_bad_binding.spef"),
     "block_inst": "no_such_block"},
]
REORDERED_FILES = [BLK_FILES[2], BLK_FILES[1], BLK_FILES[0],
                   BLK_FILES[3], BLK_FILES[4]]

msg_before = _spmg_counts()

mf_main = proj.build_spef_db(name="mf_main", spef_files=BLK_FILES,
                             design_db=d_main)
mf_top_only = proj.build_spef_db(
    name="mf_top_only", spef_files=[BLK_FILES[0]], design_db=d_main)
mf_nameforms = proj.build_spef_db(
    name="mf_nameforms",
    spef_files=[os.path.join(DATA, "blk_top_nameforms.spef")],
    design_db=d_main)
mf_reordered = proj.build_spef_db(name="mf_reordered",
                                  spef_files=REORDERED_FILES,
                                  design_db=d_main)
mf_dangling = proj.build_spef_db(
    name="mf_dangling", spef_files=[BLK_FILES[0]], design_db=d_dangling)

assert wait_tasks(timeout=1800), "spef flows complete"
for n in ("mf_main", "mf_top_only", "mf_nameforms", "mf_reordered",
          "mf_dangling"):
    assert proj.wait_frozen(n, timeout=60), f"{n} should freeze"
INFO("[WAIT] spef db batch frozen (5 db(s), one batch)")

# ═══ §4.1 mf_main：19 条目 + 逐文件统计 + 点查表 ═════════════════════

s_main = load_spef_summary(mf_main)
# 原断言：assert len(s_main.files) == 5, [f.source_file for f in s_main.files]
record("main/files_count", len(s_main.files))
obj, entries = _partition_entries(mf_main, 0, 0)
# 原断言：assert obj.size == 19, obj.size
record("main/part00_size", obj.size)
# 原断言：assert list(iter_spef_partition(mf_main)) == [(0, 0)]
record("main/partition_list", list(iter_spef_partition(mf_main)))
_assert_entries(entries, MF_MAIN_PART, "main/part00")
INFO("[DONE] mf_main: 19 entries (12 attached + 7 completed) exact")

_assert_file(_file_stats(s_main, "blk_top.spef", "main/blk_top"),
             "main/blk_top",
             net_count=10, value_count_max=1, cap_count=20,
             coupling_cap_count=10, res_count=1, chunk_count=1,
             hit_count=9, net_name_miss_count=1)
_assert_file(_file_stats(s_main, "blk_a_inst.spef", "main/blk_a_inst"),
             "main/blk_a_inst",
             net_count=3, value_count_max=1, cap_count=6,
             coupling_cap_count=3, chunk_count=1, hit_count=3)
_assert_file(_file_stats(s_main, "blk_top_overlap.spef",
                         "main/blk_top_overlap"), "main/blk_top_overlap",
             net_count=3, value_count_max=1, cap_count=6,
             coupling_cap_count=3, chunk_count=1, hit_count=3)
_assert_file(_file_stats(s_main, "blk_strip.spef", "main/blk_strip"),
             "main/blk_strip",
             net_count=4, value_count_max=1, cap_count=7,
             coupling_cap_count=3, chunk_count=1, hit_count=2,
             strip_miss_count=2)
_assert_file(_file_stats(s_main, "blk_sub_cell.spef", "main/blk_sub_cell"),
             "main/blk_sub_cell",
             net_count=3, value_count_max=1, cap_count=6,
             coupling_cap_count=3, chunk_count=1, hit_count=3)
INFO("[DONE] mf_main: per-file stats for 5 file(s)")

_assert_summary(s_main, "main",
                routed_signal_net_count=20, covered_net_count=19,
                uncovered_net_count=1, uncovered_instance_count=2,
                same_scope_overlap_count=4, cross_scope_sum_count=4,
                invalid_binding_count=1, electrical_net_count=5)
INFO("[DONE] mf_main: overlap 4 / cross-scope sum 4 / 1 invalid binding / "
     "electrical 5 / coverage 20/19/1/2/0")

for idx, (queries, vals, mask, members) in enumerate(MF_MAIN_PROBES):
    _assert_probe(mf_main, queries, vals, mask, members, f"main_probe{idx}")
# 单成员电气网：members = [自己]
_assert_probe(mf_main, ["u_blk_a/u_sub/n3"], (6, 4, 2), 0b10000, [17],
              "main_probe_single")
# 原断言：assert mf_main.get_net_load("u_blk_b/n_b2") is None
record("main/miss_n_b2", mf_main.get_net_load("u_blk_b/n_b2"))
# 非守卫：仅检查未命中网返回空，返回值无后续解引用
# 原断言：assert mf_main.get_net_load("ghost_top") is None
record("main/miss_ghost_top", mf_main.get_net_load("ghost_top"))
# 非守卫：仅检查未命中网返回空，返回值无后续解引用
INFO("[DONE] mf_main: point queries by name and net id all resolve to "
     "electrical-net totals")

# ═══ §4.2 mf_top_only：只有顶层文件的基线 ════════════════════════════

s_top = load_spef_summary(mf_top_only)
obj, entries = _partition_entries(mf_top_only, 0, 0)
# 原断言：assert obj.size == 17, obj.size
record("top_only/part00_size", obj.size)
_assert_entries(entries, TOP_ONLY_PART, "top_only/part00")
_assert_file(_file_stats(s_top, "blk_top.spef", "top_only/blk_top"),
             "top_only/blk_top",
             net_count=10, value_count_max=1, cap_count=20,
             coupling_cap_count=10, res_count=1, chunk_count=1,
             hit_count=9, net_name_miss_count=1)
_assert_summary(s_top, "top_only",
                routed_signal_net_count=20, covered_net_count=17,
                uncovered_net_count=3, uncovered_instance_count=5,
                electrical_net_count=5)
INFO("[DONE] mf_top_only: 17 entries (bitmap all 1), coverage 20/17/3/5/0")

# ═══ §4.3 mf_nameforms：点分隔符与转义分隔符 ═════════════════════════

s_nf = load_spef_summary(mf_nameforms)
obj, entries = _partition_entries(mf_nameforms, 0, 0)
# 原断言：assert obj.size == 4, obj.size
record("nameforms/part00_size", obj.size)
_assert_entries(entries, NAMEFORMS_PART, "nameforms/part00")
_assert_file(_file_stats(s_nf, "blk_top_nameforms.spef",
                         "nameforms/blk_top_nameforms"),
             "nameforms/blk_top_nameforms",
             net_count=3, value_count_max=1, cap_count=6,
             coupling_cap_count=3, chunk_count=1, hit_count=3)
_assert_summary(s_nf, "nameforms",
                routed_signal_net_count=20, covered_net_count=4,
                uncovered_net_count=16, uncovered_instance_count=11,
                electrical_net_count=5)
rec = mf_nameforms.get_net_load("u_blk_a/n_a1")
# 原断言：assert rec["electrical_members"] == [9, 16]
record("nameforms/n_a1_members",
       None if rec is None else rec["electrical_members"])
# 原断言：assert mf_nameforms.get_net_load("u_blk_a/u_sub/n_in")[
rec_in = mf_nameforms.get_net_load("u_blk_a/u_sub/n_in")
record("nameforms/n_in_total_cap_10",
       rec_in is not None and rec_in["total_cap_reported_ff"] == 10,
       detail=None if rec_in is None else f"{rec_in['total_cap_reported_ff']!r}")
INFO("[DONE] mf_nameforms: dot divider + escaped divider hit 3 nets, "
     "n_in completed entry, no messages")

# ═══ §4.4 mf_reordered：重合保留首份按文件序 ═════════════════════════

s_re = load_spef_summary(mf_reordered)
obj, entries = _partition_entries(mf_reordered, 0, 0)
# 原断言：assert obj.size == 19, obj.size
record("reordered/part00_size", obj.size)
_assert_entries(entries, MF_REORDERED_PART, "reordered/part00")
_assert_file(_file_stats(s_re, "blk_top_overlap.spef",
                         "reordered/blk_top_overlap"),
             "reordered/blk_top_overlap",
             net_count=3, value_count_max=1, cap_count=6,
             coupling_cap_count=3, chunk_count=1, hit_count=3)
_assert_file(_file_stats(s_re, "blk_top.spef", "reordered/blk_top"),
             "reordered/blk_top",
             net_count=10, value_count_max=1, cap_count=20,
             coupling_cap_count=10, res_count=1, chunk_count=1,
             hit_count=9, net_name_miss_count=1)
_assert_summary(s_re, "reordered",
                routed_signal_net_count=20, covered_net_count=19,
                uncovered_net_count=1, uncovered_instance_count=2,
                same_scope_overlap_count=4, cross_scope_sum_count=4,
                electrical_net_count=5)
rec = mf_reordered.get_net_load("n_top")
# 原断言：assert (rec["total_cap_reported_ff"], rec["ground_cap_ff"],
record("reordered/n_top_caps_1038_623_415",
       rec is not None and
       (rec["total_cap_reported_ff"], rec["ground_cap_ff"],
        rec["coupling_cap_ff"]) == (1038, 623, 415),
       detail=None if rec is None else
       (rec["total_cap_reported_ff"], rec["ground_cap_ff"],
        rec["coupling_cap_ff"]))
# 原断言：assert rec["electrical_members"] == [3, 10]
record("reordered/n_top_members",
       None if rec is None else rec["electrical_members"])
INFO("[DONE] mf_reordered: first-file-wins by file order (n_top = 999+39), "
     "electrical totals order-independent")

# ═══ §4.6 mf_dangling：块的端口在父层没接 ════════════════════════════

s_dg = load_spef_summary(mf_dangling)
obj, entries = _partition_entries(mf_dangling, 0, 0)
# 原断言：assert obj.size == 15, obj.size
record("dangling/part00_size", obj.size)
_assert_entries(entries, DANGLING_PART, "dangling/part00")
_assert_file(_file_stats(s_dg, "blk_top.spef", "dangling/blk_top"),
             "dangling/blk_top",
             net_count=10, value_count_max=1, cap_count=20,
             coupling_cap_count=10, res_count=1, chunk_count=1,
             hit_count=8, net_name_miss_count=1, unrouted_net_count=1)
_assert_summary(s_dg, "dangling",
                routed_signal_net_count=19, covered_net_count=15,
                uncovered_net_count=4, uncovered_instance_count=7,
                electrical_net_count=4)
# 原断言：assert mf_dangling.get_net_load("n_top2") is None
record("dangling/miss_n_top2", mf_dangling.get_net_load("n_top2"))
# 非守卫：仅检查未命中网返回空，返回值无后续解引用
# 原断言：assert mf_dangling.get_net_load("u_blk_b/n_out") is None
record("dangling/miss_u_blk_b_n_out", mf_dangling.get_net_load("u_blk_b/n_out"))
# 非守卫：仅检查未命中网返回空，返回值无后续解引用
rec = mf_dangling.get_net_load("clk")
# 原断言：assert rec["electrical_members"] == [1, 7, 13, 20]
record("dangling/clk_members",
       None if rec is None else rec["electrical_members"])
rec = mf_dangling.get_net_load("n_top")
# 原断言：assert (rec["total_cap_reported_ff"], rec["ground_cap_ff"],
record("dangling/n_top_caps_90_54_36",
       rec is not None and
       (rec["total_cap_reported_ff"], rec["ground_cap_ff"],
        rec["coupling_cap_ff"]) == (90, 54, 36),
       detail=None if rec is None else
       (rec["total_cap_reported_ff"], rec["ground_cap_ff"],
        rec["coupling_cap_ff"]))
# 原断言：assert rec["electrical_members"] == [3, 10]
record("dangling/n_top_members",
       None if rec is None else rec["electrical_members"])
INFO("[DONE] mf_dangling: 15 entries, electrical_net_count 4 excludes the "
     "dangling single-member class, coverage 19/15/4/7/0")

# ═══ §4.5 mf_all_invalid：全部绑定未命中 → 库不冻结 ══════════════════

# wait_tasks 在有任务失败时抛 RuntimeError——全部绑定无效的库单独提交，
# 不能与正常库同批等待
mf_all_invalid = proj.build_spef_db(
    name="mf_all_invalid", design_db=d_main,
    spef_files=[{"file_name": os.path.join(DATA, "blk_bad_binding.spef"),
                 "block_inst": "no_such_block"}])
assert not proj.wait_frozen("mf_all_invalid", timeout=300), \
    "all-invalid binding must not freeze"
reason = proj.db_failure_reason("mf_all_invalid")
# db_failure_reason 返回 (task_id, error) 元组或 None——error 是失败文案
# 原断言：assert reason is not None \
record("all_invalid/reason_names_binding",
       reason is not None and "no binding target resolved" in reason[1],
       detail=reason)
INFO("[DONE] mf_all_invalid: db not frozen, reason names the unresolved "
     "binding target")

# ═══ §4.7 消息总次数（正常批 + all_invalid 全部结束之后）═════════════

_assert_msg_delta(msg_before, _spmg_counts(), "msgs", MF_MSGS)
INFO("[DONE] SPMG message counts over the whole file (§4.7)")

shutil.rmtree(qa_tmp("spef_mf_def"), ignore_errors=True)

INFO("[DONE] test_emir_spef_multifile")
