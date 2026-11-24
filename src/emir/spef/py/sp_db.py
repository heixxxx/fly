"""SpefDb — spef db（role="spef"）与建库流程入口 build_spef_db。

⑤ 寄生数据库（plan docs/emir/spef-db-plan.md，2026-11-24 全裁定）：
解析 SPEF（Standard Parasitic Exchange Format，标准寄生交换格式，
IEEE 1481）→ 逐网寄生数据换算为 design db 全局网 id → 按 design db 分
区结构落库。首期消费者 = power db 的 net load（整网电容——emir-data-
flow 裁定 4：首期仅服务右端项 net load）。入库形态 = 网负载轻对象三值
并存（行首自报总电容 + 对地明细合计 + 耦合明细合计 + 贡献来源位图 +
缩减形态标记），按连接位分区复制（每区自足）；RC 明细不入库（裁定 2）。
落库数值已按文件头单位档位换算为内部单位（电容飞法、电阻欧姆——裁定
7），消费侧无需二次换算。

正式对象布局（同一 (xp, yp) 网格坐标系）：
  PART_{xp}_{yp}.NET_LOAD  分区网负载容器（SPPartitionNetLoad；条目按
                           net id 升序）
  "summary"                SPSummary 汇总（逐来源文件统计 + 覆盖率统计
                           + 各类跳过/破损/观测计数 + 内部单位基准声明，
                           可追溯）
  "alpha_settings"         建库 alpha 设置（SPAlphaSettings，两键）

三段式流程（dev-rules §3「建库 API 流程标准」）：本文件入口函数 ①轻量
参数预处理（schema 校验 + 文件存在性 + 建库 + alpha_settings 写入；不
读文件内容——SPEF 头嗅探〔*SPEF 头关键字 + 单位头校验 + gzip 魔数识
别解压〕下放文件入口任务，形态错任务失败透出）②提交唯一 flow 根任务
③顶层提交 freeze——任务链目录见 sp_flow._spef_flow_task。

debug 读库 API（plan §3.2，同 waveform debug 先例）：get_net_load 单网
点查——全部分区对象按需加载 + 进程内 LRU（容量 8）→ 单表查；同一网在
各分区的副本数值相同，任取一份。

块绑定三形态（同 timing/waveform 先例）：纯路径 / 块实例绑定 /
块定义绑定 + strip_prefix。绑定目标存在性在解析阶段校验，未命中的文
件跳过并计入汇总提醒（不阻塞其他文件入库）。
"""

from log import INFO

from fly import register_flow
from fly import UserDoc, Schema, document
from storage import Database

from emir.common import (ensure_readable_file, is_nonempty_str,
                         is_valid_binding_desc)

# alpha/settings 键值域单一来源（header schema 与 SPAlphaSettings 声明
# 引用同一组 validator）
from .alpha_settings import get_default_alpha_settings, is_chunk_size_mb, \
    is_value_index

# A6 后由 emir/__init__ 聚合触发（common → project → lib → design →
# timing → waveform → spef），此处包根已完成初始化，可安全取 EMIRProject。
from emir.project import EMIRProject


class SpefDb(Database):
    """spef db。role="spef"。"""

    role = "spef"

    # 正式持久化对象名（plan §3.1）
    SUMMARY_OBJ = "summary"
    # 建库 alpha 设置对象（SPAlphaSettings；master 侧随建库写入）
    ALPHA_SETTINGS_OBJ = "alpha_settings"

    # debug 读库 LRU 容量定值（plan §3.2：容量 8 同 waveform/design/timing
    # 先例）
    _PARTITION_CACHE_CAPACITY = 8

    @staticmethod
    def partition_obj_name(xp: int, yp: int) -> str:
        """分区网负载容器对象名（PART_{xp}_{yp}.NET_LOAD；`.` 作层级分
        隔——对象名字符集仅允许字母/数字/`.`/`_`，同 design db 裁定）。"""
        return f"PART_{xp}_{yp}.NET_LOAD"

    def load_spef_summary_obj(self):
        """读取汇总（EXSPSummary；sp_functions.load_spef_summary 的句柄
        方法形态）。"""
        return self.read_object(self.SUMMARY_OBJ)

    def load_partition_net_load_obj(self, xp: int, yp: int):
        """读取分区网负载容器（EXSPPartitionNetLoad）。"""
        return self.read_object(self.partition_obj_name(xp, yp))

    # ── debug 读库 API（plan §3.2：挂 db 句柄的交互定位面）────────────

    def _lru_put(self, attr, capacity, key, value):
        """进程内简单 LRU（OrderedDict；容量上限防撑爆——同 waveform
        debug API 先例）。cache 属性惰性初始化。"""
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
        """直接前驱 design db（数据库链 BFS 最近前驱；链缺失 = spef db
        未经 build_spef_db 构建——debug 面不可用）。"""
        design_db = self.find_db(role="design")
        if design_db is None:
            raise RuntimeError(
                "spef db: design db predecessor not found in db chain "
                "(debug API requires the design db link)")
        return design_db

    def _design_partitions(self):
        """design db 分区表 (xp, yp) 行主序（单条目缓存）。"""
        xy = self._lru_get("_design_cache", "partitions")
        if xy is None:
            from emir.design import DesignDb
            design = self._design_db().read_object(DesignDb.DESIGN_OBJ)
            xy = [(design.partition_at(i).xp, design.partition_at(i).yp)
                  for i in range(design.partition_count)]
            xy = self._lru_put("_design_cache", 2, "partitions", xy)
        return xy

    def _design_net_union(self):
        """design db 跨块连接归并结果（EXDSNetUnion；单条目缓存）——
        get_net_load 的电气网成员口径来源（2026-11-24 用户裁定）。"""
        union = self._lru_get("_design_cache", "net_union")
        if union is None:
            from emir.design import DesignDb
            union = self._design_db().read_object(DesignDb.NET_UNION_OBJ)
            union = self._lru_put("_design_cache", 2, "net_union", union)
        return union

    def _load_partition_obj(self, xp: int, yp: int):
        """分区网负载对象按需加载 + LRU（键 = (xp, yp)）。"""
        key = (xp, yp)
        obj = self._lru_get("_partition_net_load_cache", key)
        if obj is None:
            obj = self._lru_put("_partition_net_load_cache",
                                self._PARTITION_CACHE_CAPACITY, key,
                                self.read_object(
                                    self.partition_obj_name(xp, yp)))
        return obj

    def get_net_load(self, net_id_or_name):
        """单网网负载点查（plan §3.2 debug API；入参 = 网全局 id int 或
        层级全路径名 str——str 经 design db 名字 mapper 转换；未命中返
        回 None：全部分区对象都无该网条目）。

        同一网在各分区的副本数值相同（按连接位分区复制、每区自足），
        任取一份。数值 = 电气网合计（跨层次电气网口径——同一张电气网各
        段的负载在合并层已相加，2026-11-24 用户裁定）：任一段的网编号查
        到的都是整张电气网的负载。返回 dict：net id + 三电容（行首自报
        总电容/对地合计/耦合合计，飞法）+ 贡献来源位图 + 缩减形态标记
        （布尔）+ 该网在哪些分区有副本（[(xp, yp), …] 行主序）+
        electrical_members——同一张电气网的全部成员网编号（升序，含自
        己；并查集不在表的网 = [自己]）。
        """
        if isinstance(net_id_or_name, str):
            from emir.design import DesignDb
            gid = self._design_db().convert_to_id("net", net_id_or_name)
            if gid is None:
                return None
        else:
            gid = int(net_id_or_name)
        partitions = []
        entry = None
        for xp, yp in self._design_partitions():
            part = self._load_partition_obj(xp, yp)
            found = part.find_net(gid)
            if found is not None:
                partitions.append((xp, yp))
                if entry is None:
                    entry = found
        if entry is None:
            return None
        union = self._design_net_union()
        root = union.find(gid)
        members = sorted(int(m) for m in union.members(root))
        if not members:
            # 不在并查集表（未参与跨块连接——平展常态）：自身即单成员
            # 电气网（find 恒一步返回自身，members 对非组根未命中）
            members = [gid]
        return {
            "net_id": gid,
            "total_cap_reported_ff": entry.total_cap_reported_ff,
            "ground_cap_ff": entry.ground_cap_ff,
            "coupling_cap_ff": entry.coupling_cap_ff,
            "source_mask": entry.source_mask,
            "reduced": bool(entry.flags & 0x1),
            "partitions": partitions,
            "electrical_members": members,
        }


# ── header schema（2026-09-17 裁定范式：结构化声明 + 命名 validator，
#    禁止内联 lambda；白名单严格模式——未知键/非法值直接 raise）──

_path_schema = Schema(str, check=is_nonempty_str,
                      error="must be a non-empty file path, got {value}")


def _is_design_db_handle(value):
    """design_db 参数判定（DesignDb 句柄——带 DESIGN_OBJ 属性的 db 对象）。"""
    return hasattr(value, "DESIGN_OBJ")


_sp_binding_desc_schema = Schema.dict(
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

# spef_files 元素 schema（str 纯路径 | dict 块绑定描述符——timing/waveform
# 名字空间三形态）
_sp_file_schema = Schema.any_of(_path_schema, _sp_binding_desc_schema)

_settings_schema = Schema.dict(
    allow_extra=False,
    extra_error="spef settings currently has no available keys, "
                "unexpected key {key}")

_alpha_schema = Schema.dict(
    optional={
        "chunk_size_mb": Schema(int, check=is_chunk_size_mb,
                                error="must be an integer >= 16, got "
                                      "{value}"),
        "value_index": Schema(int, check=is_value_index,
                              error="must be an integer >= 0, got "
                                    "{value}"),
    },
    allow_extra=False)

build_spef_db_doc = UserDoc(
    "构建 spef db：解析 SPEF（标准寄生交换格式，IEEE 1481）寄生参数文"
    "件，把逐网寄生电容换算为 design db 全局网 id 并按 design db 分区结"
    "构落库。落库数值为内部单位（电容飞法）：行首自报总电容 + 对地电容"
    "合计 + 耦合电容合计三值并存，附贡献来源位图与缩减形态标记。全部文"
    "件解析完成后库冻结可读。")
build_spef_db_doc.add_param("name",
    schema=Schema(str, check=is_nonempty_str,
                  error="must be a non-empty string, got {value}"),
    required=True, desc="db 子目录名 + Project 内部 key（重名自动递增）")
build_spef_db_doc.add_param("spef_files",
    schema=Schema.list(_sp_file_schema, min_len=1),
    required=True, desc="SPEF 文件输入列表（至少 1 个）。元素 = 纯路径字"
        "符串（条目使用全层级路径名）或块绑定描述符 dict：{'file_name'"
        "（必填）, 'block_inst' / 'block_cell'（互斥，恰传其一）, "
        "'strip_prefix'（可选，只能伴随块绑定）}。文件必须存在且可读（否"
        "则立即报错）；gzip 压缩 SPEF 自动识别解压（建库结果与明文一"
        "致）；其余非 SPEF 格式文件（缺失单位头等）导致建库失败（库不冻"
        "结）。多文件：不同来源对同一网的贡献相加；同一来源（同为纯路"
        "径、绑同一块实例、或绑同一块定义）的文件描述同一网时保留首份并"
        "提醒。绑定目标（块实例路径/块 cell 名）在 design db 中找不到的"
        "文件跳过并提醒（全部文件都找不到则建库失败）")
build_spef_db_doc.add_param("design_db",
    schema=Schema(object, check=_is_design_db_handle,
                  error="must be a DesignDb instance, got {value}"),
    required=True, desc="design db（DesignDb 实例）。寄生条目按其网名/"
        "分区结构匹配归属，分区结构与之一致")
build_spef_db_doc.add_param("settings",
    schema=_settings_schema,
    required=False, default=None, none_ok=True,
    desc="创建设置（dict）。当前无可用键：传入任何键将直接报错（None "
         "合法）。总电容的消费口径（用行首自报值还是明细合计）属下游消"
         "费侧——本库保存原始三值")
build_spef_db_doc.add_param("alpha",
    schema=_alpha_schema,
    required=False, default=None, none_ok=True,
    desc="实验性配置（dict）。可用键：chunk_size_mb——单文件切块大小"
         "（MB），整数 ≥16，默认 256；value_index——多值形态取第几个值"
         "（整数 ≥0），缺省按内置规则（三个值取第 1 个典型值、其它个数"
         "取第 0 个）。未知键或非法值直接报错")
build_spef_db_doc.add_example("SPEF 建库 + 读网负载",
    code='''spef_db = proj.build_spef_db(
    name="spef", spef_files=["parasitics.spef"], design_db=design_db)
proj.wait_frozen("spef", timeout=600)
summary = spef_db.load_spef_summary_obj()
net_load = spef_db.load_partition_net_load_obj(0, 0)''',
    desc="SPEF 解析 + 名字匹配归属 → 冻结后读汇总与分区网负载")
build_spef_db_doc.add_keyword(["spef", "parasitic", "capacitance",
                               "net", "load", "emir"])


@register_flow(EMIRProject)
@document(build_spef_db_doc)
def build_spef_db(self, name: str, spef_files: list, design_db,
                  settings: dict = None, alpha: dict = None):
    """构建 spef db：SPEF 解析 + 名字匹配归属 + 冻结。

    全部文件解析完成后库冻结可读。gzip 压缩 SPEF 自动识别解压（建库
    结果与明文一致）。参数不合法（空名、spef_files/settings/alpha 含
    未知键或非法值、design_db 类型不符、文件不存在/不可读）时立即报错
    终止，不建库；非 SPEF 格式文件（缺失单位头等）在解析任务中失败（
    库不冻结）。跨来源贡献相加、同来源重合保留首份并提醒；绑定目标
    存在性在解析阶段校验，未命中的文件跳过并计入汇总提醒（不阻塞其他
    文件入库）。

    Args:
        self: 自动绑定的 EMIRProject 实例。
        name: db 子目录名 + Project 内部 key（重名自动递增）。
        spef_files: SPEF 文件输入列表（纯路径或块绑定描述符 dict，文件
            须存在且可读；gzip 压缩自动解压）。
        design_db: DesignDb 实例（寄生条目按其网名/分区结构匹配归属）。
        settings: 创建设置（当前无可用键，传任何键报错；None 合法）。
        alpha: 实验性配置（chunk_size_mb / value_index；未知键或非法值
            报错；None 合法）。

    Returns:
        ``SpefDb`` 句柄（解析与冻结异步进行，可用 wait_frozen 等待）。
    """
    # ── ① 轻量参数预处理：schema 已由 header 拦截；文件存在性（元数
    #    据级，不读内容——形态错由文件入口任务失败透出）+ 建库 + alpha
    #    写入（header 已拦截非法值，apply 防御性覆盖）──
    from .sp_utils import normalize_spef_files
    files = normalize_spef_files(spef_files)
    for f in files:
        ensure_readable_file(f["file_name"], "build_spef_db", "spef_files")

    # 直接前驱 design db 入链（dev-rules §3 数据库链——debug API
    # find_db 与读库函数的依赖来源）
    db = self._create_db(name, db_cls=SpefDb, prev=[design_db])
    alpha_settings = get_default_alpha_settings()
    alpha_settings.apply(alpha)
    db.write_object(SpefDb.ALPHA_SETTINGS_OBJ, alpha_settings)
    # alpha 展开值随根任务参数下发（header 已拦截非法值；value_index
    # 的 None = 未给，消费侧按缺省取值规则——裁定 6）
    chunk_size = alpha_settings.chunk_size_mb * 1024 * 1024
    value_index_given = alpha_settings.value_index is not None
    value_index = alpha_settings.value_index if value_index_given else 0

    # ── ② flow 根任务 + ③ freeze（master 提交 O(1) 个任务——§20
    #    编排判据；freeze 依赖固定标记 summary）──
    from .sp_flow import _freeze_spef_task, _spef_flow_task
    _spef_flow_task(db, design_db, files, chunk_size, value_index,
                    value_index_given)
    _freeze_spef_task(db)

    INFO(f"build_spef_db: '{name}' submitted ({len(files)} spef file(s))")
    return db
