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
assert gd.cols == 4 and gd.rows == 2, f"grid={gd.cols}x{gd.rows}"
assert gd.total_count == 3, f"inst={gd.total_count}"
assert gd.metal_total == 8, f"metal={gd.metal_total}"
assert gd.via_total == 0, f"via={gd.via_total}"
INFO("[OK] S8 global density: inst=3, metal=8, via=0 (hand-computed)")

# ── 分区表：'2x1' 直切（负载不均切线 + 双区域坐标）──
design = design_db.load_design()
assert design.partition_count == 2, f"parts={design.partition_count}"
p0 = design.partition_at(0)
p1 = design.partition_at(1)
assert p0.partition_id == 0 and p1.partition_id == 1
# core：格边界吸附——列切线 2（负载前缀和等分 17 跨格在 col1）
assert p0.core_rect == (0, 0, 2000, 2000), f"p0 core={p0.core_rect}"
assert p1.core_rect == (2000, 0, 4000, 2000), f"p1 core={p1.core_rect}"
# extend：最外围方向 int32 极值不截断、内侧方向 ±2w（w_eff = M1 70）
assert p0.extend_rect == (-2147483648, -2147483648, 2140, 2147483647), \
    f"p0 extend={p0.extend_rect}"
assert p1.extend_rect == (1860, -2147483648, 2147483647, 2147483647), \
    f"p1 extend={p1.extend_rect}"
INFO("[OK] S8 partitions: '2x1' direct cut at grid 2 (load prefix-sum "
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
assert iter_design_partition(design_db) == [(0, 0), (1, 0)], \
    f"iter={iter_design_partition(design_db)}"
geo0, geo_pg0, inst0, iconn0, nets0, nets_pg0 = \
    load_partition(design_db, 0, 0)
geo1, geo_pg1, inst1, iconn1, nets1, nets_pg1 = \
    load_partition(design_db, 1, 0)

# /INSTANCES：primary 恰一 + extend 副本 + 全局坐标（2026-09-13 裁定：
# 电源引脚预展开删除——坐标归 ④ 提取自取）
# （p0 = inv1/inv2 primary + inv3 extend 副本；p1 = 仅 inv3——inv1/inv2
# 放置点远在 p1 extend (x≥1860) 之外）
assert inst0.size == 3 and inst1.size == 1, \
    f"inst sizes {inst0.size}/{inst1.size}"
assert sorted(inst0.ids()) == [1, 2, 3] and sorted(inst1.ids()) == [3]
inv1_p0 = inst0.get(1)
assert inv1_p0.is_primary and (inv1_p0.pos_x, inv1_p0.pos_y) == (100, 100)
inv3_p0 = inst0.get(3)
inv3_p1 = inst1.get(3)
assert not inv3_p0.is_primary and inv3_p1.is_primary
assert (inv3_p1.pos_x, inv3_p1.pos_y) == (2100, 100)
INFO("[OK] S9 instances: primary exactly-one + extend copy (inv3) + global "
     "pos")

# /GEOMETRY：net 0 桶 = OBS 专属（2026-09-14 裁定：无真网共存——本数据
# 无 root 首网几何，桶恒纯 OBS）；n1（global 1）wire 副本不裁剪
e0 = geo0.entries_of(0)
e1 = geo1.entries_of(0)
wire0 = [x for x in e0 if not x.is_obs]
obs0 = [x for x in e0 if x.is_obs]
assert len(wire0) == 0 and len(obs0) == 1, \
    "net-0 bucket must be OBS-exclusive (key-0 ruling)"
assert obs0[0].layer_id == 2 and obs0[0].rect == (100, 100, 300, 400) \
    and not obs0[0].is_primary
assert len(e1) == 0, "obstruction must not copy into p1 (no extend overlap)"
n1_0 = geo0.entries_of(1)
n1_1 = geo1.entries_of(1)
assert n1_0 is not None and len(n1_0) == 1 and not n1_0[0].is_obs
assert n1_0[0].layer_id == 0 and n1_0[0].rect == (-35, 965, 4035, 1035), \
    f"wire={n1_0[0].rect}"
assert n1_1 is not None and len(n1_1) == 1 and not n1_1[0].is_obs
assert geo0.is_crossing(1) and geo1.is_crossing(1)
# 拆分裁定（2026-09-14）：pg 侧几何对象恒空（本数据无 pg 网）
assert geo_pg0.net_count == 0 and geo_pg1.net_count == 0
INFO("[OK] S9 geometry: wire copies both partitions unclipped + crossing, "
     "OBS owns net-0 bucket exclusively (p0 only), GEOMETRY_PG empty "
     "(pg-free data)")

# /NETS（2026-09-14 拆分裁定：信号网入 NETS 对象、pg 网入 NETS_PG 对象
# ——本数据无 pg 网）：非 pg 全量补全（跨分区连接也保存——两分区各一份完
# 整列表；n1 仅 (inv1 A) 一条连接，p1 虽无 inv1 副本仍全量保存）。
# 连接 id 形态（2026-09-13 裁定）：(端点实例 global id, 全局 pin id)——
# inv1.A = 全局 pin 0（INV_X1 的 A 为 S2 汇总首 pin）；网 id 由 DSNet 键
# 承载（net_of 单表查命中）
expect_conns = [(1, 0)]
for nets in (nets0, nets1):
    net = nets.net_of(1)
    assert net is not None, "n1 must be reachable via net_of (signal side)"
    got = [(c.inst_id, c.pin_id) for c in net.connections]
    assert got == expect_conns, f"net conns={got}"
    assert net.net_id == 1 and net.use == "SIGNAL"
    assert nets.size == 1 and sorted(nets.ids) == [1]
    assert [c.is_receiver for c in net.connections] == [True]
# 拆分断言：NETS_PG 侧对象恒空、与 NETS 互斥
assert nets_pg0.size == 0 and nets_pg1.size == 0
assert list(nets_pg0.ids) == [] and list(nets_pg1.ids) == []
# 强类型 id 导出面桥（2026-09-17 A-1 回归线）：part_id_ 是 CMPartitionId
# 成员——导出面经 property + .value() 桥返回 int（def_ro 直绑无 caster，
# Python 首次访问属性即抛 SystemError，此访问断言即覆盖）
assert nets0.part_id == 0 and nets1.part_id == 1
assert nets_pg0.part_id == 0 and nets_pg1.part_id == 1
# /INST_CONNECTIONS：跟随 instance 副本（partition.def 仅 inv1 有连接项
# ——inv1 端点只在 p0；inv2/inv3 无连接项不产条目）
assert [(c.net_global_id, c.pin_id) for c in iconn0.connections_of(1)] \
    == [(1, 0)]
assert iconn1.connections_of(1) == [], "inv1 must not appear in p1"
assert iconn0.size == 1 and iconn1.size == 0
INFO("[OK] S9 connections: non-pg net fully completed in both partitions "
     "(NETS object), NETS_PG empty (split ruling), inst connections "
     "follow copies")

# 全局 pg 网 id 集（2026-09-13 重组裁定："pg_nets" 正式对象）：本数据无
# pg 网（无 SPECIALNETS / 无 USE POWER|GROUND）→ 空 set、查询恒 False
from emir.design import load_design_pg_nets
pg_nets = load_design_pg_nets(design_db)
assert pg_nets.power_count == 0 and pg_nets.ground_count == 0
assert pg_nets.is_pg(0) is False and pg_nets.is_pg(1) is False \
    and pg_nets.is_power(1) is False and pg_nets.is_ground(1) is False
INFO("[OK] S9 pg net set: empty for pg-free data, O(1) queries false")

# alpha settings 对象：S9 聚合阈值键随建库持久化（缺省 64 MiB）
assert design_db.read_object(
    design_db.ALPHA_SETTINGS_OBJ).def_aggregate_threshold == 64 * 1024 * 1024
INFO("[OK] S9 alpha def_aggregate_threshold persisted with default 64MiB")

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
assert report.union_inconsistency == "", "frozen db must pass union check"
assert report.coverage_gap == "", "frozen db must pass coverage check"
assert report.namemap_inconsistency == "", \
    "frozen db must pass namemap check"
assert report.instance_ids.expected == 3 and report.instance_ids.actual == 3
assert report.instance_ids.holes == 0 and report.instance_ids.duplicates == 0
assert report.net_ids.expected == 1 and report.net_ids.actual == 1
assert report.net_ids.holes == 0 and report.via_ids.holes == 0
assert report.density_variance == ""
assert report.partition_count == 2
assert report.total_primary == 3 and report.total_instances == 4
assert report.total_nets == 1 and report.total_connections == 3
assert report.total_geometry_entries == 3
assert report.total_crossing_nets == 1
assert (report.density_instance_total, report.density_metal_total,
        report.density_via_total) == (3, 8, 0)
INFO("[OK] S10 verify report: clean fatal fields, id domains exact, "
     "primary-conservation pass, stats hand-computed")

# 分区级校验对象 id 桥同上（EXDSPartitionCheckResult.partition_id，
# 2026-09-17 A-1 回归线）：每分区一校验任务——xp/yp 取 iter 序
from emir.design import ds_verify_partition
chk0 = ds_verify_partition(0, 0, 0, geo0, geo_pg0, inst0, iconn0,
                           nets0, nets_pg0)
chk1 = ds_verify_partition(1, 1, 0, geo1, geo_pg1, inst1, iconn1,
                           nets1, nets_pg1)
assert chk0.partition_id == 0 and chk1.partition_id == 1
INFO("[OK] S10 partition check: partition_id int bridge (A-1 regression)")

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
    assert bad_id not in msgs_s10, f"clean run must not emit {bad_id}"
assert "DSGN::0024" in msgs_s10, "verify summary INFO should be emitted"
INFO("[OK] S10 messages: no fatal/warn on clean path, DSGN::0024 summary "
     "emitted")

get_agent().stop()
INFO("[PASS] test_emir_partition")
