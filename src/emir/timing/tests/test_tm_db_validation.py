"""build_timing_db 入口参数校验负例单测（2026-09-17 裁定：header Schema
直接校验、错误直接 raise 终止、白名单严格模式——timing_files 三形态
结构校验、alpha 两键值域、未知键一律报错）。"""


class _FakeProject:
    def _create_db(self, *args, **kwargs):
        raise AssertionError("invalid input must not create a db")


class _FakeDesignDb:
    DESIGN_OBJ = "DSDesign"


def _call(**overrides):
    from emir.timing import build_timing_db
    kwargs = dict(name="timing", timing_files=["d.twf"],
                  design_db=_FakeDesignDb())
    kwargs.update(overrides)
    return build_timing_db(_FakeProject(), **kwargs)


def test_name_empty_rejected():
    try:
        _call(name="")
        raise AssertionError("empty name must raise")
    except ValueError as e:
        assert "name" in str(e) and "non-empty" in str(e), str(e)


def test_timing_files_empty_list_rejected():
    try:
        _call(timing_files=[])
        raise AssertionError("empty timing_files must raise")
    except ValueError as e:
        assert "timing_files" in str(e) and "min" in str(e), str(e)


def test_timing_files_non_str_non_dict_element_rejected():
    try:
        _call(timing_files=[42])
        raise AssertionError("int element must raise")
    except ValueError as e:
        assert "timing_files[0]" in str(e), str(e)


def test_timing_files_empty_str_element_rejected():
    try:
        _call(timing_files=["ok.twf", ""])
        raise AssertionError("empty path string must raise")
    except ValueError as e:
        assert "timing_files[1]" in str(e) and "non-empty" in str(e), str(e)


def test_binding_desc_missing_file_name_rejected():
    try:
        _call(timing_files=[{"block_inst": "top/u1"}])
        raise AssertionError("missing file_name must raise")
    except ValueError as e:
        assert "file_name" in str(e), str(e)


def test_binding_desc_unknown_key_rejected():
    try:
        _call(timing_files=[{"file_name": "d.twf", "block_inst": "a",
                             "extra": 1}])
        raise AssertionError("unknown binding key must raise")
    except ValueError as e:
        assert "timing_files[0]" in str(e) and "extra" in str(e), str(e)


def test_binding_desc_both_block_keys_rejected():
    try:
        _call(timing_files=[{"file_name": "d.twf", "block_inst": "a",
                             "block_cell": "b"}])
        raise AssertionError("block_inst + block_cell must raise")
    except ValueError as e:
        assert "timing_files[0]" in str(e), str(e)
        assert "block_inst" in str(e) and "block_cell" in str(e), str(e)


def test_binding_desc_missing_binding_target_rejected():
    try:
        _call(timing_files=[{"file_name": "d.twf"}])
        raise AssertionError("file-only binding dict must raise")
    except ValueError as e:
        assert "timing_files[0]" in str(e), str(e)


def test_binding_desc_strip_prefix_non_str_rejected():
    try:
        _call(timing_files=[{"file_name": "d.twf", "block_cell": "blk",
                             "strip_prefix": 7}])
        raise AssertionError("non-str strip_prefix must raise")
    except ValueError as e:
        assert "timing_files[0]" in str(e), str(e)
        assert "expected str" in str(e) and "int" in str(e), str(e)


def test_design_db_wrong_type_rejected():
    try:
        _call(design_db=object())
        raise AssertionError("non-DesignDb design_db must raise")
    except ValueError as e:
        assert "design_db" in str(e) and "DesignDb" in str(e), str(e)


def test_settings_unknown_key_rejected():
    try:
        _call(settings={"anything": 1})
        raise AssertionError("unknown settings key must raise")
    except ValueError as e:
        assert "settings" in str(e) and "no available keys" in str(e), str(e)


def test_alpha_unknown_key_rejected():
    try:
        _call(alpha={"nonexistent_key": 1})
        raise AssertionError("unknown alpha key must raise")
    except ValueError as e:
        assert "alpha" in str(e) and "nonexistent_key" in str(e), str(e)


def test_alpha_chunk_size_mb_below_16_rejected():
    try:
        _call(alpha={"chunk_size_mb": 8})
        raise AssertionError("chunk_size_mb < 16 must raise")
    except ValueError as e:
        assert "chunk_size_mb" in str(e) and "16" in str(e), str(e)


def test_alpha_format_misspelled_rejected():
    try:
        _call(alpha={"format": "redhawk"})
        raise AssertionError("misspelled format must raise")
    except ValueError as e:
        assert "format" in str(e) and "auto" in str(e), str(e)
        assert "innovus" in str(e), str(e)


def test_alpha_bool_disguised_chunk_size_rejected():
    try:
        _call(alpha={"chunk_size_mb": True})
        raise AssertionError("bool-disguised chunk_size_mb must raise")
    except ValueError as e:
        assert "chunk_size_mb" in str(e), str(e)


def test_valid_schema_but_missing_file_reaches_file_check():
    """合法绑定描述符穿过 header → 函数体文件校验 FileNotFoundError。"""
    try:
        _call(timing_files=[{"file_name": "/nonexistent/d.twf",
                             "block_cell": "blk"}], alpha=None)
        raise AssertionError("missing file must raise")
    except FileNotFoundError as e:
        assert "build_timing_db" in str(e) and "timing_files" in str(e), str(e)
        assert "/nonexistent/d.twf" in str(e), str(e)


if __name__ == "__main__":
    test_name_empty_rejected()
    test_timing_files_empty_list_rejected()
    test_timing_files_non_str_non_dict_element_rejected()
    test_timing_files_empty_str_element_rejected()
    test_binding_desc_missing_file_name_rejected()
    test_binding_desc_unknown_key_rejected()
    test_binding_desc_both_block_keys_rejected()
    test_binding_desc_missing_binding_target_rejected()
    test_binding_desc_strip_prefix_non_str_rejected()
    test_design_db_wrong_type_rejected()
    test_settings_unknown_key_rejected()
    test_alpha_unknown_key_rejected()
    test_alpha_chunk_size_mb_below_16_rejected()
    test_alpha_format_misspelled_rejected()
    test_alpha_bool_disguised_chunk_size_rejected()
    test_valid_schema_but_missing_file_reaches_file_check()
    print("[PASS] test_tm_db_validation")
