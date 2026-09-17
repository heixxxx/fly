"""build_lib_db 三段式结构单测（dev-rules §3「建库 API 流程标准」）。

master 侧提交面断言（§20 编排判据）：
  - 提交任务数恰 2（flow 根任务 + freeze）——O(1)，与输入规模无关；
  - freeze 依赖恰为固定标记 LIBLibrary，不含运行时规模临时键
    （__mr__ 前缀——MapReduce 中间键清理责任在框架 cleanup 任务，随
    job 提交、依赖 library 写定后调度）；
  - 根任务无数据依赖锚（文件路径集随任务参数传递，MapReduce 任务图
    形状由根任务体内的 job 运行时自定）；
  - 预处理段不读文件内容：非 liberty 内容 .lib 不阻止提交（嗅探已下放
    map 解析任务，文件形态错由该任务失败透出）。

蕴含链（freeze 不可能早于任一正式产物）由 QA 全量回归行为验证：
freeze ⟸ LIBLibrary ⟸ MapReduce finalizer（唯一写定者）。
"""

DB_PATH = "/proj/lib"


class _FakeInner:
    def get_db_path(self):
        return DB_PATH

    def get_data_path(self):
        return DB_PATH + "/data"


class _FakeLibDb:
    """吸收 master 侧 db 面调用（write/read/get_full_name）。"""

    _db = _FakeInner()

    def get_uid(self):
        return None

    def get_full_name(self, name):
        return f"{DB_PATH}:{name}"

    def write_object(self, *args, **kwargs):
        pass

    def read_object(self, name):
        raise AssertionError("lib preprocess must not read db objects")


class _FakeProject:
    def _create_db(self, *args, **kwargs):
        return _FakeLibDb()


class _CaptureAgent:
    mode = "capture"

    def __init__(self):
        self.submitted = []

    def submit(self, task_name, module, serialized, task_inputs, **kwargs):
        self.submitted.append((task_name, task_inputs))


def _build(lib_paths):
    """在捕获 agent 下跑 build_lib_db，返回 (db, submitted)。"""
    import tempfile
    import fly.runtime
    from emir.lib import build_lib_db

    paths = []
    for item in lib_paths:
        f = tempfile.NamedTemporaryFile(suffix=".lib", delete=False)
        # 内容为非 liberty 垃圾文本——master 预处理段不得读内容（嗅探
        # 下放 map 解析任务）
        f.write(b"VERSION 5.8 ;\nUNITS\n this is not a liberty file\n")
        f.close()
        paths.append(item.format(path=f.name))

    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    try:
        db = build_lib_db(_FakeProject(), name="lib", lib_paths=paths)
    finally:
        fly.runtime.get_agent = original
    return db, agent.submitted


def test_master_submits_two_tasks_only():
    _, submitted = _build(["{path}"])
    names = [name for name, _ in submitted]
    assert names == ["_lib_flow_task", "_freeze_lib_task"], names


def test_freeze_inputs_are_fixed_markers():
    _, submitted = _build(["{path}"])
    freeze_inputs = submitted[1][1]
    assert freeze_inputs == [f"{DB_PATH}:LIBLibrary"], freeze_inputs
    assert not any("__mr__" in k for k in freeze_inputs), \
        "freeze dependency must not carry MapReduce runtime temp keys"


def test_root_task_has_no_data_anchors():
    _, submitted = _build(["{path}"])
    assert submitted[0][1] == [], submitted[0][1]


def test_preprocess_does_not_read_file_content():
    """非 liberty 内容文件不阻止提交（入口只查存在性——嗅探在 map 解析
    任务内失败透出，库不冻结）。"""
    db, submitted = _build(["{path}", "{path}"])
    assert db is not None and len(submitted) == 2


if __name__ == "__main__":
    test_master_submits_two_tasks_only()
    test_freeze_inputs_are_fixed_markers()
    test_root_task_has_no_data_anchors()
    test_preprocess_does_not_read_file_content()
    print("[PASS] test_lib_flow_structure")
