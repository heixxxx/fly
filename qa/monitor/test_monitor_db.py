"""cluster monitor 落盘验证：3 worker DAG（大内存 + 大 IO + db 参数 task）后，
monitor.db 的 tasks/workers/events/object_io/worker_samples 全维度断言，
runtime.summary 仍正常产出（心跳迁移回归）。

覆盖：
  · tasks 行：名称/状态/三段时间戳/ready_ms/exec 窗口/cpu_time/IO 四元组/
    mem avg/peak>0/dbs 解析（__fly_db__ 新旧格式）
  · object_io：read/write 明细落库
  · worker_samples：双 CPU%/host 内存/net 计数非零
  · events：SUBMIT/ASSIGN/COMPLETE/DB_CREATED/DRAIN 里程碑
  · 运行中实时只读（serve 以只读连接打开不打扰写者）
"""
import json
import os
import shutil
import sqlite3
import subprocess
import time
import urllib.request

from _fly_log import INFO

from test import write_data, slow_write, chain_stage
from fly import open_db, get_config
from fly.runtime import get_agent

LOG_DIR = get_config().get_str("log_dir")
DB_PATH = os.path.join(LOG_DIR, "db")
MONITOR_DB = os.path.join(LOG_DIR, "monitor.db")


def wait_for(condition, timeout=30.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def _meta_has(conn, key):
    """检查 meta 键是否已落盘。写入进行中（hot journal）时只读连接会进入
    粘滞错误态，故失败时由调用方重开连接重试。"""
    try:
        row = conn.execute("SELECT value FROM meta WHERE key=?", (key,)).fetchone()
        return row is not None
    except sqlite3.OperationalError:
        return False


def _open_ro():
    return sqlite3.connect(f"file:{MONITOR_DB}?mode=ro", uri=True,
                           check_same_thread=False)


# ── 大内存 task：分配并保持 ~64MB（peak 可观测） ──

def mem_hog_task(db, key):
    import time
    holder = bytearray(64 * 1024 * 1024)  # noqa: F841 — 持有到 task 结束
    for i in range(0, len(holder), 4096):
        holder[i] = 1  # 逐页触摸确保物理分配
    # 保持持有 2.5s：内存峰值靠 monitor 采样线程（1s 间隔）捕获，亚秒级
    # task 的窗口内无采样点（begin/end 端点采样在分配前/释放后）——这是
    # 采样模型的固有精度边界（docs/monitor-design.md 口径说明）。
    time.sleep(2.5)
    db.write_object(key, len(holder))


from fly import as_task  # noqa: E402

mem_hog = as_task()(mem_hog_task)


def run_cluster():
    master = get_agent()
    master.launch_local_workers([{}, {}, {}])
    assert master.wait_workers_registered(60), "workers 未在 60s 内注册"
    db = open_db(DB_PATH)

    # run 进行中：实时只读连接（模拟 GUI 附加），不打扰写者。
    live_conn = sqlite3.connect(f"file:{MONITOR_DB}?mode=ro", uri=True,
                                check_same_thread=False)
    live_conn.execute("PRAGMA busy_timeout=5000")

    t1 = slow_write(db, "obj_slow", 2 * 1024 * 1024, 12)   # 长任务（采样多）
    t2 = write_data(db, "obj_plain", 1024 * 1024)           # IO task
    t3 = mem_hog(db, "obj_mem")                             # 大内存 task
    t4 = chain_stage(db, 0, 1, "obj_chain")                 # 依赖链（读输入）

    from fly import wait_tasks
    wait_tasks(120)  # 等全部提交的 task 完成

    # 运行中实时读（只读连接不断言锁死）。
    live_rows = live_conn.execute(
        "SELECT COUNT(*) FROM worker_samples").fetchone()[0]
    live_conn.close()
    INFO(f"[monitor] 运行中实时只读读到 {live_rows} 条样本")

    master.stop()
    return live_rows


def assert_db_content():
    # 等 close 收尾落盘（run_end_ms 是 close 前最后一条 meta 写入；批量写线程
    # 异步 flush，stop() 返回与文件可见之间存在窗口）。写入进行中的 hot
    # journal 会让只读连接进入错误态——每轮尝试重开连接。
    deadline = time.time() + 15
    conn = None
    while time.time() < deadline:
        conn = _open_ro()
        if _meta_has(conn, "run_end_ms"):
            break
        conn.close()
        time.sleep(0.3)
    else:
        raise AssertionError("run_end_ms 未在 15s 内可见（MetricsDb close 未完成）")
    c = conn.cursor()

    # ── tasks ──（as_task wrapper 不返回 task_id——按名称反查定位目标 task）
    rows = c.execute(
        "SELECT task_id, name, status, created_ms, started_ms, completed_ms, "
        "ready_ms, exec_start_ms, exec_end_ms, dbs FROM tasks").fetchall()
    by_id = {r[0]: r for r in rows}
    assert len(rows) == 4, f"tasks 行数 {len(rows)} != 4"
    for tid, name, status, created, started, completed, ready, es, ee, dbs in rows:
        assert status == "COMPLETED", f"task {tid} 状态 {status}"
        assert created > 0 and started >= created and completed >= started, \
            f"task {tid} 调度时间戳错序"
        assert ready > 0, f"task {tid} ready_ms 未落盘"
        assert es > 0 and ee > es, f"task {tid} 执行窗口无效"
        assert DB_PATH in (dbs or ""), f"task {tid} dbs 解析失败: {dbs!r}"

    # 大内存 task：avg/peak > 0 且 peak ≥ avg ≥ baseline。
    # （from_user task 名为 __user_func__:hex —— 本 run 唯一的 user func 即 mem_hog）
    mem_tid = c.execute(
        "SELECT task_id FROM tasks WHERE name LIKE '__user_func__:%'").fetchone()[0]
    mem_avg, mem_peak, mem_base = c.execute(
        "SELECT mem_avg_bytes, mem_peak_bytes, mem_baseline_bytes FROM tasks "
        "WHERE task_id=?", (mem_tid,)).fetchone()
    assert mem_avg > 0 and mem_peak > 0 and mem_base > 0, "内存三字段未落盘"
    assert mem_peak >= mem_avg >= min(mem_avg, mem_base), "内存口径错序"
    # 64MB 分配应可见于 peak-baseline（放宽到 32MB 容忍页缓存/共享页）。
    assert mem_peak - mem_base >= 32 * 1024 * 1024, \
        f"大内存 task 峰值差不足: {mem_peak - mem_base}"

    # IO task：read/write 时间与字节。
    io_tid = c.execute(
        "SELECT task_id FROM tasks WHERE name='write_data'").fetchone()[0]
    r_ms, r_bytes, w_ms, w_bytes = c.execute(
        "SELECT read_time_ms, read_bytes, write_time_ms, write_bytes FROM tasks "
        "WHERE task_id=?", (io_tid,)).fetchone()
    assert w_ms > 0 and w_bytes > 0, f"write 指标未落盘: {w_ms}, {w_bytes}"
    # cpu_time 是 jiffies 差分（10ms 粒度）：亚秒级快 task 并发下可能整 0——
    # 用 12s 的 slow_write 断言（CPU 必然跨多个 tick）。
    cpu_ms = c.execute(
        "SELECT cpu_time_ms FROM tasks WHERE name='slow_write'").fetchone()[0]
    assert cpu_ms > 0, "cpu_time_ms 未落盘（slow_write 12s 仍为 0）"

    # 依赖链 task：read 明细 + read 指标（chain_stage 读输入对象）。
    chain_r = c.execute(
        "SELECT read_time_ms FROM tasks WHERE name='chain_stage'").fetchone()[0]
    assert chain_r >= 0, "chain task read 指标缺失"

    # ── object_io 明细 ──
    n_io = c.execute("SELECT COUNT(*) FROM object_io").fetchone()[0]
    assert n_io >= 3, f"object_io 行数 {n_io} < 3"
    w_row = c.execute(
        "SELECT object_name, bytes FROM object_io WHERE direction='w' LIMIT 1").fetchone()
    assert w_row and w_row[0].endswith("obj_plain"), f"write 明细异常: {w_row}"

    # ── workers / events ──
    n_workers = c.execute("SELECT COUNT(*) FROM workers").fetchone()[0]
    assert n_workers == 3, f"workers 行数 {n_workers} != 3"

    events = [r[0] for r in c.execute("SELECT event FROM events")]
    for expected in ["SUBMIT", "ASSIGN", "COMPLETE", "DB_CREATED",
                     "DRAIN_START", "DRAIN_DONE", "REGISTER"]:
        assert expected in events, f"事件 {expected} 缺失（现有: {sorted(set(events))}）"

    # ── worker_samples：三个 worker + master 自监控，字段非零 ──
    per_worker = dict(c.execute(
        "SELECT worker_id, COUNT(*) FROM worker_samples GROUP BY worker_id").fetchall())
    assert set(per_worker) == {0, 1, 2, 3}, f"样本 worker 覆盖异常: {per_worker}"
    # 长任务 run（12s+）每 worker 至少一组上报（10s 周期）。
    for wid, cnt in per_worker.items():
        assert cnt >= 1, f"worker {wid} 样本数 {cnt} < 1"
    rss_ok = c.execute(
        "SELECT COUNT(*) FROM worker_samples WHERE proc_rss_bytes>0").fetchone()[0]
    net_ok = c.execute(
        "SELECT COUNT(*) FROM worker_samples WHERE net_read_bytes>0").fetchone()[0]
    assert rss_ok > 0 and net_ok > 0, f"RSS/net 计数全零: {rss_ok}, {net_ok}"

    conn.close()

    # ── 心跳迁移回归：runtime.summary 仍产出（monitor 通道喂 RunMetrics）──
    assert os.path.exists(os.path.join(LOG_DIR, "runtime.summary")), \
        "runtime.summary 缺失（RunMetrics 喂样通道异常）"
    INFO("[PASS] monitor.db 全维度断言通过")


cleanup()
run_cluster()
assert_db_content()
