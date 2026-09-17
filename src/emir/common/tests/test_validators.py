"""emir 建库 API 入口参数命名 validator 库单测（src/emir/common/validators.py，
2026-09-17 裁定：header Schema 结构化声明 + 白名单严格模式）。

覆盖：
  - 各 is_* 判定规则的通过/拒绝两侧（含 bool 伪装 int、NaN/inf、空串）
  - 工厂 validator（is_int_in_range / is_one_of）的边界
  - is_valid_binding_desc 互斥语义（恰其一）
  - ensure_readable_file：不存在 FileNotFoundError / 不可读
    PermissionError（root 下 chmod 000 仍可读——os.access 替换触发）、
    统一文案含 api 名/参数名/路径
"""
import os
import tempfile

from emir.common import (ensure_readable_file, is_bool,
                         is_int_in_range, is_nonempty_str,
                         is_nonempty_str_list, is_none_or_str,
                         is_nonneg_finite_number, is_one_of, is_plain_int,
                         is_positive_int, is_readable_file,
                         is_valid_binding_desc)


def test_is_nonempty_str():
    assert is_nonempty_str("a.lib")
    assert not is_nonempty_str("")
    assert not is_nonempty_str(None)
    assert not is_nonempty_str(42)


def test_is_bool():
    assert is_bool(True) and is_bool(False)
    assert not is_bool(1)          # int 伪装拒绝
    assert not is_bool("true")


def test_is_plain_int():
    assert is_plain_int(0) and is_plain_int(-3) and is_plain_int(7)
    assert not is_plain_int(True)  # bool 伪装拒绝
    assert not is_plain_int(1.0)


def test_is_positive_int():
    assert is_positive_int(1) and is_positive_int(1000)
    assert not is_positive_int(0)
    assert not is_positive_int(True)
    assert not is_positive_int(-5)


def test_is_int_in_range():
    check = is_int_in_range(minimum=16)
    assert check(16) and check(1024)
    assert not check(15)
    assert not check(True)
    both = is_int_in_range(minimum=1, maximum=10)
    assert both(1) and both(10)
    assert not both(0) and not both(11)


def test_is_one_of():
    check = is_one_of("auto", "innovus")
    assert check("auto") and check("innovus")
    assert not check("Auto")       # 大小写敏感
    assert not check("redhawk")
    assert not check(None)


def test_is_none_or_str():
    assert is_none_or_str(None)
    assert is_none_or_str("")      # 格式判别在消费侧——空串是 str
    assert is_none_or_str("4x3")
    assert not is_none_or_str(4)
    assert not is_none_or_str(("4", "3"))


def test_is_nonneg_finite_number():
    assert is_nonneg_finite_number(0) and is_nonneg_finite_number(2.5)
    assert is_nonneg_finite_number(6)   # int 通道比重合法
    assert not is_nonneg_finite_number(-0.1)
    assert not is_nonneg_finite_number(True)
    assert not is_nonneg_finite_number(float("nan"))
    assert not is_nonneg_finite_number(float("inf"))
    assert not is_nonneg_finite_number("2")


def test_is_nonempty_str_list():
    assert is_nonempty_str_list([])          # 长度约束归 Schema.list
    assert is_nonempty_str_list(["a", "b"])
    assert not is_nonempty_str_list(["a", ""])
    assert not is_nonempty_str_list(["a", 1])
    assert not is_nonempty_str_list("a")     # 非 list


def test_is_valid_binding_desc():
    assert is_valid_binding_desc({"file_name": "d.twf", "block_inst": "top/u1"})
    assert is_valid_binding_desc({"file_name": "d.twf", "block_cell": "blk"})
    # 同传（歧义）与都缺（该用 str 元素）均拒绝
    assert not is_valid_binding_desc(
        {"file_name": "d.twf", "block_inst": "a", "block_cell": "b"})
    assert not is_valid_binding_desc({"file_name": "d.twf"})
    # strip_prefix 附加不影响互斥判定
    assert is_valid_binding_desc(
        {"file_name": "d.twf", "block_cell": "blk", "strip_prefix": "x/"})


def test_ensure_readable_file_not_found():
    try:
        ensure_readable_file("/nonexistent/x.lib", "build_lib_db", "lib_paths")
        raise AssertionError("missing file must raise")
    except FileNotFoundError as e:
        msg = str(e)
        assert "build_lib_db" in msg and "lib_paths" in msg
        assert "/nonexistent/x.lib" in msg


def test_ensure_readable_file_and_is_readable():
    with tempfile.NamedTemporaryFile(suffix=".lib", delete=False) as fh:
        path = fh.name
    try:
        assert is_readable_file(path)
        ensure_readable_file(path, "build_lib_db", "lib_paths")  # 不抛
        ensure_readable_file(path + ".gone", "api", "p")  # 缺失分支已覆盖
    finally:
        os.unlink(path)


def test_ensure_readable_file_permission_error(monkeypatch=None):
    """不可读分支：chmod 000 在 root（CAP_DAC_OVERRIDE）下仍可读——
    替换 os.access 模拟不可读（文件真实存在，isfile 通过），断言
    PermissionError 与文案要点。"""
    with tempfile.NamedTemporaryFile(suffix=".lib", delete=False) as fh:
        path = fh.name
    real_access = os.access
    try:
        if monkeypatch is not None:
            monkeypatch.setattr(os, "access", lambda p, m: False)
        else:
            os.access = lambda p, m: False
        try:
            ensure_readable_file(path, "build_design_db", "lef_paths")
            raise AssertionError("unreadable file must raise")
        except PermissionError as e:
            msg = str(e)
            assert "build_design_db" in msg and "lef_paths" in msg
            assert path in msg
    finally:
        os.access = real_access
        os.unlink(path)
