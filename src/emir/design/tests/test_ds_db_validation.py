"""build_design_db 入口参数校验负例单测（2026-09-17 裁定：header Schema
直接校验、错误直接 raise 终止、白名单严格模式——alpha 八键逐键类型/值域、
未知键一律报错，不再提醒忽略/回退默认继续）。"""


class _FakeProject:
    def _create_db(self, *args, **kwargs):
        raise AssertionError("invalid input must not create a db")


class _FakeLibDb:
    LIBRARY_OBJ = "LIBLibrary"


def _call(**overrides):
    from emir.design import build_design_db
    kwargs = dict(name="design", def_paths=["a.def"],
                  lef_paths=["tech.lef", "cells.lef"], lib_db=_FakeLibDb())
    kwargs.update(overrides)
    return build_design_db(_FakeProject(), **kwargs)


def test_name_empty_rejected():
    try:
        _call(name="")
        raise AssertionError("empty name must raise")
    except ValueError as e:
        assert "name" in str(e) and "non-empty" in str(e), str(e)


def test_def_paths_empty_str_rejected():
    try:
        _call(def_paths=["a.def", ""])
        raise AssertionError("empty str def path must raise")
    except ValueError as e:
        assert "def_paths[1]" in str(e) and "non-empty" in str(e), str(e)


def test_lef_paths_empty_list_rejected():
    try:
        _call(lef_paths=[])
        raise AssertionError("empty lef_paths must raise")
    except ValueError as e:
        assert "lef_paths" in str(e) and "min" in str(e), str(e)


def test_lef_paths_non_list_rejected():
    try:
        _call(lef_paths="tech.lef")
        raise AssertionError("non-list lef_paths must raise")
    except ValueError as e:
        assert "lef_paths" in str(e) and "expected list" in str(e), str(e)


def test_lib_db_wrong_type_rejected():
    try:
        _call(lib_db=object())
        raise AssertionError("non-LibDb lib_db must raise")
    except ValueError as e:
        assert "lib_db" in str(e) and "LibDb" in str(e), str(e)


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


def test_alpha_density_bin_size_below_one_rejected():
    try:
        _call(alpha={"density_bin_size": 0})
        raise AssertionError("density_bin_size < 1 must raise")
    except ValueError as e:
        assert "density_bin_size" in str(e) and ">= 1" in str(e), str(e)


def test_alpha_bool_disguised_int_rejected():
    try:
        _call(alpha={"net_batch_size": True})
        raise AssertionError("bool-disguised int must raise")
    except ValueError as e:
        assert "net_batch_size" in str(e), str(e)


def test_alpha_lcp_name_arena_non_bool_rejected():
    try:
        _call(alpha={"lcp_name_arena": 1})
        raise AssertionError("non-bool lcp_name_arena must raise")
    except ValueError as e:
        assert "lcp_name_arena" in str(e) and "bool" in str(e), str(e)


def test_alpha_target_partitions_non_str_rejected():
    try:
        _call(alpha={"target_partitions": 123})
        raise AssertionError("non-str/None target_partitions must raise")
    except ValueError as e:
        assert "target_partitions" in str(e), str(e)


def test_alpha_partition_count_non_int_rejected():
    try:
        _call(alpha={"partition_count": "4"})
        raise AssertionError("non-int partition_count must raise")
    except ValueError as e:
        assert "partition_count" in str(e) and "int" in str(e), str(e)


def test_alpha_channel_weight_negative_rejected():
    try:
        _call(alpha={"density_channel_weights": {"metal": -1}})
        raise AssertionError("negative channel weight must raise")
    except ValueError as e:
        assert "density_channel_weights.metal" in str(e), str(e)


def test_alpha_channel_weight_unknown_subkey_rejected():
    try:
        _call(alpha={"density_channel_weights": {"poly": 1}})
        raise AssertionError("unknown channel weight subkey must raise")
    except ValueError as e:
        assert "density_channel_weights" in str(e) and "poly" in str(e), str(e)


def test_alpha_def_aggregate_threshold_below_one_rejected():
    try:
        _call(alpha={"def_aggregate_threshold": 0})
        raise AssertionError("def_aggregate_threshold < 1 must raise")
    except ValueError as e:
        assert "def_aggregate_threshold" in str(e) and ">= 1" in str(e), str(e)


def test_alpha_valid_partial_dict_passes_schema():
    """合法 alpha（部分键 + 缺 key 用默认的子 dict）穿过 header，进入
    函数体文件校验 FileNotFoundError。"""
    try:
        _call(alpha={"density_bin_size": 5,
                     "density_channel_weights": {"metal": 3}})
        raise AssertionError("missing file must raise")
    except FileNotFoundError as e:
        assert "lef_paths" in str(e), str(e)


def test_file_not_found():
    try:
        _call(lef_paths=["/nonexistent/tech.lef", "/nonexistent/cells.lef"])
        raise AssertionError("missing lef file must raise")
    except FileNotFoundError as e:
        assert "build_design_db" in str(e) and "lef_paths" in str(e), str(e)
        assert "/nonexistent/tech.lef" in str(e), str(e)


if __name__ == "__main__":
    test_name_empty_rejected()
    test_def_paths_empty_str_rejected()
    test_lef_paths_empty_list_rejected()
    test_lef_paths_non_list_rejected()
    test_lib_db_wrong_type_rejected()
    test_settings_unknown_key_rejected()
    test_alpha_unknown_key_rejected()
    test_alpha_density_bin_size_below_one_rejected()
    test_alpha_bool_disguised_int_rejected()
    test_alpha_lcp_name_arena_non_bool_rejected()
    test_alpha_target_partitions_non_str_rejected()
    test_alpha_partition_count_non_int_rejected()
    test_alpha_channel_weight_negative_rejected()
    test_alpha_channel_weight_unknown_subkey_rejected()
    test_alpha_def_aggregate_threshold_below_one_rejected()
    test_alpha_valid_partial_dict_passes_schema()
    test_file_not_found()
    print("[PASS] test_ds_db_validation")
