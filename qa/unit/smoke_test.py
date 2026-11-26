"""
Layer 0 smoke test: Config 绑定与导出集成（ex_core_get_config）
验证 C++ 扩展模块能从 Python 导入，Config 的读写、缺省值与「启动后锁定」行为正确

在自动化里由 qa/unit/test_unit_pytest_modules.pyt 调度：每个模块各起一个独立的
pytest 进程执行，逐个测试函数的结果由运行器与基准文件比对。本模块必须在裸进程里
运行——测试会调用 config.reset()，不能在已经启动的 fly 进程里执行。

2026-11-26 救活时的修正（本模块从入库起就没有被执行过，见 docs/DOC_CHANGELOG.md）：
  - 引导路径少算了一级目录（qa/unit/../bazel-bin 指向不存在的 qa/bazel-bin），
    扩展模块一直找不到；
  - 几处期望与实际行为不符，按实际行为改写：Config 句柄的获取函数现在叫
    ex_core_get_config；worker 启动后被拒绝的写入是「返回 False、值不变」，不抛异常
    （src/core/cpp/config.h 写明的契约，提交 c90b156d 起如此）；master_port 的
    缺省值是 0（占位，定稿端口写在 .fly_config 里）。
"""

import sys
import os
import pytest

# Add bazel-bin output to path for extension module discovery
# 仓库根的 bazel-bin（qa/unit → qa → 仓库根）
_bazel_bin = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
                         'bazel-bin', 'src', 'core', 'export')
if os.path.exists(_bazel_bin):
    sys.path.insert(0, _bazel_bin)


def _config():
    from _fly_core import ex_core_get_config
    return ex_core_get_config()


def test_config_singleton():
    """Verify Config is a singleton - same instance returned"""
    c1 = _config()
    c2 = _config()
    assert c1 is c2


def test_config_set_get_int():
    """Verify set_int and get_int work correctly"""
    config = _config()
    config.reset()

    assert config.set_int("heartbeat_timeout", 60) is True
    assert config.get_int("heartbeat_timeout") == 60
    config.reset()


def test_config_set_get_str():
    """Verify set_str and get_str work correctly"""
    config = _config()
    config.reset()

    assert config.set_str("transport_type", "rdma") is True
    assert config.get_str("transport_type") == "rdma"
    config.reset()


def test_config_defaults():
    """Verify default values are correct"""
    config = _config()
    config.reset()

    # 0 是占位：master 启动时选定端口，定稿值写在 .fly_config 里
    assert config.get_int("master_port") == 0
    assert config.get_int("heartbeat_timeout") == 120
    assert config.get_int("heartbeat_interval") == 5
    assert config.get_str("transport_type") == "tcp"
    config.reset()


def test_config_immutable_after_launch():
    """Verify Config rejects writes after workers launched (returns False, value unchanged)"""
    config = _config()
    config.reset()
    try:
        config.set_int("heartbeat_timeout", 120)
        config.set_str("transport_type", "tcp")

        config.mark_workers_launched()

        assert config.set_int("heartbeat_timeout", 1) is False
        assert config.set_str("transport_type", "rdma") is False
        assert config.get_int("heartbeat_timeout") == 120
        assert config.get_str("transport_type") == "tcp"
    finally:
        config.reset()


def test_config_is_workers_launched():
    """Verify is_workers_launched returns correct state"""
    config = _config()
    config.reset()
    try:
        assert config.is_workers_launched() is False

        config.mark_workers_launched()
        assert config.is_workers_launched() is True
    finally:
        config.reset()


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
