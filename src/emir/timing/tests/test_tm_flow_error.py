"""timing flow 错误处理单测（dev-rules §7.2 二元处置范式 + §3 alpha 声明
式校验，同 test_ds_flow_error.py 先例）。

覆盖：
  - sniff_twf_header：TWF 形态判定（TIMING_WINDOWS 关键字），误传文件
    ValueError 秒级拦截（plan §3.1 入口校验）
  - normalize_timing_files：绑定描述符结构校验（plan §2 三形态 + 键
    白名单 + 互斥约束），非法 ValueError
  - TMAlphaSettings：两键 validator（chunk_size_mb >= 16、format 方言
    白名单）拒绝回退不 raise；normalize 兜底
  - 全部文件失败 → TIMG::0009 fatal 子进程断言 rc=80（范式 (a)）——
    产物走真实 C++ 路径构造：破损 TWF → tm_plan_file_chunks 真实块表 →
    tm_convert_chunk 破损兜底（空分片 + failed_chunk=1）→ T4 fatal 判定

端到端场景在 qa/emir/test_emir_timing_flow.py。

模块内部单测：直连 py 包内部符号（utils 不对外导出；BUILD imports 指向
src 根 + .so export 目录，同 test_ds_flow_error.py 先例）。
"""
import os
import subprocess
import sys
import tempfile

from emir.timing.py import tm_utils
from emir.timing.py.alpha_settings import (
    TMAlphaSettings,
    get_default_alpha_settings,
)


def _write(name, text):
    path = os.path.join(tempfile.mkdtemp(prefix="tm_flow_err_"), name)
    with open(path, "w") as f:
        f.write(text)
    return path


GOOD_TWF = ("(TIMING_WINDOWS\n(HEADER (VERSION \"t\") (TIME_SCALE "
            "1.000E-09))\n)\n")


def test_sniff_twf_header():
    ok = _write("ok.twf", GOOD_TWF)
    tm_utils.sniff_twf_header(ok)
    # 关键字不在头部（4096 字节嗅探窗口外）→ ValueError
    late = _write("late.twf", "x" * 5000 + "(TIMING_WINDOWS\n")
    try:
        tm_utils.sniff_twf_header(late)
        raise AssertionError("keyword beyond sniff window passed")
    except ValueError as e:
        assert "TIMING_WINDOWS" in str(e), str(e)
    # liberty 误传 → ValueError
    lib = _write("fake.twf", "library (typ) { }\n")
    try:
        tm_utils.sniff_twf_header(lib)
        raise AssertionError("liberty file passed twf sniff")
    except ValueError as e:
        assert "TIMING_WINDOWS" in str(e) and lib in str(e), str(e)
    print("[OK] sniff_twf_header")


def test_normalize_timing_files():
    # 纯路径形态
    out = tm_utils.normalize_timing_files(["a.twf"])
    assert out == [{"file_name": "a.twf", "kind": 0, "block_inst": "",
                    "block_cell": "", "strip_prefix": ""}], out
    # 块实例绑定 + strip_prefix（plan §2 形态 4）
    out = tm_utils.normalize_timing_files(
        [{"file_name": "b.twf", "block_inst": "c1",
          "strip_prefix": "tb/u_dut"}])
    assert out[0]["kind"] == 1 and out[0]["block_inst"] == "c1"
    assert out[0]["strip_prefix"] == "tb/u_dut"
    # 块定义绑定（plan §2 形态 3）
    out = tm_utils.normalize_timing_files(
        [{"file_name": "c.twf", "block_cell": "B1"}])
    assert out[0]["kind"] == 2 and out[0]["block_cell"] == "B1"
    # 结构负例已由 header schema 拦截（2026-09-17 裁定：normalize 只保留
    # 规范化职责）——见 test_tm_db_validation.py 的 timing_files 结构负例
    print("[OK] normalize_timing_files")


def test_alpha_settings():
    settings = get_default_alpha_settings()
    assert settings.chunk_size_mb == 256 and settings.format == "auto"
    # 合法覆盖
    result = settings.apply({"chunk_size_mb": 64, "format": "innovus"})
    assert not result["rejected"] and not result["unknown"]
    assert settings.chunk_size_mb == 64 and settings.format == "innovus"
    # 非法值 → rejected 保留默认（不 raise）
    for bad in ({"chunk_size_mb": 15}, {"chunk_size_mb": True},
                {"chunk_size_mb": "big"}, {"format": "redhawk"}):
        s2 = get_default_alpha_settings()
        r = s2.apply(bad)
        assert len(r["rejected"]) == 1, (bad, r)
        assert s2.chunk_size_mb == 256 and s2.format == "auto"
    # 未知键 → unknown
    s3 = get_default_alpha_settings()
    r = s3.apply({"no_such_key": 1})
    assert r["unknown"] == ["no_such_key"] and not r["rejected"]
    # normalize 兜底（旧对象缺键补默认）
    s4 = TMAlphaSettings()
    del s4._values["chunk_size_mb"]
    s4.normalize()
    assert s4.chunk_size_mb == 256
    print("[OK] TMAlphaSettings validators + normalize")


def test_all_files_failed_fatals_exit_80():
    # 全部文件失败（TIMG::0009 fatal → _exit(80)）：子进程隔离断言退出码。
    # 产物走真实 C++ 路径构造（EXTMFileChunkPlan 字段只读——解析面不可
    # Python 侧拼装，真实路径同时覆盖 T1/T2 兜底行为）。
    script = (
        "import os, tempfile\n"
        "from fly import run_direct\n"
        "from emir.timing.py import tm_flow\n"
        "from emir.timing import (EXTMChunkPlan, EXTMDesignContext, "
        "EXTMFileBinding, EXTMStatsDelta, tm_plan_file_chunks, "
        "tm_convert_chunk)\n"
        "d = tempfile.mkdtemp(prefix='tm_fatal_')\n"
        "path = os.path.join(d, 'bad.twf')\n"
        "open(path, 'w').write('(TIMING_WINDOWS (CAUSED_BY NULL (NET "
        "\"n\"')\n"
        "ctx = EXTMDesignContext()\n"
        "fp = tm_plan_file_chunks(path, 1024 * 1024)\n"
        "plan = EXTMChunkPlan()\n"
        "plan.add_file(fp)\n"
        "binding = EXTMFileBinding()\n"
        "binding.set_kind(0)\n"
        "slice_obj = tm_convert_chunk(ctx, path, fp.prefix_start, "
        "fp.prefix_end, fp.chunk_starts[0], "
        "fp.chunk_ends[0], binding, 0)\n"
        "assert slice_obj.stats.files[0].failed_chunk_count == 1, "
        "'fixture must be a failed chunk'\n"
        "class Db:\n"
        "    def __init__(self):\n"
        "        self.objs = {'clock_conflicts': 0}\n"
        "    def read_object(self, k, **kw):\n"
        "        if k.endswith('plan'):\n"
        "            return plan\n"
        "        if 'chunk' in k:\n"
        "            return slice_obj\n"
        "        return self.objs.get(k.split('__')[-1], 0)\n"
        "    def write_object(self, k, v, **kw):\n"
        "        pass\n"
        "files = [{'file_name': path, 'kind': 0, 'block_inst': '', "
        "'block_cell': '', 'strip_prefix': ''}]\n"
        "run_direct(tm_flow._t4_summary_task, Db(), files, "
        "['chunk_0_0'], ['conflicts_0'], 'clock_conflicts', 'plan', "
        "'summary')\n"
    )
    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join(p for p in sys.path if p)
    proc = subprocess.run([sys.executable, "-c", script], env=env,
                          capture_output=True, text=True, timeout=60)
    assert proc.returncode == 80, (
        f"all-failed timing files must fatal-exit 80, "
        f"got {proc.returncode}\n{proc.stderr}")
    print("[OK] all-files-failed: fatal exit 80 (subprocess)")


if __name__ == "__main__":
    test_sniff_twf_header()
    test_normalize_timing_files()
    test_alpha_settings()
    test_all_files_failed_fatals_exit_80()
    print("[PASS] test_tm_flow_error")
