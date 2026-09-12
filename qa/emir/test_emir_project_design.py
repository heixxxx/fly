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
    alpha 键 net_batch_size）：连接表（port 引用判别）、wire 段（缺省宽
    回填）、⑩ via instance（⑫ design:: 前缀名解析）、⑥ 金属/通孔逐层
    分列密度通道 → 正式 DSNet_<i> 产物对象 + DSGN::0009 统计汇总；
  - S6 汇总任务（层级树 + 起始编号，⑧⑨⑮；消费 S5a+S5b 计数）：
    主 DEF = 唯一无父者（block_parent 实例化 block_child 的两层嵌套），
    DFS 序连续分配 instance/net/via 三类区间，树嵌正式 DSDesign
    （唯一写定前构建）→ 四接口/换算/format_tree 只读面；
  - ⑱ load_design_with 统一加载注入 + load_project 动态还原。
"""
import os
import shutil

from log import INFO

from fly import get_config, launch_workers
from fly.runtime import get_agent
from emir import EMIRProject

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
assert "build_design_db" in proj.list_flows(), f"flows={proj.list_flows()}"

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
assert stack.layer_count == 3, f"layers={stack.layer_count}"  # implant 跳过
assert stack.dbu_per_micron == 1000, f"dbu={stack.dbu_per_micron}"
assert stack.manufacturing_grid == 3, f"grid={stack.manufacturing_grid}"
assert stack.layer_at(0).name == "M1"
assert stack.layer_at(1).name == "VIA1"
assert stack.layer_at(2).name == "M2"
assert stack.find_layer("M2") == 2
assert stack.find_layer("IMPLANT1") is None, "implant layer must be skipped"
INFO("[OK] S1 stack: 3 layers (M1/VIA1/M2), DBU=1000 (裁定 ㉝ 恒基准，tech lef 声明 2000 仅参考), grid=3 (0.0025µm×1000 四舍五入)")

# ── 容器：cells / lib merge / block cells ──
design = design_db.load_design()
# 正常 cell 4 个（INV/FILLER/两 block）；S5a fake cell 稀疏落位（⑳ hash
# 扰动 id）使 cells_ 尾部带占位空洞 → cell_count（表长）>= 正常数
assert design.cell_count >= 4, f"cells={design.cell_count}"

# cell namemap 双向（②；block cell 与 macro 同一编号空间，㉙）
inv = design.find_cell("INV_X1")
assert inv is not None, "INV_X1 should exist"
inv_id = design.cell_id_by_name("INV_X1")
assert design.cell_name_by_id(inv_id) == "INV_X1"
child = design.find_cell("block_child")
assert child is not None, "block_child should exist"
parent = design.find_cell("block_parent")
assert parent is not None, "block_parent should exist"
assert design.find_cell("DFF_X1") is None, "DFF_X1 = lib-only (DSGN::0003)"
INFO("[OK] cells: 4 (macro + filler + 2 block cells), simplified pins intact")

# 简化 pin（⑰）：lef 侧 3 pin（A/ZN/VDD），USE POWER → power 类型。
# R7 ㊱：DSPin 无 name——pin 名经容器 pin hasher（组合键 "cell/pin"）反查
assert inv.pin_count == 3, f"inv pins={inv.pin_count}"
pin_a = inv.pin_at(0)
assert design.pin_name_of(pin_a.pin_id) == "A", "pin name must resolve via hasher"
assert pin_a.direction == 0  # INPUT
assert pin_a.is_port is False, "macro pin must not carry the port flag (㉙)"
pin_vdd = inv.pin_at(2)
assert design.pin_name_of(pin_vdd.pin_id) == "VDD" and pin_vdd.type == 1  # POWER
# R4：cell.pins_ 的全局平铺 pin id（D1，与 pin namemap 同源）
assert pin_a.pin_id == design.pin_id_by_name("INV_X1", "A"), \
    "pin_id_ must match the global pin namemap"
INFO("[OK] simplified pins intact with global pin ids (R4)")

# S3 merge：匹配 cell 填 lib 字段；lef-only cell 不填
assert inv.library_name == "minitest_typ", f"library_name={inv.library_name}"
filler = design.find_cell("FILLER01")
assert filler is not None and not filler.library_name, \
    "lef-only cell must have empty library_name (DSGN::0002)"
INFO("[OK] S3 merge: INV_X1 matched (lib fields filled), FILLER01 lef-only")

# ── via cell 权威表（⑫ 前缀 + 生成式展开）──
assert design.via_cell_count == 3, f"vias={design.via_cell_count}"
via_tech = design.find_via_cell("VIA12")             # tech lef 首份保留
assert via_tech is not None, "tech VIA12 must survive (keep first)"
via_prefixed = design.find_via_cell("block_child::VIA12")
assert via_prefixed is not None, "⑫ prefixed DEF via missing"
assert via_prefixed.cut_rect_at(0)[2] == 40         # def 40 ×1（基准 1000）
assert via_prefixed.bottom_enclosure_at(0)[0] == -100
via_gen = design.find_via_cell("block_child::VIA12G")
assert via_gen is not None, "generated via missing"
assert via_gen.cut_rect_at(0)[0] == -30             # CUTSIZE 60 ±30 ×1
assert via_gen.bottom_enclosure_at(0)[0] == -50     # cut ±30 + ENCLOSURE 20
assert via_gen.top_enclosure_at(0)[2] == 55         # cut ±30 + ENCLOSURE 25
INFO("[OK] S4b via table: 3 (tech VIA12 + block_child::VIA12 + ::VIA12G)")

# ── S4：block cell（㉙ = DSCell + block_cell 位）与 port pin ──
assert child.is_block_cell is True, "block_child must be a block cell"
assert child.name == "block_child"
# DIEAREA 4 点双存（含负坐标）×2：bbox + polygon 全点集（㉞）。
# 恒基准 1000：def 值 ×1（UNITS 1000）
assert child.bbox == (-500, -250, 750, 1000), f"bbox={child.bbox}"
assert child.is_polygon is True, "4-point DIEAREA must set is_polygon"
assert len(child.polygon) == 4, f"polygon points={len(child.polygon)}"
assert child.polygon[0] == (-500, -250)
assert child.polygon[3] == (750, -250)
assert child.def_units_per_micron == 1000
assert child.def_path.endswith("block_child.def"), f"def_path={child.def_path}"
# P7：origin = −diearea 左下角
assert child.origin_x == 500 and child.origin_y == 250, \
    f"origin=({child.origin_x},{child.origin_y})"
# port = pins_ 的 port 位 DSPin（㉙）；port 名经 pin hasher 反查（R7 ㊱）
assert child.pin_count == 2, f"child pins={child.pin_count}"
pin_in = child.pin_at(0)
assert pin_in.is_port is True, "block port must carry the port flag"
assert pin_in.placement_status == 1  # FIXED（DSPinPlacementStatus）
pin_out = child.pin_at(1)
assert pin_out.placement_status == 3  # PLACED
assert design.pin_name_of(pin_in.pin_id) == "PIN_IN"
assert design.pin_name_of(pin_out.pin_id) == "PIN_OUT"

parent = design.find_cell("block_parent")
assert parent is not None
assert parent.is_block_cell is True
# DIEAREA 2 点矩形：bbox 直存（units 2000，恒基准 1000 → ×0.5）、polygon 空
assert parent.bbox == (0, 0, 1500, 1000), f"bbox={parent.bbox}"
assert parent.is_polygon is False, "2-point DIEAREA must not set is_polygon"
assert len(parent.polygon) == 0
assert parent.pin_count == 1
INFO("[OK] S4 block cells: bbox/polygon dual storage, ports as port-flag pins")

# ── 实例解析（对应方案 S5a 阶段）：per-DEF 产物（实例表/fake cell/密度）
# 与名字伴生对象（R7 ㊵② 落盘拆分：DSBlock_<i> 不含名字，名字在
# DSBlockNames_<i>）──
from emir.design import load_block_names, load_name_mapper
block0 = design_db.read_object(design_db.block_obj_name(0))  # block_child
assert block0.block_name == "block_child"
assert block0.instance_total == 2, "local 0 placeholder + u1 (⑧)"
assert block0.leaf_count == 1 and block0.fake_cell_count == 0
# R7 ㊵②：DSBlock_<i> 落盘不含名字（网名查询需读伴生对象注入）。
# 伴生对象双向查询（attach 为 CMSharedPtr 共享注入、hasher 归属不变）
names0 = load_block_names(design_db, 0)  # block_child 名字伴生对象
assert names0.block_name == "block_child"
# ③ 网名扫描：local net id 从 1 起（双向，经伴生对象 hasher）
assert names0.instance_id_by_name("u1") == 1
assert names0.instance_name_by_id(1) == "u1"
assert names0.net_id_by_name("n1") == 1 and names0.net_id_by_name("n2") == 2
assert names0.net_name_by_id(2) == "n2"
# 注入临时产物后经 find_instance_by_name 查名取实例（R7 ㊱）
block0.attach_names(names0)
# R6：u1 INV_X1 + PLACED (100,200) N，UNITS 1000 = 基准 1000 ×1 → pos=(100,200)
u1 = block0.find_instance_by_name("u1")
assert u1 is not None and u1.cell_id == design.cell_id_by_name("INV_X1")
assert (u1.pos_x, u1.pos_y) == (100, 200) and u1.orient == 0
assert u1.placement_status == 3  # PLACED（DSPlacementStatus）
assert block0.net_count == 2
# 密度（实例面积通道）：bin = alpha density_bin_size 10µm × 1000 = 10000
# DBU → child DIEAREA ×1 = 1250×1250 → 1×1 格；INV footprint 交叠 1 格
assert block0.density.cols == 1 and block0.density.rows == 1
assert block0.density.total_count == 1

block1 = design_db.read_object(design_db.block_obj_name(1))  # block_parent
assert block1.instance_total == 4, "placeholder + top1 + top2 + top3(block)"
assert block1.leaf_count == 3 and block1.stats.unplaced_count == 1
# ⑲/⑳ fake cell：WRAP_CELL 未定义 → block_parent::WRAP_CELL（UNPLACED
# 实例照收入表、不入密度）
assert block1.fake_cell_count == 1
names1 = load_block_names(design_db, 1)  # block_parent 名字伴生对象
assert names1.instance_name_by_id(2) == "top2"  # R7 ㊱：名字经 hasher
block1.attach_names(names1)  # 共享注入（此后经 block1 的 hasher 查询）
top2 = block1.get_instance(2)
assert top2.placement_status == 0  # UNPLACED
assert top2.orient == 0, "defi UNPLACED orient −1 must clamp to N"
assert block1.net_count == 1 and block1.net_id_by_name("n_top") == 1
# UNPLACED 不计密度：top1 + top3（block_child footprint）各交叠 1 格
assert block1.density.total_count == 2

# ── S5b：per-DEF 网内容产物（③ 分批责任链 + ⑨ local id 对齐 + ⑩⑫）──
# block_child n1：连接 2 项 + 1 wire（M1 缺省宽 0.07µm×1000=70 回填）+
# 1 via（VIA12 → ⑫ block_child::VIA12 前缀名解析）@ (500,200)
net0 = design_db.read_object(design_db.net_obj_name(0))
assert net0.block_name == "block_child"
assert net0.stats.net_count == 2
assert net0.stats.connection_count == 4 and net0.stats.wire_count == 1
assert net0.stats.via_instance_count == 1
assert net0.stats.skipped_via_count == 0 and net0.stats.skipped_net_count == 0
# 连接表（S7 并查集输入；instance "PIN" = block port 引用）
assert net0.connections_of(1) == [("u1", "A"), ("PIN", "PIN_IN")]
assert net0.connections_of(2) == [("u1", "ZN"), ("PIN", "PIN_OUT")]
wires = net0.wires_of(1)
assert len(wires) == 1, f"n1 wires={wires}"
assert wires[0][0] == 0 and wires[0][1] == 70  # M1 + 缺省宽回填
assert wires[0][2] == [(100, 200), (500, 200)]
assert len(net0.wires_of(2)) == 0, "n2 has no wiring"
# ⑩ via instance：专用 id 空间从 1 起、无 name，仅 via cell id + 位置
assert net0.via_instance_total == 1
via12_id = design.via_cell_id_by_name("block_child::VIA12")
assert via12_id is not None
assert net0.via_instance_at(1) == (via12_id, 500, 200)
# ⑥ 网侧密度逐层分列通道：M1 金属 1 格 + VIA1 通孔 1 格（bin 10000）
assert net0.density.metal_total == 1, f"metal={net0.density.metal_total}"
assert net0.density.via_total == 1, f"via={net0.density.via_total}"
assert net0.density.layer_total(0, False) == 1   # M1 金属
assert net0.density.layer_total(1, True) == 1    # VIA1 通孔

net1 = design_db.read_object(design_db.net_obj_name(1))
assert net1.block_name == "block_parent"
assert net1.connections_of(1) == [("top1", "A"), ("PIN", "TOP_IN")]
assert net1.stats.wire_count == 0 and net1.stats.via_instance_count == 0
INFO("[OK] S5b: net content (connections/wires/via instances, per-layer "
     "density channels, local net id aligned)")

# ── S6：层级树 + 起始编号（⑧⑨⑮；block_parent 实例化 block_child →
# block_parent 为唯一无父者 = 根；def_paths 序 child 在前不影响判定）──
hier = design.get_hier_tree()
assert hier.node_count == 2 and hier.design_name == "block_parent"
root = hier.node(0)
assert root.block_cell_name == "block_parent"
assert root.instance_name == "block_parent"
assert root.self_global_id == 0 and root.parent_id == 0
assert root.instance_range == (0, 4), f"root inst={root.instance_range}"
assert root.net_range == (0, 1)
child = hier.node(1)
assert child.block_cell_name == "block_child"
assert child.instance_name == "top3"
assert child.parent_id == 0 and hier.children(0) == [1]
# ⑧ child 自身 global id = 父块 inst 区间内 top3 的 local 3
assert child.self_global_id == 3, f"self={child.self_global_id}"
assert child.instance_range == (4, 6), f"child inst={child.instance_range}"
assert child.net_range == (1, 3), f"child net={child.net_range}"
# via 区间（S5b 统计计数）：root 0 个 → [0,0)；child 1 个（VIA12）→ [0,1)
assert root.via_range == (0, 0), f"root via={root.via_range}"
assert child.via_range == (0, 1), f"child via={child.via_range}"
# 四接口：区间反查 / parent-children / format_tree
assert hier.block_of_instance(3) == 0, "top3 itself belongs to parent seg"
assert hier.block_of_instance(5) == 1, "child u1 belongs to child seg"
assert hier.block_of_net(2) == 1
assert hier.block_of_via_instance(0) == 1, "child VIA12 via instance"
assert hier.block_of_via_instance(1) is None, "no more via instances"
assert hier.parent(1) == 0
# ⑨ global id 换算：local 0 → 自身（root → 0）；net/via local 从 1 起
assert hier.global_instance_id(0, 0) == 0
assert hier.global_instance_id(1, 0) == 3
assert hier.global_instance_id(1, 1) == 5, "child u1 global id"
assert hier.global_net_id(0, 1) == 0
assert hier.global_net_id(1, 1) == 1
assert hier.global_net_id(1, 2) == 2
assert hier.global_via_instance_id(0, 1) is None, "root has no via"
assert hier.global_via_instance_id(1, 1) == 0, "child VIA12 global id"
assert hier.global_via_instance_id(1, 2) is None
tree_text = hier.format_tree()
assert "block_parent as block_parent" in tree_text
assert "block_child as top3" in tree_text
assert "inst=[4,6)" in tree_text
INFO("[OK] S6: hierarchy tree (DFS numbering, ⑧ local-0 mapping, four "
     "interfaces, name-format text)")

# ── R7 ㊻：EXDSNameMapper 全局 name 组装（统一加载 API → 注入式轻壳，
# 运行时构造不落盘；instance/net 两维度双向闭环）──
# 层级（同上）：root block_parent（inst [0,4) net [0,1)）→ child top3
#（block_child，inst [4,6) net [1,3)）；伴生对象 names0/1 按序注入
design_m, mapper_i = load_name_mapper(design_db, kind=0)  # instance 维度
assert mapper_i.injected_count == 2, f"inject={mapper_i.injected_count}"
# 层级实例路径 → global id（叶实例 + block instance 自身）
assert mapper_i.get_global_id("block_parent/top1") == 1
assert mapper_i.get_global_id("block_parent/top3") == 3, \
    "block instance self global id (⑧)"
assert mapper_i.get_global_id("block_parent/top3/u1") == 5
# global id → 层级实例路径（与正向闭环）
assert mapper_i.get_full_name(3) == "block_parent/top3"
assert mapper_i.get_full_name(5) == "block_parent/top3/u1"
assert mapper_i.get_full_name(1) == "block_parent/top1"
# 未注入/路径断裂 → None（不透出哨兵）
assert mapper_i.get_global_id("block_parent/ghost") is None
assert mapper_i.get_full_name(999) is None
_, mapper_n = load_name_mapper(design_db, kind=1)  # net 维度（区间换算不同）
assert mapper_n.get_global_id("block_parent/n_top") == 0
assert mapper_n.get_global_id("block_parent/top3/n1") == 1
assert mapper_n.get_full_name(2) == "block_parent/top3/n2"
INFO("[OK] R7 name mapper: instance/net both dimensions, bidirectional "
     "close-loop over hierarchy paths (injected lightweight shell)")

# 汇总并入：fake cell 进全局 cell 表（id 保持任务内分配值 = hash 扰动
# 基址 + 递增，⑳；cells_ 稀疏落位含占位空洞 → cell_count >= 5）
assert design.cell_count >= 5, f"cells={design.cell_count} (4 + 1 fake)"
fake_id = design.cell_id_by_name("block_parent::WRAP_CELL")
assert fake_id is not None
fake_cell = design.get_cell(fake_id)
assert fake_cell.is_fake_cell is True
assert fake_cell.width == 1 and fake_cell.height == 1
assert fake_cell.pin_count == 0
assert list(design.fake_cell_ids()) == [fake_id]
# top2 的引用重映射后指向全局 fake id（无冲突时与任务内分配值一致）
assert top2.cell_id == fake_id
INFO("[OK] S5a: instance table (R6 pos/orient), fake cell (⑲/⑳), "
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
for msg_id in ("DSGN::0001", "DSGN::0002", "DSGN::0003", "DSGN::0004",
               "DSGN::0005", "DSGN::0007", "DSGN::0009"):
    assert msg_id in msgs, f"message {msg_id} should be emitted"
INFO("[OK] DSGN messages 0001-0005 + 0009 emitted (warnings/info, no raise)")

# ── ⑧ 统一加载：注入 pin 表/几何后 get_cell 指针注入 ──
from emir.design import load_design_with
loaded = load_design_with(design_db, pin_tables=True, pin_geometries=True)
loaded_inv = loaded.get_cell(inv_id)
assert loaded_inv.get_pin_tables() is not None, "pin tables should be injected"
# R4：表按全局 pin id 检索——lib 中 internal_power 挂在 ZN 上（pin A 无表）
pin_zn_id = loaded.pin_id_by_name("INV_X1", "ZN")
assert pin_zn_id is not None
assert loaded_inv.get_pin_tables().pin_has_tables(pin_zn_id), \
    "pin ZN should have extracted lib tables (keyed by global pin id)"
pin_a_id_tbl = loaded.pin_id_by_name("INV_X1", "A")
assert not loaded_inv.get_pin_tables().pin_has_tables(pin_a_id_tbl), \
    "pin A carries no lib table"
assert loaded_inv.get_pin_geometry() is not None
INFO("[OK] load_design_with: pin tables/geometry injected, keyed by pin id")

# port 几何经全局 pin id 检索（R4/R5：port 几何挂 port 的全局 pin id）
pin_in_id = loaded.pin_id_by_name("block_child", "PIN_IN")
assert pin_in_id is not None
port_geoms = loaded.get_pin_geometry().geometry_of(pin_in_id)
assert len(port_geoms) == 1, f"PIN_IN geoms={port_geoms}"
assert port_geoms[0] == (0, (-5, -10, 15, 20)), f"PIN_IN geom={port_geoms[0]}"
INFO("[OK] port geometry retrievable by global pin id (R4/R5)")

# ── R9 ㊼：wait_obj 依赖传播体系（deps 传播 + run_direct 直跑 + 等待
# 语义兜底；DEVELOPMENT_GUIDELINES Section 17）──
# R9 起 load_*/ds_functions 全部 read_object 类 API 经 @wait_obj 包装声明
# 自身数据依赖——上方全部 load_* 本地直调（数据已就绪）即其正路径回归。
from fly import run_direct
from emir.design import DesignDb, load_design

# deps() 形态：wait_obj inputs lambda 解析结果透传（条件实参条件化，
# 上层 task 的 inputs 传播数据源——防依赖漂移）
deps_with = load_design_with.deps(design_db, pin_tables=True,
                                  pin_geometries=True)
assert deps_with == [
    design_db.get_full_name(DesignDb.DESIGN_OBJ),
    design_db.get_full_name(DesignDb.PIN_TABLES_OBJ),
    design_db.get_full_name(DesignDb.PIN_GEOMETRY_OBJ),
], f"load_design_with.deps={deps_with}"
assert load_design_with.deps(design_db) == [
    design_db.get_full_name(DesignDb.DESIGN_OBJ)], \
    f"conditional deps must follow args: {load_design_with.deps(design_db)}"
assert load_block_names.deps(design_db, 1) == [
    design_db.get_full_name(DesignDb.names_obj_name(1))]
INFO("[OK] R9 deps(): inputs lambda resolution propagates (incl. "
     "conditional pin_tables/pin_geometries)")

# run_direct 形态：task 函数体内剥离本地等待直跑（依赖已声明必然就绪，
# 省 wait_obj 轮询/master 查询冗余网络 IO）——返回值与包装调用一致
direct_design = run_direct(load_design, design_db)
assert direct_design.cell_count >= 4, f"cells={direct_design.cell_count}"
INFO("[OK] R9 run_direct(): wait_obj-wrapped API unwrapped, same result")

# 等待语义兜底（负路径）：lib db（role=lib，无 DSDesign 对象、此时 master
# 无 pending/running 任务可产出）上 load_design → wait_obj 确认无法产出
# → RuntimeError——漏声明依赖/数据缺失场景的明确报错，而非静默错读
try:
    load_design(lib_db)
    raise AssertionError("load_design on lib db must raise (cannot produce)")
except RuntimeError as e:
    assert "cannot be produced" in str(e), str(e)
INFO("[OK] R9 wait_obj bottom line: absent dependency raises "
     "cannot-be-produced instead of silent misread")

# ── R8d：alpha 键 lcp_name_arena（裁定 55：id→name 侧 LCP 后缀共享）──
# 第二个 design db（同一 lef/def 输入）开启 alpha → COMPONENTS 解析任务
# 在 DSBlockNames_<i> 落盘前封口两 hasher（instance/net）为 LCP 形态；
# 读回按标记位自识别。验证 alpha 传递链（build_design_db → 封口点）与
# 封口形态的查询面/层级树组装/全局 mapper 与形态一全同。
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
assert names0_lcp.names_lcp_form is True, \
    "alpha lcp_name_arena must seal DSBlockNames_0 hashers to LCP form"
# 封口形态双向查询与形态一同值（同输入同 id 空间）
assert names0_lcp.instance_id_by_name("u1") == 1
assert names0_lcp.instance_name_by_id(1) == "u1"
assert names0_lcp.net_id_by_name("n1") == 1 and names0_lcp.net_id_by_name("n2") == 2
assert names0_lcp.net_name_by_id(2) == "n2"
names1_lcp = load_block_names(lcp_db, 1)
assert names1_lcp.names_lcp_form is True
assert names1_lcp.instance_name_by_id(2) == "top2"
assert names1_lcp.instance_id_by_name("top2") == 2
# 未登记 → None；封口形态域判别不误判
assert names1_lcp.instance_name_by_id(999) is None
assert names1_lcp.net_id_by_name("ghost") is None
INFO("[OK] R8d LCP name arena: DSBlockNames_<i> sealed, bidirectional "
     "queries identical to form-1")

# 封口形态消费链：层级树组装（实例名反查）+ 全局 mapper（get_full_name
# 走 LCP rank 回溯）与形态一同构
lcp_design = lcp_db.load_design()
hier_lcp = lcp_design.get_hier_tree()
assert hier_lcp.node_count == 2 and hier_lcp.design_name == "block_parent"
_, mapper_lcp = load_name_mapper(lcp_db, kind=0)
assert mapper_lcp.get_global_id("block_parent/top3/u1") == 5
assert mapper_lcp.get_full_name(5) == "block_parent/top3/u1"
_, mapper_lcp_n = load_name_mapper(lcp_db, kind=1)
assert mapper_lcp_n.get_global_id("block_parent/top3/n2") == 2
assert mapper_lcp_n.get_full_name(2) == "block_parent/top3/n2"
INFO("[OK] R8d LCP name arena: hier tree + global mapper closed-loop over "
     "LCP rank backtrack")

# ── load_project 动态还原 ──
import fly
restored = fly.load_project(PROJ_PATH)
assert isinstance(restored, EMIRProject), \
    f"load_project should restore EMIRProject, got {type(restored)}"
assert "build_design_db" in restored.list_flows()
restored_design = restored.get_db("design").load_design()
assert restored_design.cell_count >= 4, "restored design should be readable"
INFO("[OK] load_project restored EMIRProject with design db")

get_agent().stop()
INFO("[PASS] test_emir_project_design")
