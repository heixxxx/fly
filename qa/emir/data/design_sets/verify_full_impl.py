#!/usr/bin/env python3
"""design_sets 三套全量建库验证实现（verify_full.sh 调用；手动跑）。

与 QA case（test_emir_design_sets_{a,b,c}.py）的差异：本脚本跑全量数据
（B 套 30k + 顶层 TWF、C 套 102k 展开 + 顶层、A 套 route 后全链），
不设 runqa 30 秒预算。断言锚点与 QA case 同源（2026-09-17 实测口径）。

design db 语义口径：B/C 直写 DEF 无布线几何 → 网维度条目 TIMG::0003
悬空属预期（块绑定下按实例复制放大）。
"""
import os
import shutil
import time

from fly import get_config, launch_workers
from fly.runtime import get_agent
from emir import EMIRProject

DS = os.path.dirname(os.path.abspath(__file__))
TDATA = os.path.join(os.path.dirname(DS), "timing")
LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "verify_design_sets_full")

LIBERTY = os.path.join(TDATA, "NangateOpenCellLibrary_typical.lib")
LEFS = [os.path.join(TDATA, "NangateOpenCellLibrary.tech.lef"),
        os.path.join(TDATA, "NangateOpenCellLibrary.macro.lef")]


def expanded_instances(design_db):
    tree = design_db.load_design().get_hier_tree()
    n = 0
    for i in range(tree.node_count):
        lo, hi = tree.node(i).instance_range
        n += hi - lo
    return n - tree.node_count


def main():
    if os.path.isdir(PROJ_PATH):
        shutil.rmtree(PROJ_PATH, ignore_errors=True)
    launch_workers([{}, {}, {}, {}])
    assert get_agent().wait_workers_registered(timeout=60), "workers"
    proj = EMIRProject(PROJ_PATH)
    lib_db = proj.build_lib_db(name="lib", lib_paths=[LIBERTY])
    assert proj.wait_frozen("lib", timeout=300)

    # ── A 套全链（route 后）─────────────────────────────────────────
    set_a = os.path.join(DS, "set_a")
    twf_a = os.path.join(set_a, "arith_chain_mixed.twf")
    if os.path.isfile(os.path.join(set_a, "arith_chain.def")):
        t0 = time.time()
        dd_a = proj.build_design_db(
            name="design_a_full",
            def_paths=[os.path.join(set_a, "arith_chain.def")],
            lef_paths=LEFS, lib_db=lib_db,
            alpha={"target_partitions": "2x1"})
        assert proj.wait_frozen("design_a_full", timeout=3600)
        inst_a = expanded_instances(dd_a)
        assert inst_a == 5036, inst_a  # 生成器 4893 + CTS 插入 143
        s_a = None
        if os.path.isfile(twf_a):
            t_a = proj.build_timing_db(
                name="timing_a_full", timing_files=[twf_a],
                design_db=dd_a)
            assert proj.wait_frozen("timing_a_full", timeout=3600)
            s_a = t_a.load_timing_summary_obj()
            assert (s_a.total_entry_count, s_a.total_hit_count) == \
                (21707, 16573), (s_a.total_entry_count, s_a.total_hit_count)
            assert s_a.dangling_net_count == 3420, s_a.dangling_net_count
            clks = t_a.load_timing_clocks_obj()
            assert clks.size == 2, clks.size
            names = {clks.entry_at(i).name for i in range(clks.size)}
            assert names == {"clk_main", "clk_aux"}, names
        print(f"SET_A_FULL instances={inst_a} "
              f"timing={None if s_a is None else (s_a.total_entry_count, s_a.total_hit_count, s_a.dangling_net_count)} "
              f"in {time.time() - t0:.0f}s")
    else:
        print("SET_A_FULL SKIP (def not generated — verify_full.sh gen_a)")

    # ── B 套全量（30k 展开 + block_cell ×640 + 顶层 TWF）────────────
    set_b = os.path.join(DS, "set_b")
    t0 = time.time()
    dd_b = proj.build_design_db(
        name="design_b_full",
        def_paths=[os.path.join(set_b, "mesh_cell.def"),
                   os.path.join(set_b, "noc_mesh.def")],
        lef_paths=LEFS, lib_db=lib_db,
        alpha={"target_partitions": "4x1"})
    assert proj.wait_frozen("design_b_full", timeout=3600)
    assert expanded_instances(dd_b) == 32666
    t_b = proj.build_timing_db(
        name="timing_b_full",
        timing_files=[{"file_name": os.path.join(set_b, "mesh_cell_mixed.twf"),
                       "block_cell": "mesh_cell"}],
        design_db=dd_b)
    assert proj.wait_frozen("timing_b_full", timeout=3600)
    s_b = t_b.load_timing_summary_obj()
    assert (s_b.total_entry_count, s_b.total_hit_count) == (262, 175)
    assert s_b.dangling_net_count == 30080, s_b.dangling_net_count
    info_b = t_b.get_timing(dd_b.convert_to_id("inst", "u_tile_19_31/lp_n0"))
    assert info_b is not None and info_b["clock"] == "clk_noc"
    top_twf_b = os.path.join(set_b, "noc_mesh_mixed.twf")
    s_b_top = None
    if os.path.isfile(top_twf_b):
        t_b2 = proj.build_timing_db(name="timing_b_top",
                                    timing_files=[top_twf_b],
                                    design_db=dd_b)
        assert proj.wait_frozen("timing_b_top", timeout=3600)
        s_b2 = t_b2.load_timing_summary_obj()
        s_b_top = (s_b2.total_entry_count, s_b2.total_hit_count,
                   s_b2.dangling_net_count, s_b2.unplaced_instance_count)
        assert s_b_top[0] > 100000, s_b_top
    print(f"SET_B_FULL nodes_time={time.time() - t0:.0f}s "
          f"cell={(s_b.total_entry_count, s_b.total_hit_count)} "
          f"top={s_b_top}")

    # ── C 套全量（102k 展开 + 块绑定四文件 + rt ×750 复制）──────────
    set_c = os.path.join(DS, "set_c")
    t0 = time.time()
    dd_c = proj.build_design_db(
        name="design_c_full",
        def_paths=[os.path.join(set_c, f) for f in
                   ("pe_core.def", "tile_router.def", "ctrl_block.def",
                    "compute_tile.def", "hybrid_soc.def")],
        lef_paths=LEFS, lib_db=lib_db)
    assert proj.wait_frozen("design_c_full", timeout=3600)
    tree_c = dd_c.load_design().get_hier_tree()
    assert expanded_instances(dd_c) == 102192
    assert tree_c.node_count == 3002, tree_c.node_count
    t_c = proj.build_timing_db(
        name="timing_c_full",
        timing_files=[
            {"file_name": os.path.join(set_c, "pe_core_mixed.twf"),
             "block_inst": "u_tile_8_15/u_pe_0"},
            {"file_name": os.path.join(set_c, "tile_router_mixed.twf"),
             "block_cell": "tile_router"},
            {"file_name": os.path.join(set_c, "pe_core_strip.twf"),
             "block_inst": "u_tile_0_24/u_pe_1",
             "strip_prefix": "tb/u_dut"},
            {"file_name": os.path.join(set_c, "ctrl_block_mixed.twf"),
             "block_inst": "u_ctrl"},
        ],
        design_db=dd_c)
    assert proj.wait_frozen("timing_c_full", timeout=3600)
    s_c = t_c.load_timing_summary_obj()
    assert s_c.total_hit_count == 787, s_c.total_hit_count
    # rt block_cell ×750 复制：36×750 + pe 39 + strip 39 + ctrl 81
    assert s_c.dangling_net_count == 27159, s_c.dangling_net_count
    assert s_c.strip_miss_count == 0 and s_c.net_name_miss_count == 0
    assert s_c.unplaced_instance_count == 0
    # block_cell 复制：最远 tile 的 router 实例均有数据（×750）
    info_c = t_c.get_timing(dd_c.convert_to_id("inst",
                                               "u_tile_24_29/u_rt/rr_n0"))
    assert info_c is not None and info_c["clock"] == "clk_noc"
    top_twf_c = os.path.join(set_c, "hybrid_soc_mixed.twf")
    s_c_top = None
    if os.path.isfile(top_twf_c):
        t_c2 = proj.build_timing_db(name="timing_c_top",
                                    timing_files=[top_twf_c],
                                    design_db=dd_c)
        assert proj.wait_frozen("timing_c_top", timeout=3600)
        s_c2 = t_c2.load_timing_summary_obj()
        s_c_top = (s_c2.total_entry_count, s_c2.total_hit_count)
    print(f"SET_C_FULL time={time.time() - t0:.0f}s hit={s_c.total_hit_count} "
          f"dangling={s_c.dangling_net_count} top={s_c_top}")

    print("[PASS] verify_full_impl")


main()
