"""
Layer 0 smoke test: Config + serialization + export integration
Tests verify the C++ modules work correctly when imported from Python
"""

import sys
import os
import pytest

# Add bazel-bin output to path for extension module discovery
_bazel_bin = os.path.join(os.path.dirname(__file__), '..', 'bazel-bin', 'src', 'core', 'export')
if os.path.exists(_bazel_bin):
    sys.path.insert(0, _bazel_bin)


def test_config_singleton():
    """Verify Config is a singleton - same instance returned"""
    from _fly_core import get_config
    c1 = get_config()
    c2 = get_config()
    assert c1 is c2


def test_config_set_get_int():
    """Verify set_int and get_int work correctly"""
    from _fly_core import get_config
    config = get_config()
    config.reset()
    
    config.set_int("heartbeat_timeout", 60)
    assert config.get_int("heartbeat_timeout") == 60


def test_config_set_get_str():
    """Verify set_str and get_str work correctly"""
    from _fly_core import get_config
    config = get_config()
    config.reset()
    
    config.set_str("transport_type", "rdma")
    assert config.get_str("transport_type") == "rdma"


def test_config_defaults():
    """Verify default values are correct"""
    from _fly_core import get_config
    config = get_config()
    config.reset()
    
    assert config.get_int("master_port") == 8000
    assert config.get_int("heartbeat_timeout") == 120
    assert config.get_int("heartbeat_interval") == 5
    assert config.get_str("transport_type") == "tcp"


def test_config_immutable_after_launch():
    """Verify Config throws RuntimeError when set after workers launched"""
    from _fly_core import get_config
    config = get_config()
    config.reset()
    
    config.mark_workers_launched()
    
    with pytest.raises(RuntimeError):
        config.set_int("any_key", 1)
    
    with pytest.raises(RuntimeError):
        config.set_str("any_key", "value")


def test_config_is_workers_launched():
    """Verify is_workers_launched returns correct state"""
    from _fly_core import get_config
    config = get_config()
    config.reset()
    
    assert config.is_workers_launched() == False
    
    config.mark_workers_launched()
    assert config.is_workers_launched() == True


if __name__ == "__main__":
    pytest.main([__file__, "-v"])