"""E2E test: EMIRProject.build_timing_db — ⑦ timing db 全链（⑬ 个库体系
的时序数据库；plan docs/emir/timing-db-plan.md，2026-09-16 全裁定）。

验证（TWF 解析 → 名字换算为 design db 全局 id → 分区落库 → 冻结 → 读库）：
  - tm_design 三维度 TWF（网络/引脚/混合，Nangate45 + OpenSTA 真实数据）：
    时钟表、汇总、分区对象、debug get_timing 点查、跨维度未登记 pin 兜底
    （DFF 的 IQ/IQN 不在 LEF pin 集 → TIMG::0002 计数）与网条目兜底口径
    （tm_design.def 无布线几何 → design db NETS 表不含该网 → TIMG::0003
    悬空计数——design db「NETS 跟随网副本」既定语义）；
  - 层级设计块绑定（qa/emir/data/design 两层树 block_parent/block_child）：
    block_inst 块实例绑定局部名偏移换算（§7.1，块端口条目归块实例自身）、
    block_cell 块定义绑定全实例复制（§7.2）、strip_prefix 段级剥离
    （§7.4，含 u_dutx 防误配与剥后余空）、纯路径全层级名；
  - 时钟表跨文件合并优先级（裁定 5）：顶层文件定义优先于块绑定文件，
    周期差异 TIMG::0007 计数保留首份。
"""
import os
import shutil

from log import INFO

from fly import get_config, launch_workers
from fly.runtime import get_agent
from emir import EMIRProject

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data",
                    "timing")
LEVEL_DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "data", "design")

LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "emir_timing_flow")


def cleanup():
    if os.path.isdir(PROJ_PATH):
        shutil.rmtree(PROJ_PATH, ignore_errors=True)


cleanup()

launch_workers([{}, {}])
assert get_agent().wait_workers_registered(timeout=60), "workers connect"
INFO("  2 workers connected (user-managed)")

proj = EMIRProject(PROJ_PATH)
assert "build_timing_db" in proj.list_flows()
INFO("[OK] build_timing_db registered on EMIRProject")

# ── 前置：lib db + design db（直接前驱，显式传参）─────────────────────
TECH_LEF = os.path.join(DATA, "NangateOpenCellLibrary.tech.lef")
MACRO_LEF = os.path.join(DATA, "NangateOpenCellLibrary.macro.lef")
LIBERTY = os.path.join(DATA, "NangateOpenCellLibrary_typical.lib")

lib_db = proj.build_lib_db(name="timing_lib", lib_paths=[LIBERTY])
assert proj.wait_frozen("timing_lib", timeout=120), "lib db should freeze"
INFO("[WAIT] lib db frozen")

design_db = proj.build_design_db(
    name="design",
    def_paths=[os.path.join(DATA, "tm_design.def")],
    lef_paths=[TECH_LEF, MACRO_LEF],
    lib_db=lib_db,
)
assert proj.wait_frozen("design", timeout=300), "design db should freeze"
INFO("[WAIT] design db frozen")

# ── 场景 1：三维度 TWF（网络/引脚/混合）──────────────────────────────
TIMING_FILES = {
    "net": "tm_design.twf",
    "pins": "tm_design_pins.twf",
    "mixed": "tm_design_mixed.twf",
}
# 各维度的（条目数, 命中数, 悬空网, 未登记 pin）——tm_design.def 无布线
# 几何：design db NETS 表不含网（连接在 INST_CONNECTIONS）→ 网络条目全部
# TIMG::0003 悬空计数（合法兜底）；引脚维度命中 15（u_cb/u_inv/u_nand/
# u_d1/u_d2 的 LEF 登记引脚），IQ/IQN 未登记 → TIMG::0002 计 4
EXPECT = {
    "net": (7, 0, 7, 0),
    "pins": (19, 15, 0, 4),
    "mixed": (26, 15, 7, 4),
}
timing_dbs = {}
for tag, fname in TIMING_FILES.items():
    tdb = proj.build_timing_db(
        name=f"timing_{tag}",
        timing_files=[os.path.join(DATA, fname)],
        design_db=design_db)
    assert proj.wait_frozen(f"timing_{tag}", timeout=300), \
        f"timing_{tag} should freeze"
    timing_dbs[tag] = tdb
    s = tdb.load_timing_summary_obj()
    entry, hit, dangling, skip_pin = EXPECT[tag]
    assert s.total_entry_count == entry, \
        f"{tag}: entry={s.total_entry_count} expect {entry}"
    assert s.total_hit_count == hit, \
        f"{tag}: hit={s.total_hit_count} expect {hit}"
    assert s.dangling_net_count == dangling, \
        f"{tag}: dangling={s.dangling_net_count} expect {dangling}"
    assert s.skipped_pin_count == skip_pin, \
        f"{tag}: skip_pin={s.skipped_pin_count} expect {skip_pin}"
    assert s.cross_file_conflict_count == 0, "single file has no conflict"
    assert len(s.files) == 1 and s.files[0].source_file.endswith(fname), \
        "per-file stats traceable"
    assert abs(s.files[0].time_scale_sec - 1e-9) < 1e-12
    INFO(f"[OK] timing_{tag}: entry={entry} hit={hit} "
         f"dangling={dangling} skip_pin={skip_pin}")

# 时钟表（三库同源：clk 周期 1.0 ns）
clocks = timing_dbs["net"].load_timing_clocks_obj()
assert clocks.size == 1 and clocks.entry_at(0).name == "clk"
assert abs(clocks.entry_at(0).period - 1.0) < 1e-9
assert abs(clocks.entry_at(0).negedge - 0.5) < 1e-9
INFO("[OK] clocks table: clk period=1.0 negedge=0.5")

# 分区对象 + debug get_timing 点查（u_cb = 实例 global id 1，clk 组 2 pins）
from emir.timing import iter_timing_partition, load_partition_timing
assert iter_timing_partition(timing_dbs["pins"]) == [(0, 0)]
part = load_partition_timing(timing_dbs["pins"], 0, 0)
assert part.size == 5, f"part instances={part.size} expect 5"
gid_cb = design_db.convert_to_id("inst", "u_cb")
assert gid_cb == 1
info = timing_dbs["pins"].get_timing(gid_cb)
assert info is not None and info["instance_id"] == gid_cb
assert info["clock"] == "clk", f"clock={info['clock']}"
assert info["partition"] == (0, 0)
pin_names = sorted(p["pin_name"] for p in info["pins"])
assert pin_names == ["A", "Z"], f"pins={pin_names}"
by_name = {p["pin_name"]: p for p in info["pins"]}
# u_cb/A 到达可读（OpenSTA 数值——生成器已知局限形态见 data/timing README）
assert by_name["A"]["rise_arrival"] is not None
assert by_name["A"]["is_constant"] is False
# 未命中族显式 None（UNPLACED / 无条目实例 / 坏 id / 坏名）
assert timing_dbs["pins"].get_timing(99999) is None
assert timing_dbs["pins"].get_timing("no_such_inst") is None
INFO("[OK] partitions + debug get_timing point query")

# ── 场景 2：层级设计块绑定（block_inst / block_cell / strip_prefix /
#    纯路径全层级名；qa/emir/data/design 两层树）────────────────────────
LEVEL_DEFS = [os.path.join(LEVEL_DATA, "block_child.def"),
              os.path.join(LEVEL_DATA, "block_parent.def")]
design_blk = proj.build_design_db(
    name="design_blk",
    def_paths=LEVEL_DEFS,
    lef_paths=[os.path.join(LEVEL_DATA, "tech.lef"),
               os.path.join(LEVEL_DATA, "cells_main.lef"),
               os.path.join(LEVEL_DATA, "cells_extra.lef")],
    lib_db=lib_db,
)
assert proj.wait_frozen("design_blk", timeout=300), "design_blk should freeze"

# 全局 id（层级树 net 区间含空洞位口径）：top1=1 top3=3 u1=5；
# 网 n_top=1 n1=3 n2=4；块端口 pin PIN_IN
gid_top3 = design_blk.convert_to_id("inst", "top3")
gid_u1 = design_blk.convert_to_id("inst", "top3/u1")
assert (gid_top3, gid_u1) == (3, 5), (gid_top3, gid_u1)

# 2a. block_inst 块实例绑定（§7.1 局部名偏移换算 + 端口条目归块实例自身）
# 条目：u1/ZN(5,ZN) ✓ u1/A(5,A) ✓ n2(网→(5,ZN) 同源合一) n1(无 driver)
# PIN_IN(块端口→归 top3(3) 自身) → entry=5 hit=3。
# dangling=2 = n1（连接表无 driver 位条目）+ n2（无布线几何不落 design db
# NETS 表——「NETS 跟随网副本」既定语义，TIMG::0003 合法兜底口径）
tdb_inst = proj.build_timing_db(
    name="t_blk_inst",
    timing_files=[{"file_name": os.path.join(DATA, "tm_blk_inst.twf"),
                   "block_inst": "top3"}],
    design_db=design_blk)
assert proj.wait_frozen("t_blk_inst", timeout=300)
s = tdb_inst.load_timing_summary_obj()
assert (s.total_entry_count, s.total_hit_count) == (5, 3), \
    (s.total_entry_count, s.total_hit_count)
assert s.dangling_net_count == 2, s.dangling_net_count
assert s.net_name_miss_count == 0 and s.skipped_pin_count == 0
assert abs(s.files[0].time_scale_sec - 1e-9) < 1e-12
INFO("[OK] block_inst binding: entry=5 hit=3 dangling=2 (port entry on "
     "block instance itself)")

# 端口语义点查：top3 自身携带 PIN_IN 时序（归属键 = 块实例自身全局 id）
info_top3 = tdb_inst.get_timing(gid_top3)
assert info_top3 is not None, "top3 must carry its port timing"
port_pins = {p["pin_name"] for p in info_top3["pins"]}
assert "PIN_IN" in port_pins, f"port pins={port_pins}"
info_u1 = tdb_inst.get_timing(gid_u1)
assert info_u1 is not None and info_u1["clock"] == "wclk"
u1_pins = {p["pin_name"] for p in info_u1["pins"]}
assert u1_pins == {"A", "ZN"}, u1_pins
INFO("[OK] port entry attributed to block instance itself (top3/PIN_IN)")

# 2b. block_cell 块定义绑定（§7.2 定义级时序对全部实例成立——建库期复制）
tdb_cell = proj.build_timing_db(
    name="t_blk_cell",
    timing_files=[{"file_name": os.path.join(DATA, "tm_blk_cell.twf"),
                   "block_cell": "block_child"}],
    design_db=design_blk)
assert proj.wait_frozen("t_blk_cell", timeout=300)
s2 = tdb_cell.load_timing_summary_obj()
assert s2.total_hit_count == 3, s2.total_hit_count
INFO("[OK] block_cell binding: definition-level replication hit=3")

# 2c. strip_prefix 段级剥离（§7.4：u_dutx 段不匹配防误配 + 剥后余空）
tdb_strip = proj.build_timing_db(
    name="t_blk_strip",
    timing_files=[{"file_name": os.path.join(DATA, "tm_blk_strip.twf"),
                   "block_inst": "top3",
                   "strip_prefix": "tb/u_dut"}],
    design_db=design_blk)
assert proj.wait_frozen("t_blk_strip", timeout=300)
s3 = tdb_strip.load_timing_summary_obj()
assert (s3.total_entry_count, s3.total_hit_count, s3.strip_miss_count) == \
    (3, 1, 2), (s3.total_entry_count, s3.total_hit_count, s3.strip_miss_count)
INFO("[OK] strip_prefix: hit=1 strip_miss=2 (u_dutx mismatch + fully "
     "stripped)")

# 2d. 纯路径全层级名 + 未匹配族计数（2026-09-16 命名裁定：路径不含设计名
# 前缀）；时钟表合并优先级（裁定 5）：顶层文件 clk=1.0 优先于块绑定文件
# clk=2.0（文件序在后仍优先），周期差异 TIMG::0007 计数 1
clk_top = os.path.join(DATA, "tm_blk_clk_top.twf")
clk_bind = os.path.join(DATA, "tm_blk_clk_bind.twf")
top_twf = os.path.join(DATA, "tm_blk_top.twf")
tdb_top = proj.build_timing_db(
    name="t_blk_top",
    timing_files=[{"file_name": clk_bind, "block_inst": "top3"},
                  clk_top, top_twf],
    design_db=design_blk)
assert proj.wait_frozen("t_blk_top", timeout=300)
s4 = tdb_top.load_timing_summary_obj()
assert s4.total_entry_count == 7, s4.total_entry_count  # 1+1+5
assert s4.total_hit_count == 3, s4.total_hit_count  # u1/A(bind) + top1/A + top1/ZN
assert s4.skipped_instance_count == 1, s4.skipped_instance_count  # ghost_inst
assert s4.skipped_pin_count == 1, s4.skipped_pin_count            # top1/NOPE
assert s4.net_name_miss_count == 1, s4.net_name_miss_count        # ghost_net
assert s4.dangling_net_count == 1, s4.dangling_net_count          # n_top 无 driver
assert s4.cross_file_conflict_count == 0
assert s4.clock_conflict_count == 1, s4.clock_conflict_count
# 顶层定义保留：clk 周期 1.0（块绑定文件先出、顶层后出——顶层仍优先）
clks = tdb_top.load_timing_clocks_obj()
assert clks.size == 1 and clks.entry_at(0).name == "clk"
assert abs(clks.entry_at(0).period - 1.0) < 1e-9, clks.entry_at(0).period
assert abs(clks.entry_at(0).negedge - 0.5) < 1e-9
INFO("[OK] top-level full-path names + clock merge top-level priority "
     "(TIMG::0007 count=1)")

# debug 面名字形态入参（str 层级路径 → design db mapper 转换）
info_named = tdb_top.get_timing("top1")
assert info_named is not None, "top-level named instance query"
INFO("[OK] get_timing accepts hierarchy path name")

# ── 场景 3：多文件异构时钟表 + 时钟归属重映射（评审 P1-1：块内时钟 id
#    与最终表 id 是两个编号空间，T3 合并必须经 remap 重写——文件 A 表
#    [wclk, clk]（wclk=0/clk=1）、文件 B 表 [clk]（块内 clk=0）：未重映射
#    时 B 条目 clock_id=0 被错指为 wclk）────────────────────────────────
gid_inv = design_db.convert_to_id("inst", "u_inv")
gid_nand = design_db.convert_to_id("inst", "u_nand")
tdb_multi = proj.build_timing_db(
    name="t_multi",
    timing_files=[os.path.join(DATA, "tm_multi_a.twf"),
                  os.path.join(DATA, "tm_multi_b.twf")],
    design_db=design_db)
assert proj.wait_frozen("t_multi", timeout=300)
s5 = tdb_multi.load_timing_summary_obj()
assert (s5.total_entry_count, s5.total_hit_count) == (4, 4), \
    (s5.total_entry_count, s5.total_hit_count)
assert s5.missing_clock_count == 0, s5.missing_clock_count
assert s5.clock_conflict_count == 0, s5.clock_conflict_count
# 最终表：文件序首份 = [wclk(2.0), clk(1.0)]（A 先出）
clks5 = tdb_multi.load_timing_clocks_obj()
assert clks5.size == 2, clks5.size
assert clks5.entry_at(0).name == "wclk" \
    and abs(clks5.entry_at(0).period - 2.0) < 1e-9
assert clks5.entry_at(1).name == "clk" \
    and abs(clks5.entry_at(1).period - 1.0) < 1e-9
# 时钟归属按来源文件定义：u_cb/u_inv 挂 A 表（wclk/clk），u_nand 挂 B 表
# 的 clk——B 块内 clk=0 必须重映射为最终表 id 1
assert tdb_multi.get_timing(gid_cb)["clock"] == "wclk"
assert tdb_multi.get_timing(gid_inv)["clock"] == "clk"
info_nand = tdb_multi.get_timing(gid_nand)
assert info_nand is not None and info_nand["clock"] == "clk", \
    f"u_nand clock={info_nand['clock'] if info_nand else None}"
assert info_nand["pins"][0]["rise_arrival"] == (0.03, 0.03), \
    info_nand["pins"][0]["rise_arrival"]
INFO("[OK] heterogeneous clock tables: per-file clock id remapped to "
     "merged table (u_nand stays clk)")

# ── 场景 4：多块切分时钟归属不丢（评审 P1-1：WAVEFORM 只入首块时非首
#    块 CAUSED_BY 时钟名未登记 → missing_clock + 归属哨兵。alpha 下限
#    chunk_size_mb=16 + 程序生成 >16MB 输入强制多块）────────────────────
from test import qa_tmp

BIG_DIR = qa_tmp("timing_multiblock")
BIG_TWF = os.path.join(BIG_DIR, "big.twf")
os.makedirs(BIG_DIR, exist_ok=True)
CHUNK_MB = 16
fill_row = '(PIN "u_cb/A" 0.100000:0.100000 0.010000 * * ' \
    '0.200000:0.200000 0.020000 * *)\n'
# 填充组 > chunk 阈值（同名条目建库期合并，计数不膨胀）；u_inv/Z 独立尾组
# 落非首块——修复前该条目时钟归属静默丢失
fill_rows = (CHUNK_MB + 1) * 1024 * 1024 // len(fill_row)
with open(BIG_TWF, "w") as f:
    f.write("(TIMING_WINDOWS\n")
    f.write('(HEADER (VERSION "fly-timing-qa 1.0") (DESIGN "tm_design") '
            '(DELIMITERS "/") (TIME_SCALE 1.000E-09))\n')
    f.write('(WAVEFORM "clk" 1.000000 (POSEDGE 0.000000) '
            '(NEGEDGE 0.500000))\n')
    f.write('(CAUSED_BY "clk"\n')
    f.write(fill_row * fill_rows)
    f.write(')\n')
    f.write('(CAUSED_BY "clk"\n')
    f.write('(PIN "u_inv/Z" 0.300000:0.300000 0.030000 * * '
            '0.400000:0.400000 0.040000 * *)\n')
    f.write(')\n')
    f.write(')\n')
assert os.path.getsize(BIG_TWF) > CHUNK_MB * 1024 * 1024, "fixture too small"
# T1 真实切块表：确认输入确实切出 ≥2 块（场景构造性证据）
from emir.timing import tm_plan_file_chunks
fp_big = tm_plan_file_chunks(BIG_TWF, CHUNK_MB * 1024 * 1024)
assert len(fp_big.chunk_starts) >= 2, \
    f"fixture must chunk into >=2 blocks, got {len(fp_big.chunk_starts)}"
INFO(f"[WAIT] multiblock fixture: {os.path.getsize(BIG_TWF)} bytes -> "
     f"{len(fp_big.chunk_starts)} chunks")

tdb_big = proj.build_timing_db(
    name="t_multiblock",
    timing_files=[BIG_TWF],
    design_db=design_db,
    alpha={"chunk_size_mb": CHUNK_MB})
assert proj.wait_frozen("t_multiblock", timeout=300)
s6 = tdb_big.load_timing_summary_obj()
# 非首块条目时钟不丢：missing_clock 聚合进 summary 且为 0（修复前 = 1 且
# summary 不可见）
assert s6.missing_clock_count == 0, s6.missing_clock_count
assert (s6.total_entry_count, s6.total_hit_count) == (2, 2), \
    (s6.total_entry_count, s6.total_hit_count)
info_inv = tdb_big.get_timing(gid_inv)
assert info_inv is not None and info_inv["clock"] == "clk", \
    f"u_inv clock={info_inv['clock'] if info_inv else None}"
assert info_inv["pins"][0]["rise_arrival"] == (0.3, 0.3), \
    info_inv["pins"][0]["rise_arrival"]
INFO("[OK] multiblock chunking: non-first-block clock ownership preserved "
     "(missing_clock=0)")
shutil.rmtree(BIG_DIR, ignore_errors=True)

print("[PASS] test_emir_timing_flow")
