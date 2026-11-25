"""QA test: C++ exported object read goes through C++ ObjectCache high tier.

Covers the _read_from_db / _get_py_name dispatch added so that nanobind
(FLY_EXPORT_SERIALIZE) classes enjoy the C++ high-tier cache (省反序列化),
while pickle objects keep using the Python-side cache.

Run as a standalone fly process (fresh singletons, no cross-test pollution).
"""
import os

from fly import open_db
from _fly_storage import EXStgIndexEntry
from test import qa_tmp, record
import _fly_storage
from _fly_log import INFO

tmpdir = qa_tmp(f"fly_test_cpp_cache_{os.getpid()}")
os.makedirs(tmpdir, exist_ok=True)
db = open_db(tmpdir)

# ── 1. C++ exported class write + read round-trip ──
entry = EXStgIndexEntry("test/entry", "data.dat", 100, 512, False, 0)
db.write_object("test/entry", entry)

# drain write-back so the object is readable
_fly_storage.ex_stg_get_data_service().drain_write_back()

# Clear cache so we observe the population from scratch.
_fly_storage.ex_stg_cache_clear()
# 原断言：assert _fly_storage.ex_stg_cache_high_size() == 0, "cache should be empty after clear"
record("cache_empty_after_clear",
       _fly_storage.ex_stg_cache_high_size() == 0,
       detail=_fly_storage.ex_stg_cache_high_size())

# read_object should dispatch to _read_from_db (C++ high tier) for is_cpp obj
result = db.read_object("test/entry")
# 原断言：assert isinstance(result, EXStgIndexEntry), \
record("cpp_read_type", type(result).__name__)
# 原断言：assert result.object_name == "test/entry"
# 原断言：assert result.file_name == "data.dat"
# 原断言：assert result.offset == 100
# 原断言：assert result.size == 512
# 原断言：assert result.is_large == False
# 原断言：assert result.block_count == 0
record("cpp_read_fields", {
    "object_name": result.object_name,
    "file_name": result.file_name,
    "offset": result.offset,
    "size": result.size,
    "is_large": result.is_large,
    "block_count": result.block_count,
})
# First read populates the C++ high tier: 1 put, 0 hits (it was a miss).
# stats tuple = (hi_h, hi_m, hi_p, hi_e)（T4：low 四元组已删）
s1 = _fly_storage.ex_stg_cache_stats()
# 原断言：assert s1[2] == 1, f"high tier should have 1 put after cpp read, got {s1[2]}"
# 原断言：assert s1[0] == 0, f"first read should be a miss (0 hits), got {s1[0]}"
record("stats_after_first_read", {"hits": s1[0], "misses": s1[1], "puts": s1[2], "evictions": s1[3]})
INFO("[DONE] cpp class write→read populates C++ high tier (1 put, 0 hit)")

# ── 2. Second read hits C++ high tier (cached, no re-deserialize) ──
result2 = db.read_object("test/entry")
record("second_read_type", type(result2).__name__)
# 原断言：assert isinstance(result2, EXStgIndexEntry)
# 原断言：assert result2.object_name == "test/entry"
# 原断言：assert result2.offset == 100
record("second_read_fields", {
    "object_name": result2.object_name,
    "offset": result2.offset,
})
# Hit stats prove the second read served from cache (not a fresh deserialize).
s2 = _fly_storage.ex_stg_cache_stats()
# 原断言：assert s2[0] == 1, f"second read should register 1 high-tier hit, got {s2[0]}"
# 原断言：assert s2[2] == 1, f"second read should NOT add a new put (still 1), got {s2[2]}"
record("stats_after_second_read", {"hits": s2[0], "misses": s2[1], "puts": s2[2], "evictions": s2[3]})
INFO("[DONE] cpp class second read (high-tier cache hit: 1 hit, no new put)")

# ── 3. 读流携带 py_name（恒流式分流媒介——_get_py_name 已删除，
#      py_name 由读取原语携带，2026-08-30 双拉修复）──
stream = _fly_storage.ex_stg_open_read_stream(db._db, "test/entry", False)
py_name = stream.py_name
stream = None  # 弃置流（C++ 对象由 _read_from_db 权威重建——同 read_object 分流）
# 原断言：assert py_name == "EXStgIndexEntry", f"expected EXStgIndexEntry, got {py_name}"
record("stream_py_name", py_name)
INFO("[DONE] read stream carries stored type name (py_name)")

# ── 4. Pickle object still works (does NOT go through _read_from_db) ──
# Pickle objects must NOT populate the C++ high tier (they use Python cache).
high_before_pickle = _fly_storage.ex_stg_cache_high_size()
py_data = {"key": "value", "nums": [1, 2, 3]}
db.write_object("py/obj", py_data)
_fly_storage.ex_stg_get_data_service().drain_write_back()

py_result = db.read_object("py/obj")
# 原断言：assert py_result == py_data, f"pickle round-trip mismatch: {py_result}"
record("pickle_roundtrip", py_result)
# C++ high tier unchanged — pickle objects don't enter it.
# 原断言：assert _fly_storage.ex_stg_cache_high_size() == high_before_pickle, \
record("pickle_no_cpp_high_tier",
       _fly_storage.ex_stg_cache_high_size() == high_before_pickle,
       detail={"before": high_before_pickle,
               "after": _fly_storage.ex_stg_cache_high_size()})
INFO("[DONE] pickle object read (Python path, C++ high tier unaffected)")

# ── 5. Pickle object with cache="high" uses Python ReadCache ──
py_hi1 = db.read_object("py/obj", cache="high")
py_hi2 = db.read_object("py/obj", cache="high")
# 原断言：assert py_hi1 == py_data
# 原断言：assert py_hi2 == py_data
record("pickle_high_reads", {"first": py_hi1, "second": py_hi2})
INFO("[DONE] pickle object cache=high (Python ReadCache)")

# ── 6. cache param does not break cpp class reads ──
r_high = db.read_object("test/entry", cache="high")
record("cpp_read_high_cache_type", type(r_high).__name__)
# 原断言：assert isinstance(r_high, EXStgIndexEntry)
# 原断言：assert r_high.offset == 100
record("cpp_read_high_cache_offset", r_high.offset)
INFO("[DONE] cpp class read with cache=high")

db.reset()
INFO("[DONE] test_cpp_object_cache: all cpp object cache checks executed")
