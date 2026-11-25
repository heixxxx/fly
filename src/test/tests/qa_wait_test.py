"""qa_wait.wait_tasks 单元测试：等待任务完成的判定与记录。

qa_wait.py 设计为可脱离 fly 运行时按路径加载（与 qa_record_test 同方式）。
wait_tasks 的记录调用经由模块属性 ``_record_verdict`` 间接转发，测试里把
该属性替换成收集调用的桩，断言记录的键、值与诊断内容（真实 record 的行为
由 qa_record_test 覆盖）。master 用假对象：completed_tasks / failed_tasks
列表加 get_task_error 映射，不需要 fly。
"""
import importlib.util
import os
import time
import unittest

_this_dir = os.path.dirname(os.path.abspath(__file__))
_project_root = os.path.normpath(os.path.join(_this_dir, '..', '..', '..'))
_QA_WAIT_PATH = os.path.join(_project_root, 'src', 'test', 'py', 'qa_wait.py')

_spec = importlib.util.spec_from_file_location('qa_wait_under_test', _QA_WAIT_PATH)
qa_wait = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qa_wait)


class _FakeMaster:
    """假 master：wait_tasks 用到的三个成员的最小实现。"""

    def __init__(self, completed=(), failed=(), errors=None):
        self.completed_tasks = list(completed)
        self.failed_tasks = list(failed)
        self._errors = dict(errors or {})

    def get_task_error(self, task_id):
        return self._errors[task_id]


class _RecordSpy:
    """记录接口的桩：收集 (key, value, detail) 供断言。"""

    def __init__(self):
        self.calls = []

    def __call__(self, key, value, *, detail=None):
        self.calls.append({"key": key, "value": value, "detail": detail})


class WaitTasksTest(unittest.TestCase):

    def setUp(self):
        self.spy = _RecordSpy()
        self._orig = qa_wait._record_verdict
        qa_wait._record_verdict = self.spy
        self.addCleanup(setattr, qa_wait, '_record_verdict', self._orig)

    def test_all_completed_returns_true_and_records_true(self):
        master = _FakeMaster(completed=range(3))
        self.assertTrue(qa_wait.wait_tasks(master, 3, "counter/no_failed_tasks"))
        self.assertEqual(self.spy.calls, [
            {"key": "counter/no_failed_tasks", "value": True, "detail": []}])

    def test_failed_task_returns_false_immediately_with_detail(self):
        master = _FakeMaster(failed=[7], errors={7: "RuntimeError: boom"})
        start = time.monotonic()
        ok = qa_wait.wait_tasks(master, 3, "k/no_failed_tasks", timeout=10.0)
        elapsed = time.monotonic() - start
        self.assertFalse(ok)
        # 任务一失败就停止等待，不等满超时（timeout=10 秒，这里远小于 5 秒）
        self.assertLess(elapsed, 5.0)
        self.assertEqual(self.spy.calls, [
            {"key": "k/no_failed_tasks", "value": False,
             "detail": [{"task": 7, "error": "RuntimeError: boom"}]}])

    def test_timeout_without_failures_returns_false_and_records_true(self):
        master = _FakeMaster(completed=[1])  # 完成数永远到不了 3
        ok = qa_wait.wait_tasks(master, 3, "k/no_failed_tasks", timeout=0.3)
        self.assertFalse(ok)
        # 判定的值是「没有失败任务」：超时但无失败时记录 true
        self.assertEqual(self.spy.calls, [
            {"key": "k/no_failed_tasks", "value": True, "detail": []}])

    def test_key_none_does_not_record(self):
        master = _FakeMaster(completed=range(2))
        self.assertTrue(qa_wait.wait_tasks(master, 2))
        self.assertEqual(self.spy.calls, [])

    def test_get_task_error_failure_is_reported(self):
        class _BrokenMaster(_FakeMaster):
            def get_task_error(self, task_id):
                raise RuntimeError("no such task")

        master = _BrokenMaster(failed=[9])
        self.assertFalse(qa_wait.wait_tasks(master, 1, "k/no_failed_tasks"))
        detail = self.spy.calls[0]["detail"]
        self.assertEqual(detail[0]["task"], 9)
        self.assertIn("取错误消息失败", detail[0]["error"])
        self.assertIn("RuntimeError", detail[0]["error"])

    def test_multiline_error_keeps_last_nonempty_line(self):
        # 任务失败时 get_task_error 返回整段回溯，根因在末尾的异常行；
        # detail 只存最后一个非空行（差异报告展示 detail 只显示前 80 字符，
        # 存整段回溯会把根因截掉），完整回溯在 master.log 与 worker 日志里。
        traceback_text = (
            "Traceback (most recent call last):\n"
            '  File "<build>/python/fly/main.py", line 169, in _run_master\n'
            "    exec(compiled, script_ns)\n"
            "AssertionError: var counter=42, expected 42\n"
        )
        master = _FakeMaster(failed=[7], errors={7: traceback_text})
        qa_wait.wait_tasks(master, 1, "k/no_failed_tasks")
        error = self.spy.calls[0]["detail"][0]["error"]
        self.assertEqual(error, "AssertionError: var counter=42, expected 42")
        # 异常类型和消息出现在 detail 里
        self.assertIn("AssertionError", error)
        self.assertIn("var counter=42", error)

    def test_blank_lines_only_error_falls_back_to_stripped_text(self):
        master = _FakeMaster(failed=[5], errors={5: "\n  \n\n"})
        qa_wait.wait_tasks(master, 1, "k/no_failed_tasks")
        self.assertEqual(self.spy.calls[0]["detail"][0]["error"], "")

    def test_long_error_message_truncated(self):
        master = _FakeMaster(failed=[3], errors={3: "x" * 300})
        qa_wait.wait_tasks(master, 1, "k/no_failed_tasks")
        error = self.spy.calls[0]["detail"][0]["error"]
        self.assertLessEqual(len(error), 210)
        self.assertTrue(error.endswith("…"))

    def test_many_failed_tasks_capped(self):
        master = _FakeMaster(failed=list(range(12)))
        qa_wait.wait_tasks(master, 1, "k/no_failed_tasks")
        self.assertEqual(len(self.spy.calls[0]["detail"]), 10)


if __name__ == '__main__':
    unittest.main()
