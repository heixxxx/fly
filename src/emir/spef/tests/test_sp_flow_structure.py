"""build_spef_db 三段式结构单测（dev-rules §3「建库 API 流程标准」，同
test_wf_flow_structure.py 先例）。

master 侧提交面断言（§20 编排判据）：
  - 提交任务数恰 2（flow 根任务 + freeze）——O(1)，与输入规模无关；
  - freeze 依赖恰为固定标记 summary，不含运行时规模临时键（__spg__
    前缀——运行时键清理责任已单化到链上任务）；
  - 根任务依赖锚 = design db 必要对象 + alpha_settings（入口等待语义）；
  - 预处理段不读文件内容：非 SPEF 内容/空文件不阻止提交（嗅探已下放
    文件入口任务）。

根任务体目录断言：
  - 根任务体零写入（§21 design db 数据零副本）；提交面 = 每有效文件一
    个文件入口任务 + 合并链编排任务；
  - 全部绑定目标未命中 → 根任务 ValueError；部分未命中 → 只为有效文
    件提交 + 逐文件 SPMG::0009；
  - 作用域编号（裁定 8 判定材料）纯函数：三个纯路径文件共用一个作用
    域、两个绑同一块实例的文件共用一个、两个绑不同块定义的文件各自一
    个——相等关系断言 + 根任务到块任务到 C++ 的传递一致性。

任务体直跑断言：
  - 块任务 inputs = 文件头 temp 键全名 + design db 对象全名（跨 db 直
    读）+ 本分区 INST_CONNECTIONS（覆盖率材料，第 H 节第 4 条）；
  - 文件入口任务体：文件头写 db temp + manifest 携带 chunk_count/
    header_key/source_name/scratch_dir；gzip 输入解压副本进链；
  - 每分区合并任务体：读全部分片（序 = 文件序 × 块序——记录型替身断
    言传给 C++ 函数的顺序）+ 正式对象落盘 + 计数写 temp + 上下文读全
    部分区 NETS 与本区 INST_CONNECTIONS；
  - 汇总任务体：SPMG 消息族按计数逐条一次透出（计数为 0 不发）+ 切块
    数进逐文件统计（第 H 节第 5 条）+ 运行时键与解压目录单点清理；
  - 读库面：分区对象名格式、get_net_load 返回结构与未命中 None；
  - 消息注册：14 个 id 已注册且级别正确（reload 替身记录全局区注册语
    句）。
"""

DB_PATH = "/proj/spef"
DESIGN_PATH = "/proj/design"


class _FakeInner:
    def get_db_path(self):
        return DB_PATH

    def get_data_path(self):
        return DB_PATH + "/data"


class _FakeSpefDb:
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
        from emir.spef.py.alpha_settings import get_default_alpha_settings
        return get_default_alpha_settings()

    def remove_object(self, name):
        pass


class _RecordingSpefDb(_FakeSpefDb):
    """同 _FakeSpefDb + 记录写面对象名（根任务体零写入断言）。"""

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
        return _FakeSpefDb()


class _CaptureAgent:
    mode = "capture"

    def __init__(self):
        self.submitted = []

    def submit(self, task_name, module, serialized, task_inputs, **kwargs):
        self.submitted.append((task_name, task_inputs))


def _make_file(path):
    return {"file_name": path, "kind": 0, "block_inst": "",
            "block_cell": "", "strip_prefix": ""}


def _build(spef_files):
    """在捕获 agent 下跑 build_spef_db，返回 (db, submitted)。文件内容
    为非 SPEF 垃圾字节——master 预处理段不得读内容。"""
    import tempfile
    import fly.runtime
    from emir.spef import build_spef_db

    paths = []
    for _ in (spef_files or []):
        f = tempfile.NamedTemporaryFile(suffix=".spef", delete=False)
        f.write(b"not a spef at all")
        f.close()
        paths.append(f.name)

    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    try:
        db = build_spef_db(_FakeProject(), name="spef",
                           spef_files=paths, design_db=_FakeDesignDb())
    finally:
        fly.runtime.get_agent = original
    return db, agent.submitted


def _run_root_body(files, mapper=None):
    """直跑根任务函数体（绕过 as_task 提交——体内嵌套任务仍经捕获
    agent 提交），返回 (spef db, submitted)。名字 mapper 打桩（默认
    get_global_id 返回 None）。"""
    import fly.runtime
    from emir.spef.py import sp_flow

    db = _RecordingSpefDb()
    agent = _CaptureAgent()
    original_agent = fly.runtime.get_agent
    original_mapper = sp_flow.ds_make_instance_name_mapper
    fly.runtime.get_agent = lambda: agent
    sp_flow.ds_make_instance_name_mapper = \
        lambda design, names: (mapper if mapper is not None
                               else _StubMapper())
    try:
        sp_flow._spef_flow_task._fly_original_func(
            db, _RootDesignDb(), files, 256 * 1024 * 1024, 0, False)
    finally:
        fly.runtime.get_agent = original_agent
        sp_flow.ds_make_instance_name_mapper = original_mapper
    return db, agent.submitted


def test_master_submits_two_tasks_only():
    _, submitted = _build(["p1.spef"])
    names = [name for name, _ in submitted]
    assert names == ["_spef_flow_task", "_freeze_spef_task"], names


def test_freeze_inputs_are_fixed_markers():
    _, submitted = _build(["p1.spef"])
    freeze_inputs = submitted[1][1]
    assert freeze_inputs == [f"{DB_PATH}:summary"], freeze_inputs
    assert not any("__spg__" in k for k in freeze_inputs), \
        "freeze dependency must not carry runtime-scale temp keys"


def test_root_task_inputs_anchor_design_db():
    _, submitted = _build(["p1.spef"])
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
    """非 SPEF 内容文件不阻止提交（入口只查存在性——嗅探在文件入口
    任务内失败透出，库不冻结）。"""
    db, submitted = _build(["p1.spef", "p2.spef"])
    assert db is not None and len(submitted) == 2


def test_root_task_chain_shape():
    """根任务体零写入（§21：design db 数据零副本）；提交面 = 每有效
    文件一个文件入口任务 + 合并链编排任务（task 命名模式与体内目录逐
    行对应）。kind=2 绑定校验打桩命中。"""
    files = [{"file_name": f"f{i}.spef", "kind": 2, "block_inst": "",
              "block_cell": "blk", "strip_prefix": ""} for i in range(2)]
    db, submitted = _run_root_body(files)
    assert db.written == [], \
        f"root task body must not write to spef db: {db.written}"
    names = [name for name, _ in submitted]
    assert names == ["_sp_file_entry_task", "_sp_file_entry_task",
                     "_sp_plan_merge_chain_task"], names


def test_root_task_all_invalid_binding_raises():
    """全无效绑定（打桩 mapper 未命中 + find_cell 未命中）→ 逐文件先发
    SPMG::0009（message.log 里有解释，与部分未命中同一表现）再抛根任务
    ValueError（输入整体无意义——条目级兜底仅对部分无效放行）。"""
    import fly.runtime
    from emir.spef.py import sp_flow

    files = [{"file_name": "f0.spef", "kind": 1, "block_inst": "ghost",
              "block_cell": "", "strip_prefix": ""}]
    db = _RecordingSpefDb()
    agent = _CaptureAgent()
    msgs = []
    original_agent = fly.runtime.get_agent
    original_mapper = sp_flow.ds_make_instance_name_mapper
    original_msg = sp_flow.message
    fly.runtime.get_agent = lambda: agent
    sp_flow.ds_make_instance_name_mapper = lambda design, names: \
        _StubMapper()
    sp_flow.message = lambda domain_id, source, msg: msgs.append(domain_id)
    try:
        sp_flow._spef_flow_task._fly_original_func(
            db, _RootDesignDb(), files, 256 * 1024 * 1024, 0, False)
        raise AssertionError("all-invalid bindings must raise")
    except ValueError as e:
        assert "no binding target resolved" in str(e), str(e)
    finally:
        fly.runtime.get_agent = original_agent
        sp_flow.ds_make_instance_name_mapper = original_mapper
        sp_flow.message = original_msg
    # 消息在抛出前已发出（每个无效文件一条 SPMG::0009）
    assert msgs == ["SPMG::0009"], msgs
    assert agent.submitted == [], "no task must be submitted on all-invalid"


def test_root_task_partial_invalid_binding_skips():
    """部分无效绑定（两个文件一个 ghost）→ 只为有效文件提交文件入口
    任务 + 逐文件 SPMG::0009（error）+ 编排任务照常。"""
    import fly.runtime
    from emir.spef.py import sp_flow

    files = [{"file_name": "bad.spef", "kind": 1, "block_inst": "ghost",
              "block_cell": "", "strip_prefix": ""},
             _make_file("good.spef")]
    db = _RecordingSpefDb()
    agent = _CaptureAgent()
    msgs = []
    original_agent = fly.runtime.get_agent
    original_mapper = sp_flow.ds_make_instance_name_mapper
    original_msg = sp_flow.message
    fly.runtime.get_agent = lambda: agent
    sp_flow.ds_make_instance_name_mapper = lambda design, names: \
        _StubMapper()
    sp_flow.message = lambda domain_id, source, msg: msgs.append(domain_id)
    try:
        sp_flow._spef_flow_task._fly_original_func(
            db, _RootDesignDb(), files, 256 * 1024 * 1024, 0, False)
    finally:
        fly.runtime.get_agent = original_agent
        sp_flow.ds_make_instance_name_mapper = original_mapper
        sp_flow.message = original_msg
    assert msgs == ["SPMG::0009"], msgs
    names = [name for name, _ in agent.submitted]
    # 只为有效文件提交文件入口任务（1 个）+ 合并链编排任务
    assert names == ["_sp_file_entry_task", "_sp_plan_merge_chain_task"], \
        names


def test_assign_scope_indexes():
    """作用域编号（裁定 8 判定材料）纯函数：全部纯路径文件共用同一个
    作用域；绑同一块实例的文件共用一个；绑同一块定义的文件共用一个；
    三者互不相同。"""
    from emir.spef.py.sp_flow import assign_scope_indexes
    files = [
        _make_file("a.spef"),
        {"file_name": "b.spef", "kind": 0, "block_inst": "",
         "block_cell": "", "strip_prefix": ""},
        {"file_name": "c.spef", "kind": 1, "block_inst": "top/u1",
         "block_cell": "", "strip_prefix": ""},
        {"file_name": "d.spef", "kind": 1, "block_inst": "top/u1",
         "block_cell": "", "strip_prefix": ""},
        {"file_name": "e.spef", "kind": 1, "block_inst": "top/u2",
         "block_cell": "", "strip_prefix": ""},
        {"file_name": "f.spef", "kind": 2, "block_inst": "",
         "block_cell": "BLK_A", "strip_prefix": ""},
        {"file_name": "g.spef", "kind": 2, "block_inst": "",
         "block_cell": "BLK_B", "strip_prefix": ""},
    ]
    scopes = assign_scope_indexes(files)
    # 纯路径两文件同一作用域
    assert scopes[0] == scopes[1]
    # 同一块实例的两文件同一作用域
    assert scopes[2] == scopes[3]
    # 不同块实例不同作用域；不同块定义不同作用域
    assert scopes[4] != scopes[2]
    assert scopes[5] != scopes[6]
    # 三类互不相同（5 个不同值：纯路径 / u1 / u2 / BLK_A / BLK_B）
    assert len(set(scopes)) == 5, scopes
    print("[OK] assign_scope_indexes")


def test_scope_index_reaches_chunk_conversion():
    """作用域编号传递一致性：根任务分配的作用域随文件入口 → 块任务
    → sp_convert_chunk 到达（块任务体直跑；ctx 组装与 C++ 换算都打
    桩——结构断言不消费换算面）。"""
    import fly.runtime
    from emir.spef.py import sp_flow
    from emir.spef import EXSPEntrySlice, EXSPFileChunk

    seen = {}
    real_convert = sp_flow.sp_convert_chunk
    real_assemble = sp_flow._assemble_design_context

    def stub_assemble(design_db, names_count, inst_seg_indexes, partitions):
        return EXSPDesignContextStub()

    class EXSPDesignContextStub:
        pass

    def recording_convert(ctx, header, parsed, file_index, scope_index,
                          binding, source_file):
        seen["file_index"] = file_index
        seen["scope_index"] = scope_index
        return EXSPEntrySlice()

    sp_flow._assemble_design_context = stub_assemble
    sp_flow.sp_convert_chunk = recording_convert
    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    db = _RecordingSpefDb()
    # 文件头 temp 替身（sp_parse_chunk_text 需要真实 C++ 头部对象——
    # 解析出的网表不被替身换算消费）
    from emir.spef import EXSPFileHeader
    db.read_object = lambda name: EXSPFileHeader()
    # 字节区间读取（块任务体）需要真实文件：空区间 [0, 0) 读空文本即可
    import os
    import tempfile
    fd, parse_path = tempfile.mkstemp(prefix="sp_scope_", suffix=".spef")
    os.write(fd, b"*D_NET n_a 1.0\n")
    os.close(fd)
    try:
        sp_flow._sp_convert_chunk_task._fly_original_func(
            db, _RootDesignDb(), parse_path, EXSPFileChunk(),
            "__spg__header_0", 0, "", "", "", 0, False, 3, 7, 0,
            parse_path, 0, [], [], "__spg__chunk_7_0")
    finally:
        fly.runtime.get_agent = original
        sp_flow.sp_convert_chunk = real_convert
        sp_flow._assemble_design_context = real_assemble
        os.unlink(parse_path)
    assert seen["scope_index"] == 3 and seen["file_index"] == 7, seen


def test_chunk_task_inputs_anchor_design_db():
    """块解析任务 inputs = 文件头 temp 键全名（db 按引用共享读取）+
    design db 对象全名（跨 db 直读，§21）；枚举锚（names_count/INST 段
    号/分区表）随参展开为确定性全名序列。"""
    import fly.runtime
    from emir.spef.py.sp_flow import _sp_convert_chunk_task

    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    try:
        _sp_convert_chunk_task(_FakeSpefDb(), _RootDesignDb(), "f.spef",
                               None, "__spg__header_0", 0, "", "", "",
                               0, False, 0, 0, 0, "f.spef", 2, [0, 3],
                               [(7, 1, 2), (8, 3, 0)],
                               "__spg__chunk_0_0")
    finally:
        fly.runtime.get_agent = original
    assert len(agent.submitted) == 1, agent.submitted
    name, inputs = agent.submitted[0]
    assert name == "_sp_convert_chunk_task", name
    expected = [
        f"{DB_PATH}:__spg__header_0",
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


class _RecordingTaskDb:
    """文件入口/合并/汇总任务直跑的 db 面：正式对象/temp 写入记录（
    as_task 参数序列化要求模块级可 pickle——局部类不行）。"""

    _db = _FakeInner()

    def __init__(self):
        self.formal = []
        self.formal_objs = {}
        self.temp = {}
        # db 目录根（gzip 解压副本落 <db_path>/sp_scratch——测试重定向
        # 到临时目录，防写死路径污染文件系统）
        self.db_root = DB_PATH

    def get_db_path(self):
        return self.db_root

    def get_full_name(self, name):
        return f"{DB_PATH}:{name}"

    def write_object(self, name, obj, save_to_db=True):
        if save_to_db:
            self.formal.append(name)
            self.formal_objs[name] = obj
        else:
            self.temp[name] = obj

    def read_object(self, name):
        if name in self.temp:
            return self.temp[name]
        raise KeyError(name)

    def remove_object(self, name):
        self.temp.pop(name, None)


_ENTRY_SPEF = ('*SPEF "IEEE 1481-1998"\n*DIVIDER /\n*DELIMITER :\n'
               "*C_UNIT 1 FF\n*R_UNIT 1 OHM\n"
               "*D_NET n_a 1.0\n*CAP\n1 n_a:1 1.0\n*END\n"
               "*D_NET n_b 2.0\n*CAP\n1 n_b:1 2.0\n*END\n")


def test_file_entry_task_header_temp_and_manifest():
    """文件入口任务体：文件头写 db temp + manifest 携带 chunk_count/
    header_key/source_name（scratch_dir 空串）；每块提交一个块任务
    ——明文文件零开销直通。"""
    import os
    import tempfile
    import fly.runtime
    from emir.spef import EXSPFileHeader
    from emir.spef.py.sp_flow import _sp_file_entry_task

    work = tempfile.mkdtemp(prefix="sp_entry_")
    spef = os.path.join(work, "plain.spef")
    with open(spef, "w") as f:
        f.write(_ENTRY_SPEF)
    db = _RecordingTaskDb()
    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    try:
        _sp_file_entry_task._fly_original_func(
            db, _RootDesignDb(), spef, 0, "", "", "", 0, 0,
            1024 * 1024, 0, False, 0, [], [], "__spg__manifest_0")
    finally:
        fly.runtime.get_agent = original
    # 文件头 temp 对象写定 + manifest 结构
    assert isinstance(db.temp["__spg__header_0"], EXSPFileHeader), db.temp
    manifest = db.temp["__spg__manifest_0"]
    assert manifest["chunk_count"] == len(agent.submitted), manifest
    assert manifest["header_key"] == "__spg__header_0"
    assert manifest["source_name"] == spef, manifest
    assert manifest["scratch_dir"] == "", manifest
    # 每个块任务提交都成功（链形状 = manifest.chunk_count 块）
    assert len(agent.submitted) == manifest["chunk_count"]
    import shutil
    shutil.rmtree(work, ignore_errors=True)


def test_file_entry_task_gzip_decompresses_to_scratch():
    """文件入口任务体（gzip 输入）：魔数嗅探 → db 临时目录解压副本进
    链（块任务读副本）+ manifest 记录 scratch_dir（删除责任单点在汇
    总任务）；副本名带文件序防同名冲突；解析结果与明文一致（切块数同
    构）。"""
    import gzip
    import os
    import shutil
    import tempfile
    import fly.runtime
    from emir.spef.py.sp_flow import _sp_file_entry_task

    work = tempfile.mkdtemp(prefix="sp_entry_gz_")
    plain = os.path.join(work, "plain.spef")
    with open(plain, "w") as f:
        f.write(_ENTRY_SPEF)
    packed = os.path.join(work, "packed.spef.gz")
    with gzip.open(packed, "wb") as f:
        f.write(_ENTRY_SPEF.encode())

    def _run(spef_path, db_root):
        db = _RecordingTaskDb()
        db.db_root = db_root
        agent = _CaptureAgent()
        original = fly.runtime.get_agent
        fly.runtime.get_agent = lambda: agent
        try:
            _sp_file_entry_task._fly_original_func(
                db, _RootDesignDb(), spef_path, 0, "", "", "", 0, 3,
                1024 * 1024, 0, False, 0, [], [], "__spg__manifest_3")
        finally:
            fly.runtime.get_agent = original
        return db, agent

    db_plain, _ = _run(plain, work)
    db_gz, _ = _run(packed, work)
    # 明文直通 vs gzip 解压副本：scratch_dir 记录差异
    assert db_plain.temp["__spg__manifest_3"]["scratch_dir"] == ""
    scratch = db_gz.temp["__spg__manifest_3"]["scratch_dir"]
    assert scratch and scratch.startswith(work), scratch
    # 副本落 db 目录 sp_scratch 下、带文件序前缀防同名冲突
    assert os.path.basename(scratch) == "sp_scratch"
    decompressed = os.listdir(scratch)
    assert len(decompressed) == 1 and decompressed[0].startswith("3_"), \
        decompressed
    # 解压副本可被常规切块链消费（块任务已提交 + 切块数与明文同构）
    assert db_gz.temp["__spg__manifest_3"]["chunk_count"] == \
        db_plain.temp["__spg__manifest_3"]["chunk_count"]
    assert db_plain.temp["__spg__manifest_3"]["chunk_count"] >= 1
    shutil.rmtree(work, ignore_errors=True)


class _DesignCtxDb:
    """合并任务直跑的 design db 面：真实 C++ 空对象供 EXSPDesignContext
    注入（结构断言不消费换算面）；读面对象名记录——本区 INST_
    CONNECTIONS 枚举源断言用。"""

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
            # ctx 组装读段表注入（EXSPDesignContext.set_inst_id_map 要
            # 求真实 C++ 对象；空段号集不消费段对象）
            return EXDSIdPartitionIndex()
        if name.endswith(".NETS"):
            return EXDSPartitionNets()
        if name.endswith(".INST_CONNECTIONS"):
            from emir.design import EXDSPartInstConnections
            return EXDSPartInstConnections()
        raise KeyError(name)


def test_merge_partition_task_reads_slices_in_order():
    """每分区合并任务体：读全部分片且顺序 = 文件序 × 块序（记录型替
    身断言传给 C++ 函数的顺序——「同作用域保留首份」依赖它）+ 正式对
    象落盘 + 计数写 temp + 上下文读全部分区 NETS 与本区 INST_
    CONNECTIONS（第 H 节第 4 条）。"""
    import fly.runtime
    from emir.spef.py import sp_flow

    class _Sentinel:
        def __init__(self, tag):
            self.tag = tag

    design_db = _DesignCtxDb()
    db = _RecordingTaskDb()
    slice_keys = ["__spg__chunk_0_0", "__spg__chunk_0_1",
                  "__spg__chunk_1_0"]
    for i, k in enumerate(slice_keys):
        db.temp[k] = _Sentinel(k)
    seen = {}
    real_merge = sp_flow.sp_merge_partition

    def recording_merge(ctx, slices, pid):
        seen["order"] = [s.tag for s in slices]
        seen["pid"] = pid
        obj, stats = real_merge(ctx, [], pid)
        return obj, stats

    sp_flow.sp_merge_partition = recording_merge
    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    try:
        sp_flow._sp_merge_partition_task._fly_original_func(
            db, design_db, slice_keys, 7, 1, 2, "PART_1_2.NET_LOAD",
            "__spg__counts_7", 0, [], design_db.PARTITIONS)
    finally:
        fly.runtime.get_agent = original
        sp_flow.sp_merge_partition = real_merge
    # 分片顺序 = slice_keys 序（文件序 × 块序）
    assert seen["order"] == slice_keys, seen
    assert seen["pid"] == 7, seen
    assert db.formal == ["PART_1_2.NET_LOAD"], db.formal
    assert "__spg__counts_7" in db.temp, db.temp
    # 上下文材料：全部分区 NETS（夹具单分区 = 1 个）+ 本区 INST_
    # CONNECTIONS（第 H 节第 4 条覆盖率材料；read_names 记录对象短名
    # ——任务体内经 DesignDb.partition_obj_name 构造）
    nets_reads = [n for n in design_db.read_names if n.endswith(".NETS")]
    assert nets_reads == ["PART_1_2.NETS"], nets_reads
    assert "PART_1_2.INST_CONNECTIONS" in design_db.read_names, \
        design_db.read_names


def _make_stats(**overrides):
    """逐文件统计替身（真实 C++ 载体——统计载体字段可写，供流程组装
    与测试构造；正式对象面保持只读）。"""
    from emir.spef import EXSPFileStats
    f = EXSPFileStats()
    f.source_file = "a.spef"
    for k, v in overrides.items():
        setattr(f, k, v)
    return f


def _make_delta(files):
    from emir.spef import EXSPStatsDelta
    d = EXSPStatsDelta()
    for f in files:
        d.add_file(f)
    return d


def test_summary_task_emits_each_message_once():
    """汇总任务体：SPMG 消息族按计数逐条一次透出（计数 > 0 发一次、
    0 不发——SPMG::0008 fatal 由 test_sp_flow_error 的子进程用例覆
    盖）；计数为零时一条不发。"""
    import fly.runtime
    from emir.spef import EXSPMergeStats
    from emir.spef.py import sp_flow

    def _run(stats_files, merge_stats, invalid=0):
        db = _RecordingTaskDb()
        # 分片替身：stats 挂指定逐文件统计（汇总任务读 slice.stats）
        from emir.spef import EXSPEntrySlice
        slice_obj = EXSPEntrySlice()
        slice_obj.stats = _make_delta(stats_files)
        db.temp["__spg__chunk_0_0"] = slice_obj
        for i, m in enumerate(merge_stats):
            db.temp[f"__spg__counts_{i}"] = m
        db.temp["__spg__manifest_0"] = {
            "chunk_count": 1, "header_key": "__spg__header_0",
            "source_name": "a.spef", "scratch_dir": ""}
        db.temp["__spg__header_0"] = object()
        msgs = []
        agent = _CaptureAgent()
        original = fly.runtime.get_agent
        original_msg = sp_flow.message
        fly.runtime.get_agent = lambda: agent
        sp_flow.message = \
            lambda domain_id, source, msg: msgs.append(domain_id)
        try:
            sp_flow._sp_merge_summary_task._fly_original_func(
                db, [_make_file("a.spef")], [0], ["__spg__chunk_0_0"],
                [f"__spg__counts_{i}" for i in range(len(merge_stats))],
                ["__spg__manifest_0"], ["__spg__header_0"], [],
                list(range(invalid)), "summary")
        finally:
            fly.runtime.get_agent = original
            sp_flow.message = original_msg
        return db, msgs

    # 有命中、其余计数全零：一条不发
    _, msgs = _run([_make_stats(net_count=1, hit_count=1)],
                   [EXSPMergeStats()])
    assert msgs == [], msgs

    # 逐条非零：13 条 WARN/ERROR 消息各发一次（0004 因 hit > 0 不发）
    m = EXSPMergeStats()
    m.same_scope_overlap_count = 1
    m.cap_mismatch_count = 1
    m.cap_mismatch_max_rel = 0.02
    files_stats = _make_stats(
        net_count=5, hit_count=4, net_name_miss_count=1,
        strip_miss_count=2, pg_net_skip_count=1, rnet_count=1,
        multi_value_net_count=1, value_index_fallback_count=1,
        broken_net_count=1, unrouted_net_count=1, name_map_miss_count=1,
        pnet_skip_count=1)
    db, msgs = _run([files_stats], [m])
    expected = {"SPMG::0001", "SPMG::0002", "SPMG::0003", "SPMG::0005",
                "SPMG::0006", "SPMG::0007", "SPMG::0010", "SPMG::0011",
                "SPMG::0012", "SPMG::0013", "SPMG::0014"}
    assert set(msgs) == expected, msgs
    assert len(msgs) == len(expected), msgs   # 每族恰一次
    assert "SPMG::0004" not in msgs   # hit > 0

    # 命中数为 0（不是全部文件失败——块都成功、网全没匹配）→ 0004
    _, msgs = _run([_make_stats(net_count=0, hit_count=0)],
                   [EXSPMergeStats()])
    assert msgs == ["SPMG::0004"], msgs


def test_summary_task_fills_chunk_count_and_cleans():
    """汇总任务体（第 H 节第 5 条）：逐文件统计的切块数按文件清单填入
    （多块文件逐块累加成文件级计数后，切块数取清单里的块数）；运行时
    键（文件头/切片/计数/清单）与 gzip 解压目录单点清理 + summary 唯一
    写定。"""
    import os
    import tempfile
    import fly.runtime
    from emir.spef.py import sp_flow

    work = tempfile.mkdtemp(prefix="sp_summary_")
    scratch = os.path.join(work, "sp_scratch")
    os.makedirs(scratch)
    # 两个清单条目：a.spef 三块、b.spef 一块；分片统计按块带出（文件级
    # 计数已由 sp_merge_summary 累加——此处 summary 里的逐文件表即累加
    # 后形态）
    delta = _make_delta([
        _make_stats(source_file="a.spef", net_count=2, hit_count=2),
        _make_stats(source_file="b.spef", net_count=1, hit_count=1)])
    db = _RecordingTaskDb()

    def _slice_with_stats(d):
        from emir.spef import EXSPEntrySlice
        s = EXSPEntrySlice()
        s.stats = d
        return s

    db.temp["__spg__chunk_0_0"] = _slice_with_stats(delta)
    db.temp["__spg__chunk_0_1"] = _slice_with_stats(_make_delta([]))
    db.temp["__spg__chunk_0_2"] = _slice_with_stats(_make_delta([]))
    db.temp["__spg__chunk_1_0"] = _slice_with_stats(_make_delta([]))
    from emir.spef import EXSPMergeStats
    db.temp["__spg__counts_7"] = EXSPMergeStats()
    db.temp["__spg__manifest_0"] = {
        "chunk_count": 3, "header_key": "__spg__header_0",
        "source_name": "a.spef", "scratch_dir": scratch}
    db.temp["__spg__manifest_1"] = {
        "chunk_count": 1, "header_key": "__spg__header_1",
        "source_name": "b.spef", "scratch_dir": ""}
    db.temp["__spg__header_0"] = object()
    db.temp["__spg__header_1"] = object()
    files = [_make_file("a.spef"), _make_file("b.spef")]
    agent = _CaptureAgent()
    original = fly.runtime.get_agent
    fly.runtime.get_agent = lambda: agent
    try:
        sp_flow._sp_merge_summary_task._fly_original_func(
            db, files, [0, 1],
            ["__spg__chunk_0_0", "__spg__chunk_0_1", "__spg__chunk_0_2",
             "__spg__chunk_1_0"],
            ["__spg__counts_7"],
            ["__spg__manifest_0", "__spg__manifest_1"],
            ["__spg__header_0", "__spg__header_1"], [scratch], [],
            "summary")
    finally:
        fly.runtime.get_agent = original
    # 切块数进逐文件统计（清单里的块数）
    by_name = {f.source_file: f.chunk_count
               for f in db.formal_objs["summary"].files}
    assert by_name == {"a.spef": 3, "b.spef": 1}, by_name
    # 运行时键全部清理 + 解压目录删除（单点）+ summary 唯一写定
    assert db.temp == {}, db.temp
    assert not os.path.exists(scratch), scratch
    assert db.formal == ["summary"], db.formal
    import shutil
    shutil.rmtree(work, ignore_errors=True)


def test_partition_obj_name_and_get_net_load():
    """读库面：分区对象名格式（PART_{xp}_{yp}.NET_LOAD）；get_net_load
    的返回结构（三电容 + 来源位图 + 缩减标记 + 副本分区表）与未命中
    None（任取一份副本数值）。"""
    from emir.spef import SpefDb
    assert SpefDb.SUMMARY_OBJ == "summary"
    assert SpefDb.ALPHA_SETTINGS_OBJ == "alpha_settings"
    assert SpefDb.partition_obj_name(0, 0) == "PART_0_0.NET_LOAD"
    assert SpefDb.partition_obj_name(2, 3) == "PART_2_3.NET_LOAD"

    class _FakeEntry:
        net_id = 11
        total_cap_reported_ff = 3.0
        ground_cap_ff = 2.0
        coupling_cap_ff = 1.0
        source_mask = 5
        flags = 1   # 缩减形态（reduced）

    class _FakePartitionObj:
        def __init__(self, entry):
            self._entry = entry

        def find_net(self, net_id):
            return self._entry if net_id == self._entry.net_id else None

    class _FakeDesignObj:
        partition_count = 2

        def partition_at(self, i):
            return _FakePart(i, i, 0)

    class _FakeDesignDb:
        DESIGN_OBJ = "DSDesign"

        def read_object(self, name):
            assert name == "DSDesign", name
            return _FakeDesignObj()

    class _FakeDb:
        def __init__(self):
            self._parts = {}
            self.design_db = _FakeDesignDb()

        def _design_db(self):
            return self.design_db

        def _design_partitions(self):
            return [(0, 0), (1, 0)]

        def _load_partition_obj(self, xp, yp):
            if (xp, yp) not in self._parts:
                self._parts[(xp, yp)] = _FakePartitionObj(_FakeEntry())
            return self._parts[(xp, yp)]

    db = _FakeDb()
    loaded = SpefDb.get_net_load(db, 11)
    assert loaded is not None
    assert loaded["net_id"] == 11
    assert loaded["total_cap_reported_ff"] == 3.0
    assert loaded["ground_cap_ff"] == 2.0
    assert loaded["coupling_cap_ff"] == 1.0
    assert loaded["source_mask"] == 5
    assert loaded["reduced"] is True
    assert loaded["partitions"] == [(0, 0), (1, 0)]   # 各分区副本任取
    # 未命中（网 id 无条目）→ None
    assert SpefDb.get_net_load(db, 999) is None
    print("[OK] partition_obj_name + get_net_load")


def test_iter_and_load_functions_object_names():
    """读库函数：对象名/依赖锚面（summary、分区 NET_LOAD、design 分区
    表锚）。"""
    from emir.spef import SpefDb
    from emir.spef.py import sp_functions

    class _FakeDb:
        def get_full_name(self, name):
            return f"{DB_PATH}:{name}"

        def find_db(self, role):
            assert role == "design", role
            return _FakeDesignDb()

        def read_object(self, name):
            assert name == SpefDb.SUMMARY_OBJ, name
            return "summary-obj"

    db = _FakeDb()
    deps = sp_functions.load_spef_summary.deps(db)
    assert deps == [f"{DB_PATH}:summary"], deps
    deps = sp_functions.load_partition_net_load.deps(db, 1, 2)
    assert deps == [f"{DB_PATH}:PART_1_2.NET_LOAD"], deps
    deps = sp_functions.iter_spef_partition.deps(db)
    assert f"{DESIGN_PATH}:DSDesign" in deps, deps
    print("[OK] sp_functions deps/object names")


def test_message_ids_registered():
    """消息注册：14 个 id 都已注册且级别正确（patch fly 源头 + reload
    替身记录全局区注册语句——注册动作随 import emir 已生效，此处断言
    语句面；reload 重复注册同级别幂等无害）。"""
    import importlib
    import fly
    from emir.spef.py import sp_register_msg

    calls = []
    original = fly.register_message_id

    def recording(domain_id, level="INFO"):
        calls.append((domain_id, level))

    fly.register_message_id = recording
    try:
        importlib.reload(sp_register_msg)
    finally:
        fly.register_message_id = original
    registered = dict(calls)
    expected = {f"SPMG::{i:04d}": "WARN" for i in list(range(1, 8)) +
                list(range(10, 15))}
    expected["SPMG::0008"] = "FATAL"
    expected["SPMG::0009"] = "ERROR"
    assert registered == expected, registered
    assert len(calls) == 14
    print("[OK] message ids registered (14)")


if __name__ == "__main__":
    test_master_submits_two_tasks_only()
    test_freeze_inputs_are_fixed_markers()
    test_root_task_inputs_anchor_design_db()
    test_preprocess_does_not_read_file_content()
    test_root_task_chain_shape()
    test_root_task_all_invalid_binding_raises()
    test_root_task_partial_invalid_binding_skips()
    test_assign_scope_indexes()
    test_scope_index_reaches_chunk_conversion()
    test_chunk_task_inputs_anchor_design_db()
    test_file_entry_task_header_temp_and_manifest()
    test_file_entry_task_gzip_decompresses_to_scratch()
    test_merge_partition_task_reads_slices_in_order()
    test_summary_task_emits_each_message_once()
    test_summary_task_fills_chunk_count_and_cleans()
    test_partition_obj_name_and_get_net_load()
    test_iter_and_load_functions_object_names()
    test_message_ids_registered()
    print("[PASS] test_sp_flow_structure")
