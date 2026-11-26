"""E2E test: 真实布局布线设计四库链——designs/ 子模块的 OpenROAD gcd
（Nangate45 0.46ns）设计库 → 寄生库 + 时序库 + 波形库一次串通。

数据来自 git 子模块 designs/（独立仓库——开源流程产出，fly 仅引用；
注意子模块根下还有一层 designs/）。子模块未初始化（任一数据文件不存
在）时本用例在起 worker 前即 raise AssertionError（文案写明缺哪个文
件并提示 `git submodule update --init designs`），不静默跳过。

寄生库的逐网期望不用被测的 C++ 解析器：用例内自带一个朴素独立解析器
（标准库 re——DEF 按「`;` 切语句」、SPEF 按「一行一条记录」读，真实
工具产出满足该形态），从 DEF 与 SPEF 现场算出逐网三值（飞法）与记录
计数，与建库结果逐网核对（相对误差 1e-9）。逐网数值不写死、不落盘
（gcd 设计许可状态不明确——只有从它派生的逐网数值不进仓库）；汇总层
计数的期望值固化在 qa/emir/gold/test_emir_real_gcd.gold（数值与设计数据
仓库的说明里公开的计数一致），并与独立解析合计相互印证。期望值经主
agent 在真实数据上核对（2026-11-25）。

检查面（断言已迁移成记录，由运行器与基准文件比对）：
  - 设计库：校验报告（752 实例 / 508 网 = 506 信号 + VDD/VSS / 单分
    区 / 0 跨区网）+ 电源地网集（power 1 / ground 1——真实布线工具写
    的电源网只有通配连接 `( * VDD )`，解析期展开为每实例一条连接）+
    get_net("VDD"/"VSS") 各 752 条连接全 hybrid 位（电源引脚双向）、
    无驱动者/接收者/端口 + get_net("clk") 驱动 1 / 接收 1 / 端口 1
    （时钟端口条目按网的角色标为驱动者）；
  - 寄生库（本用例核心）：分区 (0,0) 472 条（net id 升序）逐网三值
    == 独立解析（DEF 已布线信号网 472 == SPEF 网段 472 == 覆盖 472，
    三口径交叉印证）+ 逐文件统计（电容记录 8513 其中耦合 6794 / 电阻
    1247 / 切块 1 / *C_UNIT 1 PF → 1000 飞法每单位）+ 全局覆盖（未覆
    盖 0 / 未覆盖实例 0 / 零电容实例 0 / 行首不一致 0 / 多成员电气网
    0——平展设计）+ clk 点查（值 == 独立解析、electrical_members =
    自身单成员）+ VDD/VSS 点查 None（SPEF 不含电源网段）；
  - 时序库（汇总层面——时序库自身有专门的测试）：条目 506 / 命中 472
    / 悬空 34 = 506 − 472 恰为 DEF 里未布线的网 / 各未命中族全 0 +
    分区 (0,0) 时序对象 436 条；
  - 波形库（汇总层面——读仿真器原样输出）：波形 472 / 命中 426 / 事
    件 18661 / 悬空 34 / 网名未命中 0（仿真器转义标识符已处理）/ 其
    余未命中族全 0 + 逐文件时间窗（8.74–80.5 ns、timescale 1 ps）。

单元 LEF 用流程仓库修改版 NangateOpenCellLibrary.macro.mod.lef（含
真实 DEF 用到的填充单元 TAPCELL_X1；不能用同目录不带 TAPCELL_X1 的
NangateOpenCellLibrary.macro.lef）。
"""
import os
import re
import shutil

from log import INFO

from fly import get_config, launch_workers, wait_tasks
from fly.runtime import get_agent
from emir import EMIRProject
from test import record

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TIMING_DATA = os.path.join(SCRIPT_DIR, "data", "timing")
TECH_LEF = os.path.join(TIMING_DATA, "NangateOpenCellLibrary.tech.lef")
MACRO_MOD_LEF = os.path.join(TIMING_DATA,
                             "NangateOpenCellLibrary.macro.mod.lef")
LIBERTY = os.path.join(TIMING_DATA,
                       "NangateOpenCellLibrary_typical.lib")

GCD_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, "..", "..",
                                         "designs", "designs", "gcd",
                                         "nangate45", "0.46ns"))
DEF_PATH = os.path.join(GCD_ROOT, "results", "6_final.def")
SPEF_PATH = os.path.join(GCD_ROOT, "results", "6_final.spef")
TWF_PATH = os.path.join(GCD_ROOT, "twf", "routed_with_spef.twf")
VCD_PATH = os.path.join(GCD_ROOT, "vcd", "gcd_period0.46_raw.vcd")

LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "emir_real_gcd")

# ── 子模块数据与工艺库检查（必须在任何文件读取〔含下方独立解析〕与起
#    worker 之前——未初始化即明确失败，不静默跳过）────────────────────

_MISSING = [p for p in (DEF_PATH, SPEF_PATH, TWF_PATH, VCD_PATH)
            if not os.path.isfile(p)]
# 保留：环境前置检查——子模块未初始化时必须明确失败并提示初始化命令，不静默跳过（AGENTS.md 裁定）
assert not _MISSING, (
    f"designs 子模块数据缺失: {_MISSING}（designs/ 是独立仓库，fly 仅"
    f"引用）—— 请先初始化子模块: git submodule update --init designs")
for _p in (TECH_LEF, MACRO_MOD_LEF, LIBERTY):
    assert os.path.isfile(_p), f"工艺库文件缺失: {_p}"  # 保留：环境前置检查——工艺库文件缺失时用例无法进行

# ── 汇总层期望值（字面量；与 designs/ 子模块说明的公开计数一致——
#    迁移后各计数的期望值固化在 qa/emir/gold/test_emir_real_gcd.gold）──

EXPECT_DEF_PG_NETS = ("VDD", "VSS")  # SPECIALNETS 段电源地网
EXPECT_CAP_UNIT_FF = 1000.0      # *C_UNIT 1 PF → 1 单位 = 1000 飞法
EXPECT_PG_CONNECTIONS = 752      # 每实例一条电源引脚连接（通配展开后）
EXPECT_TIMING_ENTRY = 506        # 时序库条目
EXPECT_TIMING_HIT = 472          # 时序库命中
EXPECT_TIMING_DANGLING = 34      # 悬空网 = 506 − 472 = DEF 未布线网
EXPECT_TIMING_PARTITION_SIZE = 436  # 分区 (0,0) 时序对象条目
EXPECT_WAVEFORM_COUNT = 472      # 波形库波形实体数
EXPECT_WAVEFORM_HIT = 426        # 波形库命中
EXPECT_WAVEFORM_EVENTS = 18661   # 波形库事件总数
REL_TOL = 1e-9                   # 逐网三值相对误差上限


# ── 独立参照解析器（不复用被测 C++ 解析器的任何代码）─────────────────

def _unescape(name):
    """DEF / SPEF 的名字转义还原：反斜杠的下一个字符取字面义。本数据
    集的网名为明文（无转义）——函数幂等，无反斜杠时原样返回。"""
    out = []
    i = 0
    while i < len(name):
        if name[i] == "\\" and i + 1 < len(name):
            out.append(name[i + 1])
            i += 2
        else:
            out.append(name[i])
            i += 1
    return "".join(out)


def _read_def(path):
    """朴素解析 DEF 的 NETS / SPECIALNETS 段（DEF 语句跨行——按 `;` 切
    语句）。返回 (信号网 [(名字, 是否带 + ROUTED)], 电源网名列表)。"""
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().split("\n")
    bodies = {}
    i = 0
    while i < len(lines):
        head = re.match(r"^(NETS|SPECIALNETS)\s+(\d+)\s*;", lines[i])
        if head:
            body = []
            i += 1
            while i < len(lines) and \
                    not lines[i].startswith("END " + head.group(1)):
                body.append(lines[i])
                i += 1
            bodies[head.group(1)] = body
        i += 1
    signal = []
    pg = []
    for kind, body in bodies.items():
        for stmt in "\n".join(body).split(";"):
            stmt = stmt.strip()
            if not stmt.startswith("-"):
                continue
            name = _unescape(stmt.split()[1])
            (pg if kind == "SPECIALNETS" else signal).append(
                (name, "+ ROUTED" in stmt))
    return signal, pg


_C_UNIT_FF = {"FF": 1.0, "PF": 1000.0}


def _read_spef(path):
    """朴素解析 SPEF（一行一记录）：*NAME_MAP 索引 → 名字（去转义）；
    每个 *D_NET/*R_NET 的行首总电容与 *CAP 段两字段（对地）/三字段（耦
    合）记录合计，按 *C_UNIT 档位换算飞法。返回 (逐网三值 dict［名字 →
    (行首, 对地, 耦合)］, 记录计数 dict, 电容单位飞法系数)。"""
    name_map = {}
    nets = {}
    stats = {"net": 0, "rnet": 0, "cap": 0, "coupling": 0, "res": 0}
    cap_unit = None
    cur = None
    section = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.split("//", 1)[0].strip()
            if not line:
                continue
            t = line.split()
            if t[0] == "*C_UNIT":
                cap_unit = float(t[1]) * _C_UNIT_FF[t[2].upper()]
                continue
            if t[0] == "*NAME_MAP":
                section = "NAME_MAP"
                continue
            if section == "NAME_MAP":
                if t[0].startswith("*") and not t[0][1:].isdigit():
                    section = None
                else:
                    assert t[0][1:].isdigit(), line  # 条目必为 *数字  # 保留：独立参照解析器的输入格式前置检查（数据形态破坏时参照值无效，后续核对全部失去意义）
                    name_map[t[0]] = _unescape(t[1])
                    continue
            if t[0] == "*END":
                cur = None
                section = None
                continue
            if t[0] in ("*D_NET", "*R_NET"):
                assert cap_unit is not None, "*C_UNIT must come first"  # 保留：独立参照解析器的输入格式前置检查（同上）
                name = name_map.get(t[1], t[1])
                assert name not in nets, f"SPEF 重复网段: {name}"  # 保留：独立参照解析器的输入格式前置检查（同上）
                cur = [float(t[2]) * cap_unit, 0.0, 0.0]
                nets[name] = cur
                stats["net"] += 1
                stats["rnet"] += t[0] == "*R_NET"
                section = None
                continue
            if t[0] in ("*CONN", "*CAP", "*RES"):
                section = t[0][1:]
                continue
            if t[0].startswith("*"):
                continue
            if section == "CAP":
                assert cur is not None, line  # 保留：独立参照解析器的输入格式前置检查（同上）
                if len(t) == 3:      # 编号 节点 值 —— 对地
                    cur[1] += float(t[2]) * cap_unit
                    stats["cap"] += 1
                elif len(t) == 4:    # 编号 节点 节点 值 —— 耦合（全值）
                    cur[2] += float(t[3]) * cap_unit
                    stats["cap"] += 1
                    stats["coupling"] += 1
                else:
                    raise AssertionError(f"意外的电容记录形态: {line}")
            elif section == "RES":
                assert len(t) == 4, line  # 保留：独立参照解析器的输入格式前置检查（同上）
                stats["res"] += 1
    return nets, stats, cap_unit


# ── 现场独立解析 + 与字面量相互印证 ──────────────────────────────────

REF_NETS, REF_STATS, REF_CAP_UNIT = _read_spef(SPEF_PATH)
DEF_SIGNAL, DEF_PG = _read_def(DEF_PATH)
DEF_ROUTED = [name for name, routed in DEF_SIGNAL if routed]

# 原断言：assert len(DEF_SIGNAL) == EXPECT_DEF_SIGNAL_NETS, \
record("ref/def_signal_count", len(DEF_SIGNAL))
# 原断言：assert len(set(name for name, _ in DEF_SIGNAL)) == len(DEF_SIGNAL), \
record("ref/def_net_names_unique",
       len(set(name for name, _ in DEF_SIGNAL)) == len(DEF_SIGNAL))
# 原断言：assert len(DEF_ROUTED) == EXPECT_DEF_ROUTED_NETS, \
record("ref/def_routed_count", len(DEF_ROUTED))
# 原断言：assert tuple(sorted(name for name, _ in DEF_PG)) == EXPECT_DEF_PG_NETS, \
record("ref/def_pg_names", sorted(name for name, _ in DEF_PG))
# 原断言：assert len(REF_NETS) == EXPECT_SPEF_NETS, \
record("ref/spef_net_count", len(REF_NETS))
# 原断言：assert REF_STATS["cap"] == EXPECT_CAP_RECORDS, \
record("ref/cap_records", REF_STATS["cap"])
# 原断言：assert REF_STATS["coupling"] == EXPECT_COUPLING_RECORDS, \
record("ref/coupling_records", REF_STATS["coupling"])
# 原断言：assert REF_STATS["res"] == EXPECT_RES_RECORDS, \
record("ref/res_records", REF_STATS["res"])
# 原断言：assert REF_STATS["rnet"] == 0, REF_STATS
record("ref/rnet_records", REF_STATS["rnet"])
# 原断言：assert abs(REF_CAP_UNIT - EXPECT_CAP_UNIT_FF) <= \
record("ref/cap_unit_1000ff",
       abs(REF_CAP_UNIT - EXPECT_CAP_UNIT_FF) <= 1e-9 * EXPECT_CAP_UNIT_FF,
       detail=f"{REF_CAP_UNIT!r}")
INFO(f"[DONE] independent parse: {len(DEF_SIGNAL)} DEF nets "
     f"({len(DEF_ROUTED)} routed + {len(DEF_PG)} pg), "
     f"{len(REF_NETS)} SPEF nets, {REF_STATS['cap']} cap records "
     f"({REF_STATS['coupling']} coupling), {REF_STATS['res']} res")


def cleanup():
    if os.path.isdir(PROJ_PATH):
        shutil.rmtree(PROJ_PATH, ignore_errors=True)


cleanup()

launch_workers([{}, {}])
assert get_agent().wait_workers_registered(timeout=60), "workers connect"
INFO("  2 workers connected (user-managed)")

proj = EMIRProject(PROJ_PATH)
for flow in ("build_lib_db", "build_design_db", "build_spef_db",
             "build_timing_db", "build_waveform_db"):
    # 原断言：assert flow in proj.list_flows(), flow
    record(f"flows/{flow}_registered", flow in proj.list_flows(),
           detail=sorted(proj.list_flows()))
INFO("[DONE] four-db flows registered on EMIRProject")

# ── 建库链：lib → design（冻结后）→ spef + timing + waveform 一批 ────

lib_db = proj.build_lib_db(name="gcd_lib", lib_paths=[LIBERTY])
assert proj.wait_frozen("gcd_lib", timeout=1200), "lib db should freeze"
INFO("[WAIT] lib db frozen")

design = proj.build_design_db(
    name="gcd_design", def_paths=[DEF_PATH],
    lef_paths=[TECH_LEF, MACRO_MOD_LEF], lib_db=lib_db)
assert proj.wait_frozen("gcd_design", timeout=1200), \
    "design db should freeze"
INFO("[WAIT] design db frozen")

# 设计库冻结后三个下游库一批提交（互相独立，都只依赖 design db）→
# wait_tasks 等一次 → 逐库确认冻结（同 QA 其余 flow 用例的批量口径，
# 不逐库轮询——钩子 6 路并行配方下的耗时红线）
spef = proj.build_spef_db(name="gcd_spef", spef_files=[SPEF_PATH],
                          design_db=design)
timing = proj.build_timing_db(name="gcd_timing", timing_files=[TWF_PATH],
                              design_db=design)
waveform = proj.build_waveform_db(name="gcd_waveform", design_db=design,
                                  vcd_files=[VCD_PATH])
assert wait_tasks(timeout=1800), "downstream flows complete"
for n in ("gcd_spef", "gcd_timing", "gcd_waveform"):
    assert proj.wait_frozen(n, timeout=60), f"{n} should freeze"
INFO("[WAIT] spef + timing + waveform frozen (3 db(s), one batch)")

# ═══ 设计库：校验报告 + 电源网通配展开 + 连接概要 ═════════════════════

from emir.design import (  # noqa: E402  设计库读库（校验报告 + pg 集）
    load_design_pg_nets,
    load_design_verify_report,
)

report = load_design_verify_report(design)
# 原断言：assert report.total_instances == EXPECT_VERIFY_INSTANCES, \
record("design/total_instances", report.total_instances)
# 原断言：assert report.total_nets == EXPECT_VERIFY_NETS, \
record("design/total_nets", report.total_nets)
# 原断言：assert report.partition_count == 1, report.partition_count
record("design/partition_count", report.partition_count)
# 原断言：assert report.total_crossing_nets == 0, report.total_crossing_nets
record("design/total_crossing_nets", report.total_crossing_nets)
INFO("[DONE] design verify report: 752 instances / 508 nets / "
     "1 partition / 0 crossing nets")

pg_nets = load_design_pg_nets(design)
# 原断言：assert pg_nets.power_count == 1, pg_nets.power_count
record("design/pg_power_count", pg_nets.power_count)
# 原断言：assert pg_nets.ground_count == 1, pg_nets.ground_count
record("design/pg_ground_count", pg_nets.ground_count)
INFO("[DONE] design pg nets: 1 power (VDD) + 1 ground (VSS)")

# 真实布线工具写的电源网只有通配连接 `( * VDD )`——设计库解析期展开为
# 每实例一条电源引脚连接；电源引脚双向（全 hybrid 位）、无端口条目
for name in EXPECT_DEF_PG_NETS:
    net = design.get_net(name)
    # 原断言：assert net is not None, name
    record(f"design/pg_{name}_exists", net is not None, detail=name)
    assert net is not None  # 保留：守卫（后面 net["connection_count"] 等解引用）
    # 原断言：assert net["connection_count"] == EXPECT_PG_CONNECTIONS, \
    record(f"design/pg_{name}_connection_count", net["connection_count"])
    # 原断言：assert net["hybrid_count"] == EXPECT_PG_CONNECTIONS, \
    record(f"design/pg_{name}_hybrid_count", net["hybrid_count"])
    # 原断言：assert net["driver_count"] == 0, (name, net["driver_count"])
    record(f"design/pg_{name}_driver_count", net["driver_count"])
    # 原断言：assert net["receiver_count"] == 0, (name, net["receiver_count"])
    record(f"design/pg_{name}_receiver_count", net["receiver_count"])
    # 原断言：assert net["port_count"] == 0, (name, net["port_count"])
    record(f"design/pg_{name}_port_count", net["port_count"])
INFO(f"[DONE] design VDD/VSS: {EXPECT_PG_CONNECTIONS} wildcard-expanded "
     f"connections each (all hybrid, no ports)")

clk = design.get_net("clk")
# 原断言：assert clk is not None, "clk"
record("design/clk_exists", clk is not None)
assert clk is not None  # 保留：守卫（后面 clk["driver_count"] 等解引用）
# 原断言：assert clk["driver_count"] == 1, clk["driver_count"]
record("design/clk_driver_count", clk["driver_count"])
# 原断言：assert clk["receiver_count"] == 1, clk["receiver_count"]
record("design/clk_receiver_count", clk["receiver_count"])
# 原断言：assert clk["port_count"] == 1, clk["port_count"]
record("design/clk_port_count", clk["port_count"])
INFO("[DONE] design clk: 1 driver (clock port) + 1 receiver")

# ═══ 寄生库：逐网独立参照核对（本用例的核心）══════════════════════════

from emir.spef import (  # noqa: E402  spef 读库 API（模块函数层）
    iter_spef_partition,
    load_partition_net_load,
    load_spef_summary,
)


def _close(got, want):
    """浮点对照判定（相对误差 REL_TOL；零值附近退化为绝对 1e-9 飞法——远
    小于任何记录精度）。返回是否满足容差。"""
    return abs(got - want) <= REL_TOL * max(abs(want), 1.0)


spef_summary = load_spef_summary(spef)
# 原断言：assert len(spef_summary.files) == 1, spef_summary.files
record("spef/files_count", len(spef_summary.files))
assert len(spef_summary.files) >= 1  # 保留：守卫（后面按下标取 spef_summary.files[0]，越界在 C++ 侧是未定义行为）
f0 = spef_summary.files[0]
# 原断言：assert f0.source_file.endswith("6_final.spef"), f0.source_file
record("spef/f0_source_suffix", f0.source_file.endswith("6_final.spef"),
       detail=f0.source_file)
# 逐文件统计：字面量与独立解析合计双口径交叉
# 原断言：assert f0.net_count == len(REF_NETS) == EXPECT_SPEF_NETS, \
record("spef/f0_net_count", f0.net_count)
record("spef/f0_net_count_matches_ref", f0.net_count == len(REF_NETS),
       detail=(f0.net_count, len(REF_NETS)))
# 原断言：assert f0.hit_count == EXPECT_SPEF_NETS, f0.hit_count
record("spef/f0_hit_count", f0.hit_count)
# 原断言：assert f0.net_name_miss_count == 0, f0.net_name_miss_count
record("spef/f0_net_name_miss_count", f0.net_name_miss_count)
# 原断言：assert f0.broken_net_count == 0, f0.broken_net_count
record("spef/f0_broken_net_count", f0.broken_net_count)
# 原断言：assert f0.rnet_count == REF_STATS["rnet"] == 0, f0.rnet_count
record("spef/f0_rnet_count", f0.rnet_count)
record("spef/f0_rnet_count_matches_ref", f0.rnet_count == REF_STATS["rnet"],
       detail=(f0.rnet_count, REF_STATS["rnet"]))
# 原断言：assert f0.cap_count == REF_STATS["cap"] == EXPECT_CAP_RECORDS, \
record("spef/f0_cap_count", f0.cap_count)
record("spef/f0_cap_count_matches_ref", f0.cap_count == REF_STATS["cap"],
       detail=(f0.cap_count, REF_STATS["cap"]))
# 原断言：assert f0.coupling_cap_count == REF_STATS["coupling"] == \
record("spef/f0_coupling_cap_count", f0.coupling_cap_count)
record("spef/f0_coupling_matches_ref",
       f0.coupling_cap_count == REF_STATS["coupling"],
       detail=(f0.coupling_cap_count, REF_STATS["coupling"]))
# 原断言：assert f0.res_count == REF_STATS["res"] == EXPECT_RES_RECORDS, \
record("spef/f0_res_count", f0.res_count)
record("spef/f0_res_count_matches_ref", f0.res_count == REF_STATS["res"],
       detail=(f0.res_count, REF_STATS["res"]))
# 原断言：assert f0.chunk_count == 1, f0.chunk_count
record("spef/f0_chunk_count", f0.chunk_count)
# 原断言：assert f0.pg_net_skip_count == 0, f0.pg_net_skip_count
record("spef/f0_pg_net_skip_count", f0.pg_net_skip_count)
# 原断言：assert f0.unrouted_net_count == 0, f0.unrouted_net_count
record("spef/f0_unrouted_net_count", f0.unrouted_net_count)
# 原断言：assert f0.failed_chunk_count == 0, f0.failed_chunk_count
record("spef/f0_failed_chunk_count", f0.failed_chunk_count)
# 原断言：assert abs(f0.c_unit_ff_per_unit - EXPECT_CAP_UNIT_FF) <= \
record("spef/f0_c_unit_1000ff",
       abs(f0.c_unit_ff_per_unit - EXPECT_CAP_UNIT_FF)
       <= 1e-9 * EXPECT_CAP_UNIT_FF, detail=f"{f0.c_unit_ff_per_unit!r}")
INFO("[DONE] spef file stats: 472 nets hit, 8513 cap records (6794 "
     "coupling), 1247 res, 1 chunk, *C_UNIT 1 PF = 1000 fF")

# 全局覆盖（平展设计单分区——绝对数即全局数）
# 原断言：assert spef_summary.routed_signal_net_count == len(DEF_ROUTED) == \
record("spef/routed_signal_net_count", spef_summary.routed_signal_net_count)
record("spef/routed_matches_def",
       spef_summary.routed_signal_net_count == len(DEF_ROUTED),
       detail=(spef_summary.routed_signal_net_count, len(DEF_ROUTED)))
# 原断言：assert spef_summary.covered_net_count == EXPECT_SPEF_NETS, \
record("spef/covered_net_count", spef_summary.covered_net_count)
# 原断言：assert spef_summary.uncovered_net_count == 0, spef_summary.uncovered_net_count
record("spef/uncovered_net_count", spef_summary.uncovered_net_count)
# 原断言：assert spef_summary.uncovered_instance_count == 0, \
record("spef/uncovered_instance_count", spef_summary.uncovered_instance_count)
# 原断言：assert spef_summary.zero_cap_instance_count == 0, \
record("spef/zero_cap_instance_count", spef_summary.zero_cap_instance_count)
# 原断言：assert spef_summary.cap_mismatch_count == 0, spef_summary.cap_mismatch_count
record("spef/cap_mismatch_count", spef_summary.cap_mismatch_count)
# 原断言：assert spef_summary.rnet_count == 0, spef_summary.rnet_count
record("spef/summary_rnet_count", spef_summary.rnet_count)
# 原断言：assert spef_summary.electrical_net_count == 0, \
record("spef/electrical_net_count", spef_summary.electrical_net_count)
# 原断言：assert spef_summary.invalid_binding_count == 0, \
record("spef/invalid_binding_count", spef_summary.invalid_binding_count)
INFO("[DONE] spef global coverage: routed 472 / covered 472 / uncovered "
     "0 / mismatch 0 / multi-member electrical 0")

# 分区：单分区 (0,0)，472 条按 net id 升序，逐网三值 == 独立解析
# 原断言：assert list(iter_spef_partition(spef)) == [(0, 0)], \
record("spef/partition_list", list(iter_spef_partition(spef)))
part_obj = load_partition_net_load(spef, 0, 0)
# 原断言：assert part_obj.size == EXPECT_SPEF_NETS, part_obj.size
record("spef/partition_size", part_obj.size)
entries = list(part_obj.items)
entry_ids = [e.net_id for e in entries]
# 原断言：assert entry_ids == sorted(entry_ids), "entries must be net-id ascending"
record("spef/entries_net_id_ascending", entry_ids == sorted(entry_ids))

# 名字 ↔ 全局 id：472 个 SPEF 网名在 design db 全部可解析且 id 唯一
name_by_id = {}
unresolved_names = []
duplicate_ids = []
for name in REF_NETS:
    gid = design.convert_to_id(net=name)
    if gid is None:
        unresolved_names.append(name)
    elif gid in name_by_id:
        duplicate_ids.append(name)
    else:
        name_by_id[gid] = name
# 原断言：assert gid is not None, f"design db must resolve spef net {name}"
record("spef/names_all_resolved", not unresolved_names,
       detail=unresolved_names)
# 原断言：assert gid not in name_by_id, f"duplicate design net id for {name}"
record("spef/net_ids_unique", not duplicate_ids, detail=duplicate_ids)
# 原断言：assert len(name_by_id) == EXPECT_SPEF_NETS, len(name_by_id)
record("spef/name_map_size", len(name_by_id))

missing_net_ids = []
cap_mismatch = []
mask_mismatch = []
reduced_flags = []
for e in entries:
    name = name_by_id.get(e.net_id)
    if name is None:
        missing_net_ids.append(e.net_id)
        continue
    total, ground, coupling = REF_NETS[name]
    if not _close(e.total_cap_reported_ff, total):
        cap_mismatch.append(
            (name, "total", f"{e.total_cap_reported_ff!r} vs {total!r}"))
    if not _close(e.ground_cap_ff, ground):
        cap_mismatch.append(
            (name, "ground", f"{e.ground_cap_ff!r} vs {ground!r}"))
    if not _close(e.coupling_cap_ff, coupling):
        cap_mismatch.append(
            (name, "coupling", f"{e.coupling_cap_ff!r} vs {coupling!r}"))
    if e.source_mask != 1:
        mask_mismatch.append((name, e.source_mask))
    if (e.flags & 0x1) != 0:
        reduced_flags.append((name, e.flags))
# 原断言：assert name is not None, f"unexpected net id {e.net_id}"
record("spef/per_net_names_resolved", not missing_net_ids,
       detail=missing_net_ids)
# 原断言：assert abs(got - want) <= REL_TOL * max(abs(want), 1.0), \
record("spef/per_net_cap_matches_ref", not cap_mismatch, detail=cap_mismatch)
# 原断言：assert e.source_mask == 1, (name, e.source_mask)
record("spef/per_net_source_mask_1", not mask_mismatch, detail=mask_mismatch)
# 原断言：assert (e.flags & 0x1) == 0, (name, e.flags)
record("spef/per_net_no_reduced_flag", not reduced_flags,  # 缩减位（无 *R_NET）
       detail=reduced_flags)
INFO(f"[DONE] spef: all {part_obj.size} nets match independent parse "
     f"(rel {REL_TOL:g}), net-id ascending, no reduced flags")

# 点查：clk 三值 == 独立解析（期望从独立解析现场算出，不写字面量）；
# 电气网单成员（自身）；电源网不在 SPEF（网段 0 条）→ None
rec = spef.get_net_load("clk")
# 原断言：assert rec is not None, "clk"
record("spef/clk_load_exists", rec is not None)
assert rec is not None  # 保留：守卫（后面 rec["total_cap_reported_ff"] 等解引用）
clk_id = design.convert_to_id(net="clk")
clk_total, clk_ground, clk_coupling = REF_NETS["clk"]
# 原断言：assert abs(got - want) <= REL_TOL * max(abs(want), 1.0), \
record("spef/clk_total_matches_ref",
       _close(rec["total_cap_reported_ff"], clk_total),
       detail=(f"{rec['total_cap_reported_ff']!r}", f"{clk_total!r}"))
# 原断言：assert abs(got - want) <= REL_TOL * max(abs(want), 1.0), \
record("spef/clk_ground_matches_ref",
       _close(rec["ground_cap_ff"], clk_ground),
       detail=(f"{rec['ground_cap_ff']!r}", f"{clk_ground!r}"))
# 原断言：assert abs(got - want) <= REL_TOL * max(abs(want), 1.0), \
record("spef/clk_coupling_matches_ref",
       _close(rec["coupling_cap_ff"], clk_coupling),
       detail=(f"{rec['coupling_cap_ff']!r}", f"{clk_coupling!r}"))
# 原断言：assert rec["net_id"] == clk_id, (rec["net_id"], clk_id)
record("spef/clk_net_id_matches", rec["net_id"] == clk_id,
       detail=(rec["net_id"], clk_id))
# 原断言：assert rec["electrical_members"] == [clk_id], rec["electrical_members"]
record("spef/clk_electrical_members_self",
       rec["electrical_members"] == [clk_id],
       detail=rec["electrical_members"])
# 原断言：assert rec["partitions"] == [(0, 0)], rec["partitions"]
record("spef/clk_partitions", rec["partitions"])
# 原断言：assert spef.get_net_load("VDD") is None, "VDD must be absent"
record("spef/vdd_load_absent", spef.get_net_load("VDD"))
# 原断言：assert spef.get_net_load("VSS") is None, "VSS must be absent"
record("spef/vss_load_absent", spef.get_net_load("VSS"))
INFO("[DONE] spef clk point query = independent parse, "
     "single-member electrical net; VDD/VSS absent")

# ═══ 时序库（汇总层面；时序库自身有专门的测试）════════════════════════

from emir.timing import (  # noqa: E402  timing 读库 API（模块函数层）
    iter_timing_partition,
    load_partition_timing,
    load_timing_summary,
)

timing_summary = load_timing_summary(timing)
# 原断言：assert timing_summary.total_entry_count == EXPECT_TIMING_ENTRY, \
record("timing/total_entry_count", timing_summary.total_entry_count)
# 原断言：assert timing_summary.total_hit_count == EXPECT_TIMING_HIT, \
record("timing/total_hit_count", timing_summary.total_hit_count)
# 原断言：assert timing_summary.dangling_net_count == EXPECT_TIMING_DANGLING == \
record("timing/dangling_net_count", timing_summary.dangling_net_count)
record("timing/dangling_matches_unrouted",
       timing_summary.dangling_net_count
       == len(DEF_SIGNAL) - len(DEF_ROUTED),
       detail=(timing_summary.dangling_net_count,
               len(DEF_SIGNAL) - len(DEF_ROUTED)))
# 原断言：assert timing_summary.multi_driver_net_count == 0, \
record("timing/multi_driver_net_count", timing_summary.multi_driver_net_count)
# 原断言：assert timing_summary.unplaced_instance_count == 0, \
record("timing/unplaced_instance_count", timing_summary.unplaced_instance_count)
# 原断言：assert timing_summary.net_name_miss_count == 0, \
record("timing/net_name_miss_count", timing_summary.net_name_miss_count)
# 原断言：assert timing_summary.clock_conflict_count == 0, \
record("timing/clock_conflict_count", timing_summary.clock_conflict_count)
# 原断言：assert timing_summary.missing_clock_count == 0, \
record("timing/missing_clock_count", timing_summary.missing_clock_count)
# 原断言：assert list(iter_timing_partition(timing)) == [(0, 0)], \
record("timing/partition_list", list(iter_timing_partition(timing)))
# 原断言：assert load_partition_timing(timing, 0, 0).size == \
record("timing/partition_size", load_partition_timing(timing, 0, 0).size)
INFO(f"[DONE] timing: {EXPECT_TIMING_ENTRY} entries / "
     f"{EXPECT_TIMING_HIT} hits / {EXPECT_TIMING_DANGLING} dangling "
     f"(unrouted nets), partition (0,0) size "
     f"{EXPECT_TIMING_PARTITION_SIZE}")

# ═══ 波形库（汇总层面；读仿真器原样输出）══════════════════════════════

from emir.waveform import load_waveform_summary  # noqa: E402

wave_summary = load_waveform_summary(waveform)
# 原断言：assert wave_summary.waveform_count == EXPECT_WAVEFORM_COUNT, \
record("wave/waveform_count", wave_summary.waveform_count)
# 原断言：assert wave_summary.total_hit_count == EXPECT_WAVEFORM_HIT, \
record("wave/total_hit_count", wave_summary.total_hit_count)
# 原断言：assert wave_summary.total_event_count == EXPECT_WAVEFORM_EVENTS, \
record("wave/total_event_count", wave_summary.total_event_count)
# 原断言：assert wave_summary.dangling_net_count == EXPECT_TIMING_DANGLING, \
record("wave/dangling_net_count", wave_summary.dangling_net_count)
# 原断言：assert wave_summary.net_name_miss_count == 0, \
record("wave/net_name_miss_count", wave_summary.net_name_miss_count)
# 原断言：assert wave_summary.inst_pin_miss_count == 0, \
record("wave/inst_pin_miss_count", wave_summary.inst_pin_miss_count)
# 原断言：assert wave_summary.unplaced_instance_count == 0, \
record("wave/unplaced_instance_count", wave_summary.unplaced_instance_count)
# 原断言：assert wave_summary.multi_driver_net_count == 0, \
record("wave/multi_driver_net_count", wave_summary.multi_driver_net_count)
# 原断言：assert len(wave_summary.files) == 1, wave_summary.files
record("wave/files_count", len(wave_summary.files))
assert len(wave_summary.files) >= 1  # 保留：守卫（后面按下标取 wave_summary.files[0]，越界在 C++ 侧是未定义行为）
# 原断言：assert wave_summary.files[0].source_file.endswith(
record("wave/source_suffix",
       wave_summary.files[0].source_file.endswith("gcd_period0.46_raw.vcd"),
       detail=wave_summary.files[0].source_file)
# 原断言：assert wave_summary.files[0].t_start_fs == EXPECT_WAVEFORM_T_START_FS, \
record("wave/t_start_fs", wave_summary.files[0].t_start_fs)
# 原断言：assert wave_summary.files[0].t_end_fs == EXPECT_WAVEFORM_T_END_FS, \
record("wave/t_end_fs", wave_summary.files[0].t_end_fs)
# 原断言：assert abs(wave_summary.files[0].timescale_sec - 1e-12) <= 1e-18, \
record("wave/timescale_1ps",
       abs(wave_summary.files[0].timescale_sec - 1e-12) <= 1e-18,
       detail=f"{wave_summary.files[0].timescale_sec!r}")
INFO(f"[DONE] waveform: {EXPECT_WAVEFORM_COUNT} waveforms / "
     f"{EXPECT_WAVEFORM_HIT} hits / {EXPECT_WAVEFORM_EVENTS} events / "
     f"{EXPECT_TIMING_DANGLING} dangling, window 8.74–80.5 ns @ 1 ps")

INFO("[DONE] test_emir_real_gcd")
