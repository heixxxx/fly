"""验证 run_subcase 的 expect_pass 语义（正常返回场景）。

场景1: expect_pass=False + fly FAILED  → 正常返回（预期失败达成）
场景2: expect_pass=True  + fly PASSED  → 正常返回
（场景3 expect_pass=False+PASSED 与场景4 expect_pass=True+FAILED 会 raise，需单独验证）
"""
# 场景1: 期望失败 + 实际 FAILED → ok（预期失败达成）
run_subcase("always_fail.py", timeout=15, expect_pass=False)
# 场景2: 期望成功 + 实际 PASSED → ok
run_subcase("always_pass.py", timeout=15)
INFO("[PASS] expect_pass: scenario1 (expect_fail+FAILED=ok) + scenario2 (expect_pass+PASSED=ok)")
