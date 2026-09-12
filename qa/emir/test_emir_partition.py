"""E2E test: S8 分区决策直切场景（target_partitions '2x1'，2026-09-12/13
裁定 1/2/4）。

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
验证：分区数、core/extend 坐标（最外围 int32 极值 / 内侧 ±2w）、全局密
度三通道计数。
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

get_agent().stop()
INFO("[PASS] test_emir_partition")
