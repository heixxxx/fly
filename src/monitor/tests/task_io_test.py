"""task_io 单测：归属窗口语义（set_current/record/take_result/drain/防御路径）。

纯 Python 逻辑（无 C++ 依赖）：sys.path 探测项目源码树即可。
"""
import os
import sys
import time
import unittest

_this_dir = os.path.dirname(os.path.abspath(__file__))
_project_root = os.path.normpath(os.path.join(_this_dir, '..', '..', '..'))
sys.path.insert(0, os.path.join(_project_root, 'src'))

from monitor import task_io  # noqa: E402


class TaskIoTest(unittest.TestCase):

    def setUp(self):
        # 每个用例从干净状态开始。
        task_io._current = None
        task_io._read_ms = 0.0
        task_io._read_bytes = 0
        task_io._write_ms = 0.0
        task_io._items = []

    def test_record_without_current_is_noop(self):
        task_io.record_read("db:a", 100, 1.5)
        task_io.record_write("db:a", 2.0)
        task_io.add_drain_ms(3.0)
        self.assertIsNone(task_io.current_task_id())
        result = task_io.take_result(1)
        self.assertEqual(result["read_ms"], 0.0)
        self.assertEqual(result["write_ms"], 0.0)
        self.assertEqual(result["items"], [])

    def test_basic_aggregation(self):
        task_io.set_current(9)
        self.assertEqual(task_io.current_task_id(), 9)
        task_io.record_read("db:x:obj_a", 4096, 2.5)
        task_io.record_read("db:x:obj_b", 8192, 1.5)
        task_io.record_write("db:x:obj_c", 0.7)
        task_io.add_drain_ms(4.0)
        result = task_io.take_result(9)
        self.assertAlmostEqual(result["read_ms"], 4.0)
        self.assertEqual(result["read_bytes"], 4096 + 8192)
        self.assertAlmostEqual(result["write_ms"], 4.7)
        self.assertEqual(len(result["items"]), 3)
        self.assertEqual(result["items"][0]["name"], "db:x:obj_a")
        self.assertFalse(result["items"][0]["w"])
        self.assertTrue(result["items"][2]["w"])
        self.assertGreater(result["items"][0]["epoch_ms"], 0)
        # 取走后归属清空，后续 record 不再累计。
        self.assertIsNone(task_io.current_task_id())
        task_io.record_read("db:y:obj", 1, 9.9)
        result2 = task_io.take_result(9)
        self.assertEqual(result2["read_ms"], 0.0)
        self.assertEqual(result2["items"], [])

    def test_set_current_resets_accumulators(self):
        task_io.set_current(1)
        task_io.record_read("db:a", 100, 1.0)
        task_io.set_current(2)  # 重复调用重置
        task_io.record_read("db:b", 55, 0.5)
        result = task_io.take_result(2)
        self.assertEqual(result["read_bytes"], 55)
        self.assertEqual(len(result["items"]), 1)

    def test_take_result_mismatched_task_returns_empty(self):
        task_io.set_current(5)
        task_io.record_read("db:a", 100, 1.0)
        result = task_io.take_result(99)  # 错配 task_id：空聚合且窗口保留
        self.assertEqual(result["read_ms"], 0.0)
        self.assertEqual(task_io.current_task_id(), 5)
        # 正确 task_id 仍可取走。
        result = task_io.take_result(5)
        self.assertAlmostEqual(result["read_ms"], 1.0)

    def test_items_epoch_ms_is_recent(self):
        before = int(time.time() * 1000)
        task_io.set_current(3)
        task_io.record_read("db:a", 10, 0.1)
        result = task_io.take_result(3)
        after = int(time.time() * 1000)
        self.assertTrue(before <= result["items"][0]["epoch_ms"] <= after)


if __name__ == "__main__":
    unittest.main()
