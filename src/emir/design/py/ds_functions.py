"""design 模块对外函数层——供其他模块读取 design db 的容器与独立对象。

不含解析入口（解析是 design db 的内部流程，parse/merge 函数仅经
ds_export 供 ds_utils/测试使用，不对外暴露）。

依赖声明规范（DEVELOPMENT_GUIDELINES Section 17，裁定 ㊼）：本模块全部
API 内部 read_object，一律 @wait_obj 包装声明自身数据依赖（inputs 用
lambda 声明防依赖漂移）。调用规范：
  - 本地直调（脚本/交互）：数据未就绪时 API 自动轮询等待（wait_obj 语
    义；确认无法产出时 RuntimeError 兜底）；
  - task 内调用：上层 as_task 的 inputs 必须以 `api.deps(db) + [自身其他
    依赖]` 传播数据依赖，函数体用 `run_direct(api, db)` 剥离本地等待直跑
    （依赖已声明必然就绪，省 wait_obj 轮询/master 查询的冗余网络 IO）。
"""

from fly import wait_obj

from .ds_db import DesignDb
from .ds_export import ds_make_name_mapper


@wait_obj(inputs=lambda db: [db.get_full_name(DesignDb.DESIGN_OBJ)])
def load_design(db):
    """读取 DSDesign 容器（EXDSDesign 对象，不含运行时注入字段）。

    调用规范见模块 docstring：task 内调用须 `load_design.deps(db)` 传播
    依赖 + `run_direct(load_design, db)` 直跑。
    """
    return db.read_object(DesignDb.DESIGN_OBJ)


@wait_obj(inputs=lambda db: [db.get_full_name(DesignDb.STACK_OBJ)])
def load_design_stack(db):
    """读取 DSStack 层堆叠（EXDSStack 对象）。

    调用规范见模块 docstring：task 内调用须 `load_design_stack.deps(db)`
    传播依赖 + `run_direct(load_design_stack, db)` 直跑。
    """
    return db.read_object(DesignDb.STACK_OBJ)


@wait_obj(inputs=lambda db: [db.get_full_name(DesignDb.GLOBAL_DENSITY_OBJ)])
def load_global_density(db):
    """读取 S8 全局密度图（EXDSDensityGrid 对象：三通道 + 合并后格网）。

    调用规范见模块 docstring：task 内调用须 `load_global_density.deps(db)`
    传播依赖 + `run_direct(load_global_density, db)` 直跑。
    """
    return db.read_object(DesignDb.GLOBAL_DENSITY_OBJ)


@wait_obj(inputs=lambda db: [db.get_full_name(DesignDb.NET_UNION_OBJ)])
def load_design_net_union(db):
    """读取 S7 跨块连接归并结果（EXDSNetUnion：find 恒一步（不在表 = 自
    身）/ root → 成员枚举 / class_count / dangling_count——仅 port 相连
    网，internal net 不在表）。

    调用规范见模块 docstring：task 内调用须 `load_design_net_union.deps(db)`
    传播依赖 + `run_direct(load_design_net_union, db)` 直跑。
    """
    return db.read_object(DesignDb.NET_UNION_OBJ)


@wait_obj(inputs=lambda db, pin_tables=False, pin_geometries=False: (
    [db.get_full_name(DesignDb.DESIGN_OBJ)]
    + ([db.get_full_name(DesignDb.PIN_TABLES_OBJ)] if pin_tables else [])
    + ([db.get_full_name(DesignDb.PIN_GEOMETRY_OBJ)] if pin_geometries else [])
))
def load_design_with(db, pin_tables: bool = False, pin_geometries: bool = False):
    """⑱ 统一加载封装：load DSDesign + 按需 set 运行时注入字段。

    业务方按需加载独立对象（DSPinTables/DSPinGeometry）并注入容器专用
    字段；下游经 DSDesign.get_cell 以指针注入 cell（零拷贝）。返回容器。

    依赖按实参条件化解析（deps(db, pin_tables=..., pin_geometries=...)）：
    上层 task 的 inputs 传播时须以相同实参调 deps，保证条件依赖不漂移。
    其余调用规范见模块 docstring。
    """
    design = db.read_object(DesignDb.DESIGN_OBJ)
    if pin_tables:
        design.set_pin_tables(db.read_object(DesignDb.PIN_TABLES_OBJ))
    if pin_geometries:
        design.set_pin_geometry(db.read_object(DesignDb.PIN_GEOMETRY_OBJ))
    return design


@wait_obj(inputs=lambda db, index: [
    db.get_full_name(DesignDb.names_obj_name(index))])
def load_block_names(db, index: int):
    """㊵② 按需读取 per-DEF 名字伴生对象（DSBlockNames_<index>：instance/
    net 两 hasher + block 名）。只读实例/密度数据的场景不加载本对象
   （⑰/⑱ 按需加载——名字是存储大头）。

    调用规范见模块 docstring：task 内调用须 `load_block_names.deps(db,
    index)` 传播依赖 + `run_direct(load_block_names, db, index)` 直跑。
    """
    return db.read_object(DesignDb.names_obj_name(index))


@wait_obj(inputs=lambda db, kind=0, name_indexes=None: (
    [db.get_full_name(DesignDb.DESIGN_OBJ)]
    + ([] if name_indexes is None else
       [db.get_full_name(DesignDb.names_obj_name(i)) for i in name_indexes])
))
def load_name_mapper(db, kind: int = 0, name_indexes: list = None):
    """㊻ 统一加载 API：读 DSDesign + 全部（或指定序号集的）DSBlockNames_<i>
    → ds_make_name_mapper 组装注入式轻壳 mapper（运行时构造不落盘，
    ㊻）。

    kind: 0 = instance 维度（层级实例路径 ↔ global instance id）、
    1 = net 维度（网名路径 ↔ global net id）。name_indexes 缺省 = 自
    0 起逐个读取伴生对象直到缺失（per-DEF 伴生对象按 def_paths 序连续
    落盘）。返回 (design, mapper)——mapper 持 design 内层级树的观察引
    用，两者须同生命周期。

    依赖声明边界：deps 解析 DESIGN_OBJ + 显式 name_indexes 对应的
    DSBlockNames_<i>；name_indexes 缺省（全量读取）时数量仅调用方可知，
    deps 无法静态枚举——上层 task 的 inputs 须自行列全部
    DSBlockNames_<i>（编排侧持有 def_paths 数量）再叠加
    `load_name_mapper.deps(db, kind, name_indexes)`。其余调用规范见模
    块 docstring。
    """
    design = db.read_object(DesignDb.DESIGN_OBJ)
    if name_indexes is None:
        names_list = []
        i = 0
        while True:
            try:
                names_list.append(
                    db.read_object(DesignDb.names_obj_name(i)))
            except Exception:
                break  # 连续段结束（per-DEF 伴生对象序 = def_paths 序）
            i += 1
    else:
        names_list = [db.read_object(DesignDb.names_obj_name(i))
                      for i in name_indexes]
    mapper = ds_make_name_mapper(design, names_list, kind)
    return design, mapper
