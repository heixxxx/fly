"""测试数据目录 helper（用后即清治理，2026-08-29 用户裁定）。

背景：测试曾硬编码 ``/tmp/fly_*`` 存放运行数据，清理全靠测试正常走完
teardown——超时杀进程/崩溃/中断即漏清，数月累积 44334 个目录 36G，最终
触发 WSL 磁盘事件。个人电脑硬盘紧张，必须用后即清。

语义（两层落点，均不污染系统 /tmp）：
  1. runqa 环境：``FLY_CASE_LOG_DIR``（case 级日志目录）——数据与日志同
     目录共生共死，``_clean_case_logs`` 每轮清理（用后即清，失败轮现场
     保留到下一轮启动前）。
  2. 手动直跑（无 runqa）：``$FLY_ROOT/.work/tmp/``——项目内中间产物区
     （AGENTS.md 纪律：.work 任务结束清理；崩溃残留也在项目树内可发现）。

注意：本函数【只拼路径不建目录】——fly 的 --log-dir 要求 log_dir 不存在
（存在则 resolve .N 变体），预创建会破坏该语义；数据目录由 open_db 的
makedirs 自建。
"""
import os

# src/test/py/qa_tmp.py → 上溯 4 级 = 仓库根（手动跑恒在 repo 树内；
# runqa/bazel 场景由 FLY_CASE_LOG_DIR 分支先行，不依赖此推导）。
_REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))


def qa_tmp(name: str) -> str:
    base = os.environ.get("FLY_CASE_LOG_DIR")
    if base:
        return os.path.join(base, name)
    return os.path.join(_REPO_ROOT, ".work", "tmp", name)
