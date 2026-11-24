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
  - 日志：无 DSGN::0004（pg_pin 组命中）、无 DSGN::0025（通配展开后连
    接全有效）、DSGN::0009 汇总行含「2 wildcard entries expanded to
    4 connections」；
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
assert tree.node_count == 1, f"hier nodes={tree.node_count}"
lo, hi = tree.node(0).instance_range
assert hi - lo == 3, f"inst range={lo, hi} (2 inst + placeholder)"
lo, hi = tree.node(0).net_range
assert hi - lo == 5, f"net range={lo, hi} (4 nets + local-0 hole)"
assert design_db.convert_to_id(inst="u1") == 1
assert design_db.convert_to_id(inst="u2") == 2
assert design_db.convert_to_id(pin="A") == 0
assert design_db.convert_to_id(pin="ZN") == 1
assert design_db.convert_to_id(pin="VDD") == 2
assert design_db.convert_to_id(pin="VSS") == 3
assert design_db.convert_to_id(pin="st[0]") == 4
assert design_db.convert_to_id(pin="st[1]") == 5
INFO("[OK] id layout: 2 instances, 4 nets, pin ids A=0/ZN=1/VDD=2/"
     "VSS=3/st[0]=4/st[1]=5")

# ── 电源网通配展开：全局 pg 集 + NETS_PG 对象 ────────────────────────
from emir.design import load_design_pg_nets
pg = load_design_pg_nets(design_db)
assert pg.power_count == 1, f"power={pg.power_count}"
assert pg.ground_count == 1, f"ground={pg.ground_count}"
assert pg.is_power(3) is True and pg.is_ground(4) is True
assert pg.is_pg(1) is False and pg.is_pg(2) is False

from emir.design import DesignDb, load_partition
geo, geo_pg, inst, iconn, nets, nets_pg = load_partition(design_db, 0, 0)
assert nets_pg.size == 2 and sorted(nets_pg.ids) == [3, 4], \
    f"NETS_PG={list(nets_pg.ids)} (wildcard-expanded pg nets)"
vdd_pg = nets_pg.net_of(3)
assert vdd_pg is not None and vdd_pg.use == "POWER" \
    and vdd_pg.connection_count == 2, "VDD carries both instances"
vss_pg = nets_pg.net_of(4)
assert vss_pg is not None and vss_pg.use == "GROUND" \
    and vss_pg.connection_count == 2
assert nets.size == 2 and sorted(nets.ids) == [1, 2]
INFO("[OK] pg nets: power=1/ground=1, NETS_PG={3,4} non-empty, signal "
     "side {1,2} disjoint")

# ── get_net 概要：pg 网连接数与 flags 位（pg 数据保护无明细）─────────
vdd = design_db.get_net("VDD")
assert vdd is not None and vdd["id"] == 3 and vdd["use"] == "POWER" \
    and vdd["is_pg"] is True
assert vdd["connection_count"] == 2, vdd
assert vdd["hybrid_count"] == 2 and vdd["power_count"] == 2, vdd
assert "connections" not in vdd, "pg net must not return details"
vss = design_db.get_net("VSS")
assert vss is not None and vss["id"] == 4 and vss["use"] == "GROUND"
assert vss["connection_count"] == 2 and vss["hybrid_count"] == 2 \
    and vss["ground_count"] == 2
INFO("[OK] get_net VDD/VSS: 2 wildcard-expanded conns each, hybrid+power/"
     "hybrid+ground bits")

# ── get_instance：每实例的 VDD/VSS 引脚连到对应电源网 ────────────────
for name, sig_net, sig_pin in (("u1", 1, "A"), ("u2", 2, "ZN")):
    inst_info = design_db.get_instance(name)
    assert inst_info is not None and inst_info["cell_name"] == "INV2"
    conns = {(c["net_id"], c["pin_name"]) for c in inst_info["connections"]}
    assert (3, "VDD") in conns, (name, conns)
    assert (4, "VSS") in conns, (name, conns)
    assert (sig_net, sig_pin) in conns, (name, conns)
    assert len(inst_info["connections"]) == 3, (name, conns)
    for c in inst_info["connections"]:
        if c["net_id"] == 3:
            assert c["pin_name"] == "VDD" and c["is_power"] \
                and c["is_driver"] and c["is_receiver"], c
        if c["net_id"] == 4:
            assert c["pin_name"] == "VSS" and c["is_ground"] \
                and c["is_driver"] and c["is_receiver"], c
INFO("[OK] get_instance: u1/u2 VDD→VDD net (power bit), VSS→VSS net "
     "(ground bit), signal pin intact")

# ── 名字还原：真实名字命中、转义原文查不到（网 + 端口）───────────────
n0 = design_db.get_net("ctl.st[0]")
assert n0 is not None and n0["id"] == 1 and n0["use"] == "SIGNAL"
assert n0["connection_count"] == 2 and n0["receiver_count"] == 2 \
    and n0["driver_count"] == 0
assert [(c["pin_name"], c["is_port"]) for c in n0["connections"]] == [
    ("st[0]", True), ("A", False)], n0["connections"]
n1 = design_db.get_net("ctl.st[1]")
assert n1 is not None and n1["id"] == 2
assert n1["connection_count"] == 2 and n1["driver_count"] == 2
assert [(c["pin_name"], c["is_port"]) for c in n1["connections"]] == [
    ("st[1]", True), ("ZN", False)]
assert design_db.get_net("ctl\\.st\\[0\\]") is None, \
    "escaped source form must not be stored"
assert design_db.get_net("ctl\\.st\\[1\\]") is None
assert design_db.convert_to_id(net="ctl.st[0]") == 1
assert design_db.convert_to_id(net="ctl\\.st\\[0\\]") is None
assert design_db.convert_to_name(net=1) == "ctl.st[0]"
# 端口名（convert 双向按真实名字；转义原文不进 pin 名字空间）
assert design_db.convert_to_name(pin=4) == "st[0]"
assert design_db.convert_to_name(pin=5) == "st[1]"
assert design_db.convert_to_id(pin="st\\[0\\]") is None
assert design_db.convert_to_id(pin="st\\[1\\]") is None
INFO("[OK] unescaping: nets/ports stored and queried by real names, "
     "escaped source forms absent")

# ── 校验报告（S10 手算：连接 16 = NETS 4 + NETS_PG 4 + INST_CONN 8
#    （u1 3 + u2 3 + root 自身 port 条目 2）；图形 4 = 信号 2 wire +
#    pg 2 wire）─────────────────────────────────────────────────────
from emir.design import load_design_verify_report
report = load_design_verify_report(design_db)
assert report.union_inconsistency == "" and report.coverage_gap == "" \
    and report.namemap_inconsistency == ""
assert report.total_primary == 2 and report.total_instances == 2
assert report.total_nets == 4, report.total_nets
assert report.total_connections == 16, report.total_connections
assert report.total_geometry_entries == 4, report.total_geometry_entries
assert report.total_crossing_nets == 0
INFO("[OK] verify report: 2 instances / 4 nets / 16 connections / 4 "
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
assert "DSGN::0004" not in msgs, \
    "pg_pin-only liberty must not trigger pin-mismatch warnings"
assert "DSGN::0025" not in msgs, \
    "wildcard expansion must not drop '*' as an unknown instance"
assert "DSGN::0009" in msgs
assert "2 wildcard entries expanded to 4 connections" in msgs, \
    "DSGN::0009 must report wildcard stats"
INFO("[OK] messages: no DSGN::0004 / DSGN::0025, wildcard stats in "
     "DSGN::0009")

get_agent().stop()
print("[PASS] test_emir_design_pg_wildcard")
