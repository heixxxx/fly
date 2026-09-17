"""build_timing_db 三段式结构单测（dev-rules §3「建库 API 流程标准」）。

master 侧提交面断言（§20 编排判据）：
  - 提交任务数恰 2（flow 根任务 + freeze）——O(1)，与输入规模无关；
  - freeze 依赖恰为固定标记 clocks/summary，不含运行时规模临时键
    （__tmg__ 前缀——运行时键清理责任已单化到链上任务）；
  - 根任务依赖锚 = design db 必要对象 + alpha_settings（入口等待语义）；
  - 预处理段不读文件内容：非 TWF 内容/空文件不阻止提交（嗅探已下放
    文件入口任务）。

蕴含链（freeze 不可能早于任一正式产物）由 QA 全量回归行为验证：
freeze ⟸ summary ⟸ 汇总 inputs（全部切片 + 各分区冲突计数）⟹ 分区
对象/时钟表先写定。
"""

DB_PATH = "/proj/timing"


class _FakeInner:
    def get_db_path(self):
        return DB_PATH

    def get_data_path(self):
        return DB_PATH + "/data"


class _FakeTimingDb:
    """吸收 master 侧 db 面调用（write/read/get_full_name）。"""

    _db = _FakeInner()

    def get_uid(self):
        return None

    def get_full_name(self, name):
        return f"{DB_PATH}:{name}"

    def write_object(self, *args, **kwargs):
        pass

    def read_object(self, name):
        from emir.timing.py.alpha_settings import get_default_alpha_settings
        return get_default_alpha_settings()


class _FakeDesignDb:
    DESIGN_OBJ = "DSDesign"

    def get_full_name(self, name):
        return f"/proj/design:{name}"


class _FakeProject:
    def _create_db(self, *args, **kwargs):
        return _FakeTimingDb()


class _CaptureAgent:
    mode = "capture"

    def __init__(self):
        self.submitted = []

    def submit(self, task_name, module, serialized, task_inputs, **kwargs):
        self.submitted.append((task_name, task_inputs))


def _build(timing_files):
    """在捕获 agent 下跑 build_timing_db，返回 (db, submitted)。"""
    import tempfile
    import fly.runtime
    from emir.timing import build_timing_db

    paths = []
    for item in timing_files:
        f = tempfile.NamedTemporaryFile(suffix=".twf", delete=False)
        # 内容为非 TWF 垃圾字节——master 预处理段不得读内容（嗅探下放
        # 文件入口任务）
        f.write(b"library (typ) { garbage }")
        f.close()
        paths.append(item.format(path=f.name) if isinstance(item, str)
                     else dict(item, file_name=f.name))

    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    try:
        db = build_timing_db(_FakeProject(), name="timing",
                             timing_files=paths, design_db=_FakeDesignDb())
    finally:
        fly.runtime.get_agent = original
    return db, agent.submitted


def test_master_submits_two_tasks_only():
    _, submitted = _build(["{path}"])
    names = [name for name, _ in submitted]
    assert names == ["_timing_flow_task", "_freeze_timing_task"], names


def test_freeze_inputs_are_fixed_markers():
    _, submitted = _build(["{path}"])
    freeze_inputs = submitted[1][1]
    assert freeze_inputs == [f"{DB_PATH}:clocks", f"{DB_PATH}:summary"], \
        freeze_inputs
    assert not any("__tmg__" in k for k in freeze_inputs), \
        "freeze dependency must not carry runtime-scale temp keys"


def test_root_task_inputs_anchor_design_db():
    _, submitted = _build(["{path}"])
    root_inputs = submitted[0][1]
    design = _FakeDesignDb()
    expected = [
        design.get_full_name("DSDesign"),
        design.get_full_name("global_density"),
        design.get_full_name("pg_nets"),
        design.get_full_name("id_partition_map.INST"),
        design.get_full_name("build_meta"),
        f"{DB_PATH}:alpha_settings",
    ]
    assert root_inputs == expected, root_inputs


def test_preprocess_does_not_read_file_content():
    """非 TWF 内容文件不阻止提交（入口只查存在性——嗅探在文件入口
    任务内失败透出，库不冻结）。"""
    db, submitted = _build(["{path}", "{path}"])
    assert db is not None and len(submitted) == 2


if __name__ == "__main__":
    test_master_submits_two_tasks_only()
    test_freeze_inputs_are_fixed_markers()
    test_root_task_inputs_anchor_design_db()
    test_preprocess_does_not_read_file_content()
    print("[PASS] test_tm_flow_structure")
