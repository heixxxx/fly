"""build_timing_db 三段式结构单测（dev-rules §3「建库 API 流程标准」）。

master 侧提交面断言（§20 编排判据）：
  - 提交任务数恰 2（flow 根任务 + freeze）——O(1)，与输入规模无关；
  - freeze 依赖恰为固定标记 clocks/summary，不含运行时规模临时键
    （__tmg__ 前缀——运行时键清理责任已单化到链上任务）；
  - 根任务依赖锚 = design db 必要对象 + alpha_settings（入口等待语义）；
  - 预处理段不读文件内容：非 TWF 内容/空文件不阻止提交（嗅探已下放
    文件入口任务）。

快照搬运层拆除断言（§21「db 数据所有权：禁跨 db 数据搬运」，2026-09-18
用户裁定）：
  - 根任务体零写入——不再产出 __tmg__ 快照对象族与快照键清单对象，
    timing db 内无 design db 数据副本（对象名空间检查）；
  - 块解析任务 inputs 恰锚 design db 六类对象全名（跨 db 直读——无
    timing db 中转键），枚举锚 names_count/INST 段号/分区表随参传递。

蕴含链（freeze 不可能早于任一正式产物）由 QA 全量回归行为验证：
freeze ⟸ summary ⟸ 汇总 inputs（全部切片 + 各分区冲突计数）⟹ 分区
对象/时钟表先写定。
"""

DB_PATH = "/proj/timing"
DESIGN_PATH = "/proj/design"


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


class _RecordingTimingDb(_FakeTimingDb):
    """同 _FakeTimingDb + 记录写面对象名（根任务体零写入断言）。"""

    def __init__(self):
        self.written = []

    def write_object(self, name, *args, **kwargs):
        self.written.append(name)


class _FakeDesignDb:
    DESIGN_OBJ = "DSDesign"

    def get_full_name(self, name):
        return f"{DESIGN_PATH}:{name}"


class _FakePart:
    def __init__(self, pid, xp, yp):
        self.partition_id = pid
        self.xp = xp
        self.yp = yp


class _FakeDesignObj:
    """DSDesign 容器占位（根任务体只消费分区表枚举面）。"""

    partition_count = 1

    def partition_at(self, i):
        return _FakePart(7, 1, 2)


class _FakeInstIndex:
    """id_partition_map.INST 段表占位（id_starts 升序）。"""

    id_starts = [0]


class _FakeNamesObj:
    """DSBlockNames_<i> 占位（名字 mapper 已打桩，不消费内容）。"""


class _RootDesignDb:
    """根任务体直跑的 design db 面：read_object 按名分派 + build_meta
    锚（§19 确定性消费——def_count=2）。"""

    DESIGN_OBJ = "DSDesign"
    BUILD_META_OBJ = "build_meta"
    DEF_COUNT = 2

    def get_full_name(self, name):
        return f"{DESIGN_PATH}:{name}"

    def load_build_meta(self):
        return {"def_count": self.DEF_COUNT}

    def read_object(self, name):
        if name == self.DESIGN_OBJ:
            return _FakeDesignObj()
        if name == "id_partition_map.INST":
            return _FakeInstIndex()
        return _FakeNamesObj()


class _StubMapper:
    """ds_make_instance_name_mapper 打桩（真实 C++ mapper 需要真实
    design 对象；结构断言不消费换算面——kind=0 文件不触发绑定校验）。"""

    def get_global_id(self, name):
        return None


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


def _run_root_body(files):
    """直跑根任务函数体（绕过 as_task 提交——体内嵌套任务仍经捕获
    agent 提交），返回 (timing db, submitted)。名字 mapper 打桩。"""
    import fly.runtime
    from emir.timing.py import tm_flow

    db = _RecordingTimingDb()
    agent = _CaptureAgent()
    original_agent = fly.runtime.get_agent
    original_mapper = tm_flow.ds_make_instance_name_mapper
    fly.runtime.get_agent = lambda: agent
    tm_flow.ds_make_instance_name_mapper = lambda design, names: _StubMapper()
    try:
        tm_flow._timing_flow_task._fly_original_func(db, _RootDesignDb(),
                                                     files)
    finally:
        fly.runtime.get_agent = original_agent
        tm_flow.ds_make_instance_name_mapper = original_mapper
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


def test_root_task_writes_no_snapshot_objects():
    """根任务体零写入（§21 快照搬运层拆除）：__tmg__ 快照对象族与快
    照键清单对象不复存在；design db 六类对象名一个都不出现在 timing
    db 写面（对象名空间检查——design 数据零副本）；下游编排提交保持
    （文件入口 + 合并链编排）。"""
    files = [{"file_name": f"f{i}.twf", "kind": 0, "block_inst": "",
              "block_cell": "", "strip_prefix": ""} for i in range(2)]
    db, submitted = _run_root_body(files)
    assert db.written == [], \
        f"root task body must not write to timing db: {db.written}"
    assert not any(k.startswith("__tmg__") for k in db.written), db.written
    design = _RootDesignDb()
    design_namespace = [
        design.DESIGN_OBJ, "pg_nets", "global_density",
        design.BUILD_META_OBJ, "id_partition_map.INST",
        "id_partition_map.INST.S0", "DSBlockNames_0", "DSBlockNames_1",
        "PART_1_2.NETS",
    ]
    assert not (set(db.written) & set(design_namespace)), db.written
    names = [name for name, _ in submitted]
    assert names == ["_plan_file_chunks_task"] * len(files) + \
        ["_plan_merge_chain_task"], names


def test_chunk_task_inputs_anchor_design_db():
    """块解析任务 inputs 恰锚 design db 六类对象全名（跨 db 直读，
    §21）——无 timing db 中转键；枚举锚（names_count/INST 段号/分区
    表）随参展开为确定性全名序列。"""
    import fly.runtime
    from emir.timing.py.tm_flow import _parse_chunk_task

    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    try:
        _parse_chunk_task(_FakeTimingDb(), _RootDesignDb(), "f.twf",
                          0, 10, 10, 20, 0, 0, 0, "", "", "",
                          "__tmg__chunk_0_0", 2, [0, 3], [(7, 1, 2)])
    finally:
        fly.runtime.get_agent = original
    assert len(agent.submitted) == 1, agent.submitted
    name, inputs = agent.submitted[0]
    assert name == "_parse_chunk_task", name
    expected = [
        f"{DESIGN_PATH}:DSDesign",
        f"{DESIGN_PATH}:id_partition_map.INST",
        f"{DESIGN_PATH}:pg_nets",
        f"{DESIGN_PATH}:DSBlockNames_0",
        f"{DESIGN_PATH}:DSBlockNames_1",
        f"{DESIGN_PATH}:id_partition_map.INST.S0",
        f"{DESIGN_PATH}:id_partition_map.INST.S3",
        f"{DESIGN_PATH}:PART_1_2.NETS",
    ]
    assert inputs == expected, inputs


if __name__ == "__main__":
    test_master_submits_two_tasks_only()
    test_freeze_inputs_are_fixed_markers()
    test_root_task_inputs_anchor_design_db()
    test_preprocess_does_not_read_file_content()
    test_root_task_writes_no_snapshot_objects()
    test_chunk_task_inputs_anchor_design_db()
    print("[PASS] test_tm_flow_structure")
