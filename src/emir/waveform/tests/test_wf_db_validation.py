"""build_waveform_db 入口参数校验负例单测（2026-09-17 裁定范式：header
Schema 直接校验、错误直接 raise 终止、白名单严格模式——vcd_files 三形
态结构校验、settings 两键结构（activity 结构 + x_z_handling 枚举）、
alpha 单键值域、未知键一律报错；settings-only 模式 activity 必给）。"""


class _FakeProject:
    def _create_db(self, *args, **kwargs):
        raise AssertionError("invalid input must not create a db")


class _FakeDesignDb:
    DESIGN_OBJ = "DSDesign"


def _call(**overrides):
    from emir.waveform import build_waveform_db
    kwargs = dict(name="waveform", design_db=_FakeDesignDb(),
                  vcd_files=["d.vcd"])
    kwargs.update(overrides)
    return build_waveform_db(_FakeProject(), **kwargs)


ACTIVITY = {"default": {"toggle_rate": 2.0e6, "static_probability": 0.5}}


def test_name_empty_rejected():
    try:
        _call(name="")
        raise AssertionError("empty name must raise")
    except ValueError as e:
        assert "name" in str(e) and "non-empty" in str(e), str(e)


def test_vcd_files_empty_list_rejected():
    try:
        _call(vcd_files=[])
        raise AssertionError("empty vcd_files must raise")
    except ValueError as e:
        assert "vcd_files" in str(e) and "min" in str(e), str(e)


def test_vcd_files_non_str_non_dict_element_rejected():
    try:
        _call(vcd_files=[42])
        raise AssertionError("int element must raise")
    except ValueError as e:
        assert "vcd_files[0]" in str(e), str(e)


def test_vcd_files_empty_str_element_rejected():
    try:
        _call(vcd_files=["ok.vcd", ""])
        raise AssertionError("empty path string must raise")
    except ValueError as e:
        assert "vcd_files[1]" in str(e) and "non-empty" in str(e), str(e)


def test_binding_desc_missing_file_name_rejected():
    try:
        _call(vcd_files=[{"block_inst": "top/u1"}])
        raise AssertionError("missing file_name must raise")
    except ValueError as e:
        assert "file_name" in str(e), str(e)


def test_binding_desc_unknown_key_rejected():
    try:
        _call(vcd_files=[{"file_name": "d.vcd", "block_inst": "a",
                          "extra": 1}])
        raise AssertionError("unknown binding key must raise")
    except ValueError as e:
        assert "vcd_files[0]" in str(e) and "extra" in str(e), str(e)


def test_binding_desc_both_block_keys_rejected():
    try:
        _call(vcd_files=[{"file_name": "d.vcd", "block_inst": "a",
                          "block_cell": "b"}])
        raise AssertionError("block_inst + block_cell must raise")
    except ValueError as e:
        assert "vcd_files[0]" in str(e), str(e)
        assert "block_inst" in str(e) and "block_cell" in str(e), str(e)


def test_binding_desc_missing_binding_target_rejected():
    try:
        _call(vcd_files=[{"file_name": "d.vcd"}])
        raise AssertionError("file-only binding dict must raise")
    except ValueError as e:
        assert "vcd_files[0]" in str(e), str(e)


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
        assert "settings" in str(e) and "activity" in str(e), str(e)


def test_settings_activity_missing_default_rejected():
    """duration-only activity（无 default 无 overrides）→ schema 层拒
    ——default/overrides 至少其一（空壳 activity 无输入意义）。"""
    try:
        _call(settings={"activity": {"duration": 2.0}})
        raise AssertionError("activity without default/overrides must raise")
    except ValueError as e:
        assert "activity" in str(e) and "default" in str(e), str(e)


def test_settings_activity_overrides_only_passes_schema():
    """混合模式 schema 正例（裁定 IV-2）：VCD + overrides-only activity
    （无 default）穿过 header schema——到达文件存在性校验（FileNotFoundError
    = schema 已通过的证据，同 test_valid_schema_but_missing_file 先例）。"""
    try:
        _call(settings={"activity": {"overrides": {
            "u_cnt0/Q": {"toggle_rate": 1.0e6, "static_probability": 0.3}}}})
        raise AssertionError("missing file must raise (schema passed)")
    except FileNotFoundError:
        pass  # schema 通过 → 函数体文件校验拦截


def test_settings_activity_overrides_bad_value_rejected():
    """混合模式 schema 反例：overrides-only 但覆盖条目值非法 → 仍拒。"""
    try:
        _call(settings={"activity": {"overrides": {
            "u1/ZN": {"static_probability": 1.5}}}})
        raise AssertionError("bad override value must raise")
    except ValueError as e:
        assert "static_probability" in str(e), str(e)


def test_settings_activity_default_unknown_key_rejected():
    try:
        _call(settings={"activity": {"default": {"toggle_rate": 1.0,
                                                 "extra": 2}}})
        raise AssertionError("unknown default key must raise")
    except ValueError as e:
        assert "default" in str(e) and "extra" in str(e), str(e)


def test_settings_activity_override_unknown_key_rejected():
    try:
        _call(settings={"activity": {
            "default": {"toggle_rate": 1.0, "static_probability": 0.5},
            "overrides": {"u1/ZN": {"toggle_rate": 1.0, "bogus": 2}}}})
        raise AssertionError("unknown override key must raise")
    except ValueError as e:
        assert "overrides" in str(e) and "bogus" in str(e), str(e)


def test_settings_activity_override_bad_probability_rejected():
    try:
        _call(settings={"activity": {
            "default": {"toggle_rate": 1.0, "static_probability": 0.5},
            "overrides": {"u1/ZN": {"static_probability": 1.5}}}})
        raise AssertionError("probability > 1 must raise")
    except ValueError as e:
        assert "static_probability" in str(e), str(e)


def test_settings_x_z_handling_misspelled_rejected():
    try:
        _call(settings={"x_z_handling": "drop"})
        raise AssertionError("misspelled x_z_handling must raise")
    except ValueError as e:
        assert "x_z_handling" in str(e) and "denominator_include" in \
            str(e), str(e)


def test_settings_activity_bad_duration_rejected():
    try:
        _call(settings={"activity": dict(ACTIVITY, duration=0)})
        raise AssertionError("zero duration must raise")
    except ValueError as e:
        assert "duration" in str(e), str(e)


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


def test_alpha_bool_disguised_chunk_size_rejected():
    try:
        _call(alpha={"chunk_size_mb": True})
        raise AssertionError("bool-disguised chunk_size_mb must raise")
    except ValueError as e:
        assert "chunk_size_mb" in str(e), str(e)


def test_settings_only_without_activity_rejected():
    """settings-only 模式（vcd_files=None）无 settings.activity → 入口
    ValueError（Step ①——不建库、不起任务）。"""
    try:
        _call(vcd_files=None, settings=None)
        raise AssertionError("settings-only without activity must raise")
    except ValueError as e:
        assert "settings.activity" in str(e), str(e)
    try:
        _call(vcd_files=None, settings={"x_z_handling": "exclude"})
        raise AssertionError("settings-only without activity must raise")
    except ValueError as e:
        assert "settings.activity" in str(e), str(e)


def test_valid_schema_but_missing_file_reaches_file_check():
    """合法绑定描述符穿过 header → 函数体文件校验 FileNotFoundError。"""
    try:
        _call(vcd_files=[{"file_name": "/nonexistent/d.vcd",
                          "block_cell": "blk"}], alpha=None)
        raise AssertionError("missing file must raise")
    except FileNotFoundError as e:
        assert "build_waveform_db" in str(e) and "vcd_files" in str(e), \
            str(e)
        assert "/nonexistent/d.vcd" in str(e), str(e)


if __name__ == "__main__":
    test_name_empty_rejected()
    test_vcd_files_empty_list_rejected()
    test_vcd_files_non_str_non_dict_element_rejected()
    test_vcd_files_empty_str_element_rejected()
    test_binding_desc_missing_file_name_rejected()
    test_binding_desc_unknown_key_rejected()
    test_binding_desc_both_block_keys_rejected()
    test_binding_desc_missing_binding_target_rejected()
    test_design_db_wrong_type_rejected()
    test_settings_unknown_key_rejected()
    test_settings_activity_missing_default_rejected()
    test_settings_activity_overrides_only_passes_schema()
    test_settings_activity_overrides_bad_value_rejected()
    test_settings_activity_default_unknown_key_rejected()
    test_settings_activity_override_unknown_key_rejected()
    test_settings_activity_override_bad_probability_rejected()
    test_settings_x_z_handling_misspelled_rejected()
    test_settings_activity_bad_duration_rejected()
    test_alpha_unknown_key_rejected()
    test_alpha_chunk_size_mb_below_16_rejected()
    test_alpha_bool_disguised_chunk_size_rejected()
    test_settings_only_without_activity_rejected()
    test_valid_schema_but_missing_file_reaches_file_check()
    print("[PASS] test_wf_db_validation")
