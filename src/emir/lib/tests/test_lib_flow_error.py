"""lib flow 错误处理兜底单测（流程错误处理范式 2026-09-13，dev-rules §7.2）。

覆盖：
  - lib_parse_one：语法错误兜底（空产物 + 失败清单，不 raise）、
    文件缺失仍 FileNotFoundError（dev-rules §7 第一类）
  - sniff_liberty_header：liberty 形态判定（library 关键字 + '('），
    非 liberty 文件（如 .lef 误传）ValueError 秒级拦截
  - _lib_merge_two / _make_finalize：失败清单随合并树汇聚、部分失败
    message 透出（返回纯 LIBLibrary）、全败 fatal（子进程断言 rc=80）

模块内部单测：直连 py 包内部符号（utils 不对外导出；BUILD imports 指向
src 根 + 各 .so export 目录，同 emir_project_test 先例）。
"""
import os
import subprocess
import sys
import tempfile


from emir.lib.py import lib_flow, lib_utils  # noqa: E402


GOOD_LIB = 'library (good_typ) { cell (INV) { pin (A) { direction : input; } } }\n'


def _write(name, text):
    path = os.path.join(tempfile.mkdtemp(prefix="lib_flow_err_"), name)
    with open(path, "w") as f:
        f.write(text)
    return path


def test_sniff_liberty_header():
    # 合法 liberty 头（含注释噪声）通过
    good = _write("good.lib", "// lead comment\n/* block\n comment */\n"
                             "library (good_typ) { }\n")
    lib_utils.sniff_liberty_header(good)
    # .lef 误传（VERSION 开头）→ ValueError
    lef = _write("fake.lef", "VERSION 5.8 ;\nUNITS\n DATABASE MICRONS 2000 ;\n"
                             "END UNITS\nLAYER M1\n TYPE ROUTING\nEND M1\n")
    try:
        lib_utils.sniff_liberty_header(lef)
        raise AssertionError("lef file passed liberty sniff")
    except ValueError as e:
        assert "library (" in str(e) and lef in str(e), str(e)
    # 垃圾文本 → ValueError
    junk = _write("junk.lib", "this is not a liberty file at all\n")
    try:
        lib_utils.sniff_liberty_header(junk)
        raise AssertionError("junk text passed liberty sniff")
    except ValueError:
        pass
    print("[OK] sniff_liberty_header")


def test_parse_one_fallback():
    # 好文件：正常解析，无失败清单
    good = _write("good.lib", GOOD_LIB)
    lib, failures = lib_utils.lib_parse_one(good)
    assert not failures, failures
    assert len(lib.cells) == 1, f"cells={len(lib.cells)}"

    # 语法错误（缺右括号）：兜底——空产物 + 失败清单（含路径与原因），不 raise
    bad = _write("bad.lib", "library (broken) { cell (C) { pin (P) "
                            "{ direction : input; }\n")
    lib, failures = lib_utils.lib_parse_one(bad)
    assert len(failures) == 1, failures
    assert bad in failures[0], failures[0]
    assert len(lib.cells) == 0, "fallback product must be an empty library"

    # 文件缺失：仍 raise（dev-rules §7 第一类）
    try:
        lib_utils.lib_parse_one("/nonexistent/no.lib")
        raise AssertionError("missing file must raise FileNotFoundError")
    except FileNotFoundError:
        pass
    print("[OK] lib_parse_one fallback")


def test_merge_and_finalize():
    # 失败清单随二元合并汇聚
    lib_a, _ = lib_utils.lib_parse_one(_write("a.lib", GOOD_LIB))
    merged = lib_flow._lib_merge_two((lib_a, ["a.lib: syntax"]),
                                     (type(lib_a)(), ["b.lib: syntax"]))
    lib, failures = merged
    assert failures == ["a.lib: syntax", "b.lib: syntax"], failures

    # finalize 全成功：返回 lib 本体（正式对象形态不变），不触发 message
    lib_clean, _ = lib_utils.lib_parse_one(_write("clean.lib", GOOD_LIB))
    out = lib_flow._make_finalize(1)((lib_clean, []))
    assert out is lib_clean

    # finalize 部分失败（1/2）：LIBR::0003 message + 返回纯 LIBLibrary
    out = lib_flow._make_finalize(2)((lib, ["a.lib: syntax"]))
    assert len(out.cells) == 1, "successful part must survive"
    print("[OK] merge carries failures / finalize partial fallback")


def test_finalize_all_failed_fatal():
    # 全败分支（LIBR::0004 fatal → _exit(80)）：子进程隔离断言退出码。
    # PYTHONPATH 继承父进程 sys.path（import emir 全链的 .so 目录由 bazel
    # bootstrap 收集，子进程复用同一批路径）。
    script = (
        "from emir.lib.py import lib_flow\n"
        "lib_flow._make_finalize(2)((object(), ['a.lib: syntax', 'b.lib: syntax']))\n"
    )
    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join(p for p in sys.path if p)
    proc = subprocess.run([sys.executable, "-c", script], env=env,
                          capture_output=True, text=True, timeout=60)
    assert proc.returncode == 80, \
        f"all-failed finalize must fatal-exit 80, got {proc.returncode}\n{proc.stderr}"
    print("[OK] finalize all-failed fatal exit 80 (subprocess)")


test_sniff_liberty_header()
test_parse_one_fallback()
test_merge_and_finalize()
test_finalize_all_failed_fatal()
print("[PASS] test_lib_flow_error")
