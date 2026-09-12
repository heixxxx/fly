"""fatal message 退出码 e2e（子进程隔离）。

runqa 把 fly 主进程非零退出视为 case 失败——fatal 场景（进程退出码 80）必须
用子进程隔离：本 case 亲自 Popen 子 fly 进程跑触发脚本，断言退出码与日志
（先例：qa/fault/test_worker_exit_code.py）。

覆盖（机制文档 docs/message-system.md §14）：
  1. worker 触发联动：子 fly 起 master + local worker 拓扑，task 内
     fly.fatal_message() → worker 发 FatalMessage（发送 + 写缓冲排空等待）
     → master fast_exit（失败在途任务 + StopNow 杀全部 worker）→ 子 fly
     退出码 80、message.log 含 [workerN] [FATAL] 行、master debug log 含
     fast_exit 记录、无 worker 残留进程。
  2. master 自身触发：子 fly master 脚本直接 fly.fatal_message() →
     MessageSink 落盘（豁免配额）+ detached 线程 fast_exit → 退出码 80 +
     message.log [master] [FATAL] 行。

宏行为（配额豁免 / trigger 计数 / _exit(80)）的 C++ 单测在
src/message/tests/message_registry_test.cpp（fork 用例）；存储数据损坏真实
路径在 src/storage/tests/data_corruption_test.cpp（fork 断言退出码 80）。
"""
import os
import subprocess

from _fly_log import INFO

from fly import get_fly_binary
from test import qa_tmp, wait_until

CASE_TMP = qa_tmp("fatal_exit")
os.makedirs(CASE_TMP, exist_ok=True)
FLY_BIN = get_fly_binary()

# 场景 1 脚本：子 fly master 进程内起 master + local worker，task 内触发 fatal。
# 注意 {{}} 是转义：本模板经 .format(db_path=...) 渲染。
SCRIPT_WORKER_FATAL = '''\
from fly import open_db, as_task, fatal_message, register_message_id
from fly.runtime import get_agent

register_message_id("QAFATAL::0001", "FATAL")

master = get_agent()
master.launch_local_workers([{{}}])
assert master.wait_workers_registered(timeout=60), "worker not registered"

db = open_db(r"{db_path}")


@as_task()
def trigger_fatal(db):
    # worker 是独立进程：message id 白名单按进程各自注册。
    register_message_id("QAFATAL::0001", "FATAL")
    fatal_message("QAFATAL::0001", 0, "worker fatal trigger (e2e)")
    return "never"


trigger_fatal(db)
'''

# 场景 2 脚本：子 fly master 进程自身直接触发 fatal。
SCRIPT_MASTER_FATAL = '''\
from fly import fatal_message, register_message_id

register_message_id("QAFATAL::0002", "FATAL")
fatal_message("QAFATAL::0002", 0, "master fatal trigger (e2e)")
'''


def _write_script(name, text):
    path = os.path.join(CASE_TMP, name)
    with open(path, "w") as f:
        f.write(text)
    return path


def _spawn_fly_master(script_path, log_dir):
    """Popen 子 fly master 进程（独立 log 目录，stderr 落文件供排查）。"""
    err = open(os.path.join(CASE_TMP, os.path.basename(log_dir) + ".stderr"), "ab")
    p = subprocess.Popen([FLY_BIN, "--log-dir", log_dir, script_path],
                         stdout=subprocess.DEVNULL, stderr=err)
    # stderr 句柄保存到 Popen 对象上（防局部名丢失后被 GC 关闭 fd），
    # _wait_exit 收尸后由调用方显式 close。
    p.stderr_file = err
    return p


def _wait_exit(p, timeout=120):
    try:
        return p.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        p.kill()
        raise AssertionError("fatal child fly process must exit promptly")


def _read_log(log_dir, filename):
    path = os.path.join(log_dir, filename)
    if not os.path.isfile(path):
        return ""
    with open(path, errors="replace") as f:
        return f.read()


def scenario_worker_fatal():
    """worker task 内触发 → master 联动 fast_exit，双侧同码 80。"""
    db_path = os.path.join(CASE_TMP, "fatal_db")
    log_base = os.path.join(CASE_TMP, "worker_fatal_log")
    script = _write_script("fatal_worker_task.py",
                           SCRIPT_WORKER_FATAL.format(db_path=db_path))
    p = _spawn_fly_master(script, log_base)
    rc = _wait_exit(p)
    p.stderr_file.close()
    assert rc == 80, f"worker-fatal child fly must exit 80, got {rc}"
    INFO("[PASS] worker fatal -> child fly exit code 80")

    # 子 master 的实际日志目录（--log-dir 存在时 resolve 出 .N 变体）。
    master_log = log_base if os.path.isfile(os.path.join(log_base, "message.log")) \
        else log_base + ".1"
    message_log = _read_log(master_log, "message.log")
    assert "[FATAL]" in message_log and "QAFATAL::0001" in message_log, \
        f"message.log must contain FATAL row: {message_log[-2000:]}"
    assert "[worker" in message_log, \
        f"worker fatal row must carry [workerN] tag: {message_log[-2000:]}"
    INFO("[PASS] message.log carries [workerN] [FATAL] row")

    debug_log = _read_log(master_log, "master.log")
    assert "fatal message from worker" in debug_log, \
        "master debug log must record fast_exit reason"
    INFO("[PASS] master debug log records fast_exit reason")

    # 无 worker 残留进程：StopNow 广播后 worker SIGKILL 自身；以共享
    # .fly_config 路径精确定位本拓扑的 worker 命令行。
    config_path = os.path.join(master_log, ".fly_config")

    def _worker_left():
        try:
            out = subprocess.run(["pgrep", "-af", config_path],
                                 capture_output=True, text=True, timeout=10)
        except subprocess.TimeoutExpired:
            return True
        return any("--worker" in line for line in out.stdout.splitlines())

    assert wait_until(lambda: not _worker_left(), timeout=15), \
        "no worker process may survive master fast_exit"
    INFO("[PASS] no leftover worker process")
    INFO("[PASS] scenario_worker_fatal")


def scenario_master_fatal():
    """master 脚本自身触发 → MessageSink 落盘 + fast_exit，退出码 80。"""
    log_base = os.path.join(CASE_TMP, "master_fatal_log")
    script = _write_script("fatal_master_task.py", SCRIPT_MASTER_FATAL)
    p = _spawn_fly_master(script, log_base)
    rc = _wait_exit(p)
    p.stderr_file.close()
    assert rc == 80, f"master-fatal child fly must exit 80, got {rc}"
    INFO("[PASS] master self fatal -> child fly exit code 80")

    master_log = log_base if os.path.isfile(os.path.join(log_base, "message.log")) \
        else log_base + ".1"
    message_log = _read_log(master_log, "message.log")
    assert "[FATAL]" in message_log and "[master]" in message_log \
        and "QAFATAL::0002" in message_log, \
        f"message.log must contain [master] [FATAL] row: {message_log[-2000:]}"
    INFO("[PASS] message.log carries [master] [FATAL] row")
    INFO("[PASS] scenario_master_fatal")


scenario_worker_fatal()
scenario_master_fatal()
INFO("[PASS] test_fatal_exit")
