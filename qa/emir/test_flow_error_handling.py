"""流程错误处理闭环 e2e（2026-09-13 裁定范式，dev-rules §7.2）。

覆盖场景（范式二元处置逐条落实）：
  1. 部分 lib 文件语法错误 → 兜底：flow 完成（freeze 成功）+ message.log
     有 LIBR::0003 ERROR 行 + 成功部分照常产出（范式 (b)）——主 fly 进程
     正常退出，直接跑。
  2. 全部 lib 文件失败 → fatal LIBR::0004（范式 (a)）：子 fly 退出码 80 +
     message.log FATAL 行（runqa 把主 fly 非零退出判 FAIL，fatal 场景必须
     子进程隔离——先例 qa/message/test_fatal_exit.py）。
  3. DEF 文件语法错误 → fatal DSGN::0016：子 fly 退出码 80（design db 数据
     不完整无意义）。
  4. .lef 误传 build_lib_db → 入口嗅探 ValueError 秒级拦截（不建库、不起
     任务）：子 fly 非零退出、不悬挂。
  5. 判死闭环：上游 task FAILED → 下游 freeze 判死（TASK::0002）→
     wait_frozen 立即返回 False（不等满超时）+ db_failure_reason 给出原因。

数据：qa/emir/data/（cells_a.lib 合法；bad_syntax.lib / bad_syntax.def 坏
样例；design/tech.lef 合法 lef 兼作场景 4 的「误传 .lef」）。
"""
import os
import shutil
import subprocess
import time

from _fly_log import INFO

from fly import get_config, get_fly_binary, launch_workers
from fly.runtime import get_agent
from emir import EMIRProject

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(SCRIPT_DIR, "data")
GOOD_LIB = os.path.join(DATA, "cells_a.lib")
BAD_LIB = os.path.join(DATA, "bad_syntax.lib")
BAD_DEF = os.path.join(DATA, "bad_syntax.def")
GOOD_LEF = os.path.join(DATA, "design", "tech.lef")

LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "flow_error_handling")
CASE_TMP = os.path.join(LOG_DIR, "flow_error_handling_sub")
FLY_BIN = get_fly_binary()


def _fresh(path):
    if os.path.isdir(path):
        shutil.rmtree(path, ignore_errors=True)
    os.makedirs(path, exist_ok=True)
    return path


def _read_message_log(log_base):
    """子 master 的 message.log（--log-dir resolve 出 .N 变体的兜底同先例）。"""
    log_dir = log_base if os.path.isfile(os.path.join(log_base, "message.log")) \
        else log_base + ".1"
    path = os.path.join(log_dir, "message.log")
    if not os.path.isfile(path):
        return ""
    with open(path, errors="replace") as f:
        return f.read()


def _spawn_fly(script_path, log_dir):
    """Popen 子 fly master（独立 log 目录，stderr 落文件供排查）。"""
    err = open(os.path.join(CASE_TMP, os.path.basename(log_dir) + ".stderr"), "ab")
    p = subprocess.Popen([FLY_BIN, "--log-dir", log_dir, script_path],
                         stdout=subprocess.DEVNULL, stderr=err)
    p.stderr_file = err
    return p


def _wait_exit(p, timeout=120):
    try:
        return p.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        p.kill()
        raise AssertionError("child fly process must exit promptly (no hang)")


def _write_script(name, text):
    path = os.path.join(CASE_TMP, name)
    with open(path, "w") as f:
        f.write(text)
    return path


def scenario1_partial_lib_failure_falls_back():
    """部分文件坏 → 兜底完成 + LIBR::0003 + 成功部分产出（范式 (b)）。"""
    proj = EMIRProject(_fresh(PROJ_PATH))
    lib_db = proj.build_lib_db(name="lib_partial",
                               lib_paths=[GOOD_LIB, BAD_LIB])
    assert proj.wait_frozen("lib_partial", timeout=120), \
        "partial failure must fall back and complete (freeze reached)"
    library = lib_db.load_library()
    cell_names = sorted(c.name for c in library.cells)
    assert cell_names == ["INV_X1"], \
        f"successful file's cells must survive (cells_a.lib yields INV_X1): " \
        f"{cell_names}"
    message_log_path = os.path.join(LOG_DIR, "message.log")
    with open(message_log_path, errors="replace") as f:
        message_log = f.read()
    assert "LIBR::0003" in message_log, \
        "message.log must carry LIBR::0003 (partial failure) row"
    INFO("[PASS] scenario1: partial lib failure -> fallback + LIBR::0003")


def scenario2_all_lib_failure_fatal():
    """全部文件坏 → fatal LIBR::0004，子 fly 退出码 80（范式 (a)）。"""
    script = _write_script("flow_err_all_bad.py", '''\
from fly.runtime import get_agent
from emir import EMIRProject

master = get_agent()
master.launch_local_workers([{{}}])
assert master.wait_workers_registered(timeout=60), "worker not registered"

proj = EMIRProject(r"{proj}")
proj.build_lib_db(name="lib_all_bad",
                  lib_paths=[r"{bad1}", r"{bad2}"])
proj.wait_frozen("lib_all_bad", timeout=120)  # fatal 会在等待期杀掉进程
'''.format(proj=PROJ_PATH + "_all_bad", bad1=BAD_LIB, bad2=BAD_LIB))
    log_dir = os.path.join(CASE_TMP, "all_bad_log")
    p = _spawn_fly(script, log_dir)
    rc = _wait_exit(p)
    p.stderr_file.close()
    assert rc == 80, f"all-failed lib flow must fatal-exit 80, got {rc}"
    message_log = _read_message_log(log_dir)
    assert "LIBR::0004" in message_log and "[FATAL]" in message_log, \
        f"message.log must carry LIBR::0004 FATAL row: {message_log[-2000:]}"
    INFO("[PASS] scenario2: all lib files failed -> fatal exit 80 + LIBR::0004")


def scenario3_bad_def_fatal():
    """DEF 语法错误 → fatal DSGN::0016，子 fly 退出码 80（范式 (a)）。"""
    proj_dir = _fresh(PROJ_PATH + "_bad_def")
    script = _write_script("flow_err_bad_def.py", '''\
from fly.runtime import get_agent
from emir import EMIRProject

master = get_agent()
master.launch_local_workers([{{}}])
assert master.wait_workers_registered(timeout=60), "worker not registered"

proj = EMIRProject(r"{proj}")
lib_db = proj.build_lib_db(name="lib", lib_paths=[r"{good_lib}"])
assert proj.wait_frozen("lib", timeout=120), "lib db should freeze"

proj.build_design_db(name="design", def_paths=[r"{bad_def}"],
                     lef_paths=[r"{good_lef}", r"{good_lef}"],
                     lib_db=lib_db)
proj.wait_frozen("design", timeout=120)  # fatal 会在等待期杀掉进程
'''.format(proj=proj_dir, good_lib=GOOD_LIB, bad_def=BAD_DEF,
           good_lef=GOOD_LEF))
    log_dir = os.path.join(CASE_TMP, "bad_def_log")
    p = _spawn_fly(script, log_dir)
    rc = _wait_exit(p)
    p.stderr_file.close()
    assert rc == 80, f"bad DEF flow must fatal-exit 80, got {rc}"
    message_log = _read_message_log(log_dir)
    assert "DSGN::0016" in message_log and "[FATAL]" in message_log, \
        f"message.log must carry DSGN::0016 FATAL row: {message_log[-2000:]}"
    INFO("[PASS] scenario3: bad DEF -> fatal exit 80 + DSGN::0016")


def scenario4_lef_mistyped_as_lib_sniffed():
    """.lef 误传 build_lib_db → 入口嗅探 ValueError 秒级失败（不悬挂）。"""
    script = _write_script("flow_err_mistyped.py", '''\
from fly.runtime import get_agent
from emir import EMIRProject

master = get_agent()  # master-only 脚本：无需 worker（入口即拦截）

proj = EMIRProject(r"{proj}")
proj.build_lib_db(name="lib", lib_paths=[r"{lef}"])  # .lef 误传 → ValueError
'''.format(proj=PROJ_PATH + "_mistyped", lef=GOOD_LEF))
    _fresh(PROJ_PATH + "_mistyped")
    log_dir = os.path.join(CASE_TMP, "mistyped_log")
    t0 = time.time()
    p = _spawn_fly(script, log_dir)
    rc = _wait_exit(p)
    elapsed = time.time() - t0
    p.stderr_file.close()
    assert rc != 0, f"mistyped input must fail the run, got rc={rc}"
    assert elapsed < 30, \
        f"sniff must reject in seconds (no tasks, no hang), took {elapsed:.1f}s"
    err_path = os.path.join(CASE_TMP, "mistyped_log.stderr")
    with open(err_path, errors="replace") as f:
        err = f.read()
    assert "does not look like a liberty" in err, \
        f"ValueError must name the sniff reason: {err[-2000:]}"
    INFO(f"[PASS] scenario4: .lef mistyped as lib -> ValueError in {elapsed:.1f}s")


def scenario5_judged_death_signal_closes_the_loop():
    """判死闭环：上游 FAILED → 下游 freeze 判死 → wait_frozen 快速感知。

    注意：本场景结尾 stop 主进程 agent（判死感知经 get_agent 直调）——
    **必须在全部主进程场景（scenario1）之后、子进程场景（2/3/4）之前**
    执行；若未来在其后追加主进程场景会因 agent 已停而静默失败（review
    2026-09-13：顺序约束固化）。
    """
    from fly import as_task

    proj = EMIRProject(_fresh(PROJ_PATH + "_signal"))
    db = proj._create_db("sig")

    @as_task()
    def broken_producer(db):
        raise RuntimeError("simulated producer failure (signal closed-loop)")

    @as_task(inputs=lambda db: [db.get_full_name("never_produced_obj")])
    def freeze_gate(db):
        db.freeze()

    broken_producer(db)
    freeze_gate(db)

    t0 = time.time()
    ok = proj.wait_frozen("sig", timeout=60)
    elapsed = time.time() - t0
    assert not ok, "judged-death db must not be reported frozen"
    assert elapsed < 30, \
        f"wait_frozen must return promptly on failure signal, took {elapsed:.1f}s"
    reason = proj.db_failure_reason("sig")
    assert reason is not None, "db_failure_reason must surface the signal"
    task_id, error = reason
    assert "Unresolvable data dependencies" in error, error
    INFO(f"[PASS] scenario5: judged-death signal closes the loop "
         f"(wait {elapsed:.1f}s, task={task_id})")

    get_agent().stop()


launch_workers([{}, {}])
assert get_agent().wait_workers_registered(timeout=60), "workers should connect"
os.makedirs(CASE_TMP, exist_ok=True)

scenario1_partial_lib_failure_falls_back()
scenario5_judged_death_signal_closes_the_loop()
scenario2_all_lib_failure_fatal()
scenario3_bad_def_fatal()
scenario4_lef_mistyped_as_lib_sniffed()

get_agent().stop()
INFO("[PASS] test_flow_error_handling")
