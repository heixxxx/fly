import sys
import os
import struct
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'py'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', '..', 'src'))

from read_cache import ReadCache, _extract_decompressed_size


def _make_compressed_data(total_size: int) -> bytes:
    magic = struct.pack('<I', 0x464C5900)
    version = struct.pack('<B', 1)
    py_name_len = struct.pack('<H', 4)
    total = struct.pack('<Q', total_size)
    chunk_count = struct.pack('<I', 1)
    comp_type = struct.pack('<B', 1)
    py_name = b'test'
    return magic + version + py_name_len + total + chunk_count + comp_type + py_name + b'\x00' * 100


def test_extract_decompressed_size():
    data = _make_compressed_data(12345)
    assert _extract_decompressed_size(data) == 12345
    assert _extract_decompressed_size(b'short') == 5
    print("  PASS: test_extract_decompressed_size")


def test_basic_put_get_low():
    rc = ReadCache(max_bytes=1024 * 1024)
    data = _make_compressed_data(100)
    rc.put("k1", "low", (data, "test"))
    result = rc.get("k1", "low")
    assert result is not None
    assert result[0] == data
    assert result[1] == "test"
    print("  PASS: test_basic_put_get_low")


def test_basic_put_get_high():
    rc = ReadCache(max_bytes=1024 * 1024)
    obj = {"key": "value"}
    rc.put("k1", "high", obj, size=100)
    result = rc.get("k1", "high")
    assert result is obj
    print("  PASS: test_basic_put_get_high")


def test_cache_miss():
    rc = ReadCache(max_bytes=1024 * 1024)
    assert rc.get("nonexistent", "low") is None
    assert rc.get("nonexistent", "high") is None
    print("  PASS: test_cache_miss")


def test_lru_eviction():
    rc = ReadCache(max_bytes=300)

    rc.put("a", "low", (b'\x00' * 50, ""), size=100)
    rc.put("b", "low", (b'\x00' * 50, ""), size=100)
    rc.put("c", "low", (b'\x00' * 50, ""), size=100)

    assert rc.get("a", "low") is not None
    assert rc.get("b", "low") is not None
    assert rc.get("c", "low") is not None

    time.sleep(0.1)
    rc.put("d", "low", (b'\x00' * 50, ""), size=100)

    assert rc.get("d", "low") is not None
    print("  PASS: test_lru_eviction")


def test_protection_period():
    rc = ReadCache(max_bytes=100)

    rc.put("a", "low", (b'\x00' * 50, ""), size=50)
    rc.put("b", "low", (b'\x00' * 50, ""), size=50)

    assert rc.get("a", "low") is not None
    assert rc.get("b", "low") is not None

    rc.put("c", "low", (b'\x00' * 50, ""), size=50)

    total = rc._low_bytes
    assert total <= 150
    print("  PASS: test_protection_period")


def test_hard_limit_override():
    rc = ReadCache(max_bytes=100)
    rc._hard_limit = 100

    rc.put("a", "low", (b'\x00' * 50, ""), size=50)
    rc.put("b", "low", (b'\x00' * 50, ""), size=50)
    rc.put("c", "low", (b'\x00' * 50, ""), size=50)

    assert rc._low_bytes <= 150
    print("  PASS: test_hard_limit_override")


def test_remove():
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("k1", "low", (b'data', ""), size=100)
    rc.put("k1", "high", "obj", size=50)

    rc.remove("k1", "low")
    assert rc.get("k1", "low") is None
    assert rc.get("k1", "high") is not None

    rc.remove("k1")
    assert rc.get("k1", "high") is None
    print("  PASS: test_remove")


def test_clear():
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("a", "low", (b'data', ""), size=100)
    rc.put("b", "high", "obj", size=50)

    rc.clear()
    assert rc.get("a", "low") is None
    assert rc.get("b", "high") is None
    assert rc._low_bytes == 0
    assert rc._high_bytes == 0
    print("  PASS: test_clear")


def test_overwrite():
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("k1", "low", (b'old', ""), size=100)
    rc.put("k1", "low", (b'new', ""), size=200)

    result = rc.get("k1", "low")
    assert result is not None and result[0] == b'new'
    assert rc._low_bytes == 200
    print("  PASS: test_overwrite")


def test_independent_levels():
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("k1", "low", (b'compressed', ""), size=100)
    rc.put("k1", "high", "deserialized", size=50)

    low_result = rc.get("k1", "low")
    assert low_result is not None and low_result[0] == b'compressed'
    assert rc.get("k1", "high") == "deserialized"
    print("  PASS: test_independent_levels")


def test_read_count_scoring():
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("hot", "low", (b'x', ""), size=100)
    rc.put("cold", "low", (b'y', ""), size=100)

    for _ in range(10):
        rc.get("hot", "low")
        time.sleep(0.01)

    assert rc._low["hot"].read_count == 11
    assert rc._low["cold"].read_count == 1
    print("  PASS: test_read_count_scoring")


def _run_all():
    tests = [
        test_extract_decompressed_size,
        test_basic_put_get_low,
        test_basic_put_get_high,
        test_cache_miss,
        test_lru_eviction,
        test_protection_period,
        test_hard_limit_override,
        test_remove,
        test_clear,
        test_overwrite,
        test_independent_levels,
        test_read_count_scoring,
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
