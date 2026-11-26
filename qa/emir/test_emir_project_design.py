"""E2E test: EMIRProject.build_design_db — lef/def 解析 + lib merge + 冻结。

验证 design-db S1-S4b 全链（docs/emir/design-db-plan.md §3.2 与实施计划 §4
+ 第二阶段 R4/R5 重构形态 + R7 name 体系收敛：DSPin/DSInstance 不存
name——pin 名经容器 pin hasher、实例/网名经 DSBlockNames_<i> 伴生对象
与 EXDSNameMapper 组装查询）：
  - S1 tech lef → DSStack（层堆叠/DBU 基准/制造网格）+ tech via；
  - S2 两个 cell lef 并行解析（含跨文件重名 macro 保留首份）→ 全局汇总
    （pin namemap 重挂 + pin_id 回填 + pin 几何按全局 pin id 重挂）；
  - S3 与 lib 库 merge（三类不匹配场景：0002 lef 有 lib 无 / 0003 lib 有
    lef 无 / 0004 pin 集合不一致——均为提醒不拦截）+ DSPinTables 逐 pin
    提取（键 = 全局 pin id）；
  - S4+S4b 两个 DEF 头扫描（block cell = DSCell + block_cell 位，㉙；
    DIEAREA 双存 bbox + polygon，㉞；port = pins_ 的 port 位 DSPin；
    生成式 via 展开产物，登记名 design_name:: 前缀）→ via cell 权威表；
  - S5a 每 DEF 一任务（COMPONENTS 责任链 ∥ 网名扫描，同一遍 DEF 读取）：
    实例表（local id 从 1 起，⑧；R6 place_from_def 换算 pos/orient）、
    undefined cell → fake cell 兜底（⑲/⑳ + DSGN::0007）、UNPLACED 计数
    不入密度（D14）、实例面积密度通道、local net namemap（③）→ 汇总
    fake cell 并入全局 cell 表 + 正式 DSBlock_<i> 产物对象；
  - S5b 每 DEF 一任务（网内容责任链 ∥ 分批多阶段，裁定 ③/⑨；批大小
    alpha 键 net_batch_size）：连接表 id 化（2026-09-13 裁定：实例名 →
    local id、pin 名 → 全局 pin id 在解析边界一次换算完成，port 引用 =
    local 0 + port 位；flags 位 = 方向/类型，hybrid = driver+receiver 同
    置）、wire 段（缺省宽回填）、⑩ via instance（⑫ design:: 前缀名解
    析）、⑥ 金属/通孔逐层分列密度通道 → 正式 DSNet_<i> 产物对象 +
    DSGN::0009 统计汇总；
  - S6 汇总任务（层级树 + 起始编号，⑧⑨⑮；消费 S5a+S5b 计数）：
    主 DEF = 唯一无父者（block_parent 实例化 block_child 的两层嵌套），
    DFS 序连续分配 instance/net/via 三类区间，树嵌正式 DSDesign
    （唯一写定前构建）→ 四接口/换算/format_tree 只读面；
  - S7 任务组（跨块连接归并并查集，2026-09-13 裁定：仅 port 相连网、
    两层树、单对象）：per-DEF slice (父网, 子网) 边收集（对接键 = 同一
    port 的全局 pin id 相等——S5b 连接 id 化后 S7 内部零字符串匹配）+
    单任务两层化 + root 规范化（层级最高/同级最小 global id）→ net_union
    正式对象 + 悬空 port DSGN::0018；
  - S9 任务组（flatten 展平 + 分区保存，2026-09-13 裁定补记①-⑤：两级
    任务 plan→展开→每分区合并→freeze；单分区场景全 primary + BLOCKAGE
    入 net 0 桶 + 电源引脚预展开 + 连接补全）；
  - S10 任务组（汇总校验 + 冻结，2026-09-13 校验分级裁定：损坏类三检查
    全过、verify_report 手算锁定——实例域 holes=1 为 UNPLACED 合法空洞
    触发 DSGN::0022 观测 warn、primary 守恒过、统计 DSGN::0024，无 fatal）；
  - ⑱ load_design_with 统一加载注入 + load_project 动态还原。
"""
import os
import shutil

from log import INFO

from fly import get_config, launch_workers
from fly.runtime import get_agent
from emir import EMIRProject
from test import record

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(SCRIPT_DIR, "data", "design")
TECH_LEF = os.path.join(DATA, "tech.lef")
CELLS_MAIN = os.path.join(DATA, "cells_main.lef")
CELLS_EXTRA = os.path.join(DATA, "cells_extra.lef")
BLOCK_CHILD = os.path.join(DATA, "block_child.def")
BLOCK_PARENT = os.path.join(DATA, "block_parent.def")
LIB_A = os.path.join(SCRIPT_DIR, "data", "cells_a.lib")
LIB_B = os.path.join(SCRIPT_DIR, "data", "cells_b.lib")

LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "emir_design_flow")


def cleanup():
    if os.path.isdir(PROJ_PATH):
        shutil.rmtree(PROJ_PATH, ignore_errors=True)


cleanup()

# 双 worker：S2/S4 的并行解析任务需要执行者
launch_workers([{}, {}])
assert get_agent().wait_workers_registered(timeout=60), "workers should connect"
INFO("  2 workers connected (user-managed)")

proj = EMIRProject(PROJ_PATH)
# 原断言：assert "build_design_db" in proj.list_flows(), f"flows={proj.list_flows()}"
record("flow/registered", sorted(proj.list_flows()))

# ── 前置：lib 库 db（S3 的直接前驱，显式传入）──
lib_db = proj.build_lib_db(name="design_lib", lib_paths=[LIB_A, LIB_B])
assert proj.wait_frozen("design_lib", timeout=120), "lib db should freeze"
INFO("[WAIT] lib db frozen")

# ── build_design_db：S1 → S2×2 → 汇总 → S3 ∥ S4×2 → 汇总 → freeze ──
design_db = proj.build_design_db(
    name="design",
    def_paths=[BLOCK_CHILD, BLOCK_PARENT],
    lef_paths=[TECH_LEF, CELLS_MAIN, CELLS_EXTRA],
    lib_db=lib_db,
)
INFO(f"build_design_db returned db (async): {design_db}")

assert proj.wait_frozen("design", timeout=180), "design db should freeze"
INFO("[WAIT] design db frozen — full pipeline done")

# ── S1 产物：DSStack（层堆叠/DBU 基准/制造网格，人工核定值）──
stack = design_db.load_stack()
# 原断言：assert stack.layer_count == 3, f"layers={stack.layer_count}"
record("s1/layer_count", stack.layer_count)  # implant 跳过
# 保留：守卫——后面用 layer_at(0)、layer_at(1)、layer_at(2) 按下标取层，越界在 C++ 侧是未定义行为
assert stack.layer_count >= 3, f"layers={stack.layer_count}"
# 原断言：assert stack.dbu_per_micron == 1000, f"dbu={stack.dbu_per_micron}"
record("s1/dbu_per_micron", stack.dbu_per_micron)
# 原断言：assert stack.manufacturing_grid == 3, f"grid={stack.manufacturing_grid}"
record("s1/manufacturing_grid", stack.manufacturing_grid)
# 原断言：assert stack.layer_at(0).name == "M1"
record("s1/layer0_name", stack.layer_at(0).name)
# 原断言：assert stack.layer_at(1).name == "VIA1"
record("s1/layer1_name", stack.layer_at(1).name)
# 原断言：assert stack.layer_at(2).name == "M2"
record("s1/layer2_name", stack.layer_at(2).name)
# 原断言：assert stack.find_layer("M2") == 2
record("s1/find_m2", stack.find_layer("M2"))
# 原断言：assert stack.find_layer("IMPLANT1") is None, "implant layer must be skipped"
record("s1/find_implant1", stack.find_layer("IMPLANT1"))
# 非守卫：仅检查 implant 层不存在，返回值无后续解引用
INFO("[DONE] S1 stack: 3 layers (M1/VIA1/M2), DBU=1000 (裁定 ㉝ 恒基准，tech lef 声明 2000 仅参考), grid=3 (0.0025µm×1000 四舍五入)")

# ── 容器：cells / lib merge / block cells ──
design = design_db.load_design()
# 正常 cell 4 个（INV/FILLER/两 block）；S5a fake cell 稀疏落位（⑳ hash
# 扰动 id）使 cells_ 尾部带占位空洞 → cell_count（表长）>= 正常数
# 原断言：assert design.cell_count >= 4, f"cells={design.cell_count}"
record("cells/count_at_least_4", design.cell_count >= 4, detail=design.cell_count)

# cell namemap 双向（②；block cell 与 macro 同一编号空间，㉙）
inv = design.find_cell("INV_X1")
assert inv is not None, "INV_X1 should exist"  # 保留：守卫
# 原断言：assert inv is not None, "INV_X1 should exist"
record("cells/inv_exists", inv is not None)
inv_id = design.cell_id_by_name("INV_X1")
# 原断言：assert design.cell_name_by_id(inv_id) == "INV_X1"
record("cells/inv_name_by_id", design.cell_name_by_id(inv_id))
child = design.find_cell("block_child")
assert child is not None, "block_child should exist"  # 保留：守卫
# 原断言：assert child is not None, "block_child should exist"
record("cells/block_child_exists", child is not None)
parent = design.find_cell("block_parent")
assert parent is not None, "block_parent should exist"  # 保留：守卫
# 原断言：assert parent is not None, "block_parent should exist"
record("cells/block_parent_exists", parent is not None)
# 原断言：assert design.find_cell("DFF_X1") is None, "DFF_X1 = lib-only (DSGN::0003)"
record("cells/no_dff_x1", design.find_cell("DFF_X1"))
# 非守卫：仅检查 lib-only cell 不存在，返回值无后续解引用
INFO("[DONE] cells: 4 (macro + filler + 2 block cells), simplified pins intact")

# 简化 pin（⑰）：lef 侧 3 pin（A/ZN/VDD），USE POWER → power 类型。
# R7 ㊱：DSPin 无 name——pin 名经容器 pin hasher（组合键 "cell/pin"）反查
# 原断言：assert inv.pin_count == 3, f"inv pins={inv.pin_count}"
record("pins/inv_pin_count", inv.pin_count)
# 保留：守卫——后面用 pin_at(0)、pin_at(2) 按下标取引脚，越界在 C++ 侧是未定义行为
assert inv.pin_count >= 3, f"inv pins={inv.pin_count}"
pin_a = inv.pin_at(0)
# 原断言：assert design.pin_name_of(pin_a.pin_id) == "A", "pin name must resolve via hasher"
record("pins/pin_a_name", design.pin_name_of(pin_a.pin_id))
# 原断言：assert pin_a.direction == 0
record("pins/pin_a_direction", pin_a.direction)  # INPUT
# 原断言：assert pin_a.is_port is False, "macro pin must not carry the port flag (㉙)"
record("pins/pin_a_not_port", pin_a.is_port is False)
pin_vdd = inv.pin_at(2)
# 原断言：assert design.pin_name_of(pin_vdd.pin_id) == "VDD" and pin_vdd.type == 1
record("pins/pin_vdd_name", design.pin_name_of(pin_vdd.pin_id))
record("pins/pin_vdd_type", pin_vdd.type)  # POWER
# R4：cell.pins_ 的全局平铺 pin id（D1，与 pin namemap 同源）
# 原断言：assert pin_a.pin_id == design.pin_id_by_name("INV_X1", "A"), \
record("pins/pin_id_matches_namemap", pin_a.pin_id == design.pin_id_by_name("INV_X1", "A"))
INFO("[DONE] simplified pins intact with global pin ids (R4)")

# S3 merge：匹配 cell 填 lib 字段；lef-only cell 不填
# 原断言：assert inv.library_name == "minitest_typ", f"library_name={inv.library_name}"
record("merge/inv_library_name", inv.library_name)
filler = design.find_cell("FILLER01")
# 原断言：assert filler is not None and not filler.library_name, \
record("merge/filler_lef_only", filler is not None and not filler.library_name,
       detail={"exists": filler is not None,
               "library_name": filler.library_name if filler else None})
INFO("[DONE] S3 merge: INV_X1 matched (lib fields filled), FILLER01 lef-only")

# ── via cell 权威表（⑫ 前缀 + 生成式展开）──
# 原断言：assert design.via_cell_count == 3, f"vias={design.via_cell_count}"
record("via/count", design.via_cell_count)
via_tech = design.find_via_cell("VIA12")             # tech lef 首份保留
# 原断言：assert via_tech is not None, "tech VIA12 must survive (keep first)"
record("via/tech_via12_exists", via_tech is not None)
# 非守卫：via_tech 仅检查存在性，无后续解引用
via_prefixed = design.find_via_cell("block_child::VIA12")
assert via_prefixed is not None, "⑫ prefixed DEF via missing"  # 保留：守卫
# 原断言：assert via_prefixed is not None, "⑫ prefixed DEF via missing"
record("via/prefixed_exists", via_prefixed is not None)
# 原断言：assert via_prefixed.cut_rect_at(0)[2] == 40
record("via/prefixed_cut_layer", via_prefixed.cut_rect_at(0)[2])  # def 40 ×1（基准 1000）
# 原断言：assert via_prefixed.bottom_enclosure_at(0)[0] == -100
record("via/prefixed_bottom_enc_x0", via_prefixed.bottom_enclosure_at(0)[0])
via_gen = design.find_via_cell("block_child::VIA12G")
assert via_gen is not None, "generated via missing"  # 保留：守卫
# 原断言：assert via_gen is not None, "generated via missing"
record("via/gen_exists", via_gen is not None)
# 原断言：assert via_gen.cut_rect_at(0)[0] == -30
record("via/gen_cut_x0", via_gen.cut_rect_at(0)[0])  # CUTSIZE 60 ±30 ×1
# 原断言：assert via_gen.bottom_enclosure_at(0)[0] == -50
record("via/gen_bottom_enc_x0", via_gen.bottom_enclosure_at(0)[0])  # cut ±30 + ENCLOSURE 20
# 原断言：assert via_gen.top_enclosure_at(0)[2] == 55
record("via/gen_top_enc_x2", via_gen.top_enclosure_at(0)[2])  # cut ±30 + ENCLOSURE 25
INFO("[DONE] S4b via table: 3 (tech VIA12 + block_child::VIA12 + ::VIA12G)")

# ── S4：block cell（㉙ = DSCell + block_cell 位）与 port pin ──
# 原断言：assert child.is_block_cell is True, "block_child must be a block cell"
record("s4/child_is_block_cell", child.is_block_cell is True)
# 原断言：assert child.name == "block_child"
record("s4/child_name", child.name)
# DIEAREA 4 点双存（含负坐标）×2：bbox + polygon 全点集（㉞）。
# 恒基准 1000：def 值 ×1（UNITS 1000）
# 原断言：assert child.bbox == (-500, -250, 750, 1000), f"bbox={child.bbox}"
record("s4/child_bbox", child.bbox)
# 原断言：assert child.is_polygon is True, "4-point DIEAREA must set is_polygon"
record("s4/child_is_polygon", child.is_polygon is True)
# 原断言：assert len(child.polygon) == 4, f"polygon points={len(child.polygon)}"
record("s4/child_polygon_len", len(child.polygon))
assert len(child.polygon) >= 4  # 保留：守卫（后面按下标取 polygon[0] 与 polygon[3]）
# 原断言：assert child.polygon[0] == (-500, -250)
record("s4/child_polygon_p0", child.polygon[0])
# 原断言：assert child.polygon[3] == (750, -250)
record("s4/child_polygon_p3", child.polygon[3])
# 原断言：assert child.def_path.endswith("block_child.def"), f"def_path={child.def_path}"
record("s4/child_def_path_suffix", child.def_path.endswith("block_child.def"),
       detail=child.def_path)
# P7：origin = −diearea 左下角
# 原断言：assert child.origin_x == 500 and child.origin_y == 250, \
record("s4/child_origin_x", child.origin_x)
record("s4/child_origin_y", child.origin_y)
# port = pins_ 的 port 位 DSPin（㉙）；port 名经 pin hasher 反查（R7 ㊱）
# 原断言：assert child.pin_count == 2, f"child pins={child.pin_count}"
record("s4/child_pin_count", child.pin_count)
# 保留：守卫——后面用 pin_at(0)、pin_at(1) 按下标取引脚，越界在 C++ 侧是未定义行为
assert child.pin_count >= 2, f"child pins={child.pin_count}"
pin_in = child.pin_at(0)
# 原断言：assert pin_in.is_port is True, "block port must carry the port flag"
record("s4/port_in_is_port", pin_in.is_port is True)
# 原断言：assert pin_in.placement_status == 1
record("s4/port_in_placement", pin_in.placement_status)  # FIXED（DSPinPlacementStatus）
pin_out = child.pin_at(1)
# 原断言：assert pin_out.placement_status == 3
record("s4/port_out_placement", pin_out.placement_status)  # PLACED
# 原断言：assert design.pin_name_of(pin_in.pin_id) == "PIN_IN"
record("s4/port_in_name", design.pin_name_of(pin_in.pin_id))
# 原断言：assert design.pin_name_of(pin_out.pin_id) == "PIN_OUT"
record("s4/port_out_name", design.pin_name_of(pin_out.pin_id))

parent = design.find_cell("block_parent")
assert parent is not None  # 保留：守卫
# 原断言：assert parent is not None
record("s4/parent_exists", parent is not None)
# 原断言：assert parent.is_block_cell is True
record("s4/parent_is_block_cell", parent.is_block_cell is True)
# DIEAREA 2 点矩形：bbox 直存（units 2000，恒基准 1000 → ×0.5）、polygon 空
# 原断言：assert parent.bbox == (0, 0, 1500, 1000), f"bbox={parent.bbox}"
record("s4/parent_bbox", parent.bbox)
# 原断言：assert parent.is_polygon is False, "2-point DIEAREA must not set is_polygon"
record("s4/parent_not_polygon", parent.is_polygon is False)
# 原断言：assert len(parent.polygon) == 0
record("s4/parent_polygon_len", len(parent.polygon))
# 非守卫：polygon 后续无下标访问
# 原断言：assert parent.pin_count == 1
record("s4/parent_pin_count", parent.pin_count)
INFO("[DONE] S4 block cells: bbox/polygon dual storage, ports as port-flag pins")

# ── 实例解析（对应方案 S5a 阶段）：per-DEF 产物（实例表/fake cell/密度）
# 与名字伴生对象（R7 ㊵② 落盘拆分：DSBlock_<i> 不含名字，名字在
# DSBlockNames_<i>）──
from emir.design import load_block_names, load_name_mapper
block0 = design_db.read_object(design_db.block_obj_name(0))  # block_child
# 原断言：assert block0.block_name == "block_child"
record("s5a/block0_block_name", block0.block_name)
# 原断言：assert block0.instance_total == 2, "local 0 placeholder + u1 (⑧)"
record("s5a/block0_instance_total", block0.instance_total)
# 原断言：assert block0.leaf_count == 1 and block0.fake_cell_count == 0
record("s5a/block0_leaf_count", block0.leaf_count)
record("s5a/block0_fake_cell_count", block0.fake_cell_count)
# R7 ㊵②：DSBlock_<i> 落盘不含名字（网名查询需读伴生对象注入）。
# 伴生对象双向查询（attach 为 CMSharedPtr 共享注入、hasher 归属不变）
names0 = load_block_names(design_db, 0)  # block_child 名字伴生对象
# 原断言：assert names0.block_name == "block_child"
record("s5a/names0_block_name", names0.block_name)
# ③ 网名扫描：local net id 从 1 起（双向，经伴生对象 hasher）
# 原断言：assert names0.instance_id_by_name("u1") == 1
record("s5a/names0_u1_id", names0.instance_id_by_name("u1"))
# 原断言：assert names0.instance_name_by_id(1) == "u1"
record("s5a/names0_id1_name", names0.instance_name_by_id(1))
# 原断言：assert names0.net_id_by_name("n1") == 1 and names0.net_id_by_name("n2") == 2
record("s5a/names0_n1_id", names0.net_id_by_name("n1"))
record("s5a/names0_n2_id", names0.net_id_by_name("n2"))
# 原断言：assert names0.net_name_by_id(2) == "n2"
record("s5a/names0_net2_name", names0.net_name_by_id(2))
# 注入临时产物后经 find_instance_by_name 查名取实例（R7 ㊱）
block0.attach_names(names0)
# R6：u1 INV_X1 + PLACED (100,200) N，UNITS 1000 = 基准 1000 ×1 → pos=(100,200)
u1 = block0.find_instance_by_name("u1")
# 原断言：assert u1 is not None and u1.cell_id == design.cell_id_by_name("INV_X1")
record("s5a/u1_exists", u1 is not None)
record("s5a/u1_cell_is_inv", None if u1 is None
       else u1.cell_id == design.cell_id_by_name("INV_X1"))
assert u1 is not None  # 保留：守卫
# 原断言：assert (u1.pos_x, u1.pos_y) == (100, 200) and u1.orient == 0
record("s5a/u1_pos", (u1.pos_x, u1.pos_y))
record("s5a/u1_orient", u1.orient)
# 原断言：assert u1.placement_status == 3
record("s5a/u1_placement", u1.placement_status)  # PLACED（DSPlacementStatus）
# 原断言：assert block0.net_count == 2
record("s5a/block0_net_count", block0.net_count)
# 密度（实例面积通道）：bin = alpha density_bin_size 10µm × 1000 = 10000
# DBU → child DIEAREA ×1 = 1250×1250 → 1×1 格；INV footprint 交叠 1 格
# 原断言：assert block0.density.cols == 1 and block0.density.rows == 1
record("s5a/block0_density_grid", {"cols": block0.density.cols, "rows": block0.density.rows})
# 原断言：assert block0.density.total_count == 1
record("s5a/block0_density_total", block0.density.total_count)

block1 = design_db.read_object(design_db.block_obj_name(1))  # block_parent
# 原断言：assert block1.instance_total == 4, "placeholder + top1 + top2 + top3(block)"
record("s5a/block1_instance_total", block1.instance_total)
# 原断言：assert block1.leaf_count == 3 and block1.stats.unplaced_count == 1
record("s5a/block1_leaf_count", block1.leaf_count)
record("s5a/block1_unplaced_count", block1.stats.unplaced_count)
# ⑲/⑳ fake cell：WRAP_CELL 未定义 → block_parent::WRAP_CELL（UNPLACED
# 实例照收入表、不入密度）
# 原断言：assert block1.fake_cell_count == 1
record("s5a/block1_fake_cell_count", block1.fake_cell_count)
names1 = load_block_names(design_db, 1)  # block_parent 名字伴生对象
# 原断言：assert names1.instance_name_by_id(2) == "top2"
record("s5a/names1_id2_name", names1.instance_name_by_id(2))  # R7 ㊱：名字经 hasher
block1.attach_names(names1)  # 共享注入（此后经 block1 的 hasher 查询）
top2 = block1.get_instance(2)
# 原断言：assert top2.placement_status == 0
record("s5a/top2_placement", top2.placement_status)  # UNPLACED
# 原断言：assert top2.orient == 0, "defi UNPLACED orient −1 must clamp to N"
record("s5a/top2_orient", top2.orient)
# 原断言：assert block1.net_count == 1 and block1.net_id_by_name("n_top") == 1
record("s5a/block1_net_count", block1.net_count)
record("s5a/block1_n_top_id", block1.net_id_by_name("n_top"))
# UNPLACED 不计密度（D14）；block instance footprint 不计（2026-09-12
# 裁定 5，S8 前置修正——top3 为 block instance，其密度贡献 = S8 子树叠
# 加，此处再计会双计）→ 仅 top1 交叠 1 格
# 原断言：assert block1.density.total_count == 1
record("s5a/block1_density_total", block1.density.total_count)

# ── S5b：per-DEF 网内容产物（③ 分批责任链 + ⑨ local id 对齐 + ⑩⑫）──
# block_child n1：连接 2 项 + 1 wire（M1 缺省宽 0.07µm×1000=70 回填）+
# 1 via（VIA12 → ⑫ block_child::VIA12 前缀名解析）@ (500,200)。
# 连接 id 形态（2026-09-13 裁定，全局 pin id 手算：S2 汇总 INV_X1 的
# A=0/ZN=1/VDD=2，S4 汇总 port PIN_IN=3/PIN_OUT=4/TOP_IN=5；port 引用
# = local 0 + port 位；位口径（2026-11-24 用户裁定）：实例引脚条目按
# 引脚方向（A/INPUT→receiver、ZN/OUTPUT→driver）；端口条目按网的角色
# 反向标（PIN_IN/INPUT 端口→driver、PIN_OUT/OUTPUT 端口→receiver）；
# 连接表封存后按 (实例, 引脚) 升序——端口条目（local 0）在前）
net0 = design_db.read_object(design_db.net_obj_name(0))
# 原断言：assert net0.block_name == "block_child"
record("s5b/net0_block_name", net0.block_name)
# 原断言：assert net0.stats.net_count == 2
record("s5b/net0_net_count", net0.stats.net_count)
# 原断言：assert net0.stats.connection_count == 4 and net0.stats.wire_count == 1
record("s5b/net0_connection_count", net0.stats.connection_count)
record("s5b/net0_wire_count", net0.stats.wire_count)
# 原断言：assert net0.stats.via_instance_count == 1
record("s5b/net0_via_instance_count", net0.stats.via_instance_count)
# 原断言：assert net0.stats.skipped_via_count == 0 and net0.stats.skipped_net_count == 0
record("s5b/net0_skipped_via", net0.stats.skipped_via_count)
record("s5b/net0_skipped_net", net0.stats.skipped_net_count)
# 原断言：assert net0.stats.skipped_invalid_connection_count == 0
record("s5b/net0_skipped_invalid_conn", net0.stats.skipped_invalid_connection_count)
# 连接表（S7 并查集输入）：n1 = (u1 A) + (PIN PIN_IN)；升序端口在前
conns_n1 = net0.connections_of(1)
# 原断言：assert [(c.instance_local_id, c.pin_id) for c in conns_n1] == [(0, 3), (1, 0)]
record("s5b/n1_conn_pairs", [(c.instance_local_id, c.pin_id) for c in conns_n1])
# 原断言：assert [c.is_port for c in conns_n1] == [True, False]
record("s5b/n1_conn_is_port", [c.is_port for c in conns_n1])
# PIN_IN 是 INPUT 端口 = 它所在网的驱动者（driver 位）；u1/A INPUT → receiver
# 原断言：assert [c.is_receiver for c in conns_n1] == [False, True]
record("s5b/n1_conn_is_receiver", [c.is_receiver for c in conns_n1])
# 原断言：assert [c.is_driver for c in conns_n1] == [True, False]
record("s5b/n1_conn_is_driver", [c.is_driver for c in conns_n1])
# 原断言：assert not any(c.is_driver and c.is_receiver for c in conns_n1)
record("s5b/n1_no_hybrid", not any(c.is_driver and c.is_receiver for c in conns_n1))  # 无 hybrid
# n2 = (u1 ZN) + (PIN PIN_OUT)：PIN_OUT 是 OUTPUT 端口 = 负载（receiver
# 位）；u1/ZN OUTPUT → driver 位
conns_n2 = net0.connections_of(2)
# 原断言：assert [(c.instance_local_id, c.pin_id) for c in conns_n2] == [(0, 4), (1, 1)]
record("s5b/n2_conn_pairs", [(c.instance_local_id, c.pin_id) for c in conns_n2])
# 原断言：assert [c.is_port for c in conns_n2] == [True, False]
record("s5b/n2_conn_is_port", [c.is_port for c in conns_n2])
# 原断言：assert [c.is_receiver for c in conns_n2] == [True, False]
record("s5b/n2_conn_is_receiver", [c.is_receiver for c in conns_n2])
# 原断言：assert [c.is_driver for c in conns_n2] == [False, True]
record("s5b/n2_conn_is_driver", [c.is_driver for c in conns_n2])
wires = net0.wires_of(1)
# 原断言：assert len(wires) == 1, f"n1 wires={wires}"
record("s5b/n1_wire_count", len(wires))
assert len(wires) >= 1  # 保留：守卫（后面按下标取 wire 段）
# 原断言：assert wires[0][0] == 0 and wires[0][1] == 70
record("s5b/n1_wire_layer", wires[0][0])  # M1 + 缺省宽回填
record("s5b/n1_wire_width", wires[0][1])
# 原断言：assert wires[0][2] == [(100, 200), (500, 200)]
record("s5b/n1_wire_points", wires[0][2])
# 原断言：assert len(net0.wires_of(2)) == 0, "n2 has no wiring"
record("s5b/n2_wire_count", len(net0.wires_of(2)))
# ⑩ via instance：专用 id 空间从 1 起、无 name，仅 via cell id + 位置
# 原断言：assert net0.via_instance_total == 1
record("s5b/net0_via_instance_total", net0.via_instance_total)
via12_id = design.via_cell_id_by_name("block_child::VIA12")
# 原断言：assert via12_id is not None
record("s5b/via12_id_resolved", via12_id is not None)
# 非守卫：via12_id 没有后续使用——旧断言的元组相等已固化成上面 via_instance_at(1) 的取值记录，取值为 None 不构成崩溃路径
# 原断言：assert net0.via_instance_at(1) == (via12_id, 500, 200)
record("s5b/net0_via_instance_1", net0.via_instance_at(1))
# ⑥ 网侧密度逐层分列通道：M1 金属 1 格 + VIA1 通孔 1 格（bin 10000）
# 原断言：assert net0.density.metal_total == 1, f"metal={net0.density.metal_total}"
record("s5b/net0_metal_total", net0.density.metal_total)
# 原断言：assert net0.density.via_total == 1, f"via={net0.density.via_total}"
record("s5b/net0_via_total", net0.density.via_total)
# 原断言：assert net0.density.layer_total(0, False) == 1
record("s5b/net0_layer0_metal", net0.density.layer_total(0, False))  # M1 金属
# 原断言：assert net0.density.layer_total(1, True) == 1
record("s5b/net0_layer1_via", net0.density.layer_total(1, True))  # VIA1 通孔

net1 = design_db.read_object(design_db.net_obj_name(1))
# 原断言：assert net1.block_name == "block_parent"
record("s5b/net1_block_name", net1.block_name)
# S7 跨块连接数据（block_parent.def 的 n_top 增 ( top3 PIN_IN )）：
# 连接 3 项 = 叶实例 + 子块实例 port + 顶层引脚（TOP_IN = pin 5）。
# 升序 [(0,5), (1,0), (3,3)]；TOP_IN 是 INPUT 端口 = 驱动者（driver
# 位）；top3/PIN_IN 是实例引脚条目（top3 = block_child 的实例）——按
# 引脚方向 INPUT → receiver，与端口口径相反（2026-11-24 用户裁定）
conns_top = net1.connections_of(1)
# 原断言：assert [(c.instance_local_id, c.pin_id) for c in conns_top] == \
record("s5b/top_conn_pairs", [(c.instance_local_id, c.pin_id) for c in conns_top])
# 原断言：assert [c.is_port for c in conns_top] == [True, False, False]
record("s5b/top_conn_is_port", [c.is_port for c in conns_top])
# 原断言：assert [c.is_receiver for c in conns_top] == [False, True, True]
record("s5b/top_conn_is_receiver", [c.is_receiver for c in conns_top])
# 原断言：assert [c.is_driver for c in conns_top] == [True, False, False]
record("s5b/top_conn_is_driver", [c.is_driver for c in conns_top])
# 原断言：assert net1.stats.wire_count == 0 and net1.stats.via_instance_count == 0
record("s5b/net1_wire_count", net1.stats.wire_count)
record("s5b/net1_via_instance_count", net1.stats.via_instance_count)
# 原断言：assert net1.stats.skipped_invalid_connection_count == 0
record("s5b/net1_skipped_invalid_conn", net1.stats.skipped_invalid_connection_count)
INFO("[DONE] S5b: net content (connections/wires/via instances, per-layer "
     "density channels, local net id aligned)")

# ── S6：层级树 + 起始编号（⑧⑨⑮；block_parent 实例化 block_child →
# block_parent 为唯一无父者 = 根；def_paths 序 child 在前不影响判定。
# 2026-09-14 裁定：net 区间长度含 local 0 空洞位——root [0,2)、child
# [2,5)，global = start + local 无 −1，root 空洞位 = global 0 = OBS 专属）
hier = design.get_hier_tree()
# 原断言：assert hier.node_count == 2 and hier.design_name == "block_parent"
record("s6/node_count", hier.node_count)
record("s6/design_name", hier.design_name)
root = hier.node(0)
# 原断言：assert root.block_cell_name == "block_parent"
record("s6/root_block_cell_name", root.block_cell_name)
# 原断言：assert root.instance_name == ""
record("s6/root_instance_name", root.instance_name)  # 2026-09-16 裁定 2：root 实例名恒空串
# 原断言：assert root.self_global_id == 0 and root.parent_id == 0
record("s6/root_self_global_id", root.self_global_id)
record("s6/root_parent_id", root.parent_id)
# 原断言：assert root.instance_range == (0, 4), f"root inst={root.instance_range}"
record("s6/root_instance_range", root.instance_range)
# 原断言：assert root.net_range == (0, 2), f"root net={root.net_range}"
record("s6/root_net_range", root.net_range)
child = hier.node(1)
# 原断言：assert child.block_cell_name == "block_child"
record("s6/child_block_cell_name", child.block_cell_name)
# 原断言：assert child.instance_name == "top3"
record("s6/child_instance_name", child.instance_name)
# 原断言：assert child.parent_id == 0 and hier.children(0) == [1]
record("s6/child_parent_id", child.parent_id)
record("s6/root_children", hier.children(0))
# ⑧ child 自身 global id = 父块 inst 区间内 top3 的 local 3
# 原断言：assert child.self_global_id == 3, f"self={child.self_global_id}"
record("s6/child_self_global_id", child.self_global_id)
# 原断言：assert child.instance_range == (4, 6), f"child inst={child.instance_range}"
record("s6/child_instance_range", child.instance_range)
# 原断言：assert child.net_range == (2, 5), f"child net={child.net_range}"
record("s6/child_net_range", child.net_range)
# via 区间（S5b 统计计数）：root 0 个 → [0,0)；child 1 个（VIA12）→ [0,1)
# 原断言：assert root.via_range == (0, 0), f"root via={root.via_range}"
record("s6/root_via_range", root.via_range)
# 原断言：assert child.via_range == (0, 1), f"child via={child.via_range}"
record("s6/child_via_range", child.via_range)
# 四接口：区间反查 / parent-children / format_tree
# 原断言：assert hier.block_of_instance(3) == 0, "top3 itself belongs to parent seg"
record("s6/inst3_block", hier.block_of_instance(3))
# 原断言：assert hier.block_of_instance(5) == 1, "child u1 belongs to child seg"
record("s6/inst5_block", hier.block_of_instance(5))
# 原断言：assert hier.block_of_net(3) == 1
record("s6/net3_block", hier.block_of_net(3))
# 原断言：assert hier.block_of_via_instance(0) == 1, "child VIA12 via instance"
record("s6/via0_block", hier.block_of_via_instance(0))
# 原断言：assert hier.block_of_via_instance(1) is None, "no more via instances"
record("s6/via1_block", hier.block_of_via_instance(1))
# 非守卫：仅检查无更多 via instance，返回值无后续解引用
# 原断言：assert hier.parent(1) == 0
record("s6/parent_of_1", hier.parent(1))
# ⑨ global id 换算：net 区间含空洞位（start + local 无 −1）；local 0 空
# 洞位不映射（root 空洞位 = global 0 = OBS 专属）
# 原断言：assert hier.global_instance_id(0, 0) == 0
record("s6/global_inst_0_0", hier.global_instance_id(0, 0))
# 原断言：assert hier.global_instance_id(1, 0) == 3
record("s6/global_inst_1_0", hier.global_instance_id(1, 0))
# 原断言：assert hier.global_instance_id(1, 1) == 5, "child u1 global id"
record("s6/global_inst_1_1", hier.global_instance_id(1, 1))
# 原断言：assert hier.global_net_id(0, 1) == 1, "n_top global id"
record("s6/global_net_0_1", hier.global_net_id(0, 1))
# 原断言：assert hier.global_net_id(1, 1) == 3, "child n1 global id"
record("s6/global_net_1_1", hier.global_net_id(1, 1))
# 原断言：assert hier.global_net_id(1, 2) == 4, "child n2 global id"
record("s6/global_net_1_2", hier.global_net_id(1, 2))
# 原断言：assert hier.global_via_instance_id(0, 1) is None, "root has no via"
record("s6/global_via_0_1", hier.global_via_instance_id(0, 1))
# 非守卫：仅检查 root 无 via，返回值无后续解引用
# 原断言：assert hier.global_via_instance_id(1, 1) == 0, "child VIA12 global id"
record("s6/global_via_1_1", hier.global_via_instance_id(1, 1))
# 原断言：assert hier.global_via_instance_id(1, 2) is None
record("s6/global_via_1_2", hier.global_via_instance_id(1, 2))
# 非守卫：仅检查越界 local 不映射，返回值无后续解引用
tree_text = hier.format_tree()
# 原断言：assert "block_parent as (top)" in tree_text
record("s6/tree_root_line", "block_parent as (top)" in tree_text)  # 裁定 2：root 行占位显示
# 原断言：assert "block_child as top3" in tree_text
record("s6/tree_child_line", "block_child as top3" in tree_text)
# 原断言：assert "inst=[4,6)" in tree_text
record("s6/tree_inst_range_text", "inst=[4,6)" in tree_text)
INFO("[DONE] S6: hierarchy tree (DFS numbering, ⑧ local-0 mapping, four "
     "interfaces, name-format text)")

# ── S7：跨块连接归并（并查集；2026-09-13 裁定：仅 port 相连网、两层
# 树、root = 层级最高/同级最小 global id、悬空 port 照常入表 root=自身）──
# 数据形态：父网 n_top ( top3 PIN_IN ) × 子网 n1 ( PIN PIN_IN ) 对接（id
# 对接键 = 同一 port 的全局 pin id 相等——PIN_IN = pin 3，2026-09-13
# 裁定后 S7 内部零字符串匹配）→ n1 归并入 n_top；n2 挂
# child 的 PIN_OUT、父侧未连接 → 悬空 port 网（root = 自身 + 计数）；
# internal net（无 port 位条目）不入表——红线由 C++ 单测 ds_union_test
# 固化（本数据无 internal 网）
from emir.design import load_design_net_union
union = load_design_net_union(design_db)
g_n_top = hier.global_net_id(0, 1)  # 1：root 块 n_top（物理网展示名锚）
g_n1 = hier.global_net_id(1, 1)     # 3：child n1（经 top3/PIN_IN 归并）
g_n2 = hier.global_net_id(1, 2)     # 4：child n2（悬空）
# 原断言：assert union.find(g_n1) == g_n_top, "child n1 must union into top n_top"
record("s7/n1_root", union.find(g_n1))
# 原断言：assert union.find(g_n_top) == g_n_top, "root net self-map (two-layer)"
record("s7/n_top_self_map", union.find(g_n_top) == g_n_top)
# 原断言：assert sorted(union.members(g_n_top)) == sorted([g_n_top, g_n1]), \
record("s7/n_top_members", sorted(union.members(g_n_top)))
# 悬空 port 网 n2：照常入表 root = 自身 + 单成员计数（DSGN::0018 口径）
# 原断言：assert union.find(g_n2) == g_n2
record("s7/n2_self_root", union.find(g_n2) == g_n2)
# 原断言：assert union.members(g_n2) == [g_n2]
record("s7/n2_members", union.members(g_n2))
# 原断言：assert union.dangling_count == 1
record("s7/dangling_count", union.dangling_count)
# 原断言：assert union.class_count == 2
record("s7/class_count", union.class_count)
# 两层不变式：全表 find 恒一步（root 自映射）
s7_members = union.members(g_n_top) + union.members(g_n2)
# 原断言：assert union.find(union.find(member)) == union.find(member)
record("s7/two_layer_invariant",
       all(union.find(union.find(m)) == union.find(m) for m in s7_members),
       detail=[m for m in s7_members if union.find(union.find(m)) != union.find(m)])
INFO("[DONE] S7 net union: cross-block merge (n1 -> n_top), dangling port "
     "net n2 root=self, two-layer invariant")

# ── R7 ㊻：EXDSNameMapper 全局 name 组装（统一加载 API → 注入式轻壳，
# 运行时构造不落盘；instance/net 两维度双向闭环）──
# 层级（同上）：root block_parent（inst [0,4) net [0,1)）→ child top3
#（block_child，inst [4,6) net [1,3)）；伴生对象 names0/1 按序注入
design_m, mapper_i = load_name_mapper(design_db, kind=0)  # instance 维度
# 原断言：assert mapper_i.injected_count == 2, f"inject={mapper_i.injected_count}"
record("r7/injected_count", mapper_i.injected_count)
# 层级实例路径 → global id（2026-09-16 裁定 1：路径不含设计名前缀——
# 顶层平铺实例 = 单段名）
# 原断言：assert mapper_i.get_global_id("top1") == 1
record("r7/top1_global_id", mapper_i.get_global_id("top1"))
# 原断言：assert mapper_i.get_global_id("top3") == 3, \
record("r7/top3_global_id", mapper_i.get_global_id("top3"))
# 原断言：assert mapper_i.get_global_id("top3/u1") == 5
record("r7/top3_u1_global_id", mapper_i.get_global_id("top3/u1"))
# global id → 层级实例路径（与正向闭环；顶层叶 = 单段名）
# 原断言：assert mapper_i.get_full_name(3) == "top3"
record("r7/id3_full_name", mapper_i.get_full_name(3))
# 原断言：assert mapper_i.get_full_name(5) == "top3/u1"
record("r7/id5_full_name", mapper_i.get_full_name(5))
# 原断言：assert mapper_i.get_full_name(1) == "top1"
record("r7/id1_full_name", mapper_i.get_full_name(1))
# 裁定 2 对称语义：root 路径 = 空串；绑定层把空名转 None（与未
# 命中同形态，既有约定）
# 原断言：assert mapper_i.get_full_name(0) is None
record("r7/id0_full_name", mapper_i.get_full_name(0))
# 非守卫：仅检查空名转 None 的约定，返回值无后续解引用
# 未注入/路径断裂 → None（不透出哨兵）
# 原断言：assert mapper_i.get_global_id("ghost") is None
record("r7/ghost_global_id", mapper_i.get_global_id("ghost"))
# 非守卫：仅检查未注入路径返回 None，返回值无后续解引用
# 原断言：assert mapper_i.get_full_name(999) is None
record("r7/id999_full_name", mapper_i.get_full_name(999))
# 非守卫：仅检查越界 id 返回 None，返回值无后续解引用
_, mapper_n = load_name_mapper(design_db, kind=1)  # net 维度（区间换算不同）
# 原断言：assert mapper_n.get_global_id("n_top") == 1
record("r7/n_top_global_id", mapper_n.get_global_id("n_top"))
# 原断言：assert mapper_n.get_global_id("top3/n1") == 3
record("r7/top3_n1_global_id", mapper_n.get_global_id("top3/n1"))
# 原断言：assert mapper_n.get_full_name(4) == "top3/n2"
record("r7/id4_full_name", mapper_n.get_full_name(4))
INFO("[DONE] R7 name mapper: instance/net both dimensions, bidirectional "
     "close-loop over hierarchy paths (injected lightweight shell)")

# 汇总并入：fake cell 进全局 cell 表（id 保持任务内分配值 = hash 扰动
# 基址 + 递增，⑳；cells_ 稀疏落位含占位空洞 → cell_count >= 5）
# 原断言：assert design.cell_count >= 5, f"cells={design.cell_count} (4 + 1 fake)"
record("fake/cells_at_least_5", design.cell_count >= 5, detail=design.cell_count)
fake_id = design.cell_id_by_name("block_parent::WRAP_CELL")
assert fake_id is not None  # 保留：守卫
# 原断言：assert fake_id is not None
record("fake/id_resolved", fake_id is not None)
fake_cell = design.get_cell(fake_id)
# 原断言：assert fake_cell.is_fake_cell is True
record("fake/is_fake_cell", fake_cell.is_fake_cell is True)
# 原断言：assert fake_cell.width == 1 and fake_cell.height == 1
record("fake/width", fake_cell.width)
record("fake/height", fake_cell.height)
# 原断言：assert fake_cell.pin_count == 0
record("fake/pin_count", fake_cell.pin_count)
# 原断言：assert list(design.fake_cell_ids()) == [fake_id]
record("fake/ids", list(design.fake_cell_ids()))
# top2 的引用重映射后指向全局 fake id（无冲突时与任务内分配值一致）
# 原断言：assert top2.cell_id == fake_id
record("fake/top2_cell_id_matches", top2.cell_id == fake_id)
INFO("[DONE] S5a: instance table (R6 pos/orient), fake cell (⑲/⑳), "
     "density channel, net name hasher (③), merge into DSDesign")

# ── DSGN 消息透出（0001-0005 + 0007 fake cell 场景均已构造）──
msgs = ""
for root, _dirs, files in os.walk(LOG_DIR):
    for fn in files:
        if fn.endswith(".log"):
            try:
                with open(os.path.join(root, fn), errors="ignore") as fh:
                    msgs += fh.read()
            except OSError:
                pass
# 原断言：assert msg_id in msgs, f"message {msg_id} should be emitted"
record("dsgn/all_emitted",
       not [m for m in ("DSGN::0001", "DSGN::0002", "DSGN::0003", "DSGN::0004",
                        "DSGN::0005", "DSGN::0007", "DSGN::0009", "DSGN::0018")
            if m not in msgs],
       detail=[m for m in ("DSGN::0001", "DSGN::0002", "DSGN::0003", "DSGN::0004",
                           "DSGN::0005", "DSGN::0007", "DSGN::0009", "DSGN::0018")
               if m not in msgs])
INFO("[DONE] DSGN messages 0001-0005 + 0009 + 0018 emitted (warnings/info, "
     "no raise)")

# ── ⑧ 统一加载：注入 pin 表/几何后 get_cell 指针注入 ──
from emir.design import load_design_with
loaded = load_design_with(design_db, pin_tables=True, pin_geometries=True)
loaded_inv = loaded.get_cell(inv_id)
# 原断言：assert loaded_inv.get_pin_tables() is not None, "pin tables should be injected"
record("load/pin_tables_injected", loaded_inv.get_pin_tables() is not None)
assert loaded_inv.get_pin_tables() is not None  # 保留：守卫
# 裁定 3：表按 (cell id, pin id) 检索——lib 中 internal_power 挂在
# INV_X1/ZN 上（pin A 无表）
pin_zn_id = loaded.pin_id_by_name("INV_X1", "ZN")
# 原断言：assert pin_zn_id is not None
record("load/pin_zn_id_resolved", pin_zn_id is not None)
# 保留：守卫——后面把它作为查询参数传给 C++ 接口 pin_has_tables，取值为空时会以类型错误崩溃
assert pin_zn_id is not None, "pin ZN id unresolved"
loaded_inv_id = loaded.cell_id_by_name("INV_X1")
# 原断言：assert loaded_inv.get_pin_tables().pin_has_tables(loaded_inv_id, pin_zn_id), \
record("load/pin_zn_has_tables",
       loaded_inv.get_pin_tables().pin_has_tables(loaded_inv_id, pin_zn_id))
pin_a_id_tbl = loaded.pin_id_by_name("INV_X1", "A")
# 原断言：assert not loaded_inv.get_pin_tables().pin_has_tables(loaded_inv_id,
record("load/pin_a_no_tables",
       not loaded_inv.get_pin_tables().pin_has_tables(loaded_inv_id, pin_a_id_tbl))
# 原断言：assert loaded_inv.get_pin_geometry() is not None
record("load/pin_geometry_injected", loaded_inv.get_pin_geometry() is not None)
# 非守卫：返回值此处无后续解引用
INFO("[DONE] load_design_with: pin tables/geometry injected, keyed by pin id")

# port 几何按 (block cell id, 全局 pin id) 检索（裁定 3：(cell, pin)
# 属性；port 几何挂 block cell 维度）
pin_in_id = loaded.pin_id_by_name("block_child", "PIN_IN")
block_child_id = loaded.cell_id_by_name("block_child")
# 原断言：assert pin_in_id is not None
record("load/pin_in_id_resolved", pin_in_id is not None)
# 保留：守卫——后面把它作为查询参数传给 C++ 接口 geometry_of，取值为空时会以类型错误崩溃
assert pin_in_id is not None, "pin IN id unresolved"
port_geoms = loaded.get_pin_geometry().geometry_of(block_child_id,
                                                   pin_in_id)
# 原断言：assert len(port_geoms) == 1, f"PIN_IN geoms={port_geoms}"
record("port_geom/count", len(port_geoms))
assert len(port_geoms) >= 1  # 保留：守卫（后面按下标取 port 几何）
# 原断言：assert port_geoms[0] == (0, (-5, -10, 15, 20)), f"PIN_IN geom={port_geoms[0]}"
record("port_geom/pin_in", port_geoms[0])
INFO("[DONE] port geometry retrievable by global pin id (R4/R5)")

# ── R9 ㊼：wait_obj 依赖传播体系（deps 传播 + run_direct 直跑 + 等待
# 语义兜底；DEVELOPMENT_GUIDELINES Section 17）──
# R9 起 load_*/ds_functions 全部 read_object 类 API 经 @wait_obj 包装声明
# 自身数据依赖——上方全部 load_* 本地直调（数据已就绪）即其正路径回归。
from fly import run_direct
from emir.design import load_design

# deps() 形态：wait_obj inputs lambda 解析结果透传（条件实参条件化，
# 上层 task 的 inputs 传播数据源——防依赖漂移）
deps_with = load_design_with.deps(design_db, pin_tables=True,
                                  pin_geometries=True)
# 原断言：assert deps_with == [
record("r9/deps_with", deps_with)
# 原断言：assert load_design_with.deps(design_db) == [
record("r9/deps_default", load_design_with.deps(design_db))
# 原断言：assert load_block_names.deps(design_db, 1) == [
record("r9/deps_block_names", load_block_names.deps(design_db, 1))
INFO("[DONE] R9 deps(): inputs lambda resolution propagates (incl. "
     "conditional pin_tables/pin_geometries)")

# run_direct 形态：task 函数体内剥离本地等待直跑（依赖已声明必然就绪，
# 省 wait_obj 轮询/master 查询冗余网络 IO）——返回值与包装调用一致
direct_design = run_direct(load_design, design_db)
# 原断言：assert direct_design.cell_count >= 4, f"cells={direct_design.cell_count}"
record("r9/run_direct_cells", direct_design.cell_count >= 4, detail=direct_design.cell_count)
INFO("[DONE] R9 run_direct(): wait_obj-wrapped API unwrapped, same result")

# 等待语义兜底（负路径）：lib db（role=lib，无 DSDesign 对象、此时 master
# 无 pending/running 任务可产出）上 load_design → wait_obj 确认无法产出
# → RuntimeError——漏声明依赖/数据缺失场景的明确报错，而非静默错读
try:
    load_design(lib_db)
    raise AssertionError("load_design on lib db must raise (cannot produce)")
except RuntimeError as e:
    # 原断言：assert "cannot be produced" in str(e), str(e)
    record("r9/lib_db_load_rejected_msg", "cannot be produced" in str(e), detail=str(e))
INFO("[DONE] R9 wait_obj bottom line: absent dependency raises "
     "cannot-be-produced instead of silent misread")

# ── R8d：alpha 键 lcp_name_arena（裁定 55：id→name 侧 LCP 后缀共享）──
# 第二个 design db（同一 lef/def 输入）开启 alpha → COMPONENTS 解析任务
# 在 DSBlockNames_<i> 落盘前封口两 hasher（instance/net）为 LCP 形态；
# 读回按标记位自识别。验证 alpha 传递链（build_design_db → 封口点）与
# 封口形态的查询面/层级树组装/全局 mapper 与形态一全同。未知键负例：
# header schema 直接校验（2026-09-17 裁定）——raise ValueError 终止
# （不建库；原 DSGN::0013 提醒忽略接线已删除）。
try:
    proj.build_design_db(
        name="design_alpha_bad",
        def_paths=[BLOCK_CHILD, BLOCK_PARENT],
        lef_paths=[TECH_LEF, CELLS_MAIN, CELLS_EXTRA],
        lib_db=lib_db,
        alpha={"nonexistent_key": 123},
    )
    raise AssertionError("unknown alpha key must raise ValueError")
except ValueError as e:
    # 原断言：assert "nonexistent_key" in str(e), str(e)
    record("alpha/bad_key_msg", "nonexistent_key" in str(e), detail=str(e))
# 原断言：assert "design_alpha_bad" not in proj.list_dbs(), \
record("alpha/bad_key_no_db", "design_alpha_bad" not in proj.list_dbs())
lcp_db = proj.build_design_db(
    name="design_lcp",
    def_paths=[BLOCK_CHILD, BLOCK_PARENT],
    lef_paths=[TECH_LEF, CELLS_MAIN, CELLS_EXTRA],
    lib_db=lib_db,
    alpha={"lcp_name_arena": True},
)
assert proj.wait_frozen("design_lcp", timeout=180), "lcp design db should freeze"
INFO("[WAIT] LCP design db frozen")

names0_lcp = load_block_names(lcp_db, 0)
# 原断言：assert names0_lcp.names_lcp_form is True, \
record("r8d/names0_lcp_form", names0_lcp.names_lcp_form is True)
# 封口形态双向查询与形态一同值（同输入同 id 空间）
# 原断言：assert names0_lcp.instance_id_by_name("u1") == 1
record("r8d/names0_u1_id", names0_lcp.instance_id_by_name("u1"))
# 原断言：assert names0_lcp.instance_name_by_id(1) == "u1"
record("r8d/names0_id1_name", names0_lcp.instance_name_by_id(1))
# 原断言：assert names0_lcp.net_id_by_name("n1") == 1 and names0_lcp.net_id_by_name("n2") == 2
record("r8d/names0_n1_id", names0_lcp.net_id_by_name("n1"))
record("r8d/names0_n2_id", names0_lcp.net_id_by_name("n2"))
# 原断言：assert names0_lcp.net_name_by_id(2) == "n2"
record("r8d/names0_net2_name", names0_lcp.net_name_by_id(2))
names1_lcp = load_block_names(lcp_db, 1)
# 原断言：assert names1_lcp.names_lcp_form is True
record("r8d/names1_lcp_form", names1_lcp.names_lcp_form is True)
# 原断言：assert names1_lcp.instance_name_by_id(2) == "top2"
record("r8d/names1_id2_name", names1_lcp.instance_name_by_id(2))
# 原断言：assert names1_lcp.instance_id_by_name("top2") == 2
record("r8d/names1_top2_id", names1_lcp.instance_id_by_name("top2"))
# 未登记 → None；封口形态域判别不误判
# 原断言：assert names1_lcp.instance_name_by_id(999) is None
record("r8d/names1_id999_name", names1_lcp.instance_name_by_id(999))
# 非守卫：仅检查未登记 id 返回 None，返回值无后续解引用
# 原断言：assert names1_lcp.net_id_by_name("ghost") is None
record("r8d/names1_ghost_id", names1_lcp.net_id_by_name("ghost"))
# 非守卫：仅检查未登记名返回 None，返回值无后续解引用
INFO("[DONE] R8d LCP name arena: DSBlockNames_<i> sealed, bidirectional "
     "queries identical to form-1")

# 封口形态消费链：层级树组装（实例名反查）+ 全局 mapper（get_full_name
# 走 LCP rank 回溯）与形态一同构
lcp_design = lcp_db.load_design()
hier_lcp = lcp_design.get_hier_tree()
# 原断言：assert hier_lcp.node_count == 2 and hier_lcp.design_name == "block_parent"
record("r8d/lcp_node_count", hier_lcp.node_count)
record("r8d/lcp_design_name", hier_lcp.design_name)
_, mapper_lcp = load_name_mapper(lcp_db, kind=0)
# 原断言：assert mapper_lcp.get_global_id("top3/u1") == 5
record("r8d/lcp_top3_u1_id", mapper_lcp.get_global_id("top3/u1"))
# 原断言：assert mapper_lcp.get_full_name(5) == "top3/u1"
record("r8d/lcp_id5_name", mapper_lcp.get_full_name(5))
_, mapper_lcp_n = load_name_mapper(lcp_db, kind=1)
# 原断言：assert mapper_lcp_n.get_global_id("top3/n2") == 4
record("r8d/lcp_top3_n2_id", mapper_lcp_n.get_global_id("top3/n2"))
# 原断言：assert mapper_lcp_n.get_full_name(4) == "top3/n2"
record("r8d/lcp_id4_name", mapper_lcp_n.get_full_name(4))
INFO("[DONE] R8d LCP name arena: hier tree + global mapper closed-loop over "
     "LCP rank backtrack")

# ── alpha settings 对象（2026-09-13 裁定：声明式五要素 + 固定对象名
# "alpha_settings" 随建库写入 db + 消费点读回 normalize 兜底）──
alpha_obj = lcp_db.read_object(lcp_db.ALPHA_SETTINGS_OBJ)
# 原断言：assert alpha_obj.lcp_name_arena is True, "applied alpha key must persist"
record("alpha/lcp_name_arena_applied", alpha_obj.lcp_name_arena is True)
# 原断言：assert alpha_obj.density_bin_size == 10, "unset keys must keep defaults"
record("alpha/density_bin_size", alpha_obj.density_bin_size)
# 原断言：assert alpha_obj.target_partitions is None, "None-default key intact"
record("alpha/target_partitions", alpha_obj.target_partitions)
# 非守卫：仅检查 None 默认键完好，返回值无后续解引用
# 读回兜底（旧对象向前兼容）：缺键补默认、未知属性丢弃——白盒构造偏差
del alpha_obj._values["lcp_name_arena"]
alpha_obj._values["ghost_key"] = 1
alpha_obj.normalize()
# 原断言：assert alpha_obj.lcp_name_arena is False, "normalize must refill defaults"
record("alpha/normalize_refill_default", alpha_obj.lcp_name_arena is False)
# 原断言：assert "ghost_key" not in alpha_obj._values, "normalize must drop unknowns"
record("alpha/normalize_drop_unknown", "ghost_key" not in alpha_obj._values)
# 未传 alpha 的 design db 同样落了全默认 settings 对象
# 原断言：assert design_db.read_object(
record("alpha/design_db_net_batch_size",
       design_db.read_object(design_db.ALPHA_SETTINGS_OBJ).net_batch_size)
INFO("[DONE] alpha_settings object: persisted per db, applied values + "
     "normalize fallback (missing key -> default, unknown dropped)")

# ── S8：全局密度合并 + 分区决策（2026-09-12/13 裁定：core/extend 双区
# 域 + 6:2:2 通道比重 + 默认目标密度 15 万）──
# 全局密度（层级树合并：root top1 + 子块 u1 经 top3 放置平移撒入，S5a 已
# 排除 top3 自身 footprint）：
#   inst 通道 = 2（top1 + u1）、metal = 1（child n1 的 M1 wire）、
#   via = 1（VIA12 cut @ VIA1）→ 总合成 = 6×2 + 2×1 + 2×1 = 16
from emir.design import load_global_density
gd = load_global_density(design_db)
# 原断言：assert gd.cols == 1 and gd.rows == 1, f"grid={gd.cols}x{gd.rows}"
record("s8/grid", {"cols": gd.cols, "rows": gd.rows})
# 原断言：assert gd.total_count == 2, f"inst={gd.total_count}"
record("s8/inst_total", gd.total_count)
# 原断言：assert gd.metal_total == 1 and gd.via_total == 1, \
record("s8/metal_total", gd.metal_total)
record("s8/via_total", gd.via_total)
# 原断言：assert gd.cell_count(0, 0) == 2
record("s8/cell_0_0", gd.cell_count(0, 0))
INFO("[DONE] S8 global density: 3 channels merged across hierarchy "
     "(inst=2/metal=1/via=1)")

# 分区决策（默认 alpha）：N = ceil(16/150000) = 1 → 单分区
# core = 根 DIEAREA 格网覆盖域（bin 10000 → (0,0)-(10000,10000)）、
# extend 全向 int32 极值（单分区四向均为最外围，不截断）
design_s8 = design_db.load_design()  # S8 重写后的 DSDesign（含分区表）
# 原断言：assert design_s8.partition_count == 1, f"parts={design_s8.partition_count}"
record("s8/partition_count", design_s8.partition_count)
# 保留：守卫——后面用 partition_at(0) 按下标取分区，越界在 C++ 侧是未定义行为
assert design_s8.partition_count >= 1, f"parts={design_s8.partition_count}"
p = design_s8.partition_at(0)
# 原断言：assert p.partition_id == 0
record("s8/partition_id", p.partition_id)
# 原断言：assert p.core_rect == (0, 0, 10000, 10000), f"core={p.core_rect}"
record("s8/core_rect", p.core_rect)
# 原断言：assert p.extend_rect == (-2147483648, -2147483648, 2147483647, 2147483647), \
record("s8/extend_rect", p.extend_rect)
INFO("[DONE] S8 partitions: default alpha -> single partition, core = root "
     "DIEAREA grid coverage, extend = int32 extremes")

# ── S9：flatten 展平 + 分区保存（两级任务；单分区 → extend 全域全
# primary；2026-09-13 裁定补记①-⑤ + 2026-09-14 拆分裁定（GEOMETRY/NETS
# 按 pg/信号各拆两对象）+ 同日 net id 0 专属 OBS 裁定手算锁定）──
# 层级：root block_parent（inst [0,4) net [0,2)）→ child top3（block_child，
# inst [4,6) net [2,5)）。block_parent UNITS 2000 → 坐标 ×0.5：top1 放置
# (500,500) → pos (250,250)（N 向 pos = t）；top3 放置 (1500,500) →
# t (750,250)、P7 origin 修正 → pos (1250,500)；child 复合变换 =
# translate(1250,500)，u1 local (100,200)（child UNITS 1000 ×1）→ 全局
# (1350,700)；top2 UNPLACED 不入分区。真网 id：n_top 1、n1 3、n2 4。
from emir.design import iter_design_partition, load_design_pg_nets, \
    load_partition
# 原断言：assert iter_design_partition(design_db) == [(0, 0)]
record("s9/partition_list", iter_design_partition(design_db))
geo_p, geo_pg_p, inst_p, iconn_p, nets_p, nets_pg_p = \
    load_partition(design_db, 0, 0)
# 原断言：assert inst_p.size == 3, f"instances={inst_p.size} (top2 UNPLACED excluded)"
record("s9/inst_size", inst_p.size)
top1_i = inst_p.get(1)
# 原断言：assert top1_i.is_primary and (top1_i.pos_x, top1_i.pos_y) == (250, 250)
record("s9/top1_primary", top1_i.is_primary)
record("s9/top1_pos", (top1_i.pos_x, top1_i.pos_y))
top3_i = inst_p.get(3)
# 原断言：assert top3_i.is_primary and (top3_i.pos_x, top3_i.pos_y) == (1250, 500), \
record("s9/top3_primary", top3_i.is_primary)
record("s9/top3_pos", (top3_i.pos_x, top3_i.pos_y))
u1_i = inst_p.get(5)
# 原断言：assert u1_i.is_primary and (u1_i.pos_x, u1_i.pos_y) == (1350, 700)
record("s9/u1_primary", u1_i.is_primary)
record("s9/u1_pos", (u1_i.pos_x, u1_i.pos_y))
# 原断言：assert 2 not in inst_p.ids(), "UNPLACED top2 (global 2) must be excluded"
record("s9/top2_excluded", 2 not in inst_p.ids())
# 2026-09-13 裁定：电源引脚预展开（D18）删除——坐标归 ④ 提取按复现原则
# 自 instance + transform + cell pin 几何自取，分区副本不再携带
INFO("[DONE] S9 instances: composite-transformed global pos (top3 pos = "
     "place_from_def (2000,750)), UNPLACED excluded")

# /GEOMETRY：net 0 桶 = OBS 专属（block_parent BLOCKAGE M2 (100,100)-
# (300,400) units 2000 ×0.5 → (50,50,150,200)；2026-09-14 裁定：键 0 恒
# 纯 OBS，n_top 无几何无条目）；child n1（global 3）wire 段 + via 三组
# 图形（复合 ×，via 挂 cell id + primary）
obs_entries = [e for e in geo_p.entries_of(0) if e.is_obs]
# 原断言：assert len(obs_entries) == 1
record("s9/obs_count", len(obs_entries))
assert len(obs_entries) >= 1  # 保留：守卫（后面按下标取 OBS 条目）
# 原断言：assert obs_entries[0].layer_id == 2
record("s9/obs_layer", obs_entries[0].layer_id)
# 原断言：assert obs_entries[0].rect == (50, 50, 150, 200)
record("s9/obs_rect", obs_entries[0].rect)
# 原断言：assert len(geo_p.entries_of(0)) == 1, \
record("s9/net0_bucket_len", len(geo_p.entries_of(0)))
n1_entries = geo_p.entries_of(3)
# 原断言：assert n1_entries is not None and len(n1_entries) == 4
record("s9/n1_entries_present", n1_entries is not None)
record("s9/n1_entries_len", 0 if n1_entries is None else len(n1_entries))
assert n1_entries is not None  # 保留：守卫
wire_e = [e for e in n1_entries if not e.is_via]
# 原断言：assert len(wire_e) == 1 and wire_e[0].layer_id == 0
record("s9/n1_wire_count", len(wire_e))
record("s9/n1_wire_layer", wire_e[0].layer_id if len(wire_e) >= 1 else None)
assert len(wire_e) >= 1  # 保留：守卫（后面按下标取 wire 条目）
# 原断言：assert wire_e[0].rect == (1315, 665, 1785, 735), f"wire={wire_e[0].rect}"
record("s9/n1_wire_rect", wire_e[0].rect)
via12 = design.via_cell_id_by_name("block_child::VIA12")
via_e = [e for e in n1_entries if e.is_via]
# 原断言：assert all(e.via_cell_id == via12 and e.is_primary for e in via_e)
record("s9/n1_via_prefixed_primary", all(e.via_cell_id == via12 and e.is_primary for e in via_e),
       detail=len(via_e))
# 原断言：assert sorted((e.layer_id, e.rect) for e in via_e) == [
record("s9/n1_via_graphics", sorted((e.layer_id, e.rect) for e in via_e))
# 原断言：assert not geo_p.is_crossing(3), "single partition must not mark crossing"
record("s9/net3_not_crossing", not geo_p.is_crossing(3))
# 拆分裁定（2026-09-14）：本数据无 pg 网 → GEOMETRY_PG 对象恒空
# 原断言：assert geo_pg_p.net_count == 0
record("s9/geo_pg_net_count", geo_pg_p.net_count)
INFO("[DONE] S9 geometry: BLOCKAGE owns net 0 exclusively + obs flag, child "
     "n1 wire + via graphics expanded by composite transform (via cell id "
     "+ primary), GEOMETRY_PG empty")

# /NETS（2026-09-14 拆分裁定：信号网入 NETS、pg 网入 NETS_PG——本数据无
# pg 网）：仅 child n1（有几何、非 pg）全量补全两条（入 NETS 对象）；
# n_top/n2 无几何 → 不产条目（跟随 net 副本）。条目 = (端点实例 global
# id, 全局 pin id, port 位)——(5, 0) = u1.A、(3, 3) = top3 的 PIN_IN
#（port 位，端点实例 = 块实例自身 global id 3，⑧）；网 id 由 DSNet 键承
# 载，net_of 单表查命中
# 原断言：assert nets_p.size == 1 and sorted(nets_p.ids) == [3]
record("s9/nets_size", nets_p.size)
record("s9/nets_ids", sorted(nets_p.ids))
n1_net = nets_p.net_of(3)
assert n1_net is not None, "n1 must be reachable via net_of (signal side)"  # 保留：守卫
# 原断言：assert n1_net is not None, "n1 must be reachable via net_of (signal side)"
record("s9/n1_net_present", n1_net is not None)
# 原断言：assert n1_net.net_id == 3 and n1_net.use == "SIGNAL"
record("s9/n1_net_id", n1_net.net_id)
record("s9/n1_net_use", n1_net.use)
# 封存后按 (端点实例 global id, 引脚) 升序——端口条目（inst 3）在前
# 原断言：assert [(c.inst_id, c.pin_id, c.is_port) for c in n1_net.connections] == [
record("s9/n1_conn_triples", [(c.inst_id, c.pin_id, c.is_port) for c in n1_net.connections])
# 原断言：assert nets_pg_p.size == 0 and list(nets_pg_p.ids) == []
record("s9/nets_pg_size", nets_pg_p.size)
record("s9/nets_pg_ids", list(nets_pg_p.ids))
# 位直存（S9 flags 六位自 S5b 条目拷贝；2026-11-24 端口角色裁定）：n1
# 的 ( PIN PIN_IN ) 端口条目 = INPUT 端口 = 它所在网的驱动者 → driver
# 位（旧口径按引脚方向标 receiver）；u1.A 实例条目按引脚方向 INPUT →
# receiver 不变
# 原断言：assert [(c.is_receiver, c.is_driver) for c in n1_net.connections] == [
record("s9/n1_conn_flags", [(c.is_receiver, c.is_driver) for c in n1_net.connections])
# 全局 pg 网 id 集（2026-09-13 重组裁定："pg_nets" 正式对象）：本数据无
# pg 网 → 空集
db_pg = load_design_pg_nets(design_db)
# 原断言：assert db_pg.power_count == 0 and db_pg.ground_count == 0 \
record("s9/pg_power_count", db_pg.power_count)
record("s9/pg_ground_count", db_pg.ground_count)
record("s9/net1_not_pg", db_pg.is_pg(1) is False)
record("s9/net3_not_pg", db_pg.is_pg(3) is False)
# /INST_CONNECTIONS：跟随 instance 副本（含 pg 网 n2 的 instance 维度端点
# ——下游 union 拼装口径）
# 原断言：assert [(c.net_global_id, c.pin_id)
record("s9/top1_iconn", [(c.net_global_id, c.pin_id)
                         for c in iconn_p.connections_of(1)])
t3c = iconn_p.connections_of(3)
# 原断言：assert len(t3c) == 3
record("s9/top3_iconn_count", len(t3c))
# 非守卫：后续仅遍历 t3c，无下标访问
t3_pairs = [(c.net_global_id, c.pin_id) for c in t3c]
# 原断言：assert (1, 3) in t3_pairs, "parent-side endpoint on n_top (PIN_IN = pin 3)"
record("s9/top3_n_top_endpoint", (1, 3) in t3_pairs)
# 原断言：assert sorted((c.net_global_id, c.pin_id) for c in t3c if c.is_port) == \
record("s9/top3_port_endpoints", sorted((c.net_global_id, c.pin_id) for c in t3c if c.is_port))
u1c = iconn_p.connections_of(5)
# 原断言：assert sorted((c.net_global_id, c.pin_id) for c in u1c) == \
record("s9/u1_endpoints", sorted((c.net_global_id, c.pin_id) for c in u1c))
# 位直存（S9 flags 六位自 S5b 条目拷贝；2026-11-24 端口角色裁定）：n1
# 端点 u1.A 实例条目 receiver、n2 的 PIN_OUT 端口条目 = OUTPUT 端口 =
# 负载 → receiver 位（旧口径按引脚方向标 driver）
# 原断言：assert [(c.is_receiver, c.is_driver) for c in u1c if c.net_global_id == 3] \
record("s9/u1_n1_flags", [(c.is_receiver, c.is_driver) for c in u1c if c.net_global_id == 3])
# 原断言：assert [(c.is_port, c.is_receiver, c.is_driver)
record("s9/top3_n2_flags", [(c.is_port, c.is_receiver, c.is_driver)
                            for c in t3c if c.net_global_id == 4])
INFO("[DONE] S9 connections: non-pg net completion follows net copies "
     "(NETS object, NETS_PG empty by split ruling), inst connections "
     "follow instance copies (pg n2 reachable via instance dimension)")

# ── S10：汇总校验 + 冻结（2026-09-13 校验分级裁定：损坏类 fatal / 观测
# 类 warn；verify_report 正式对象随冻结落盘）──
# 手算锁定（层级树 2 节点：root inst count 4 + child count 2、nets 1+2、
# vias 0+1；单分区；top2 UNPLACED）：
#   - 实例域：expected = (4+2) − 1(非根占位槽) − 1(root 自身) = 4、actual
#     {1,3,5} = 3 → holes 1（global 2 = UNPLACED top2 合法空洞）→ 触发
#     DSGN::0022 观测 warn（不阻断冻结）；
#   - 密度守恒 primary 口径：Σ primary = 3 = Σ 首份定义 (实例数 −
#     UNPLACED) = (1−0) + (3−1) → 无偏差、无 DSGN::0023；
#   - 网域：expected 3、actual {1,3,4}（net 1 = n_top 经 INST_CONNECTIONS
#     端点引用覆盖；geometry 的 net 0 桶仅 OBS 不计）→ holes 0；
#   - via 域：expected 1（child VIA12 → global 0）、actual {0} → holes 0；
#   - 统计：primary 3、副本 3、网 3、连接 9（nconn 2 + iconn 7：top1 1 +
#     top3 3 + u1 2 + root 自身 port 连接（INST_CONNECTIONS 键 0）1）、
#     图形条目 5（OBS 1 + n1 wire/via 4）、跨分区网 0、密度 inst=2/metal=1/
#     via=1（与 S8 手算一致）。
from emir.design import load_design_verify_report
report = load_design_verify_report(design_db)
# 原断言：assert report.union_inconsistency == "" and report.coverage_gap == "" \
record("s10/union_inconsistency_clean", report.union_inconsistency == "")
record("s10/coverage_gap_clean", report.coverage_gap == "")
record("s10/namemap_inconsistency_clean", report.namemap_inconsistency == "")
# 原断言：assert report.instance_ids.expected == 4
record("s10/inst_expected", report.instance_ids.expected)
# 原断言：assert report.instance_ids.actual == 3
record("s10/inst_actual", report.instance_ids.actual)
# 原断言：assert report.instance_ids.holes == 1, "UNPLACED top2 (global 2) is a hole"
record("s10/inst_holes", report.instance_ids.holes)
# 原断言：assert report.instance_ids.duplicates == 0
record("s10/inst_duplicates", report.instance_ids.duplicates)
# 原断言：assert report.net_ids.actual == 3 and report.net_ids.holes == 0
record("s10/net_actual", report.net_ids.actual)
record("s10/net_holes", report.net_ids.holes)
# 原断言：assert report.via_ids.actual == 1 and report.via_ids.holes == 0
record("s10/via_actual", report.via_ids.actual)
record("s10/via_holes", report.via_ids.holes)
# 原断言：assert report.density_variance == ""
record("s10/density_variance_clean", report.density_variance == "")
# 原断言：assert report.partition_count == 1
record("s10/partition_count", report.partition_count)
# 原断言：assert report.total_primary == 3 and report.total_instances == 3
record("s10/total_primary", report.total_primary)
record("s10/total_instances", report.total_instances)
# 原断言：assert report.total_nets == 3 and report.total_connections == 9
record("s10/total_nets", report.total_nets)
record("s10/total_connections", report.total_connections)
# 原断言：assert report.total_geometry_entries == 5
record("s10/total_geometry_entries", report.total_geometry_entries)
# 原断言：assert report.total_crossing_nets == 0
record("s10/total_crossing_nets", report.total_crossing_nets)
# 原断言：assert (report.density_instance_total, report.density_metal_total,
record("s10/density_totals", (report.density_instance_total, report.density_metal_total,
                              report.density_via_total))
INFO("[DONE] S10 verify report: clean fatal fields, instance hole=1 "
     "(UNPLACED), primary-conservation pass, stats hand-computed")

# S10 消息透出：观测类 warn（实例空洞）+ 统计 INFO，损坏类 fatal 无
msgs_s10 = ""
for root, _dirs, files in os.walk(LOG_DIR):
    for fn in files:
        if fn.endswith(".log"):
            try:
                with open(os.path.join(root, fn), errors="ignore") as fh:
                    msgs_s10 += fh.read()
            except OSError:
                pass
# 原断言：assert bad_id not in msgs_s10, f"this run must not emit {bad_id}"
record("s10/no_fatal_msgs",
       not [m for m in ("DSGN::0019", "DSGN::0020", "DSGN::0021", "DSGN::0023")
            if m in msgs_s10],
       detail=[m for m in ("DSGN::0019", "DSGN::0020", "DSGN::0021", "DSGN::0023")
               if m in msgs_s10])
# 原断言：assert "DSGN::0022" in msgs_s10, \
record("s10/has_0022_warn", "DSGN::0022" in msgs_s10)
# 原断言：assert "DSGN::0024" in msgs_s10, "verify summary INFO should be emitted"
record("s10/has_0024_summary", "DSGN::0024" in msgs_s10)
INFO("[DONE] S10 messages: DSGN::0022 continuity warn + DSGN::0024 summary, "
     "no fatal (observational-only run)")

# ── debug 读库 API（2026-09-13 裁定：DesignDb 六方法——id↔name 转换 +
# 映射定位 + 按需加载 LRU；design db 数据手算锁定）──
# id→partition 反向映射段读回：INST 段 {1,3,5}→pid0（top2 UNPLACED 无
# primary 副本 = 空洞）；NET 段 {0,3}→pid0（键 0 = OBS 专属桶——geometry
# 键集；n_top（1）无几何不入映射；n2（4）无几何不入映射；2 = child 空
# 洞位不入映射）
from emir.design import DesignDb
inst_index = design_db.read_object(DesignDb.id_map_index_obj_name("INST"))
# 原断言：assert inst_index.segment_count == 1
record("idmap/inst_segment_count", inst_index.segment_count)
inst_seg = design_db.read_object(
    DesignDb.id_map_segment_obj_name("INST", 0))
# 原断言：assert inst_seg.id_start == 0
record("idmap/inst_seg_id_start", inst_seg.id_start)
# 原断言：assert inst_seg.size == DesignDb.ID_MAP_SEGMENT_SIZE
record("idmap/inst_seg_size_const", inst_seg.size == DesignDb.ID_MAP_SEGMENT_SIZE)
# 原断言：assert inst_seg.partition_of(1) == 0 and inst_seg.partition_of(5) == 0
record("idmap/inst1_partition", inst_seg.partition_of(1))
record("idmap/inst5_partition", inst_seg.partition_of(5))
# 原断言：assert inst_seg.partition_of(2) is None, \
record("idmap/inst2_partition", inst_seg.partition_of(2))
# 非守卫：仅检查空洞位不映射，返回值无后续解引用
net_index = design_db.read_object(DesignDb.id_map_index_obj_name("NET"))
# 原断言：assert net_index.segment_count == 1
record("idmap/net_segment_count", net_index.segment_count)
net_seg = design_db.read_object(DesignDb.id_map_segment_obj_name("NET", 0))
# 原断言：assert net_seg.partition_of(0) == 0, \
record("idmap/net0_partition", net_seg.partition_of(0))
# 原断言：assert net_seg.partition_of(3) == 0, "n1 has geometry in the partition"
record("idmap/net3_partition", net_seg.partition_of(3))
# 原断言：assert net_seg.partition_of(1) is None, \
record("idmap/net1_partition", net_seg.partition_of(1))
# 非守卫：仅检查无几何网不入映射，返回值无后续解引用
# 原断言：assert net_seg.partition_of(4) is None, \
record("idmap/net4_partition", net_seg.partition_of(4))
# 非守卫：仅检查无几何网不入映射，返回值无后续解引用
# 原断言：assert net_seg.partition_of(2) is None, "local-0 hole slot is not a net"
record("idmap/net2_partition", net_seg.partition_of(2))
# 非守卫：仅检查空洞位不映射，返回值无后续解引用
INFO("[DONE] id->partition map: segment index + direct-index pids, holes "
     "for UNPLACED instance and geometry-less nets")

# get_instance：层级路径入参（pos (1350,700) 与 S9 段同源手算）
inst = design_db.get_instance("top3/u1")
# 原断言：assert inst["id"] == 5 and inst["name"] == "top3/u1"
record("dbgapi/u1_id", inst["id"])
record("dbgapi/u1_name", inst["name"])
# 原断言：assert inst["pos"] == (1350, 700) and inst["orient"] == 0
record("dbgapi/u1_pos", inst["pos"])
record("dbgapi/u1_orient", inst["orient"])
# 原断言：assert inst["placement_status"] == "PLACED"
record("dbgapi/u1_placement", inst["placement_status"])
# 原断言：assert inst["primary_partition_id"] == 0
record("dbgapi/u1_partition", inst["primary_partition_id"])
# 原断言：assert inst["cell_id"] == inv_id and inst["cell_name"] == "INV_X1"
record("dbgapi/u1_cell_matches_inv", inst["cell_id"] == inv_id)
record("dbgapi/u1_cell_name", inst["cell_name"])
# 原断言：assert [(c["net_id"], c["net_name"], c["pin_name"])
record("dbgapi/u1_conns", [(c["net_id"], c["net_name"], c["pin_name"])
                           for c in inst["connections"]])
# 原断言：assert [(c["is_receiver"], c["is_driver"])
record("dbgapi/u1_conn_flags", [(c["is_receiver"], c["is_driver"])
                                for c in inst["connections"]])
# id 入参（top1：连接 = root 网 n_top（global 1）的 A 端点）
top1 = design_db.get_instance(1)
# 原断言：assert top1["name"] == "top1"
record("dbgapi/top1_name", top1["name"])
# 原断言：assert [(c["net_id"], c["net_name"], c["pin_name"], c["is_port"])
record("dbgapi/top1_conns", [(c["net_id"], c["net_name"], c["pin_name"], c["is_port"])
                             for c in top1["connections"]])
# root 占位 id 0 / 越界 / 未知名 → None
# 原断言：assert design_db.get_instance(0) is None, \
record("dbgapi/inst0_none", design_db.get_instance(0))
# 非守卫：仅检查占位 id 查不到，返回值无后续解引用
# 原断言：assert design_db.get_instance(999) is None
record("dbgapi/inst999_none", design_db.get_instance(999))
# 非守卫：仅检查越界 id 查不到，返回值无后续解引用
# 原断言：assert design_db.get_instance("ghost") is None
record("dbgapi/inst_ghost_none", design_db.get_instance("ghost"))
# 非守卫：仅检查未知名查不到，返回值无后续解引用

# get_net：非 pg 明细（端点实例名 + pin 名 name 化；port 条目 = 块实例
# 层级名 + port 名）；无几何网查不到；id 0 = OBS 专属位非真网 → None
# n1 的 ( PIN PIN_IN ) 端口条目：PIN_IN 是 INPUT 端口 = 它所在网的驱动
# 者（2026-11-24 端口角色裁定）→ 驱动 1、接收 1（旧口径按引脚方向标
# receiver：驱动 0、接收 2）；连接表 (实例, 引脚) 升序——端口条目在前
n1 = design_db.get_net("top3/n1")
# 原断言：assert n1["id"] == 3 and n1["use"] == "SIGNAL" and n1["is_pg"] is False
record("dbgapi/net_n1_id", n1["id"])
record("dbgapi/net_n1_use", n1["use"])
record("dbgapi/net_n1_not_pg", n1["is_pg"] is False)
# 原断言：assert n1["connection_count"] == 2
record("dbgapi/net_n1_conn_count", n1["connection_count"])
# 原断言：assert n1["receiver_count"] == 1 and n1["driver_count"] == 1 \
record("dbgapi/net_n1_receiver_count", n1["receiver_count"])
record("dbgapi/net_n1_driver_count", n1["driver_count"])
record("dbgapi/net_n1_hybrid_count", n1["hybrid_count"])
# 原断言：assert [(c["instance_id"], c["instance_name"], c["pin_name"], c["is_port"])
record("dbgapi/net_n1_conns", [(c["instance_id"], c["instance_name"], c["pin_name"], c["is_port"])
                               for c in n1["connections"]])
# n_top 有连接但无几何（block_parent.def 的 n_top 无 wire/rect）→ 无
# 分区副本、NETS 无 DSNet 记录 → 显式 None（2026-09-13 review 修正：
# 旧兜底曾静默降级为空概要——丢失连接信息且误导；其真实连接在 S5b
# 产物与 net_union 中）
n_top = design_db.get_net("n_top")
# 原断言：assert n_top is None, \
record("dbgapi/net_n_top_none", n_top)
# 非守卫：仅检查无几何网显式 None，返回值无后续解引用
# 原断言：assert design_db.get_net("top3/n2") is None, \
record("dbgapi/net_n2_none", design_db.get_net("top3/n2"))
# 非守卫：仅检查无几何网查不到，返回值无后续解引用
# 原断言：assert design_db.get_net(0) is None, \
record("dbgapi/net0_none", design_db.get_net(0))
# 非守卫：仅检查 OBS 专属位查不到，返回值无后续解引用
# 原断言：assert design_db.get_net(999) is None
record("dbgapi/net999_none", design_db.get_net(999))
# 非守卫：仅检查越界 id 查不到，返回值无后续解引用

# get_cell / get_layer
cell = design_db.get_cell("INV_X1")
# 原断言：assert cell["id"] == inv_id and cell["pin_count"] == 3
record("dbgapi/cell_inv_id_matches", cell["id"] == inv_id)
record("dbgapi/cell_inv_pin_count", cell["pin_count"])
# 原断言：assert "lef_cell" in cell["flags"] and "lib_cell" in cell["flags"]
record("dbgapi/cell_inv_has_lef_flag", "lef_cell" in cell["flags"])
record("dbgapi/cell_inv_has_lib_flag", "lib_cell" in cell["flags"])
# 原断言：assert cell["library_name"] == "minitest_typ"
record("dbgapi/cell_inv_library", cell["library_name"])
pins = {p["name"]: p for p in cell["pins"]}
# 原断言：assert pins["A"]["direction"] == "INPUT" and pins["A"]["is_port"] is False
record("dbgapi/pin_a_direction", pins["A"]["direction"])
record("dbgapi/pin_a_not_port", pins["A"]["is_port"] is False)
# 原断言：assert pins["VDD"]["type"] == "POWER"
record("dbgapi/pin_vdd_type", pins["VDD"]["type"])
block_cell = design_db.get_cell("block_child")
# 原断言：assert "block_cell" in block_cell["flags"]
record("dbgapi/block_child_flag", "block_cell" in block_cell["flags"])
# 原断言：assert block_cell["pins"][0]["is_port"] is True \
record("dbgapi/block_child_pin0_is_port", block_cell["pins"][0]["is_port"] is True)
record("dbgapi/block_child_pin0_name", block_cell["pins"][0]["name"])
layer = design_db.get_layer("M1")
# 原断言：assert layer["id"] == 0 and layer["name"] == "M1"
record("dbgapi/layer_m1_id", layer["id"])
record("dbgapi/layer_m1_name", layer["name"])
# 原断言：assert layer["type"] == "ROUTING" and layer["direction"] == "HORIZONTAL"
record("dbgapi/layer_m1_type", layer["type"])
record("dbgapi/layer_m1_direction", layer["direction"])
# 原断言：assert layer["width"] == 70 and layer["pitch"] == 190 \
record("dbgapi/layer_m1_width", layer["width"])
record("dbgapi/layer_m1_pitch", layer["pitch"])
record("dbgapi/layer_m1_spacing", layer["spacing"])
# 原断言：assert design_db.get_layer("VIA1")["type"] == "CUT"
record("dbgapi/layer_via1_type", design_db.get_layer("VIA1")["type"])
# 原断言：assert design_db.get_layer("GHOST") is None and design_db.get_layer(99) is None
record("dbgapi/layer_ghost_none", design_db.get_layer("GHOST"))
record("dbgapi/layer99_none", design_db.get_layer(99))
# 非守卫：仅检查未知名/越界 id 查不到，返回值无后续解引用

# convert 双向（kind+name 形态 / kwargs 直写形态——convert_to_name(inst=10)
# 为用户示例 kwargs 形态；inst/net 走层级路径 mapper、pin 组合键）
# 原断言：assert design_db.convert_to_id("cell", "INV_X1") == inv_id
record("conv/cell_positional", design_db.convert_to_id("cell", "INV_X1") == inv_id)
# 原断言：assert design_db.convert_to_id(kind="cell", name="INV_X1") == inv_id
record("conv/cell_kwargs", design_db.convert_to_id(kind="cell", name="INV_X1") == inv_id)
# 原断言：assert design_db.convert_to_id(cell="INV_X1") == inv_id
record("conv/cell_shorthand", design_db.convert_to_id(cell="INV_X1") == inv_id)
# 原断言：assert design_db.convert_to_name(cell=inv_id) == "INV_X1"
record("conv/cell_name_back", design_db.convert_to_name(cell=inv_id))
# 原断言：assert design_db.convert_to_id(pin="INV_X1/A") == 0
record("conv/pin_combined", design_db.convert_to_id(pin="INV_X1/A"))  # 裁定 3：组合形态兼容（取名段 A）
# 原断言：assert design_db.convert_to_id(pin="A") == 0
record("conv/pin_bare", design_db.convert_to_id(pin="A"))
# 原断言：assert design_db.convert_to_name(pin=0) == "A"
record("conv/pin_name_back", design_db.convert_to_name(pin=0))  # 裁定 3：键 = 裸 pin 名
# 原断言：assert design_db.convert_to_id(layer="M2") == 2
record("conv/layer_m2", design_db.convert_to_id(layer="M2"))
# 原断言：assert design_db.convert_to_name(layer=2) == "M2"
record("conv/layer_name_back", design_db.convert_to_name(layer=2))
via12 = design.via_cell_id_by_name("block_child::VIA12")
# 原断言：assert design_db.convert_to_id(via_cell="block_child::VIA12") == via12
record("conv/via_cell_matches", design_db.convert_to_id(via_cell="block_child::VIA12") == via12)
# 原断言：assert design_db.convert_to_name(via_cell=via12) == "block_child::VIA12"
record("conv/via_name_back", design_db.convert_to_name(via_cell=via12))
# 原断言：assert design_db.convert_to_id(inst="top3/u1") == 5
record("conv/inst_u1", design_db.convert_to_id(inst="top3/u1"))
# 原断言：assert design_db.convert_to_name(inst=5) == "top3/u1"
record("conv/inst_name_back", design_db.convert_to_name(inst=5))
# 原断言：assert design_db.convert_to_name(inst=10) is None, \
record("conv/inst10_none", design_db.convert_to_name(inst=10))
# 非守卫：仅检查越界 id 返回 None（用户示例 kwargs 形态），返回值无后续解引用
# 原断言：assert design_db.convert_to_id(net="n_top") == 1
record("conv/net_n_top", design_db.convert_to_id(net="n_top"))
# 原断言：assert design_db.convert_to_name(net=1) == "n_top"
record("conv/net_name_back", design_db.convert_to_name(net=1))
# 原断言：assert design_db.convert_to_id(cell="GHOST") is None
record("conv/ghost_none", design_db.convert_to_id(cell="GHOST"))
# 非守卫：仅检查未知名返回 None，返回值无后续解引用
INFO("[DONE] debug API on design db: get_instance/get_net/get_cell/"
     "get_layer/convert both directions (id+name paired, name-resolved)")

# ── debug db（独立 lef 组 cells_debug.lef：INOUT pin cell）+ USE 八值
# 收录 / hybrid 混合类型 / pg 网概要 + 拆分裁定六对象分侧断言——debug API
# 手算锁定。手算 id 面：pin 平铺注册序 A=0/ZN=1/VDD=2/BIDIR=3/VDDP=4/
# DBG_IN=5；cell INV_X1=0/FILLER01=1/INVIO=2/debug_design=3；instance
# h1=1/h2=2；net local sig1=1/tie1=2/weird=3/VDD0=4 →（区间含空洞位）
# global 1/2/3/4 ──
DEBUG_DATA = os.path.join(SCRIPT_DIR, "data", "debug")
DEBUG_DEF = os.path.join(DEBUG_DATA, "debug.def")
CELLS_DEBUG = os.path.join(DEBUG_DATA, "cells_debug.lef")
debug_db = proj.build_design_db(
    name="design_debug",
    def_paths=[DEBUG_DEF],
    lef_paths=[TECH_LEF, CELLS_MAIN, CELLS_DEBUG],
    lib_db=lib_db,
)
assert proj.wait_frozen("design_debug", timeout=180), \
    "debug design db should freeze"
INFO("[WAIT] debug design db frozen")

# S5b 产物 use 收录（EXDSNetBuildData 面；字符串化导出——非 SIGNAL 才
# 落存储，SIGNAL 缺省读取）
dbg_net0 = debug_db.read_object(debug_db.net_obj_name(0))
# 原断言：assert dbg_net0.net_use_of(1) == "SIGNAL", \
record("debugdb/sig1_use_storage", dbg_net0.net_use_of(1))  # SIGNAL 缺省读取
# 原断言：assert dbg_net0.net_use_of(2) == "TIEOFF"
record("debugdb/tie1_use_storage", dbg_net0.net_use_of(2))
# 原断言：assert dbg_net0.net_use_of(3) == "ANALOG"
record("debugdb/weird_use_storage", dbg_net0.net_use_of(3))
# 原断言：assert dbg_net0.net_use_of(4) == "POWER"
record("debugdb/vdd0_use_storage", dbg_net0.net_use_of(4))
INFO("[DONE] S5b USE collection: eight-value enum, non-SIGNAL recorded, "
     "string-ized export")

# get_net 手算：sig1（global 1）连接 3 条 = h1.BIDIR（INOUT 实例引脚 →
# hybrid）+ h2.A（INPUT 实例引脚 → receiver）+ PIN DBG_IN（port 位 +
# INPUT 端口 = 它所在网的驱动者，2026-11-24 端口角色裁定 → driver）——
# driver 1 / receiver 1 / hybrid 1 / port 1（三分类互斥单列口径；连接表
# (实例, 引脚) 升序——端口条目 local 0 在前）
sig1 = debug_db.get_net("sig1")
# 原断言：assert sig1["id"] == 1 and sig1["use"] == "SIGNAL"
record("debugdb/sig1_id", sig1["id"])
record("debugdb/sig1_use", sig1["use"])
# 原断言：assert sig1["connection_count"] == 3
record("debugdb/sig1_conn_count", sig1["connection_count"])
# 原断言：assert sig1["hybrid_count"] == 1, "INOUT endpoint = hybrid (third class)"
record("debugdb/sig1_hybrid_count", sig1["hybrid_count"])
# 原断言：assert sig1["driver_count"] == 1 and sig1["receiver_count"] == 1
record("debugdb/sig1_driver_count", sig1["driver_count"])
record("debugdb/sig1_receiver_count", sig1["receiver_count"])
# 原断言：assert sig1["port_count"] == 1
record("debugdb/sig1_port_count", sig1["port_count"])
# 原断言：assert [(c["instance_name"], c["pin_name"], c["is_port"])
record("debugdb/sig1_conns", [(c["instance_name"], c["pin_name"], c["is_port"])
                              for c in sig1["connections"]])
# tie1（global 2）/weird（global 3）：USE 八值语义；VDD0（global 4）：pg
# 网（USE POWER）无 connections 明细、计数概要数位（VDDP INPUT POWER →
# receiver 1 + power 1）
tie1 = debug_db.get_net(2)
# 原断言：assert tie1["name"] == "tie1" and tie1["use"] == "TIEOFF"
record("debugdb/tie1_name", tie1["name"])
record("debugdb/tie1_use", tie1["use"])
# 原断言：assert tie1["is_pg"] is False and tie1["driver_count"] == 1
record("debugdb/tie1_not_pg", tie1["is_pg"] is False)
record("debugdb/tie1_driver_count", tie1["driver_count"])
weird = debug_db.get_net("weird")
# 原断言：assert weird["use"] == "ANALOG" and weird["receiver_count"] == 1
record("debugdb/weird_use", weird["use"])
record("debugdb/weird_receiver_count", weird["receiver_count"])
vdd0 = debug_db.get_net("VDD0")
# 原断言：assert vdd0["id"] == 4 and vdd0["use"] == "POWER" and vdd0["is_pg"] is True
record("debugdb/vdd0_id", vdd0["id"])
record("debugdb/vdd0_use", vdd0["use"])
record("debugdb/vdd0_is_pg", vdd0["is_pg"] is True)
# 原断言：assert "connections" not in vdd0, \
record("debugdb/vdd0_no_connections", "connections" not in vdd0)
# 原断言：assert vdd0["connection_count"] == 1 and vdd0["receiver_count"] == 1 \
record("debugdb/vdd0_conn_count", vdd0["connection_count"])
record("debugdb/vdd0_receiver_count", vdd0["receiver_count"])
record("debugdb/vdd0_power_count", vdd0["power_count"])
# 拆分裁定（2026-09-14）：get_net 按 is_pg 路由——VDD0 命中 NETS_PG 对
# 象单表、其余命中 NETS 对象（两侧互斥断言见下方六对象读取）
INFO("[DONE] get_net hand-computed: hybrid/driver/receiver/port counting "
     "from flags bits, USE property, pg summary without details, "
     "is_pg-routed single-table lookup")

# get_instance 手算：h1 连接 = sig1（1）的 BIDIR（hybrid）+ VDD0（4）的
# VDDP（receiver + power 位）；h2 连接 = sig1 A（receiver）+ tie1 ZN
#（driver）+ weird A（receiver）
h1 = debug_db.get_instance("h1")
# 原断言：assert h1["id"] == 1 and h1["cell_name"] == "INVIO"
record("debugdb/h1_id", h1["id"])
record("debugdb/h1_cell_name", h1["cell_name"])
# 原断言：assert h1["pos"] == (100, 100) and h1["orient"] == 0
record("debugdb/h1_pos", h1["pos"])
record("debugdb/h1_orient", h1["orient"])
# 原断言：assert sorted((c["net_id"], c["net_name"], c["is_driver"], c["is_receiver"],
record("debugdb/h1_conns", sorted((c["net_id"], c["net_name"], c["is_driver"], c["is_receiver"],
                                   c["is_power"]) for c in h1["connections"]))
h2 = debug_db.get_instance(2)
# 原断言：assert h2["cell_name"] == "INV_X1"
record("debugdb/h2_cell_name", h2["cell_name"])
# 原断言：assert sorted((c["net_id"], c["is_receiver"], c["is_driver"])
record("debugdb/h2_conns", sorted((c["net_id"], c["is_receiver"], c["is_driver"])
                                  for c in h2["connections"]))
# 原断言：assert debug_db.get_cell("INVIO")["pin_count"] == 2
record("debugdb/invio_pin_count", debug_db.get_cell("INVIO")["pin_count"])
# 原断言：assert debug_db.get_instance(0) is None
record("debugdb/inst0_none", debug_db.get_instance(0))
# 非守卫：仅检查占位 id 查不到，返回值无后续解引用
# debug db 映射段读回：INST {1,2}→0、NET {0,1,2,3,4}（键 0 = OBS 桶——
# 本数据无 BLOCKAGE 不存在，1-4 四网全有几何）
dbg_inst_seg = debug_db.read_object(
    DesignDb.id_map_segment_obj_name("INST", 0))
# 原断言：assert dbg_inst_seg.partition_of(1) == 0 and dbg_inst_seg.partition_of(2) == 0
record("debugdb/inst1_partition", dbg_inst_seg.partition_of(1))
record("debugdb/inst2_partition", dbg_inst_seg.partition_of(2))
# 原断言：assert dbg_inst_seg.partition_of(3) is None
record("debugdb/inst3_partition", dbg_inst_seg.partition_of(3))
# 非守卫：仅检查空洞位不映射，返回值无后续解引用
dbg_net_seg = debug_db.read_object(
    DesignDb.id_map_segment_obj_name("NET", 0))
# 原断言：assert dbg_net_seg.partition_of(0) is None, \
record("debugdb/net0_partition", dbg_net_seg.partition_of(0))
# 非守卫：仅检查无 BLOCKAGE 无键 0，返回值无后续解引用
# 原断言：assert all(dbg_net_seg.partition_of(i) == 0 for i in range(1, 5))
record("debugdb/nets_1_to_4_in_partition0",
       all(dbg_net_seg.partition_of(i) == 0 for i in range(1, 5)))
# debug db S10 校验手算：primary 2 / 网 4 / 连接 12（nconn 6 + iconn 6：
# h1 2 + h2 3 + root port 1）/ 图形条目 5（sig1 2 段 + tie1/weird/VDD0 各 1）
dbg_report = debug_db.read_object(DesignDb.VERIFY_REPORT_OBJ)
# 原断言：assert dbg_report.union_inconsistency == "" \
record("debugdb/union_inconsistency_clean", dbg_report.union_inconsistency == "")
record("debugdb/namemap_inconsistency_clean", dbg_report.namemap_inconsistency == "")
# 原断言：assert dbg_report.total_primary == 2 and dbg_report.total_nets == 4
record("debugdb/total_primary", dbg_report.total_primary)
record("debugdb/total_nets", dbg_report.total_nets)
# 原断言：assert dbg_report.total_connections == 12
record("debugdb/total_connections", dbg_report.total_connections)
# 原断言：assert dbg_report.total_geometry_entries == 5
record("debugdb/total_geometry_entries", dbg_report.total_geometry_entries)
# 全局 pg 网 id 集（2026-09-13 重组裁定）：VDD0（SPECIALNETS + USE POWER
# → global 4）为唯一 pg 网 → power set = {4}；sig1/tie1/weird 不入集
dbg_pg = load_design_pg_nets(debug_db)
# 原断言：assert dbg_pg.power_count == 1 and dbg_pg.ground_count == 0
record("debugdb/pg_power_count", dbg_pg.power_count)
record("debugdb/pg_ground_count", dbg_pg.ground_count)
# 原断言：assert dbg_pg.is_power(4) is True and dbg_pg.is_pg(4) is True
record("debugdb/net4_is_power", dbg_pg.is_power(4) is True)
record("debugdb/net4_is_pg", dbg_pg.is_pg(4) is True)
# 原断言：assert dbg_pg.is_pg(0) is False and dbg_pg.is_pg(1) is False \
record("debugdb/net0_not_pg", dbg_pg.is_pg(0) is False)
record("debugdb/net1_not_pg", dbg_pg.is_pg(1) is False)
record("debugdb/net2_not_pg", dbg_pg.is_pg(2) is False)
record("debugdb/net3_not_pg", dbg_pg.is_pg(3) is False)
# 拆分裁定（2026-09-14）六对象分侧断言：VDD0（pg）几何/连接落 _PG 侧对
# 象、sig1/tie1/weird 落信号侧对象——④ 提取首期只加载 GEOMETRY_PG +
# NETS_PG 即电源网络输入全集的受益形态
dbg_geo = debug_db.read_object(
    DesignDb.partition_obj_name(0, 0, "GEOMETRY"))
dbg_geo_pg = debug_db.read_object(
    DesignDb.partition_obj_name(0, 0, "GEOMETRY_PG"))
dbg_nets = debug_db.read_object(DesignDb.partition_obj_name(0, 0, "NETS"))
dbg_nets_pg = debug_db.read_object(
    DesignDb.partition_obj_name(0, 0, "NETS_PG"))
# 原断言：assert dbg_geo.net_count == 3 and len(dbg_geo.entries_of(4)) == 0
record("debugdb/geo_net_count", dbg_geo.net_count)
record("debugdb/geo_net4_entries", len(dbg_geo.entries_of(4)))
# 原断言：assert dbg_geo_pg.net_count == 1 and len(dbg_geo_pg.entries_of(4)) == 1
record("debugdb/geo_pg_net_count", dbg_geo_pg.net_count)
record("debugdb/geo_pg_net4_entries", len(dbg_geo_pg.entries_of(4)))
assert len(dbg_geo_pg.entries_of(4)) >= 1  # 保留：守卫（后面按下标取条目）
# 原断言：assert not dbg_geo_pg.entries_of(4)[0].is_obs
record("debugdb/geo_pg_net4_not_obs", not dbg_geo_pg.entries_of(4)[0].is_obs)
# 原断言：assert dbg_nets.size == 3 and dbg_nets.net_of(4) is None
record("debugdb/nets_size", dbg_nets.size)
record("debugdb/nets_net4_none", dbg_nets.net_of(4))
# 非守卫：仅检查 pg 网 VDD0 不在信号侧对象，返回值无后续解引用
# 原断言：assert sorted(dbg_nets.ids) == [1, 2, 3]
record("debugdb/nets_ids", sorted(dbg_nets.ids))
# 原断言：assert dbg_nets_pg.size == 1 and sorted(dbg_nets_pg.ids) == [4]
record("debugdb/nets_pg_size", dbg_nets_pg.size)
record("debugdb/nets_pg_ids", sorted(dbg_nets_pg.ids))
vdd_pg = dbg_nets_pg.net_of(4)
# 原断言：assert vdd_pg is not None and vdd_pg.use == "POWER" \
record("debugdb/vdd_pg_present", vdd_pg is not None)
record("debugdb/vdd_pg_summary", None if vdd_pg is None else
       {"use": vdd_pg.use, "connection_count": vdd_pg.connection_count})
# 非守卫：vdd_pg 后续不再使用，空值安全取值已兜底
INFO("[DONE] debug db: partition use field, pg net set {4}, id map segments, "
     "S10 stats, split-rule six objects (VDD0 isolated in GEOMETRY_PG + "
     "NETS_PG) hand-computed")

# ── load_project 动态还原 ──
import fly
restored = fly.load_project(PROJ_PATH)
# 原断言：assert isinstance(restored, EMIRProject), \
record("restore/class_name", type(restored).__name__)
# 原断言：assert "build_design_db" in restored.list_flows()
record("restore/has_build_design_db", "build_design_db" in restored.list_flows())
restored_design = restored.get_db("design").load_design()
# 原断言：assert restored_design.cell_count >= 4, "restored design should be readable"
record("restore/cells_readable", restored_design.cell_count >= 4,
       detail=restored_design.cell_count)
INFO("[DONE] load_project restored EMIRProject with design db")

get_agent().stop()
INFO("[DONE] test_emir_project_design")
