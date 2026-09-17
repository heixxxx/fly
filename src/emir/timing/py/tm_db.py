"""TimingDb — timing db（role="timing"）与建库流程入口 build_timing_db。

⑦ 时序数据库（plan docs/emir/timing-db-plan.md，2026-09-16 全裁定）：
解析 TWF（楷登 Innovus write_timing_windows 格式）→ 名字换算为 design db
全局 id → 按 design db 分区结构落库。直接前驱 design db 显式传参（名字
映射器 + 分区表 + id 反向映射的消费源）。

正式对象布局（§8，同一 (xp, yp) 网格坐标系）：
  PART_{xp}_{yp}.TIMING   逐分区时序（TMPartitionTiming：实例 → 时钟归属
                          + 引脚稀疏表；primary 恰一——时序为点数据无
                          extend 副本语义）
  "clocks"                TMClockTable 时钟表（跨文件按名合并，顶层文件
                          定义优先；频率 = 1/period 由消费侧推导——Innovus
                          格式无实例级频率字段，裁定 3）
  "summary"               TMSummary 汇总（覆盖率/弃收/兜底计数 + 逐来源
                          文件统计，可追溯）
  "alpha_settings"        建库 alpha 设置（TMAlphaSettings，两键）

阶段链装配在 tm_flow.run_timing_flow（流程实现与入口分离）。

debug 读库 API（plan §3.2，同 design debug API 先例）：get_timing 单实例
点查——实例 id 经 design db 的 id_partition_map.INST 反向映射定位分区 →
整区 TIMING 对象按需加载 + 进程内 LRU（容量 8）→ 单表查。design db 经
数据库链 find_db(role="design") 获取（dev-rules §3 间接前置经数据库链）。
"""

from log import INFO

from fly import register_flow
from fly import UserDoc, Schema, document
from storage import Database

from emir.common import (ensure_readable_file, is_nonempty_str,
                         is_valid_binding_desc)

# alpha 键值域单一来源（header schema 与 TMAlphaSettings 声明同一函数）
from .alpha_settings import is_chunk_size_mb, is_twf_format

# A6 后由 emir/__init__ 聚合触发（common → project → lib → design →
# timing），此处包根已完成初始化，可安全取 EMIRProject。
from emir.project import EMIRProject


class TimingDb(Database):
    """timing db。role="timing"。"""

    role = "timing"

    # 正式持久化对象名（plan §3.1/§8）
    CLOCKS_OBJ = "clocks"
    SUMMARY_OBJ = "summary"
    # 建库 alpha 设置对象（TMAlphaSettings；master 侧随建库写入）
    ALPHA_SETTINGS_OBJ = "alpha_settings"

    # debug 读库 LRU 容量定值（plan §3.2：容量 8 同 design debug API 先例）
    _PARTITION_CACHE_CAPACITY = 8

    @staticmethod
    def partition_obj_name(xp: int, yp: int) -> str:
        """分区正式对象名（PART_{xp}_{yp}.TIMING；`.` 作层级分隔——对象
        名字符集仅允许字母/数字/`.`/`_`，同 design db 裁定）。"""
        return f"PART_{xp}_{yp}.TIMING"

    def load_timing_clocks_obj(self):
        """读取时钟表（EXTMClockTable；tm_functions.load_timing_clocks 的
        句柄方法形态）。"""
        return self.read_object(self.CLOCKS_OBJ)

    def load_timing_summary_obj(self):
        """读取汇总（EXTMSummary）。"""
        return self.read_object(self.SUMMARY_OBJ)

    # ── debug 读库 API（plan §3.2：挂 db 句柄的交互定位面）────────────

    def _lru_put(self, attr, capacity, key, value):
        """进程内简单 LRU（OrderedDict；容量上限防撑爆——同 design debug
        API 先例）。cache 属性惰性初始化。"""
        cache = getattr(self, attr, None)
        if cache is None:
            import collections
            cache = collections.OrderedDict()
            setattr(self, attr, cache)
        if key in cache:
            cache.move_to_end(key)
        cache[key] = value
        while len(cache) > capacity:
            cache.popitem(last=False)
        return value

    def _lru_get(self, attr, key):
        cache = getattr(self, attr, None)
        if cache is None or key not in cache:
            return None
        cache.move_to_end(key)
        return cache[key]

    def _design_db(self):
        """直接前驱 design db（数据库链 BFS 最近前驱；链缺失 = 时序 db
        未经 build_timing_db 构建——debug 面不可用）。"""
        design_db = self.find_db(role="design")
        if design_db is None:
            raise RuntimeError(
                "timing db: design db predecessor not found in db chain "
                "(debug API requires the design db link)")
        return design_db

    def _locate_partition(self, inst_id: int):
        """实例 id → (partition id, (xp, yp))（design db 的
        id_partition_map.INST 段表 → 段对象按需加载 + LRU；未放置实例/
        空洞返回 None）。"""
        from emir.design import DesignDb
        design_db = self._design_db()
        index = self._lru_get("_inst_index_cache", "INST")
        if index is None:
            index = self._lru_put(
                "_inst_index_cache", 2, "INST",
                design_db.read_object(DesignDb.id_map_index_obj_name("INST")))
        seg_start = index.find_segment_start(inst_id)
        if seg_start is None:
            return None
        seg_index = seg_start >> DesignDb.ID_MAP_SEGMENT_BITS
        segment = self._lru_get("_inst_segment_cache", seg_index)
        if segment is None:
            segment = self._lru_put(
                "_inst_segment_cache", self._PARTITION_CACHE_CAPACITY,
                seg_index,
                design_db.read_object(
                    DesignDb.id_map_segment_obj_name("INST", seg_index)))
        pid = segment.partition_of(inst_id)
        if pid is None:  # 空洞（导出面哨兵已转 None）
            return None
        # partition id → (xp, yp)（design 分区表；单条目缓存）
        xy = self._lru_get("_design_cache", "partitions")
        if xy is None:
            design = design_db.read_object(DesignDb.DESIGN_OBJ)
            xy = {}
            for i in range(design.partition_count):
                p = design.partition_at(i)
                xy[p.partition_id] = (p.xp, p.yp)
            xy = self._lru_put("_design_cache", 1, "partitions", xy)
        if pid not in xy:
            return None
        return pid, xy[pid]

    def _load_partition_timing(self, xp: int, yp: int):
        """分区 TIMING 对象按需加载 + LRU（键 = (xp, yp)）。"""
        key = (xp, yp)
        obj = self._lru_get("_partition_cache", key)
        if obj is None:
            obj = self._lru_put(
                "_partition_cache", self._PARTITION_CACHE_CAPACITY, key,
                self.read_object(self.partition_obj_name(xp, yp)))
        return obj

    def _pin_name_of(self, pin_id):
        """全局 pin 名字空间反查（design db 容器 pin hasher；未命中 None）。"""
        from emir.design import DesignDb
        design = self._lru_get("_design_cache", "design")
        if design is None:
            design = self._lru_put("_design_cache", 1, "design",
                                   self._design_db().read_object(
                                       DesignDb.DESIGN_OBJ))
        key = design.pin_key_by_id(pin_id)
        return key or None

    def get_timing(self, inst_id):
        """单实例点查（plan §3.2 debug API；入参 = 实例全局 id int 或
        层级全路径名 str——str 经 design db 名字 mapper 转换；未命中返回
        None：无 primary 分区副本的实例（UNPLACED/root 占位 id 0）或
        TWF 未覆盖的实例）。

        返回 dict：instance id/时钟名（时钟表反查）/primary 分区
        (id, xp, yp)/pins 列表（每 pin：pin id + 名 + rise/fall 到达窗口
        与翻转时间 (min, max) 元组 + 存在位 + 常量/多源标记）。
        """
        design_db = self._design_db()
        if isinstance(inst_id, str):
            from emir.design import DesignDb
            gid = design_db.convert_to_id("inst", inst_id)
            if gid is None:
                return None
        else:
            gid = int(inst_id)
        located = self._locate_partition(gid)
        if located is None:
            return None
        pid, (xp, yp) = located
        part = self._load_partition_timing(xp, yp)
        inst_timing = part.instance_at(gid)
        if inst_timing is None:
            return None  # 映射与分区产物失配的防御（正常建库不触发）
        # 时钟名反查（时钟表单条目缓存）
        clocks = self._lru_get("_clocks_cache", "clocks")
        if clocks is None:
            clocks = self._lru_put("_clocks_cache", 1, "clocks",
                                   self.read_object(self.CLOCKS_OBJ))
        clock_id = inst_timing.clock_id
        clock_name = None
        if clock_id < clocks.size:
            clock_name = clocks.entry_at(clock_id).name

        pins = []
        for p in inst_timing.pins:
            (ra, fa, rs, fs) = p.summary[1], p.summary[2], p.summary[3], \
                p.summary[4]
            has = p.summary[5:]
            pins.append({
                "pin_id": p.pin_id,
                "pin_name": self._pin_name_of(p.pin_id),
                "rise_arrival": tuple(ra) if has[0] else None,
                "fall_arrival": tuple(fa) if has[1] else None,
                "rise_slew": tuple(rs) if has[2] else None,
                "fall_slew": tuple(fs) if has[3] else None,
                "is_constant": has[4],
                "is_multi_source": has[5],
            })
        return {
            "instance_id": gid,
            "clock": clock_name,
            "primary_partition_id": pid,
            "partition": (xp, yp),
            "pins": pins,
        }


# ── header schema（2026-09-17 裁定：结构化声明 + 命名 validator，禁止
#    内联 lambda；白名单严格模式——未知键/非法值直接 raise）──

_path_schema = Schema(str, check=is_nonempty_str,
                      error="must be a non-empty file path, got {value}")


def _is_design_db_handle(value):
    """design_db 参数判定（DesignDb 句柄——带 DESIGN_OBJ 属性的 db 对象）。"""
    return hasattr(value, "DESIGN_OBJ")


_binding_desc_schema = Schema.dict(
    required={"file_name": _path_schema},
    optional={
        "block_inst": Schema(str, check=is_nonempty_str,
                             error="must be a non-empty hierarchy path, "
                                   "got {value}"),
        "block_cell": Schema(str, check=is_nonempty_str,
                             error="must be a non-empty cell name, got "
                                   "{value}"),
        "strip_prefix": Schema(str,
                               error="must be a string, got {value}"),
    },
    allow_extra=False,
    check=is_valid_binding_desc,
    error="must specify exactly one of 'block_inst' or 'block_cell', "
          "got {value}")

# timing_files 元素 schema（str 纯路径 | dict 块绑定描述符）
_timing_file_schema = Schema.any_of(_path_schema, _binding_desc_schema)

_alpha_schema = Schema.dict(
    optional={
        "chunk_size_mb": Schema(int, check=is_chunk_size_mb,
                                error="must be an integer >= 16, got "
                                      "{value}"),
        "format": Schema(str, check=is_twf_format,
                         error="must be one of ('auto', 'innovus'), got "
                               "{value}"),
    },
    allow_extra=False)

build_timing_db_doc = UserDoc(
    "构建 timing db：解析 TWF（时序窗口文件，楷登 Innovus "
    "write_timing_windows 格式，网络/引脚/混合三维度），把时序数据（时钟"
    "归属 + 到达窗口 + 翻转时间 + 常量标记）按 design db 的实例/网/pin "
    "名字匹配归属并落库。timing_files 支持文件列表（分块 TWF）与块绑定"
    "形态：纯路径 = 条目使用全层级路径名；{'file_name', 'block_inst'} = "
    "条目使用块实例内局部名；{'file_name', 'block_cell'} = 块定义级时序"
    "（对全部实例成立，建库期复制）；后两类可附加 'strip_prefix' 剥离"
    "包装顶层前缀。")
build_timing_db_doc.add_param("name",
    schema=Schema(str, check=is_nonempty_str,
                  error="must be a non-empty string, got {value}"),
    required=True, desc="db 子目录名 + Project 内部 key（重名自动递增）")
build_timing_db_doc.add_param("timing_files",
    schema=Schema.list(_timing_file_schema, min_len=1),
    required=True, desc="TWF 文件输入列表（至少 1 个；元素 = 纯路径字符串"
        "或块绑定描述符 dict；单文件亦列表；分块 TWF 传全部块文件）。"
        "描述符键：file_name（必填）、block_inst / block_cell（互斥，"
        "恰传其一）、strip_prefix（可选）。文件必须存在且可读，否则报错")
build_timing_db_doc.add_param("design_db",
    schema=Schema(object, check=_is_design_db_handle,
                  error="must be a DesignDb instance, got {value}"),
    required=True, desc="design db（DesignDb 实例）。时序条目按其实例/网/pin 名字匹配归属")
build_timing_db_doc.add_param("settings",
    schema=Schema.dict(
        allow_extra=False,
        extra_error="settings currently has no available keys, "
                    "unexpected key {key}"),
    required=False, default=None, none_ok=True,
    desc="配置项（dict）。当前无可用键：传入任何键将直接报错"
         "（None 合法）")
build_timing_db_doc.add_param("alpha",
    schema=_alpha_schema,
    required=False, default=None, none_ok=True,
    desc="实验性配置（dict）。可用键：chunk_size_mb——单文件切块大小"
         "（MB），整数 ≥16，默认 256；format——TWF 格式，'auto' 或 "
         "'innovus'，默认 'auto'。未知键或非法值将直接报错")
build_timing_db_doc.add_example("构建 timing db",
    code='''timing_db = proj.build_timing_db(
    name="timing", timing_files=["design.twf"], design_db=design_db)
proj.wait_frozen("timing", timeout=600)
clocks = timing_db.load_timing_clocks_obj()''',
    desc="TWF 解析 + 名字匹配归属 → 冻结后读时钟表")
build_timing_db_doc.add_keyword(["timing", "twf", "clock", "window",
                                 "arrival", "slew", "emir"])


@register_flow(EMIRProject)
@document(build_timing_db_doc)
def build_timing_db(self, name: str, timing_files: list, design_db,
                    settings: dict = None, alpha: dict = None):
    """构建 timing db：TWF 解析 + 名字匹配归属 + 冻结。

    全部文件解析完成后库冻结可读。参数不合法（空名、timing_files
    结构错误、settings/alpha 含未知键或非法值、design_db 类型不符、
    文件不存在/不可读/非 TWF 格式）时立即报错终止，不建库。块绑定目标
    存在性（块实例路径/块 cell 名在 design db 命中）在解析阶段校验，
    未命中的条目跳过并计入汇总提醒（不阻塞其他条目入库）。

    Args:
        self: 自动绑定的 EMIRProject 实例。
        name: db 子目录名 + Project 内部 key（重名自动递增）。
        timing_files: TWF 文件输入列表（至少 1 个；纯路径或块绑定描述
            符 dict，文件须存在且可读）。
        design_db: DesignDb 实例（时序条目按其实例/网/pin 名字匹配归属）。
        settings: 配置项（当前无可用键，传任何键报错；None 合法）。
        alpha: 实验性配置（两键：chunk_size_mb、format；未知键或非法值
            报错；None 合法）。

    Returns:
        ``TimingDb`` 句柄（解析与冻结异步进行，可用 wait_frozen 等待）。
    """
    # ── Step 1: 检查输入（master 侧前置；结构已由 header schema 拦截，
    #    这里做可读性显式校验 + TWF 头嗅探）──
    from .tm_utils import normalize_timing_files, sniff_twf_header
    files = normalize_timing_files(timing_files)
    for f in files:
        path = f["file_name"]
        ensure_readable_file(path, "build_timing_db", "timing_files")
        sniff_twf_header(path)

    # ── Step 2: 建库（TimingDb，role="timing"；直接前驱 design db 入
    #    链——dev-rules §3 数据库链，debug API find_db 的依赖来源）──
    db = self._create_db(name, db_cls=TimingDb, prev=[design_db])

    # ── Step 2.5: alpha 设置：header schema 已拦截未知键/非法值（直接
    #    raise）；此处 apply 防御性覆盖（理论不再拒绝）+ settings 对象随
    #    建库写入 db（消费点 read_object 读回 + normalize 兜底，向前兼容
    #    旧 db 对象）──
    from .alpha_settings import get_default_alpha_settings
    alpha_settings = get_default_alpha_settings()
    alpha_settings.apply(alpha)
    db.write_object(TimingDb.ALPHA_SETTINGS_OBJ, alpha_settings)

    # ── Step 3 + 4: 阶段链提交 + freeze 提交 ──
    settings = db.read_object(TimingDb.ALPHA_SETTINGS_OBJ)
    settings.normalize()
    from .tm_flow import run_timing_flow
    run_timing_flow(db, design_db, files, settings)

    INFO(f"build_timing_db: '{name}' submitted ({len(files)} timing file(s))")
    return db
