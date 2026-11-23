"""WaveformDb — waveform db（role="waveform"）与建库流程入口
build_waveform_db。

⑧ 波形数据库（plan docs/emir/waveform-db-plan.md，2026-10-12 全裁定；
⑧ vcd db 与 ⑨ switching db 合并更名——库体系 13 库 → 12 库）：解析
VCD（Value Change Dump，值变化转储文件，IEEE 1364 标准四态 ASCII）→
名字换算为 design db 全局 id（net 名推导挂全部连接位 / inst pin 名直配
两形态）→ 按 design db 分区结构落库。同一 db 双容器（裁定：双对象独立
加载）——波形容器（翻转事件序列，按分区去重存储 + 分区局部波形 id 引
用）与统计容器（逐实例引脚活动，SAIF 等价原始量 {T0, T1, TX, TZ,
rise_count, fall_count} + 全局 DURATION 分列，率与概率由消费侧推导）。
settings-only 模式（vcd_files=None，settings.activity 必给）照常产出逐
分区对象（波形容器空、统计容器物化 overrides、波形引用全哨兵）——消
费接口恒定。混合模式（裁定 IV-2：VCD + settings.activity.overrides 并
存）用户覆盖优先——合并产物统计容器上替换命中条目统计值，波形引用保
留；default 在 VCD 模式不生效（仅 summary 元数据）。

正式对象布局（§8，同一 (xp, yp) 网格坐标系）：
  PART_{xp}_{yp}.WAVEFORM  分区波形容器（WFPartitionWaveform；波形 id =
                           数组下标，settings-only 恒空）
  PART_{xp}_{yp}.ACTIVITY  分区统计容器（WFPartitionActivity；键 = 实例
                           全局 id，primary 恰一——活动挂实例，点数据
                           语义同 timing db 裁定）
  "summary"                WFSummary 汇总（全局 DURATION + settings 默
                           认值元数据〔消费侧展开依据〕+ 覆盖率/弃收/
                           兜底计数 + 逐来源文件统计，可追溯）
  "alpha_settings"         建库 alpha 设置（WFAlphaSettings，单键）

三段式流程（dev-rules §3「建库 API 流程标准」）：本文件入口函数 ①轻量
参数预处理（schema 校验 + 文件存在性 + settings 结构 + settings-only
判定 + 建库；不读文件内容——VCD 头嗅探〔$timescale 校验 + gzip 魔数
识别解压，裁定 IV-1〕下放文件入口任务，形态错任务失败透出）②提交唯
一 flow 根任务 ③顶层提交 freeze——任务链目录见 wf_flow._waveform_flow_task。

debug 读库 API（plan §3.3，同 design/timing debug 先例）：get_activity/
get_waveform 单实例点查——实例 id 经 design db 的 id_partition_map.INST
反向映射定位分区 → 整区双对象按需加载 + 进程内 LRU（容量 8）→ 单表查；
导出视图（toggle_rate / P1 / 方向占比）按 summary 元数据口径现算（率由
消费侧推导——裁定 2）。design db 经数据库链 find_db(role="design") 获
取。
"""

from log import INFO

from fly import register_flow
from fly import UserDoc, Schema, document
from storage import Database

from emir.common import (ensure_readable_file, is_nonempty_str,
                         is_valid_binding_desc)

# alpha/settings 键值域单一来源（header schema 与 WFAlphaSettings 声明
# 引用同一组 validator）
from .alpha_settings import (DEFAULT_DURATION_SEC, X_Z_HANDLING_VALUES,
                             get_default_alpha_settings, is_activity_struct,
                             is_chunk_size_mb, is_x_z_handling)

# A6 后由 emir/__init__ 聚合触发（common → project → lib → design →
# timing → waveform），此处包根已完成初始化，可安全取 EMIRProject。
from emir.project import EMIRProject


class WaveformDb(Database):
    """waveform db。role="waveform"。"""

    role = "waveform"

    # 正式持久化对象名（plan §3.1/§8）
    SUMMARY_OBJ = "summary"
    # 建库 alpha 设置对象（WFAlphaSettings；master 侧随建库写入）
    ALPHA_SETTINGS_OBJ = "alpha_settings"

    # 无波形哨兵（WFPinActivity.waveform_id_；plan §5.2 裁定值
    # = UINT32_MAX——settings-only 条目 / VCD 模式零事件命中）
    NO_WAVEFORM_ID = 0xFFFFFFFF

    # debug 读库 LRU 容量定值（plan §3.3：容量 8 同 design/timing 先例）
    _PARTITION_CACHE_CAPACITY = 8

    @staticmethod
    def partition_obj_name(xp: int, yp: int) -> str:
        """分区波形容器对象名（PART_{xp}_{yp}.WAVEFORM；`.` 作层级分隔
        ——对象名字符集仅允许字母/数字/`.`/`_`，同 design db 裁定）。"""
        return f"PART_{xp}_{yp}.WAVEFORM"

    @staticmethod
    def activity_obj_name(xp: int, yp: int) -> str:
        """分区统计容器对象名（PART_{xp}_{yp}.ACTIVITY）。"""
        return f"PART_{xp}_{yp}.ACTIVITY"

    def load_waveform_summary_obj(self):
        """读取汇总（EXWFSummary；wf_functions.load_waveform_summary 的
        句柄方法形态）。"""
        return self.read_object(self.SUMMARY_OBJ)

    def load_partition_activity_obj(self, xp: int, yp: int):
        """读取分区统计容器（EXWFPartitionActivity）。"""
        return self.read_object(self.activity_obj_name(xp, yp))

    def load_partition_waveform_obj(self, xp: int, yp: int):
        """读取分区波形容器（EXWFPartitionWaveform）。"""
        return self.read_object(self.partition_obj_name(xp, yp))

    # ── debug 读库 API（plan §3.3：挂 db 句柄的交互定位面）────────────

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
        """直接前驱 design db（数据库链 BFS 最近前驱；链缺失 = 波形 db
        未经 build_waveform_db 构建——debug 面不可用）。"""
        design_db = self.find_db(role="design")
        if design_db is None:
            raise RuntimeError(
                "waveform db: design db predecessor not found in db chain "
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

    def _load_partition_obj(self, xp: int, yp: int, container: str):
        """分区双容器对象按需加载 + LRU（键 = (xp, yp, container)；
        container ∈ {"WAVEFORM", "ACTIVITY"}——双容器独立缓存）。"""
        key = (xp, yp, container)
        cache_attr = f"_partition_{container.lower()}_cache"
        obj = self._lru_get(cache_attr, key)
        if obj is None:
            name = (self.partition_obj_name(xp, yp) if container ==
                    "WAVEFORM" else self.activity_obj_name(xp, yp))
            obj = self._lru_put(cache_attr, self._PARTITION_CACHE_CAPACITY,
                                key, self.read_object(name))
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

    def _summary(self):
        """summary 单条目缓存（导出视图现算的元数据基准）。"""
        summary = self._lru_get("_summary_cache", "summary")
        if summary is None:
            summary = self._lru_put("_summary_cache", 1, "summary",
                                    self.read_object(self.SUMMARY_OBJ))
        return summary

    @staticmethod
    def _activity_view(pin, summary):
        """导出视图现算（plan §3.2 下游换算口径——本库不预除，裁定 2）：
        toggle_rate = (rise + fall) / DURATION；P1 按 x/z 口径（
        denominator_include 分母含 x/z、exclude/hold_previous 分母排除
        ——hold_previous 的重分配语义归消费侧，本库给排除口径参考值）；
        方向占比 = rise / (rise + fall)（喂 Liberty rise/fall 能量分表）。
        分母为零（零时长/零活动）返回 None。"""
        total = summary.total_duration_fs
        rise, fall = pin.rise_count, pin.fall_count
        t0, t1 = pin.t0, pin.t1
        tx, tz = pin.tx, pin.tz
        toggle_rate = ((rise + fall) / (total * 1e-15)) if total else None
        if summary.x_z_handling == "denominator_include":
            denom = t0 + t1 + tx + tz
        else:
            denom = t0 + t1
        p1 = (t1 / denom) if denom else None
        direction = (rise / (rise + fall)) if (rise + fall) else None
        return {"toggle_rate": toggle_rate, "p1": p1, "direction": direction}

    def _resolve_inst_id(self, inst_id):
        """实例入参归一（int 全局 id / str 层级全路径名——str 经 design
        db 名字 mapper 转换；未命中 None）。"""
        if isinstance(inst_id, str):
            from emir.design import DesignDb
            return self._design_db().convert_to_id("inst", inst_id)
        return int(inst_id)

    def get_activity(self, inst_id):
        """单实例统计点查（plan §3.3 debug API；入参 = 实例全局 id int
        或层级全路径名 str；未命中返回 None：无 primary 分区副本的实例
        或统计容器无该实例条目）。

        返回 dict：instance id + primary 分区 (id, xp, yp) + pins 列表
        （每 pin：pin id + 名 + waveform id〔哨兵 = 无波形〕+ T0/T1/TX/
        TZ（fs）+ rise/fall 计数 + 导出视图 toggle_rate/p1/direction，
        按 summary 元数据口径现算）。
        """
        gid = self._resolve_inst_id(inst_id)
        if gid is None:
            return None
        located = self._locate_partition(gid)
        if located is None:
            return None
        pid, (xp, yp) = located
        activity = self._load_partition_obj(xp, yp, "ACTIVITY")
        inst_activity = activity.instance_at(gid)
        if inst_activity is None:
            return None  # 映射与分区产物失配的防御（正常建库不触发）
        summary = self._summary()
        pins = []
        for p in inst_activity.pins:
            view = self._activity_view(p, summary)
            pins.append({
                "pin_id": p.pin_id,
                "pin_name": self._pin_name_of(p.pin_id),
                "waveform_id": p.waveform_id,
                "t0": p.t0,
                "t1": p.t1,
                "tx": p.tx,
                "tz": p.tz,
                "rise_count": p.rise_count,
                "fall_count": p.fall_count,
                "toggle_rate": view["toggle_rate"],
                "p1": view["p1"],
                "direction": view["direction"],
            })
        return {
            "instance_id": gid,
            "primary_partition_id": pid,
            "partition": (xp, yp),
            "pins": pins,
        }

    def get_waveform(self, inst_id, pin_name=None):
        """单实例波形点查（plan §3.3 debug API；入参同 get_activity +
        可选 pin 名过滤；未命中返回 None）。

        返回 dict：instance id + primary 分区 + pins 列表（每 pin：pin
        id + 名 + waveform id + 事件序列 [(time_fs, value_str)]，
        value_str ∈ "0"/"1"/"x"/"z"）。settings-only 模式或哨兵波形 id
        （无事件命中）→ 每引脚空序列。
        """
        gid = self._resolve_inst_id(inst_id)
        if gid is None:
            return None
        located = self._locate_partition(gid)
        if located is None:
            return None
        pid, (xp, yp) = located
        activity = self._load_partition_obj(xp, yp, "ACTIVITY")
        inst_activity = activity.instance_at(gid)
        if inst_activity is None:
            return None
        waveform = self._load_partition_obj(xp, yp, "WAVEFORM")
        pins = []
        for p in inst_activity.pins:
            name = self._pin_name_of(p.pin_id)
            if pin_name is not None and name != pin_name:
                continue
            if p.waveform_id == self.NO_WAVEFORM_ID:
                events = []
            else:
                events = list(waveform.waveform_at(p.waveform_id).events)
            pins.append({
                "pin_id": p.pin_id,
                "pin_name": name,
                "waveform_id": p.waveform_id,
                "events": events,
            })
        return {
            "instance_id": gid,
            "primary_partition_id": pid,
            "partition": (xp, yp),
            "pins": pins,
        }


# ── header schema（2026-09-17 裁定范式：结构化声明 + 命名 validator，
#    禁止内联 lambda；白名单严格模式——未知键/非法值直接 raise）──

_path_schema = Schema(str, check=is_nonempty_str,
                      error="must be a non-empty file path, got {value}")


def _is_design_db_handle(value):
    """design_db 参数判定（DesignDb 句柄——带 DESIGN_OBJ 属性的 db 对象）。"""
    return hasattr(value, "DESIGN_OBJ")


_vcd_binding_desc_schema = Schema.dict(
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

# vcd_files 元素 schema（str 纯路径 | dict 块绑定描述符——timing db 名字
# 空间三形态）
_vcd_file_schema = Schema.any_of(_path_schema, _vcd_binding_desc_schema)

_settings_schema = Schema.dict(
    optional={
        "activity": Schema(
            object, check=is_activity_struct,
            error="must be an activity struct: 'default' with "
                  "'toggle_rate' and 'static_probability' and/or "
                  "non-empty 'overrides', optional 'duration'; "
                  "got {value}"),
        "x_z_handling": Schema(
            str, check=is_x_z_handling,
            error=f"must be one of {X_Z_HANDLING_VALUES}, got {{value}}"),
    },
    allow_extra=False,
    extra_error="settings accepts only 'activity' and 'x_z_handling' "
                "keys, unexpected key {key}")

_alpha_schema = Schema.dict(
    optional={
        "chunk_size_mb": Schema(int, check=is_chunk_size_mb,
                                error="must be an integer >= 16, got "
                                      "{value}"),
    },
    allow_extra=False)

build_waveform_db_doc = UserDoc(
    "构建 waveform db：解析 VCD（值变化转储文件，IEEE 1364 标准四态 "
    "ASCII）与/或用户统计设置，把信号翻转事件与活动统计换算为 design db "
    "全局 id（net 名推导挂全部连接位 / 实例引脚名直配）并按 design db "
    "分区结构落库。双容器：波形容器（事件序列，同网去重存储）+ 统计容"
    "器（逐实例逐引脚四态时长与翻转计数）。vcd_files 缺省 None = 仅按 "
    "settings 设置物化统计（波形容器为空）。")
build_waveform_db_doc.add_param("name",
    schema=Schema(str, check=is_nonempty_str,
                  error="must be a non-empty string, got {value}"),
    required=True, desc="db 子目录名 + Project 内部 key（重名自动递增）")
build_waveform_db_doc.add_param("design_db",
    schema=Schema(object, check=_is_design_db_handle,
                  error="must be a DesignDb instance, got {value}"),
    required=True, desc="design db（DesignDb 实例）。波形条目按其实例/网"
        "/pin 名字匹配归属，分区结构与之一致")
build_waveform_db_doc.add_param("vcd_files",
    schema=Schema.list(_vcd_file_schema, min_len=1),
    required=False, default=None, none_ok=True,
    desc="VCD 文件输入列表（None = settings-only 模式，此时 settings."
        "activity 必须给出）。元素 = 纯路径字符串（条目使用全层级路径名）"
        "或块绑定描述符 dict：{'file_name'（必填）, 'block_inst' / "
        "'block_cell'（互斥，恰传其一）, 'strip_prefix'（可选）}。文件必"
        "须存在且可读（否则立即报错）；gzip 压缩 VCD 自动识别解压（裁定 "
        "IV-1，建库结果与明文一致）；其余非 VCD 格式文件导致建库失败（库"
        "不冻结）。多文件按文件形态合并：纯路径文件统计直加、事件按文件"
        "序偏移拼接；块绑定文件重合条目后文件覆盖")
build_waveform_db_doc.add_param("settings",
    schema=_settings_schema,
    required=False, default=None, none_ok=True,
    desc="统计设置（dict）。键：activity——活动统计设置 {'default': "
        "{'toggle_rate'（次/秒，rise+fall 合计）, 'static_probability'}"
        "（settings-only 模式必给；VCD/混合模式可选）, 'duration'（统计"
        "窗口秒数，缺省 "
        f"{DEFAULT_DURATION_SEC}）, 'overrides'（逐实体覆盖，键 = 实例"
        "引脚层级路径名或 net 名，值 = {'toggle_rate'?, "
        "'static_probability'?, 'rise_fraction'?}}（VCD 输入并存时同样"
        "生效——混合模式裁定 IV-2：覆盖条目建库期替换实测统计、"
        "waveform_id 保留，default 在 VCD 模式不生效；overrides 显式"
        "条目物化、default 作为汇总元数据）}；x_z_handling——x/z 消费口"
        "径 'denominator_include'（默认）/ 'exclude' / 'hold_previous'。"
        "未知键或非法值直接报错")
build_waveform_db_doc.add_param("alpha",
    schema=_alpha_schema,
    required=False, default=None, none_ok=True,
    desc="实验性配置（dict）。可用键：chunk_size_mb——单文件切块大小"
         "（MB），整数 ≥16，默认 256。未知键或非法值将直接报错")
build_waveform_db_doc.add_example("VCD 建库 + 读统计",
    code='''waveform_db = proj.build_waveform_db(
    name="waveform", vcd_files=["sim.vcd"], design_db=design_db)
proj.wait_frozen("waveform", timeout=600)
summary = waveform_db.load_waveform_summary_obj()
act = waveform_db.load_partition_activity_obj(0, 0)''',
    desc="VCD 解析 + 名字匹配归属 → 冻结后读汇总与分区统计")
build_waveform_db_doc.add_example("settings-only 建库",
    code='''waveform_db = proj.build_waveform_db(
    name="waveform", vcd_files=None, design_db=design_db,
    settings={"activity": {"default": {"toggle_rate": 2.0e6,
                                       "static_probability": 0.5}}})
proj.wait_frozen("waveform", timeout=600)''',
    desc="无 VCD 输入：按 settings 物化逐分区统计（波形容器空）")
build_waveform_db_doc.add_keyword(["waveform", "vcd", "activity",
                                   "toggle", "switching", "emir"])


@register_flow(EMIRProject)
@document(build_waveform_db_doc)
def build_waveform_db(self, name: str, design_db, vcd_files: list = None,
                      settings: dict = None, alpha: dict = None):
    """构建 waveform db：VCD 解析 / settings 物化 + 名字匹配归属 + 冻结。

    VCD 模式（vcd_files 列表）：全部文件解析完成后库冻结可读；gzip 压
    缩 VCD 自动识别解压（裁定 IV-1，建库结果与明文一致）。混合模式
    （settings.activity.overrides 与 VCD 并存）：覆盖条目建库期替换实
    测统计（waveform_id 保留），覆盖计数入 summary。settings-only 模式
    （vcd_files=None）：settings.activity 必须给出，否则立即报错；逐分
    区双对象照常产出（波形容器空、统计容器物化 overrides）。参数不合
    法（空名、vcd_files/settings/alpha 含未知键或非法值、design_db 类
    型不符、文件不存在/不可读）时立即报错终止，不建库；非 VCD 格式文
    件（缺失 $timescale 等）在解析任务中失败（库不冻结）。块绑定目标
    存在性（块实例路径/块 cell 名在 design db 命中）在解析阶段校验，
    未命中的文件跳过并计入汇总提醒（不阻塞其他文件入库）。

    Args:
        self: 自动绑定的 EMIRProject 实例。
        name: db 子目录名 + Project 内部 key（重名自动递增）。
        design_db: DesignDb 实例（波形条目按其实例/网/pin 名字匹配归属）。
        vcd_files: VCD 文件输入列表（纯路径或块绑定描述符 dict，文件须
            存在且可读；gzip 压缩自动解压）；None = settings-only 模式。
        settings: 统计设置（activity / x_z_handling；未知键或非法值报
            错；None 合法——VCD 模式统计全部来自实测；overrides 在 VCD
            输入并存时同样生效，见混合模式）。
        alpha: 实验性配置（chunk_size_mb；未知键或非法值报错；None 合法）。

    Returns:
        ``WaveformDb`` 句柄（解析与冻结异步进行，可用 wait_frozen 等待）。
    """
    # ── ① 轻量参数预处理：schema 已由 header 拦截；settings-only 模式
    #    判定（activity 必给）+ 文件存在性（元数据级，不读内容——形态
    #    错由文件入口任务失败透出）+ 建库 + alpha 写入（header 已拦截
    #    非法值，apply 防御性覆盖）──
    from .wf_utils import (duration_fs_of, normalize_vcd_files,
                           settings_meta_params)
    settings_only = vcd_files is None
    if settings_only and (settings is None or "activity" not in settings):
        raise ValueError(
            "build_waveform_db: vcd_files=None (settings-only mode) "
            "requires settings.activity (default toggle_rate/static_"
            "probability) to be given")
    files = [] if settings_only else normalize_vcd_files(vcd_files)
    for f in files:
        ensure_readable_file(f["file_name"], "build_waveform_db",
                             "vcd_files")
    meta_params = settings_meta_params(settings)
    activity = (settings or {}).get("activity") or {}
    override_table = activity.get("overrides") or {}
    # 逐实体覆盖 → 物化参数（缺省键与 C++ 侧 WFActivityOverride 缺省一致）
    overrides = [{"name": entry_name,
                  "toggle_rate": entry.get("toggle_rate", 0.0),
                  "static_probability": entry.get("static_probability",
                                                  0.5),
                  "rise_fraction": entry.get("rise_fraction", 0.5)}
                 for entry_name, entry in override_table.items()]
    duration_fs = duration_fs_of(meta_params["duration_sec"])
    # 直接前驱 design db 入链（dev-rules §3 数据库链——debug API
    # find_db 的依赖来源）
    db = self._create_db(name, db_cls=WaveformDb, prev=[design_db])
    alpha_settings = get_default_alpha_settings()
    alpha_settings.apply(alpha)
    db.write_object(WaveformDb.ALPHA_SETTINGS_OBJ, alpha_settings)

    # ── ② flow 根任务 + ③ freeze（master 提交 O(1) 个任务——§20
    #    编排判据；freeze 依赖固定标记 summary）──
    from .wf_flow import _freeze_waveform_task, _waveform_flow_task
    _waveform_flow_task(db, design_db, files, meta_params, overrides,
                        duration_fs)
    _freeze_waveform_task(db)

    if settings_only:
        mode = "settings-only"
    else:
        mode = f"{len(files)} vcd file(s)"
    INFO(f"build_waveform_db: '{name}' submitted ({mode})")
    return db
