#!/usr/bin/env python3
"""Test: _is_coarse 缓存必须反映当前 cfg，不能跨 solve / 跨 db 残留旧值。

复现 bug: _COARSE_CACHE 是模块级全局 dict，不区分 db 实例也不随 cfg 失效。
worker 进程常驻，跑完 solve A (omega=1.0, _is_coarse=False) 后，
跑 solve B (omega="coarse", _is_coarse 应=True) 时，仍读到 A 的缓存值 False，
导致 _compute_deps 选错依赖对象（__rasg__x_ vs __rasg__xc_）。
"""
from _fly_log import INFO
from solver.py.ras_graph import _is_coarse


class _FakeDb:
    """最小 mock：read_object 只返回预设的 cfg。"""
    def __init__(self, cfg):
        self._cfg = cfg

    def read_object(self, name):
        if name == "__rasg__cfg":
            return self._cfg
        raise KeyError(name)


def main():
    # ── 场景 1: omega=1.0（数值）→ _is_coarse 应为 False ──
    db_a = _FakeDb({"omega": 1.0})
    result_a = _is_coarse(db_a)
    assert result_a is False, f"omega=1.0 should be non-coarse, got {result_a}"
    INFO("[1] omega=1.0 → _is_coarse=False ✓")

    # ── 场景 2: 同进程、新 db、omega="coarse_aitken" → 应为 True ──
    #    BUG: 当前实现读 _COARSE_CACHE["v"]=False，返回错误结果
    db_b = _FakeDb({"omega": "coarse_aitken"})
    result_b = _is_coarse(db_b)
    assert result_b is True, (
        f"omega='coarse_aitken' should be coarse=True, got {result_b}. "
        f"BUG: _COARSE_CACHE leaked stale value from previous solve."
    )
    INFO("[2] omega='coarse_aitken' → _is_coarse=True ✓ (缓存未污染)")

    # ── 场景 3: 再切回 omega=1.0 → 应为 False（验证双向正确）──
    db_c = _FakeDb({"omega": 1.0})
    result_c = _is_coarse(db_c)
    assert result_c is False, (
        f"omega=1.0 should be non-coarse, got {result_c}. "
        f"BUG: _COARSE_CACHE leaked stale value from coarse solve."
    )
    INFO("[3] omega=1.0 again → _is_coarse=False ✓ (双向正确)")

    # ── 场景 4: omega="adaptive"（字符串但不含 coarse）→ 应为 False ──
    db_d = _FakeDb({"omega": "adaptive"})
    result_d = _is_coarse(db_d)
    assert result_d is False, f"omega='adaptive' should be non-coarse, got {result_d}"
    INFO("[4] omega='adaptive' → _is_coarse=False ✓")

    INFO("[PASS] test_is_coarse_cache")


if __name__ == "__main__":
    main()
