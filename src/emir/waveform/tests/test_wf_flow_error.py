"""waveform flow 错误处理单测（dev-rules §7.2 二元处置范式 + §3 alpha
声明式校验，同 test_tm_flow_error.py 先例）。

覆盖：
  - normalize_vcd_files：绑定描述符结构规范化（plan §2 三形态），非法
    结构已由 header schema 拦截（见 test_wf_db_validation.py）
  - WFAlphaSettings：单键 validator（chunk_size_mb >= 16）拒绝回退不
    raise；normalize 兜底
  - settings 键级 validator（is_x_z_handling 三枚举 / is_probability /
    is_activity_struct 白名单——default 可选、default/overrides 至少其
    一：混合模式 overrides-only 合法〔裁定 IV-2〕，settings-only 的
    activity 必给判定在 build_waveform_db 入口不变）
  - wf_plan_file_chunks 入口形态语义：档位随头部解析随切块计划产出
    （timescale_fs/timescale_sec）；gzip 压缩 VCD 在 C++ 切块扫描入口
    明确报错（防御面——正常链 Python 层已在文件入口任务解压，裁定
    IV-1，该口径仅在直喂压缩文件时触达）；缺失 $timescale 报错（文件
    形态错在文件入口任务失败透出的本体——透出口不变，形态错仍在切块
    扫描入口）
  - 全部文件失败 → WFMG::0009 fatal 子进程断言 rc=80（范式 (a)）——
    产物走真实 C++ 路径构造：合法头部 VCD 过切块扫描 →
    wf_convert_chunk 绑定目标未命中兜底（空分片 + failed_chunk=1）→
    汇总任务 fatal 判定

端到端场景在 qa/emir/test_emir_waveform_flow.py。

模块内部单测：直连 py 包内部符号（utils 不对外导出；BUILD imports 指
向 src 根 + .so export 目录，同 test_tm_flow_error.py 先例）。
"""
import gzip
import os
import subprocess
import sys
import tempfile

from emir.waveform.py import wf_utils
from emir.waveform.py.alpha_settings import (
    WFAlphaSettings,
    get_default_alpha_settings,
    is_activity_struct,
    is_probability,
    is_x_z_handling,
)


def _write(name, text):
    path = os.path.join(tempfile.mkdtemp(prefix="wf_flow_err_"), name)
    with open(path, "w") as f:
        f.write(text)
    return path


GOOD_VCD = ("$date\n\tnow\n$end\n$version\n\tt\n$end\n"
            "$timescale 1ns $end\n"
            "$scope module top $end\n$var wire 1 ! a $end\n"
            "$upscope $end\n$enddefinitions $end\n"
            "#0\n$dumpvars\n0!\n$end\n#10\n1!\n")


def test_normalize_vcd_files():
    # 纯路径形态
    out = wf_utils.normalize_vcd_files(["a.vcd"])
    assert out == [{"file_name": "a.vcd", "kind": 0, "block_inst": "",
                    "block_cell": "", "strip_prefix": ""}], out
    # 块实例绑定 + strip_prefix（plan §2 形态 2）
    out = wf_utils.normalize_vcd_files(
        [{"file_name": "b.vcd", "block_inst": "c1",
          "strip_prefix": "tb/u_dut"}])
    assert out[0]["kind"] == 1 and out[0]["block_inst"] == "c1"
    assert out[0]["strip_prefix"] == "tb/u_dut"
    # 块定义绑定（plan §2 形态 3）
    out = wf_utils.normalize_vcd_files(
        [{"file_name": "c.vcd", "block_cell": "B1"}])
    assert out[0]["kind"] == 2 and out[0]["block_cell"] == "B1"
    # 结构负例已由 header schema 拦截（2026-09-17 裁定：normalize 只保
    # 留规范化职责）——见 test_wf_db_validation.py 的 vcd_files 结构负例
    print("[OK] normalize_vcd_files")


def test_alpha_settings():
    settings = get_default_alpha_settings()
    assert settings.chunk_size_mb == 256
    # 合法覆盖
    result = settings.apply({"chunk_size_mb": 64})
    assert not result["rejected"] and not result["unknown"]
    assert settings.chunk_size_mb == 64
    # 非法值 → rejected 保留默认（不 raise）
    for bad in ({"chunk_size_mb": 15}, {"chunk_size_mb": True},
                {"chunk_size_mb": "big"}):
        s2 = get_default_alpha_settings()
        r = s2.apply(bad)
        assert len(r["rejected"]) == 1, (bad, r)
        assert s2.chunk_size_mb == 256
    # 未知键 → unknown
    s3 = get_default_alpha_settings()
    r = s3.apply({"no_such_key": 1})
    assert r["unknown"] == ["no_such_key"] and not r["rejected"]
    # normalize 兜底（旧对象缺键补默认）
    s4 = WFAlphaSettings()
    del s4._values["chunk_size_mb"]
    s4.normalize()
    assert s4.chunk_size_mb == 256
    print("[OK] WFAlphaSettings validators + normalize")


def test_settings_validators():
    # x/z 口径三枚举
    for ok in ("denominator_include", "exclude", "hold_previous"):
        assert is_x_z_handling(ok), ok
    assert not is_x_z_handling("drop")
    assert not is_x_z_handling(None)
    # 概率值域 [0, 1]
    assert is_probability(0) and is_probability(0.5) and \
        is_probability(1)
    for bad in (1.5, -0.1, True, "0.5", float("nan")):
        assert not is_probability(bad), bad
    # activity 整体结构：default/overrides 至少其一、白名单严格——
    # overrides-only 合法（混合模式 schema 正例，裁定 IV-2）
    good = {"default": {"toggle_rate": 2.0e6, "static_probability": 0.5}}
    assert is_activity_struct(good)
    assert is_activity_struct(dict(good, duration=2.0,
                                   overrides={"u1/ZN": {
                                       "rise_fraction": 0.6}}))
    assert is_activity_struct({"overrides": {"u1/ZN": {
        "toggle_rate": 1.0e6}}})  # 混合模式：VCD + overrides 无 default
    assert not is_activity_struct({"duration": 1.0})  # 无 default/overrides
    assert not is_activity_struct({"overrides": {}})  # 空壳 activity
    assert not is_activity_struct(dict(good, bogus=1))  # 未知键
    assert not is_activity_struct(dict(good, duration=0))  # 零窗口
    assert not is_activity_struct(
        {"default": {"toggle_rate": 1.0}})  # default 缺 static_probability
    assert not is_activity_struct(
        {"overrides": {"u1/ZN": {"bogus": 1}}})  # overrides 非法值仍拒
    print("[OK] settings validators")


def test_plan_file_chunks_head_semantics():
    from emir.waveform import wf_plan_file_chunks
    # 合法头部：档位随切块计划产出（免二次整读嗅探）+ 切块扫描可用
    ok = _write("ok.vcd", GOOD_VCD)
    plan, vars_table = wf_plan_file_chunks(ok, 1024 * 1024)
    assert plan.timescale_fs == 1000000 and plan.timescale_sec == 1e-9
    assert len(plan.chunks) == 1
    assert len(vars_table.track_ids) == 1
    # gzip 压缩 VCD：直连 C++ API 不支持压缩输入（流程层入口已解压——
    # 裁定 IV-1），魔数嗅探在切块扫描入口明确报错
    gz = os.path.join(tempfile.mkdtemp(prefix="wf_flow_err_"), "a.vcd.gz")
    with gzip.open(gz, "wb") as f:
        f.write(GOOD_VCD.encode())
    try:
        wf_plan_file_chunks(gz, 1024 * 1024)
        raise AssertionError("gzip vcd must be rejected")
    except RuntimeError as e:
        assert "gzip" in str(e).lower() or "compress" in str(e).lower(), \
            str(e)
    # 缺失 $timescale：文件形态错（文件入口任务失败透出的本体）。夹具
    # = 除 $timescale 外头段完整的合法形态——切块扫描在 $enddefinitions
    # 处判缺失 raise（其余头部破损有各自报错，不在此覆盖）
    no_ts = _write("no_ts.vcd",
                   "$date\n\tnow\n$end\n$scope module top $end\n"
                   "$var wire 1 ! a $end\n$upscope $end\n"
                   "$enddefinitions $end\n#0\n0!\n")
    try:
        wf_plan_file_chunks(no_ts, 1024 * 1024)
        raise AssertionError("missing $timescale must raise")
    except RuntimeError as e:
        assert "timescale" in str(e).lower(), str(e)
    print("[OK] wf_plan_file_chunks head semantics "
          "(timescale fields / gzip / missing $timescale)")


def test_all_files_failed_fatals_exit_80():
    # 全部文件失败（WFMG::0009 fatal → _exit(80)）：子进程隔离断言退出
    # 码。产物走真实 C++ 路径构造（合法 VCD 过切块扫描后文件删除 →
    # wf_convert_chunk 运行期不可读兜底：空分片 + failed_chunk=1）。
    script = (
        "import os, tempfile\n"
        "from fly import run_direct\n"
        "from emir.waveform.py import wf_flow\n"
        "from emir.waveform import (EXWFDesignContext, EXWFFileBinding,\n"
        "    EXWFSettingsMeta, wf_plan_file_chunks, wf_convert_chunk)\n"
        "good = ('$date\\n\\tnow\\n$end\\n$version\\n\\tt\\n$end\\n'\n"
        "        '$timescale 1ns $end\\n'\n"
        "        '$scope module top $end\\n$var wire 1 ! a $end\\n'\n"
        "        '$upscope $end\\n$enddefinitions $end\\n'\n"
        "        '#0\\n$dumpvars\\n0!\\n$end\\n#10\\n1!\\n')\n"
        "d = tempfile.mkdtemp(prefix='wf_fatal_')\n"
        "path = os.path.join(d, 'ok.vcd')\n"
        "open(path, 'w').write(good)\n"
        "plan, vars_table = wf_plan_file_chunks(path, 1024 * 1024)\n"
        "os.unlink(path)\n"  # 运行期不可读 → 块失败兜底（范式 (b) 计数）
        "binding = EXWFFileBinding()\n"
        "binding.set_kind(0)\n"
        "vars_table.rebuild_indexes()\n"
        "ctx = EXWFDesignContext()\n"  # PATH 绑定 + 读失败早退：ctx 不解引
        "slice_obj = wf_convert_chunk(ctx, path, plan.chunks[0],\n"
        "    vars_table, 1000000, binding, 0)\n"
        "assert slice_obj.stats.files[0].failed_chunk_count == 1, "
        "'fixture must be a failed chunk'\n"
        "class Db:\n"
        "    def read_object(self, k, **kw):\n"
        "        if 'chunk' in k:\n"
        "            return slice_obj\n"
        "        if 'counts' in k:\n"
        "            return {'overlap_override_count': 0,\n"
        "                    'time_anomaly_count': 0, 'waveform_count': 0,\n"
        "                    'override_entry_count': 0,\n"
        "                    'applied_names': []}\n"
        "        if 'manifest' in k:\n"
        "            return {'chunk_count': 1, 'parse_name': path,\n"
        "                    'vars_key': '__wfg__vars_0',\n"
        "                    'scratch_dir': None}\n"
        "        raise KeyError(k)\n"
        "    def write_object(self, k, v, **kw):\n"
        "        pass\n"
        "    def remove_object(self, k, **kw):\n"
        "        pass\n"
        "files = [{'file_name': path, 'kind': 1, 'block_inst': 'ghost', "
        "'block_cell': '', 'strip_prefix': ''}]\n"
        "meta = {'default_toggle_rate': 0.0, "
        "'default_static_probability': 0.5, 'duration_sec': 1.0,\n"
        "        'x_z_handling': 'denominator_include'}\n"
        "run_direct(wf_flow._wf_merge_summary_task, Db(), files, "
        "['__wfg__chunk_0_0'], ['__wfg__counts_0'], ['__wfg__manifest_0'], "
        "['__wfg__vars_0'], [], [], meta, [], 'summary')\n"
    )
    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join(p for p in sys.path if p)
    proc = subprocess.run([sys.executable, "-c", script], env=env,
                          capture_output=True, text=True, timeout=60)
    assert proc.returncode == 80, (
        f"all-failed waveform files must fatal-exit 80, "
        f"got {proc.returncode}\n{proc.stderr}")
    print("[OK] all-files-failed: fatal exit 80 (subprocess)")


if __name__ == "__main__":
    test_normalize_vcd_files()
    test_alpha_settings()
    test_settings_validators()
    test_plan_file_chunks_head_semantics()
    test_all_files_failed_fatals_exit_80()
    print("[PASS] test_wf_flow_error")
