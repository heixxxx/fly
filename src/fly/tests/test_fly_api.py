"""Unit tests for fly 表层 API 纯逻辑分支（__getattr__ / get_fly_binary）。

运行：./fly.sh test //src/fly/tests:fly_api_test

覆盖（2026-09 覆盖率批次 14 项之 2，单测部分）：
  - fly.<不存在符号> → AttributeError（模块级 __getattr__ 兜底分支）
  - get_fly_binary 解析序：sys._fly_binary 注入 > FLY_BUILD env > PATH > RuntimeError
  （completed_tasks/pending_tasks 等透传分支需活 agent，由 qa/api/test_fly_surface_api 覆盖）
"""
import os
import sys
import shutil
import stat
import tempfile

import fly
from fly import get_fly_binary


def _make_executable(path):
    with open(path, "w") as f:
        f.write("#!/bin/sh\nexit 0\n")
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return path


def test_module_getattr_unknown_raises():
    try:
        fly.no_such_symbol_anywhere
        raise AssertionError("unknown module attribute must raise AttributeError")
    except AttributeError as e:
        assert "no attribute" in str(e), str(e)
        assert "no_such_symbol_anywhere" in str(e), str(e)
    print("  PASS: test_module_getattr_unknown_raises")


def test_get_fly_binary_injected_wins():
    # 分支 1：sys._fly_binary 注入（C++ launcher 启发）——最高优先级。
    tmpdir = tempfile.mkdtemp()
    injected = _make_executable(os.path.join(tmpdir, "injected_fly"))
    old = getattr(sys, "_fly_binary", None)
    sys._fly_binary = injected
    try:
        assert get_fly_binary() == injected
    finally:
        if old is None:
            del sys._fly_binary
        else:
            sys._fly_binary = old
        shutil.rmtree(tmpdir, ignore_errors=True)
    print("  PASS: test_get_fly_binary_injected_wins")


def test_get_fly_binary_fly_build_env():
    # 分支 2：FLY_BUILD env → {FLY_BUILD}/bin/fly 可执行即命中。
    tmpdir = tempfile.mkdtemp()
    bindir = os.path.join(tmpdir, "bin")
    os.makedirs(bindir)
    candidate = _make_executable(os.path.join(bindir, "fly"))
    old_attr = getattr(sys, "_fly_binary", None)
    old_env = os.environ.get("FLY_BUILD")
    sys.modules.pop("fly", None)  # noqa: F841 — get_fly_binary 按 import 时绑定，无需清
    if old_attr is not None:
        del sys._fly_binary
    os.environ["FLY_BUILD"] = tmpdir
    try:
        assert get_fly_binary() == candidate, get_fly_binary()
    finally:
        if old_attr is not None:
            sys._fly_binary = old_attr
        if old_env is None:
            os.environ.pop("FLY_BUILD", None)
        else:
            os.environ["FLY_BUILD"] = old_env
        shutil.rmtree(tmpdir, ignore_errors=True)
    print("  PASS: test_get_fly_binary_fly_build_env")


def test_get_fly_binary_fly_build_not_executable_skipped():
    # FLY_BUILD 指向的 bin/fly 不可执行 → 该分支跳过（不命中）。
    tmpdir = tempfile.mkdtemp()
    bindir = os.path.join(tmpdir, "bin")
    os.makedirs(bindir)
    with open(os.path.join(bindir, "fly"), "w") as f:
        f.write("not executable")
    old_attr = getattr(sys, "_fly_binary", None)
    old_env = os.environ.get("FLY_BUILD")
    if old_attr is not None:
        del sys._fly_binary
    os.environ["FLY_BUILD"] = tmpdir
    real_which = shutil.which
    try:
        # PATH fallback 也 stub 掉，保证全 miss → RuntimeError（确定性）。
        shutil.which = lambda cmd: None
        try:
            get_fly_binary()
            raise AssertionError("all-miss resolution must raise RuntimeError")
        except RuntimeError as e:
            assert "Cannot find fly binary" in str(e), str(e)
    finally:
        shutil.which = real_which
        if old_attr is not None:
            sys._fly_binary = old_attr
        if old_env is None:
            os.environ.pop("FLY_BUILD", None)
        else:
            os.environ["FLY_BUILD"] = old_env
        shutil.rmtree(tmpdir, ignore_errors=True)
    print("  PASS: test_get_fly_binary_fly_build_not_executable_skipped")


def _run_all():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    failed = 0
    for t in tests:
        try:
            t()
        except Exception as e:
            failed += 1
            print(f"  FAIL: {t.__name__}: {e}")
            import traceback
            traceback.print_exc()
    print(f"fly_api: {len(tests) - failed}/{len(tests)} passed")
    if failed:
        sys.exit(1)


if __name__ == "__main__":
    _run_all()
