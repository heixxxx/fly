"""serve.py API 单测：临时 monitor.db + 线程内起 HTTP server，断言各端点 JSON
与静态文件托管。stdlib-only（与 serve.py 同约束）。
"""
import json
import os
import shutil
import sqlite3
import sys
import tempfile
import threading
import time
import unittest
import urllib.request

_this_dir = os.path.dirname(os.path.abspath(__file__))
_project_root = os.path.normpath(os.path.join(_this_dir, '..', '..', '..'))
sys.path.insert(0, os.path.join(_project_root, 'src', 'monitor', 'py'))

import serve  # noqa: E402


def build_test_db(path):
    db = sqlite3.connect(path)
    c = db.cursor()
    c.executescript("""
    CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);
    CREATE TABLE workers(worker_id INTEGER PRIMARY KEY, hostname TEXT, ip TEXT,
        role TEXT, attributes TEXT, first_seen_ms INTEGER, last_event_ms INTEGER, last_event TEXT);
    CREATE TABLE worker_samples(worker_id INTEGER, epoch_ms INTEGER,
        proc_rss_bytes INTEGER, proc_cpu_bps INTEGER, host_cpu_bps INTEGER,
        host_mem_total_bytes INTEGER, host_mem_avail_bytes INTEGER,
        host_load1_x100 INTEGER, net_read_bytes INTEGER, net_write_bytes INTEGER,
        PRIMARY KEY(worker_id, epoch_ms)) WITHOUT ROWID;
    CREATE TABLE tasks(task_id INTEGER PRIMARY KEY, name TEXT, module TEXT,
        is_internal INTEGER, status TEXT, worker_id INTEGER, priority INTEGER,
        error TEXT, created_ms INTEGER, ready_ms INTEGER, started_ms INTEGER,
        completed_ms INTEGER, exec_start_ms INTEGER, exec_end_ms INTEGER,
        cpu_time_ms INTEGER, read_time_ms INTEGER, write_time_ms INTEGER,
        read_bytes INTEGER, write_bytes INTEGER, mem_baseline_bytes INTEGER,
        mem_avg_bytes INTEGER, mem_peak_bytes INTEGER, dbs TEXT);
    CREATE TABLE object_io(id INTEGER PRIMARY KEY AUTOINCREMENT, epoch_ms INTEGER,
        task_id INTEGER, worker_id INTEGER, direction TEXT, object_name TEXT,
        bytes INTEGER, duration_ms INTEGER);
    CREATE TABLE events(id INTEGER PRIMARY KEY AUTOINCREMENT, epoch_ms INTEGER,
        category TEXT, event TEXT, worker_id INTEGER, task_id INTEGER, detail TEXT);
    """)
    c.execute("INSERT INTO meta VALUES('run_start_ms','1000')")
    c.execute("INSERT INTO meta VALUES('hostname','test-host')")
    c.execute("INSERT INTO workers VALUES(1,'h1','10.0.0.1','hybrid','',100,200,'REGISTER')")
    c.execute("INSERT INTO worker_samples VALUES(1,1500,1000,2500,5000,64000,32000,100,0,0)")
    c.execute("INSERT INTO worker_samples VALUES(1,2500,1200,3000,5200,64000,31000,110,500,300)")
    c.execute("INSERT INTO tasks(task_id,name,status,worker_id,dbs,exec_start_ms,exec_end_ms,cpu_time_ms) "
              "VALUES(7,'my_task','COMPLETED',1,'/tmp/a.db',1100,1900,400)")
    c.execute("INSERT INTO tasks(task_id,name,status,worker_id) VALUES(8,'other','FAILED',1)")
    c.execute("INSERT INTO events(epoch_ms,category,event,worker_id,task_id,detail) "
              "VALUES(300,'task','SUBMIT',0,7,'my_task')")
    c.execute("INSERT INTO events(epoch_ms,category,event,worker_id,task_id,detail) "
              "VALUES(400,'db','DB_CREATED',0,0,'/tmp/a.db')")
    c.execute("INSERT INTO object_io(epoch_ms,task_id,worker_id,direction,object_name,bytes,duration_ms) "
              "VALUES(1200,7,1,'r','/tmp/a.db:x',4096,12)")
    db.commit()
    db.close()


class ServeTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.mkdtemp(prefix="fly_serve_test_")
        cls.db_path = os.path.join(cls.dir, "monitor.db")
        build_test_db(cls.db_path)
        serve.open_db(cls.db_path)
        cls.httpd = serve.ThreadingHTTPServer(("127.0.0.1", 0), serve.MonitorHandler)
        cls.port = cls.httpd.server_address[1]
        cls.thread = threading.Thread(target=cls.httpd.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        shutil.rmtree(cls.dir, ignore_errors=True)

    def get(self, path):
        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}{path}", timeout=5) as r:
            return json.loads(r.read().decode())

    def test_meta(self):
        m = self.get("/api/meta")
        self.assertEqual(m["meta"]["hostname"], "test-host")
        self.assertEqual(m["task_counts"], {"COMPLETED": 1, "FAILED": 1})
        self.assertEqual(m["workers"], 1)
        self.assertEqual(m["sample_lo"], 1500)

    def test_workers_with_latest_sample(self):
        w = self.get("/api/workers")["workers"]
        self.assertEqual(len(w), 1)
        self.assertEqual(w[0]["hostname"], "h1")
        self.assertEqual(w[0]["latest"]["epoch_ms"], 2500)

    def test_worker_samples_range(self):
        s = self.get("/api/workers/1/samples?from_ms=2000")
        self.assertEqual(len(s["samples"]), 1)
        self.assertEqual(s["samples"][0]["net_read_bytes"], 500)

    def test_worker_samples_incremental_cursor(self):
        # after_ms 增量游标：只返回严格大于游标的样本（前端增量刷新通道）。
        first = self.get("/api/workers/1/samples")
        self.assertEqual(len(first["samples"]), 2)
        cursor = first["samples"][0]["epoch_ms"]
        inc = self.get(f"/api/workers/1/samples?after_ms={cursor}")
        self.assertEqual(len(inc["samples"]), 1)
        self.assertEqual(inc["samples"][0]["epoch_ms"], 2500)
        # 游标超过最大值 → 空结果。
        done = self.get("/api/workers/1/samples?after_ms=2500")
        self.assertEqual(done["samples"], [])

    def test_tasks_filter_and_search(self):
        all_t = self.get("/api/tasks")
        self.assertEqual(all_t["total"], 2)
        failed = self.get("/api/tasks?status=FAILED")
        self.assertEqual(failed["total"], 1)
        self.assertEqual(failed["tasks"][0]["name"], "other")
        searched = self.get("/api/tasks?q=my_")
        self.assertEqual(searched["total"], 1)
        by_worker = self.get("/api/tasks?worker=1")
        self.assertEqual(by_worker["total"], 2)

    def test_task_detail(self):
        d = self.get("/api/tasks/7")
        self.assertEqual(d["task"]["dbs"], "/tmp/a.db")
        self.assertEqual(len(d["events"]), 1)
        self.assertEqual(len(d["io"]), 1)
        self.assertEqual(d["io"][0]["object_name"], "/tmp/a.db:x")

    def test_tasks_sort_whitelist(self):
        """排序参数：白名单键生效、非法键回落 task_id DESC、desc 参数控制
        升降序。插入行 exec_start_ms=0——不进 timeline（字母序在后的
        test_timeline 断言不受本用例污染）。"""
        db = sqlite3.connect(self.db_path)
        db.execute("INSERT OR REPLACE INTO tasks(task_id, name, status, worker_id, "
                   "cpu_time_ms) VALUES "
                   "(90, 's_lo', 'COMPLETED', 1, 100), "
                   "(91, 's_hi', 'COMPLETED', 1, 900), "
                   "(92, 's_mid', 'COMPLETED', 1, 500)")
        db.commit()
        db.close()
        asc = [t["task_id"] for t in self.get("/api/tasks?order=cpu&desc=0")["tasks"]]
        self.assertLess(asc.index(90), asc.index(92))
        self.assertLess(asc.index(92), asc.index(91))
        desc = [t["task_id"] for t in self.get("/api/tasks?order=cpu&desc=1")["tasks"]]
        self.assertLess(desc.index(91), desc.index(92))
        self.assertLess(desc.index(92), desc.index(90))
        # 非法键回落 task_id DESC（默认序）。
        fallback = [t["task_id"] for t in self.get("/api/tasks?order=--drop")["tasks"]]
        self.assertEqual(fallback, sorted(fallback, reverse=True))

    def test_events_category(self):
        evs = self.get("/api/events?category=db")["events"]
        self.assertEqual(len(evs), 1)
        self.assertEqual(evs[0]["event"], "DB_CREATED")

    def test_timeline(self):
        tl = self.get("/api/timeline")["tasks"]
        self.assertEqual(len(tl), 1)  # task 8 无 exec 窗口不进 timeline
        self.assertEqual(tl[0]["task_id"], 7)

    def test_dbs_simple_view(self):
        dbs = self.get("/api/dbs")["dbs"]
        self.assertEqual(len(dbs), 1)
        self.assertEqual(dbs[0]["db"], "/tmp/a.db")
        self.assertEqual(dbs[0]["created_ms"], 400)
        # DB_FROZEN + DB_DU 由新 run 数据补充后断言（本库构造里没有
        # freeze/du 事件——覆盖各字段的缺省形态）。
        self.assertIsNone(dbs[0]["frozen_ms"])
        self.assertIsNone(dbs[0]["disk_bytes"])
        self.assertNotIn("events", dbs[0])       # 简化口径：不带事件明细
        self.assertNotIn("task_count", dbs[0])   # 简化口径：不带关联统计

    def test_dbs_with_du_event(self):
        # DB_FROZEN/DB_DU 事件的解析（freeze 终值 + stop 补测覆盖）。
        # serve 的查询连接是 mode=ro——用独立读写连接注入事件。
        w = sqlite3.connect(self.db_path)
        w.execute(
            "INSERT INTO events(epoch_ms,category,event,worker_id,task_id,detail) "
            "VALUES(500,'db','DB_FROZEN',0,0,'/tmp/a.db')")
        w.execute(
            "INSERT INTO events(epoch_ms,category,event,worker_id,task_id,detail) "
            "VALUES(600,'db','DB_DU',0,0,'/tmp/a.db|1073741824')")
        w.execute(
            "INSERT INTO events(epoch_ms,category,event,worker_id,task_id,detail) "
            "VALUES(700,'db','DB_DU',0,0,'/tmp/a.db|2147483648')")
        w.commit()
        w.close()
        dbs = self.get("/api/dbs")["dbs"]
        self.assertEqual(dbs[0]["frozen_ms"], 500)
        self.assertEqual(dbs[0]["disk_bytes"], 2147483648)  # 取最新 DB_DU
        # 清理注入（测试库与本类其余用例共享，防串扰）。
        w = sqlite3.connect(self.db_path)
        w.execute("DELETE FROM events WHERE epoch_ms>=500 AND category='db'")
        w.commit()
        w.close()

    def test_static_index_and_vendor(self):
        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}/", timeout=5) as r:
            body = r.read().decode()
            self.assertIn("cluster monitor", body)
            self.assertIn("text/html", r.headers["Content-Type"])
        with urllib.request.urlopen(
                f"http://127.0.0.1:{self.port}/static/vendor/echarts.min.js", timeout=5) as r:
            self.assertGreater(len(r.read()), 100000)

    def test_path_traversal_blocked(self):
        try:
            urllib.request.urlopen(
                f"http://127.0.0.1:{self.port}/static/../serve.py", timeout=5)
            self.fail("path traversal should be rejected")
        except urllib.error.HTTPError as e:
            self.assertIn(e.code, (403, 404))

    def test_unknown_api_404(self):
        try:
            urllib.request.urlopen(f"http://127.0.0.1:{self.port}/api/nope", timeout=5)
            self.fail("unknown api should 404")
        except urllib.error.HTTPError as e:
            self.assertEqual(e.code, 404)


class ServeRobustnessTest(unittest.TestCase):
    """db 替换重连 / 端口占用 / db 等待的独立场景（不起 HTTP，直测函数）。"""

    def test_query_reopens_when_db_replaced(self):
        # 同路径 rm+重建（新 run 的典型形态）→ query 自动切到新 inode。
        d = tempfile.mkdtemp(prefix="fly_serve_reopen_")
        try:
            p = os.path.join(d, "monitor.db")
            build_test_db(p)
            serve.open_db(p)
            rows = serve.query("SELECT COUNT(*) AS n FROM tasks")
            self.assertEqual(rows[0]["n"], 2)
            # 新 run 的库：只含 1 个 task（hostname 可区分）。
            os.remove(p)
            db2 = sqlite3.connect(p)
            db2.executescript(
                "CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);"
                "CREATE TABLE workers(worker_id INTEGER PRIMARY KEY, hostname TEXT, ip TEXT,"
                " role TEXT, attributes TEXT, first_seen_ms INTEGER, last_event_ms INTEGER,"
                " last_event TEXT);"
                "CREATE TABLE worker_samples(worker_id INTEGER, epoch_ms INTEGER,"
                " proc_rss_bytes INTEGER, proc_cpu_bps INTEGER, host_cpu_bps INTEGER,"
                " host_mem_total_bytes INTEGER, host_mem_avail_bytes INTEGER,"
                " host_load1_x100 INTEGER, net_read_bytes INTEGER, net_write_bytes INTEGER,"
                " kind INTEGER, PRIMARY KEY(worker_id, epoch_ms)) WITHOUT ROWID;"
                "CREATE TABLE tasks(task_id INTEGER PRIMARY KEY, name TEXT, module TEXT,"
                " is_internal INTEGER, status TEXT, worker_id INTEGER, priority INTEGER,"
                " error TEXT, created_ms INTEGER, ready_ms INTEGER, started_ms INTEGER,"
                " completed_ms INTEGER, exec_start_ms INTEGER, exec_end_ms INTEGER,"
                " cpu_time_ms INTEGER, read_time_ms INTEGER, write_time_ms INTEGER,"
                " read_bytes INTEGER, write_bytes INTEGER, mem_baseline_bytes INTEGER,"
                " mem_avg_bytes INTEGER, mem_peak_bytes INTEGER, dbs TEXT);"
                "CREATE TABLE object_io(id INTEGER PRIMARY KEY AUTOINCREMENT, epoch_ms INTEGER,"
                " task_id INTEGER, worker_id INTEGER, direction TEXT, object_name TEXT,"
                " bytes INTEGER, duration_ms INTEGER);"
                "CREATE TABLE events(id INTEGER PRIMARY KEY AUTOINCREMENT, epoch_ms INTEGER,"
                " category TEXT, event TEXT, worker_id INTEGER, task_id INTEGER, detail TEXT);"
                "INSERT INTO tasks(task_id,name,status) VALUES(1,'new_run_task','RUNNING');")
            db2.commit()
            db2.close()
            rows = serve.query("SELECT COUNT(*) AS n FROM tasks")
            self.assertEqual(rows[0]["n"], 1, "db 替换后 query 应反映新文件")
            self.assertEqual(serve.query("SELECT name FROM tasks")[0]["name"],
                             "new_run_task")
        finally:
            shutil.rmtree(d, ignore_errors=True)

    def test_wait_for_db_times_out(self):
        d = tempfile.mkdtemp(prefix="fly_serve_wait_")
        try:
            t0 = time.time()
            with self.assertRaises(FileNotFoundError):
                serve.wait_for_db(os.path.join(d, "monitor.db"), timeout_s=0.5)
            self.assertLess(time.time() - t0, 5)
        finally:
            shutil.rmtree(d, ignore_errors=True)

    def test_port_conflict_exits_friendly(self):
        # 已有实例占端口：第二个 serve 退出码 2（友好提示路径）。
        d = tempfile.mkdtemp(prefix="fly_serve_port_")
        try:
            p = os.path.join(d, "monitor.db")
            build_test_db(p)
            serve.open_db(p)
            httpd = serve.ThreadingHTTPServer(("127.0.0.1", 0), serve.MonitorHandler)
            port = httpd.server_address[1]
            threading.Thread(target=httpd.serve_forever, daemon=True).start()
            with self.assertRaises(SystemExit) as cm:
                serve.serve(os.path.join(d), port=port)
            self.assertEqual(cm.exception.code, 2)
            httpd.shutdown()
        finally:
            shutil.rmtree(d, ignore_errors=True)

    def test_local_addresses_contains_lan_ip(self):
        # UDP connect 技巧：非回环地址应出现在候选列表（本机必有出口路由）。
        addrs = serve.local_addresses()
        self.assertIn("127.0.0.1", addrs)
        self.assertTrue(any(not a.startswith("127.") for a in addrs),
                        f"应含非回环地址: {addrs}")


class ServeLifecycleTest(unittest.TestCase):
    """GUI 启动生命周期（用户裁定）：未指定端口=随机口、地址写入 log 目录
    记录文件、同 log 目录二次启动先探测已有实例（活=复用，死=重启）。
    测试环境抑制浏览器尝试（无副作用）。"""

    def setUp(self):
        os.environ["FLY_MONITOR_NO_BROWSER"] = "1"
        self._d = tempfile.mkdtemp(prefix="fly_serve_life_")
        self._db = os.path.join(self._d, "monitor.db")
        build_test_db(self._db)

    def tearDown(self):
        os.environ.pop("FLY_MONITOR_NO_BROWSER", None)
        shutil.rmtree(self._d, ignore_errors=True)

    def _record(self):
        return serve._read_gui_record(serve._gui_record_path(self._db))

    def _wait_record(self, timeout_s=10):
        for _ in range(int(timeout_s / 0.1)):
            rec = self._record()
            if rec and serve._gui_alive(rec["host"], rec["port"], timeout=0.5):
                return rec
            time.sleep(0.1)
        return None

    def test_random_port_and_record_file(self):
        # port=None（用户未指定）→ OS 随机口 + 记录含跨机器可达的 host
        # （非回环出口 IP）+ 服务在该地址可达。
        t = threading.Thread(target=serve.serve, args=(self._d,), daemon=True)
        t.start()
        rec = self._wait_record()
        self.assertIsNotNone(rec, "记录文件未在时限内出现/服务不可达")
        self.assertGreater(rec["port"], 0)
        self.assertNotEqual(rec["host"], "", "记录应含 host（跨机器访问）")
        self.assertFalse(rec["host"].startswith("127."),
                         f"host 应为对外地址（实际 {rec['host']}）")
        with urllib.request.urlopen(
                f"http://{rec['host']}:{rec['port']}/api/meta", timeout=2) as r:
            self.assertEqual(r.status, 200)
        # daemon 线程随测试进程结束（serve_forever 无外部句柄可关）。

    def test_reuse_running_gui(self):
        # 记录文件指向活实例 → serve() 走复用分支立即返回（不起新服务、
        # 不改记录）。
        serve.open_db(self._db)
        httpd = serve.ThreadingHTTPServer(("0.0.0.0", 0), serve.MonitorHandler)
        threading.Thread(target=httpd.serve_forever, daemon=True).start()
        serve._write_gui_record(serve._gui_record_path(self._db),
                                serve._primary_host(), httpd.server_address[1])
        before = self._record()
        t0 = time.time()
        serve.serve(self._d)   # 复用分支：不阻塞
        self.assertLess(time.time() - t0, 5, "复用分支应立即返回")
        self.assertEqual(self._record(), before, "复用路径不应改写记录文件")
        httpd.shutdown()

    def test_dead_record_restarts(self):
        # 记录指向已死端口 → serve() 重启服务端并更新记录为新口。
        dead = serve.ThreadingHTTPServer(("127.0.0.1", 0), serve.MonitorHandler)
        dead_port = dead.server_address[1]
        dead.server_close()   # 拿到口后立即关闭 = 死端口
        serve._write_gui_record(serve._gui_record_path(self._db),
                                "127.0.0.1", dead_port)
        t = threading.Thread(target=serve.serve, args=(self._d,), daemon=True)
        t.start()
        rec = self._wait_record()
        self.assertIsNotNone(rec, "死记录未触发重启")
        self.assertNotEqual(rec["port"], dead_port, "应换新端口重启")

    def test_record_cleaned_on_exit(self):
        # SIGTERM 退出 → finally 清理记录文件（SIGKILL 场景由下次启动的
        # 可达性探测兜底，另有用例覆盖）。真子进程端到端验证。
        import signal
        import subprocess
        import sys
        env = dict(os.environ, FLY_MONITOR_NO_BROWSER="1")
        proc = subprocess.Popen(
            [sys.executable, os.path.abspath(serve.__file__), self._d],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
        try:
            rec = self._wait_record(15)
            self.assertIsNotNone(rec, "serve 子进程未就绪")
            proc.send_signal(signal.SIGTERM)
            gone = False
            for _ in range(50):  # 等 finally 清理（最长 5s）
                if not os.path.exists(serve._gui_record_path(self._db)):
                    gone = True
                    break
                time.sleep(0.1)
            self.assertTrue(gone, "SIGTERM 退出后记录文件未清理")
        finally:
            proc.terminate()
            proc.wait(timeout=10)

    def test_frontend_app_smoke(self):
        # 前端模块图 + 页面 mount 冒烟（node + DOM/fetch stub）——抓
        # 「curl 200 但 JS 崩导致页面空白」类回归（escapeHtml 重复声明事故）。
        import shutil
        import subprocess
        if not shutil.which("node"):
            self.skipTest("node 不可用")
        smoke = os.path.join(_project_root, "src", "monitor", "tests",
                             "app_smoke_test.mjs")
        r = subprocess.run(["node", smoke], capture_output=True, text=True,
                           timeout=30)
        self.assertEqual(r.returncode, 0,
                         f"前端冒烟失败: {r.stderr.strip()[:300]}")


if __name__ == "__main__":
    unittest.main()
