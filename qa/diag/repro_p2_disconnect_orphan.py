"""诊断复现脚本（issue 007 — Problem 2，高）：on_disconnect snapshot 在锁外 → task 孤儿。

定位：master_agent.cpp on_disconnect 中 get_task_ids_by_worker(W) 的 snapshot 原在
schedule_mutex_ 之外，与 scheduler 持锁的 get_idle_workers→assign 序列交错时，snapshot
会漏掉「刚 assign 到 W、尚未被 scheduler 释放锁提交 metadata 的 task」→ 该 task 永久孤儿
（RUNNING@DEAD-W，graph 已 remove，无人恢复）。修复：DEAD 标记 + snapshot 纳入 schedule_mutex_。

触发形式（真实场景）：worker 在被 scheduler assign task 的瞬间断连/被杀。

本脚本不是 runqa 常态用例（文件名非 test_ 前缀，runqa 的 os.walk 不收集），仅作问题确认/
回归诊断：可手动 `./build/bin/fly qa/diag/repro_p2_disconnect_orphan.py` 运行。
判定：所有提交的 task 最终必须 completed 或 failed，不得有 task 永久卡 RUNNING@DEAD worker。
"""
from _fly_log import INFO
import os
import time
import signal


N_TASKS = 60          # 足量 task 放大「assign 中断连」窗口命中概率
KILL_AT = 10          # 完成 10 个后立刻杀一个 worker，制造 assign-期间-断连
DONE_TIMEOUT = 90.0


def wait_for(cond, timeout=20.0, interval=0.2):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if cond():
            return True
        time.sleep(interval)
    return False


def main():
    from fly.runtime import get_agent
    from fly import open_db, get_config
    from test import write_data

    get_config().set_int("fail_unscheduleable_tasks", 0)
    log_dir = get_config().get_str("log_dir")
    db_path = os.path.join(log_dir, "diag_p2_db")

    import shutil
    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)

    master = get_agent()
    master.launch_local_workers([{}, {}])
    assert master.wait_for_workers(2), f"need 2 workers, got {master.worker_count}"

    db = open_db(db_path)
    for i in range(N_TASKS):
        write_data(db, f"k{i}", i)

    # 等少量 task 跑起来，随即杀一个 worker —— 制造「assign 进行中断连」场景。
    assert wait_for(lambda: len(master.completed_tasks) >= KILL_AT, timeout=30.0), \
        f"预热失败：仅 {len(master.completed_tasks)} 完成"
    INFO(f"[diag] 预热完成 {len(master.completed_tasks)}，杀死一个 worker 以触发 assign-期-断连")

    pids = master.get_worker_pids()
    assert len(pids) >= 2
    os.kill(pids[0], signal.SIGKILL)
    try:
        os.waitpid(pids[0], 0)
    except ChildProcessError:
        pass

    # 不变量：所有 task 最终必须 done（completed 或 failed）；不得有孤儿卡 RUNNING。
    total_done = lambda: len(master.completed_tasks) + len(master.failed_tasks)
    ok = wait_for(lambda: total_done() >= N_TASKS, timeout=DONE_TIMEOUT)
    running = master.running_tasks
    INFO(f"[diag] 终态：completed={len(master.completed_tasks)} "
         f"failed={len(master.failed_tasks)} running={len(running)}")

    if not ok:
        # 失败现场：仍有 task 卡 running（疑似 Problem 2 孤儿）。
        detail = (f"期望全部 {N_TASKS} task 完成，实际 done={total_done()}，"
                  f"running={list(running)[:10]}")
        if running:
            INFO(f"[RED] 疑似 Problem 2 孤儿：{len(running)} 个 task 卡 RUNNING@死 worker：{detail}")
        raise SystemExit(f"[FAIL] {detail}")
    INFO(f"[GREEN] 全部 {N_TASKS} task 均已 done（无孤儿）")


main()
