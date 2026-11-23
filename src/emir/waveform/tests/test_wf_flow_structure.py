"""build_waveform_db 三段式结构单测（dev-rules §3「建库 API 流程标准」，
同 test_tm_flow_structure.py 先例）。

master 侧提交面断言（§20 编排判据）：
  - 提交任务数恰 2（flow 根任务 + freeze）——O(1)，与输入规模无关；
  - freeze 依赖恰为固定标记 summary，不含运行时规模临时键（__wfg__
    前缀——运行时键清理责任已单化到链上任务）；
  - 根任务依赖锚 = design db 必要对象 + alpha_settings（入口等待语义）；
  - 预处理段不读文件内容：非 VCD 内容/空文件不阻止提交（嗅探已下放
    文件入口任务）；
  - settings-only 模式（vcd_files=None + settings.activity）同样两任务。

根任务体双模式目录断言：
  - VCD 模式根任务体零写入（§21 快照搬运层拆除——design db 数据零副本；
    var 表 temp 写入发生在文件入口任务体，不在根任务体）；
    提交面 = 每有效文件一个文件入口任务 + 合并链编排任务；
  - settings-only 模式根任务体提交面 = 每分区一物化任务 + settings 汇
    总任务（零临时清理键残留由汇总任务单点负责——结构面只断提交形状）；
  - 块解析任务 inputs = var 表 temp 键全名（裁定 IV-4：db 按引用共享，
    不随任务参数复制）+ design db 对象全名（跨 db 直读），枚举锚
    names_count/INST 段号/分区表随参传递。

任务体直跑断言：
  - 文件入口任务体：var 表写 db temp + manifest 携带 chunk_count/
    vars_key/scratch_dir；块任务提交参数带 vars_key；gzip 输入解压副
    本进链（裁定 IV-1——副本名带文件序，明文零开销直通）；
  - 合并任务体：design db 直读组装 ctx（wf_merge_partition ctx 首参）
    + 混合模式 overrides 空时零覆盖直通 + counts 携带覆盖条目字段；
  - 汇总任务体：运行时键（切片/var 表/计数/清单）与 gzip 解压目录单
    点清理 + summary 唯一写定。
"""

DB_PATH = "/proj/waveform"
DESIGN_PATH = "/proj/design"


class _FakeInner:
    def get_db_path(self):
        return DB_PATH

    def get_data_path(self):
        return DB_PATH + "/data"


class _FakeWaveformDb:
    """吸收 master 侧 db 面调用（write/read/get_full_name）。"""

    _db = _FakeInner()

    def get_uid(self):
        return None

    def get_db_path(self):
        return DB_PATH

    def get_full_name(self, name):
        return f"{DB_PATH}:{name}"

    def write_object(self, *args, **kwargs):
        pass

    def read_object(self, name):
        from emir.waveform.py.alpha_settings import (
            get_default_alpha_settings)
        return get_default_alpha_settings()

    def remove_object(self, name):
        pass


class _RecordingWaveformDb(_FakeWaveformDb):
    """同 _FakeWaveformDb + 记录写面对象名（根任务体零写入断言）。"""

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
    """DSDesign 容器占位（根任务体只消费分区表枚举面 + find_cell）。"""

    partition_count = 2

    def partition_at(self, i):
        return _FakePart(i, i, 0)

    def find_cell(self, name):
        return object() if name == "blk" else None


class _FakeInstIndex:
    """id_partition_map.INST 段表占位（id_starts 升序）。"""

    id_starts = [0, 3]


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


class _InstancesObj:
    """分区 INSTANCES 占位（settings-only 物化任务的实例枚举面）。"""

    def ids(self):
        return [5, 3]


class _MaterializeDesignDb:
    """物化任务直跑的 design db 面：真实 C++ 空对象供 EXWFDesignContext
    注入（结构断言不消费换算面——overrides 为空，物化换算路径由
    wf_partition_test 的 SettingsOnlyMaterialize 覆盖）；读面对象名记录
    ——INSTANCES 枚举源断言用。"""

    DESIGN_OBJ = "DSDesign"
    PARTITIONS = [(7, 1, 2)]

    def __init__(self):
        self.read_names = []

    def get_full_name(self, name):
        return f"{DESIGN_PATH}:{name}"

    def read_object(self, name):
        from emir.design import (EXDSDesign, EXDSIdPartitionIndex,
                                 EXDSPartitionNets, EXDSPgNetSet)
        self.read_names.append(name)
        if name == self.DESIGN_OBJ:
            return EXDSDesign()
        if name == "pg_nets":
            return EXDSPgNetSet()
        if name == "id_partition_map.INST":
            # ctx 组装读段表注入（EXWFDesignContext.set_inst_id_map 要
            # 求真实 C++ 对象；空段号集不消费段对象）
            return EXDSIdPartitionIndex()
        if name.endswith(".INSTANCES"):
            return _InstancesObj()
        if name.endswith(".NETS"):
            return EXDSPartitionNets()
        raise KeyError(name)


class _StubMapper:
    """ds_make_instance_name_mapper 打桩（真实 C++ mapper 需要真实
    design 对象；结构断言不消费换算面）。get_global_id 返回值可指定
    ——kind=1 绑定校验分支的命中/未命中形态均可控。"""

    def __init__(self, inst_id=None):
        self._inst_id = inst_id

    def get_global_id(self, name):
        return self._inst_id


class _FakeProject:
    def _create_db(self, *args, **kwargs):
        return _FakeWaveformDb()


class _CaptureAgent:
    mode = "capture"

    def __init__(self):
        self.submitted = []

    def submit(self, task_name, module, serialized, task_inputs, **kwargs):
        self.submitted.append((task_name, task_inputs))


def _build(vcd_files, settings=None):
    """在捕获 agent 下跑 build_waveform_db，返回 (db, submitted)。"""
    import tempfile
    import fly.runtime
    from emir.waveform import build_waveform_db

    paths = []
    for item in (vcd_files or []):
        f = tempfile.NamedTemporaryFile(suffix=".vcd", delete=False)
        # 内容为非 VCD 垃圾字节——master 预处理段不得读内容（嗅探下放
        # 文件入口任务）
        f.write(b"not a vcd at all")
        f.close()
        paths.append(item.format(path=f.name) if isinstance(item, str)
                     else dict(item, file_name=f.name))

    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    try:
        db = build_waveform_db(_FakeProject(), name="waveform",
                               design_db=_FakeDesignDb(),
                               vcd_files=(paths if vcd_files else None),
                               settings=settings)
    finally:
        fly.runtime.get_agent = original
    return db, agent.submitted


def _run_root_body(files, meta_params=None, overrides=None, duration_fs=0,
                   mapper=None):
    """直跑根任务函数体（绕过 as_task 提交——体内嵌套任务仍经捕获
    agent 提交），返回 (waveform db, submitted)。名字 mapper 打桩
    （默认 get_global_id 返回 None）。"""
    import fly.runtime
    from emir.waveform.py import wf_flow

    db = _RecordingWaveformDb()
    agent = _CaptureAgent()
    original_agent = fly.runtime.get_agent
    original_mapper = wf_flow.ds_make_instance_name_mapper
    fly.runtime.get_agent = lambda: agent
    wf_flow.ds_make_instance_name_mapper = \
        lambda design, names: (mapper if mapper is not None
                               else _StubMapper())
    try:
        wf_flow._waveform_flow_task._fly_original_func(
            db, _RootDesignDb(), files,
            meta_params or {"default_toggle_rate": 0.0,
                            "default_static_probability": 0.5,
                            "duration_sec": 1.0,
                            "x_z_handling": "denominator_include"},
            overrides or [], duration_fs)
    finally:
        fly.runtime.get_agent = original_agent
        wf_flow.ds_make_instance_name_mapper = original_mapper
    return db, agent.submitted


ACTIVITY_STRUCT = {"default": {"toggle_rate": 2.0e6,
                               "static_probability": 0.5}}


def test_master_submits_two_tasks_only():
    _, submitted = _build(["{path}"])
    names = [name for name, _ in submitted]
    assert names == ["_waveform_flow_task", "_freeze_waveform_task"], names


def test_master_submits_two_tasks_settings_only():
    _, submitted = _build(None, settings={"activity": ACTIVITY_STRUCT})
    names = [name for name, _ in submitted]
    assert names == ["_waveform_flow_task", "_freeze_waveform_task"], names


def test_freeze_inputs_are_fixed_markers():
    _, submitted = _build(["{path}"])
    freeze_inputs = submitted[1][1]
    assert freeze_inputs == [f"{DB_PATH}:summary"], freeze_inputs
    assert not any("__wfg__" in k for k in freeze_inputs), \
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
    """非 VCD 内容文件不阻止提交（入口只查存在性——嗅探在文件入口
    任务内失败透出，库不冻结）。"""
    db, submitted = _build(["{path}", "{path}"])
    assert db is not None and len(submitted) == 2


def test_root_task_vcd_mode_chain_shape():
    """VCD 模式根任务体零写入（§21：design db 数据零副本）；提交面 =
    每有效文件一个文件入口任务 + 合并链编排任务（task 命名模式与体内
    目录逐行对应）。kind=2 绑定校验打桩命中。"""
    files = [{"file_name": f"f{i}.vcd", "kind": 2, "block_inst": "",
              "block_cell": "blk", "strip_prefix": ""} for i in range(2)]
    db, submitted = _run_root_body(files)
    assert db.written == [], \
        f"root task body must not write to waveform db: {db.written}"
    names = [name for name, _ in submitted]
    assert names == ["_wf_file_entry_task", "_wf_file_entry_task",
                     "_wf_plan_merge_chain_task"], names


def test_root_task_all_invalid_binding_raises():
    """全无效绑定（打桩 mapper 未命中 + find_cell 未命中）→ 根任务
    ValueError（输入整体无意义——条目级兜底仅对部分无效放行）。"""
    files = [{"file_name": "f0.vcd", "kind": 1, "block_inst": "ghost",
              "block_cell": "", "strip_prefix": ""}]
    try:
        _run_root_body(files)
        raise AssertionError("all-invalid bindings must raise")
    except ValueError as e:
        assert "no binding target resolved" in str(e), str(e)


def test_root_task_settings_only_chain_shape():
    """settings-only 模式根任务体：每分区一物化任务 + settings 汇总任
    务（零写入——双正式对象由物化任务写定）。"""
    files = []  # vcd_files=None
    db, submitted = _run_root_body(files)
    assert db.written == [], \
        f"root task body must not write to waveform db: {db.written}"
    names = [name for name, _ in submitted]
    # _FakeDesignObj.partition_count = 2
    assert names == ["_wf_materialize_task", "_wf_materialize_task",
                     "_wf_settings_summary_task"], names


def test_chunk_task_inputs_anchor_design_db():
    """块解析任务 inputs = var 表 temp 键全名（裁定 IV-4：db 按引用共
    享读取）+ design db 对象全名（跨 db 直读，§21）；枚举锚
    （names_count/INST 段号/分区表）随参展开为确定性全名序列。"""
    import fly.runtime
    from emir.waveform.py.wf_flow import _wf_convert_chunk_task

    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    try:
        _wf_convert_chunk_task(_FakeWaveformDb(), _RootDesignDb(), "f.vcd",
                               None, "__wfg__vars_0", 1000000, 0, "", "",
                               "", 0, 0, 2, [0, 3],
                               [(7, 1, 2), (8, 3, 0)],
                               "__wfg__chunk_0_0")
    finally:
        fly.runtime.get_agent = original
    assert len(agent.submitted) == 1, agent.submitted
    name, inputs = agent.submitted[0]
    assert name == "_wf_convert_chunk_task", name
    expected = [
        f"{DB_PATH}:__wfg__vars_0",
        f"{DESIGN_PATH}:DSDesign",
        f"{DESIGN_PATH}:id_partition_map.INST",
        f"{DESIGN_PATH}:pg_nets",
        f"{DESIGN_PATH}:DSBlockNames_0",
        f"{DESIGN_PATH}:DSBlockNames_1",
        f"{DESIGN_PATH}:id_partition_map.INST.S0",
        f"{DESIGN_PATH}:id_partition_map.INST.S3",
        f"{DESIGN_PATH}:PART_1_2.NETS",
        f"{DESIGN_PATH}:PART_3_0.NETS",
    ]
    assert inputs == expected, inputs


class _RecordingMaterializeDb:
    """物化/合并/汇总任务直跑的 db 面：正式对象/temp 写入记录（as_task
    参数序列化要求模块级可 pickle——局部类不行）。"""

    _db = _FakeInner()

    def __init__(self):
        self.formal = []
        self.temp = {}
        # db 目录根（gzip 解压副本落 <db_path>/wf_scratch——测试重定向
        # 到临时目录，防写死路径污染文件系统）
        self.db_root = DB_PATH

    def get_db_path(self):
        return self.db_root

    def get_full_name(self, name):
        return f"{DB_PATH}:{name}"

    def write_object(self, name, obj, save_to_db=True):
        if save_to_db:
            self.formal.append(name)
        else:
            self.temp[name] = obj

    def read_object(self, name):
        if name in self.temp:
            return self.temp[name]
        from emir.waveform import EXWFEntrySlice
        return EXWFEntrySlice()  # 空分片占位（结构断言不消费事件面）

    def remove_object(self, name):
        self.temp.pop(name, None)


def test_materialize_task_writes_dual_objects():
    """settings-only 物化任务体：双正式对象写定（WAVEFORM/ACTIVITY）+
    计数 temp；实例枚举 = 分区 INSTANCES 对象（升序去重）。"""
    import fly.runtime
    from emir.waveform.py.wf_flow import _wf_materialize_task

    design_db = _MaterializeDesignDb()
    db = _RecordingMaterializeDb()
    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    try:
        _wf_materialize_task._fly_original_func(
            db, design_db, 7, 1, 2, 0, design_db.PARTITIONS, [],
            1000000000000000, "__wfg__counts_7")
    finally:
        fly.runtime.get_agent = original
    assert db.formal == ["PART_1_2.WAVEFORM", "PART_1_2.ACTIVITY"], db.formal
    assert "__wfg__counts_7" in db.temp, db.temp
    # INSTANCES 对象已读（实例枚举源）
    assert any(n.endswith(".INSTANCES") for n in design_db.read_names), \
        design_db.read_names


# ── 任务体直跑：文件入口 / 合并 / 汇总（裁定 IV-1/IV-2/IV-4 接线）─────

_ENTRY_VCD = ("$date\n\tnow\n$end\n$timescale 1ns $end\n"
              "$scope module top $end\n$var wire 1 ! a $end\n"
              "$upscope $end\n$enddefinitions $end\n"
              "#0\n$dumpvars\n0!\n$end\n#10\n1!\n")


def test_file_entry_task_vars_temp_and_manifest():
    """文件入口任务体：var 表写 db temp（裁定 IV-4）+ manifest 携带
    chunk_count/vars_key（scratch_dir=None）；块任务提交参数带 vars_key
    ——明文文件零开销直通（file_name 原样下传）。"""
    import os
    import tempfile
    import fly.runtime
    from emir.waveform import EXWFVarTable
    from emir.waveform.py.wf_flow import _wf_file_entry_task

    work = tempfile.mkdtemp(prefix="wf_entry_")
    vcd = os.path.join(work, "plain.vcd")
    with open(vcd, "w") as f:
        f.write(_ENTRY_VCD)
    db = _RecordingMaterializeDb()
    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    try:
        _wf_file_entry_task._fly_original_func(
            db, _RootDesignDb(), vcd, 0, "", "", "", 0, 1024 * 1024,
            0, [], [], "__wfg__manifest_0")
    finally:
        fly.runtime.get_agent = original
    # var 表 temp 对象写定 + manifest 结构
    assert isinstance(db.temp["__wfg__vars_0"], EXWFVarTable), db.temp
    manifest = db.temp["__wfg__manifest_0"]
    assert manifest["chunk_count"] == len(agent.submitted), manifest
    assert manifest["parse_name"] == vcd, manifest
    assert manifest["vars_key"] == "__wfg__vars_0"
    assert manifest["scratch_dir"] is None, manifest
    # 每个块任务提交都成功（链形状 = manifest.chunk_count 块）；vars 键
    # 锚定见 test_chunk_task_inputs_anchor_design_db
    assert len(agent.submitted) == manifest["chunk_count"]
    import shutil
    shutil.rmtree(work, ignore_errors=True)


def test_file_entry_task_gzip_decompresses_to_scratch():
    """文件入口任务体（gzip 输入，裁定 IV-1）：魔数嗅探 → db 临时目录
    解压副本进链（块任务 file_name = 副本路径）+ manifest 记录
    scratch_dir（删除责任单点在汇总任务）；解析结果与明文一致（var 表
    同构）。"""
    import gzip
    import os
    import shutil
    import tempfile
    import fly.runtime
    from emir.waveform.py.wf_flow import _wf_file_entry_task

    work = tempfile.mkdtemp(prefix="wf_entry_gz_")
    plain = os.path.join(work, "plain.vcd")
    with open(plain, "w") as f:
        f.write(_ENTRY_VCD)
    packed = os.path.join(work, "packed.vcd.gz")
    with gzip.open(packed, "wb") as f:
        f.write(_ENTRY_VCD.encode())

    def _run(vcd_path, db_root):
        db = _RecordingMaterializeDb()
        db.db_root = db_root
        agent = _CaptureAgent()
        original = fly.runtime.get_agent
        fly.runtime.get_agent = lambda: agent
        try:
            _wf_file_entry_task._fly_original_func(
                db, _RootDesignDb(), vcd_path, 0, "", "", "", 3,
                1024 * 1024, 0, [], [], "__wfg__manifest_3")
        finally:
            fly.runtime.get_agent = original
        return db, agent

    db_plain, _ = _run(plain, work)
    db_gz, agent_gz = _run(packed, work)
    # 明文直通 vs gzip 解压副本：scratch_dir 记录差异
    assert db_plain.temp["__wfg__manifest_3"]["scratch_dir"] is None
    scratch = db_gz.temp["__wfg__manifest_3"]["scratch_dir"]
    assert scratch and scratch.startswith(work), scratch
    # 副本落 db 目录 wf_scratch 下、带文件序前缀防同名冲突
    assert os.path.basename(scratch) == "wf_scratch"
    decompressed = os.listdir(scratch)
    assert len(decompressed) == 1 and decompressed[0].startswith("3_"), \
        decompressed
    # 解压副本可被常规切块链消费（块任务已提交 + var 表与明文同构）
    assert db_gz.temp["__wfg__manifest_3"]["chunk_count"] == \
        db_plain.temp["__wfg__manifest_3"]["chunk_count"]
    assert len(db_gz.temp["__wfg__vars_3"].track_ids) == \
        len(db_plain.temp["__wfg__vars_3"].track_ids)
    # 块任务拿到的 file_name = 解压副本（file_index=3 的提交参数经
    # as_task 序列化，这里以 manifest + scratch 副本存在性佐证链路）
    assert len(agent_gz.submitted) == \
        db_gz.temp["__wfg__manifest_3"]["chunk_count"]
    shutil.rmtree(work, ignore_errors=True)


def test_merge_partition_task_ctx_and_counts():
    """合并任务体：design db 直读组装 ctx（wf_merge_partition ctx 首参
    ——四级回落判定材料）+ 混合模式 overrides 空时零覆盖直通（不查分
    区 INSTANCES——实例枚举仅覆盖路径消费）+ 双正式对象与 counts
    （含 override_entry_count/applied_names）写定。"""
    import fly.runtime
    from emir.waveform.py.wf_flow import _wf_merge_partition_task

    design_db = _MaterializeDesignDb()
    db = _RecordingMaterializeDb()
    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    try:
        _wf_merge_partition_task._fly_original_func(
            db, design_db, ["__wfg__chunk_0_0"], 7, 1, 2,
            "PART_1_2.WAVEFORM", "PART_1_2.ACTIVITY", "__wfg__counts_7",
            0, [], design_db.PARTITIONS, [], 10 ** 15)
    finally:
        fly.runtime.get_agent = original
    assert db.formal == ["PART_1_2.WAVEFORM", "PART_1_2.ACTIVITY"], \
        db.formal
    counts = db.temp["__wfg__counts_7"]
    assert counts["override_entry_count"] == 0, counts
    assert counts["applied_names"] == [], counts
    assert counts["waveform_count"] == 0, counts
    # ctx 组装发生（design 对象已直读）；覆盖路径未触发（无 INSTANCES 读）
    assert design_db.DESIGN_OBJ in design_db.read_names, \
        design_db.read_names
    assert not any(n.endswith(".INSTANCES")
                   for n in design_db.read_names), design_db.read_names


def test_summary_task_cleans_keys_and_scratch_dirs():
    """汇总任务体（清理责任单点）：运行时键（切片/var 表/计数/清单）
    严格 remove + gzip 解压目录删除 + summary 唯一写定；空输入形态下
    覆盖条目聚合为零。"""
    import os
    import tempfile
    import fly.runtime
    from emir.waveform.py.wf_flow import _wf_merge_summary_task

    work = tempfile.mkdtemp(prefix="wf_summary_")
    scratch = os.path.join(work, "wf_scratch")
    os.makedirs(os.path.join(scratch, "0_plain.vcd"))
    from emir.waveform import EXWFEntrySlice
    db = _RecordingMaterializeDb()
    db.temp.update({
        "__wfg__chunk_0_0": EXWFEntrySlice(),
        "__wfg__counts_7": {"overlap_override_count": 0,
                            "time_anomaly_count": 0,
                            "waveform_count": 0,
                            "override_entry_count": 0,
                            "applied_names": []},
        "__wfg__manifest_0": {"chunk_count": 1,
                              "parse_name": "f.vcd",
                              "vars_key": "__wfg__vars_0",
                              "scratch_dir": scratch},
        "__wfg__vars_0": object(),
    })
    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    try:
        _wf_merge_summary_task._fly_original_func(
            db, ["f.vcd"], ["__wfg__chunk_0_0"], ["__wfg__counts_7"],
            ["__wfg__manifest_0"], ["__wfg__vars_0"], [scratch], [],
            {"default_toggle_rate": 0.0, "default_static_probability": 0.5,
             "duration_sec": 1.0, "x_z_handling": "denominator_include"},
            [], "summary")
    finally:
        fly.runtime.get_agent = original
    # 运行时键全部清理 + 解压目录删除（单点）+ summary 唯一写定
    assert db.temp == {}, db.temp
    assert not os.path.exists(scratch), scratch
    assert db.formal == ["summary"], db.formal
    import shutil
    shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    test_master_submits_two_tasks_only()
    test_master_submits_two_tasks_settings_only()
    test_freeze_inputs_are_fixed_markers()
    test_root_task_inputs_anchor_design_db()
    test_preprocess_does_not_read_file_content()
    test_root_task_vcd_mode_chain_shape()
    test_root_task_all_invalid_binding_raises()
    test_root_task_settings_only_chain_shape()
    test_chunk_task_inputs_anchor_design_db()
    test_materialize_task_writes_dual_objects()
    test_file_entry_task_vars_temp_and_manifest()
    test_file_entry_task_gzip_decompresses_to_scratch()
    test_merge_partition_task_ctx_and_counts()
    test_summary_task_cleans_keys_and_scratch_dirs()
    print("[PASS] test_wf_flow_structure")
