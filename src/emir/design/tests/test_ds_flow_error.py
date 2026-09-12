"""design flow 错误处理单测（流程错误处理范式 2026-09-13，dev-rules §7.2）。

覆盖：
  - sniff_lef_header / sniff_def_header：LEF/DEF 形态判定（VERSION/
    DESIGN 关键字），非 LEF/DEF 文件（如 liberty 误传）ValueError 秒级拦截
  - ds_parse_cell_one：返回 (design, geoms, vias, stats) 四元组
    （stats.parse_failed_count 失败标记通道，cell lef 语法错误兜底）
  - _cell_lef_merge_task 汇总层（review 2026-09-13 补测——此前 flow 接缝
    零覆盖）：部分失败 → DSGN::0014 + merged/geoms 照常写出（依赖链
    满足，范式 (b)）；全部失败 → fatal 子进程断言 rc=80（范式 (a)）；
    全好不发 message；直调经 run_direct（R9），db 用内存桩

tech lef / DEF 语法错误 fatal 路径的 fork 断言在 ds_lef_adapter_test /
ds_def_adapter_test（C++ 层）；端到端场景在 qa/emir/test_flow_error_
handling.py。

模块内部单测：直连 py 包内部符号（utils 不对外导出；BUILD imports 指向
src 根 + 各 .so export 目录，同 emir_project_test 先例）。
"""
import os
import subprocess
import sys
import tempfile

import fly
from fly import run_direct  # noqa: E402

from emir.design.py import ds_flow, ds_utils  # noqa: E402
from emir.design import EXDSDesign, EXDSPinGeometry  # noqa: E402


def _write(name, text):
    path = os.path.join(tempfile.mkdtemp(prefix="ds_flow_err_"), name)
    with open(path, "w") as f:
        f.write(text)
    return path


GOOD_LEF = "VERSION 5.8 ;\nUNITS\n DATABASE MICRONS 2000 ;\nEND UNITS\n" \
           "LAYER M1\n TYPE ROUTING\nEND M1\nEND LIBRARY\n"
GOOD_DEF = "VERSION 5.8 ;\nDIVIDERCHAR \"/\" ;\nDESIGN top_block ;\n" \
           "UNITS\n DATABASE MICRONS 100 ;\nEND UNITS\nDIEAREA ( 0 0 ) " \
           "( 100 100 ) ;\nEND DESIGN\n"


def test_sniff_lef_header():
    lib_utils_ok = _write("ok.lef", GOOD_LEF)
    ds_utils.sniff_lef_header(lib_utils_ok)
    ds_utils.sniff_lef_header(_write("comment.lef",
                                     "# just a comment line\n" + GOOD_LEF))
    # liberty 误传（library 开头，无 VERSION）→ ValueError
    lib = _write("fake.lef", "library (typ) { cell (C) { } }\n")
    try:
        ds_utils.sniff_lef_header(lib)
        raise AssertionError("liberty file passed lef sniff")
    except ValueError as e:
        assert "VERSION" in str(e) and lib in str(e), str(e)
    print("[OK] sniff_lef_header")


def test_sniff_def_header():
    ds_utils.sniff_def_header(_write("ok.def", GOOD_DEF))
    # 有 VERSION 无 DESIGN（如 LEF 变体 / 残缺 DEF）→ ValueError
    no_design = _write("no_design.def",
                       "VERSION 5.8 ;\nUNITS\n DATABASE MICRONS 100 ;\n"
                       "END UNITS\n")
    try:
        ds_utils.sniff_def_header(no_design)
        raise AssertionError("file without DESIGN passed def sniff")
    except ValueError as e:
        assert "DESIGN" in str(e), str(e)
    print("[OK] sniff_def_header")


def test_parse_cell_one_returns_stats_channel():
    # S2 包装返回四元组（stats 为失败标记通道）：合法 cell lef 正常解析、
    # parse_failed_count=0。数据用 tests/data 合成样例（与 C++ 单测同源）。
    from _fly_emir_design import EXDSStack
    data = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
    stack = EXDSStack()
    ds_utils.ds_parse_tech_one(os.path.join(data, "tech_synth.lef"), stack)
    parts = ds_utils.ds_parse_cell_one(
        os.path.join(data, "cells_synth.lef"), stack)
    assert len(parts) == 4, f"cell lef parse must return 4-tuple, got {len(parts)}"
    design, geoms, vias, stats = parts
    assert stats.parse_failed_count == 0, (
        f"good cell lef must not be flagged failed: "
        f"macro={stats.macro_count}")
    assert stats.macro_count > 0, "good cell lef must yield macros"
    print("[OK] ds_parse_cell_one 4-tuple with stats channel")


class _MemDb:
    """内存 db 桩：read 预置对象 / write 收集键（run_direct 不走 as_task
    wrapper，无需 get_full_name——inputs 依赖解析仅在 wrapper 路径发生）。"""

    def __init__(self, objs):
        self._objs = objs
        self.written = []

    def read_object(self, key, **kw):
        return self._objs[key]

    def write_object(self, key, val, **kw):
        self._objs[key] = val
        self.written.append(key)


def _part(db_objs, prefix, failed):
    """构造一个 cell lef part 的四元组产物（空设计 + 失败清单可选）。"""
    design_key, geoms_key, vias_key, failed_key = (
        f"{prefix}.design", f"{prefix}.geoms", f"{prefix}.vias",
        f"{prefix}.failed")
    db_objs[design_key] = EXDSDesign()
    db_objs[geoms_key] = EXDSPinGeometry()
    db_objs[vias_key] = []
    db_objs[failed_key] = [f"{prefix}.lef"] if failed else []
    return (design_key, geoms_key, vias_key, failed_key)


def _run_merge(db_objs, part_tuples):
    db = _MemDb(db_objs)
    run_direct(ds_flow._cell_lef_merge_task, db, list(part_tuples),
               "stack", "tech_vias", "merged", "macro_geoms")
    return db


def test_cell_merge_partial_failure_falls_back_with_message():
    sent = []
    orig = fly.message
    fly.message = lambda *a, **k: sent.append(a)
    try:
        objs = {"tech_vias": []}
        good = _part(objs, "good", failed=False)
        bad = _part(objs, "bad", failed=True)
        db = _run_merge(objs, [good, bad])
    finally:
        fly.message = orig
    # 范式 (b)：merged/geoms 照常写出（依赖链满足——下游不悬挂）
    assert "merged" in db.written and "macro_geoms" in db.written, db.written
    # DSGN::0014 恰一条，含失败文件名与总数
    assert len(sent) == 1, sent
    assert sent[0][0] == "DSGN::0014", sent
    assert "bad.lef" in sent[0][2] and "1/2" in sent[0][2], sent
    print("[OK] cell merge partial failure: DSGN::0014 + products written")


def test_cell_merge_all_good_sends_no_message():
    sent = []
    orig = fly.message
    fly.message = lambda *a, **k: sent.append(a)
    try:
        objs = {"tech_vias": []}
        db = _run_merge(objs, [_part(objs, "a", failed=False),
                               _part(objs, "b", failed=False)])
    finally:
        fly.message = orig
    assert not sent, sent
    assert "merged" in db.written and "macro_geoms" in db.written
    print("[OK] cell merge all-good: no message, products written")


def test_cell_merge_all_failed_fatals_exit_80():
    # 全败分支（DSGN::0015 fatal → _exit(80)）：子进程隔离断言退出码
    #（PYTHONPATH 继承父进程 sys.path，同 test_lib_flow_error.py 先例）。
    script = (
        "from fly import run_direct\n"
        "from emir.design.py import ds_flow\n"
        "from emir.design import EXDSDesign, EXDSPinGeometry\n"
        "class Db:\n"
        "    def read_object(self, k, **kw):\n"
        "        return [k + '.lef'] if k.endswith('failed') "
        "else (EXDSDesign() if k.endswith('design') "
        "else (EXDSPinGeometry() if k.endswith('geoms') else []))\n"
        "    def write_object(self, k, v, **kw):\n"
        "        pass\n"
        "tuples = [('p%d.design' % i, 'p%d.geoms' % i, 'p%d.vias' % i,\n"
        "           'p%d.failed' % i) for i in range(2)]\n"
        "run_direct(ds_flow._cell_lef_merge_task, Db(), tuples,\n"
        "           'stack', 'tech_vias', 'merged', 'macro_geoms')\n"
    )
    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join(p for p in sys.path if p)
    proc = subprocess.run([sys.executable, "-c", script], env=env,
                          capture_output=True, text=True, timeout=60)
    assert proc.returncode == 80, \
        (f"all-failed cell lef merge must fatal-exit 80, "
         f"got {proc.returncode}\n{proc.stderr}")
    print("[OK] cell merge all-failed: fatal exit 80 (subprocess)")


test_sniff_lef_header()
test_sniff_def_header()
test_parse_cell_one_returns_stats_channel()
test_cell_merge_partial_failure_falls_back_with_message()
test_cell_merge_all_good_sends_no_message()
test_cell_merge_all_failed_fatals_exit_80()
print("[PASS] test_ds_flow_error")
