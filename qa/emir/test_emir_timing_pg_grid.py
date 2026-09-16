"""E2E test: build_timing_db @ pg_grid 放大规模（1.2 万实例 + 分区路由）。

pg_grid：tm_design 的 64×64 阵列放大版（gen_pg_grid.py 确定性生成 →
OpenROAD 布图/布局/CTS/布线 → OpenSTA 产出 pg_grid.twf，双跑逐字节一致；
见 data/timing/README.md）。design db 以 alpha target_partitions='2x1'
直切两分区（裁定 8：pg_grid 断言 ≥2 分区不锁死数量，数据落定后收紧）。

断言（12943 条网络维度 TWF）：
  - hit=12878（精确回归锚：全部有连接的网条目挂到 driver 引脚所在分区）
  - dangling=65（无连接记录的网——design db NETS 表不含的合法兜底口径）
  - unplaced=1（无 primary 分区副本）
  - net_name_miss=0（pg_grid 网名与 DEF 全量对齐）
  - 分区路由：两分区均有实例时序数据、并集覆盖命中实例
"""
import os
import shutil

from log import INFO

from fly import get_config, launch_workers
from fly.runtime import get_agent
from emir import EMIRProject

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
assert s.total_entry_count == 12943, s.total_entry_count
assert s.total_hit_count == 12878, s.total_hit_count
assert s.dangling_net_count == 65, s.dangling_net_count
assert s.net_name_miss_count == 0, s.net_name_miss_count
assert s.unplaced_instance_count == 1, s.unplaced_instance_count
assert s.cross_file_conflict_count == 0
assert len(s.files) == 1
assert s.files[0].failed_chunk_count == 0
INFO(f"[OK] summary: hit={s.total_hit_count}/{s.total_entry_count} "
     f"dangling={s.dangling_net_count} unplaced={s.unplaced_instance_count}")

clocks = timing_db.load_timing_clocks_obj()
assert clocks.size == 1 and clocks.entry_at(0).name == "clk"
assert abs(clocks.entry_at(0).period - 1.0) < 1e-9
INFO("[OK] clocks: clk period=1.0 (frequency = 1/period by consumer)")

# 分区路由：两分区均有数据（≥2 分区下限断言——裁定 8 不锁死数量），
# 时序覆盖的实例并集 = 命中实例数（每实例 primary 恰一）
from emir.timing import iter_timing_partition, load_partition_timing
parts = iter_timing_partition(timing_db)
assert len(parts) >= 2, f"partitions={parts}"
total = 0
for xp, yp in parts:
    part = load_partition_timing(timing_db, xp, yp)
    assert part.size > 0, f"PART({xp},{yp}) unexpectedly empty"
    total += part.size
    INFO(f"[OK] PART({xp},{yp}): {part.size} instances with timing")
assert total == s.total_hit_count, (total, s.total_hit_count)
INFO(f"[OK] partition routing: {total} instances across {len(parts)} "
     f"partitions (primary exactly-one)")

# 点查抽查：分区 0 的首个实例（遍历面 → debug API 一致性）
part0 = load_partition_timing(timing_db, 0, 0)
some_id = part0.instance_ids[0]
info = timing_db.get_timing(some_id)
assert info is not None and info["instance_id"] == some_id
assert info["partition"] == (0, 0)
assert len(info["pins"]) >= 1
INFO(f"[OK] get_timing({some_id}): {len(info['pins'])} pin(s), "
     f"clock={info['clock']}")

print("[PASS] test_emir_timing_pg_grid")
