"""LibDb — lib 库 db（role="lib"）与建库流程入口 build_lib_db。

保存 Liberty 单元库数据：顶层产出为单一整合容器 LIBLibrary
（cell 集合 + 库头单位与默认参数 + 查找表模板集，docs/emir-data-flow.md
§4 裁定 10），lib 阶段以 cell name 区分 cell；cell id 由 design db 建库时
读入 LIBLibrary 后分配（裁定 9）。

MapReduce 四阶段装配在 lib_flow.run_lib_flow（流程实现与入口分离）。
"""

from log import INFO

from fly import register_flow
from fly import UserDoc, Schema, document
from storage import Database

# A6 后由 emir/__init__ 聚合触发（先 project 后 lib），此处包根已完成
# 初始化，可安全取 EMIRProject（尾部迂回注册链已移除）。
from emir.project import EMIRProject


class LibDb(Database):
    """lib 库 db。role="lib"。"""

    role = "lib"

    # LIBLibrary 容器对象名（唯一正式数据对象）
    LIBRARY_OBJ = "LIBLibrary"
    # 建库 alpha 设置对象（LIBAlphaSettings，声明式五要素 2026-09-13 裁
    # 定；master 侧随建库写入，消费点 read_object 读回 + normalize 兜底）
    ALPHA_SETTINGS_OBJ = "alpha_settings"

    def load_library(self):
        """读取 LIBLibrary 整合容器（EXLIBLibrary 对象）。"""
        return self.read_object(self.LIBRARY_OBJ)


# ── flow：build_lib_db ──────────────────────────────────────────────

build_lib_db_doc = UserDoc(
    "构建 lib 库 db：解析多份 Liberty（.lib）单元库文件，分布式解析后整合为"
    "单一 LIBLibrary 容器（cell 集合 + 库头单位与默认参数 + 查找表模板集）。"
    "保存引脚电容、internal_power 功耗表、timing 时序表等全量数据表。")
build_lib_db_doc.add_param("name",
    schema=Schema(str, check=lambda s: len(s) > 0, error="must not be empty"),
    required=True, desc="db 子目录名 + Project 内部 key（重名自动递增）")
build_lib_db_doc.add_param("lib_paths",
    schema=Schema(list, check=lambda ps: len(ps) > 0 and all(
        isinstance(p, str) and p for p in ps),
        error="must be a non-empty list of non-empty file paths"),
    required=True, desc=".lib 文件路径列表（每文件一独立解析任务，天然分布式）")
build_lib_db_doc.add_param("alpha",
    schema=Schema(dict), required=False, default=None, none_ok=True,
    desc="未稳定配置项（dict）；键经 LIBAlphaSettings 声明式定义（五要素"
         "：src/emir/lib/py/alpha_settings.py，2026-09-13 裁定）——lib 首"
         "版无激活键，未知键 LIBR::0005 一次汇总提醒后忽略，不 raise")
build_lib_db_doc.add_example("构建单元库",
    code='''lib_db = proj.build_lib_db(name="lib", lib_paths=["nangate45_typ.lib"])
proj.wait_frozen("lib", timeout=600)
library = lib_db.load_library()   # EXLIBLibrary 整合容器''',
    desc="多文件解析 → LIBLibrary 汇整 → 冻结后读容器")
build_lib_db_doc.add_keyword(["lib", "liberty", "cell", "power", "timing", "emir"])


@register_flow(EMIRProject)
@document(build_lib_db_doc)
def build_lib_db(self, name: str, lib_paths: list, alpha: dict = None):
    """构建 lib 库 db：分布式解析多份 .lib 并整合为 LIBLibrary。

    异步 4 步：检查输入 → 建库（LibDb，role="lib"）→ MapReduce 提交
    （每文件一解析任务 + 全量合并，语义见 lib_flow.run_lib_flow）→ freeze
    task（依赖 LIBLibrary 写完）。cell 重复 = 库版本混用：保留当前、抛弃
    后续重复 + LIBR::0001 提醒（不抛异常）。alpha 设置经 LIBAlphaSettings
    声明式校验后随建库写入（问题一次汇总 LIBR::0005，不 raise）。

    Args:
        self: 自动绑定的 EMIRProject 实例。
        name: db 子目录名 + Project 内部 key。
        lib_paths: .lib 文件路径列表。
        alpha: 未稳定配置项（lib 首版无激活键；未知键 LIBR::0005 提醒忽
            略）。

    Returns:
        ``LibDb`` 句柄（freeze 异步进行中，可用 wait_frozen 等待）。
    """
    import os

    # ── Step 1: 检查输入（文件存在性 + liberty 形态嗅探，schema 无法覆盖）──
    from .lib_utils import sniff_liberty_header
    for p in lib_paths:
        if not os.path.isfile(p):
            raise FileNotFoundError(f"build_lib_db: lib file not found: {p}")
        sniff_liberty_header(p)

    # ── Step 2: 建库（LibDb，role="lib"）──
    db = self._create_db(name, db_cls=LibDb)

    # ── Step 2.5: alpha 设置（声明式五要素，2026-09-13 裁定）：逐键校验
    #    覆盖 → 问题一次汇总 LIBR::0005（user warn，不 raise）→ settings
    #    对象随建库写入 db（消费点 read_object 读回 + normalize 兜底）──
    from .alpha_settings import get_default_alpha_settings
    alpha_settings = get_default_alpha_settings()
    apply_result = alpha_settings.apply(alpha)
    detail = alpha_settings.format_apply_result(apply_result)
    if detail:
        from fly import message
        message("LIBR::0005", 0, f"invalid lib alpha settings — {detail}")
    db.write_object(LibDb.ALPHA_SETTINGS_OBJ, alpha_settings)

    # ── Step 3 + 4: MapReduce 分布式解析整合 + freeze 提交 ──
    from .lib_flow import run_lib_flow
    run_lib_flow(db, lib_paths)

    INFO(f"build_lib_db: '{name}' submitted ({len(lib_paths)} lib files, "
         f"MapReduce distributed parse)")
    return db
