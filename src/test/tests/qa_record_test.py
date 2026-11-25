"""qa_record 单元测试：记录接口、规范化、比对预设、基准文件读写、重录规则。

qa_record.py 设计为可脱离 fly 运行时按路径加载（qa/runqa 用 importlib 加载它，
本测试用同样的方式加载），模块顶层只依赖标准库。测试目录用 bazel 的
TEST_TMPDIR（bazel 沙箱自动清理），不落 /tmp。

测例产物命名约定：子阶段脚本名去掉 .py 记为 S，缺省结果文件是 S.result.json，
诊断文件是 S.detail.json，基准文件是 S.gold（或 S.<类型>.gold）。测试里通过
设置 sys._fly_script_path 固定 S = fake_stage。
"""
import importlib.util
import json
import math
import os
import shutil
import sys
import tempfile
import unittest

_this_dir = os.path.dirname(os.path.abspath(__file__))
_project_root = os.path.normpath(os.path.join(_this_dir, '..', '..', '..'))
_QA_RECORD_PATH = os.path.join(_project_root, 'src', 'test', 'py', 'qa_record.py')

_spec = importlib.util.spec_from_file_location('qa_record_under_test', _QA_RECORD_PATH)
qa_record = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qa_record)

# 固定的命名约定主题 S（子阶段脚本名）。
S = 'fake_stage'


def _tmp_dir(prefix='qa_record_test_'):
    base = os.environ.get('TEST_TMPDIR') or tempfile.gettempdir()
    return tempfile.mkdtemp(prefix=prefix, dir=base)


def _read_text(path):
    with open(path, encoding='utf-8') as f:
        return f.read()


def _write_text(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)


def _write_json(path, obj):
    _write_text(path, json.dumps(obj, ensure_ascii=False, indent=2) + "\n")


class QaRecordTestBase(unittest.TestCase):
    """公共脚手架：隔离环境变量、命名约定主题与记录状态。"""

    MASK_ENV_VARS = ('FLY_CASE_SUB_DIR', 'FLY_CASE_LOG_DIR', 'FLY_CASE_DIR', 'FLY_BUILD')

    def setUp(self):
        self._saved_env = {v: os.environ.get(v) for v in self.MASK_ENV_VARS}
        for v in self.MASK_ENV_VARS:
            os.environ.pop(v, None)
        self._saved_script_path = getattr(sys, '_fly_script_path', None)
        # 命名约定主题 S 来自 sys._fly_script_path（runqa 启动 fly 时注入），
        # 测试里固定为 fake_stage.py，使产物文件名可控。
        sys._fly_script_path = os.path.join(_project_root, 'qa', 'fake_stage.py')
        qa_record._recorded_keys.clear()

    def tearDown(self):
        if self._saved_script_path is None:
            sys.__dict__.pop('_fly_script_path', None)
        else:
            sys._fly_script_path = self._saved_script_path
        for v, old in self._saved_env.items():
            if old is None:
                os.environ.pop(v, None)
            else:
                os.environ[v] = old
        qa_record._recorded_keys.clear()

    def _write_text(self, path, text):
        _write_text(path, text)


class ScriptStemTest(QaRecordTestBase):

    def test_stem_from_fly_script_path(self):
        self.assertEqual(qa_record.script_stem(), S)

    def test_stem_falls_back_to_argv0(self):
        sys.__dict__.pop('_fly_script_path', None)
        old_argv = sys.argv
        try:
            sys.argv = ['/somewhere/real_stage.py', '--flag']
            self.assertEqual(qa_record.script_stem(), 'real_stage')
            sys.argv = ['/somewhere/no_ext']
            self.assertEqual(qa_record.script_stem(), 'no_ext')
        finally:
            sys.argv = old_argv

    def test_stem_missing_everything_raises(self):
        sys.__dict__.pop('_fly_script_path', None)
        old_argv = sys.argv
        try:
            sys.argv = []
            with self.assertRaises(RuntimeError):
                qa_record.script_stem()
        finally:
            sys.argv = old_argv


class NormalizeTest(QaRecordTestBase):

    def test_scalars_pass_through(self):
        self.assertIsNone(qa_record.normalize(None))
        self.assertIs(qa_record.normalize(True), True)
        self.assertIs(qa_record.normalize(False), False)
        self.assertEqual(qa_record.normalize(42), 42)
        self.assertEqual(qa_record.normalize(1.5), 1.5)
        self.assertEqual(qa_record.normalize("hello"), "hello")

    def test_tuple_and_list_become_list(self):
        self.assertEqual(qa_record.normalize((1, (2, 3))), [1, [2, 3]])
        self.assertEqual(qa_record.normalize([1, "a", None]), [1, "a", None])

    def test_set_sorted_by_normalized_text(self):
        self.assertEqual(qa_record.normalize({"b", "a", "c"}), ["a", "b", "c"])
        # 元素混类型：统一按规范化后的文本表示排序，不直接比较类型
        self.assertEqual(qa_record.normalize({2, 1}), [1, 2])

    def test_dict_keys_by_json_rule(self):
        got = qa_record.normalize({1: "a", 2.5: "b", None: "d", "s": "e"})
        self.assertEqual(got, {"1": "a", "2.5": "b", "null": "d", "s": "e"})
        # 布尔键按 json 规则转（Python 字典里 1 与 True 同键，须分开验证）
        self.assertEqual(qa_record.normalize({True: "c", False: "f"}),
                         {"true": "c", "false": "f"})

    def test_dict_key_conflict_raises(self):
        with self.assertRaises(ValueError):
            qa_record.normalize({"1": "a", 1: "b"})

    def test_dict_bad_key_type_raises(self):
        with self.assertRaises(TypeError):
            qa_record.normalize({(1, 2): "a"})

    def test_bytes_become_len_and_sha256(self):
        import hashlib
        got = qa_record.normalize(b"hello")
        self.assertEqual(got["len"], 5)
        self.assertEqual(got["sha256"], hashlib.sha256(b"hello").hexdigest())

    def test_memoryview_like_bytes(self):
        got = qa_record.normalize(memoryview(b"ab"))
        self.assertEqual(got["len"], 2)

    def test_pathlib_path_becomes_string(self):
        import pathlib
        self.assertEqual(qa_record.normalize(pathlib.Path("/x/y")), "/x/y")

    def test_unknown_object_raises_typeerror(self):
        class Mystery:
            pass
        with self.assertRaises(TypeError):
            qa_record.normalize(Mystery())

    def test_numpy_if_available(self):
        try:
            import numpy as np
        except ImportError:
            self.skipTest("numpy 不可用")
        self.assertEqual(qa_record.normalize(np.int64(7)), 7)
        self.assertEqual(qa_record.normalize(np.float32(1.5)), 1.5)
        self.assertEqual(qa_record.normalize(np.array([[1, 2], [3, 4]])), [[1, 2], [3, 4]])

    def test_mask_sub_dir_placeholder(self):
        os.environ["FLY_CASE_SUB_DIR"] = "/tmpish/sub"
        self.assertEqual(qa_record.normalize("/tmpish/sub/fly.log"), "<sub_log_dir>/fly.log")

    def test_mask_longest_path_first(self):
        os.environ["FLY_CASE_LOG_DIR"] = "/tmpish"
        os.environ["FLY_CASE_SUB_DIR"] = "/tmpish/sub"
        got = qa_record.normalize("/tmpish/sub/a /tmpish/b")
        self.assertEqual(got, "<sub_log_dir>/a <case_log_dir>/b")

    def test_mask_nested_and_dict_keys(self):
        os.environ["FLY_CASE_DIR"] = "/tmpish/case"
        got = qa_record.normalize({"p": ["x /tmpish/case y"]})
        self.assertEqual(got, {"p": ["x <case_dir> y"]})

    def test_mask_repo_root(self):
        # 仓库根取自 qa_record 模块自身的推导（runfiles 里 realpath 指回源码树，
        # 与测试进程的 _project_root 不必一致），用同一来源构造输入。
        got = qa_record.normalize(qa_record._REPO_ROOT + "/qa/x.py")
        self.assertEqual(got, "<repo>/qa/x.py")

    def test_mask_disabled(self):
        os.environ["FLY_CASE_SUB_DIR"] = "/tmpish/sub"
        self.assertEqual(qa_record.normalize("/tmpish/sub/x", mask_paths=False), "/tmpish/sub/x")


class RecordTest(QaRecordTestBase):

    def setUp(self):
        super().setUp()
        self.sub_dir = _tmp_dir()
        os.environ["FLY_CASE_SUB_DIR"] = self.sub_dir

    def tearDown(self):
        shutil.rmtree(self.sub_dir, ignore_errors=True)
        super().tearDown()

    def test_default_file_name_follows_convention(self):
        qa_record.record("alpha", 100)
        path = os.path.join(self.sub_dir, f"{S}.result.json")
        self.assertTrue(os.path.isfile(path))
        data = json.loads(_read_text(path))
        self.assertEqual(data, {"alpha": 100})
        # 第二次调用立即追加并保留首次的键，键按记录顺序排列
        qa_record.record("beta", "v")
        data = json.loads(_read_text(path))
        self.assertEqual(data, {"alpha": 100, "beta": "v"})
        self.assertEqual(list(data.keys()), ["alpha", "beta"])

    def test_file_format_indent2_and_trailing_newline(self):
        qa_record.record("k", {"中文": "值"})
        text = _read_text(os.path.join(self.sub_dir, f"{S}.result.json"))
        self.assertTrue(text.endswith("\n"))
        self.assertIn("中文", text)  # ensure_ascii=False
        self.assertIn('\n  "k": {', text)  # 缩进 2

    def test_corrupted_result_file_raises(self):
        # E7：已存在的结果文件不是合法 JSON → RuntimeError，不静默当空文件覆盖
        _write_text(os.path.join(self.sub_dir, f"{S}.result.json"), "{not json")
        with self.assertRaises(RuntimeError) as ctx:
            qa_record.record("k", 1)
        self.assertIn(f"{S}.result.json", str(ctx.exception))
        # 原损坏内容不被覆盖
        self.assertEqual(_read_text(os.path.join(self.sub_dir, f"{S}.result.json")),
                         "{not json")

    def test_corrupted_detail_file_raises(self):
        _write_text(os.path.join(self.sub_dir, f"{S}.detail.json"), "][")
        with self.assertRaises(RuntimeError) as ctx:
            qa_record.record("k", 1)
        self.assertIn(f"{S}.detail.json", str(ctx.exception))

    def test_duplicate_key_raises_runtime_error(self):
        qa_record.record("k", 1)
        with self.assertRaises(RuntimeError):
            qa_record.record("k", 2)

    def test_empty_or_non_string_key_rejected(self):
        with self.assertRaises(ValueError):
            qa_record.record("", 1)
        with self.assertRaises(ValueError):
            qa_record.record(3, 1)

    def test_file_must_be_relative_and_s_prefixed(self):
        with self.assertRaises(ValueError):
            qa_record.record("k", 1, file="/abs/x.json")
        with self.assertRaises(ValueError):
            qa_record.record("k", 1, file="../escape.json")
        # 只能是纯文件名：不允许路径分隔符（E6）
        with self.assertRaises(ValueError):
            qa_record.record("k", 1, file="sub/escape.json")
        with self.assertRaises(ValueError):
            qa_record.record("k", 1, file="sub\\escape.json")
        with self.assertRaises(ValueError):
            qa_record.record("k", 1, file="sub/../escape.json")
        # 不以 S. 开头的文件名不符合命名约定
        with self.assertRaises(ValueError):
            qa_record.record("k", 1, file="other_stage.result.json")

    def test_file_with_s_prefix_plain_name_ok(self):
        qa_record.record("k", 1, file=f"{S}.extra.txt")
        self.assertTrue(os.path.isfile(os.path.join(self.sub_dir, f"{S}.extra.txt")))

    def test_detail_file_one_per_subcase_keyed_by_result_file(self):
        qa_record.record("k", False, detail=[7])
        detail_path = os.path.join(self.sub_dir, f"{S}.detail.json")
        data = json.loads(_read_text(detail_path))
        # 键是 <结果文件名>/<键>
        self.assertIn(f"{S}.result.json/k", data)
        self.assertTrue(data[f"{S}.result.json/k"]["at"].startswith("qa_record_test.py:"))
        self.assertEqual(data[f"{S}.result.json/k"]["detail"], [7])

    def test_detail_file_shared_across_result_files(self):
        qa_record.record("a", 1)
        qa_record.record("b", 2, file=f"{S}.extra.json")
        detail_path = os.path.join(self.sub_dir, f"{S}.detail.json")
        data = json.loads(_read_text(detail_path))
        self.assertIn(f"{S}.result.json/a", data)
        self.assertIn(f"{S}.extra.json/b", data)

    def test_detail_unserializable_falls_back_to_repr(self):
        # E4：诊断信息不能成为用例失败的原因
        qa_record.record("k", 1, detail=object())
        detail_path = os.path.join(self.sub_dir, f"{S}.detail.json")
        data = json.loads(_read_text(detail_path))
        entry = data[f"{S}.result.json/k"]
        self.assertTrue(entry["detail"].startswith("<object object at"))

    def test_detail_file_written_even_without_detail(self):
        qa_record.record("k", 1)
        detail_path = os.path.join(self.sub_dir, f"{S}.detail.json")
        data = json.loads(_read_text(detail_path))
        self.assertTrue(data[f"{S}.result.json/k"]["at"].startswith("qa_record_test.py:"))
        self.assertIsNone(data[f"{S}.result.json/k"]["detail"])


class DiffValueTest(QaRecordTestBase):

    def test_equal_returns_empty(self):
        self.assertEqual(qa_record.diff_value({"a": 1}, {"a": 1}), [])

    def test_value_mismatch(self):
        diffs = qa_record.diff_value({"a": 1}, {"a": 2})
        self.assertEqual(len(diffs), 1)
        self.assertIn("值不一致", diffs[0])
        self.assertIn("a", diffs[0])

    def test_type_mismatch(self):
        self.assertIn("类型不一致", qa_record.diff_value("1", 1)[0])
        self.assertIn("类型不一致", qa_record.diff_value(1, 1.0)[0])
        # 布尔与整数是不同类型
        self.assertIn("类型不一致", qa_record.diff_value(1, True)[0])

    def test_missing_and_extra_keys(self):
        diffs = qa_record.diff_value({"a": 1}, {"a": 1, "b": 2, "c": 3})
        kinds = "".join(diffs)
        self.assertIn("缺键", kinds)
        self.assertIn("b", kinds)
        self.assertIn("c", kinds)
        diffs = qa_record.diff_value({"a": 1, "z": 9}, {"a": 1})
        self.assertIn("多键", diffs[0])
        self.assertIn("z", diffs[0])

    def test_nested_dict_recursion(self):
        diffs = qa_record.diff_value({"g": {"x": 1}}, {"g": {"x": 2}})
        self.assertEqual(len(diffs), 1)
        self.assertIn("g.x", diffs[0])

    def test_list_length_and_order(self):
        self.assertIn("长度不一致", qa_record.diff_value([1, 2], [1, 2, 3])[0])
        diffs = qa_record.diff_value([1, 2], [2, 1])
        self.assertEqual(len(diffs), 2)
        self.assertIn("[1]", diffs[1])

    def test_nan_equals_nan(self):
        self.assertEqual(
            qa_record.diff_value(float('nan'), float('nan')), [])
        self.assertEqual(
            qa_record.diff_value({"a": float('nan')}, {"a": float('nan')}), [])

    def test_verdict_false_against_true(self):
        diffs = qa_record.diff_value(False, True)
        self.assertEqual(len(diffs), 1)
        self.assertIn("判定不成立", diffs[0])
        # 值为 true 时是普通的相等比较，不产生差异
        self.assertEqual(qa_record.diff_value(True, True), [])

    def test_top_level_false_is_verdict(self):
        diffs = qa_record.diff_value({"v": False}, {"v": True})
        self.assertEqual(len(diffs), 1)
        self.assertIn("判定不成立", diffs[0])

    def test_nested_false_is_data_not_verdict(self):
        # 嵌套在字典、列表里的布尔是数据：false 对 true 按值比对，不是「判定不成立」
        diffs = qa_record.diff_value({"g": {"b": False}}, {"g": {"b": True}})
        self.assertEqual(len(diffs), 1)
        self.assertIn("值不一致", diffs[0])
        self.assertNotIn("判定不成立", diffs[0])
        diffs = qa_record.diff_value({"l": [False]}, {"l": [True]})
        self.assertEqual(len(diffs), 1)
        self.assertIn("值不一致", diffs[0])
        self.assertNotIn("判定不成立", diffs[0])

    def test_nested_false_equal_is_no_diff(self):
        self.assertEqual(
            qa_record.diff_value({"g": {"b": False}}, {"g": {"b": False}}), [])

    def test_top_level_path_empty(self):
        diffs = qa_record.diff_value(1, 2)
        self.assertEqual(len(diffs), 1)


class ValidateCheckItemTest(QaRecordTestBase):

    def test_type_suffix_gets_s_prefix(self):
        opts = qa_record.validate_check_item("result.json", S)
        self.assertEqual(opts["file"], f"{S}.result.json")
        self.assertEqual(opts["method"], "kv")
        self.assertIsNone(opts["compare"])
        self.assertIsNone(opts["gold"])
        self.assertIs(opts["manual"], False)

    def test_full_name_passes_through(self):
        opts = qa_record.validate_check_item(f"{S}.messages.txt", S)
        self.assertEqual(opts["file"], f"{S}.messages.txt")

    def test_contains_defaults_to_manual(self):
        opts = qa_record.validate_check_item({"file": "m.log", "method": "contains"}, S)
        self.assertEqual(opts["file"], f"{S}.m.log")
        self.assertIs(opts["manual"], True)

    def test_missing_file_rejected(self):
        with self.assertRaises(ValueError):
            qa_record.validate_check_item({"method": "kv"}, S)

    def test_unknown_key_rejected(self):
        with self.assertRaises(ValueError):
            qa_record.validate_check_item({"file": "a.json", "who": 1}, S)

    def test_method_and_compare_exclusive(self):
        with self.assertRaises(ValueError):
            qa_record.validate_check_item(
                {"file": "a.json", "method": "kv", "compare": lambda p, g: []}, S)

    def test_absolute_or_parent_path_rejected(self):
        with self.assertRaises(ValueError):
            qa_record.validate_check_item("/abs/a.json", S)
        with self.assertRaises(ValueError):
            qa_record.validate_check_item("../a.json", S)
        with self.assertRaises(ValueError):
            qa_record.validate_check_item({"file": "x/../a.json"}, S)

    def test_unknown_preset_rejected(self):
        with self.assertRaises(ValueError):
            qa_record.validate_check_item({"file": "a.json", "method": "no_such"}, S)

    def test_non_string_non_dict_item_rejected(self):
        with self.assertRaises(ValueError):
            qa_record.validate_check_item(3, S)

    def test_duplicate_file_in_list_rejected(self):
        with self.assertRaises(ValueError):
            qa_record.validate_check_list(["a.json", {"file": "a.json"}], S)
        # 清单级规则：单项校验本身不涉及重复，合法清单按约定展开返回
        validated = qa_record.validate_check_list(["a.json", "b.txt"], S)
        self.assertEqual([o["file"] for o in validated],
                         [f"{S}.a.json", f"{S}.b.txt"])

    def test_file_without_extension_rejected_early(self):
        # E6：缺扩展名在 validate_check_item 就报（启动子进程之前）
        with self.assertRaises(ValueError):
            qa_record.validate_check_item("noext", S)
        with self.assertRaises(ValueError):
            qa_record.validate_check_item({"file": "noext"}, S)

    def test_file_with_separator_rejected(self):
        with self.assertRaises(ValueError):
            qa_record.validate_check_item("sub/a.json", S)
        with self.assertRaises(ValueError):
            qa_record.validate_check_item({"file": "a.json",
                                           "gold": f"data/{S}.x.gold"}, S)

    def test_custom_gold_file_name_convention(self):
        opts = qa_record.validate_check_item(
            {"file": "trace.txt", "compare": lambda p, g: [],
             "gold": f"{S}.trace.gold"}, S)
        self.assertEqual(opts["gold"], f"{S}.trace.gold")
        # 不以 S. 开头
        with self.assertRaises(ValueError):
            qa_record.validate_check_item(
                {"file": "trace.txt", "compare": lambda p, g: [],
                 "gold": "other.trace.gold"}, S)
        # 不以 .gold 结尾
        with self.assertRaises(ValueError):
            qa_record.validate_check_item(
                {"file": "trace.txt", "compare": lambda p, g: [],
                 "gold": f"{S}.trace.json"}, S)


class GoldFileTest(QaRecordTestBase):

    def setUp(self):
        super().setUp()
        self.tmp = _tmp_dir()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)
        super().tearDown()

    def test_gold_file_name_convention(self):
        # 缺省类型 result 省略：S.result.json → S.gold
        self.assertEqual(
            qa_record.gold_file_name("stage", "stage.result.json"), "stage.gold")
        # 其他类型：S.<类型>.<扩展名> → S.<类型>.gold
        self.assertEqual(
            qa_record.gold_file_name("stage", "stage.messages.txt"), "stage.messages.gold")
        self.assertEqual(
            qa_record.gold_file_name("stage", "stage.trace.dat"), "stage.trace.gold")

    def test_gold_file_name_rejects_bad_result_names(self):
        with self.assertRaises(ValueError):
            qa_record.gold_file_name("stage", "other.result.json")  # 不以 S. 开头
        with self.assertRaises(ValueError):
            qa_record.gold_file_name("stage", "stage.noext")  # 缺扩展名

    def test_gold_path_for_result(self):
        path = qa_record.gold_path_for_result(os.path.join(self.tmp, "gold"), "stage",
                                              "stage.result.json")
        self.assertEqual(
            os.path.basename(os.path.dirname(path)), "gold")
        self.assertEqual(os.path.basename(path), "stage.gold")

    def test_save_and_load_roundtrip(self):
        path = os.path.join(self.tmp, "g.gold")
        content = {"k": 1, "nested": {"x": [1, 2]}}
        qa_record.save_gold_file(path, content)
        self.assertEqual(qa_record.load_gold_file(path), content)

    def test_save_format_sorted_keys_indent2_newline(self):
        path = os.path.join(self.tmp, "g.gold")
        qa_record.save_gold_file(path, {"b": {"z": 1, "a": 2}, "a": {}})
        text = _read_text(path)
        # 键排序：外层 a 在 b 前，内层 a 在 z 前
        self.assertLess(text.index('"a"'), text.index('"b"'))
        self.assertTrue(text.endswith("\n"))
        self.assertIn('\n  "b": {', text)

    def test_load_missing_returns_none(self):
        self.assertIsNone(qa_record.load_gold_file(
            os.path.join(self.tmp, "nope.gold")))

    def test_ascii_false(self):
        path = os.path.join(self.tmp, "g.gold")
        qa_record.save_gold_file(path, {"a": {"k": "中文"}})
        self.assertIn("中文", _read_text(path))


class CheckResultFilesTest(QaRecordTestBase):

    def setUp(self):
        super().setUp()
        self.tmp = _tmp_dir()
        self.sub_dir = os.path.join(self.tmp, "1_stage")
        self.gold_dir = os.path.join(self.tmp, "gold")
        os.makedirs(self.sub_dir)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)
        super().tearDown()

    def _write_result(self, name, obj):
        _write_json(os.path.join(self.sub_dir, name), obj)

    def _write_gold(self, name, obj):
        _write_json(os.path.join(self.gold_dir, name), obj)

    def _check(self, items, script_stem=S):
        lines, _bad = qa_record.check_result_files(
            qa_record.validate_check_list(items, script_stem),
            self.sub_dir, self.gold_dir, script_stem,
            regold_hint="./qa/runqa --regold qa/x/case.pyt")
        return lines

    def test_all_match_reports_nothing(self):
        self._write_result(f"{S}.result.json", {"a": 1, "v": True})
        self._write_gold(f"{S}.gold", {"a": 1, "v": True})
        lines = self._check(["result.json"])
        self.assertEqual(lines, [])

    def test_value_diff_report_shape(self):
        self._write_result(f"{S}.result.json", {"a": 1})
        self._write_gold(f"{S}.gold", {"a": 2})
        lines = self._check(["result.json"])
        self.assertEqual(len(lines), 2)
        self.assertIn(f"{S}.result.json", lines[0])
        self.assertIn("[kv]", lines[0])
        self.assertIn("不一致 1 处", lines[0])
        self.assertIn("值不一致", lines[1])
        self.assertIn("期望 2", lines[1])
        self.assertIn("实际 1", lines[1])

    def test_diff_lines_carry_at_and_detail_per_top_level_key(self):
        # C4：诊断条目按顶层键直接定位，键含点也能正确带出 at/detail。
        # detail 只出现在「判定不成立」行；「值不一致」行带调用点。
        self._write_result(f"{S}.result.json", {"a.b": False, "plain": 5})
        self._write_gold(f"{S}.gold", {"a.b": True, "plain": 6})
        _write_json(os.path.join(self.sub_dir, f"{S}.detail.json"), {
            f"{S}.result.json/a.b": {"at": "stage_run.py:10", "detail": "dotted"},
            f"{S}.result.json/plain": {"at": "stage_run.py:11", "detail": None},
        })
        lines = self._check(["result.json"])
        verdict_line = [ln for ln in lines if "判定不成立" in ln][0]
        self.assertIn("a.b", verdict_line)
        self.assertIn("detail: \"dotted\"", verdict_line)
        self.assertIn("(stage_run.py:10)", verdict_line)
        value_line = [ln for ln in lines if "plain" in ln][0]
        self.assertIn("(stage_run.py:11)", value_line)

    def test_missing_result_file(self):
        self._write_gold(f"{S}.gold", {"a": 1})
        lines = self._check(["result.json"])
        self.assertTrue(any("结果文件缺失" in ln for ln in lines))

    def test_missing_expanded_item_gets_expansion_hint(self):
        self._write_gold(f"{S}.gold", {"a": 1})
        lines = self._check(["message.log"])  # 自动展开为 S.message.log
        joined = "".join(lines)
        self.assertIn("结果文件缺失", joined)
        self.assertIn("已按命名约定展开", joined)
        self.assertIn(f"{S}.message.log", joined)

    def test_missing_full_name_item_has_no_expansion_hint(self):
        self._write_gold(f"{S}.gold", {"a": 1})
        lines = self._check([f"{S}.result.json"])  # 完整名，未展开
        joined = "".join(lines)
        self.assertIn("结果文件缺失", joined)
        self.assertNotIn("已按命名约定展开", joined)

    def test_non_dict_gold_message_differs_from_result(self):
        # E5：基准不是 JSON 对象与结果不是 JSON 对象的文案区分开
        self._write_result(f"{S}.result.json", {"a": 1})
        self._write_gold(f"{S}.gold", ["not", "an", "object"])
        lines = self._check(["result.json"])
        joined = "".join(lines)
        self.assertIn("基准格式不是 JSON 对象", joined)
        self.assertNotIn("文件格式不是 JSON 对象", joined)

    def test_missing_gold(self):
        self._write_result(f"{S}.result.json", {"a": 1})
        lines = self._check(["result.json"])
        joined = "".join(lines)
        self.assertIn("基准缺失", joined)
        self.assertIn("./qa/runqa --regold qa/x/case.pyt", joined)

    def test_top_level_false_gold_rejected(self):
        self._write_result(f"{S}.result.json", {"a": 1})
        self._write_gold(f"{S}.gold", {"a": 1, "v": False})
        lines = self._check(["result.json"])
        joined = "".join(lines)
        self.assertIn("基准里顶层的布尔值只允许 true", joined)
        self.assertIn("v", joined)

    def test_nested_false_gold_is_legal(self):
        # 嵌套布尔是数据，false 是合法期望值
        self._write_result(f"{S}.result.json", {"g": {"b": False}})
        self._write_gold(f"{S}.gold", {"g": {"b": False}})
        self.assertEqual(self._check(["result.json"]), [])

    def test_nested_false_mismatch_is_value_diff(self):
        self._write_result(f"{S}.result.json", {"g": {"b": False}})
        self._write_gold(f"{S}.gold", {"g": {"b": True}})
        lines = self._check(["result.json"])
        joined = "".join(lines)
        self.assertIn("值不一致", joined)
        self.assertNotIn("判定不成立", joined)

    def test_contains_must_and_must_not(self):
        self._write_text(os.path.join(self.sub_dir, f"{S}.m.log"), "good line\nbad line\n")
        self._write_gold(f"{S}.m.gold",
                         {"must": ["good line", "absent"], "must_not": ["bad line"]})
        lines = self._check([{"file": "m.log", "method": "contains"}])
        joined = "".join(lines)
        self.assertIn("缺少片段", joined)
        self.assertIn("absent", joined)
        self.assertIn("出现禁止片段", joined)
        self.assertIn("bad line", joined)

    def test_custom_compare_used(self):
        self._write_result(f"{S}.trace.txt", "line1\n")

        def compare(result_path, gold_arg):
            return ["行 1: 期望 a 实际 b"]

        lines = self._check([{"file": "trace.txt", "compare": compare}])
        self.assertIn("行 1: 期望 a 实际 b", "".join(lines))

    def test_custom_compare_default_gold_from_convention_file(self):
        # C2：没声明 gold= 时，缺省取 <S>.<类型>.gold 的 JSON 值
        self._write_result(f"{S}.trace.txt", "line1\n")
        self._write_gold(f"{S}.trace.gold", {"sep": ","})
        seen = {}

        def compare(result_path, gold_arg):
            seen["gold_arg"] = gold_arg
            return []

        self._check([{"file": "trace.txt", "compare": compare}])
        self.assertEqual(seen["gold_arg"], {"sep": ","})

    def test_custom_compare_default_gold_missing_is_none(self):
        self._write_result(f"{S}.trace.txt", "line1\n")
        seen = {}

        def compare(result_path, gold_arg):
            seen["gold_arg"] = gold_arg
            return []

        self._check([{"file": "trace.txt", "compare": compare}])
        self.assertIsNone(seen["gold_arg"])

    def test_custom_compare_with_independent_gold_path(self):
        self._write_result(f"{S}.trace.txt", "line1\n")
        self._write_text(os.path.join(self.gold_dir, "data", f"{S}.trace.gold"),
                         "CONFIG-MARKER")
        seen = {}

        def compare(result_path, gold_arg):
            seen["gold_arg"] = gold_arg
            return []

        self._check([{"file": "trace.txt", "compare": compare,
                      "gold": f"{S}.trace.gold"}])
        self.assertEqual(seen["gold_arg"],
                         os.path.join(self.gold_dir, "data", f"{S}.trace.gold"))

    def test_custom_compare_exception_reported(self):
        self._write_result(f"{S}.trace.txt", "x")

        def compare(result_path, gold_arg):
            raise KeyError("boom")

        lines = self._check([{"file": "trace.txt", "compare": compare}])
        joined = "".join(lines)
        self.assertIn("自定义比较方式执行出错", joined)
        self.assertIn("boom", joined)

    def test_diff_display_limit_twenty(self):
        self._write_result(f"{S}.result.json", {f"k{i}": 0 for i in range(30)})
        self._write_gold(f"{S}.gold", {f"k{i}": 1 for i in range(30)})
        lines = self._check(["result.json"])
        shown = [ln for ln in lines if "值不一致" in ln]
        self.assertEqual(len(shown), 20)
        joined = "".join(lines)
        self.assertIn("30", joined)  # 总数提示


class RegoldTest(QaRecordTestBase):

    def setUp(self):
        super().setUp()
        self.tmp = _tmp_dir()
        self.sub_dir = os.path.join(self.tmp, "1_stage")
        os.makedirs(self.sub_dir)
        # 固定的「fly 二进制修改时间」参照值（数值本身任意，只要为正且与墙钟
        # 无关）。不要改回从 os.path.getmtime(__file__) 或当前时间推导——原写法
        # 「源文件修改时间 + 3600」只在源文件最后一次修改之后的一小时内成立
        # （它指望运行时刚写出的结果文件比它旧），一小时之后
        # test_regoldable_preset_makes_gold 必然失败，推送前校验的单测阶段会被
        # 拦下（2026-11-25 实证）。本类的新旧两个场景各自用 os.utime 把结果
        # 文件的修改时间显式设成比它旧/比它新，与墙上时钟完全无关。
        self.fly_mtime = 1_000_000_000.0

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)
        super().tearDown()

    def _write_result(self, name, obj):
        path = os.path.join(self.sub_dir, name)
        _write_json(path, obj)
        # 显式设成「比 fly 二进制旧」（缺省场景：提示结果可能来自旧版本的二进制）。
        # 不能依赖写入时刻与 fly_mtime 的天然先后——那又是依赖墙上时钟。
        os.utime(path, (self.fly_mtime - 10, self.fly_mtime - 10))

    def _regold(self, items):
        return qa_record.regold_result_files(
            qa_record.validate_check_list(items, S), self.sub_dir, self.fly_mtime, S)

    def test_regoldable_preset_makes_gold(self):
        self._write_result(f"{S}.result.json", {"a": 1, "v": True})
        pending, notes = self._regold(["result.json"])
        self.assertEqual(
            pending,
            {f"{S}.result.json": {"gold_rel": f"{S}.gold", "value": {"a": 1, "v": True}}})
        self.assertTrue(any("旧版本" in n for n in notes))

    def test_missing_result_file_skipped(self):
        pending, notes = self._regold(["result.json"])
        self.assertEqual(pending, {})
        self.assertTrue(any("未重录" in n for n in notes))

    def test_explicit_manual_kv_not_regolded(self):
        # E3：manual=True 声明手工维护，kv 项也不写基准
        self._write_result(f"{S}.result.json", {"a": 1})
        pending, notes = self._regold(
            [{"file": "result.json", "manual": True}])
        self.assertEqual(pending, {})
        self.assertTrue(any("手工维护" in n for n in notes))

    def test_top_level_false_rejected(self):
        self._write_result(f"{S}.result.json", {"v": False})
        with self.assertRaises(qa_record.RegoldRejected) as ctx:
            self._regold(["result.json"])
        self.assertIn("不能重录", str(ctx.exception))

    def test_nested_false_result_can_regold(self):
        # C1：嵌套布尔是数据，false 可以重录成期望值
        self._write_result(f"{S}.result.json", {"g": {"b": False}, "l": [True, False]})
        pending, _notes = self._regold(["result.json"])
        self.assertEqual(
            pending,
            {f"{S}.result.json": {
                "gold_rel": f"{S}.gold",
                "value": {"g": {"b": False}, "l": [True, False]}}})

    def test_contains_skipped_as_manual(self):
        self._write_text(os.path.join(self.sub_dir, f"{S}.m.log"), "text")
        pending, notes = self._regold([{"file": "m.log", "method": "contains"}])
        self.assertEqual(pending, {})
        self.assertTrue(any("手工维护" in n for n in notes))

    def test_custom_compare_without_make_gold_skipped(self):
        self._write_text(os.path.join(self.sub_dir, f"{S}.t.txt"), "x")

        def compare(result_path, gold_arg):
            return []

        pending, notes = self._regold([{"file": "t.txt", "compare": compare}])
        self.assertEqual(pending, {})
        self.assertTrue(any("手工维护" in n for n in notes))

    def test_custom_compare_with_make_gold_target(self):
        self._write_text(os.path.join(self.sub_dir, f"{S}.t.txt"), "x")

        def compare(result_path, gold_arg):
            return []

        compare.make_gold = lambda result_path: {"made": True}
        # C2：无 gold= → 落到按命名约定推导的 S.<类型>.gold
        pending, _notes = self._regold([{"file": "t.txt", "compare": compare}])
        self.assertEqual(
            pending, {f"{S}.t.txt": {"gold_rel": f"{S}.t.gold", "value": {"made": True}}})

        # C2：声明 gold= → 落到 gold/data/<gold>
        compare.make_gold = lambda result_path: {"made2": True}
        pending, _notes = self._regold(
            [{"file": "t.txt", "compare": compare, "gold": f"{S}.trace.gold"}])
        self.assertEqual(
            pending,
            {f"{S}.t.txt": {"gold_rel": os.path.join("data", f"{S}.trace.gold"),
                            "value": {"made2": True}}})

    def test_recent_result_no_stale_hint(self):
        self._write_result(f"{S}.result.json", {"a": 1})
        os.utime(os.path.join(self.sub_dir, f"{S}.result.json"),
                 (self.fly_mtime + 10, self.fly_mtime + 10))
        pending, notes = self._regold(["result.json"])
        self.assertFalse(any("旧版本" in n for n in notes))


class GoldChangeStatsTest(QaRecordTestBase):

    def test_all_added_when_old_missing(self):
        stats = qa_record.gold_change_stats(None, {"a": 1, "b": 2})
        self.assertEqual(stats, {"added": 2, "removed": 0, "changed": 0})

    def test_added_removed_changed(self):
        old = {"keep": 1, "drop": 2, "mod": 3}
        new = {"keep": 1, "mod": 9, "fresh": 4}
        stats = qa_record.gold_change_stats(old, new)
        self.assertEqual(stats, {"added": 1, "removed": 1, "changed": 1})

    def test_nested_leaves_counted(self):
        old = {"g": {"x": 1}}
        new = {"g": {"x": 1, "y": 2}}
        stats = qa_record.gold_change_stats(old, new)
        self.assertEqual(stats, {"added": 1, "removed": 0, "changed": 0})

    def test_no_change_is_zero(self):
        stats = qa_record.gold_change_stats({"a": [1, 2]}, {"a": [1, 2]})
        self.assertEqual(stats, {"added": 0, "removed": 0, "changed": 0})

    def test_nan_no_change(self):
        stats = qa_record.gold_change_stats(
            {"a": float('nan')}, {"a": float('nan')})
        self.assertEqual(stats, {"added": 0, "removed": 0, "changed": 0})


class FindTopLevelFalseTest(QaRecordTestBase):

    def test_finds_top_level_false_only(self):
        # C1：只有顶层布尔条目是判定型；嵌套布尔是数据，不在检查范围
        got = qa_record.find_top_level_false(
            {"ok": True, "bad": False, "g": {"b": False}, "l": [False]})
        self.assertEqual(got, ["bad"])

    def test_no_false_or_non_dict(self):
        self.assertEqual(
            qa_record.find_top_level_false({"a": True, "b": 0, "c": "false"}), [])
        self.assertEqual(qa_record.find_top_level_false(False), [])


if __name__ == "__main__":
    unittest.main()
