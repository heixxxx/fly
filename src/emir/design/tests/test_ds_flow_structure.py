"""build_design_db 三段式结构单测（dev-rules §3「建库 API 流程标准」）。

master 侧提交面断言（§20 编排判据）：
  - 提交任务数恰 2（flow 根任务 + freeze）——O(1)，与输入规模无关；
  - freeze 依赖恰为固定标记 verify_report（权威终态锚），不含运行时
    规模临时键（__dsn__ 前缀——运行时键清理责任已单化到链上任务）；
  - 根任务依赖锚 = alpha_settings + lib 库对象（入口等待语义）；
  - 预处理段不读文件内容：非 lef/def 内容文件不阻止提交（嗅探已下放
    文件首个解析任务）。

lib 快照搬运层拆除断言（§21「db 数据所有权：禁跨 db 数据搬运」）：
  - 根任务体零写入——不再产出 __dsn__lib_library 中转对象，design db
    内无 lib db 数据副本；
  - lib merge 任务 inputs 恰锚 design db merged 对象 + lib db 库对象
    全名（跨 db 直读——无 design db 中转键）。

蕴含链（freeze 不可能早于任一正式产物）由 QA 全量回归行为验证：
freeze ⟸ verify_report ⟸ 全局校验 inputs（各分区校验结果 + pg_nets +
id 映射段表 + DSDesign 重写版 + per-DEF 正式产物与伴生名）。
"""

DB_PATH = "/proj/design"
LIB_PATH = "/proj/lib"


class _FakeInner:
    def get_db_path(self):
        return DB_PATH

    def get_data_path(self):
        return DB_PATH + "/data"


class _FakeDesignDb:
    _db = _FakeInner()

    def get_uid(self):
        return None

    def get_full_name(self, name):
        return f"{DB_PATH}:{name}"

    def write_object(self, *args, **kwargs):
        pass

    def read_object(self, name):
        from emir.design.py.alpha_settings import get_default_alpha_settings
        return get_default_alpha_settings()


class _FakeLibDb:
    LIBRARY_OBJ = "library"

    def get_full_name(self, name):
        return f"{LIB_PATH}:{name}"


class _FakeProject:
    def _create_db(self, *args, **kwargs):
        return _FakeDesignDb()


class _RecordingDesignDb(_FakeDesignDb):
    """同 _FakeDesignDb + 记录写面对象名（根任务体零写入断言）。"""

    def __init__(self):
        self.written = []

    def write_object(self, name, *args, **kwargs):
        self.written.append(name)


class _CaptureAgent:
    mode = "capture"

    def __init__(self):
        self.submitted = []

    def submit(self, task_name, module, serialized, task_inputs, **kwargs):
        self.submitted.append((task_name, task_inputs))


def _build():
    """在捕获 agent 下跑 build_design_db，返回 (db, submitted)。"""
    import tempfile
    import fly.runtime
    from emir.design import build_design_db

    paths = []
    for _ in range(3):  # 1 tech lef + 2 cell lef；2 def
        f = tempfile.NamedTemporaryFile(suffix=".lef", delete=False)
        f.write(b"not a lef file at all")  # master 预处理段不得读内容
        f.close()
        paths.append(f.name)
    def_paths = []
    for _ in range(2):
        f = tempfile.NamedTemporaryFile(suffix=".def", delete=False)
        f.write(b"not a def file at all")
        f.close()
        def_paths.append(f.name)

    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    try:
        db = build_design_db(_FakeProject(), name="design",
                             def_paths=def_paths, lef_paths=paths,
                             lib_db=_FakeLibDb())
    finally:
        fly.runtime.get_agent = original
    return db, agent.submitted


def test_master_submits_two_tasks_only():
    _, submitted = _build()
    names = [name for name, _ in submitted]
    assert names == ["_design_flow_task", "_freeze_design_task"], names


def test_freeze_inputs_are_fixed_markers():
    _, submitted = _build()
    freeze_inputs = submitted[1][1]
    assert freeze_inputs == [f"{DB_PATH}:verify_report"], freeze_inputs
    assert not any("__dsn__" in k for k in freeze_inputs), \
        "freeze dependency must not carry runtime-scale temp keys"


def test_root_task_inputs_anchor_lib_and_alpha():
    _, submitted = _build()
    root_inputs = submitted[0][1]
    assert root_inputs == [f"{DB_PATH}:alpha_settings",
                           f"{LIB_PATH}:library"], root_inputs


def test_preprocess_does_not_read_file_content():
    """非 lef/def 内容文件不阻止提交（入口只查存在性——嗅探在文件首个
    解析任务内失败透出，库不冻结）。"""
    db, submitted = _build()
    assert db is not None and len(submitted) == 2


def _run_root_body(n_lefs=2, n_defs=2):
    """直跑根任务函数体（绕过 as_task 提交——体内嵌套任务仍经捕获
    agent 提交），返回 (design db, submitted)。"""
    import fly.runtime
    from emir.design.py import ds_flow

    db = _RecordingDesignDb()
    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    try:
        ds_flow._design_flow_task._fly_original_func(
            db, [f"f{i}.lef" for i in range(n_lefs)],
            [f"f{i}.def" for i in range(n_defs)], _FakeLibDb())
    finally:
        fly.runtime.get_agent = original
    return db, agent.submitted


def test_root_task_writes_no_lib_snapshot():
    """根任务体零写入（§21 lib 快照搬运层拆除）：__dsn__lib_library
    中转对象不复存在——lib 数据不经 design db 搬运，lib merge 任务直
    接锚/读 lib db；下游任务族提交保持（merge lib 仍在链上）。"""
    db, submitted = _run_root_body()
    assert db.written == [], \
        f"root task body must not write to design db: {db.written}"
    names = [name for name, _ in submitted]
    assert "_merge_lib_task" in names, names


def test_merge_lib_task_inputs_anchor_lib_full_name():
    """lib merge 任务 inputs 恰锚 design db merged 对象 + lib db 库对
    象全名（跨 db 直读——无 design db 中转键，§21）。"""
    import fly.runtime
    from emir.design.py.ds_flow import _merge_lib_task

    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    try:
        _merge_lib_task(_FakeDesignDb(), "__dsn__merged", _FakeLibDb(),
                        "tables", "__dsn__lib_merged_design")
    finally:
        fly.runtime.get_agent = original
    assert len(agent.submitted) == 1, agent.submitted
    name, inputs = agent.submitted[0]
    assert name == "_merge_lib_task", name
    assert inputs == [f"{DB_PATH}:__dsn__merged", f"{LIB_PATH}:library"], \
        inputs


if __name__ == "__main__":
    test_master_submits_two_tasks_only()
    test_freeze_inputs_are_fixed_markers()
    test_root_task_inputs_anchor_lib_and_alpha()
    test_preprocess_does_not_read_file_content()
    test_root_task_writes_no_lib_snapshot()
    test_merge_lib_task_inputs_anchor_lib_full_name()
    print("[PASS] test_ds_flow_structure")
