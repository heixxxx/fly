import sys
import os
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'py'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', '..', 'src'))

from read_cache import ReadCache


def test_basic_put_get_high():
    rc = ReadCache(max_bytes=1024 * 1024)
    obj = {"key": "value"}
    rc.put("k1", "high", obj, size=100)
    result = rc.get("k1", "high")
    assert result is obj
    print("  PASS: test_basic_put_get_high")


def test_cache_miss():
    rc = ReadCache(max_bytes=1024 * 1024)
    assert rc.get("nonexistent", "high") is None
    print("  PASS: test_cache_miss")


def test_lru_eviction():
    rc = ReadCache(max_bytes=300)

    rc.put("a", "high", "obj_a", size=100)
    rc.put("b", "high", "obj_b", size=100)
    rc.put("c", "high", "obj_c", size=100)

    assert rc.get("a", "high") is not None
    assert rc.get("b", "high") is not None
    assert rc.get("c", "high") is not None

    time.sleep(0.1)
    rc.put("d", "high", "obj_d", size=100)

    assert rc.get("d", "high") is not None
    print("  PASS: test_lru_eviction")


def test_protection_period():
    rc = ReadCache(max_bytes=100)

    rc.put("a", "high", "obj_a", size=50)
    rc.put("b", "high", "obj_b", size=50)

    assert rc.get("a", "high") is not None
    assert rc.get("b", "high") is not None

    rc.put("c", "high", "obj_c", size=50)

    assert rc._high_bytes <= 150
    print("  PASS: test_protection_period")


def test_hard_limit_override():
    rc = ReadCache(max_bytes=100)
    rc._hard_limit = 100

    rc.put("a", "high", "obj_a", size=50)
    rc.put("b", "high", "obj_b", size=50)
    rc.put("c", "high", "obj_c", size=50)

    assert rc._high_bytes <= 150
    print("  PASS: test_hard_limit_override")


def test_remove():
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("k1", "high", "obj", size=50)

    rc.remove("k1", "high")
    assert rc.get("k1", "high") is None

    rc.remove("k1")
    assert rc.get("k1", "high") is None
    print("  PASS: test_remove")


def test_clear():
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("a", "high", "obj", size=50)

    rc.clear()
    assert rc.get("a", "high") is None
    assert rc._high_bytes == 0
    print("  PASS: test_clear")


def test_overwrite():
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("k1", "high", "old", size=100)
    rc.put("k1", "high", "new", size=200)

    result = rc.get("k1", "high")
    assert result is not None and result == "new"
    assert rc._high_bytes == 200
    print("  PASS: test_overwrite")


def test_read_count_scoring():
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("hot", "high", "x", size=100)
    rc.put("cold", "high", "y", size=100)

    for _ in range(10):
        rc.get("hot", "high")
        time.sleep(0.01)

    assert rc._high["hot"].read_count == 11
    assert rc._high["cold"].read_count == 1
    print("  PASS: test_read_count_scoring")


def test_low_level_ignored():
    # Low tier is handled by C++ ObjectCache; Python ReadCache ignores it.
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("k1", "low", (b'data', ""), size=100)
    assert rc.get("k1", "low") is None
    assert rc._high_bytes == 0
    print("  PASS: test_low_level_ignored")


def _run_all():
    tests = [
        test_basic_put_get_high,
        test_cache_miss,
        test_lru_eviction,
        test_protection_period,
        test_hard_limit_override,
        test_remove,
        test_clear,
        test_overwrite,
        test_read_count_scoring,
        test_low_level_ignored,
    ]

    passed = 0
    failed = 0
    for test in tests:
        try:
            test()
            passed += 1
        except Exception as e:
            failed += 1
            print(f"  FAIL: {test.__name__}: {e}")
            import traceback
            traceback.print_exc()

    print(f"\n{'='*60}")
    print(f"Results: {passed} passed, {failed} failed, {passed + failed} total")
    if failed:
        print("FAILED")
        sys.exit(1)
    print("ALL PASSED")


if __name__ == "__main__":
    _run_all()
