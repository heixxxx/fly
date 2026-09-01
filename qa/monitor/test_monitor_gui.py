"""cluster monitor GUI QA：run 结束后以 `fly --serve-monitor` 独立启动 GUI，
HTTP 断言各 API 端点与静态前端资源；验证 monitor GUI 与 fly 主进程完全独立
（不依赖 agent/Logger 初始化，db 为干净终态只读可开）。
"""
import json
import os
import subprocess
import urllib.request

from _fly_log import INFO

from test import write_data, wait_until
from fly import open_db, get_config, wait_tasks
from fly.runtime import get_agent

LOG_DIR = get_config().get_str("log_dir")
DB_PATH = os.path.join(LOG_DIR, "db")
MONITOR_DB = os.path.join(LOG_DIR, "monitor.db")
PORT = 8797


def get_fly_binary():
    return getattr(__import__("sys"), "_fly_binary", None) or os.environ.get(
        "FLY_BUILD") and os.path.join(os.environ["FLY_BUILD"], "bin", "fly") or "fly"


def http_get(url, timeout=5):
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return r.status, r.read()


def run_and_stop():
    from test import slow_write
    master = get_agent()
    master.launch_local_workers([{}])
    assert master.wait_workers_registered(60)
    db = open_db(DB_PATH)
    write_data(db, "obj_gui", 512 * 1024)
    slow_write(db, "obj_slow", 256 * 1024, 11)  # >10s：master 自监控/worker 上报落盘
    wait_tasks(60)  # 等全部提交的 task 完成
    master.stop()
    assert os.path.exists(MONITOR_DB), "monitor.db 未产生"


def main():
    run_and_stop()

    fly_bin = get_fly_binary()
    # FLY_MONITOR_NO_BROWSER：QA 环境抑制 serve 尝试弹浏览器（副作用）。
    env = dict(os.environ, FLY_MONITOR_NO_BROWSER="1")
    proc = subprocess.Popen(
        [fly_bin, "--serve-monitor", MONITOR_DB, "--port", str(PORT)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    try:
        base = f"http://127.0.0.1:{PORT}"

        def serve_ready():
            try:
                status, _ = http_get(base + "/api/meta", timeout=1)
                return status == 200
            except OSError:
                return False

        assert wait_until(serve_ready, timeout=10), "serve-monitor 未在 10s 内就绪"

        # 各 API 端点。
        _, body = http_get(base + "/api/meta")
        meta = json.loads(body)
        # workers 表只含真实 worker（master wid=0 仅在 worker_samples 出现）。
        assert meta["workers"] >= 1, f"meta.workers={meta['workers']}"
        _, body = http_get(base + "/api/tasks")
        tasks = json.loads(body)
        assert tasks["total"] >= 1, "tasks 表为空"
        assert tasks["tasks"][0]["name"] in ("write_data", "slow_write"), \
            f"task[0] name 异常: {tasks['tasks'][0]['name']}"
        _, body = http_get(base + f"/api/tasks/{tasks['tasks'][0]['task_id']}")
        detail = json.loads(body)
        assert "events" in detail and "io" in detail
        _, body = http_get(base + "/api/workers/0/samples")
        assert len(json.loads(body)["samples"]) >= 1, "master 自监控样本缺失"
        _, body = http_get(base + "/api/timeline")
        assert len(json.loads(body)["tasks"]) >= 1
        _, body = http_get(base + "/api/dbs")
        assert len(json.loads(body)["dbs"]) >= 1

        # 静态前端（index + ECharts + 页面模块）。
        for path in ["/", "/static/vendor/echarts.min.js",
                     "/static/js/app.js", "/static/js/pages/timeline.js",
                     "/static/css/app.css"]:
            status, body = http_get(base + path)
            assert status == 200 and len(body) > 0, f"静态资源 {path} 异常"

        INFO("[PASS] monitor GUI API 与静态资源全部通过")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


main()
