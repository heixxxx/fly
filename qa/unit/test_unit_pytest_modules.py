"""在自动化里执行 qa/unit 下两个 pytest 风格的模块，逐个测试函数记录结果。

被调度的模块：smoke_test.py（Config 绑定的冒烟测试，6 个函数）、storage_test.py
（存储层 C++ 类的集成测试，18 个函数）。它们从入库起就没有被任何自动化流程执行过
（2026-11-26 救活，见 docs/DOC_CHANGELOG.md）。

做法：每个模块各起一个独立的 pytest 进程——这些测试必须在裸进程里跑，smoke_test
会调用 config.reset()，不能在已经启动的 fly 进程里执行。pytest 进程用干净的环境：
不继承 fly 的环境变量，临时目录指向本子阶段日志目录下的 tmp（随用例日志一起清理，
不进系统临时目录）。结果从 pytest 输出的 JUnit XML 里读出来：每个测试函数记一条
取值（passed / failed / error / skipped，失败时 detail 带断言信息），另记各结果的
个数和一条「全部通过」的判定。哪个函数失败、消失或被跳过，比对都会报出来。
"""
import os
import subprocess
import sys
import xml.etree.ElementTree as ET

from _fly_log import INFO
from test import record

HERE = os.path.dirname(os.path.abspath(__file__))
SUB_LOG_DIR = os.environ.get("FLY_CASE_SUB_DIR")
if not SUB_LOG_DIR:
    raise RuntimeError("FLY_CASE_SUB_DIR 没有设置：本脚本由 runqa 经编排脚本调度运行"
                       "（./qa/runqa qa/unit/test_unit_pytest_modules.pyt），不能手动直接跑")
MODULES = ["smoke_test.py", "storage_test.py"]
# 单个 pytest 子进程的超时。编排脚本给整个子阶段 120 秒，两个模块串行执行，
# 所以这里取 50 秒：模块卡死时由这里先超时，报错能指出是哪个模块；120 秒只是兜底。
PYTEST_TIMEOUT_SEC = 50


def _clean_env(tmp_dir):
    """给 pytest 子进程的干净环境：只带定位解释器与本地化需要的变量，不继承 fly 的。"""
    env = {k: os.environ[k] for k in ("PATH", "HOME", "LANG", "LC_ALL") if k in os.environ}
    env["TMPDIR"] = tmp_dir            # tempfile.mkdtemp 落在这里
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    return env


def _run_module(name, tmp_dir):
    report = os.path.join(SUB_LOG_DIR, f"junit_{name[:-3]}.xml")
    cmd = [sys.executable, "-m", "pytest", os.path.join(HERE, name),
           "-q", "-p", "no:cacheprovider", f"--junitxml={report}"]
    proc = subprocess.run(cmd, cwd=SUB_LOG_DIR, env=_clean_env(tmp_dir),
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, timeout=PYTEST_TIMEOUT_SEC)
    INFO(f"[{name}] pytest exit code {proc.returncode}")
    return proc, report


def _outcomes(report):
    """JUnit XML 里每个测试函数的结果：{函数全名: (结果, 失败信息)}。"""
    results = {}
    if not os.path.isfile(report):
        return results
    for case in ET.parse(report).getroot().iter("testcase"):
        outcome, message = "passed", ""
        for tag, label in (("failure", "failed"), ("error", "error"), ("skipped", "skipped")):
            node = case.find(tag)
            if node is not None:
                outcome = label
                message = ((node.get("message") or "") + "\n" + (node.text or "")).strip()
                break
        results[f"{case.get('classname', '')}::{case.get('name', '')}"] = (outcome, message)
    return results


tmp_dir = os.path.join(SUB_LOG_DIR, "tmp")
os.makedirs(tmp_dir, exist_ok=True)

for name in MODULES:
    stem = name[:-3]
    proc, report = _run_module(name, tmp_dir)
    outcomes = _outcomes(report)
    for test_name in sorted(outcomes):
        outcome, message = outcomes[test_name]
        # 条件记录：键来自 pytest 实际收集到的测试函数；基准锁定了函数名与个数，函数消失时缺键、新增时多键，都会报出来
        record(f"{stem}/tests/{test_name}", outcome, detail=message[-2000:])
    counts = {want: sum(1 for outcome_, _ in outcomes.values() if outcome_ == want)
              for want in ("passed", "failed", "error", "skipped")}
    record(f"{stem}/passed_count", counts["passed"])
    record(f"{stem}/failed_count", counts["failed"] + counts["error"])
    record(f"{stem}/skipped_count", counts["skipped"])
    # 全部通过：pytest 以 0 退出，每个函数都是 passed，至少执行了一个函数
    record(f"{stem}/all_passed",
           proc.returncode == 0 and counts["failed"] + counts["error"] == 0
           and counts["skipped"] == 0 and counts["passed"] > 0
           and len(outcomes) == counts["passed"],
           detail={"exit_code": proc.returncode, "tail": proc.stdout[-2000:]})

INFO("[DONE] test_unit_pytest_modules")
