"""Unit tests for fly.runtime module."""
import sys
import os
import shutil

_this_dir = os.path.dirname(os.path.abspath(__file__))
_project_root = os.path.normpath(os.path.join(_this_dir, '..', '..', '..'))
_bazel_bin = os.path.join(_project_root, 'bazel-bin', 'src')

for _subpath in ['core/export', 'log/export']:
    _full = os.path.join(_bazel_bin, _subpath)
    if os.path.exists(_full):
        sys.path.insert(0, _full)

import _fly_log as log
from _fly_core import ex_core_get_config


def setup_module():
    if os.path.exists("test_main_logs"):
        import shutil
        shutil.rmtree("test_main_logs", ignore_errors=True)
    log.init_log("test_main_logs", 0)


def teardown_module():
    log.shutdown_log()
    import shutil
    shutil.rmtree("test_main_logs", ignore_errors=True)


def test_config_singleton():
    config = ex_core_get_config()
    config2 = ex_core_get_config()
    assert config is config2, "Config should be singleton"


def test_config_defaults():
    config = ex_core_get_config()
    config.reset()
    assert config.get_int("heartbeat_timeout") == 120
    assert config.get_int("heartbeat_interval") == 5
    assert config.get_str("transport_type") == "tcp"


def test_config_set_get_int():
    config = ex_core_get_config()
    config.reset()
    config.set_int("test_key_int", 42)
    assert config.get_int("test_key_int") == 42


def test_config_set_get_str():
    config = ex_core_get_config()
    config.reset()
    config.set_str("test_key_str", "hello")
    assert config.get_str("test_key_str") == "hello"


def test_config_reset():
    config = ex_core_get_config()
    config.reset()
    config.set_int("heartbeat_timeout", 999)
    assert config.get_int("heartbeat_timeout") == 999
    config.reset()
    assert config.get_int("heartbeat_timeout") == 120


def test_log_level_functions_exist():
    assert callable(log.DBG)
    assert callable(log.INFO)
    assert callable(log.WARN)
    assert callable(log.ERR)


def test_init_shutdown_cycle():
    import shutil
    if os.path.exists("test_cycle_logs"):
        shutil.rmtree("test_cycle_logs", ignore_errors=True)
    log.shutdown_log()
    log.init_log("test_cycle_logs", 0)
    log.INFO("message after reinit")
    log.shutdown_log()
    shutil.rmtree("test_cycle_logs", ignore_errors=True)
    log.init_log("test_main_logs", 0)


if __name__ == "__main__":
    setup_module()

    test_config_singleton()
    test_config_defaults()
    test_config_set_get_int()
    test_config_set_get_str()
    test_config_reset()
    test_log_level_functions_exist()
    test_init_shutdown_cycle()

    teardown_module()
    print("\nAll main/runtime tests passed!")