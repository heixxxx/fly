"""DesignDb — design db（role="design"）与建库流程入口 build_design_db。

保存物理设计库数据：tech lef → DSStack（层堆叠，裁定 ⑭ 独立对象）+ via
集合（VIARULE 展开模板，㉚）；cell lef → DSCell 集合 + 简化 pin（裁定
⑰）+ pin 几何独立对象（R4 键 = 全局 pin id）；S3 与 lib 库 merge（裁定
⑯）产出 lib 关联与 DSPinTables（逐 pin 落位）；DEF 头扫描 → block cell
（㉙ = DSCell + block_cell 位，DIEAREA 双存 bbox/polygon ㉞）+ port 几何 +
via cell 权威表（⑫ design_name:: 前缀）。DSDesign 容器的运行时专用字段
（pin 表/几何）不序列化（裁定 ⑱），业务方经 load_design_with 按需注入。

阶段链装配在 ds_flow.run_design_flow（流程实现与入口分离）。
约定（择简）：lef_paths[0] 为 tech lef（确立 DBU 基准与层堆叠），其余
为 cell lef。
"""

from log import INFO

from fly import register_flow
from fly import UserDoc, Schema, document
from storage import Database

# A6 后由 emir/__init__ 聚合触发（先 project 后 design），此处包根已完成
# 初始化，可安全取 EMIRProject。
from emir.project import EMIRProject


class DesignDb(Database):
    """design db。role="design"。"""

    role = "design"

    # 正式持久化对象名（裁定 ⑭/⑰/⑱：Stack 独立对象 + design 容器 +
    # 两个运行时注入独立对象）+ per-DEF 实例产物（DSBlock_<i>：instances/
    # density/stats，按 def_paths 序号，block 名冗余在对象 block_name_
    # 字段）+ per-DEF 名字伴生对象（DSBlockNames_<i>：instance/net 两
    # hasher，R7 ㊵② 落盘拆分——按需加载其一）
    DESIGN_OBJ = "DSDesign"
    STACK_OBJ = "DSStack"
    PIN_TABLES_OBJ = "DSPinTables"
    PIN_GEOMETRY_OBJ = "DSPinGeometry"
    # S8 全局密度图（三通道独立对象，不进 DSDesign 容器——⑬ 大体量数据
    # 独立对象；S8 任务唯一写定）
    GLOBAL_DENSITY_OBJ = "global_density"
    # 建库 alpha 设置对象（DSAlphaSettings，声明式五要素 2026-09-13 裁
    # 定；master 侧随建库写入，消费点 read_object 读回 + normalize 兜底）
    ALPHA_SETTINGS_OBJ = "alpha_settings"

    @staticmethod
    def block_obj_name(index: int) -> str:
        """per-DEF 实例产物对象名（DSBlockBuildData，按 def_paths 序号；
        不含名字——名字在 names_obj_name 伴生对象）。"""
        return f"DSBlock_{index}"

    @staticmethod
    def names_obj_name(index: int) -> str:
        """per-DEF 名字伴生对象名（DSBlockNames，按 def_paths 序号，
        与 block_obj_name 同序对齐；R7 ㊵②）。"""
        return f"DSBlockNames_{index}"

    @staticmethod
    def net_obj_name(index: int) -> str:
        """per-DEF 网内容产物对象名（DSNetBuildData，按 def_paths 序号，
        与 block_obj_name 同序对齐）。"""
        return f"DSNet_{index}"

    def load_design(self):
        """读取 DSDesign 容器（EXDSDesign 对象，不含运行时注入字段）。"""
        return self.read_object(self.DESIGN_OBJ)

    def load_stack(self):
        """读取 DSStack 层堆叠（EXDSStack 对象）。"""
        return self.read_object(self.STACK_OBJ)


build_design_db_doc = UserDoc(
    "构建 design db：解析 tech lef（层堆叠/DBU 基准/通孔定义）+ 多份 cell "
    "lef（macro/简化 pin/禁布区/pin 几何）+ 多份 DEF（DIEAREA/port/通孔"
    "定义），并与 lib 库 db 按 cell 名 merge（填 lib 字段与关联、提取功耗/"
    "时序表）。lef_paths[0] 按 tech lef 解析，其余按 cell lef 解析。")
build_design_db_doc.add_param("name",
    schema=Schema(str, check=lambda s: len(s) > 0, error="must not be empty"),
    required=True, desc="db 子目录名 + Project 内部 key（重名自动递增）")
build_design_db_doc.add_param("def_paths",
    schema=Schema(list, check=lambda ps: all(isinstance(p, str) and p for p in ps),
                  error="must be a list of non-empty file paths"),
    required=True, desc="DEF 文件路径列表（每文件一独立头扫描任务；可为空列表）")
build_design_db_doc.add_param("lef_paths",
    schema=Schema(list, check=lambda ps: len(ps) > 0 and all(
        isinstance(p, str) and p for p in ps),
        error="must be a non-empty list of non-empty file paths"),
    required=True, desc="lef 文件路径列表；首元素为 tech lef，其余为 cell lef")
build_design_db_doc.add_param("lib_db",
    schema=Schema(object, check=lambda v: hasattr(v, "LIBRARY_OBJ"),
                  error="must be a LibDb instance"),
    required=True, desc="lib 库 db（LibDb 实例，S3 merge 的直接前驱）")
build_design_db_doc.add_param("settings",
    schema=Schema(dict), required=False, default=None, none_ok=True,
    desc="稳定配置项（dict）；首版无激活键，保留参数位")
build_design_db_doc.add_param("alpha",
    schema=Schema(dict), required=False, default=None, none_ok=True,
    desc="未稳定配置项（dict）；键经 DSAlphaSettings 声明式定义（五要素："
         "src/emir/design/py/alpha_settings.py，2026-09-13 裁定）："
         "density_bin_size（密度采样格边长，µm，≥1，缺省 10）、"
         "net_batch_size（网内容批界网数，≥1，缺省 1000）、lcp_name_arena"
         "（R8d 裁定 55：bool，缺省 False——名字伴生对象 instance/net 两 "
         "hasher id→name 侧 LCP 后缀压缩封口，容量换内存的 alpha 路径）、"
         "S8 分区决策四键：target_partitions（'{x}x{y}' 直切，如 '4x3'，"
         "None=未设置）、partition_count（总分区数，0=未设置）、"
         "partition_target_density（目标合成负载，缺省 150000——N = "
         "ceil(总负载/目标)）、density_channel_weights（通道比重 dict "
         "{'instance': 6, 'metal': 2, 'via': 2}，缺 key 用默认）；优先级 "
         "target_partitions > partition_count > partition_target_density；"
         "非法值/未知键 DSGN::0013 一次汇总提醒后回退默认/忽略，不 raise")
build_design_db_doc.add_example("构建 design db",
    code='''design_db = proj.build_design_db(
    name="design", def_paths=["block.def"], lef_paths=["tech.lef", "cells.lef"],
    lib_db=lib_db)
proj.wait_frozen("design", timeout=600)
design = design_db.load_design()   # EXDSDesign 容器''',
    desc="lef/def 解析 + lib merge → 冻结后读容器")
build_design_db_doc.add_keyword(["design", "def", "lef", "stack", "via",
                                 "block", "port", "emir"])


@register_flow(EMIRProject)
@document(build_design_db_doc)
def build_design_db(self, name: str, def_paths: list, lef_paths: list,
                    lib_db, settings: dict = None, alpha: dict = None):
    """构建 design db：lef/def 解析 + lib merge + 冻结。

    异步 4 步：检查输入 → 建库（DesignDb，role="design"）→ 阶段链提交
    （S1 tech lef → S2 每 cell lef 一任务 + 汇总 → S3 lib merge ∥ S4+S4b
    每 DEF 一任务 + 汇总 → freeze，语义见 ds_flow.run_design_flow）。
    重名 macro/port/via 保留首份抛弃后续 + DSGN 消息提醒（不抛异常）。

    Args:
        self: 自动绑定的 EMIRProject 实例。
        name: db 子目录名 + Project 内部 key。
        def_paths: DEF 文件路径列表。
        lef_paths: lef 文件路径列表（首元素 tech lef，其余 cell lef）。
        lib_db: LibDb 实例（S3 merge 的直接前驱）。
        settings: 稳定配置项（首版无激活键）。
        alpha: 未稳定配置项（七键经 DSAlphaSettings 声明式定义，见
            UserDoc；非法值/未知键 DSGN::0013 提醒后回退/忽略）。

    Returns:
        ``DesignDb`` 句柄（freeze 异步进行中，可用 wait_frozen 等待）。
    """
    import os

    # ── Step 1: 检查输入（文件存在性 + LEF/DEF 形态嗅探，schema 无法覆盖；
    #    master 侧前置）──
    from .ds_utils import sniff_def_header, sniff_lef_header
    for p in lef_paths:
        if not os.path.isfile(p):
            raise FileNotFoundError(f"build_design_db: lef file not found: {p}")
        sniff_lef_header(p)
    for p in def_paths:
        if not os.path.isfile(p):
            raise FileNotFoundError(f"build_design_db: def file not found: {p}")
        sniff_def_header(p)

    # ── Step 2: 建库（DesignDb，role="design"）──
    db = self._create_db(name, db_cls=DesignDb)

    # ── Step 2.5: alpha 设置（声明式五要素，2026-09-13 裁定）：逐键校验
    #    覆盖 → 问题一次汇总 DSGN::0013（user warn，不 raise）→ settings
    #    对象随建库写入 db（消费点 read_object 读回 + normalize 兜底）──
    from .alpha_settings import get_default_alpha_settings
    alpha_settings = get_default_alpha_settings()
    apply_result = alpha_settings.apply(alpha)
    detail = alpha_settings.format_apply_result(apply_result)
    if detail:
        from fly import message
        message("DSGN::0013", 0, f"invalid design alpha settings — {detail}")
    db.write_object(DesignDb.ALPHA_SETTINGS_OBJ, alpha_settings)

    # ── Step 3 + 4: 阶段链提交 + freeze 提交 ──
    from .ds_flow import run_design_flow
    run_design_flow(db, lef_paths, def_paths, lib_db)

    INFO(f"build_design_db: '{name}' submitted "
         f"({len(lef_paths)} lef files [1 tech + {len(lef_paths) - 1} cell], "
         f"{len(def_paths)} def files)")
    return db
