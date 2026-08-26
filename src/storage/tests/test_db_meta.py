"""Unit tests for db_meta.py — _DB_META JSON 工具、uid 生成、readers-writer 锁。

纯 Python 测试（不需要 fly runtime），直接 import db_meta 模块。
运行：./fly.sh test //src/storage/tests:db_meta_test
"""
import sys
import os
import json
import time
import threading
import shutil
import tempfile

# 让 import 能找到 storage/py
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'py'))

# stub _fly_log — 测试不依赖真实 C++ log
import types
_log_mod = types.ModuleType("_fly_log")
_log_mod.WARN = lambda *a, **kw: None
_log_mod.INFO = lambda *a, **kw: None
_log_mod.DBG = lambda *a, **kw: None
_log_mod.ERR = lambda *a, **kw: None
sys.modules["_fly_log"] = _log_mod

from db_meta import (
    generate_uid, DbMetaFile,
    make_meta, make_edge, find_edge, update_edge_path, remove_edge,
    append_edge, match_edge,
    _META_FILE, _META_VERSION,
)


def test_uid_generation_unique():
    """uid 应该在快速连续生成时保持唯一（纳秒时间戳保证）。"""
    uids = set()
    for _ in range(1000):
        u = generate_uid("/test/path", "matrix")
        assert len(u) == 12
        assert u not in uids
        uids.add(u)
    print("  PASS: test_uid_generation_unique")


def test_uid_includes_role():
    """不同 role 应生成不同 uid（即使时间戳相同）。"""
    u1 = generate_uid("/test/path", "matrix")
    u2 = generate_uid("/test/path", "solve")
    assert u1 != u2
    print("  PASS: test_uid_includes_role")


def test_meta_file_write_read():
    """基本写入和读取（完整 schema：chain 字段 + workers + data_path）。"""
    tmpdir = tempfile.mkdtemp()
    try:
        mf = DbMetaFile(tmpdir)
        meta = make_meta("abc123", "matrix", "matrix")
        mf.write_new(meta)

        assert mf.exists()
        read_back = mf.read()
        assert read_back["uid"] == "abc123"
        assert read_back["role"] == "matrix"
        assert read_back["logical_name"] == "matrix"
        assert read_back["version"] == _META_VERSION
        assert read_back["prev"] == []
        assert read_back["next"] == []
        assert read_back["absorbed_from"] == []
        assert read_back["workers"] == []
        assert read_back["data_path"] == ""
        assert read_back["created_at"] > 0
        assert os.path.isfile(os.path.join(tmpdir, _META_FILE))
        print("  PASS: test_meta_file_write_read")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def test_meta_file_read_missing():
    """读不存在的文件返回 None，不报错。"""
    tmpdir = tempfile.mkdtemp()
    try:
        mf = DbMetaFile(tmpdir)
        assert mf.read() is None
        assert not mf.exists()
        print("  PASS: test_meta_file_read_missing")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def test_meta_file_update():
    """update 在持锁下 read-modify-write。"""
    tmpdir = tempfile.mkdtemp()
    try:
        mf = DbMetaFile(tmpdir)
        mf.write_new(make_meta("uid1", "matrix", "matrix"))

        # update 追加 prev
        def add_prev(d):
            d["prev"].append(make_edge("uid2", "input", "input", "/path/input"))
            return d

        result = mf.update(add_prev)
        assert len(result["prev"]) == 1
        assert result["prev"][0]["uid"] == "uid2"

        # 确认落盘
        read_back = mf.read()
        assert len(read_back["prev"]) == 1
        print("  PASS: test_meta_file_update")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def test_meta_file_update_on_empty():
    """update 在文件不存在时从空 dict 开始。"""
    tmpdir = tempfile.mkdtemp()
    try:
        mf = DbMetaFile(tmpdir)

        def init(d):
            d["uid"] = "new_uid"
            d["role"] = "test"
            return d

        result = mf.update(init)
        assert result["uid"] == "new_uid"
        assert mf.exists()
        print("  PASS: test_meta_file_update_on_empty")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def test_append_worker():
    """append_worker 追加写者登记；重复条目不重复追加。"""
    tmpdir = tempfile.mkdtemp()
    try:
        mf = DbMetaFile(tmpdir)
        mf.write_new(make_meta("uid_w", "test", "test"))

        entry = {"worker_id": 1, "writer_id": "d5681a56", "hostname": "h1",
                 "ip_address": "10.0.0.1", "launch_command": ""}
        mf.append_worker(entry)
        mf.append_worker(entry)  # 幂等
        mf.append_worker(dict(entry, writer_id="d9999999"))  # 不同 writer → 新条目

        workers = mf.read()["workers"]
        assert len(workers) == 2
        assert workers[0]["hostname"] == "h1"
        assert workers[1]["writer_id"] == "d9999999"
        print("  PASS: test_append_worker")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def test_write_new_preserves_workers():
    """write_new（merge target 重写）保留已有 workers 键——并发 WorkerInfo
    追加与整文件重写的窄窗口下不丢登记条目。"""
    tmpdir = tempfile.mkdtemp()
    try:
        mf = DbMetaFile(tmpdir)
        mf.write_new(make_meta("uid_p", "test", "test"))
        mf.append_worker({"worker_id": 1, "writer_id": "w1", "hostname": "h1",
                          "ip_address": "", "launch_command": ""})

        # merge 场景：write_new 重写身份字段（uid 变化），workers 必须保留
        mf.write_new(make_meta("uid_new", "test", "test", absorbed_from=["/old"]))

        read_back = mf.read()
        assert read_back["uid"] == "uid_new"
        assert read_back["absorbed_from"] == ["/old"]
        assert len(read_back["workers"]) == 1
        assert read_back["workers"][0]["writer_id"] == "w1"
        print("  PASS: test_write_new_preserves_workers")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def test_concurrent_readers():
    """多个线程并发读不阻塞（LOCK_SH）。"""
    tmpdir = tempfile.mkdtemp()
    try:
        mf = DbMetaFile(tmpdir)
        mf.write_new(make_meta("uid_c", "test", "test"))

        errors = []

        def reader():
            try:
                for _ in range(100):
                    d = mf.read()
                    assert d is not None
                    assert d["uid"] == "uid_c"
            except Exception as e:
                errors.append(e)

        threads = [threading.Thread(target=reader) for _ in range(4)]
        t0 = time.time()
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=10)
        elapsed = time.time() - t0

        assert not errors, f"reader errors: {errors}"
        assert elapsed < 10, f"concurrent reads took too long: {elapsed}s"
        print(f"  PASS: test_concurrent_readers ({elapsed:.2f}s)")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def test_writer_serializes_with_readers():
    """写者（LOCK_EX）应与读者（LOCK_SH）互斥：写时读者等完。"""
    tmpdir = tempfile.mkdtemp()
    try:
        mf = DbMetaFile(tmpdir)
        mf.write_new(make_meta("uid_s", "test", "test"))

        errors = []
        write_done = threading.Event()

        def writer():
            try:
                time.sleep(0.05)  # 让读者先拿到 SH 锁
                def bump(d):
                    d["counter"] = d.get("counter", 0) + 1
                    return d
                mf.update(bump)
                write_done.set()
            except Exception as e:
                errors.append(e)

        def reader():
            try:
                d = mf.read()
                assert d is not None
            except Exception as e:
                errors.append(e)

        t_write = threading.Thread(target=writer)
        t_write.start()

        readers = [threading.Thread(target=reader) for _ in range(3)]
        for t in readers:
            t.start()

        t_write.join(timeout=10)
        for t in readers:
            t.join(timeout=10)

        assert not errors, f"errors: {errors}"
        assert write_done.is_set(), "writer should complete"
        print("  PASS: test_writer_serializes_with_readers")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def test_concurrent_writers():
    """多个写者并发 update 不丢更新（LOCK_EX 串行化）。"""
    tmpdir = tempfile.mkdtemp()
    try:
        mf = DbMetaFile(tmpdir)
        mf.write_new(make_meta("uid_cw", "test", "test"))

        errors = []

        def incrementer():
            try:
                for _ in range(20):
                    def bump(d):
                        d["counter"] = d.get("counter", 0) + 1
                        return d
                    mf.update(bump)
            except Exception as e:
                errors.append(e)

        threads = [threading.Thread(target=incrementer) for _ in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=30)

        assert not errors, f"writer errors: {errors}"
        final = mf.read()
        # 4 threads × 20 increments = 80
        assert final["counter"] == 80, f"expected 80, got {final['counter']} (lost update!)"
        print(f"  PASS: test_concurrent_writers (final counter={final['counter']})")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def test_concurrent_writers_disjoint_keys():
    """线程级并发写不同键区（workers 追加 ↔ chain 边改写）：双方更新都保留。

    这是合并后 master 进程内真并发的直接验证——C++ lane 线程的 WorkerInfo
    回调与主线程 merge/migrate 边改写并发，flock（同进程不同 fd 互斥）+
    持锁 RMW 保证不丢。
    """
    tmpdir = tempfile.mkdtemp()
    try:
        mf = DbMetaFile(tmpdir)
        mf.write_new(make_meta("uid_dj", "test", "test"))

        errors = []
        N = 30

        def worker_appender():
            try:
                for i in range(N):
                    mf.append_worker({"worker_id": i, "writer_id": f"w{i}",
                                      "hostname": f"h{i % 3}",
                                      "ip_address": "", "launch_command": ""})
            except Exception as e:
                errors.append(e)

        def edge_writer():
            try:
                for i in range(N):
                    def add_prev(d, i=i):
                        d["prev"].append(make_edge(f"uid_p{i}", "up", "up", f"/p{i}"))
                        return d
                    mf.update(add_prev)
            except Exception as e:
                errors.append(e)

        t1 = threading.Thread(target=worker_appender)
        t2 = threading.Thread(target=edge_writer)
        t1.start()
        t2.start()
        t1.join(timeout=30)
        t2.join(timeout=30)

        assert not errors, f"errors: {errors}"
        final = mf.read()
        assert len(final["workers"]) == N, \
            f"workers lost updates: {len(final['workers'])}/{N}"
        assert len(final["prev"]) == N, \
            f"prev edges lost updates: {len(final['prev'])}/{N}"
        print("  PASS: test_concurrent_writers_disjoint_keys")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def test_edge_helpers():
    """make_edge / find_edge / update_edge_path / remove_edge / append_edge。"""
    edges = [
        make_edge("uid1", "matrix", "matrix", "/path/matrix"),
        make_edge("uid2", "input", "input", "/path/input"),
    ]

    # find_edge
    assert find_edge(edges, "uid1")["role"] == "matrix"
    assert find_edge(edges, "uid3") is None

    # update_edge_path
    assert update_edge_path(edges, "uid1", "/new/path")
    assert edges[0]["db_path"] == "/new/path"
    assert not update_edge_path(edges, "uid3", "/x")

    # remove_edge
    new_edges = remove_edge(edges, "uid1")
    assert len(new_edges) == 1
    assert new_edges[0]["uid"] == "uid2"

    # append_edge (new)
    new_list, appended = append_edge(edges, make_edge("uid3", "x", "x", "/p"))
    assert appended is True
    assert len(new_list) == 3

    # append_edge (duplicate uid)
    new_list2, appended2 = append_edge(edges, make_edge("uid1", "dup", "dup", "/dup"))
    assert appended2 is False
    assert len(new_list2) == 2
    print("  PASS: test_edge_helpers")


def test_match_edge():
    """match_edge 的 AND 组合匹配。"""
    edge = make_edge("uid1", "matrix", "matrix", "/path")

    # 全 None → 匹配任意
    assert match_edge(edge)

    # uid 匹配
    assert match_edge(edge, uid="uid1")
    assert not match_edge(edge, uid="uid2")

    # role 匹配
    assert match_edge(edge, role="matrix")
    assert not match_edge(edge, role="solve")

    # logical_name 匹配
    assert match_edge(edge, logical_name="matrix")
    assert not match_edge(edge, logical_name="solve")

    # 组合匹配
    assert match_edge(edge, uid="uid1", role="matrix")
    assert not match_edge(edge, uid="uid1", role="solve")
    print("  PASS: test_match_edge")


def test_remove_meta():
    """remove 删除 _DB_META 和 lock 文件。"""
    tmpdir = tempfile.mkdtemp()
    try:
        mf = DbMetaFile(tmpdir)
        mf.write_new(make_meta("uid_r", "test", "test"))
        assert mf.exists()

        mf.remove()
        assert not mf.exists()
        assert not os.path.isfile(os.path.join(tmpdir, _META_FILE + ".lock"))

        # 再删不报错
        mf.remove()
        print("  PASS: test_remove_meta")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def test_corrupted_meta():
    """损坏的 _DB_META 文件：read 返回 None，update 视为空。"""
    tmpdir = tempfile.mkdtemp()
    try:
        mf = DbMetaFile(tmpdir)
        os.makedirs(tmpdir, exist_ok=True)
        with open(mf.path, "w") as f:
            f.write("{ broken json")

        # read 损坏 → None
        assert mf.read() is None

        # update 损坏 → 视为空，写入新数据
        def fix(d):
            d["uid"] = "recovered"
            return d
        result = mf.update(fix)
        assert result["uid"] == "recovered"

        # 后续 read 正常
        assert mf.read()["uid"] == "recovered"
        print("  PASS: test_corrupted_meta")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def _run_all():
    tests = [
        test_uid_generation_unique,
        test_uid_includes_role,
        test_meta_file_write_read,
        test_meta_file_read_missing,
        test_meta_file_update,
        test_meta_file_update_on_empty,
        test_append_worker,
        test_write_new_preserves_workers,
        test_concurrent_readers,
        test_writer_serializes_with_readers,
        test_concurrent_writers,
        test_concurrent_writers_disjoint_keys,
        test_edge_helpers,
        test_match_edge,
        test_remove_meta,
        test_corrupted_meta,
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
