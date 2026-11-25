"""运行器自测：check_list 判定、差异报告、重录规则（QA 改造任务书步骤 1/2）。

场景全部确定性、秒级完成，每轮全量运行都会执行：
  1. 清单合法且结果与基准一致 → 正常通过；
  2. 结果与基准不符 → SubcaseError，差异报告含「值不一致 / 判定不成立 / 多键」，
     编排脚本 try 捕获后继续；
  3. 子阶段不写结果文件 → 差异「结果文件缺失」（含命名约定展开提示）；
  4. 基准里没有该子阶段的条目 → 差异「基准缺失」+ 重录提示；
  5. check_list 各种非法写法 → 启动子进程之前抛 ValueError；
  6. 重录单元规则：构造临时结果文件直接调用 qa_record.regold_result_files，
     断言基准生成、顶层布尔为假被拒、嵌套布尔可重录、manual 手工项与缺失
     结果文件被跳过；
  7. expect_pass=False 的子阶段带 check_list：失败时刻的状态同样比对；
  8. 退出状态与比对差异同时失败 → 报告里两者都在；
  9. 重录的调度分支与收尾落盘：在 logs/ 下构造临时编排脚本与结果文件，
     直接调用运行器的 _regold_one，断言基准生成、结果不变不重写、结果变化
     基准更新。

差异报告落盘断言（追加写、分段）在场景 8 之后统一做。

本脚本在重录模式（--regold）下整体跳过、只打印一行说明：场景 2 的判定为假会被
拒绝重录、场景 4 的子阶段会被重录出「基准缺失」场景的基准文件，全量重录不能被
自测用例拖成失败。
"""
import json
import os
import shutil

if QA_MODE == "regold":
    INFO("[skip] test_runner_check: 自测用例不参与重录（场景 2 判定为假会被拒绝、"
         "场景 4 会被重录出基准缺失场景的基准）；正常运行时覆盖全部场景")
else:
    # ── 场景 1：清单合法 + 结果与基准一致 → 正常通过 ──
    run_subcase("runner_check_record.py", timeout=15, check_list=["result.json"])

    # ── 场景 2：结果与基准不符 → SubcaseError，捕获后继续 ──
    try:
        run_subcase("runner_check_wrong.py", timeout=15, check_list=["result.json"])
        raise RuntimeError("场景 2 未按预期失败：与基准不符应当抛 SubcaseError")
    except SubcaseError as exc:
        text = str(exc)
        for expected in ("值不一致", "判定不成立", "多键", "extra_key", "detail: [7]"):
            assert expected in text, f"场景 2 差异报告缺少 {expected!r}：\n{text}"

    # ── 场景 3：子阶段不写结果文件 → 差异「结果文件缺失」+ 展开提示 ──
    try:
        run_subcase("runner_check_silent.py", timeout=15, check_list=["result.json"])
        raise RuntimeError("场景 3 未按预期失败：结果文件缺失应当抛 SubcaseError")
    except SubcaseError as exc:
        text = str(exc)
        assert "结果文件缺失" in text, text
        assert "已按命名约定展开" in text, text

    # ── 场景 4：基准里没有该子阶段的条目 → 差异「基准缺失」+ 重录提示 ──
    try:
        run_subcase("runner_check_nogold.py", timeout=15, check_list=["result.json"])
        raise RuntimeError("场景 4 未按预期失败：基准缺失应当抛 SubcaseError")
    except SubcaseError as exc:
        text = str(exc)
        assert "基准缺失" in text, text
        assert "--regold" in text, text

    # ── 场景 5：check_list 非法写法 → 启动子进程之前抛 ValueError ──
    def _expect_value_error(desc, **kwargs):
        try:
            run_subcase("runner_check_dummy_never_runs.py", timeout=15, **kwargs)
        except ValueError as exc:
            INFO(f"[PASS] check_list 非法写法被拒绝（{desc}）：{exc}")
            return
        raise RuntimeError(f"场景 5 未按预期失败（{desc}）：应当抛 ValueError")

    _expect_value_error("缺 file", check_list=[{"method": "kv"}])
    _expect_value_error("未知键", check_list=[{"file": "a.json", "who": 1}])
    _expect_value_error("method 与 compare 互斥", check_list=[
        {"file": "a.json", "method": "kv", "compare": lambda p, g: []}])
    _expect_value_error("绝对路径", check_list=["/abs/a.json"])
    _expect_value_error("路径分隔符", check_list=["sub/a.json"])
    _expect_value_error(".. 路径", check_list=["../a.json"])
    _expect_value_error("缺扩展名", check_list=["noext"])
    _expect_value_error("同一清单 file 重复", check_list=["a.json", {"file": "a.json"}])
    _expect_value_error("预设名不存在", check_list=[{"file": "a.json", "method": "no_such"}])

    # ── 场景 6：重录单元规则（不启动 fly，直接调用重录函数）──
    tmp_root = os.path.join(FLY_CASE_LOG_DIR, "logs", "regold_selftest")
    if os.path.isdir(tmp_root):
        shutil.rmtree(tmp_root)
    os.makedirs(tmp_root)

    def _write_result(name, obj):
        with open(os.path.join(tmp_root, name), "w", encoding="utf-8") as f:
            f.write(json.dumps(obj, ensure_ascii=False, indent=2) + "\n")

    S6 = "selftest"  # 场景 6 不经过调度函数，手动指定命名约定主题 S
    _write_result("selftest.result.json", {"a": 1, "v": True})
    pending, _notes = qa_record.regold_result_files(
        [qa_record.validate_check_item("result.json", S6)], tmp_root, 0.0, S6)
    assert pending == {"selftest.result.json": {
        "gold_rel": "selftest.gold", "value": {"a": 1, "v": True}}}, pending

    # 嵌套布尔是数据，false 可以重录（顶层布尔才是判定型条目）
    _write_result("selftest.nested.json", {"g": {"flag": False}})
    pending, _notes = qa_record.regold_result_files(
        [qa_record.validate_check_item("nested.json", S6)], tmp_root, 0.0, S6)
    assert pending == {"selftest.nested.json": {
        "gold_rel": "selftest.nested.gold", "value": {"g": {"flag": False}}}}, pending

    _write_result("selftest.bad.json", {"v": False})
    try:
        qa_record.regold_result_files(
            [qa_record.validate_check_item("bad.json", S6)], tmp_root, 0.0, S6)
        raise RuntimeError("场景 6 未按预期失败：顶层判定型条目为假应当拒绝重录")
    except qa_record.RegoldRejected as exc:
        assert "不能重录" in str(exc), str(exc)

    with open(os.path.join(tmp_root, "selftest.m.log"), "w", encoding="utf-8") as f:
        f.write("some text")
    pending, _notes = qa_record.regold_result_files(
        [qa_record.validate_check_item({"file": "m.log", "method": "contains"}, S6)],
        tmp_root, 0.0, S6)
    assert pending == {}, pending  # contains 是手工维护项（manual 缺省 true），不重录

    # manual=True 显式声明手工维护：即使 kv 可重录也不写基准（E3）
    _write_result("selftest.manual.json", {"a": 1})
    pending, _notes = qa_record.regold_result_files(
        [qa_record.validate_check_item({"file": "manual.json", "manual": True}, S6)],
        tmp_root, 0.0, S6)
    assert pending == {}, pending

    pending, _notes = qa_record.regold_result_files(
        [qa_record.validate_check_item("nope.json", S6)], tmp_root, 0.0, S6)
    assert pending == {}, pending  # 结果文件不存在，旧基准不动

    shutil.rmtree(tmp_root)

    # ── 场景 7：expect_pass=False 的子阶段带 check_list（失败时刻的状态可比对）──
    try:
        run_subcase("runner_check_fail_wrong.py", timeout=15,
                    expect_pass=False, check_list=["result.json"])
        raise RuntimeError("场景 7 未按预期失败：结果与基准不符应当抛 SubcaseError")
    except SubcaseError as exc:
        text = str(exc)
        assert "值不一致" in text, text
        # 预期失败达成（fly FAILED 正是 expect_pass=False 的期望），报告里
        # 不应出现退出状态问题行
        assert "expected" not in text, text

    # ── 场景 8：退出状态与比对差异同时失败 → 报告里两者都在 ──
    try:
        run_subcase("runner_check_fail_wrong2.py", timeout=15, check_list=["result.json"])
        raise RuntimeError("场景 8 未按预期失败：非零退出且结果不符应当抛 SubcaseError")
    except SubcaseError as exc:
        text = str(exc)
        assert "expected PASSED but got FAILED" in text, text
        assert "值不一致" in text, text

    # ── 差异报告落盘断言：追加写、分段（E8）──
    check_path = os.path.join(FLY_CASE_LOG_DIR, "test_runner_check.check.txt")
    assert os.path.isfile(check_path), check_path
    with open(check_path, encoding="utf-8") as f:
        check_text = f.read()
    for frag in ("判定不成立", "结果文件缺失", "基准缺失",
                 "===== sub case runner_check_wrong.py =====",
                 "===== sub case runner_check_silent.py =====",
                 "===== sub case runner_check_nogold.py =====",
                 "===== sub case runner_check_fail_wrong.py =====",
                 "===== sub case runner_check_fail_wrong2.py ====="):
        assert frag in check_text, f"check 报告缺少 {frag!r}"

    # ── 场景 9：重录的调度分支与收尾落盘（依赖线程局部的模式变量 E2）──
    # 在 logs/ 下构造临时编排脚本与结果文件（用例发现机制跳过 logs/），直接
    # 调用运行器的单用例重录函数。放在所有 run_subcase 场景之后：_regold_one
    # 会重写本线程的 case 上下文。
    tmp_case_dir = os.path.join(FLY_CASE_LOG_DIR, "logs", "regold_case")
    if os.path.isdir(tmp_case_dir):
        shutil.rmtree(tmp_case_dir)
    # _regold_one 的用例日志目录是「编排脚本所在目录/<stem>」，结果文件必须
    # 放在同一布局下（tmp_case_dir/tmp_regold_case/0_tmp_regold_stage/）。
    sub_log_dir = os.path.join(tmp_case_dir, "tmp_regold_case", "0_tmp_regold_stage")
    os.makedirs(sub_log_dir)
    result_path = os.path.join(sub_log_dir, "tmp_regold_stage.result.json")
    with open(result_path, "w", encoding="utf-8") as f:
        f.write('{\n  "answer": 42\n}\n')
    with open(os.path.join(tmp_case_dir, "tmp_regold_stage.py"), "w",
              encoding="utf-8") as f:
        f.write('from test import record\n\nrecord("answer", 42)\n')
    with open(os.path.join(tmp_case_dir, "tmp_regold_case.pyt"), "w",
              encoding="utf-8") as f:
        f.write('run_subcase("tmp_regold_stage.py", timeout=15, check_list=["result.json"])\n')

    runner = sys.modules["__main__"]
    tmp_pyt = os.path.join(tmp_case_dir, "tmp_regold_case.pyt")
    # 基准目录是「编排脚本所在目录/gold/」；结果文件目录才带 <stem> 层。
    gold_path = os.path.join(tmp_case_dir, "gold", "tmp_regold_stage.gold")

    assert runner._regold_one(tmp_pyt), "重录单用例应成功"
    assert os.path.isfile(gold_path), f"基准未按命名约定生成：{gold_path}"
    with open(gold_path, encoding="utf-8") as f:
        assert '"answer": 42' in f.read()

    # 结果不变再调用 → 基准文件修改时间不变（内容无变化不写盘）
    mtime_before = os.path.getmtime(gold_path)
    assert runner._regold_one(tmp_pyt)
    assert os.path.getmtime(gold_path) == mtime_before, "结果不变时基准不应重写"

    # 结果变化再调用 → 基准更新
    with open(result_path, "w", encoding="utf-8") as f:
        f.write('{\n  "answer": 43\n}\n')
    assert runner._regold_one(tmp_pyt)
    with open(gold_path, encoding="utf-8") as f:
        assert '"answer": 43' in f.read()

    shutil.rmtree(tmp_case_dir)
    INFO("[PASS] test_runner_check: 全部场景符合预期")
