"""spef flow 错误处理单测（dev-rules §7.2 二元处置范式 + §3 alpha 声明
式校验，同 test_wf_flow_error.py 先例）。

覆盖：
  - normalize_spef_files：绑定描述符结构规范化（plan §2 三形态），非法
    结构已由 header schema 拦截（见 test_sp_db_validation.py）
  - SPAlphaSettings：两键 validator（chunk_size_mb >= 16、value_index
    >= 0 缺省 None 表示未给）拒绝回退不 raise；normalize 兜底
  - sp_plan_file_chunks 绑定层形态：返回 (切块计划, 文件头)——文件头里
    有单位换算系数与分隔符；缺 *C_UNIT / 单位词非法抛异常
  - sp_read_byte_range 越界抛异常（C1）
  - gzip 压缩 SPEF：流程层解压后能解析（见 test_sp_flow_structure 的
    文件入口任务用例）；压缩文件直喂 C++ 接口明确报错（防御面）
  - 全部文件失败 → SPMG::0008 fatal 子进程断言 rc=80（范式 (a)）——
    产物走真实 C++ 路径构造：合法头部 SPEF 过切块扫描后，用绑定目标
    不存在的绑定做换算（空分片 + failed_chunk 计数）
  - 段级破损的文件（某个网记录残缺）仍然能建库：破损网被跳过并计数，
    其它网照常

端到端场景在 qa/emir/（下一阶段）。

模块内部单测：直连 py 包内部符号（utils 不对外导出；BUILD imports 指
向 src 根 + .so export 目录，同 test_wf_flow_error.py 先例）。
"""
import gzip
import os
import subprocess
import sys
import tempfile

from emir.spef.py import sp_utils
from emir.spef.py.alpha_settings import (
    SPAlphaSettings,
    get_default_alpha_settings,
    is_chunk_size_mb,
    is_value_index,
)


def _write(name, text):
    path = os.path.join(tempfile.mkdtemp(prefix="sp_flow_err_"), name)
    with open(path, "w") as f:
        f.write(text)
    return path


GOOD_SPEF = ('*SPEF "IEEE 1481-1998"\n*DIVIDER /\n*DELIMITER :\n'
             "*C_UNIT 1 FF\n*R_UNIT 1 OHM\n"
             "*D_NET n_a 1.0\n*CAP\n1 n_a:1 1.0\n*END\n"
             "*D_NET n_b 2.0\n*CAP\n1 n_b:1 2.0\n*END\n")


def test_normalize_spef_files():
    # 纯路径形态
    out = sp_utils.normalize_spef_files(["a.spef"])
    assert out == [{"file_name": "a.spef", "kind": 0, "block_inst": "",
                    "block_cell": "", "strip_prefix": ""}], out
    # 块实例绑定 + strip_prefix（形态 2）
    out = sp_utils.normalize_spef_files(
        [{"file_name": "b.spef", "block_inst": "c1",
          "strip_prefix": "tb/u_dut"}])
    assert out[0]["kind"] == 1 and out[0]["block_inst"] == "c1"
    assert out[0]["strip_prefix"] == "tb/u_dut"
    # 块定义绑定（形态 3）
    out = sp_utils.normalize_spef_files(
        [{"file_name": "c.spef", "block_cell": "B1"}])
    assert out[0]["kind"] == 2 and out[0]["block_cell"] == "B1"
    print("[OK] normalize_spef_files")


def test_alpha_settings():
    settings = get_default_alpha_settings()
    assert settings.chunk_size_mb == 256
    assert settings.value_index is None   # 未给的缺省形态
    # 合法覆盖
    result = settings.apply({"chunk_size_mb": 64, "value_index": 1})
    assert not result["rejected"] and not result["unknown"]
    assert settings.chunk_size_mb == 64 and settings.value_index == 1
    # 非法值 → rejected 保留默认（不 raise）
    for bad in ({"chunk_size_mb": 15}, {"chunk_size_mb": True},
                {"chunk_size_mb": "big"}, {"value_index": -1},
                {"value_index": "0"}, {"value_index": 1.5}):
        s2 = get_default_alpha_settings()
        r = s2.apply(bad)
        assert len(r["rejected"]) == 1, (bad, r)
    s3 = get_default_alpha_settings()
    s3.apply({"chunk_size_mb": 8})
    assert s3.chunk_size_mb == 256 and s3.value_index is None
    # 未知键 → unknown
    s4 = get_default_alpha_settings()
    r = s4.apply({"no_such_key": 1})
    assert r["unknown"] == ["no_such_key"] and not r["rejected"]
    # normalize 兜底（旧对象缺键补默认）
    s5 = SPAlphaSettings()
    del s5._values["chunk_size_mb"]
    del s5._values["value_index"]
    s5.normalize()
    assert s5.chunk_size_mb == 256 and s5.value_index is None
    # validator 值域（header schema 与 AlphaSetting 共用同一函数——
    # 值域单一来源）
    assert is_chunk_size_mb(16) and not is_chunk_size_mb(15)
    assert not is_chunk_size_mb(True)
    assert is_value_index(0) and not is_value_index(-1)
    assert not is_value_index(True)
    print("[OK] SPAlphaSettings validators + normalize")


def test_plan_file_chunks_returns_plan_and_header():
    from emir.spef import sp_plan_file_chunks
    # 绑定层返回 (plan, header) 二元组；文件头有单位换算系数与分隔符
    ok = _write("ok.spef", GOOD_SPEF)
    plan, header = sp_plan_file_chunks(ok, 1024 * 1024)
    assert len(plan.chunks) == 1
    assert header.cap_scale_ff == 1.0          # 1 FF = 1 fF
    assert header.res_scale_ohm == 1.0
    assert header.divider == "/" and header.delimiter == ":"
    assert header.cap_unit_decl == "1 FF"
    print("[OK] sp_plan_file_chunks (plan, header)")


def test_plan_file_chunks_file_level_errors():
    from emir.spef import sp_plan_file_chunks
    # 缺 *C_UNIT（文件级错误——文件入口任务失败透出的本体）
    no_c_unit = _write(
        "no_c_unit.spef",
        '*SPEF "IEEE 1481-1998"\n*R_UNIT 1 OHM\n'
        "*D_NET n_a 1.0\n*END\n")
    try:
        sp_plan_file_chunks(no_c_unit, 1024 * 1024)
        raise AssertionError("missing *C_UNIT must raise")
    except RuntimeError as e:
        assert "C_UNIT" in str(e), str(e)
    # 单位词非法
    bad_unit = _write(
        "bad_unit.spef",
        '*SPEF "IEEE 1481-1998"\n*C_UNIT 1 UF\n*R_UNIT 1 OHM\n'
        "*D_NET n_a 1.0\n*END\n")
    try:
        sp_plan_file_chunks(bad_unit, 1024 * 1024)
        raise AssertionError("unknown unit word must raise")
    except RuntimeError as e:
        assert "unit" in str(e).lower(), str(e)
    print("[OK] sp_plan_file_chunks file-level errors")


def test_read_byte_range_out_of_range():
    from emir.spef import sp_read_byte_range
    p = _write("range.spef", "0123456789")
    assert sp_read_byte_range(p, 2, 5) == "234"
    try:
        sp_read_byte_range(p, 0, 11)
        raise AssertionError("out-of-range byte range must raise")
    except RuntimeError as e:
        assert "range" in str(e).lower() or "size" in str(e).lower(), str(e)
    print("[OK] sp_read_byte_range out-of-range")


def test_gzip_rejected_by_direct_cpp_api():
    # 压缩文件直喂 C++ 接口明确报错（防御面——正常链流程层已在文件入口
    # 任务解压；解压后可解析在 test_sp_flow_structure 的文件入口任务用例）
    from emir.spef import sp_plan_file_chunks
    gz = os.path.join(tempfile.mkdtemp(prefix="sp_flow_err_"), "a.spef.gz")
    with gzip.open(gz, "wb") as f:
        f.write(GOOD_SPEF.encode())
    try:
        sp_plan_file_chunks(gz, 1024 * 1024)
        raise AssertionError("gzip spef must be rejected")
    except RuntimeError as e:
        assert "gzip" in str(e).lower() or "compress" in str(e).lower(), \
            str(e)
    print("[OK] gzip rejected by direct C++ API")


def test_all_files_failed_fatals_exit_80():
    # 全部文件失败（SPMG::0008 fatal → _exit(80)）：子进程隔离断言退出
    # 码。产物走真实 C++ 路径构造（合法头部 SPEF 过切块扫描 →
    # sp_convert_chunk 绑定目标未命中兜底：空分片 + failed_chunk=1）。
    # 绑定形态用 kind=2 + 不存在的块 cell 名——空 EXDSDesign 的 cell
    # 名 hasher 查询安全返回未命中（kind=1 路径要遍历空层级树，不适
    # 合夹具）。
    script = (
        "import os, tempfile\n"
        "from fly import run_direct\n"
        "from emir.spef.py import sp_flow\n"
        "from emir.spef import (EXSPDesignContext, EXSPFileBinding,\n"
        "    sp_plan_file_chunks, sp_parse_chunk_text, sp_convert_chunk)\n"
        "from emir.design import EXDSDesign\n"
        "good = ('*SPEF \"IEEE 1481-1998\"\\n*DIVIDER /\\n'\n"
        "        '*DELIMITER :\\n*C_UNIT 1 FF\\n*R_UNIT 1 OHM\\n'\n"
        "        '*D_NET n_a 1.0\\n*CAP\\n1 n_a:1 1.0\\n*END\\n')\n"
        "d = tempfile.mkdtemp(prefix='sp_fatal_')\n"
        "path = os.path.join(d, 'ok.spef')\n"
        "open(path, 'w').write(good)\n"
        "plan, header = sp_plan_file_chunks(path, 1024 * 1024)\n"
        "binding = EXSPFileBinding()\n"
        "binding.set_kind(2)\n"      # 绑定目标不存在（ghost 块 cell）
        "binding.block_cell = 'ghost'\n"
        "ctx = EXSPDesignContext()\n"
        "ctx.set_design(EXDSDesign())\n"   # 空 design：定位必失败
        "parsed = sp_parse_chunk_text(\n"
        "    '*D_NET n_a 1.0\\n*CAP\\n1 n_a:1 1.0\\n*END\\n', header, 0,\n"
        "    False, path)\n"
        "slice_obj = sp_convert_chunk(ctx, header, parsed, 0, 0, binding,\n"
        "    path)\n"
        "assert slice_obj.stats.files[0].failed_chunk_count == 1, "
        "'fixture must be a failed chunk'\n"
        "from emir.spef import EXSPMergeStats\n"
        "class Db:\n"
        "    def __init__(self):\n"
        "        self.removed = []\n"
        "    def read_object(self, k, **kw):\n"
        "        if 'chunk' in k:\n"
        "            return slice_obj\n"
        "        if 'counts' in k:\n"
        "            return EXSPMergeStats()   # 合并统计（真实流程由分\n"
        "                                      # 区合并任务写 temp）\n"
        "        if 'manifest' in k:\n"
        "            return {'chunk_count': 1, 'source_name': path,\n"
        "                    'header_key': '__spg__header_0',\n"
        "                    'scratch_dir': ''}\n"
        "        raise KeyError(k)\n"
        "    def write_object(self, k, v, **kw):\n"
        "        pass\n"
        "    def remove_object(self, k, **kw):\n"
        "        self.removed.append(k)\n"
        "files = [{'file_name': path, 'kind': 2, 'block_inst': '', "
        "'block_cell': 'ghost', 'strip_prefix': ''}]\n"
        "run_direct(sp_flow._sp_merge_summary_task, Db(), files, [0], "
        "['__spg__chunk_0_0'], ['__spg__counts_0'], ['__spg__manifest_0'], "
        "['__spg__header_0'], [], [], 0, 'summary')\n"
    )
    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join(p for p in sys.path if p)
    proc = subprocess.run([sys.executable, "-c", script], env=env,
                          capture_output=True, text=True, timeout=60)
    assert proc.returncode == 80, (
        f"all-failed spef files must fatal-exit 80, "
        f"got {proc.returncode}\n{proc.stderr}")
    print("[OK] all-files-failed: fatal exit 80 (subprocess)")


def test_broken_net_still_builds():
    # 段级破损（某个网记录残缺）仍然解析：破损网被跳过并计数，其它网照
    # 常收录——库可以继续建（范式 (b) 的解析面）。绑定层 sp_parse_spef_
    # text 返回 (result, header) 二元组
    from emir.spef import sp_parse_spef_text
    text = GOOD_SPEF.replace(
        "*D_NET n_b 2.0\n*CAP\n1 n_b:1 2.0\n*END\n",
        "*D_NET n_b 2.0\n*CAP\n1 n_b:1 abc\n*END\n"
        "*D_NET n_c 3.0\n*CAP\n1 n_c:1 3.0\n*END\n")
    result, header = sp_parse_spef_text(text, 0, False, "t")
    assert result.net_count == 2, result.net_count
    names = [n.name for n in result.nets]
    assert names == ["n_a", "n_c"], names
    assert result.broken_net_count == 1
    assert header.design_name == ""   # 头部产物随返回值一并给出
    print("[OK] broken net skipped and counted")


if __name__ == "__main__":
    test_normalize_spef_files()
    test_alpha_settings()
    test_plan_file_chunks_returns_plan_and_header()
    test_plan_file_chunks_file_level_errors()
    test_read_byte_range_out_of_range()
    test_gzip_rejected_by_direct_cpp_api()
    test_all_files_failed_fatals_exit_80()
    test_broken_net_still_builds()
    print("[PASS] test_sp_flow_error")
