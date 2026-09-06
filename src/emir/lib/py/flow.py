"""build_lib_db flow — lib 库 db 的创建流程（EMIRProject 第一个 flow）。

多文件分布式解析 + LIBLibrary 整合（MapReduce，docs/emir-data-flow.md
§4 裁定 14）：每 .lib 文件一个分区（独立解析任务，天然分布式点），全量
合并产出单一 LIBLibrary 容器写入 lib db；freeze 依赖 LIBLibrary 写完。
"""

import os

from _fly_log import INFO
from _fly_emir_lib import (
    EXLIBLibrary,
    lib_parse_lib_file,
)
from fly import as_task, register_flow
from fly import UserDoc, Schema, document
from fly.mapreduce import MapReduceJob

# 循环 import：本模块由 emir.project.py.project 尾部触发（注册链起点），
# 此处从「定义模块」取 EMIRProject——彼时该模块尚在部分初始化，但
# EMIRProject 类已执行定义进入命名空间（包根 emir.project 则未完成，
# 不可从这里取）。与 solver/flows.py 的 from .project import 同构。
from emir.project.py.project import EMIRProject
from .db import LibDb


# ── 内部 task：freeze（依赖 LIBLibrary 写完，由 master 调度）──────────

@as_task(inputs=lambda db, dep_keys: list(dep_keys))
def _freeze_db_task(db, dep_keys):
    """冻结 db。inputs 依赖 LIBLibrary 对象写完后才被 master 调度。"""
    db.freeze()


# ── MapReduce 各阶段函数（worker 上执行）────────────────────────────

def _lib_parse_one(part_path: str) -> EXLIBLibrary:
    """processor：解析单个 .lib 文件（C++ 解析器，每文件一独立任务）。"""
    if not os.path.isfile(part_path):
        raise FileNotFoundError(f"build_lib_db: lib file missing: {part_path}")
    return lib_parse_lib_file(part_path)


def _lib_merge_two(a: EXLIBLibrary, b: EXLIBLibrary) -> EXLIBLibrary:
    """merger（二元）：把 b 的 cell 并入 a（cell name 冲突 = 库版本混用，报错）。

    注意 nanobind def_rw 的 vector 属性读取时是拷贝出的临时 list——必须
    整体赋值（走 setter 回写 C++ vector），不能 in-place append。
    """
    from _fly_log import WARN

    if b.name != a.name:
        WARN(f"build_lib_db: library name mismatch: '{b.name}' vs '{a.name}' "
             f"(keeping first)")
    merged_cells = list(a.cells)
    seen = {c.name for c in merged_cells}
    for cell in b.cells:
        if cell.name in seen:
            raise ValueError(
                f"build_lib_db: duplicate cell name '{cell.name}' across lib "
                f"files (library version mixing?)")
        merged_cells.append(cell)
        seen.add(cell.name)
    a.cells = merged_cells
    return a


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
build_lib_db_doc.add_example("构建单元库",
    code='''lib_db = proj.build_lib_db(name="lib", lib_paths=["nangate45_typ.lib"])
proj.wait_frozen("lib", timeout=600)
library = lib_db.load_library()   # EXLIBLibrary 整合容器''',
    desc="多文件解析 → LIBLibrary 汇整 → 冻结后读容器")
build_lib_db_doc.add_keyword(["lib", "liberty", "cell", "power", "timing", "emir"])


@register_flow(EMIRProject)
@document(build_lib_db_doc)
def build_lib_db(self, name: str, lib_paths: list):
    """构建 lib 库 db：分布式解析多份 .lib 并整合为 LIBLibrary。

    异步 4 步：检查输入 → 建库（LibDb，role="lib"）→ MapReduce 提交
    （每文件一解析任务 + 全量合并）→ freeze task（依赖 LIBLibrary 写完）。

    Args:
        self: 自动绑定的 EMIRProject 实例。
        name: db 子目录名 + Project 内部 key。
        lib_paths: .lib 文件路径列表。

    Returns:
        ``LibDb`` 句柄（freeze 异步进行中，可用 wait_frozen 等待）。
    """
    # ── Step 1: 检查输入（文件存在性，schema 无法覆盖）──
    for p in lib_paths:
        if not os.path.isfile(p):
            raise FileNotFoundError(f"build_lib_db: lib file not found: {p}")

    # ── Step 2: 建库（LibDb，role="lib"）──
    db = self._create_db(name, db_cls=LibDb)

    # ── Step 3: MapReduce 分布式解析 + 整合（对象驱动异步推进）──
    mr = MapReduceJob(db, output_name=LibDb.LIBRARY_OBJ)
    mr.set_partitioner(lambda paths: list(paths))   # 每文件一分区
    mr.set_processor(_lib_parse_one)
    mr.set_merger(_lib_merge_two, merge_type="full")
    mr.run(input_data=list(lib_paths))

    # ── Step 4: 提交 freeze task（依赖 LIBLibrary 写完）──
    _freeze_db_task(db, [mr.get_output_name()])

    INFO(f"build_lib_db: '{name}' submitted ({len(lib_paths)} lib files, "
         f"MapReduce distributed parse)")
    return db
