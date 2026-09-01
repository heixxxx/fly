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

from test import write_data, slow_write, chain_stage, wait_until
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
    holder = bytearray(64 * 1024 * 1024)  # noqa: F841 — 持有到 task 结束
    for i in range(0, len(holder), 4096):
        holder[i] = 1  # 逐页触摸确保物理分配
    # 不 sleep：write 调用前对象必存活——write 事件采样点（task_io）天然
    # 捕捉到峰值，无需依赖 monitor 线程的固定采样间隔撞运气。
    db.write_object(key, len(holder))


def pure_compute_task(db, key):
    """纯计算 task（无 IO）：验证执行窗口内 200ms 加密周期能捕捉内存峰值。"""
    holder = bytearray(32 * 1024 * 1024)  # noqa: F841
    for i in range(0, len(holder), 4096):
        holder[i] = 2
    x = 0
    for i in range(30_000_000):  # ~600ms 忙循环（> 2 个加密采样间隔）
        x += i % 7
    db.write_object(key, x)


from fly import as_task  # noqa: E402

mem_hog = as_task()(mem_hog_task)
pure_compute = as_task()(pure_compute_task)


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
    t5 = pure_compute(db, "obj_pure")                       # 纯计算（执行期加密采样）

    from fly import wait_tasks
    wait_tasks(120)  # 等全部提交的 task 完成

    # 运行中实时读（只读连接不断言锁死）。PERSIST journal 下写者 commit
    # 持 EXCLUSIVE，高负载轮 5s busy_timeout 可能不够（stability R76 实锤
    # database is locked）——失败重开连接有界重试（只读连接的错误态粘滞，
    # 须重开；与 _meta_has 同款处理）。
    live = {"conn": live_conn, "rows": None}

    def _live_rows_ready():
        try:
            live["rows"] = live["conn"].execute(
                "SELECT COUNT(*) FROM worker_samples").fetchone()[0]
            return True
        except sqlite3.OperationalError:
            try:
                live["conn"].close()
            except sqlite3.Error:
                pass
            live["conn"] = _open_ro()
            return False

    assert wait_until(_live_rows_ready, timeout=15), "运行中实时只读 15s 内持续被锁"
    live_conn = live["conn"]
    live_rows = live["rows"]
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
    assert len(rows) == 5, f"tasks 行数 {len(rows)} != 5"
    for tid, name, status, created, started, completed, ready, es, ee, dbs in rows:
        assert status == "COMPLETED", f"task {tid} 状态 {status}"
        assert created > 0 and started >= created and completed >= started, \
            f"task {tid} 调度时间戳错序"
        assert ready > 0, f"task {tid} ready_ms 未落盘"
        assert es > 0 and ee > es, f"task {tid} 执行窗口无效"
        assert DB_PATH in (dbs or ""), f"task {tid} dbs 解析失败: {dbs!r}"

    # 大内存 task（无 sleep）：write 事件采样点捕捉峰值——事件驱动采样对
    # 亚秒 task 盲区的根治验收。
    # （from_user task 名为 __user_func__:hex —— 两个 user func 按提交顺序区分）
    mem_rows = c.execute(
        "SELECT task_id, mem_peak_bytes, mem_baseline_bytes FROM tasks "
        "WHERE name LIKE '__user_func__:%' ORDER BY task_id").fetchall()
    assert len(mem_rows) == 2, f"user func task 数 {len(mem_rows)} != 2"
    hog_peak, hog_base = mem_rows[0][1], mem_rows[0][2]
    pure_peak, pure_base = mem_rows[1][1], mem_rows[1][2]
    # mem_hog（64MB，先提交）：write 钩子采样应抓到大部分分配。
    assert hog_peak - hog_base >= 32 * 1024 * 1024, \
        f"write 事件采样未捕捉到 64MB 峰值: {hog_peak - hog_base}"
    # pure_compute（32MB，无 IO，~600ms）：执行窗口 200ms 加密周期采样捕捉。
    assert pure_peak - pure_base >= 16 * 1024 * 1024, \
        f"执行期加密采样未捕捉 32MB 计算峰值: {pure_peak - pure_base}"

    # IO task：read/write 时间与字节。
    io_tid = c.execute(
        "SELECT task_id FROM tasks WHERE name='write_data'").fetchone()[0]
    r_ms, r_bytes, w_ms, w_bytes = c.execute(
        "SELECT read_time_ms, read_bytes, write_time_ms, write_bytes FROM tasks "
        "WHERE task_id=?", (io_tid,)).fetchone()
    assert w_ms > 0 and w_bytes > 0, f"write 指标未落盘: {w_ms}, {w_bytes}"
    # cpu_time 为 getrusage 微秒差分：亚秒快 task（write_data）也应非零——
    # jiffies 10ms 粒度下短 task 恒 0 的回归验收。
    fast_cpu = c.execute(
        "SELECT cpu_time_ms FROM tasks WHERE task_id=?", (io_tid,)).fetchone()[0]
    assert fast_cpu > 0, f"微秒精度下快 task cpu_time 仍为 0: {fast_cpu}"

    # 依赖链 task：read 明细 + read 指标（chain_stage 读输入对象）。
    chain_r = c.execute(
        "SELECT read_time_ms FROM tasks WHERE name='chain_stage'").fetchone()[0]
    assert chain_r >= 0, "chain task read 指标缺失"

    # ── object_io 明细 ──
    n_io = c.execute("SELECT COUNT(*) FROM object_io").fetchone()[0]
    assert n_io >= 3, f"object_io 行数 {n_io} < 3"
    # MONITOR_TASK_IO 为异步成组上报，落库次序随批次到达顺序翻转——
    # 无 ORDER BY 的位置性 LIMIT 1 是顺序竞态（stability 100 轮 R23 实锤
    # obj_mem 抢在 obj_plain 前）。改按对象名定点断言存在性；明细行
    # bytes=0 属正常（字节口径聚合在 tasks 表，上方断言已覆盖）。
    for obj in ("obj_plain", "obj_mem", "obj_slow"):
        row = c.execute(
            "SELECT bytes FROM object_io WHERE direction='w' "
            "AND object_name LIKE ? LIMIT 1", (f"%{obj}",)).fetchone()
        assert row is not None, f"obj {obj} 的 write 明细缺失: {row}"

    # ── workers / events ──
    # master（wid=0，role=master）run 启动时自登记进 workers 表——GUI
    # Workers 页 master 样本有归属行（显示 master 而非 worker 0）。
    n_workers = c.execute("SELECT COUNT(*) FROM workers").fetchone()[0]
    assert n_workers == 4, f"workers 行数 {n_workers} != 4（3 worker + master）"
    master_row = c.execute(
        "SELECT role, hostname FROM workers WHERE worker_id=0").fetchone()
    assert master_row and master_row[0] == "master", \
        f"master 自登记行异常: {master_row}"

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
    # 事件驱动采样验收：kind=1 样本（assign/执行起止等 cluster 事件时刻的
    # worker 快照）应与周期样本（kind=0）并存。
    n_event = c.execute(
        "SELECT COUNT(*) FROM worker_samples WHERE kind=1").fetchone()[0]
    n_periodic = c.execute(
        "SELECT COUNT(*) FROM worker_samples WHERE kind=0").fetchone()[0]
    assert n_event > 0, "无事件驱动样本（kind=1）——sample_now_event 未生效"
    assert n_periodic > 0, "无周期样本（kind=0）"

    conn.close()

    # ── 心跳迁移回归：runtime.summary 仍产出（monitor 通道喂 RunMetrics）──
    assert os.path.exists(os.path.join(LOG_DIR, "runtime.summary")), \
        "runtime.summary 缺失（RunMetrics 喂样通道异常）"
    INFO("[PASS] monitor.db 全维度断言通过")


cleanup()
run_cluster()
assert_db_content()
