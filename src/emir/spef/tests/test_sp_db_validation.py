"""build_spef_db 入口参数校验负例单测（2026-09-17 裁定范式：header Schema
直接校验、错误直接 raise 终止、白名单严格模式——spef_files 两形态结构
（纯路径 / 块绑定描述符）、settings「当前无可用键」、alpha 两键值域、
未知键一律报错）。"""


class _FakeProject:
    def _create_db(self, *args, **kwargs):
        raise AssertionError("invalid input must not create a db")


class _FakeDesignDb:
    DESIGN_OBJ = "DSDesign"


def _call(**overrides):
    from emir.spef import build_spef_db
    kwargs = dict(name="spef", spef_files=["d.spef"],
                  design_db=_FakeDesignDb())
    kwargs.update(overrides)
    return build_spef_db(_FakeProject(), **kwargs)


def test_name_empty_rejected():
    try:
        _call(name="")
        raise AssertionError("empty name must raise")
    except ValueError as e:
        assert "name" in str(e) and "non-empty" in str(e), str(e)


def test_spef_files_non_list_rejected():
    try:
        _call(spef_files="d.spef")
        raise AssertionError("non-list spef_files must raise")
    except ValueError as e:
        assert "spef_files" in str(e), str(e)


def test_spef_files_empty_list_rejected():
    try:
        _call(spef_files=[])
        raise AssertionError("empty spef_files must raise")
    except ValueError as e:
        assert "spef_files" in str(e) and "min" in str(e), str(e)


def test_spef_files_int_element_rejected():
    try:
        _call(spef_files=[42])
        raise AssertionError("int element must raise")
    except ValueError as e:
        assert "spef_files[0]" in str(e), str(e)


def test_spef_files_empty_str_element_rejected():
    try:
        _call(spef_files=["ok.spef", ""])
        raise AssertionError("empty path string must raise")
    except ValueError as e:
        assert "spef_files[1]" in str(e) and "non-empty" in str(e), str(e)


def test_binding_desc_missing_file_name_rejected():
    try:
        _call(spef_files=[{"block_inst": "top/u1"}])
        raise AssertionError("missing file_name must raise")
    except ValueError as e:
        assert "file_name" in str(e), str(e)


def test_binding_desc_both_block_keys_rejected():
    try:
        _call(spef_files=[{"file_name": "d.spef", "block_inst": "a",
                           "block_cell": "b"}])
        raise AssertionError("block_inst + block_cell must raise")
    except ValueError as e:
        assert "vcd" not in str(e)
        assert "spef_files[0]" in str(e), str(e)
        assert "block_inst" in str(e) and "block_cell" in str(e), str(e)


def test_binding_desc_missing_binding_target_rejected():
    try:
        _call(spef_files=[{"file_name": "d.spef"}])
        raise AssertionError("file-only binding dict must raise")
    except ValueError as e:
        assert "spef_files[0]" in str(e), str(e)


def test_binding_desc_strip_prefix_on_plain_path_rejected():
    """strip_prefix 出现在纯路径描述符（无 block_inst/block_cell）里拒
    ——剥前缀只能伴随块绑定（同 waveform 的互斥 check 口径）。"""
    try:
        _call(spef_files=[{"file_name": "d.spef",
                           "strip_prefix": "tb/u_dut"}])
        raise AssertionError("strip_prefix on plain path must raise")
    except ValueError as e:
        assert "spef_files[0]" in str(e), str(e)


def test_binding_desc_unknown_key_rejected():
    try:
        _call(spef_files=[{"file_name": "d.spef", "block_inst": "a",
                           "extra": 1}])
        raise AssertionError("unknown binding key must raise")
    except ValueError as e:
        assert "spef_files[0]" in str(e) and "extra" in str(e), str(e)


def test_missing_file_rejected():
    try:
        _call(spef_files=["/nonexistent/dir/d.spef"])
        raise AssertionError("missing file must raise")
    except FileNotFoundError as e:
        assert "build_spef_db" in str(e) and "spef_files" in str(e), str(e)
        assert "/nonexistent/dir/d.spef" in str(e), str(e)


def test_unreadable_file_rejected():
    """不可读文件 → PermissionError。root 进程对 0000 权限文件仍可读
    （内核 root 豁免），os.access 打桩构造不可读形态（存在性由上一个
    用例的真实路径覆盖）。"""
    import os
    import tempfile
    from unittest import mock
    work = tempfile.mkdtemp(prefix="sp_db_val_")
    path = os.path.join(work, "noread.spef")
    with open(path, "w") as f:
        f.write("*SPEF\n")
    with mock.patch("os.access", return_value=False):
        try:
            _call(spef_files=[path])
            raise AssertionError("unreadable file must raise")
        except PermissionError as e:
            assert "build_spef_db" in str(e) and "spef_files" in str(e), \
                str(e)


def test_design_db_wrong_type_rejected():
    try:
        _call(design_db=object())
        raise AssertionError("non-DesignDb design_db must raise")
    except ValueError as e:
        assert "design_db" in str(e) and "DesignDb" in str(e), str(e)


def test_settings_any_key_rejected():
    """settings 首版没有可用键——给任何键直接报错（「当前无可用键」的
    extra_error 写法，同 lib 模块）。"""
    try:
        _call(settings={"anything": 1})
        raise AssertionError("non-empty settings must raise")
    except ValueError as e:
        assert "settings" in str(e), str(e)


def test_alpha_unknown_key_rejected():
    try:
        _call(alpha={"nonexistent_key": 1})
        raise AssertionError("unknown alpha key must raise")
    except ValueError as e:
        assert "alpha" in str(e) and "nonexistent_key" in str(e), str(e)


def test_alpha_chunk_size_mb_below_16_rejected():
    try:
        _call(alpha={"chunk_size_mb": 15})
        raise AssertionError("chunk_size_mb < 16 must raise")
    except ValueError as e:
        assert "chunk_size_mb" in str(e) and "16" in str(e), str(e)


def test_alpha_bool_disguised_chunk_size_rejected():
    try:
        _call(alpha={"chunk_size_mb": True})
        raise AssertionError("bool-disguised chunk_size_mb must raise")
    except ValueError as e:
        assert "chunk_size_mb" in str(e), str(e)


def test_alpha_str_chunk_size_rejected():
    try:
        _call(alpha={"chunk_size_mb": "256"})
        raise AssertionError("str chunk_size_mb must raise")
    except ValueError as e:
        assert "chunk_size_mb" in str(e), str(e)


def test_alpha_negative_value_index_rejected():
    try:
        _call(alpha={"value_index": -1})
        raise AssertionError("negative value_index must raise")
    except ValueError as e:
        assert "value_index" in str(e), str(e)


def test_alpha_non_int_value_index_rejected():
    for bad in ("0", 1.5, True):
        try:
            _call(alpha={"value_index": bad})
            raise AssertionError(f"bad value_index {bad!r} must raise")
        except ValueError as e:
            assert "value_index" in str(e), (bad, str(e))


def test_valid_plain_path_passes_schema_reaches_file_check():
    """合法纯路径形态穿过 header → 函数体文件校验 FileNotFoundError
    （结构校验通过的证据）。"""
    try:
        _call(spef_files=["/nonexistent/d.spef"])
        raise AssertionError("missing file must raise (schema passed)")
    except FileNotFoundError:
        pass


def test_valid_block_inst_binding_passes_schema():
    """块实例绑定描述符穿过 header → 文件校验拦截。"""
    try:
        _call(spef_files=[{"file_name": "/nonexistent/d.spef",
                           "block_inst": "top/u_blk",
                           "strip_prefix": "tb"}])
        raise AssertionError("missing file must raise (schema passed)")
    except FileNotFoundError:
        pass


def test_valid_block_cell_binding_passes_schema():
    """块定义绑定描述符穿过 header → 文件校验拦截。"""
    try:
        _call(spef_files=[{"file_name": "/nonexistent/d.spef",
                           "block_cell": "BLK_S"}])
        raise AssertionError("missing file must raise (schema passed)")
    except FileNotFoundError:
        pass


def test_valid_alpha_passes_schema():
    """合法 alpha（两键同给）穿过 header → 文件校验拦截。"""
    try:
        _call(spef_files=["/nonexistent/d.spef"],
              alpha={"chunk_size_mb": 64, "value_index": 1})
        raise AssertionError("missing file must raise (schema passed)")
    except FileNotFoundError:
        pass


if __name__ == "__main__":
    test_name_empty_rejected()
    test_spef_files_non_list_rejected()
    test_spef_files_empty_list_rejected()
    test_spef_files_int_element_rejected()
    test_spef_files_empty_str_element_rejected()
    test_binding_desc_missing_file_name_rejected()
    test_binding_desc_both_block_keys_rejected()
    test_binding_desc_missing_binding_target_rejected()
    test_binding_desc_strip_prefix_on_plain_path_rejected()
    test_binding_desc_unknown_key_rejected()
    test_missing_file_rejected()
    test_unreadable_file_rejected()
    test_design_db_wrong_type_rejected()
    test_settings_any_key_rejected()
    test_alpha_unknown_key_rejected()
    test_alpha_chunk_size_mb_below_16_rejected()
    test_alpha_bool_disguised_chunk_size_rejected()
    test_alpha_str_chunk_size_rejected()
    test_alpha_negative_value_index_rejected()
    test_alpha_non_int_value_index_rejected()
    test_valid_plain_path_passes_schema_reaches_file_check()
    test_valid_block_inst_binding_passes_schema()
    test_valid_block_cell_binding_passes_schema()
    test_valid_alpha_passes_schema()
    print("[PASS] test_sp_db_validation")
