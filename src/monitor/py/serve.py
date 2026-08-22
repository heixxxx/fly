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

# ---------------------------------------------------------------------------
# 只读 DB 连接
# ---------------------------------------------------------------------------

_conn_lock = threading.Lock()
_conn = None
_db_path = None


def open_db(path):
    """打开只读连接（进程内单连接串行复用；sqlite3 线程检查关闭）。"""
    global _conn, _db_path
    path = os.path.abspath(path)
    if not os.path.exists(path):
        raise FileNotFoundError(f"monitor.db not found: {path}")
    _db_path = path
    _conn = sqlite3.connect(f"file:{path}?mode=ro", uri=True, check_same_thread=False)
    _conn.row_factory = sqlite3.Row
    _conn.execute("PRAGMA busy_timeout=5000")
    return _conn


def query(sql, args=()):
    """执行只读查询。NFS/写锁冲突（BUSY）时短重试；其余异常向上抛。"""
    global _conn
    for attempt in range(3):
        try:
            with _conn_lock:
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


def resolve_db_path_arg(arg):
    """接受 db 文件路径或 log_dir（目录内找 monitor.db）。"""
    if os.path.isdir(arg):
        cand = os.path.join(arg, "monitor.db")
        if os.path.exists(cand):
            return cand
        raise FileNotFoundError(f"monitor.db not found in dir: {arg}")
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
        latest = query(
            "SELECT * FROM worker_samples WHERE worker_id=? ORDER BY epoch_ms DESC LIMIT 1",
            (r["worker_id"],))
        workers.append({
            "worker_id": r["worker_id"],
            "hostname": r["hostname"],
            "ip": r["ip"],
            "role": r["role"],
            "attributes": r["attributes"],
            "first_seen_ms": r["first_seen_ms"],
            "last_event_ms": r["last_event_ms"],
            "last_event": r["last_event"],
            "latest": dict(latest[0]) if latest else None,
        })
    return {"workers": workers}


def api_worker_samples(worker_id, from_ms=0, to_ms=0):
    sql = "SELECT * FROM worker_samples WHERE worker_id=?"
    args = [worker_id]
    if from_ms:
        sql += " AND epoch_ms>=?"
        args.append(from_ms)
    if to_ms:
        sql += " AND epoch_ms<=?"
        args.append(to_ms)
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
    sql = "SELECT * FROM events"
    args = []
    if category:
        sql += " WHERE category=?"
        args.append(category)
    sql += " ORDER BY id DESC LIMIT ?"
    args.append(int(limit))
    return {"events": [dict(r) for r in query(sql, args)]}


def api_timeline(from_ms=0, to_ms=0):
    """按 worker 分组的 task 执行窗口（Gantt 数据源）。"""
    sql = ("SELECT task_id, name, status, worker_id, "
           "exec_start_ms, exec_end_ms, started_ms, completed_ms FROM tasks "
           "WHERE exec_start_ms>0")
    args = []
    if from_ms:
        sql += " AND exec_start_ms>=?"
        args.append(from_ms)
    if to_ms:
        sql += " AND exec_start_ms<=?"
        args.append(to_ms)
    rows = [dict(r) for r in query(sql, args)]
    return {"tasks": rows}


def api_dbs():
    """db 生命周期 + 关联 task 统计（GUI 的 DBs 页数据源）。"""
    db_events = [dict(r) for r in query(
        "SELECT * FROM events WHERE category='db' ORDER BY id")]
    dbs = {}
    for e in db_events:
        d = dbs.setdefault(e["detail"], {"db": e["detail"], "events": []})
        d["events"].append(e)
    # tasks.dbs 反查关联（逗号分隔 LIKE 匹配）。
    for d in dbs.values():
        d["task_count"] = query(
            "SELECT COUNT(*) AS n FROM tasks WHERE dbs=? OR dbs LIKE ? OR dbs LIKE ? OR dbs LIKE ?",
            (d["db"], f"{d['db']},%", f"%,{d['db']}", f"%,{d['db']},%"))[0]["n"]
    return {"dbs": sorted(dbs.values(), key=lambda x: x["db"])}


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
                    int(qs.get("to_ms", ["0"])[0])))
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
                    int(qs.get("to_ms", ["0"])[0])))
            elif api == "dbs":
                self._send_json(api_dbs())
            else:
                self._send_json({"error": "unknown api"}, 404)
        except Exception as e:  # API 异常不杀服务（轮询端下一轮恢复）
            self._send_json({"error": str(e)}, 500)


def local_addresses():
    """本机全部 IPv4 地址（打印多入口 URL，分布式环境直接给可用地址）。"""
    addrs = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            addrs.add(info[4][0])
    except OSError:
        pass
    addrs.add("127.0.0.1")
    return sorted(addrs)


def serve(db_arg, port=8788):
    db_path = resolve_db_path_arg(db_arg)
    open_db(db_path)
    httpd = ThreadingHTTPServer(("0.0.0.0", port), MonitorHandler)
    addrs = local_addresses()
    urls = "  ".join(f"http://{a}:{port}/" for a in addrs)
    print(f"[fly-monitor] db: {db_path}", flush=True)
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
