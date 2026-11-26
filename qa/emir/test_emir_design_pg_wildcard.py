"""E2E test: design db 真实布线工具产出三形态兼容（2026-11-24 修复回归）。

数据 data/design/pg_wild/pg_wild.def 复刻真实工具（OpenROAD gcd 产出）的三个
特征：电源网只写通配连接 `( * VDD )` 不写端口连接；网名带转义
（`ctl\.st\[0\]`，真实名字 = `ctl.st[0]`）；端口名带转义（`st\[0\]` →
`st[0]`）。配套 cells.lef 的 INV2 有 VDD/VSS 引脚、cells.lib 只以
pg_pin 组声明电源地引脚（Nangate45 形态——DSGN::0004 比对认 pg_pin 组）。

断言（恒基准 DBU 1000、DEF UNITS 1000 → ×1，全手算）：
  - 全局 id 面：S2 汇总 cells.lef 的 INV2 四引脚按序 A=0/ZN=1/VDD=2/
    VSS=3，S4 端口接续 st[0]=4/st[1]=5；实例 u1=1/u2=2；网
    ctl.st[0]=1/ctl.st[1]=2/VDD=3/VSS=4（root 块 global = local）；
  - 电源网通配展开：power_count=1、ground_count=1、NETS_PG = {3,4}；
    get_net("VDD"/"VSS") 各 2 条连接（两实例各一条，hybrid + power /
    hybrid + ground 位）；get_instance 两实例的 VDD/VSS 引脚确实连到
    对应电源网；
  - 名字还原：get_net("ctl.st[0]")/("ctl.st[1]") 命中且转义原文形态返
    回 None；端口 convert 双向按真实名字（转义原文查不到）；
  - 端口条目按网的角色标记（2026-11-24 用户裁定）：ctl.st[0] 连着输入
    端口 st[0]（= 它所在网的驱动者，driver 位）+ u1/A（receiver）→
    驱动 1、接收 1；ctl.st[1] 连着输出端口 st[1]（= 负载，receiver
    位）+ u2/ZN（driver）→ 驱动 1、接收 1；连接表按 (实例, 引脚) 升序
    ——端口条目（块自身编号 0）在前，连接顺序断言不变；
  - 日志：无 DSGN::0004（pg_pin 组命中）、无 DSGN::0025（通配展开后连
    接全有效）、DSGN::0009 汇总行含「2 wildcard entries expanded to
    4 connections」；
  - 重复连接条目（连接表唯一性裁定）：case 内派生一个网写两条完全相同
    显式条目的 DEF 副本建库——连接数不变（数据结构保证唯一）、DSGN::
    0028 发一次、DSGN::0009 汇总行末尾「1 duplicate connections removed」；
  - 校验报告：2 实例 / 4 网 / 连接 16（NETS 4 + NETS_PG 4 +
    INST_CONNECTIONS 8 = u1 3 + u2 3 + root 自身 port 条目 2）/
    图形 4（信号 2 wire + pg 2 wire）。
"""
import os
import shutil

from log import INFO

from fly import get_config, launch_workers
from fly.runtime import get_agent
from emir import EMIRProject
from test import record

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(SCRIPT_DIR, "data", "design", "pg_wild")

LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "emir_design_pg_wildcard")


def cleanup():
    if os.path.isdir(PROJ_PATH):
        shutil.rmtree(PROJ_PATH, ignore_errors=True)


cleanup()

launch_workers([{}, {}])
assert get_agent().wait_workers_registered(timeout=60), "workers connect"

proj = EMIRProject(PROJ_PATH)
lib_db = proj.build_lib_db(name="wild_lib",
                           lib_paths=[os.path.join(DATA, "cells.lib")])
assert proj.wait_frozen("wild_lib", timeout=120), "lib freeze"

design_db = proj.build_design_db(
    name="wild_design",
    def_paths=[os.path.join(DATA, "pg_wild.def")],
    lef_paths=[os.path.join(DATA, "tech.lef"), os.path.join(DATA,
                                                           "cells.lef")],
    lib_db=lib_db,
)
assert proj.wait_frozen("wild_design", timeout=180), "design freeze"
INFO("[WAIT] wild_design frozen (wildcard PG + escaped names)")

# ── 全局 id 面（手算锁定，见 docstring 推导）─────────────────────────
design = design_db.load_design()
tree = design.get_hier_tree()
# 原断言：assert tree.node_count == 1, f"hier nodes={tree.node_count}"
record("id/tree_node_count", tree.node_count)
assert tree.node_count >= 1  # 保留：守卫（后面 tree.node(0) 取根节点，越界在 C++ 侧是未定义行为）
lo, hi = tree.node(0).instance_range
# 原断言：assert hi - lo == 3, f"inst range={lo, hi} (2 inst + placeholder)"
record("id/inst_range_span", hi - lo)
lo, hi = tree.node(0).net_range
# 原断言：assert hi - lo == 5, f"net range={lo, hi} (4 nets + local-0 hole)"
record("id/net_range_span", hi - lo)
# 原断言：assert design_db.convert_to_id(inst="u1") == 1
record("id/inst_u1", design_db.convert_to_id(inst="u1"))
# 原断言：assert design_db.convert_to_id(inst="u2") == 2
record("id/inst_u2", design_db.convert_to_id(inst="u2"))
# 原断言：assert design_db.convert_to_id(pin="A") == 0
record("id/pin_A", design_db.convert_to_id(pin="A"))
# 原断言：assert design_db.convert_to_id(pin="ZN") == 1
record("id/pin_ZN", design_db.convert_to_id(pin="ZN"))
# 原断言：assert design_db.convert_to_id(pin="VDD") == 2
record("id/pin_VDD", design_db.convert_to_id(pin="VDD"))
# 原断言：assert design_db.convert_to_id(pin="VSS") == 3
record("id/pin_VSS", design_db.convert_to_id(pin="VSS"))
# 原断言：assert design_db.convert_to_id(pin="st[0]") == 4
record("id/pin_st[0]", design_db.convert_to_id(pin="st[0]"))
# 原断言：assert design_db.convert_to_id(pin="st[1]") == 5
record("id/pin_st[1]", design_db.convert_to_id(pin="st[1]"))
INFO("[DONE] id layout: 2 instances, 4 nets, pin ids A=0/ZN=1/VDD=2/"
     "VSS=3/st[0]=4/st[1]=5")

# ── 电源网通配展开：全局 pg 集 + NETS_PG 对象 ────────────────────────
from emir.design import load_design_pg_nets
pg = load_design_pg_nets(design_db)
# 原断言：assert pg.power_count == 1, f"power={pg.power_count}"
record("pg/power_count", pg.power_count)
# 原断言：assert pg.ground_count == 1, f"ground={pg.ground_count}"
record("pg/ground_count", pg.ground_count)
# 原断言：assert pg.is_power(3) is True and pg.is_ground(4) is True
record("pg/net3_is_power", pg.is_power(3) is True)
# 原断言：assert pg.is_power(3) is True and pg.is_ground(4) is True
record("pg/net4_is_ground", pg.is_ground(4) is True)
# 原断言：assert pg.is_pg(1) is False and pg.is_pg(2) is False
record("pg/net1_not_pg", pg.is_pg(1) is False)
# 原断言：assert pg.is_pg(1) is False and pg.is_pg(2) is False
record("pg/net2_not_pg", pg.is_pg(2) is False)

from emir.design import DesignDb, load_partition
geo, geo_pg, inst, iconn, nets, nets_pg = load_partition(design_db, 0, 0)
# 原断言：assert nets_pg.size == 2 and sorted(nets_pg.ids) == [3, 4], \
record("pg/nets_pg_size", nets_pg.size)
# 原断言：assert nets_pg.size == 2 and sorted(nets_pg.ids) == [3, 4], \
record("pg/nets_pg_ids", sorted(nets_pg.ids))
vdd_pg = nets_pg.net_of(3)
# 原断言：assert vdd_pg is not None and vdd_pg.use == "POWER" \
record("pg/vdd_pg_exists", vdd_pg is not None)
assert vdd_pg is not None  # 保留：守卫（后面 vdd_pg.use、vdd_pg.connection_count 解引用）
# 原断言：assert vdd_pg is not None and vdd_pg.use == "POWER" \
record("pg/vdd_pg_use", vdd_pg.use)
# 原断言：assert vdd_pg is not None and vdd_pg.use == "POWER" \
record("pg/vdd_pg_connection_count", vdd_pg.connection_count)
vss_pg = nets_pg.net_of(4)
# 原断言：assert vss_pg is not None and vss_pg.use == "GROUND" \
record("pg/vss_pg_exists", vss_pg is not None)
assert vss_pg is not None  # 保留：守卫（后面 vss_pg.use、vss_pg.connection_count 解引用）
# 原断言：assert vss_pg is not None and vss_pg.use == "GROUND" \
record("pg/vss_pg_use", vss_pg.use)
# 原断言：assert vss_pg is not None and vss_pg.use == "GROUND" \
record("pg/vss_pg_connection_count", vss_pg.connection_count)
# 原断言：assert nets.size == 2 and sorted(nets.ids) == [1, 2]
record("pg/signal_nets_size", nets.size)
# 原断言：assert nets.size == 2 and sorted(nets.ids) == [1, 2]
record("pg/signal_nets_ids", sorted(nets.ids))
INFO("[DONE] pg nets: power=1/ground=1, NETS_PG={3,4} non-empty, signal "
     "side {1,2} disjoint")

# ── get_net 概要：pg 网连接数与 flags 位（pg 数据保护无明细）─────────
vdd = design_db.get_net("VDD")
# 原断言：assert vdd is not None and vdd["id"] == 3 and vdd["use"] == "POWER" \
record("net/vdd_exists", vdd is not None)
assert vdd is not None  # 保留：守卫（后面 vdd["id"] 等按下标取）
# 原断言：assert vdd is not None and vdd["id"] == 3 and vdd["use"] == "POWER" \
record("net/vdd_id", vdd["id"])
# 原断言：assert vdd is not None and vdd["id"] == 3 and vdd["use"] == "POWER" \
record("net/vdd_use", vdd["use"])
# 原断言：assert vdd is not None and vdd["id"] == 3 and vdd["use"] == "POWER" \
record("net/vdd_is_pg", vdd["is_pg"] is True)
# 原断言：assert vdd["connection_count"] == 2, vdd
record("net/vdd_connection_count", vdd["connection_count"])
# 原断言：assert vdd["hybrid_count"] == 2 and vdd["power_count"] == 2, vdd
record("net/vdd_hybrid_count", vdd["hybrid_count"])
# 原断言：assert vdd["hybrid_count"] == 2 and vdd["power_count"] == 2, vdd
record("net/vdd_power_count", vdd["power_count"])
# 原断言：assert "connections" not in vdd, "pg net must not return details"
record("net/vdd_keys", sorted(vdd))
vss = design_db.get_net("VSS")
# 原断言：assert vss is not None and vss["id"] == 4 and vss["use"] == "GROUND"
record("net/vss_exists", vss is not None)
assert vss is not None  # 保留：守卫（后面 vss["id"] 等按下标取）
# 原断言：assert vss is not None and vss["id"] == 4 and vss["use"] == "GROUND"
record("net/vss_id", vss["id"])
# 原断言：assert vss is not None and vss["id"] == 4 and vss["use"] == "GROUND"
record("net/vss_use", vss["use"])
# 原断言：assert vss["connection_count"] == 2 and vss["hybrid_count"] == 2 \
record("net/vss_connection_count", vss["connection_count"])
# 原断言：assert vss["connection_count"] == 2 and vss["hybrid_count"] == 2 \
record("net/vss_hybrid_count", vss["hybrid_count"])
# 原断言：assert vss["connection_count"] == 2 and vss["hybrid_count"] == 2 \
record("net/vss_ground_count", vss["ground_count"])
INFO("[DONE] get_net VDD/VSS: 2 wildcard-expanded conns each, hybrid+power/"
     "hybrid+ground bits")

# ── get_instance：每实例的 VDD/VSS 引脚连到对应电源网 ────────────────
for name, sig_net, sig_pin in (("u1", 1, "A"), ("u2", 2, "ZN")):
    inst_info = design_db.get_instance(name)
    # 原断言：assert inst_info is not None and inst_info["cell_name"] == "INV2"
    record(f"inst/{name}_exists", inst_info is not None)
    assert inst_info is not None  # 保留：守卫（后面 inst_info["cell_name"] 等按下标取）
    # 原断言：assert inst_info is not None and inst_info["cell_name"] == "INV2"
    record(f"inst/{name}_cell_name", inst_info["cell_name"])
    conns = {(c["net_id"], c["pin_name"]) for c in inst_info["connections"]}
    # 原断言：assert (3, "VDD") in conns, (name, conns)
    record(f"inst/{name}_vdd_conn_present", (3, "VDD") in conns,
           detail=sorted(conns))
    # 原断言：assert (4, "VSS") in conns, (name, conns)
    record(f"inst/{name}_vss_conn_present", (4, "VSS") in conns,
           detail=sorted(conns))
    # 原断言：assert (sig_net, sig_pin) in conns, (name, conns)
    record(f"inst/{name}_signal_conn_present", (sig_net, sig_pin) in conns,
           detail=sorted(conns))
    # 原断言：assert len(inst_info["connections"]) == 3, (name, conns)
    record(f"inst/{name}_connection_total", len(inst_info["connections"]))
    vdd_flag_issues = []
    vss_flag_issues = []
    for c in inst_info["connections"]:
        if c["net_id"] == 3 and not (c["pin_name"] == "VDD" and c["is_power"]
                                     and c["is_driver"] and c["is_receiver"]):
            vdd_flag_issues.append(c)
        if c["net_id"] == 4 and not (c["pin_name"] == "VSS" and c["is_ground"]
                                     and c["is_driver"] and c["is_receiver"]):
            vss_flag_issues.append(c)
    # 原断言：assert c["pin_name"] == "VDD" and c["is_power"] \
    record(f"inst/{name}_vdd_conn_flags", not vdd_flag_issues,
           detail=vdd_flag_issues)
    # 原断言：assert c["pin_name"] == "VSS" and c["is_ground"] \
    record(f"inst/{name}_vss_conn_flags", not vss_flag_issues,
           detail=vss_flag_issues)
INFO("[DONE] get_instance: u1/u2 VDD→VDD net (power bit), VSS→VSS net "
     "(ground bit), signal pin intact")

# ── 名字还原 + 端口条目按网的角色标记（2026-11-24 用户裁定）─────────
# ctl.st[0]：输入端口 st[0]（= 它所在网的驱动者，driver 位）+ u1/A
# （receiver）→ 驱动 1、接收 1；ctl.st[1]：输出端口 st[1]（= 负载，
# receiver 位）+ u2/ZN（driver）→ 驱动 1、接收 1。连接表按 (实例, 引
# 脚) 升序——端口条目（块自身编号 0）在前，顺序断言与改造前一致。
n0 = design_db.get_net("ctl.st[0]")
# 原断言：assert n0 is not None and n0["id"] == 1 and n0["use"] == "SIGNAL"
record("names/n0_exists", n0 is not None)
assert n0 is not None  # 保留：守卫（后面 n0["id"] 等按下标取）
# 原断言：assert n0 is not None and n0["id"] == 1 and n0["use"] == "SIGNAL"
record("names/n0_id", n0["id"])
# 原断言：assert n0 is not None and n0["id"] == 1 and n0["use"] == "SIGNAL"
record("names/n0_use", n0["use"])
# 原断言：assert n0["connection_count"] == 2 and n0["receiver_count"] == 1 \
record("names/n0_connection_count", n0["connection_count"])
assert n0["connection_count"] >= 2  # 保留：守卫（后面 n0["connections"][0]、[1] 按下标取，越界在 C++ 侧是未定义行为）
# 原断言：assert n0["connection_count"] == 2 and n0["receiver_count"] == 1 \
record("names/n0_receiver_count", n0["receiver_count"])
# 原断言：assert n0["connection_count"] == 2 and n0["receiver_count"] == 1 \
record("names/n0_driver_count", n0["driver_count"])
# 原断言：assert [(c["pin_name"], c["is_port"]) for c in n0["connections"]] == [
record("names/n0_pin_port_seq",
       [(c["pin_name"], c["is_port"]) for c in n0["connections"]])
# 原断言：assert n0["connections"][0]["is_driver"] \
record("names/n0_conn0_driver_only",
       n0["connections"][0]["is_driver"]
       and not n0["connections"][0]["is_receiver"],
       detail=n0["connections"][0])
# 原断言：assert n0["connections"][1]["is_receiver"] \
record("names/n0_conn1_receiver_only",
       n0["connections"][1]["is_receiver"]
       and not n0["connections"][1]["is_driver"],
       detail=n0["connections"][1])
n1 = design_db.get_net("ctl.st[1]")
# 原断言：assert n1 is not None and n1["id"] == 2
record("names/n1_exists", n1 is not None)
assert n1 is not None  # 保留：守卫（后面 n1["id"] 等按下标取）
# 原断言：assert n1 is not None and n1["id"] == 2
record("names/n1_id", n1["id"])
# 原断言：assert n1["connection_count"] == 2 and n1["driver_count"] == 1 \
record("names/n1_connection_count", n1["connection_count"])
assert n1["connection_count"] >= 2  # 保留：守卫（后面 n1["connections"][0]、[1] 按下标取，越界在 C++ 侧是未定义行为）
# 原断言：assert n1["connection_count"] == 2 and n1["driver_count"] == 1 \
record("names/n1_driver_count", n1["driver_count"])
# 原断言：assert n1["connection_count"] == 2 and n1["driver_count"] == 1 \
record("names/n1_receiver_count", n1["receiver_count"])
# 原断言：assert [(c["pin_name"], c["is_port"]) for c in n1["connections"]] == [
record("names/n1_pin_port_seq",
       [(c["pin_name"], c["is_port"]) for c in n1["connections"]])
# 原断言：assert n1["connections"][0]["is_receiver"] \
record("names/n1_conn0_receiver_only",
       n1["connections"][0]["is_receiver"]
       and not n1["connections"][0]["is_driver"],
       detail=n1["connections"][0])
# 原断言：assert n1["connections"][1]["is_driver"] \
record("names/n1_conn1_driver_only",
       n1["connections"][1]["is_driver"]
       and not n1["connections"][1]["is_receiver"],
       detail=n1["connections"][1])
# 原断言：assert design_db.get_net("ctl\\.st\\[0\\]") is None, \
record("names/escaped_net0_query", design_db.get_net("ctl\\.st\\[0\\]"))
# 非守卫：仅检查转义原文不落库，返回值无后续解引用
# 原断言：assert design_db.get_net("ctl\\.st\\[1\\]") is None
record("names/escaped_net1_query", design_db.get_net("ctl\\.st\\[1\\]"))
# 非守卫：仅检查转义原文不落库，返回值无后续解引用
# 原断言：assert design_db.convert_to_id(net="ctl.st[0]") == 1
record("names/net_id_real", design_db.convert_to_id(net="ctl.st[0]"))
# 原断言：assert design_db.convert_to_id(net="ctl\\.st\\[0\\]") is None
record("names/net_id_escaped", design_db.convert_to_id(net="ctl\\.st\\[0\\]"))
# 非守卫：仅检查转义原文不进 id 名字空间，返回值无后续解引用
# 原断言：assert design_db.convert_to_name(net=1) == "ctl.st[0]"
record("names/net_name_by_id", design_db.convert_to_name(net=1))
# 端口名（convert 双向按真实名字；转义原文不进 pin 名字空间）
# 原断言：assert design_db.convert_to_name(pin=4) == "st[0]"
record("names/pin_name_4", design_db.convert_to_name(pin=4))
# 原断言：assert design_db.convert_to_name(pin=5) == "st[1]"
record("names/pin_name_5", design_db.convert_to_name(pin=5))
# 原断言：assert design_db.convert_to_id(pin="st\\[0\\]") is None
record("names/pin_id_escaped0", design_db.convert_to_id(pin="st\\[0\\]"))
# 非守卫：仅检查转义原文不进 pin id 名字空间，返回值无后续解引用
# 原断言：assert design_db.convert_to_id(pin="st\\[1\\]") is None
record("names/pin_id_escaped1", design_db.convert_to_id(pin="st\\[1\\]"))
# 非守卫：仅检查转义原文不进 pin id 名字空间，返回值无后续解引用
INFO("[DONE] unescaping: nets/ports stored and queried by real names, "
     "escaped source forms absent")

# ── 校验报告（S10 手算：连接 16 = NETS 4 + NETS_PG 4 + INST_CONN 8
#    （u1 3 + u2 3 + root 自身 port 条目 2）；图形 4 = 信号 2 wire +
#    pg 2 wire）─────────────────────────────────────────────────────
from emir.design import load_design_verify_report
report = load_design_verify_report(design_db)
# 原断言：assert report.union_inconsistency == "" and report.coverage_gap == "" \
record("verify/union_inconsistency", report.union_inconsistency)
# 原断言：assert report.union_inconsistency == "" and report.coverage_gap == "" \
record("verify/coverage_gap", report.coverage_gap)
# 原断言：assert report.union_inconsistency == "" and report.coverage_gap == "" \
record("verify/namemap_inconsistency", report.namemap_inconsistency)
# 原断言：assert report.total_primary == 2 and report.total_instances == 2
record("verify/total_primary", report.total_primary)
# 原断言：assert report.total_primary == 2 and report.total_instances == 2
record("verify/total_instances", report.total_instances)
# 原断言：assert report.total_nets == 4, report.total_nets
record("verify/total_nets", report.total_nets)
# 原断言：assert report.total_connections == 16, report.total_connections
record("verify/total_connections", report.total_connections)
# 原断言：assert report.total_geometry_entries == 4, report.total_geometry_entries
record("verify/total_geometry_entries", report.total_geometry_entries)
# 原断言：assert report.total_crossing_nets == 0
record("verify/total_crossing_nets", report.total_crossing_nets)
INFO("[DONE] verify report: 2 instances / 4 nets / 16 connections / 4 "
     "geometry entries (hand-computed)")

# ── 日志：无 DSGN::0004（pg_pin 组命中）、无 DSGN::0025（通配展开后
#    连接全有效）、DSGN::0009 通配统计行 ──────────────────────────────
msgs = ""
for root, _dirs, files in os.walk(LOG_DIR):
    for fn in files:
        if fn.endswith(".log"):
            try:
                with open(os.path.join(root, fn), errors="ignore") as fh:
                    msgs += fh.read()
            except OSError:
                pass
# 原断言：assert "DSGN::0004" not in msgs, \
record("msg/no_dsgn0004", "DSGN::0004" not in msgs,
       detail=msgs.count("DSGN::0004"))
# 原断言：assert "DSGN::0025" not in msgs, \
record("msg/no_dsgn0025", "DSGN::0025" not in msgs,
       detail=msgs.count("DSGN::0025"))
# 原断言：assert "DSGN::0009" in msgs
record("msg/dsgn0009_present", "DSGN::0009" in msgs)
# 原断言：assert "2 wildcard entries expanded to 4 connections" in msgs, \
record("msg/dsgn0009_wildcard_stats",
       "2 wildcard entries expanded to 4 connections" in msgs)
INFO("[DONE] messages: no DSGN::0004 / DSGN::0025, wildcard stats in "
     "DSGN::0009")

# ── 重复连接条目（连接表唯一性裁定）：派生副本网写两条相同显式条目 ────
# ctl.st[0] 网重复写一条 ( u1 A )（DEF 重复显式条目——上游工具当集合处
# 理）：连接数不变（DSConnList 封存去重，数据结构保证唯一）、DSGN::0028
# 发一次（数据质量信号）、DSGN::0009 行末尾 duplicate 计数 1。
DUP_DIR = os.path.join(LOG_DIR, "emir_design_pg_wild_dup")
os.makedirs(DUP_DIR, exist_ok=True)
DUP_DEF = os.path.join(DUP_DIR, "pg_wild_dup.def")
with open(os.path.join(DATA, "pg_wild.def")) as f:
    dup_text = f.read()
OLD_NET = ("    - ctl\\.st\\[0\\] ( PIN st\\[0\\] ) ( u1 A )\n"
           "      + ROUTED M1 ( 100 100 ) ( 500 500 ) ;\n")
NEW_NET = ("    - ctl\\.st\\[0\\] ( PIN st\\[0\\] ) ( u1 A ) ( u1 A )\n"
           "      + ROUTED M1 ( 100 100 ) ( 500 500 ) ;\n")
# 保留：测试数据形态前置检查——pg_wild.def 网语句形态漂移时派生副本失真，后续核对全部失去意义
assert OLD_NET in dup_text, "pg_wild.def net statement shape drifted"
with open(DUP_DEF, "w") as f:
    f.write(dup_text.replace(OLD_NET, NEW_NET))

dup_db = proj.build_design_db(
    name="wild_design_dup",
    def_paths=[DUP_DEF],
    lef_paths=[os.path.join(DATA, "tech.lef"), os.path.join(DATA,
                                                           "cells.lef")],
    lib_db=lib_db,
)
assert proj.wait_frozen("wild_design_dup", timeout=180), "dup design freeze"
dup_n0 = dup_db.get_net("ctl.st[0]")
# 原断言：assert dup_n0 is not None and dup_n0["connection_count"] == 2 \
record("dup/n0_exists", dup_n0 is not None)
assert dup_n0 is not None  # 保留：守卫（后面 dup_n0["connection_count"] 等按下标取）
# 原断言：assert dup_n0 is not None and dup_n0["connection_count"] == 2 \
record("dup/n0_connection_count", dup_n0["connection_count"])
# 原断言：assert dup_n0 is not None and dup_n0["connection_count"] == 2 \
record("dup/n0_driver_count", dup_n0["driver_count"])
# 原断言：assert dup_n0 is not None and dup_n0["connection_count"] == 2 \
record("dup/n0_receiver_count", dup_n0["receiver_count"])
with open(os.path.join(LOG_DIR, "message.log"), errors="replace") as f:
    dup_msgs = f.read()
# 原断言：assert dup_msgs.count("DSGN::0028") == 1, \
record("dup/dsgn0028_count", dup_msgs.count("DSGN::0028"))
# 原断言：assert "1 duplicate connections removed" in dup_msgs, \
record("dup/duplicate_count_in_msg",
       "1 duplicate connections removed" in dup_msgs)
INFO("[DONE] duplicate explicit entries: connection count unchanged, "
     "DSGN::0028 once, DSGN::0009 duplicate count")
shutil.rmtree(DUP_DIR, ignore_errors=True)

get_agent().stop()
INFO("[DONE] test_emir_design_pg_wildcard")
