"""cluster monitor Web GUI 服务（stdlib-only，只读 monitor.db）。

职责：JSON API + 静态前端托管。页面渲染/交互/图表全部在前端 JS
（static/，vendor ECharts），Python 侧不做任何模板渲染。

只读安全：mode=ro URI + busy_timeout=5s + BUSY 重试，绝不写库——
可安全附加在正在运行的 fly master 上（单写者不被打扰），也可在
run 结束后独立浏览历史。

启动方式：
  · python3 src/monitor/py/serve.py <db路径|log_dir> [--port 8788]
    （仅标准库依赖，任何 python3 >= 3.8 可直接运行）
  · fly --serve-monitor <db> [--port N]（复用 fly 二进制的嵌入式解释器）
  · fly.launch_monitor_gui()（detached spawn 上述 fly 入口）

NFS 注意：monitor.db 使用 rollback journal（PERSIST），跨机只读 +
master 单写是 SQLite 的安全模式；BUSY 时读请求自动重试。
"""
import argparse
import json
import os
import posixpath
import socket
import sqlite3
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

_STATIC_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "static")

# db 不存在时的等待上限（run 刚启动时首条上报落盘前的窗口）。
DB_WAIT_TIMEOUT_S = 60

# ---------------------------------------------------------------------------
# 只读 DB 连接
# ---------------------------------------------------------------------------

_conn_lock = threading.Lock()
_conn = None
_db_path = None
_db_inode = None


def open_db(path):
    """打开只读连接（进程内单连接串行复用；sqlite3 线程检查关闭）。

    记录文件 inode：同路径被新 run 替换（rm + 重建）时自动重开——否则
    连接握着已删除文件的旧 inode，页面永远显示旧数据（demo 实测踩坑）。
    """
    global _conn, _db_path, _db_inode
    path = os.path.abspath(path)
    if not os.path.exists(path):
        raise FileNotFoundError(f"monitor.db not found: {path}")
    _db_path = path
    _db_inode = os.stat(path).st_ino
    _conn = sqlite3.connect(f"file:{path}?mode=ro", uri=True, check_same_thread=False)
    _conn.row_factory = sqlite3.Row
    _conn.execute("PRAGMA busy_timeout=5000")
    return _conn


def _reopen_if_replaced():
    """db 文件被同路径替换（新 run）时重开连接。每次 query 前调用（一次
    stat，µs 级）。文件被删且未重建时保持旧连接（旧数据仍可看，重建后
    inode 变化触发切换）。"""
    global _conn, _db_inode
    try:
        inode = os.stat(_db_path).st_ino
    except OSError:
        return
    if inode == _db_inode:
        return
    try:
        _conn.close()
    except Exception:
        pass
    _conn = sqlite3.connect(f"file:{_db_path}?mode=ro", uri=True,
                            check_same_thread=False)
    _conn.row_factory = sqlite3.Row
    _conn.execute("PRAGMA busy_timeout=5000")
    _db_inode = inode


def query(sql, args=()):
    """执行只读查询。NFS/写锁冲突（BUSY）时短重试；其余异常向上抛。"""
    global _conn
    for attempt in range(3):
        try:
            with _conn_lock:
                _reopen_if_replaced()
                cur = _conn.execute(sql, args)
                rows = cur.fetchall()
            return rows
        except sqlite3.OperationalError as e:
            if "locked" in str(e) or "busy" in str(e):
                if attempt == 2:
                    return []  # 读监控数据最终让路（下一轮轮询补上）
                time.sleep(0.3)
                # 连接可能失效（journal 回收窗口），重开一次。
                try:
                    _conn.close()
                except Exception:
                    pass
                _conn = sqlite3.connect(f"file:{_db_path}?mode=ro", uri=True,
                                        check_same_thread=False)
                _conn.row_factory = sqlite3.Row
                _conn.execute("PRAGMA busy_timeout=5000")
                continue
            raise
    return []


def wait_for_db(path, timeout_s=DB_WAIT_TIMEOUT_S):
    """run 刚启动时 monitor.db 可能尚未生成（首条上报落盘前）——等待而非
    立即报错，覆盖「起 run 后立刻起 GUI」的典型用法。"""
    deadline = time.time() + timeout_s
    while not os.path.exists(path):
        if time.time() >= deadline:
            raise FileNotFoundError(
                f"monitor.db 未在 {timeout_s}s 内出现: {path}（run 是否用了 "
                "monitor_db_enabled=0？或尚未产出首条上报）")
        print(f"[fly-monitor] 等待 {path} 生成（run 进行中，每秒检查）…", flush=True)
        time.sleep(1.0)


def resolve_db_path_arg(arg):
    """接受 db 文件路径或 log_dir（目录内找 monitor.db）。"""
    if os.path.isdir(arg):
        cand = os.path.join(arg, "monitor.db")
        if os.path.exists(cand):
            return cand
        return cand  # 不存在也返回——交给 wait_for_db 等待生成
    return arg


# ---------------------------------------------------------------------------
# API 实现（全部返回可 JSON 序列化的 dict/list）
# ---------------------------------------------------------------------------

def api_meta():
    meta = {r["key"]: r["value"] for r in query("SELECT key, value FROM meta")}
    task_counts = {r["status"]: r["n"] for r in query(
        "SELECT status, COUNT(*) AS n FROM tasks GROUP BY status")}
    worker_cnt = query("SELECT COUNT(*) AS n FROM workers")[0]["n"]
    sample_range = query(
        "SELECT MIN(epoch_ms) AS lo, MAX(epoch_ms) AS hi FROM worker_samples")[0]
    return {
        "db_path": _db_path,
        "meta": meta,
        "task_counts": task_counts,
        "workers": worker_cnt,
        "sample_lo": sample_range["lo"] or 0,
        "sample_hi": sample_range["hi"] or 0,
    }


def api_workers():
    workers = []
    for r in query("SELECT * FROM workers ORDER BY worker_id"):
        wid = r["worker_id"]
        latest = query(
            "SELECT * FROM worker_samples WHERE worker_id=? ORDER BY epoch_ms DESC LIMIT 1",
            (wid,))
        # 推导终态语义：DEAD 前若有关停指令（SHUTDOWN_SENT/STOP_NOW_SENT）
        # 则为正常退出（EXITED，绿），否则为异常死亡（DEAD，红——心跳超时/
        # 宽限耗尽判死）。旧数据无指令事件时保守显示 DEAD。
        exit_kind = None
        if r["last_event"] == "DEAD":
            cmd = query(
                "SELECT 1 FROM events WHERE worker_id=? AND event IN "
                "('SHUTDOWN_SENT','STOP_NOW_SENT') AND epoch_ms<=? LIMIT 1",
                (wid, r["last_event_ms"]))
            exit_kind = "EXITED" if cmd else "DEAD"
        workers.append({
            "worker_id": wid,
            "hostname": r["hostname"],
            "ip": r["ip"],
            "role": r["role"],
            "attributes": r["attributes"],
            "first_seen_ms": r["first_seen_ms"],
            "last_event_ms": r["last_event_ms"],
            "last_event": r["last_event"],
            "exit_kind": exit_kind,
            "latest": dict(latest[0]) if latest else None,
        })
    return {"workers": workers}


def api_worker_samples(worker_id, from_ms=0, to_ms=0, after_ms=0):
    """after_ms 为增量游标（只返回 epoch_ms 严格大于它的样本）——前端
    增量刷新：首轮拉全量，之后每轮只传新增，传输量从 O(总样本) 降为
    O(新增)。(worker_id, epoch_ms) 主键保证不重不漏。"""
    sql = "SELECT * FROM worker_samples WHERE worker_id=?"
    args = [worker_id]
    if from_ms:
        sql += " AND epoch_ms>=?"
        args.append(from_ms)
    if to_ms:
        sql += " AND epoch_ms<=?"
        args.append(to_ms)
    if after_ms:
        sql += " AND epoch_ms>?"
        args.append(after_ms)
    sql += " ORDER BY epoch_ms"
    rows = [dict(r) for r in query(sql, args)]
    return {"worker_id": worker_id, "samples": rows}


def api_tasks(worker=0, status="", q="", limit=200, offset=0):
    where, args = "1=1", []
    if worker:
        where += " AND worker_id=?"
        args.append(worker)
    if status:
        where += " AND status=?"
        args.append(status)
    if q:
        where += " AND name LIKE ?"
        args.append(f"%{q}%")
    total = query(f"SELECT COUNT(*) AS n FROM tasks WHERE {where}", args)[0]["n"]
    rows = [dict(r) for r in query(
        f"SELECT * FROM tasks WHERE {where} ORDER BY task_id DESC LIMIT ? OFFSET ?",
        args + [int(limit), int(offset)])]
    return {"total": total, "tasks": rows}


def api_task_detail(task_id):
    rows = query("SELECT * FROM tasks WHERE task_id=?", (task_id,))
    if not rows:
        return {"error": "not found"}
    events = [dict(r) for r in query(
        "SELECT * FROM events WHERE task_id=? ORDER BY id", (task_id,))]
    io = [dict(r) for r in query(
        "SELECT * FROM object_io WHERE task_id=? ORDER BY id LIMIT 500", (task_id,))]
    return {"task": dict(rows[0]), "events": events, "io": io}


def api_events(category="", limit=100):
    """用户事件流：过滤实现细节事件（DB_DU 是 DBs 页磁盘数据的来源通道，
    非用户语义）；worker DEAD 附带 exit_kind 推导（关停指令先行 = 正常
    退出，否则异常死亡），前端据此显示绿色/红色。"""
    sql = "SELECT * FROM events WHERE event!='DB_DU'"
    args = []
    if category:
        sql += " AND category=?"
        args.append(category)
    sql += " ORDER BY id DESC LIMIT ?"
    args.append(int(limit))
    events = []
    for r in query(sql, args):
        e = dict(r)
        if e["category"] == "worker" and e["event"] == "DEAD":
            cmd = query(
                "SELECT 1 FROM events WHERE worker_id=? AND event IN "
                "('SHUTDOWN_SENT','STOP_NOW_SENT') AND epoch_ms<=? LIMIT 1",
                (e["worker_id"], e["epoch_ms"]))
            e["exit_kind"] = "EXITED" if cmd else "DEAD"
        events.append(e)
    return {"events": events}


def api_timeline(from_ms=0, to_ms=0, changed_since_ms=0):
    """按 worker 分组的 task 执行窗口（Gantt 数据源）。含负载分类字段
    （cpu/io 时间、排队等待 ready→started），前端按 CPU/IO/Wait/Queue 分色。

    changed_since_ms 为增量游标（增强刷新）：只返回「执行开始晚于游标」的
    新 task 或「完成时刻晚于游标」的窗口更新（RUNNING task 结束时
    exec_end/status 更新），前端按 task_id merge——传输量从 O(全部 task)
    降为 O(新增+刚完成)。"""
    sql = ("SELECT task_id, name, status, worker_id, created_ms, "
           "exec_start_ms, exec_end_ms, started_ms, completed_ms, "
           "cpu_time_ms, read_time_ms, write_time_ms, ready_ms FROM tasks "
           "WHERE exec_start_ms>0")
    args = []
    if changed_since_ms:
        sql += " AND (exec_start_ms>? OR completed_ms>?)"
        args.extend([changed_since_ms, changed_since_ms])
    else:
        if from_ms:
            sql += " AND exec_start_ms>=?"
            args.append(from_ms)
        if to_ms:
            sql += " AND exec_start_ms<=?"
            args.append(to_ms)
    rows = [dict(r) for r in query(sql, args)]
    return {"tasks": rows}


def api_dbs():
    """DBs 页数据源（用户裁定简化口径）：db 列表 + 创建时间 + 冻结时间 +
    磁盘占用。磁盘来自 master 落的 DB_DU 事件（freeze 终值 / stop 收尾补测；
    detail 编码 "<path>|<bytes>"，-1=未测得）。"""
    created, frozen, disk = {}, {}, {}
    for r in query("SELECT epoch_ms, event, detail FROM events "
                   "WHERE category='db' ORDER BY id"):
        if r["event"] == "DB_CREATED":
            created.setdefault(r["detail"], r["epoch_ms"])
        elif r["event"] == "DB_FROZEN":
            frozen.setdefault(r["detail"], r["epoch_ms"])
        elif r["event"] == "DB_DU":
            path, _, b = r["detail"].rpartition("|")
            try:
                disk[path] = int(b)
            except ValueError:
                pass
    dbs = [{"db": d, "created_ms": created[d], "frozen_ms": frozen.get(d),
            "disk_bytes": disk.get(d)} for d in sorted(created)]
    return {"dbs": dbs}


# ---------------------------------------------------------------------------
# HTTP 服务
# ---------------------------------------------------------------------------

class MonitorHandler(BaseHTTPRequestHandler):
    server_version = "fly-monitor/1.0"

    def log_message(self, fmt, *args):  # 安静模式（轮询日志刷屏无价值）
        pass

    def _send_json(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _send_bytes(self, body, ctype):
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        # 静态资源禁缓存：迭代频繁且体量小（≤百 KB），启发式缓存的旧
        # JS/CSS 混搭新版会导致页面行为错乱（如 CSS 变量缺失→图表空色）。
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _serve_static(self, rel):
        rel = posixpath.normpath(rel).lstrip("/")
        path = os.path.join(_STATIC_ROOT, rel)
        # 防路径穿越：规范化后必须仍在 static 根下。
        if not os.path.abspath(path).startswith(os.path.abspath(_STATIC_ROOT) + os.sep):
            self._send_json({"error": "forbidden"}, 403)
            return
        if not os.path.isfile(path):
            self._send_json({"error": "not found"}, 404)
            return
        ctype = "application/octet-stream"
        if path.endswith(".html"):
            ctype = "text/html; charset=utf-8"
        elif path.endswith(".js"):
            ctype = "application/javascript; charset=utf-8"
        elif path.endswith(".css"):
            ctype = "text/css; charset=utf-8"
        elif path.endswith(".svg"):
            ctype = "image/svg+xml"
        with open(path, "rb") as f:
            self._send_bytes(f.read(), ctype)

    def do_GET(self):
        parsed = urlparse(self.path)
        parts = [p for p in parsed.path.split("/") if p]
        qs = parse_qs(parsed.query)

        try:
            if not parts or parts[0] != "api":
                # 静态：/ → index.html；/static/xxx → 文件。
                if not parts:
                    self._serve_static("index.html")
                elif parts[0] == "static":
                    self._serve_static("/".join(parts[1:]))
                else:
                    self._serve_static("index.html")
                return

            api = parts[1] if len(parts) > 1 else ""
            if api == "workers" and len(parts) == 2:
                self._send_json(api_workers())
            elif api == "workers" and len(parts) == 4 and parts[3] == "samples":
                self._send_json(api_worker_samples(
                    int(parts[2]),
                    int(qs.get("from_ms", ["0"])[0]),
                    int(qs.get("to_ms", ["0"])[0]),
                    int(qs.get("after_ms", ["0"])[0])))
            elif api == "meta":
                self._send_json(api_meta())
            elif api == "tasks" and len(parts) == 3:
                self._send_json(api_task_detail(int(parts[2])))
            elif api == "tasks":
                self._send_json(api_tasks(
                    worker=int(qs.get("worker", ["0"])[0]),
                    status=qs.get("status", [""])[0],
                    q=qs.get("q", [""])[0],
                    limit=qs.get("limit", ["200"])[0],
                    offset=qs.get("offset", ["0"])[0]))
            elif api == "events":
                self._send_json(api_events(
                    category=qs.get("category", [""])[0],
                    limit=qs.get("limit", ["100"])[0]))
            elif api == "timeline":
                self._send_json(api_timeline(
                    int(qs.get("from_ms", ["0"])[0]),
                    int(qs.get("to_ms", ["0"])[0]),
                    int(qs.get("changed_since_ms", ["0"])[0])))
            elif api == "dbs":
                self._send_json(api_dbs())
            else:
                self._send_json({"error": "unknown api"}, 404)
        except Exception as e:  # API 异常不杀服务（轮询端下一轮恢复）
            self._send_json({"error": str(e)}, 500)


def local_addresses():
    """本机全部 IPv4 地址（打印多入口 URL）。WSL/NAT 环境下 getaddrinfo(
    hostname) 只返回回环地址——用 UDP connect 的路由决策拿真实出口 IP
    （不发实际包，不依赖外网连通），Windows 宿主浏览器才能拿到可用地址。"""
    addrs = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            addrs.add(info[4][0])
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.connect(("8.8.8.8", 80))  # 仅路由决策，无发包
            addrs.add(s.getsockname()[0])
        finally:
            s.close()
    except OSError:
        pass
    addrs.add("127.0.0.1")
    # 回环排前（本机优先），外网/内网出口 IP 其后（跨机访问用）。
    def sort_key(a):
        return (0 if a.startswith("127.") else 1, a)
    return sorted(addrs, key=sort_key)


def _latest_hint(db_path):
    """若传入的是旧 run 的轮转目录（fly_log / fly_log.N）且旁边存在指向
    更新 run 的 fly_log.latest 软链，返回 latest 路径（供打印引导）。"""
    d = os.path.dirname(db_path)
    base = os.path.basename(d)
    stem, _, suffix = base.rpartition('.')
    if not stem or not suffix.isdigit():
        stem = base
    latest = os.path.join(os.path.dirname(d), stem + '.latest')
    if os.path.islink(latest):
        try:
            if os.path.realpath(d) != os.path.realpath(latest):
                return latest
        except OSError:
            pass
    return None


def serve(db_arg, port=8788):
    db_path = os.path.abspath(resolve_db_path_arg(db_arg))
    wait_for_db(db_path)
    open_db(db_path)
    try:
        httpd = ThreadingHTTPServer(("0.0.0.0", port), MonitorHandler)
    except OSError as e:
        if "in use" in str(e) or "Address already" in str(e):
            # 端口被占用：大概率是已有 GUI 实例在跑——友好退出而非裸栈崩溃。
            print(f"[fly-monitor] 端口 {port} 已被占用——可能已有一个 GUI 实例")
            print(f"[fly-monitor] 在运行。直接在浏览器打开 http://<本机地址>:{port}/ ，")
            print(f"[fly-monitor] 或用 --port 换一个端口。查找残留进程：")
            print(f"[fly-monitor]   ps aux | grep serve-monitor")
            sys.exit(2)
        raise
    addrs = local_addresses()
    urls = "  ".join(f"http://{a}:{port}/" for a in addrs)
    print(f"[fly-monitor] db: {db_path}", flush=True)
    # monitor.db 随 run 轮转目录隔离（fly_log.N）；传入旧目录时引导 latest
    # 软链（指向最新 run，且经 inode 重连在新 run 落盘后自动跟随）。
    latest = _latest_hint(db_path)
    if latest:
        print(f"[fly-monitor] 提示：当前指向旧 run；最新 run 在 {latest}", flush=True)
        print(f"[fly-monitor]   用 --serve-monitor {latest} 可查看最新 run（新 run 落盘后自动跟随）",
              flush=True)
    print(f"[fly-monitor] serving on port {port} — open in browser:", flush=True)
    print(f"[fly-monitor]   {urls}", flush=True)
    httpd.serve_forever()


def launch_monitor_gui(db_path, port=8788):
    """在 fly 进程内便捷启动独立 GUI（detached 子进程，不阻塞调用方）。

    优先复用当前 fly 二进制（sys._fly_binary，嵌入式解释器内由 C++ 注入）
    走 --serve-monitor 分支；无 fly 上下文时退回当前 Python 解释器直接
    运行 serve.py（stdlib-only 同样可跑）。返回 Popen 句柄（调用方可不管）。
    """
    import subprocess
    import sys
    db_path = resolve_db_path_arg(db_path)
    fly_bin = getattr(sys, "_fly_binary", None)
    if fly_bin and os.path.exists(fly_bin):
        cmd = [fly_bin, "--serve-monitor", db_path, "--port", str(port)]
    else:
        cmd = [sys.executable, os.path.abspath(__file__), db_path, "--port", str(port)]
    proc = subprocess.Popen(
        cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        start_new_session=True)  # detached：fly 主进程退出不带走 GUI
    addrs = local_addresses()
    print(f"[fly-monitor] GUI launched (pid={proc.pid}) — open in browser:")
    for a in addrs:
        print(f"[fly-monitor]   http://{a}:{port}/")
    return proc


def main(argv=None):
    ap = argparse.ArgumentParser(description="fly cluster monitor GUI (read-only)")
    ap.add_argument("db", help="monitor.db path or log_dir containing it")
    ap.add_argument("--port", type=int, default=8788)
    ns = ap.parse_args(argv)
    serve(ns.db, ns.port)


if __name__ == "__main__":
    main()
