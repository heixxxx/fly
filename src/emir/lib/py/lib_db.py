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

from emir.common import ensure_readable_file, is_nonempty_str

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

# header schema（2026-09-17 裁定：结构化声明 + 命名 validator，禁止
# 内联 lambda；白名单严格模式——未知键/非法值直接 raise）
_path_schema = Schema(str, check=is_nonempty_str,
                      error="must be a non-empty file path, got {value}")

build_lib_db_doc = UserDoc(
    "构建 lib 库 db：解析一份或多份 Liberty（.lib）单元库文件，产出单一"
    "整合库容器（cell 集合 + 库头单位与默认参数 + 查找表模板集），保存引"
    "脚电容、internal_power 功耗表、timing 时序表等全量数据表。全部文件"
    "解析完成后库冻结可读。")
build_lib_db_doc.add_param("name",
    schema=Schema(str, check=is_nonempty_str,
                  error="must be a non-empty string, got {value}"),
    required=True, desc="db 子目录名 + Project 内部 key（重名自动递增）")
build_lib_db_doc.add_param("lib_paths",
    schema=Schema.list(_path_schema, min_len=1),
    required=True, desc=".lib 文件路径列表（至少 1 个）。文件必须存在且"
        "可读，否则报错；非 liberty 格式报错")
build_lib_db_doc.add_param("alpha",
    schema=Schema.dict(allow_extra=False,
                       extra_error="lib alpha currently has no available "
                                   "keys, unexpected key {key}"),
    required=False, default=None, none_ok=True,
    desc="实验性配置（dict）。当前无可用键：传入任何键将直接报错"
         "（None 合法）")
build_lib_db_doc.add_example("构建单元库",
    code='''lib_db = proj.build_lib_db(name="lib", lib_paths=["nangate45_typ.lib"])
proj.wait_frozen("lib", timeout=600)
library = lib_db.load_library()   # EXLIBLibrary 整合容器''',
    desc="多文件解析 → 冻结后读整合容器")
build_lib_db_doc.add_keyword(["lib", "liberty", "cell", "power", "timing", "emir"])


@register_flow(EMIRProject)
@document(build_lib_db_doc)
def build_lib_db(self, name: str, lib_paths: list, alpha: dict = None):
    """构建 lib 库 db：解析多份 .lib 并整合为单一库容器。

    全部文件解析完成后库冻结可读。跨文件重名 cell（库版本混用迹象）
    保留首份并提醒，不报错终止。参数不合法（空名、空路径列表、alpha
    含未知键、文件不存在/不可读/非 liberty 格式）时立即报错终止，
    不建库。

    Args:
        self: 自动绑定的 EMIRProject 实例。
        name: db 子目录名 + Project 内部 key（重名自动递增）。
        lib_paths: .lib 文件路径列表（至少 1 个；文件须存在且可读）。
        alpha: 实验性配置（当前无可用键，传任何键报错；None 合法）。

    Returns:
        ``LibDb`` 句柄（解析与冻结异步进行，可用 wait_frozen 等待）。
    """
    # ── Step 1: 检查输入（可读性显式校验 + liberty 形态嗅探，schema
    #    无法覆盖）──
    from .lib_utils import sniff_liberty_header
    for p in lib_paths:
        ensure_readable_file(p, "build_lib_db", "lib_paths")
        sniff_liberty_header(p)

    # ── Step 2: 建库（LibDb，role="lib"）──
    db = self._create_db(name, db_cls=LibDb)

    # ── Step 2.5: alpha 设置：header schema 已拦截未知键/非法值（直接
    #    raise）；此处 apply 防御性覆盖（理论不再拒绝）+ settings 对象随
    #    建库写入 db（消费点 read_object 读回 + normalize 兜底，向前兼容
    #    旧 db 对象）──
    from .alpha_settings import get_default_alpha_settings
    alpha_settings = get_default_alpha_settings()
    alpha_settings.apply(alpha)
    db.write_object(LibDb.ALPHA_SETTINGS_OBJ, alpha_settings)

    # ── Step 3 + 4: 并行解析整合 + 冻结提交 ──
    from .lib_flow import run_lib_flow
    run_lib_flow(db, lib_paths)

    INFO(f"build_lib_db: '{name}' submitted ({len(lib_paths)} lib files, "
         f"parallel per-file parse)")
    return db
