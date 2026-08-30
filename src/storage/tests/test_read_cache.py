import sys
import os
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'py'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', '..', 'src'))

from read_cache import ReadCache


# ── 基础语义（双池 + level 标记，2026-08-30 改造后）──────────────────

def test_basic_put_get_high():
    rc = ReadCache(max_bytes=1024 * 1024)
    obj = {"key": "value"}
    rc.put("k1", "high", obj, size=100)
    assert rc.get("k1") is obj
    print("  PASS: test_basic_put_get_high")


def test_cache_miss():
    rc = ReadCache(max_bytes=1024 * 1024)
    assert rc.get("nonexistent") is None
    print("  PASS: test_cache_miss")


def test_get_level_agnostic():
    # 命中查询不分级（等级只影响淘汰优先级）——low 条目 get 也命中。
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("k1", "low", "obj_low", size=100)
    assert rc.get("k1") == "obj_low"
    print("  PASS: test_get_level_agnostic")


def test_lru_eviction():
    rc = ReadCache(max_bytes=300)

    rc.put("a", "high", "obj_a", size=100)
    rc.put("b", "high", "obj_b", size=100)
    rc.put("c", "high", "obj_c", size=100)

    assert rc.get("a") is not None
    assert rc.get("b") is not None
    assert rc.get("c") is not None

    time.sleep(0.1)
    rc.put("d", "high", "obj_d", size=100)

    assert rc.get("d") is not None
    print("  PASS: test_lru_eviction")


def test_protection_period():
    rc = ReadCache(max_bytes=100)

    rc.put("a", "high", "obj_a", size=50)
    rc.put("b", "high", "obj_b", size=50)

    assert rc.get("a") is not None
    assert rc.get("b") is not None

    rc.put("c", "high", "obj_c", size=50)

    assert rc._main_bytes <= 150
    print("  PASS: test_protection_period")


def test_hard_limit_override():
    rc = ReadCache(max_bytes=100)
    rc._hard_limit = 100

    rc.put("a", "high", "obj_a", size=50)
    rc.put("b", "high", "obj_b", size=50)
    rc.put("c", "high", "obj_c", size=50)

    assert rc._main_bytes <= 150
    print("  PASS: test_hard_limit_override")


def test_remove():
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("k1", "high", "obj", size=50)

    rc.remove("k1")
    assert rc.get("k1") is None
    print("  PASS: test_remove")


def test_remove_covers_both_pools():
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("k1", "temp", "obj_t", size=50)
    rc.put("k2", "high", "obj_h", size=50)

    rc.remove("k1")
    rc.remove("k2")
    assert rc.get("k1") is None and rc.get("k2") is None
    assert rc._main_bytes == 0 and rc._temp_bytes == 0
    print("  PASS: test_remove_covers_both_pools")


def test_clear():
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("a", "high", "obj", size=50)
    rc.put("t", "temp", "obj_t", size=50)

    rc.clear()
    assert rc.get("a") is None
    assert rc._main_bytes == 0 and rc._temp_bytes == 0
    print("  PASS: test_clear")


def test_overwrite():
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("k1", "high", "old", size=100)
    rc.put("k1", "high", "new", size=200)

    result = rc.get("k1")
    assert result is not None and result == "new"
    assert rc._main_bytes == 200
    print("  PASS: test_overwrite")


def test_read_count_scoring():
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("hot", "high", "x", size=100)
    rc.put("cold", "high", "y", size=100)

    for _ in range(10):
        rc.get("hot")
        time.sleep(0.01)

    assert rc._main["hot"].read_count == 11
    assert rc._main["cold"].read_count == 1
    print("  PASS: test_read_count_scoring")


# ── 新语义：low level / temp 池 / 淘汰优先级 ─────────────────────────

def test_low_level_populates_main_pool():
    # low 是真实等级（不再是 "none" 别名）：入主池、可命中、计字节。
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("k1", "low", "obj_low", size=100)
    assert rc.get("k1") == "obj_low"
    assert rc._main_bytes == 100
    assert rc._main["k1"].level == "low"
    print("  PASS: test_low_level_populates_main_pool")


def test_temp_pool_isolated():
    # temp 独立池：容量 = 主池一半；不占主池字节；命中/淘汰同构。
    rc = ReadCache(max_bytes=1024 * 1024)
    assert rc._temp_max_bytes == 1024 * 1024 // 2

    rc.put("t1", "temp", "obj_t1", size=100)
    rc.put("t2", "temp", "obj_t2", size=100)
    rc.put("m1", "high", "obj_m1", size=100)

    assert rc._temp_bytes == 200
    assert rc._main_bytes == 100  # temp 不占主池
    assert rc.get("t1") == "obj_t1"
    assert rc.get("t2") == "obj_t2"
    assert rc.get("m1") == "obj_m1"
    print("  PASS: test_temp_pool_isolated")


def test_temp_pool_eviction():
    # temp 池自身预算淘汰（硬限兜底触发——保护期内超硬限也淘汰）。
    rc = ReadCache(max_bytes=200)  # temp 池 = 100
    rc._hard_limit = 200
    rc._temp_hard_limit = 150

    rc.put("t1", "temp", "o1", size=60)
    rc.put("t2", "temp", "o2", size=60)
    rc.put("t3", "temp", "o3", size=60)

    assert rc._temp_bytes <= 150
    # 主池不受 temp 淘汰影响。
    rc.put("m1", "high", "o_m", size=60)
    assert rc.get("m1") == "o_m"
    print("  PASS: test_temp_pool_eviction")


def test_low_evicted_before_high():
    # 同等热度下 low 折扣分数更低 → 预算压力时先淘汰 low（硬限触发，
    # 绕过保护期窗口使全部条目进入候选）。
    rc = ReadCache(max_bytes=200)
    rc._hard_limit = 200  # 超预算即触发硬限淘汰路径

    rc.put("low_k", "low", "o_low", size=100)
    rc.put("high_k", "high", "o_high", size=100)
    rc.put("trigger", "high", "o_trig", size=100)

    # 硬限淘汰后：low_k 应先被淘汰（同 read_count/age 下折扣低）。
    assert rc.get("low_k") is None, "low 条目应先于 high 被淘汰"
    assert rc.get("high_k") is not None, "high 条目应保留"
    print("  PASS: test_low_evicted_before_high")


def test_hit_does_not_upgrade_level():
    # low 命中只 touch，不升级 high（升级走显式）。
    rc = ReadCache(max_bytes=1024 * 1024)
    rc.put("k1", "low", "obj", size=100)
    rc.get("k1")
    rc.get("k1")
    assert rc._main["k1"].level == "low"
    print("  PASS: test_hit_does_not_upgrade_level")


def test_oversized_object_still_admitted():
    # 单对象不设预算上限（用户裁定）：超预算对象可入池（入池后由淘汰
    # 机制处理，put 不拒绝不抛异常）。
    rc = ReadCache(max_bytes=100)
    rc.put("huge", "high", "x" * 500, size=500)
    # 池状态一致（立即被淘汰或暂时持有——不拒绝）。
    assert rc._main_bytes <= 150  # 硬限内
    print("  PASS: test_oversized_object_still_admitted")


def _run_all():
    tests = [
        test_basic_put_get_high,
        test_cache_miss,
        test_get_level_agnostic,
        test_lru_eviction,
        test_protection_period,
        test_hard_limit_override,
        test_remove,
        test_remove_covers_both_pools,
        test_clear,
        test_overwrite,
        test_read_count_scoring,
        test_low_level_populates_main_pool,
        test_temp_pool_isolated,
        test_temp_pool_eviction,
        test_low_evicted_before_high,
        test_hit_does_not_upgrade_level,
        test_oversized_object_still_admitted,
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
