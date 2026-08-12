"""load_db 重建 write_provenance_ 的端到端验证（.pyt 编排，替代旧 subprocess wrapper）。

sub1 (provenance_load_db_run1.py): 写 prov_key=42
sub2 (provenance_load_db_run2.py): load_db + idempotent 重跑 + 不同 context 写 mismatch 验证

db 在 case log 目录（FLY_CASE_LOG_DIR/prov_db），自动随 case 清。两个 sub case 经 env
共享 FLY_DB_PATH（per-subcase env，不污染全局 os.environ，并发安全）。
"""
import os

db_path = os.path.join(FLY_CASE_LOG_DIR, "prov_db")
shared_env = {"FLY_DB_PATH": db_path}

run_subcase("provenance_load_db_run1.py", timeout=60, env=shared_env)
run_subcase("provenance_load_db_run2.py", timeout=60, env=shared_env)
INFO("[PASS] test_write_provenance_load_db: provenance rebuilt from idx after load_db")
