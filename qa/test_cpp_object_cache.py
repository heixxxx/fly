"""QA test: C++ exported object read goes through C++ ObjectCache high tier.

Covers the _read_from_db / _get_py_name dispatch added so that nanobind
(FLY_EXPORT_SERIALIZE) classes enjoy the C++ high-tier cache (省反序列化),
while pickle objects keep using the Python-side cache.

Run as a standalone fly process (fresh singletons, no cross-test pollution).
"""
import os
import sys
import tempfile


def main():
    from fly import open_db
    from _fly_storage import EXStgIndexEntry

    tmpdir = tempfile.mkdtemp(prefix="fly_test_cpp_cache_")
    db = open_db(tmpdir)

    # ── 1. C++ exported class write + read round-trip ──
    entry = EXStgIndexEntry("test/entry", "data.dat", 100, 512, False, 0)
    db.write_object("test/entry", entry)

    # drain write-back so the object is readable
    import _fly_storage
    _fly_storage.ex_stg_get_data_service().drain_write_back()

    # Clear cache so we observe the population from scratch.
    _fly_storage.ex_stg_cache_clear()
    assert _fly_storage.ex_stg_cache_high_size() == 0, "cache should be empty after clear"

    # read_object should dispatch to _read_from_db (C++ high tier) for is_cpp obj
    result = db.read_object("test/entry")
    assert isinstance(result, EXStgIndexEntry), \
        f"expected EXStgIndexEntry, got {type(result)}"
    assert result.object_name == "test/entry"
    assert result.file_name == "data.dat"
    assert result.offset == 100
    assert result.size == 512
    assert result.is_large == False
    assert result.block_count == 0
    # First read must populate the C++ high tier.
    assert _fly_storage.ex_stg_cache_high_size() == 1, \
        f"high tier should have 1 entry after cpp read, got {_fly_storage.ex_stg_cache_high_size()}"
    print("[PASS] cpp class write→read round-trip + populates C++ high tier")

    # ── 2. Second read hits C++ high tier (cached, no re-deserialize) ──
    result2 = db.read_object("test/entry")
    assert isinstance(result2, EXStgIndexEntry)
    assert result2.object_name == "test/entry"
    assert result2.offset == 100
    # Still exactly 1 entry (hit, not a new populate).
    assert _fly_storage.ex_stg_cache_high_size() == 1, \
        "second read should hit (not add) high tier entry"
    print("[PASS] cpp class second read (high-tier cache hit)")

    # ── 3. _get_py_name returns the stored type name ──
    py_name = db._db._get_py_name("test/entry")
    assert py_name == "EXStgIndexEntry", f"expected EXStgIndexEntry, got {py_name}"
    print("[PASS] _get_py_name returns stored type name")

    # ── 4. Pickle object still works (does NOT go through _read_from_db) ──
    # Pickle objects must NOT populate the C++ high tier (they use Python cache).
    high_before_pickle = _fly_storage.ex_stg_cache_high_size()
    py_data = {"key": "value", "nums": [1, 2, 3]}
    db.write_object("py/obj", py_data)
    _fly_storage.ex_stg_get_data_service().drain_write_back()

    py_result = db.read_object("py/obj")
    assert py_result == py_data, f"pickle round-trip mismatch: {py_result}"
    # C++ high tier unchanged — pickle objects don't enter it.
    assert _fly_storage.ex_stg_cache_high_size() == high_before_pickle, \
        "pickle read must not populate C++ high tier"
    print("[PASS] pickle object read (Python path, C++ high tier unaffected)")

    # ── 5. Pickle object with cache="high" uses Python ReadCache ──
    py_hi1 = db.read_object("py/obj", cache="high")
    py_hi2 = db.read_object("py/obj", cache="high")
    assert py_hi1 == py_data
    assert py_hi2 == py_data
    print("[PASS] pickle object cache=high (Python ReadCache)")

    # ── 6. cache param does not break cpp class reads ──
    r_high = db.read_object("test/entry", cache="high")
    assert isinstance(r_high, EXStgIndexEntry)
    assert r_high.offset == 100
    print("[PASS] cpp class read with cache=high")

    db.reset()
    print("\nAll cpp object cache tests passed!")


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"[FAIL] {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
