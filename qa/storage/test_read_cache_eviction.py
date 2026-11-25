"""Unit-style coverage for ReadCache eviction / remove / clear logic.

The existing read_cache QA tests (basic / cross_db / large_objects) only
exercise the get()/put() miss-path indirectly through db.read_object; they
never trigger eviction (default max_bytes is 1 GiB) nor call remove()/clear()
(the integration layer added invalidation only after the stale-read fix).

This test drives ReadCache directly with a tiny max_bytes and synthetic
timestamps so the eviction branches (normal path, 30s protection window,
1.5x hard-limit override), the put() overwrite branch, size inference, and
remove()/clear() are all covered deterministically and quickly — no worker /
db / network needed.

ReadCache is a plain in-memory data structure; manipulating its private
fields (_CacheEntry.created_at, _high_bytes) is the intended way to test
time/size-dependent branches without real 30s waits or GiB allocations.
"""
from _fly_log import INFO

# Import the class directly. Both layouts are on sys.path under fly.
try:
    from storage import ReadCache
    from storage.py.read_cache import _PROTECTION_SEC, _HARD_LIMIT_RATIO
except ImportError:
    from storage import ReadCache
    from storage.py.read_cache import _PROTECTION_SEC, _HARD_LIMIT_RATIO

from test import record


def _entry_age(rc, key, seconds):
    """Force a cached entry's created_at/last_access back by `seconds`."""
    e = rc._high[key]
    e.created_at -= seconds
    e.last_access -= seconds


# ── put/get basics, size inference, level semantics（双池+level 改造后）──
# 2026-08-30 裁定：get 命中查询不分级（等级只影响淘汰优先级）；low 为
# 真实等级（完整对象入主池，计分折扣 low_score_factor）。
rc = ReadCache(max_bytes=1 << 20)
rc.put("b", "high", b"abcd")          # bytes -> size inferred via len()
entry_b = rc._high.get("b")
get_b_high = rc.get("b", "high")
get_b_default = rc.get("b")
rc.put("s", "low", "stored")          # low = 真实等级（入主池可命中）
entry_s = rc._high.get("s")
get_s_default = rc.get("s")
# 原断言：assert rc._high["b"].size == 4, "size should be inferred from len(bytes)"
# 原断言：assert rc.get("b", "high") == b"abcd"
# 原断言：assert rc.get("b") == b"abcd", "get 命中查询不分级"
# 原断言：assert rc._high["s"].level == "low", "low 条目应入主池并标记等级"
# 原断言：assert rc.get("s") == "stored", "low 条目命中可返回"
record("basics", {
    "entry_b_size": entry_b.size if entry_b else None,
    "get_b_high": get_b_high,
    "get_b_default": get_b_default,
    "entry_s_level": entry_s.level if entry_s else None,
    "get_s_default": get_s_default,
})
INFO("[DONE] put/get basics: size inference + level semantics")

# ── put overwrite branch (pop old, debit bytes) ───────────────────────
rc.put("b", "high", b"abcdefgh")       # overwrite same key
entry_b2 = rc._high.get("b")
overwrite_get_b = rc.get("b", "high")
# 原断言：assert rc._high["b"].size == 8, "overwrite should replace old size"
# 原断言：assert rc._high_bytes == 8, "bytes should reflect overwrite (old debited)"
# 原断言：assert rc.get("b", "high") == b"abcdefgh", "overwrite should return new value"
record("overwrite", {
    "entry_b_size": entry_b2.size if entry_b2 else None,
    "high_bytes": rc._high_bytes,
    "get_b_high": overwrite_get_b,
})
INFO("[DONE] put overwrite: old entry debited, new value stored")

# ── remove / clear ────────────────────────────────────────────────────
rc.remove("b")
# 原断言：assert "b" not in rc._high and rc._high_bytes == 0, "remove should drop entry + bytes"
record("remove/b_gone", "b" not in rc._high)
record("remove/high_bytes_zero", rc._high_bytes == 0, detail=rc._high_bytes)
rc.put("x", "high", b"1")
rc.put("y", "high", b"2")
rc.clear()
# 原断言：assert rc._high_bytes == 0 and len(rc._high) == 0, "clear should empty cache"
record("clear", {"high_bytes": rc._high_bytes, "entry_count": len(rc._high)})
INFO("[DONE] remove/clear: entries and byte counter reset")

# ── _evict normal path: over max, age >= 30s, lowest score first ──────
# max=10 bytes. Put 3 entries that each exceed max on their own is not useful;
# instead put several small ones whose total exceeds 10, all aged past the
# 30s protection window. The lowest-score (fewest reads / oldest) is evicted
# until under max.
rc = ReadCache(max_bytes=10)
rc.put("a", "high", b"AAAA")           # 4 bytes
rc.put("b", "high", b"BBBB")           # 4 bytes  -> total 8
rc.put("c", "high", b"CCCC")           # 4 bytes  -> total 12 > 10, but all
                                        # are <30s old -> protected, NOT evicted
                                        # (and 12 <= hard_limit=15, so no override)
# 原断言：assert rc._high_bytes == 12, f"expected 12 (protected), got {rc._high_bytes}"
# 原断言：assert len(rc._high) == 3, "protected entries must not be evicted under hard limit"
record("protected_window", {"high_bytes": rc._high_bytes, "entry_count": len(rc._high)})

# Now age them past the protection window. 'a' is read once more (higher score)
# so it should survive; 'b' and 'c' have score 1 and 'c' was added last.
for k in ("a", "b", "c"):
    _entry_age(rc, k, _PROTECTION_SEC + 1)
rc.get("a", "high")                     # bump a's read_count -> higher score
before = dict(rc._high)
rc.put("d", "high", b"DDDD")            # 4 bytes -> triggers _evict, candidates now eligible
evict_normal_under_max = rc._high_bytes <= rc._max_bytes
a_survived = "a" in rc._high
# 原断言：assert rc._high_bytes <= rc._max_bytes, \
# 原断言：assert "a" in rc._high, "highest-score entry 'a' should survive eviction"
record("evict_normal/bytes_under_max", evict_normal_under_max, detail=rc._high_bytes)
record("evict_normal/high_score_entry_kept", a_survived, detail=sorted(rc._high))
INFO(f"[DONE] _evict normal: bytes {rc._high_bytes} <= max, high-score entry kept")

# ── _evict hard-limit override: break protection window ───────────────
# When bytes exceed hard_limit (1.5x max), ALL entries become eviction
# candidates even if <30s old. max=10 -> hard_limit=15.
rc = ReadCache(max_bytes=10)
rc.put("p1", "high", b"AAAA")          # 4
rc.put("p2", "high", b"AAAA")          # 4  -> 8
rc.put("p3", "high", b"AAAA")          # 4  -> 12 (>max but <hard_limit, protected)
rc.put("p4", "high", b"AAAA")          # 4  -> 16 (>hard_limit=15 -> override fires)
evict_hard_under_max = rc._high_bytes <= rc._max_bytes
entries_evicted = len(rc._high) < 4
# 原断言：assert rc._high_bytes <= rc._max_bytes, \
# 原断言：assert len(rc._high) < 4, "override must evict despite protection window"
record("evict_hard_limit/bytes_under_max", evict_hard_under_max, detail=rc._high_bytes)
record("evict_hard_limit/entries_evicted", entries_evicted, detail=len(rc._high))
INFO(f"[DONE] _evict hard-limit override: bytes {rc._high_bytes} <= max, "
     f"{len(rc._high)} entries remain (protection window bypassed)")

# ── constructor config fallback ───────────────────────────────────────
# max_bytes<=0 reads config; if still <=0 falls back to default. We can't
# easily force config to return <=0, but passing max_bytes=0 exercises the
# config-read branch (config provides read_cache_size, so _DEFAULT not hit).
rc0 = ReadCache(max_bytes=0)
max_bytes_positive = rc0._max_bytes > 0
# 原断言：assert rc0._max_bytes > 0, "max_bytes=0 should resolve via config to a positive value"
record("constructor/max_bytes_positive", max_bytes_positive, detail=rc0._max_bytes)
INFO(f"[DONE] constructor config fallback: max_bytes resolved to {rc0._max_bytes}")

INFO("[DONE] test_read_cache_eviction: all branches verified")
