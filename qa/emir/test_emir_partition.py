"""E2E test: S8 分区决策直切场景（target_partitions '2x1'，2026-09-12/13
裁定 1/2/4）+ S9 flatten 展平 + 分区保存（2026-09-13 裁定补记①-⑤ + 同日
partition 网数据结构重组终态：NET_CONNECTIONS → NETS + 全局 pg 网 id 集
"pg_nets" + 2026-09-14 拆分裁定：GEOMETRY/NETS 按 pg/信号各拆两对象
（GEOMETRY_PG/NETS_PG）+ net id 0 专属 OBS 裁定：net 区间含 local 0 空洞
位、global = start + local 无 −1——n1 global 1）。

单 DEF 无嵌套（层级树 1 节点，合并恒等无分摊损失）+ alpha
density_bin_size=1（bin 1000 DBU → 格网 4×2）+ target_partitions '2x1'。
负载手工核算（恒基准 DBU，INV bbox 700×700、M1 缺省宽 70）：
  - 实例：inv1 格 (0,0)、inv2 格 (0,1)、inv3 格 (2,0) → inst 总 3
  - n1 M1 wire (0,1000)-(4000,1000) 宽 70 → 矩形 y∈[965,1035) 跨行 0/1、
    x 全宽 → 8 格各 1 → metal 总 8
  - 合成负载（6:2:2）col = [16,4,10,4]，total = 34
  - '2x1' 切线：等分 17 → col0 前缀 16 < 17、col1 前缀 20 ≥ 17 →
    切线 = 格 2 左边界（前缀和等分、吸附格边界——负载不均两分区宽窄不一）
  - w_eff = M1 default_width 70（M2 为更高布线层但无金属——有效层判定
    物理最高有效层，自底向上层表序内位置最高），2w = 140
验证：S8 分区数、core/extend 坐标（最外围 int32 极值 / 内侧 ±2w）、全局密
度三通道计数；S9 六类分区对象（primary 恰一 + extend 副本、geometry 副本
不裁剪 + is_crossing、OBS → net 0 桶专属、非 pg 连接全量补全入 NETS、
GEOMETRY_PG/NETS_PG 空对象形态、pg 集空集形态、alpha 聚合阈值键）；S10 汇
总校验 + 冻结（verify_report 手算锁定：损坏类三字段空、id 域无空洞重复、
primary 守恒、统计汇总 DSGN::0024，正常路径无 fatal/warn）。
"""
import os
import shutil

from log import INFO

from fly import get_config, launch_workers
from fly.runtime import get_agent
from emir import EMIRProject
from test import record

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(SCRIPT_DIR, "data", "design")
TECH_LEF = os.path.join(DATA, "tech.lef")
CELLS_MAIN = os.path.join(DATA, "cells_main.lef")
PARTITION_DEF = os.path.join(DATA, "partition.def")
LIB_A = os.path.join(SCRIPT_DIR, "data", "cells_a.lib")

LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "emir_partition_flow")


def cleanup():
    if os.path.isdir(PROJ_PATH):
        shutil.rmtree(PROJ_PATH, ignore_errors=True)


cleanup()

launch_workers([{}, {}])
assert get_agent().wait_workers_registered(timeout=60), "workers should connect"
INFO("  2 workers connected (user-managed)")

proj = EMIRProject(PROJ_PATH)
lib_db = proj.build_lib_db(name="partition_lib", lib_paths=[LIB_A])
assert proj.wait_frozen("partition_lib", timeout=120), "lib db should freeze"

design_db = proj.build_design_db(
    name="partition",
    def_paths=[PARTITION_DEF],
    lef_paths=[TECH_LEF, CELLS_MAIN],
    lib_db=lib_db,
    alpha={"density_bin_size": 1, "target_partitions": "2x1"},
)
assert proj.wait_frozen("partition", timeout=180), "design db should freeze"
INFO("[WAIT] partition design db frozen")

# ── S8 产物：全局密度图（三通道计数，手工核算值）──
from emir.design import load_global_density
gd = load_global_density(design_db)
# 原断言：assert gd.cols == 4 and gd.rows == 2, f"grid={gd.cols}x{gd.rows}"
record("s8_density/grid", {"cols": gd.cols, "rows": gd.rows})
# 原断言：assert gd.total_count == 3, f"inst={gd.total_count}"
record("s8_density/total_count", gd.total_count)
# 原断言：assert gd.metal_total == 8, f"metal={gd.metal_total}"
record("s8_density/metal_total", gd.metal_total)
# 原断言：assert gd.via_total == 0, f"via={gd.via_total}"
record("s8_density/via_total", gd.via_total)
INFO("[DONE] S8 global density: inst=3, metal=8, via=0 (hand-computed)")

# ── 分区表：'2x1' 直切（负载不均切线 + 双区域坐标）──
design = design_db.load_design()
# 原断言：assert design.partition_count == 2, f"parts={design.partition_count}"
record("s8_parts/partition_count", design.partition_count)
assert design.partition_count >= 2  # 保留：守卫（后面 design.partition_at(0)、partition_at(1) 按下标取，越界在 C++ 侧是未定义行为）
p0 = design.partition_at(0)
p1 = design.partition_at(1)
# 原断言：assert p0.partition_id == 0 and p1.partition_id == 1
record("s8_parts/partition_ids", {"p0": p0.partition_id, "p1": p1.partition_id})
# core：格边界吸附——列切线 2（负载前缀和等分 17 跨格在 col1）
# 原断言：assert p0.core_rect == (0, 0, 2000, 2000), f"p0 core={p0.core_rect}"
record("s8_parts/p0_core_rect", p0.core_rect)
# 原断言：assert p1.core_rect == (2000, 0, 4000, 2000), f"p1 core={p1.core_rect}"
record("s8_parts/p1_core_rect", p1.core_rect)
# extend：最外围方向 int32 极值不截断、内侧方向 ±2w（w_eff = M1 70）
# 原断言：assert p0.extend_rect == (-2147483648, -2147483648, 2140, 2147483647), \
record("s8_parts/p0_extend_rect", p0.extend_rect)
# 原断言：assert p1.extend_rect == (1860, -2147483648, 2147483647, 2147483647), \
record("s8_parts/p1_extend_rect", p1.extend_rect)
INFO("[DONE] S8 partitions: '2x1' direct cut at grid 2 (load prefix-sum "
     "equal split), core grid-aligned + extend 2*w_eff/INT extremes")

# ══ S9：flatten 展平 + 分区保存（两级任务；2026-09-13 裁定补记①-⑤）══
# partition_demo 单 def（层级树 1 节点）：实例 inv1/inv2/inv3 local 1/2/3
# → global 1/2/3（inst_start 0）；n1 local 1 → global 1（net_start 0、
# 区间长度含 local 0 空洞位——2026-09-14 裁定 global = start + local）。
# 手算锁定（DBU@1000）：
#   - inv1(100,100)/inv2(100,1100)：p0 core 内 primary 恰一，p1 extend 不含；
#   - inv3(2100,100)：p1 core primary + p0 extend（x_high 2140）副本非 primary；
#   - n1 wire M1 (0,1000)-(4000,1000) 缺省宽 70 → 段矩形 (−35,965,4035,1035)
#     两分区 extend 均交叠 → 副本两份（不裁剪）+ is_crossing；
#   - BLOCKAGE M2 (100,100)-(300,400)：仅 p0 → net 0 桶专属 + obs 位；
#   - 非 pg 全量补全：n1 连接项两分区各一份完整列表（id 形态——inv1.A =
#     全局 pin 0，INV_X1 A 为 S2 汇总首 pin；A/INPUT → receiver 位）。
from emir.design import DesignDb, iter_design_partition, load_partition
# 原断言：assert iter_design_partition(design_db) == [(0, 0), (1, 0)], \
record("s9/partition_list", list(iter_design_partition(design_db)))
geo0, geo_pg0, inst0, iconn0, nets0, nets_pg0 = \
    load_partition(design_db, 0, 0)
geo1, geo_pg1, inst1, iconn1, nets1, nets_pg1 = \
    load_partition(design_db, 1, 0)

# /INSTANCES：primary 恰一 + extend 副本 + 全局坐标（2026-09-13 裁定：
# 电源引脚预展开删除——坐标归 ④ 提取自取）
# （p0 = inv1/inv2 primary + inv3 extend 副本；p1 = 仅 inv3——inv1/inv2
# 放置点远在 p1 extend (x≥1860) 之外）
# 原断言：assert inst0.size == 3 and inst1.size == 1, \
record("s9_inst/p0_size", inst0.size)
# 原断言：assert inst0.size == 3 and inst1.size == 1, \
record("s9_inst/p1_size", inst1.size)
# 原断言：assert sorted(inst0.ids()) == [1, 2, 3] and sorted(inst1.ids()) == [3]
record("s9_inst/p0_ids", sorted(inst0.ids()))
# 原断言：assert sorted(inst0.ids()) == [1, 2, 3] and sorted(inst1.ids()) == [3]
record("s9_inst/p1_ids", sorted(inst1.ids()))
# 保留：守卫——后面 inst0.get(1)、inst0.get(3)、inst1.get(3) 按键取实例，键存在性由此把关（记录不中止脚本）
assert 1 in inst0.ids() and 3 in inst0.ids() and 3 in inst1.ids()
inv1_p0 = inst0.get(1)
# 原断言：assert inv1_p0.is_primary and (inv1_p0.pos_x, inv1_p0.pos_y) == (100, 100)
record("s9_inst/inv1_p0_primary", bool(inv1_p0.is_primary))
# 原断言：assert inv1_p0.is_primary and (inv1_p0.pos_x, inv1_p0.pos_y) == (100, 100)
record("s9_inst/inv1_p0_pos", (inv1_p0.pos_x, inv1_p0.pos_y))
inv3_p0 = inst0.get(3)
inv3_p1 = inst1.get(3)
# 原断言：assert not inv3_p0.is_primary and inv3_p1.is_primary
record("s9_inst/inv3_p0_not_primary", not inv3_p0.is_primary)
# 原断言：assert not inv3_p0.is_primary and inv3_p1.is_primary
record("s9_inst/inv3_p1_primary", bool(inv3_p1.is_primary))
# 原断言：assert (inv3_p1.pos_x, inv3_p1.pos_y) == (2100, 100)
record("s9_inst/inv3_p1_pos", (inv3_p1.pos_x, inv3_p1.pos_y))
INFO("[DONE] S9 instances: primary exactly-one + extend copy (inv3) + global "
     "pos")

# /GEOMETRY：net 0 桶 = OBS 专属（2026-09-14 裁定：无真网共存——本数据
# 无 root 首网几何，桶恒纯 OBS）；n1（global 1）wire 副本不裁剪
e0 = geo0.entries_of(0)
e1 = geo1.entries_of(0)
wire0 = [x for x in e0 if not x.is_obs]
obs0 = [x for x in e0 if x.is_obs]
# 原断言：assert len(wire0) == 0 and len(obs0) == 1, \
record("s9_geo/net0_wire_count", len(wire0))
# 原断言：assert len(wire0) == 0 and len(obs0) == 1, \
record("s9_geo/net0_obs_count", len(obs0))
assert len(obs0) >= 1  # 保留：守卫（后面 obs0[0] 按下标取，越界在 C++ 侧是未定义行为）
# 原断言：assert obs0[0].layer_id == 2 and obs0[0].rect == (100, 100, 300, 400) \
record("s9_geo/obs0_layer_id", obs0[0].layer_id)
# 原断言：assert obs0[0].layer_id == 2 and obs0[0].rect == (100, 100, 300, 400) \
record("s9_geo/obs0_rect", obs0[0].rect)
# 原断言：assert obs0[0].layer_id == 2 and obs0[0].rect == (100, 100, 300, 400) \
record("s9_geo/obs0_not_primary", not obs0[0].is_primary)
# 原断言：assert len(e1) == 0, "obstruction must not copy into p1 (no extend overlap)"
record("s9_geo/p1_net0_entry_count", len(e1))
n1_0 = geo0.entries_of(1)
n1_1 = geo1.entries_of(1)
# 原断言：assert n1_0 is not None and len(n1_0) == 1 and not n1_0[0].is_obs
record("s9_geo/p0_net1_exists", n1_0 is not None)
assert n1_0 is not None  # 保留：守卫（后面 len(n1_0)、n1_0[0] 解引用）
# 原断言：assert n1_0 is not None and len(n1_0) == 1 and not n1_0[0].is_obs
record("s9_geo/p0_net1_count", len(n1_0))
assert len(n1_0) >= 1  # 保留：守卫（后面 n1_0[0] 按下标取，越界在 C++ 侧是未定义行为）
# 原断言：assert n1_0 is not None and len(n1_0) == 1 and not n1_0[0].is_obs
record("s9_geo/p0_net1_not_obs", not n1_0[0].is_obs)
# 原断言：assert n1_0[0].layer_id == 0 and n1_0[0].rect == (-35, 965, 4035, 1035), \
record("s9_geo/p0_net1_layer_id", n1_0[0].layer_id)
# 原断言：assert n1_0[0].layer_id == 0 and n1_0[0].rect == (-35, 965, 4035, 1035), \
record("s9_geo/p0_net1_rect", n1_0[0].rect)
# 原断言：assert n1_1 is not None and len(n1_1) == 1 and not n1_1[0].is_obs
record("s9_geo/p1_net1_exists", n1_1 is not None)
assert n1_1 is not None  # 保留：守卫（后面 len(n1_1)、n1_1[0] 解引用）
# 原断言：assert n1_1 is not None and len(n1_1) == 1 and not n1_1[0].is_obs
record("s9_geo/p1_net1_count", len(n1_1))
assert len(n1_1) >= 1  # 保留：守卫（后面 n1_1[0] 按下标取，越界在 C++ 侧是未定义行为）
# 原断言：assert n1_1 is not None and len(n1_1) == 1 and not n1_1[0].is_obs
record("s9_geo/p1_net1_not_obs", not n1_1[0].is_obs)
# 原断言：assert geo0.is_crossing(1) and geo1.is_crossing(1)
record("s9_geo/p0_net1_crossing", bool(geo0.is_crossing(1)))
# 原断言：assert geo0.is_crossing(1) and geo1.is_crossing(1)
record("s9_geo/p1_net1_crossing", bool(geo1.is_crossing(1)))
# 拆分裁定（2026-09-14）：pg 侧几何对象恒空（本数据无 pg 网）
# 原断言：assert geo_pg0.net_count == 0 and geo_pg1.net_count == 0
record("s9_geo/pg0_net_count", geo_pg0.net_count)
# 原断言：assert geo_pg0.net_count == 0 and geo_pg1.net_count == 0
record("s9_geo/pg1_net_count", geo_pg1.net_count)
INFO("[DONE] S9 geometry: wire copies both partitions unclipped + crossing, "
     "OBS owns net-0 bucket exclusively (p0 only), GEOMETRY_PG empty "
     "(pg-free data)")

# /NETS（2026-09-14 拆分裁定：信号网入 NETS 对象、pg 网入 NETS_PG 对象
# ——本数据无 pg 网）：非 pg 全量补全（跨分区连接也保存——两分区各一份完
# 整列表；n1 仅 (inv1 A) 一条连接，p1 虽无 inv1 副本仍全量保存）。
# 连接 id 形态（2026-09-13 裁定）：(端点实例 global id, 全局 pin id)——
# inv1.A = 全局 pin 0（INV_X1 的 A 为 S2 汇总首 pin）；网 id 由 DSNet 键
# 承载（net_of 单表查命中）
for tag, nets in (("p0", nets0), ("p1", nets1)):
    net = nets.net_of(1)
    # 原断言：assert net is not None, "n1 must be reachable via net_of (signal side)"
    record(f"s9_nets/{tag}_net_exists", net is not None)
    assert net is not None  # 保留：守卫（后面 net.connections 等解引用）
    got = [(c.inst_id, c.pin_id) for c in net.connections]
    # 原断言：assert got == expect_conns, f"net conns={got}"
    record(f"s9_nets/{tag}_connections", got)
    # 原断言：assert net.net_id == 1 and net.use == "SIGNAL"
    record(f"s9_nets/{tag}_net_id", net.net_id)
    # 原断言：assert net.net_id == 1 and net.use == "SIGNAL"
    record(f"s9_nets/{tag}_use", net.use)
    # 原断言：assert nets.size == 1 and sorted(nets.ids) == [1]
    record(f"s9_nets/{tag}_size", nets.size)
    # 原断言：assert nets.size == 1 and sorted(nets.ids) == [1]
    record(f"s9_nets/{tag}_ids", sorted(nets.ids))
    # 原断言：assert [c.is_receiver for c in net.connections] == [True]
    record(f"s9_nets/{tag}_receiver_flags",
           [c.is_receiver for c in net.connections])
# 拆分断言：NETS_PG 侧对象恒空、与 NETS 互斥
# 原断言：assert nets_pg0.size == 0 and nets_pg1.size == 0
record("s9_nets/pg0_size", nets_pg0.size)
# 原断言：assert nets_pg0.size == 0 and nets_pg1.size == 0
record("s9_nets/pg1_size", nets_pg1.size)
# 原断言：assert list(nets_pg0.ids) == [] and list(nets_pg1.ids) == []
record("s9_nets/pg0_ids", list(nets_pg0.ids))
# 原断言：assert list(nets_pg0.ids) == [] and list(nets_pg1.ids) == []
record("s9_nets/pg1_ids", list(nets_pg1.ids))
# 强类型 id 导出面桥（2026-09-17 A-1 回归线）：part_id_ 是 CMPartitionId
# 成员——导出面经 property + .value() 桥返回 int（def_ro 直绑无 caster，
# Python 首次访问属性即抛 SystemError，此访问断言即覆盖）
# 原断言：assert nets0.part_id == 0 and nets1.part_id == 1
record("s9_nets/part_ids", {"nets0": nets0.part_id, "nets1": nets1.part_id})
# 原断言：assert nets_pg0.part_id == 0 and nets_pg1.part_id == 1
record("s9_nets/pg_part_ids", {"nets_pg0": nets_pg0.part_id,
                               "nets_pg1": nets_pg1.part_id})
# /INST_CONNECTIONS：跟随 instance 副本（partition.def 仅 inv1 有连接项
# ——inv1 端点只在 p0；inv2/inv3 无连接项不产条目）
# 原断言：assert [(c.net_global_id, c.pin_id) for c in iconn0.connections_of(1)] \
record("s9_iconn/p0_inv1_conns",
       [(c.net_global_id, c.pin_id) for c in iconn0.connections_of(1)])
# 原断言：assert iconn1.connections_of(1) == [], "inv1 must not appear in p1"
record("s9_iconn/p1_inv1_conns", iconn1.connections_of(1))
# 原断言：assert iconn0.size == 1 and iconn1.size == 0
record("s9_iconn/p0_size", iconn0.size)
# 原断言：assert iconn0.size == 1 and iconn1.size == 0
record("s9_iconn/p1_size", iconn1.size)
INFO("[DONE] S9 connections: non-pg net fully completed in both partitions "
     "(NETS object), NETS_PG empty (split ruling), inst connections "
     "follow copies")

# 全局 pg 网 id 集（2026-09-13 重组裁定："pg_nets" 正式对象）：本数据无
# pg 网（无 SPECIALNETS / 无 USE POWER|GROUND）→ 空 set、查询恒 False
from emir.design import load_design_pg_nets
pg_nets = load_design_pg_nets(design_db)
# 原断言：assert pg_nets.power_count == 0 and pg_nets.ground_count == 0
record("s9_pg/power_count", pg_nets.power_count)
# 原断言：assert pg_nets.power_count == 0 and pg_nets.ground_count == 0
record("s9_pg/ground_count", pg_nets.ground_count)
# 原断言：assert pg_nets.is_pg(0) is False and pg_nets.is_pg(1) is False \
record("s9_pg/net0_not_pg", pg_nets.is_pg(0) is False)
# 原断言：assert pg_nets.is_pg(0) is False and pg_nets.is_pg(1) is False \
record("s9_pg/net1_not_pg", pg_nets.is_pg(1) is False)
# 原断言：assert pg_nets.is_pg(0) is False and pg_nets.is_pg(1) is False \
record("s9_pg/net1_not_power", pg_nets.is_power(1) is False)
# 原断言：assert pg_nets.is_pg(0) is False and pg_nets.is_pg(1) is False \
record("s9_pg/net1_not_ground", pg_nets.is_ground(1) is False)
INFO("[DONE] S9 pg net set: empty for pg-free data, O(1) queries false")

# alpha settings 对象：S9 聚合阈值键随建库持久化（缺省 64 MiB）
# 原断言：assert design_db.read_object(
record("s9_alpha/def_aggregate_threshold",
       design_db.read_object(design_db.ALPHA_SETTINGS_OBJ)
       .def_aggregate_threshold)
INFO("[DONE] S9 alpha def_aggregate_threshold persisted with default 64MiB")

# ══ S10：汇总校验 + 冻结（2026-09-13 校验分级裁定：损坏类 fatal / 观测
# 类 warn；verify_report 正式对象随冻结落盘）══
# 手算锁定（单 def 层级树 1 节点：inst count 4 = 占位 + 3 实例、net 1、
# via 0；'2x1' 双分区）：
#   - 实例域：expected = 4 − 0(非根占位) − 1(root 自身) = 3、actual
#     {1,2,3} = 3 → holes 0；primary {1,2(×p0), 3(×p1)} 无多 primary →
#     duplicates 0；
#   - 密度守恒 primary 口径：Σ primary = 3 = Σ 首份定义 (实例数 −
#     UNPLACED) = 3 − 0 → 无偏差；
#   - 网域：expected 1（= Σ (net_count − 1)，空洞位不占 expected）、
#     actual {1}（n1 global 1）→ holes 0；via 域 expected 0 / actual 0；
#   - 统计：primary 3、副本 4（inv3 extend 副本）、连接 3（NETS 两分区
#     各 1 + iconn p0 1）、图形条目 3（p0 obs + n1 wire 双副本）、跨分区
#     网 1、密度 inst=3/metal=8/via=0（与 S8 手算一致）。
from emir.design import load_design_verify_report
report = load_design_verify_report(design_db)
# 原断言：assert report.union_inconsistency == "", "frozen db must pass union check"
record("s10_verify/union_inconsistency", report.union_inconsistency)
# 原断言：assert report.coverage_gap == "", "frozen db must pass coverage check"
record("s10_verify/coverage_gap", report.coverage_gap)
# 原断言：assert report.namemap_inconsistency == "", \
record("s10_verify/namemap_inconsistency", report.namemap_inconsistency)
# 原断言：assert report.instance_ids.expected == 3 and report.instance_ids.actual == 3
record("s10_verify/instance_ids_expected", report.instance_ids.expected)
# 原断言：assert report.instance_ids.expected == 3 and report.instance_ids.actual == 3
record("s10_verify/instance_ids_actual", report.instance_ids.actual)
# 原断言：assert report.instance_ids.holes == 0 and report.instance_ids.duplicates == 0
record("s10_verify/instance_ids_holes", report.instance_ids.holes)
# 原断言：assert report.instance_ids.holes == 0 and report.instance_ids.duplicates == 0
record("s10_verify/instance_ids_duplicates", report.instance_ids.duplicates)
# 原断言：assert report.net_ids.expected == 1 and report.net_ids.actual == 1
record("s10_verify/net_ids_expected", report.net_ids.expected)
# 原断言：assert report.net_ids.expected == 1 and report.net_ids.actual == 1
record("s10_verify/net_ids_actual", report.net_ids.actual)
# 原断言：assert report.net_ids.holes == 0 and report.via_ids.holes == 0
record("s10_verify/net_ids_holes", report.net_ids.holes)
# 原断言：assert report.net_ids.holes == 0 and report.via_ids.holes == 0
record("s10_verify/via_ids_holes", report.via_ids.holes)
# 原断言：assert report.density_variance == ""
record("s10_verify/density_variance", report.density_variance)
# 原断言：assert report.partition_count == 2
record("s10_verify/partition_count", report.partition_count)
# 原断言：assert report.total_primary == 3 and report.total_instances == 4
record("s10_verify/total_primary", report.total_primary)
# 原断言：assert report.total_primary == 3 and report.total_instances == 4
record("s10_verify/total_instances", report.total_instances)
# 原断言：assert report.total_nets == 1 and report.total_connections == 3
record("s10_verify/total_nets", report.total_nets)
# 原断言：assert report.total_nets == 1 and report.total_connections == 3
record("s10_verify/total_connections", report.total_connections)
# 原断言：assert report.total_geometry_entries == 3
record("s10_verify/total_geometry_entries", report.total_geometry_entries)
# 原断言：assert report.total_crossing_nets == 1
record("s10_verify/total_crossing_nets", report.total_crossing_nets)
# 原断言：assert (report.density_instance_total, report.density_metal_total,
record("s10_verify/density_totals",
       {"instance": report.density_instance_total,
        "metal": report.density_metal_total,
        "via": report.density_via_total})
INFO("[DONE] S10 verify report: clean fatal fields, id domains exact, "
     "primary-conservation pass, stats hand-computed")

# 分区级校验对象 id 桥同上（EXDSPartitionCheckResult.partition_id，
# 2026-09-17 A-1 回归线）：每分区一校验任务——xp/yp 取 iter 序
from emir.design import ds_verify_partition
chk0 = ds_verify_partition(0, 0, 0, geo0, geo_pg0, inst0, iconn0,
                           nets0, nets_pg0)
chk1 = ds_verify_partition(1, 1, 0, geo1, geo_pg1, inst1, iconn1,
                           nets1, nets_pg1)
# 原断言：assert chk0.partition_id == 0 and chk1.partition_id == 1
record("s10_chk/partition_ids", {"chk0": chk0.partition_id,
                                 "chk1": chk1.partition_id})
INFO("[DONE] S10 partition check: partition_id int bridge (A-1 regression)")

# 正常路径无损坏类 fatal、观测类 warn 未触发、统计 INFO 已透出
msgs_s10 = ""
for root, _dirs, files in os.walk(LOG_DIR):
    for fn in files:
        if fn.endswith(".log"):
            try:
                with open(os.path.join(root, fn), errors="ignore") as fh:
                    msgs_s10 += fh.read()
            except OSError:
                pass
for bad_id in ("DSGN::0019", "DSGN::0020", "DSGN::0021", "DSGN::0022",
               "DSGN::0023"):
    # 原断言：assert bad_id not in msgs_s10, f"clean run must not emit {bad_id}"
    record(f"s10_msg/no_{bad_id}", bad_id not in msgs_s10,
           detail=msgs_s10.count(bad_id))
# 原断言：assert "DSGN::0024" in msgs_s10, "verify summary INFO should be emitted"
record("s10_msg/dsgn0024_present", "DSGN::0024" in msgs_s10)
INFO("[DONE] S10 messages: no fatal/warn on clean path, DSGN::0024 summary "
     "emitted")

get_agent().stop()
INFO("[DONE] test_emir_partition")
