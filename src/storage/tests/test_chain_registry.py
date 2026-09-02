"""Unit tests for chain_registry.py — uid↔db_path 双向映射注册表。

纯 Python 测试（不需要 fly runtime），stub _fly_log 后直接 import。
运行：./fly.sh test //src/storage/tests:chain_registry_test

覆盖（2026-09 覆盖率批次 14 项之 7）：
  - register/unregister 双向消失
  - resolve_uid / resolve_path
  - update_path 未注册 uid → WARN no-op
  - all_uids / clear
  - 同 uid 换 path（merge 场景）：旧 path 反向映射清除
  - 单例 get_registry 进程级复用
"""
import sys
import os
import threading

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'py'))

# stub _fly_log — 测试不依赖真实 C++ log
import types
_log_mod = types.ModuleType("_fly_log")
_log_mod.DBG = lambda *a, **kw: None
_log_mod.INFO = lambda *a, **kw: None
_log_mod.WARN = lambda *a, **kw: None
_log_mod.ERR = lambda *a, **kw: None
sys.modules["_fly_log"] = _log_mod

from chain_registry import DbChainRegistry, get_registry


def test_register_bidirectional():
    r = DbChainRegistry()
    r.register("uid_a", "/db/a")
    assert r.resolve_uid("uid_a") == "/db/a"
    assert r.resolve_path("/db/a") == "uid_a"
    print("  PASS: test_register_bidirectional")


def test_unregister_removes_both_directions():
    r = DbChainRegistry()
    r.register("uid_a", "/db/a")
    r.unregister("uid_a")
    assert r.resolve_uid("uid_a") is None, "uid 正向应消失"
    assert r.resolve_path("/db/a") is None, "path 反向应消失"
    # 注销不存在的 uid：静默 no-op
    r.unregister("uid_never")
    print("  PASS: test_unregister_removes_both_directions")


def test_same_uid_new_path_clears_old_reverse():
    # merge 场景：uid 不变，path 更新 → 旧 path 的反向映射必须清除，
    # 否则 resolve_path(旧path) 仍返回该 uid（悬空映射）。
    r = DbChainRegistry()
    r.register("uid_a", "/db/a")
    r.register("uid_a", "/db/a_merged")
    assert r.resolve_uid("uid_a") == "/db/a_merged"
    assert r.resolve_path("/db/a") is None, "旧 path 反向映射应清除"
    assert r.resolve_path("/db/a_merged") == "uid_a"
    print("  PASS: test_same_uid_new_path_clears_old_reverse")


def test_update_path_unregistered_warns_noop():
    r = DbChainRegistry()
    r.update_path("uid_missing", "/db/x")  # WARN + no-op，不抛
    assert r.resolve_uid("uid_missing") is None
    assert r.resolve_path("/db/x") is None
    print("  PASS: test_update_path_unregistered_warns_noop")


def test_update_path_same_path_keeps_reverse():
    # update_path 到相同 path：不得误清反向映射（幂等）。
    r = DbChainRegistry()
    r.register("uid_a", "/db/a")
    r.update_path("uid_a", "/db/a")
    assert r.resolve_uid("uid_a") == "/db/a"
    assert r.resolve_path("/db/a") == "uid_a"
    print("  PASS: test_update_path_same_path_keeps_reverse")


def test_all_uids_snapshot():
    r = DbChainRegistry()
    r.register("uid_a", "/db/a")
    r.register("uid_b", "/db/b")
    uids = r.all_uids()
    assert sorted(uids) == ["uid_a", "uid_b"]
    # 快照语义：外部修改返回列表不影响注册表
    uids.append("uid_fake")
    assert "uid_fake" not in r.all_uids()
    print("  PASS: test_all_uids_snapshot")


def test_clear():
    r = DbChainRegistry()
    r.register("uid_a", "/db/a")
    r.register("uid_b", "/db/b")
    r.clear()
    assert r.all_uids() == []
    assert r.resolve_path("/db/a") is None
    print("  PASS: test_clear")


def test_get_registry_singleton():
    r1 = get_registry()
    r2 = get_registry()
    assert r1 is r2, "get_registry 必须返回进程级单例"
    assert isinstance(r1, DbChainRegistry)
    print("  PASS: test_get_registry_singleton")


def test_concurrent_register_no_corruption():
    # 线程安全（RLock 保护）：并发 register/unregister 后双向映射一致。
    r = DbChainRegistry()
    def worker(prefix):
        for i in range(200):
            r.register(f"{prefix}_{i}", f"/db/{prefix}/{i}")
            r.unregister(f"{prefix}_{i}")
    threads = [threading.Thread(target=worker, args=(f"t{n}",)) for n in range(4)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    assert r.all_uids() == [], "并发 register/unregister 后应全清"
    print("  PASS: test_concurrent_register_no_corruption")


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
    print(f"chain_registry: {len(tests) - failed}/{len(tests)} passed")
    if failed:
        sys.exit(1)


if __name__ == "__main__":
    _run_all()
