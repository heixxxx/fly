"""ras_graph 矩阵文件 IO 原子性测试（P3-24 修复的回归防护）。

P3-24：compute_exact_solution 后台线程曾原地 np.savez 重写共享 npz——
高负载（coverage 全量 -j6）下并行 worker task 读到 truncate 窗口的截断
视图（zipfile EOFError，2/2 复现）。修复为 tmp + os.replace 原子替换。

本测试验证写协议的确定性不变量：文件始终 testzip 完整、无 tmp 残留、
幂等重写安全。
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "py"))
from ras_graph import generate_poisson_matrix, compute_exact_solution


def _assert_npz_complete(path, expect_exact):
    import zipfile
    import numpy as np
    z = zipfile.ZipFile(path)
    assert z.testzip() is None, f"npz corrupt (truncated?): {path}"
    d = np.load(path, allow_pickle=False)
    for key in ("n", "N", "rows", "cols", "vals", "b"):
        assert key in d.files, f"missing {key} in {path}"
    if expect_exact:
        assert "x_exact" in d.files, f"missing x_exact in {path}"
        assert d["x_exact"].shape == (int(d["N"]),)


def test_generate_atomic_no_tmp_residue():
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, "m.npz")
        generate_poisson_matrix(4, p, compute_exact=False)
        _assert_npz_complete(p, expect_exact=False)
        assert not os.path.exists(p + ".tmp_gen"), "tmp file must not residue"
        assert not os.path.exists(p + ".tmp_exact"), "tmp file must not residue"


def test_generate_with_exact_atomic():
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, "m.npz")
        generate_poisson_matrix(4, p, compute_exact=True)
        _assert_npz_complete(p, expect_exact=True)
        assert not os.path.exists(p + ".tmp_gen")
        assert not os.path.exists(p + ".tmp_exact")


def test_compute_exact_rewrite_atomic_and_idempotent():
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, "m.npz")
        generate_poisson_matrix(4, p, compute_exact=False)
        x1 = compute_exact_solution(4, p)
        _assert_npz_complete(p, expect_exact=True)
        assert not os.path.exists(p + ".tmp_exact")

        # 幂等：二次重写（模拟并发/重复调用）仍完整。
        x2 = compute_exact_solution(4, p)
        _assert_npz_complete(p, expect_exact=True)
        assert abs(x1[0] - x2[0]) < 1e-12, "idempotent rewrite must not change data"


if __name__ == "__main__":
    test_generate_atomic_no_tmp_residue()
    print("PASS: generate atomic, no residue")
    test_generate_with_exact_atomic()
    print("PASS: generate+exact atomic")
    test_compute_exact_rewrite_atomic_and_idempotent()
    print("PASS: exact rewrite atomic + idempotent")
    print("ALL PASSED")
