"""build_lib_db 入口参数校验负例单测（2026-09-17 裁定：header Schema
直接校验、错误直接 raise 终止、白名单严格模式）。

校验触发点：@document 装饰器在调用时按 schema 校验（聚合 ValueError），
错误不进入函数体（不建库、不起任务）。文件参数经 ensure_readable_file
（不存在 FileNotFoundError / 不可读 PermissionError）+ liberty 头嗅探。

fake project：校验意外放行时 _create_db 抛 AssertionError 兜底（负例
不得创建真实 db）。
"""


class _FakeProject:
    def _create_db(self, *args, **kwargs):
        raise AssertionError("invalid input must not create a db")


def test_name_empty_rejected():
    from emir.lib import build_lib_db
    try:
        build_lib_db(_FakeProject(), name="", lib_paths=["a.lib"])
        raise AssertionError("empty name must raise")
    except ValueError as e:
        assert "name" in str(e) and "non-empty" in str(e), str(e)


def test_lib_paths_empty_list_rejected():
    from emir.lib import build_lib_db
    try:
        build_lib_db(_FakeProject(), name="lib", lib_paths=[])
        raise AssertionError("empty lib_paths must raise")
    except ValueError as e:
        assert "lib_paths" in str(e) and "min" in str(e), str(e)


def test_lib_paths_non_str_element_rejected():
    from emir.lib import build_lib_db
    try:
        build_lib_db(_FakeProject(), name="lib", lib_paths=[42])
        raise AssertionError("non-str lib path must raise")
    except ValueError as e:
        assert "lib_paths[0]" in str(e) and "expected str" in str(e), str(e)


def test_lib_paths_empty_str_element_rejected():
    from emir.lib import build_lib_db
    try:
        build_lib_db(_FakeProject(), name="lib", lib_paths=["ok.lib", ""])
        raise AssertionError("empty str lib path must raise")
    except ValueError as e:
        assert "lib_paths[1]" in str(e) and "non-empty" in str(e), str(e)


def test_alpha_unknown_key_rejected():
    from emir.lib import build_lib_db
    try:
        build_lib_db(_FakeProject(), name="lib", lib_paths=["a.lib"],
                     alpha={"nonexistent_key": 42})
        raise AssertionError("unknown alpha key must raise")
    except ValueError as e:
        assert "alpha" in str(e) and "nonexistent_key" in str(e), str(e)
        assert "no available keys" in str(e), str(e)


def test_alpha_non_dict_rejected():
    from emir.lib import build_lib_db
    try:
        build_lib_db(_FakeProject(), name="lib", lib_paths=["a.lib"],
                     alpha=[1, 2])
        raise AssertionError("non-dict alpha must raise")
    except ValueError as e:
        assert "alpha" in str(e) and "expected dict" in str(e), str(e)


def test_file_not_found():
    from emir.lib import build_lib_db
    try:
        build_lib_db(_FakeProject(), name="lib",
                     lib_paths=["/nonexistent/path/x.lib"])
        raise AssertionError("missing file must raise")
    except FileNotFoundError as e:
        assert "build_lib_db" in str(e) and "lib_paths" in str(e), str(e)
        assert "/nonexistent/path/x.lib" in str(e), str(e)


def test_valid_schema_but_missing_file_reaches_file_check():
    """schema 全过（name/alpha 合法）→ 进入函数体文件校验 FileNotFoundError
    （证明合法参数穿过 header、被 Step 1 文件检查拦截）。"""
    from emir.lib import build_lib_db
    try:
        build_lib_db(_FakeProject(), name="lib",
                     lib_paths=["/nonexistent/y.lib"], alpha=None)
        raise AssertionError("missing file must raise")
    except FileNotFoundError:
        pass


if __name__ == "__main__":
    test_name_empty_rejected()
    test_lib_paths_empty_list_rejected()
    test_lib_paths_non_str_element_rejected()
    test_lib_paths_empty_str_element_rejected()
    test_alpha_unknown_key_rejected()
    test_alpha_non_dict_rejected()
    test_file_not_found()
    test_valid_schema_but_missing_file_reaches_file_check()
    print("[PASS] test_lib_db_validation")
