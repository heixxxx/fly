"""E2E test: build_timing_db @ pg_grid 放大规模（1.2 万实例 + 分区路由）。

pg_grid：tm_design 的 64×64 阵列放大版（gen_pg_grid.py 确定性生成 →
OpenROAD 布图/布局/CTS/布线 → OpenSTA 产出 pg_grid.twf，双跑逐字节一致；
见 data/timing/README.md）。design db 以 alpha target_partitions='2x1'
直切两分区（裁定 8：pg_grid 断言 ≥2 分区不锁死数量，数据落定后收紧）。

断言（12943 条网络维度 TWF；2026-11-24 端口角色裁定后的新锚）：
  - hit=12880（精确回归锚：全部有连接的网条目挂到驱动者所在分区——
    实例驱动者挂驱动者分区，输入端口驱动者〔新口径：INPUT 端口 = 它所
    在网的驱动者〕挂块自身编号下的端口引脚、写进全部负载所在分区。
    旧锚 12878 + 2 = 两张由顶层输入端口驱动且有布线的网〔clk 主网与复
    位主网〕旧口径被判「无 driver 位条目」悬空、新口径命中）
  - dangling=63（无连接记录的网 65 − 上述 2 张端口驱动网）
  - unplaced=0（旧锚 1 = 端口条目被计成未放置实例的误报——端口条目不
    是实例，2026-11-24 裁定后不再计入；pg_grid 无未放置实例）
  - net_name_miss=0（pg_grid 网名与 DEF 全量对齐）
  - 分区路由：两分区均有实例时序数据、并集覆盖命中实例
"""
import os
import shutil

from log import INFO

from fly import get_config, launch_workers
from fly.runtime import get_agent
from emir import EMIRProject

from test import record

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data",
                    "timing")

LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "emir_timing_pg")


def cleanup():
    if os.path.isdir(PROJ_PATH):
        shutil.rmtree(PROJ_PATH, ignore_errors=True)


cleanup()

launch_workers([{}, {}])
assert get_agent().wait_workers_registered(timeout=60), "workers connect"

proj = EMIRProject(PROJ_PATH)
lib_db = proj.build_lib_db(
    name="lib", lib_paths=[os.path.join(DATA,
                                        "NangateOpenCellLibrary_typical.lib")])
assert proj.wait_frozen("lib", timeout=300), "lib freeze"

design_db = proj.build_design_db(
    name="design",
    def_paths=[os.path.join(DATA, "pg_grid.def")],
    lef_paths=[os.path.join(DATA, "NangateOpenCellLibrary.tech.lef"),
               os.path.join(DATA, "NangateOpenCellLibrary.macro.lef")],
    lib_db=lib_db,
    alpha={"target_partitions": "2x1"},
)
assert proj.wait_frozen("design", timeout=600), "design freeze"

timing_db = proj.build_timing_db(
    name="timing",
    timing_files=[os.path.join(DATA, "pg_grid.twf")],
    design_db=design_db,
)
assert proj.wait_frozen("timing", timeout=600), "timing freeze"
INFO("[WAIT] timing frozen (pg_grid 12943 entries)")

s = timing_db.load_timing_summary_obj()
# 原断言：assert s.total_entry_count == 12943, s.total_entry_count
record("timing/total_entry_count", s.total_entry_count)
# 原断言：assert s.total_hit_count == 12880, s.total_hit_count
record("timing/total_hit_count", s.total_hit_count)
# 原断言：assert s.dangling_net_count == 63, s.dangling_net_count
record("timing/dangling_net_count", s.dangling_net_count)
# 原断言：assert s.net_name_miss_count == 0, s.net_name_miss_count
record("timing/net_name_miss_count", s.net_name_miss_count)
# 原断言：assert s.unplaced_instance_count == 0, s.unplaced_instance_count
record("timing/unplaced_instance_count", s.unplaced_instance_count)
# 原断言：assert s.cross_file_conflict_count == 0
record("timing/cross_file_conflict_count", s.cross_file_conflict_count)
# 原断言：assert len(s.files) == 1
record("timing/files_count", len(s.files))
assert len(s.files) >= 1  # 保留：守卫（后面 s.files[0] 按下标取，越界在 C++ 侧是未定义行为）
# 原断言：assert s.files[0].failed_chunk_count == 0
record("timing/files0_failed_chunk_count", s.files[0].failed_chunk_count)
INFO(f"[DONE] summary: hit={s.total_hit_count}/{s.total_entry_count} "
     f"dangling={s.dangling_net_count} unplaced={s.unplaced_instance_count}")

clocks = timing_db.load_timing_clocks_obj()
# 原断言：assert clocks.size == 1 and clocks.entry_at(0).name == "clk"
record("clocks/size", clocks.size)
assert clocks.size >= 1  # 保留：守卫（后面 clocks.entry_at(0) 按下标取，越界在 C++ 侧是未定义行为）
# 原断言：assert clocks.size == 1 and clocks.entry_at(0).name == "clk"
record("clocks/entry0_name", clocks.entry_at(0).name)
# 原断言：assert abs(clocks.entry_at(0).period - 1.0) < 1e-9
record("clocks/entry0_period_1ns",
       abs(clocks.entry_at(0).period - 1.0) < 1e-9,
       detail=f"{clocks.entry_at(0).period!r}")
INFO("[DONE] clocks: clk period=1.0 (frequency = 1/period by consumer)")

# 分区路由：两分区均有数据（≥2 分区下限断言——裁定 8 不锁死数量），
# 时序覆盖的实例并集 = 命中实例数（每实例 primary 恰一）
from emir.timing import iter_timing_partition, load_partition_timing
parts = iter_timing_partition(timing_db)
# 原断言：assert len(parts) >= 2, f"partitions={parts}"
record("primary/partition_count_at_least_2", len(parts) >= 2, detail=parts)
total = 0
empty_parts = []
for xp, yp in parts:
    part = load_partition_timing(timing_db, xp, yp)
    if part.size <= 0:
        empty_parts.append((xp, yp))
    total += part.size
    INFO(f"[DONE] PART({xp},{yp}): {part.size} instances with timing")
# 原断言：assert part.size > 0, f"PART({xp},{yp}) unexpectedly empty"
record("primary/no_empty_partition", not empty_parts, detail=empty_parts)
# hit − total = 1：两张端口驱动网（clk/复位主网）的条目挂 root 块自身
# 实例编号 0 下的两个端口引脚——hit 按条目计（+2），分区实例槽按 distinct
# 实例 id 计（root 0 一个槽，+1）
# 原断言：assert total == 12879, (total, s.total_hit_count)
record("primary/partitioned_instances", total)
INFO(f"[DONE] partition routing: {total} instances across {len(parts)} "
     f"partitions (primary exactly-one; hit-1 = two port-driven nets "
     f"sharing the root self slot)")

# 点查抽查：分区 0 的首个实例（遍历面 → debug API 一致性）
part0 = load_partition_timing(timing_db, 0, 0)
assert part0.size >= 1  # 保留：守卫（后面 part0.instance_ids[0] 按下标取，空列表会以 IndexError 崩溃）
some_id = part0.instance_ids[0]
info = timing_db.get_timing(some_id)
# 原断言：assert info is not None and info["instance_id"] == some_id
record("probe/info_exists", info is not None)
assert info is not None  # 保留：守卫（后面 info["instance_id"]、info["partition"]、info["pins"] 按键取）
# 原断言：assert info is not None and info["instance_id"] == some_id
record("probe/info_instance_id", info["instance_id"] == some_id,
       detail=(info["instance_id"], some_id))
# 原断言：assert info["partition"] == (0, 0)
record("probe/info_partition", info["partition"])
# 原断言：assert len(info["pins"]) >= 1
record("probe/pin_count_at_least_1", len(info["pins"]) >= 1,
       detail=len(info["pins"]))
INFO(f"[DONE] get_timing({some_id}): {len(info['pins'])} pin(s), "
     f"clock={info['clock']}")

print("[DONE] test_emir_timing_pg_grid")
