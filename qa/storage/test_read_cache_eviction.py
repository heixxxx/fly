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


def _entry_age(rc, key, seconds):
    """Force a cached entry's created_at/last_access back by `seconds`."""
    e = rc._high[key]
    e.created_at -= seconds
    e.last_access -= seconds


# ── put/get basics, size inference, level filtering ───────────────────
rc = ReadCache(max_bytes=1 << 20)
rc.put("b", "high", b"abcd")          # bytes -> size inferred via len()
assert rc._high["b"].size == 4, "size should be inferred from len(bytes)"
assert rc.get("b", "high") == b"abcd"
assert rc.get("b", "low") is None, "get(level!=high) must return None"
rc.put("s", "low", "ignored")          # level!=high -> not stored
assert "s" not in rc._high, "put(level!=high) must not store"
INFO("[PASS] put/get basics: size inference + level filtering")

# ── put overwrite branch (pop old, debit bytes) ───────────────────────
rc.put("b", "high", b"abcdefgh")       # overwrite same key
assert rc._high["b"].size == 8, "overwrite should replace old size"
assert rc._high_bytes == 8, "bytes should reflect overwrite (old debited)"
assert rc.get("b", "high") == b"abcdefgh", "overwrite should return new value"
INFO("[PASS] put overwrite: old entry debited, new value stored")

# ── remove / clear ────────────────────────────────────────────────────
rc.remove("b")
assert "b" not in rc._high and rc._high_bytes == 0, "remove should drop entry + bytes"
rc.put("x", "high", b"1")
rc.put("y", "high", b"2")
rc.clear()
assert rc._high_bytes == 0 and len(rc._high) == 0, "clear should empty cache"
INFO("[PASS] remove/clear: entries and byte counter reset")

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
assert rc._high_bytes == 12, f"expected 12 (protected), got {rc._high_bytes}"
assert len(rc._high) == 3, "protected entries must not be evicted under hard limit"

# Now age them past the protection window. 'a' is read once more (higher score)
# so it should survive; 'b' and 'c' have score 1 and 'c' was added last.
for k in ("a", "b", "c"):
    _entry_age(rc, k, _PROTECTION_SEC + 1)
rc.get("a", "high")                     # bump a's read_count -> higher score
before = dict(rc._high)
rc.put("d", "high", b"DDDD")            # 4 bytes -> triggers _evict, candidates now eligible
assert rc._high_bytes <= rc._max_bytes, \
    f"eviction should bring bytes under max ({rc._max_bytes}), got {rc._high_bytes}"
assert "a" in rc._high, "highest-score entry 'a' should survive eviction"
INFO(f"[PASS] _evict normal: bytes {rc._high_bytes} <= max, high-score entry kept")

# ── _evict hard-limit override: break protection window ───────────────
# When bytes exceed hard_limit (1.5x max), ALL entries become eviction
# candidates even if <30s old. max=10 -> hard_limit=15.
rc = ReadCache(max_bytes=10)
rc.put("p1", "high", b"AAAA")          # 4
rc.put("p2", "high", b"AAAA")          # 4  -> 8
rc.put("p3", "high", b"AAAA")          # 4  -> 12 (>max but <hard_limit, protected)
rc.put("p4", "high", b"AAAA")          # 4  -> 16 (>hard_limit=15 -> override fires)
assert rc._high_bytes <= rc._max_bytes, \
    f"hard-limit override should evict under max, got {rc._high_bytes}"
assert len(rc._high) < 4, "override must evict despite protection window"
INFO(f"[PASS] _evict hard-limit override: bytes {rc._high_bytes} <= max, "
     f"{len(rc._high)} entries remain (protection window bypassed)")

# ── constructor config fallback ───────────────────────────────────────
# max_bytes<=0 reads config; if still <=0 falls back to default. We can't
# easily force config to return <=0, but passing max_bytes=0 exercises the
# config-read branch (config provides read_cache_size, so _DEFAULT not hit).
rc0 = ReadCache(max_bytes=0)
assert rc0._max_bytes > 0, "max_bytes=0 should resolve via config to a positive value"
INFO(f"[PASS] constructor config fallback: max_bytes resolved to {rc0._max_bytes}")

INFO("[PASS] test_read_cache_eviction: all branches verified")
