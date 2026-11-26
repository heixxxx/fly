"""E2E test: EMIRProject.build_waveform_db — ⑧ waveform db 全链
（vcd db 与 switching db 合并更名后的波形数据库；plan
docs/emir/waveform-db-plan.md，2026-10-12 全裁定）。

验证（VCD 解析 → 名字换算为 design db 全局 id → 分区双容器落库 → 冻结
→ 读库），断言依据 qa/emir/data/waveform/README.md 推导表（§11.3 矩阵
逐 case）：
  - 基础建库（small.vcd）：双容器产出 + README 推导表逐引脚精确断言
    （T0/T1/TX/TZ/rise/fall）+ 波形去重（同网连接位共享 waveform_id、
    去重后实体数 = 引用数〔事件不复制〕）+ debug get_activity/get_waveform
    点查与导出视图 + 全分区双对象（消费接口恒定）；
  - 空分区空对象（design alpha partition_count=16 强制多分区）：无条目
    分区双对象照常产出且为空；
  - 多文件场景 a（part_a1+part_a2）：统计直和 + 偏移后全局单调 + summary
    逐文件统计（分段转储样张分属两个 DESIGN 名——按 README「按 DESIGN
    名前缀剥离后共用」口径，case 内派生改名副本共用 wf_small_a1.def）；
  - 多文件场景 b（block_b1+block_b2，绑定 block_inst="u_blk_a" +
    strip_prefix="u_blk_a"）：重合条目后文件覆盖 + WFMG::0006 计数 1；
  - x/z 统计（xz.vcd）：TX/TZ 非零精确断言 + rise/fall 不含 x 参与转换
    + 概率两口径（denominator_include / exclude）消费侧换算验证；
  - inst pin 直配（instpin.vcd）：形态②命中路径 + 与场景 b 形态①网统
    计交叉一致；
  - settings-only 模式：无 VCD——每分区 WAVEFORM 空对象 + ACTIVITY 物
    化 overrides（net 名形态①展开 + 实例引脚形态②直配）+ 全哨兵
    waveform_id + default 元数据落 summary + 未解析条目 WFMG::0001；
  - 混合模式（裁定 IV-2）：small.vcd + settings.activity.overrides 并
    存——覆盖条目统计 == 用户值换算（形态②直配 + 形态①网名展开）、
    未覆盖条目 == 实测、waveform_id 保留、summary 覆盖条目计数、未解
    析条目 WFMG::0001 聚合口径；
  - 大规模（big.vcd）：2 万实例 + 分区数下限（≥2）+ 命名点查 + 建库
    完成冻结；
  - 确定性：waveform_id 分配序 = 身份键全序（net id 升序逐对断言）+
    同输入双跑 db 对象逐字节一致（summary + 双容器 __getstate__）；
  - gzip 压缩输入（裁定 IV-1）：small.vcd.gz 建库成功且与明文版逐项
    一致（双容器逐字节 + summary 统计字段；source_file 记录解压副本
    路径属预期差异），解压副本冻结前清理；破损 VCD 文件入口任务失败不
    冻结；vcd_files=None 且无 settings.activity 入口 raise（全部文件失
    败 fatal 码 80 的子进程断言在模块单测
    src/emir/waveform/tests/test_wf_flow_error.py——同 timing db 先例）。
"""
import gzip
import os
import shutil

from log import INFO

from fly import get_config, launch_workers, wait_tasks
from fly.runtime import get_agent
from emir import EMIRProject

from test import qa_tmp, record

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(SCRIPT_DIR, "data", "waveform")
DESIGN_DATA = os.path.join(DATA, "design")
TIMING_DATA = os.path.join(SCRIPT_DIR, "data", "timing")
TECH_LEF = os.path.join(TIMING_DATA, "NangateOpenCellLibrary.tech.lef")
MACRO_LEF = os.path.join(TIMING_DATA,
                         "NangateOpenCellLibrary.macro.lef")
LIBERTY = os.path.join(TIMING_DATA,
                       "NangateOpenCellLibrary_typical.lib")

LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "emir_waveform_flow")

NS = 1_000_000  # fs 每 ns（样张全部 1ns 档）

from emir.waveform import (  # noqa: E402  读库 API（模块函数层）
    iter_waveform_partition,
    load_partition_activity,
    load_partition_waveform,
    load_waveform_summary,
)
from emir.design import DesignDb  # noqa: E402


def cleanup():
    if os.path.isdir(PROJ_PATH):
        shutil.rmtree(PROJ_PATH, ignore_errors=True)


cleanup()

launch_workers([{}, {}])
assert get_agent().wait_workers_registered(timeout=60), "workers connect"
INFO("  2 workers connected (user-managed)")

proj = EMIRProject(PROJ_PATH)
# 原断言：assert "build_waveform_db" in proj.list_flows()
record("flows/waveform_registered", "build_waveform_db" in proj.list_flows(),
       detail=sorted(proj.list_flows()))
INFO("[DONE] build_waveform_db registered on EMIRProject")

# ── 前置：lib db + design db 族（直接前驱，显式传参）─────────────────
lib_db = proj.build_lib_db(name="wf_lib", lib_paths=[LIBERTY])
assert proj.wait_frozen("wf_lib", timeout=1200), "lib db should freeze"
INFO("[WAIT] lib db frozen")


def _routed_def(def_name):
    """派生带 ROUTED 几何的 DEF 副本（qa_tmp scratch；每网补一段 metal1
    布线）。design db 的分区 NETS 对象跟随网几何副本（未布线网不计入
    ——timing db TIMG::0003 同款合法兜底口径），共享数据 DEF 全部无布
    线；形态①net 名连接位展开 + 波形去重（裁定 4）的 e2e 载体由此派生
    出网几何，激励与连接关系零改动。"""
    src = os.path.join(DESIGN_DATA, def_name)
    out_dir = qa_tmp("waveform_routed_def")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, def_name)
    with open(src) as f:
        text = f.read()
    lines = []
    for line in text.splitlines():
        stripped = line.rstrip()
        compact = stripped.strip()
        if (compact.startswith("- ") and compact.endswith(") ;")
                and "+ ROUTED" not in compact):
            # net 语句尾补单段 metal1 布线（DEF NETS 语句 ROUTED 子句；
            # net 语句恒以 `- 名 ( 连接 )* ;` 形态起止——COMPONENTS 行以
            # `N ;` 结尾、PINS 行跨行以 `N ;` 结尾，均不匹配本形态）
            stripped = compact[:-1].rstrip() + \
                " + ROUTED metal1 ( 1000 4000 ) ( 2000 4000 ) ;"
        lines.append(stripped)
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")
    return out


ROUTED_SMALL_DEF = _routed_def("wf_small.def")


def _submit_design(name, def_paths, **alpha):
    """提交 design db 建库（异步——提交即返回句柄，不逐库等待冻结）。"""
    kwargs = {}
    if alpha:
        kwargs["alpha"] = alpha
    return proj.build_design_db(
        name=name,
        def_paths=def_paths,
        lef_paths=[TECH_LEF, MACRO_LEF],
        lib_db=lib_db, **kwargs)


def _design_def(name):
    """qa/emir/data/waveform/design/<name> 的绝对路径。"""
    return os.path.join(DESIGN_DATA, name)


def _wait_dbs(*names, timeout=1800):
    """一批建库统一等待（同 QA 其余 flow 用例的 wait_tasks 口径，不逐库
    轮询）：先阻塞等全部已提交任务完成——freeze 是任务链末环，任务排空
    即各库已冻结；再逐库确认冻结（已冻结即返回，保留 wait_frozen 的失败
    信号即时返回语义）。

    逐库「提交→wait_frozen」每次至少等满一个轮询周期（缺省 0.5s）：本
    case 22 次串行建库光轮询即占约 11s，推送钩子的 6 路并行配方（单 case
    20s 上限）实测超时（2026-11-23，超时瞬间栈停在 wait_frozen 的 sleep
    上）。"""
    assert wait_tasks(timeout=timeout), "submitted flows must complete"
    for n in names:
        assert proj.wait_frozen(n, timeout=60), f"{n} should freeze"


# design db 族一批提交（互相独立）→ 一次等待全部任务完成。design_xz2 =
# 场景 4 概率两口径的对照库（x_z_handling 属 waveform db 设置，design 侧
# 同一 DEF 另建一库）
design_small = _submit_design("wf_design_small", [ROUTED_SMALL_DEF])
design_grid = _submit_design("wf_design_grid", [ROUTED_SMALL_DEF],
                             partition_count=16)
design_blk = _submit_design(
    "wf_design_blk",
    [_routed_def(n) for n in ("wf_sub.def", "wf_blk_a.def",
                              "wf_blk_b.def", "wf_blk_top.def")])
design_xz = _submit_design("wf_design_xz", [_design_def("wf_xz.def")])
design_a1 = _submit_design("wf_design_a1",
                           [_design_def("wf_small_a1.def")])
design_big = _submit_design("wf_design_big", [_design_def("wf_big.def")],
                            partition_count=4)
design_xz2 = _submit_design("wf_design_xz2", [_design_def("wf_xz.def")])
_wait_dbs("wf_design_small", "wf_design_grid", "wf_design_blk",
          "wf_design_xz", "wf_design_a1", "wf_design_big",
          "wf_design_xz2")
INFO("[WAIT] design db family frozen (7 db(s), one batch)")

# waveform db 族第一批：场景 1-7 与确定性对照共 11 库互相独立——一批提交、
# 一次等待。混合模式库（场景 8b）留到第二批：场景 6 的「恰 1 条未解析
# 覆盖」断言读进程级 message.log，须在无其它 settings 覆盖库运行时判定。
#
# 分段样张分属 wf_small_a1/wf_small_a2 两个 DESIGN 名（README：两文件各
# 自绑各 DEF 或按 DESIGN 名前缀剥离后共用——flow 侧一 db 一 design，按
# 后者口径在 case 内派生「根 scope 改名为 wf_small_a1」的 a2 副本，激励
# 与事件零改动）
A2_DIR = qa_tmp("waveform_scenario_a")
os.makedirs(A2_DIR, exist_ok=True)
A2_RENAMED = os.path.join(A2_DIR, "part_a2_a1.vcd")
with open(os.path.join(DATA, "part_a2.vcd")) as f:
    text = f.read()
# 原断言：assert "wf_small_a2" in text
record("derived/a2_source_has_design_name", "wf_small_a2" in text)
with open(A2_RENAMED, "w") as f:
    f.write(text.replace("wf_small_a2", "wf_small_a1"))

BLK_INST = "u_blk_a"
STRIP = "u_blk_a"

wf_small = proj.build_waveform_db(
    name="wf_small",
    vcd_files=[os.path.join(DATA, "small.vcd")],
    design_db=design_small)
wf_grid = proj.build_waveform_db(
    name="wf_grid",
    vcd_files=[os.path.join(DATA, "small.vcd")],
    design_db=design_grid)
wf_a = proj.build_waveform_db(
    name="wf_a",
    vcd_files=[os.path.join(DATA, "part_a1.vcd"), A2_RENAMED],
    design_db=design_a1)
wf_b = proj.build_waveform_db(
    name="wf_b",
    vcd_files=[
        {"file_name": os.path.join(DATA, "block_b1.vcd"),
         "block_inst": BLK_INST, "strip_prefix": STRIP},
        {"file_name": os.path.join(DATA, "block_b2.vcd"),
         "block_inst": BLK_INST, "strip_prefix": STRIP},
    ],
    design_db=design_blk)
wf_xz = proj.build_waveform_db(
    name="wf_xz",
    vcd_files=[os.path.join(DATA, "xz.vcd")],
    design_db=design_xz)
wf_xz2 = proj.build_waveform_db(
    name="wf_xz2",
    vcd_files=[os.path.join(DATA, "xz.vcd")],
    design_db=design_xz2,
    settings={"x_z_handling": "exclude"})
wf_ip = proj.build_waveform_db(
    name="wf_instpin",
    vcd_files=[os.path.join(DATA, "instpin.vcd")],
    design_db=design_blk)
wf_b1 = proj.build_waveform_db(
    name="wf_b1",
    vcd_files=[{"file_name": os.path.join(DATA, "block_b1.vcd"),
                "block_inst": BLK_INST, "strip_prefix": STRIP}],
    design_db=design_blk)
wf_so = proj.build_waveform_db(
    name="wf_settings_only",
    vcd_files=None,
    design_db=design_grid,
    settings={
        "activity": {
            "default": {"toggle_rate": 2.0e6, "static_probability": 0.4},
            "duration": 2.0,
            "overrides": {
                "u_cnt0/Q": {"toggle_rate": 1.0e6,
                             "static_probability": 0.3},  # 形态②直配
                "ghost/x": {},                        # 未解析（0001 观测）
            },
        },
        "x_z_handling": "exclude",
    })
wf_big = proj.build_waveform_db(
    name="wf_big",
    vcd_files=[os.path.join(DATA, "big.vcd")],
    design_db=design_big)
wf_small2 = proj.build_waveform_db(
    name="wf_small_rerun",
    vcd_files=[os.path.join(DATA, "small.vcd")],
    design_db=design_small)
_wait_dbs("wf_small", "wf_grid", "wf_a", "wf_b", "wf_xz", "wf_xz2",
          "wf_instpin", "wf_b1", "wf_settings_only", "wf_big",
          "wf_small_rerun")
INFO("[WAIT] waveform db batch 1 frozen (11 db(s), one batch)")


# ── 读库断言辅助 ─────────────────────────────────────────────────────

def _all_partitions(db):
    """全部分区 (xp, yp)（行主序）。"""
    return iter_waveform_partition(db)


def _activity_map(db):
    """全分区 ACTIVITY 合并视图：{inst_id: {pin_id: EXWFPinActivity}}。"""
    result = {}
    for xp, yp in _all_partitions(db):
        act = load_partition_activity(db, xp, yp)
        for iid in act.instance_ids:
            inst = act.instance_at(iid)
            result[iid] = {p.pin_id: p for p in inst.pins}
    return result


def _waveform_container_count(db):
    """全分区波形容器实体数合计（去重后总数）。"""
    total = 0
    for xp, yp in _all_partitions(db):
        total += load_partition_waveform(db, xp, yp).waveform_count
    return total


def _pin_activity(amap, design_db, inst_path, pin_name):
    """(实例路径, pin 名) → EXWFPinActivity（未命中 KeyError 透出）。"""
    gid = design_db.convert_to_id("inst", inst_path)
    pid = design_db.convert_to_id("pin", pin_name)
    return amap[gid][pid]


def _assert_stats(amap, design_db, table, tag):
    """README 推导表逐引脚精确断言：(inst, pin) → (t0, t1, tx, tz,
    rise, fall)（单位 ns；fs = ns × 1e6）。tag 为记录键的常量标签（由
    调用点字面量传入）。"""
    mismatches = []
    for (inst_path, pin_name), expect in table.items():
        p = _pin_activity(amap, design_db, inst_path, pin_name)
        actual = (p.t0, p.t1, p.tx, p.tz, p.rise_count, p.fall_count)
        expected = tuple(v * NS for v in expect[:4]) + tuple(expect[4:])
        if actual != expected:
            mismatches.append(
                f"{inst_path}/{pin_name}: {actual} != {expected}")
    # 原断言：assert actual == expected, \
    record(f"{tag}/table_exact", not mismatches, detail=mismatches)


def _all_dual_objects(db):
    """全分区双对象加载可达（空分区亦产出——消费接口恒定，plan §8）。"""
    for xp, yp in _all_partitions(db):
        load_partition_waveform(db, xp, yp)
        load_partition_activity(db, xp, yp)


# ═══ 场景 1：基础建库（small.vcd）════════════════════════════════════

summary = load_waveform_summary(wf_small)
# 原断言：assert len(summary.files) == 1, summary.files
record("s1/files_count", len(summary.files))
assert len(summary.files) >= 1  # 保留：守卫（后面按下标取 summary.files[0]，越界在 C++ 侧是未定义行为）
# 原断言：assert summary.files[0].source_file.endswith("small.vcd")
record("s1/source_suffix",
       summary.files[0].source_file.endswith("small.vcd"),
       detail=summary.files[0].source_file)
# 原断言：assert abs(summary.files[0].timescale_sec - 1e-9) < 1e-15
record("s1/timescale_1ns",
       abs(summary.files[0].timescale_sec - 1e-9) < 1e-15,
       detail=f"{summary.files[0].timescale_sec!r}")
# 原断言：assert summary.files[0].t_start_fs == 0
record("s1/t_start_fs", summary.files[0].t_start_fs)
# 原断言：assert summary.files[0].t_end_fs == 201 * NS
record("s1/t_end_fs", summary.files[0].t_end_fs)
# 原断言：assert summary.total_duration_fs == 201 * NS, summary.total_duration_fs
record("s1/total_duration_fs", summary.total_duration_fs)
# 原断言：assert summary.failed_chunk_count == 0
record("s1/failed_chunk_count", summary.failed_chunk_count)
# 原断言：assert summary.total_hit_count > 0
record("s1/has_hits", summary.total_hit_count > 0,
       detail=summary.total_hit_count)
# 原断言：assert summary.invalid_binding_count == 0
record("s1/invalid_binding_count", summary.invalid_binding_count)
# 网形态①连接位展开（派生布线后 design db NETS 全量在册）：16 网全部
# 挂载成功——无悬空兜底计数
# 原断言：assert summary.dangling_net_count == 0, summary.dangling_net_count
record("s1/dangling_net_count", summary.dangling_net_count)
# settings 元数据缺省口径（VCD 模式无 settings——缺省值随 summary 落库）
# 原断言：assert summary.default_toggle_rate == 0.0
record("s1/default_toggle_rate_zero", summary.default_toggle_rate == 0.0,
       detail=f"{summary.default_toggle_rate!r}")
# 原断言：assert summary.default_static_probability == 0.5
record("s1/default_static_probability_half",
       summary.default_static_probability == 0.5,
       detail=f"{summary.default_static_probability!r}")
# 原断言：assert summary.duration_sec == 1.0
record("s1/duration_sec_1", summary.duration_sec == 1.0,
       detail=f"{summary.duration_sec!r}")
# 原断言：assert summary.x_z_handling == "denominator_include"
record("s1/x_z_handling", summary.x_z_handling)
INFO("[DONE] wf_small summary traceable (201ns window, 1ns scale)")

# README 推导表逐引脚精确断言（cell 端口对偶形态；Q ≡ count[i]，
# D ≡ n_cnt{i}_d，CK/RN ≡ clk/rn）
SMALL_TABLE = {
    ("u_cnt0", "CK"): (101, 100, 0, 0, 20, 20),
    ("u_cnt0", "RN"): (23, 178, 0, 0, 1, 0),
    ("u_cnt0", "Q"): (111, 90, 0, 0, 9, 9),
    ("u_cnt0", "QN"): (90, 111, 0, 0, 9, 9),
    ("u_cnt0", "D"): (90, 111, 0, 0, 9, 9),
    ("u_cnt1", "Q"): (115, 86, 0, 0, 5, 4),
    ("u_cnt1", "D"): (105, 96, 0, 0, 5, 4),
    ("u_cnt2", "Q"): (121, 80, 0, 0, 2, 2),
    ("u_cnt2", "D"): (121, 80, 0, 0, 2, 2),
    ("u_cnt3", "Q"): (121, 80, 0, 0, 1, 1),
    ("u_cnt3", "D"): (121, 80, 0, 0, 1, 1),
    ("u_div", "Q"): (111, 90, 0, 0, 9, 9),
    ("u_div", "D"): (90, 111, 0, 0, 9, 9),
    ("u_div", "CK"): (101, 100, 0, 0, 20, 20),
    ("u_and1", "ZN"): (161, 40, 0, 0, 1, 1),   # ≡ n_and（driver 位）
    ("u_xor1", "Z"): (105, 96, 0, 0, 5, 4),    # ≡ n_xor（driver 位）
    ("u_and_c2", "ZN"): (161, 40, 0, 0, 4, 4),  # ≡ n_b1（driver 位）
    ("u_and_c3a", "ZN"): (181, 20, 0, 0, 2, 2),  # ≡ n_b2（driver 位）
}
amap = _activity_map(wf_small)
_assert_stats(amap, design_small, SMALL_TABLE, "small")
INFO(f"[DONE] small.vcd README table exact on {len(SMALL_TABLE)} pin(s)")

# 波形去重（裁定 4）+ 四级回落（裁定 V-4）：clk 网 = 同 id 码轨道归并
#（全部 CK var 与 clk 网同 id 码——引脚名不另立实体，统计挂网身份）→
# 连接位共享同一 waveform_id；count[0] 网 = 连接位引脚各有独立 var 轨
# 道（u_cnt0/Q、u_inv_c0/A、u_xor_c1/A、u_and_c2/A1、u_xor1/B 的 id 码
# 互异）→ 四级回落第 1 级（引脚自身轨道优先于所连网轨道）各引脚独立
# 实体——去重语义按「同一份翻转数据一份实体」而非「同一张网一份实体」
gid_cnt0 = design_small.convert_to_id("inst", "u_cnt0")
clk_ids = {amap[design_small.convert_to_id("inst", p)][
    design_small.convert_to_id("pin", "CK")].waveform_id
    for p in ("u_cnt0", "u_cnt1", "u_cnt2", "u_cnt3", "u_div")}
# 原断言：assert len(clk_ids) == 1, f"clk net pins must share one id: {clk_ids}"
record("s1/clk_single_waveform_id", len(clk_ids), detail=sorted(clk_ids))
cnt0_ids = {amap[design_small.convert_to_id("inst", p)][
    design_small.convert_to_id("pin", pn)].waveform_id
    for p, pn in (("u_cnt0", "Q"), ("u_inv_c0", "A"), ("u_xor_c1", "A"),
                  ("u_and_c2", "A1"), ("u_xor1", "B"))}
# 原断言：assert len(cnt0_ids) == 5, \
record("s1/cnt0_pin_own_waveform_ids", len(cnt0_ids),
       detail=sorted(cnt0_ids))
referenced_ids = {p.waveform_id
                  for pins in amap.values() for p in pins.values()
                  if p.waveform_id != 0xFFFFFFFF}
waveform_entities = _waveform_container_count(wf_small)
# 原断言：assert waveform_entities == summary.waveform_count
record("s1/entities_match_summary",
       waveform_entities == summary.waveform_count,
       detail=(waveform_entities, summary.waveform_count))
# 全部引用落在实体 id 空间内（引用完整性）；同网连接位共享（clk 归并）
# 与独立轨道回落（count[0] 五引脚）已在上断言。轨道归并（裁定 V-1）
# 后同 id 码双名只立网身份实体——首版实现的形态②别名残影实体已消除，
# 实体数 = 不同身份键实体数（容器实体数 == summary 口径一致）。
# 原断言：assert all(0 <= i < waveform_entities for i in referenced_ids)
record("s1/referenced_ids_in_range",
       all(0 <= i < waveform_entities for i in referenced_ids),
       detail=sorted(i for i in referenced_ids
                     if not 0 <= i < waveform_entities))
INFO(f"[DONE] waveform dedup: {waveform_entities} entities, "
     f"{len(referenced_ids)} referenced (clk merged, count[0] per-pin)")

# waveform_id 分配序 = 身份键全序（P1-3 裁定）：net 形态身份按 design db
# net id 升序编 id。样张网 var 全部先于 cell 端口 var 声明——SMALL_TABLE
# 引脚的统计条目均带 net 身份键（首现保留），16 网逐对断言
# net id 序 == waveform id 序（单分区库——id 序可直接对照）。引脚对照
# 与 SMALL_TABLE 同源（Q ≡ count[i] / u_div.Q ≡ clk_div2、D ≡ n_cnt{i}_d
# / u_div.D ≡ n_div_d、CK/RN ≡ clk/rn、门输出 ≡ n_*）
NET_PIN_OF = {
    "clk": ("u_cnt0", "CK"), "rn": ("u_cnt0", "RN"),
    "count[0]": ("u_cnt0", "Q"), "count[1]": ("u_cnt1", "Q"),
    "count[2]": ("u_cnt2", "Q"), "count[3]": ("u_cnt3", "Q"),
    "clk_div2": ("u_div", "Q"), "n_and": ("u_and1", "ZN"),
    "n_xor": ("u_xor1", "Z"), "n_b1": ("u_and_c2", "ZN"),
    "n_b2": ("u_and_c3a", "ZN"), "n_cnt0_d": ("u_cnt0", "D"),
    "n_cnt1_d": ("u_cnt1", "D"), "n_cnt2_d": ("u_cnt2", "D"),
    "n_cnt3_d": ("u_cnt3", "D"), "n_div_d": ("u_div", "D"),
}
key_order_pairs = []
own_order_pairs = []
for net_name, (inst_path, pin_name) in NET_PIN_OF.items():
    net_id = design_small.convert_to_id("net", net_name)
    # 原断言：assert net_id is not None, net_name
    record(f"s1/net/{net_name}/id_resolved", net_id is not None,
           detail=net_name)
    wave_id = _pin_activity(amap, design_small, inst_path, pin_name) \
        .waveform_id
    # 原断言：assert wave_id != 0xFFFFFFFF, net_name
    record(f"s1/net/{net_name}/waveform_assigned", wave_id != 0xFFFFFFFF,
           detail=(net_name, wave_id))
    assert net_id is not None  # 保留：守卫（net_id 进 key_order_pairs 后参与 sort 排序，None 参与 int 比较会 TypeError）
    if net_name.startswith("count"):
        # count[0..3]：Q var 独立 id 码轨道 → 四级回落第 1 级（own 优先
        # 于所连网轨道）→ 引用形态② own 实体（分配序 (inst, pin) 字典
        # 序段）
        own_order_pairs.append((inst_path, pin_name, wave_id))
    else:
        # 同 id 码轨道归并（CK≡clk / RN≡rn / ZN≡n_* / Q≡clk_div2 /
        # D≡n_cnt*_d）→ 统计挂网键 → 引用形态①网实体（分配序 net id
        # 升序段）
        key_order_pairs.append((net_id, wave_id))
key_order_pairs.sort()  # 按身份键（net id）升序
wave_id_seq = [w for _, w in key_order_pairs]
own_id_seq = [w for _, _, w in own_order_pairs]
# 原断言：assert len(set(wave_id_seq + own_id_seq)) == \
record("s1/all_net_waveforms_distinct",
       len(set(wave_id_seq + own_id_seq))
       == len(wave_id_seq) + len(own_id_seq),
       detail=len(wave_id_seq) + len(own_id_seq)
       - len(set(wave_id_seq + own_id_seq)))
# 原断言：assert wave_id_seq == sorted(wave_id_seq), \
record("s1/merged_ids_net_id_order", wave_id_seq == sorted(wave_id_seq),
       detail=wave_id_seq)
# 原断言：assert own_id_seq == sorted(own_id_seq), \
record("s1/own_ids_inst_pin_order", own_id_seq == sorted(own_id_seq),
       detail=own_id_seq)
INFO("[DONE] waveform_id assignment follows identity-key order "
     "(merged nets: net-id ascending / own tracks: inst-pin ascending)")

# 全分区双对象可达（消费接口恒定）
_all_dual_objects(wf_small)
INFO(f"[DONE] all {_all_partitions(wf_small)} partition(s) yield dual "
     f"objects")

# debug get_activity：点查 + 导出视图（summary 元数据口径现算）
info = wf_small.get_activity("u_cnt0")
gid_u_cnt0 = design_small.convert_to_id("inst", "u_cnt0")
# 原断言：assert info is not None and info["instance_id"] == gid_u_cnt0
record("s1/activity_exists", info is not None)
record("s1/activity_instance_matches",
       info is not None and info["instance_id"] == gid_u_cnt0,
       detail={"exists": info is not None,
               "instance_id": None if info is None else info["instance_id"],
               "expected": gid_u_cnt0})
assert info is not None  # 保留：守卫（后面 info["pins"] 解引用）
pins = {p["pin_name"]: p for p in info["pins"]}
# 原断言：assert set(pins) == {"D", "CK", "RN", "Q", "QN"}, set(pins)
record("s1/activity_pin_names", sorted(pins))
# 保留：守卫——后面按键取 pins["CK"]、pins["Q"]，缺键时会以 KeyError 崩溃
assert "CK" in pins and "Q" in pins, f"pins={sorted(pins)}"
ck = pins["CK"]
# 原断言：assert ck["t0"] == 101 * NS and ck["t1"] == 100 * NS
record("s1/ck_t0", ck["t0"])
record("s1/ck_t1", ck["t1"])
duration_s = summary.total_duration_fs * 1e-15
expect_toggle = (20 + 20) / duration_s
# 原断言：assert abs(ck["toggle_rate"] - expect_toggle) < 1e-6 * expect_toggle, \
record("s1/ck_toggle_rate_matches",
       abs(ck["toggle_rate"] - expect_toggle) < 1e-6 * expect_toggle,
       detail=(f"{ck['toggle_rate']!r}", f"{expect_toggle!r}"))
expect_p1 = 100.0 / 201.0  # denominator_include：t1/(t0+t1+tx+tz)
# 原断言：assert abs(ck["p1"] - expect_p1) < 1e-12, (ck["p1"], expect_p1)
record("s1/ck_p1_include_formula", abs(ck["p1"] - expect_p1) < 1e-12,
       detail=(f"{ck['p1']!r}", f"{expect_p1!r}"))
# 原断言：assert abs(ck["direction"] - 0.5) < 1e-12
record("s1/ck_direction_half", abs(ck["direction"] - 0.5) < 1e-12,
       detail=f"{ck['direction']!r}")
q = pins["Q"]
# 原断言：assert abs(q["direction"] - 9 / 18) < 1e-12
record("s1/q_direction_half", abs(q["direction"] - 9 / 18) < 1e-12,
       detail=f"{q['direction']!r}")
# 未命中族显式 None
# 原断言：assert wf_small.get_activity(99999) is None
record("s1/get_activity_unknown_id", wf_small.get_activity(99999))
# 原断言：assert wf_small.get_activity("no_such_inst") is None
record("s1/get_activity_unknown_name", wf_small.get_activity("no_such_inst"))
# debug get_waveform：事件序列 + 哨兵/过滤
winfo = wf_small.get_waveform("u_cnt0", pin_name="CK")
# 原断言：assert winfo is not None and len(winfo["pins"]) == 1
record("s1/waveform_info", None if winfo is None
       else {"pin_count": len(winfo["pins"])})
assert winfo is not None and len(winfo["pins"]) >= 1  # 保留：守卫（后面按下标取 winfo["pins"][0]）
events = winfo["pins"][0]["events"]
# 原断言：assert len(events) == 41, len(events)
record("s1/ck_event_count", len(events))
assert len(events) >= 2  # 保留：守卫（后面按下标取 events[1]）
# 原断言：assert all(v in ("0", "1", "x", "z") for _, v in events)
record("s1/ck_event_values_legal",
       all(v in ("0", "1", "x", "z") for _, v in events),
       detail=sorted({v for _, v in events}))
# 原断言：assert events[1][0] == 5 * NS and events[1][1] == "1"
record("s1/ck_event1_time", events[1][0])
record("s1/ck_event1_value", events[1][1])
# 原断言：assert wf_small.get_waveform("no_such_inst") is None
record("s1/get_waveform_unknown", wf_small.get_waveform("no_such_inst"))
INFO("[DONE] debug get_activity/get_waveform + exported view")

# ═══ 场景 1b：空分区空对象（partition_count 强制多分区）══════════════

grid_parts = _all_partitions(wf_grid)
design_grid_parts = [
    (design_grid.read_object(DesignDb.DESIGN_OBJ).partition_at(i).xp,
     design_grid.read_object(DesignDb.DESIGN_OBJ).partition_at(i).yp)
    for i in range(
        design_grid.read_object(DesignDb.DESIGN_OBJ).partition_count)]
# 原断言：assert grid_parts == design_grid_parts, (grid_parts, design_grid_parts)
record("s1b/partitions_match_design", grid_parts == design_grid_parts,
       detail=(grid_parts, design_grid_parts))
# 全分区双对象可达 + 逐分区自洽（有数据则双容器非空，无数据则双空——
# 消费接口恒定；分区数/空否由 S8 负载自适应裁定，不假设具体数）
inconsistent = []
for xp, yp in grid_parts:
    wave = load_partition_waveform(wf_grid, xp, yp)
    act = load_partition_activity(wf_grid, xp, yp)
    if wave.waveform_count == 0 or act.size == 0:
        if wave.waveform_count != 0 or act.size != 0:
            inconsistent.append((xp, yp, wave.waveform_count, act.size))
# 原断言：assert wave.waveform_count == 0 and act.size == 0, \
record("s1b/partitions_self_consistent", not inconsistent,
       detail=inconsistent)
INFO(f"[DONE] all {len(grid_parts)} partition(s) yield self-consistent "
     f"dual objects")

# ═══ 场景 2：多文件场景 a（part_a1 + part_a2，统计直和 + 偏移拼接）════

s_a = load_waveform_summary(wf_a)
# 原断言：assert len(s_a.files) == 2, s_a.files
record("s2/files_count", len(s_a.files))
assert len(s_a.files) >= 2  # 保留：守卫（后面按下标取 s_a.files[0] 与 s_a.files[1]，越界在 C++ 侧是未定义行为）
# 原断言：assert s_a.files[0].source_file.endswith("part_a1.vcd")
record("s2/file0_source_suffix",
       s_a.files[0].source_file.endswith("part_a1.vcd"),
       detail=s_a.files[0].source_file)
# 原断言：assert s_a.files[1].source_file.endswith("part_a2_a1.vcd")
record("s2/file1_source_suffix",
       s_a.files[1].source_file.endswith("part_a2_a1.vcd"),
       detail=s_a.files[1].source_file)
# 逐文件时间范围（偏移对齐依据）：两文件各自 [0,101]
# 原断言：assert s_a.files[0].t_start_fs == 0 and s_a.files[0].t_end_fs == 101 * NS
record("s2/file0_t_start_fs", s_a.files[0].t_start_fs)
record("s2/file0_t_end_fs", s_a.files[0].t_end_fs)
# 原断言：assert s_a.files[1].t_start_fs == 0 and s_a.files[1].t_end_fs == 101 * NS
record("s2/file1_t_start_fs", s_a.files[1].t_start_fs)
record("s2/file1_t_end_fs", s_a.files[1].t_end_fs)
# 全局 DURATION 直和（README：101+101 = 202——分段固有语义）
# 原断言：assert s_a.total_duration_fs == 202 * NS, s_a.total_duration_fs
record("s2/total_duration_fs", s_a.total_duration_fs)
# 原断言：assert s_a.time_anomaly_count == 0, s_a.time_anomaly_count
record("s2/time_anomaly_count", s_a.time_anomaly_count)

amap_a = _activity_map(wf_a)
# 直和（README 分段表）：count[0]（u_cnt0/Q）a1 61/40 4/4 + a2 51/50 5/5
p = _pin_activity(amap_a, design_a1, "u_cnt0", "Q")
# 原断言：assert (p.t0, p.t1, p.rise_count, p.fall_count) == \
record("s2/u_cnt0_Q_sum", (p.t0, p.t1, p.rise_count, p.fall_count))
# count[3]：a1 95/6 1/0 + a2 26/75 0/1（SAIF 原始量直和口径）
p = _pin_activity(amap_a, design_a1, "u_cnt3", "Q")
# 原断言：assert (p.t0, p.t1, p.rise_count, p.fall_count) == \
record("s2/u_cnt3_Q_sum", (p.t0, p.t1, p.rise_count, p.fall_count))
# rn：a1 23/78 1/0 + a2 0/101 0/0
p = _pin_activity(amap_a, design_a1, "u_cnt0", "RN")
# 原断言：assert (p.t0, p.t1, p.rise_count, p.fall_count) == (23 * NS, 179 * NS, 1, 0)
record("s2/u_cnt0_RN_sum", (p.t0, p.t1, p.rise_count, p.fall_count))
# 偏移拼接：全部波形事件偏移后全局单调不减、存在跨过 a1 窗口（>101ns）
# 的事件（a2 段整体 +101ns 生效）
crossed = False
non_monotonic = []
for xp, yp in _all_partitions(wf_a):
    wave = load_partition_waveform(wf_a, xp, yp)
    for i in range(wave.waveform_count):
        w = wave.waveform_at(i)
        times = [t for t, _ in w.events]
        if times != sorted(times):
            non_monotonic.append((xp, yp, i))
        if w.t_end > 101 * NS:
            crossed = True
# 原断言：assert times == sorted(times), f"non-monotonic waveform {i}"
record("s2/waveforms_monotonic", not non_monotonic, detail=non_monotonic)
# 原断言：assert crossed, "a2 segment must be offset beyond a1 window"
record("s2/offset_events_cross_a1_window", crossed)
INFO("[DONE] scenario a: direct-sum stats + offset-monotonic events + "
     "per-file stats")

# ═══ 场景 3：多文件场景 b（block_b1 + block_b2，后文件覆盖）══════════

s_b = load_waveform_summary(wf_b)
# 原断言：assert s_b.overlap_override_count == 1, s_b.overlap_override_count
record("s3/overlap_override_count", s_b.overlap_override_count)
amap_b = _activity_map(wf_b)
# (1) 重合实体 n_a1 → u_dff_a/Q 取 b2 值（≠ b1 的 54/30/3/3）
p = _pin_activity(amap_b, design_blk, "u_blk_a/u_dff_a", "Q")
# 原断言：assert (p.t0, p.t1, p.rise_count, p.fall_count) == \
record("s3/override_u_dff_a_Q", (p.t0, p.t1, p.rise_count, p.fall_count))
# (2) b1 独有保留：clk（u_dff_a/CK）44/40 8/8、n_a2（u_inv1/ZN driver 位
#     ——b1 表 30/54 3/3；u_dff_a/D receiver 位同值）
p = _pin_activity(amap_b, design_blk, "u_blk_a/u_dff_a", "CK")
# 原断言：assert (p.t0, p.t1, p.rise_count, p.fall_count) == (44 * NS, 40 * NS, 8, 8)
record("s3/b1_only_u_dff_a_CK", (p.t0, p.t1, p.rise_count, p.fall_count))
p = _pin_activity(amap_b, design_blk, "u_blk_a/u_inv1", "ZN")
# 原断言：assert (p.t0, p.t1, p.rise_count, p.fall_count) == (30 * NS, 54 * NS, 3, 3)
record("s3/b1_only_u_inv1_ZN", (p.t0, p.t1, p.rise_count, p.fall_count))
# (3) b2 独有：u_sub/n1 → u_dff_s/Q 45/29 2/1
p = _pin_activity(amap_b, design_blk, "u_blk_a/u_sub/u_dff_s", "Q")
# 原断言：assert (p.t0, p.t1, p.rise_count, p.fall_count) == (45 * NS, 29 * NS, 2, 1)
record("s3/b2_only_u_dff_s_Q", (p.t0, p.t1, p.rise_count, p.fall_count))
with open(os.path.join(LOG_DIR, "message.log"), errors="replace") as f:
    _messages = f.read()
# 原断言：assert "WFMG::0006" in _messages, "message.log must carry WFMG::0006"
record("s3/wfmg0006_in_msg", "WFMG::0006" in _messages,
       detail=_messages[-2000:])
INFO("[DONE] scenario b: last-file-wins override + WFMG::0006 count 1")

# ═══ 场景 4：x/z 统计（xz.vcd）═══════════════════════════════════════

amap_xz = _activity_map(wf_xz)
XZ_TABLE = {
    ("u_dff", "Q"): (40, 46, 15, 0, 2, 1),    # ≡ n_q（x→0@15 不计）
    ("u_inv1", "ZN"): (46, 40, 15, 0, 1, 2),  # ≡ n_inv
    ("u_tri", "Z"): (15, 15, 0, 71, 1, 0),    # ≡ n_tri（z 段 71ns）
    ("u_tri", "EN"): (71, 30, 0, 0, 1, 1),    # ≡ en
    ("u_buf", "Z"): (23, 23, 35, 20, 0, 0),   # ≡ n_f（净转换全被 x/z 吞）
    ("u_dff", "D"): (42, 51, 8, 0, 2, 1),     # ≡ n_seed
    ("u_dff", "CK"): (51, 50, 0, 0, 10, 10),  # ≡ clk
}
_assert_stats(amap_xz, design_xz, XZ_TABLE, "xz")
s_xz = load_waveform_summary(wf_xz)
# 原断言：assert s_xz.x_z_event_count > 0, s_xz.x_z_event_count
record("s4/has_x_z_events", s_xz.x_z_event_count > 0,
       detail=s_xz.x_z_event_count)
INFO(f"[DONE] xz.vcd TX/TZ exact + rise/fall exclude x/z conversions "
     f"(x_z events = {s_xz.x_z_event_count})")

# 概率两口径（消费侧 API 验证）：denominator_include（默认库）vs exclude
s_xz2 = load_waveform_summary(wf_xz2)
# 原断言：assert s_xz2.x_z_handling == "exclude"
record("s4/xz2_x_z_handling", s_xz2.x_z_handling)
info_inc = wf_xz.get_activity("u_dff")
info_exc = wf_xz2.get_activity("u_dff")
q_inc = {p["pin_name"]: p for p in info_inc["pins"]}["Q"]
q_exc = {p["pin_name"]: p for p in info_exc["pins"]}["Q"]
# 原断言：assert abs(q_inc["p1"] - 46 / 101) < 1e-12, q_inc["p1"]
record("s4/p1_include_formula", abs(q_inc["p1"] - 46 / 101) < 1e-12,
       detail=f"{q_inc['p1']!r}")
# 原断言：assert abs(q_exc["p1"] - 46 / 86) < 1e-12, q_exc["p1"]
record("s4/p1_exclude_formula", abs(q_exc["p1"] - 46 / 86) < 1e-12,
       detail=f"{q_exc['p1']!r}")
INFO("[DONE] P1 two formulas: include=46/101, exclude=46/86")

# ═══ 场景 5：inst pin 直配（instpin.vcd，形态②命中）══════════════════

amap_ip = _activity_map(wf_ip)
# README instpin 表：u_inv1/A 54/30 3/3、u_inv1/ZN 30/54 3/3（var 名不在
# 任何 net 名空间——形态①必然未命中 → 落形态②）
p = _pin_activity(amap_ip, design_blk, "u_blk_a/u_inv1", "A")
# 原断言：assert (p.t0, p.t1, p.rise_count, p.fall_count) == (54 * NS, 30 * NS, 3, 3)
record("s5/u_inv1_A", (p.t0, p.t1, p.rise_count, p.fall_count))
p = _pin_activity(amap_ip, design_blk, "u_blk_a/u_inv1", "ZN")
# 原断言：assert (p.t0, p.t1, p.rise_count, p.fall_count) == (30 * NS, 54 * NS, 3, 3)
record("s5/u_inv1_ZN", (p.t0, p.t1, p.rise_count, p.fall_count))
# 两引脚各自独享波形（形态②单引脚独享——id 独立）
# 原断言：assert p.waveform_id != _pin_activity(
record("s5/zn_a_distinct_waveform_ids",
       p.waveform_id != _pin_activity(
           amap_ip, design_blk, "u_blk_a/u_inv1", "A").waveform_id,
       detail=(p.waveform_id,
               _pin_activity(amap_ip, design_blk, "u_blk_a/u_inv1",
                             "A").waveform_id))
# 形态②与形态①网统计交叉一致（A ≡ u_blk_a/n_a1——同 id 轨道；对照 =
# b1 单文件库，窗口与 instpin 同为 [0,84]——wf_b 的 n_a1 已被 b2 覆盖
# 〔[0,74] 窗〕，不作对照）
p_b1 = _pin_activity(_activity_map(wf_b1), design_blk,
                     "u_blk_a/u_dff_a", "Q")
p_ip = _pin_activity(amap_ip, design_blk, "u_blk_a/u_inv1", "A")
# 原断言：assert (p_ip.t0, p_ip.t1, p_ip.rise_count, p_ip.fall_count) == \
record("s5/form2_matches_form1_window",
       (p_ip.t0, p_ip.t1, p_ip.rise_count, p_ip.fall_count)
       == (p_b1.t0, p_b1.t1, p_b1.rise_count, p_b1.fall_count),
       detail=((p_ip.t0, p_ip.t1, p_ip.rise_count, p_ip.fall_count),
               (p_b1.t0, p_b1.t1, p_b1.rise_count, p_b1.fall_count)))
INFO("[DONE] instpin.vcd form-② direct attach + cross-consistent with "
     "form-① (b1 window)")

# ═══ 场景 6：settings-only 模式（无 VCD；含确定性空分区断言——overrides
# 仅打 u_cnt0，其余分区必空）═══════════════════════════════════════════

s_so = load_waveform_summary(wf_so)
# default 元数据落 summary（不逐引脚物化——裁定 6）；overrides 不入
# settings-only 汇总的 VCD 计数
# 原断言：assert s_so.default_toggle_rate == 2.0e6
record("s6/default_toggle_rate", s_so.default_toggle_rate == 2.0e6,
       detail=f"{s_so.default_toggle_rate!r}")
# 原断言：assert s_so.default_static_probability == 0.4
record("s6/default_static_probability",
       s_so.default_static_probability == 0.4,
       detail=f"{s_so.default_static_probability!r}")
# 原断言：assert s_so.duration_sec == 2.0
record("s6/duration_sec_2", s_so.duration_sec == 2.0,
       detail=f"{s_so.duration_sec!r}")
# 原断言：assert s_so.x_z_handling == "exclude"
record("s6/x_z_handling", s_so.x_z_handling)
# 原断言：assert s_so.total_event_count == 0 and s_so.total_hit_count == 0
record("s6/total_event_count", s_so.total_event_count)
record("s6/total_hit_count", s_so.total_hit_count)
amap_so = _activity_map(wf_so)
# 波形容器恒空 + 统计条目全哨兵 waveform_id（消费接口恒定）
# 原断言：assert _waveform_container_count(wf_so) == 0
record("s6/waveform_container_empty", _waveform_container_count(wf_so))
# 形态②：u_cnt0/Q 直配（T1 = 0.3×2s、rise_fraction 缺省 0.5）
p = _pin_activity(amap_so, design_grid, "u_cnt0", "Q")
# 原断言：assert p.waveform_id == 0xFFFFFFFF
record("s6/q_sentinel_waveform_id", p.waveform_id)
# 原断言：assert (p.t1, p.t0) == (int(0.3 * D_FS), int(0.7 * D_FS))
record("s6/q_override_times", (p.t1, p.t0))
# 原断言：assert (p.rise_count, p.fall_count) == (1_000_000, 1_000_000)
record("s6/q_override_toggles", (p.rise_count, p.fall_count))
# 未物化实例无条目（default 不物化——u_and1 仅 default 覆盖面）
gid_and1 = design_grid.convert_to_id("inst", "u_and1")
# 原断言：assert gid_and1 not in amap_so
record("s6/default_not_materialized", gid_and1 not in amap_so,
       detail=gid_and1)
# 空分区空对象（确定性构造）：overrides 仅命中 u_cnt0（恰落一个分区）
# ——其余分区 ACTIVITY 恒空 + WAVEFORM 恒空（双容器照常产出，plan §8）
# 原断言：assert len(grid_parts) >= 2, grid_parts
record("s6/multi_partitions", len(grid_parts) >= 2, detail=grid_parts)
non_empty = 0
empty_but_wave = []
oversize = []
for xp, yp in grid_parts:
    act = load_partition_activity(wf_so, xp, yp)
    wave = load_partition_waveform(wf_so, xp, yp)
    if act.size == 0:
        if wave.waveform_count != 0:
            empty_but_wave.append((xp, yp, wave.waveform_count))
    else:
        non_empty += 1
        if act.size != 1:
            oversize.append((xp, yp, act.size))
# 原断言：assert wave.waveform_count == 0
record("s6/empty_partitions_have_empty_wave", not empty_but_wave,
       detail=empty_but_wave)
# 原断言：assert act.size == 1
record("s6/nonempty_partition_single_inst", not oversize, detail=oversize)
# 原断言：assert non_empty == 1, \
record("s6/override_lands_one_partition", non_empty == 1, detail=non_empty)
# 未解析条目 WFMG::0001 一次汇总透出（聚合判定口径：全部分区都未物化
# 的条目才计——u_cnt0/Q 落一区已物化不计，仅 ghost/x 计 1；计数随文案
# 透出，精确断言 =1）
with open(os.path.join(LOG_DIR, "message.log"), errors="replace") as f:
    _messages = f.read()
# 原断言：assert "WFMG::0001" in _messages, "unresolved override must be messaged"
record("s6/wfmg0001_in_msg", "WFMG::0001" in _messages,
       detail=_messages[-2000:])
# 原断言：assert "1 settings activity override entr(y/ies) unresolved" in _messages, \
record("s6/exactly_one_unresolved_override",
       "1 settings activity override entr(y/ies) unresolved" in _messages,
       detail=_messages[-2000:])
INFO("[DONE] settings-only: empty waveform container + overrides "
     "materialized + sentinels + default metadata + empty partitions")

# ═══ 场景 7：大规模（big.vcd，2 万实例）══════════════════════════════

s_big = load_waveform_summary(wf_big)
# 原断言：assert s_big.files[0].source_file.endswith("big.vcd")
record("s7/source_suffix", s_big.files[0].source_file.endswith("big.vcd"),
       detail=s_big.files[0].source_file)
# 原断言：assert s_big.failed_chunk_count == 0
record("s7/failed_chunk_count", s_big.failed_chunk_count)
big_parts = _all_partitions(wf_big)
# 原断言：assert len(big_parts) >= 2, f"big design must partition: {big_parts}"
record("s7/multi_partitions", len(big_parts) >= 2, detail=big_parts)
_all_dual_objects(wf_big)
# 万级下限口径（README）：实例覆盖 ≥ 10000 + 命名点查 + 命中条目下限
inst_covered = 0
for xp, yp in big_parts:
    inst_covered += load_partition_activity(wf_big, xp, yp).size
# 原断言：assert inst_covered >= 10000, inst_covered
record("s7/inst_covered_at_least_10000", inst_covered >= 10000,
       detail=inst_covered)
# 原断言：assert s_big.total_hit_count >= 20000, s_big.total_hit_count
record("s7/total_hit_at_least_20000", s_big.total_hit_count >= 20000,
       detail=s_big.total_hit_count)
for name in ("u_dff_0_0", "u_dff_99_99"):
    info = wf_big.get_activity(name)
    # 原断言：assert info is not None, f"{name} must resolve"
    record(f"s7/{name}/resolved", info is not None)
    # 原断言：assert info["pins"], f"{name} must carry pin stats"
    record(f"s7/{name}/has_pins",
           bool(info["pins"]) if info is not None else False)
    assert info is not None  # 保留：守卫（后面 info["pins"] 解引用）
INFO(f"[DONE] big.vcd: {inst_covered} instances, "
     f"{len(big_parts)} partitions, hit={s_big.total_hit_count}")

# ═══ 场景 8：确定性（身份键序确定 + 同输入双跑逐字节一致）════════════

# id 分配序 = 身份键全序已在场景 1 断言（net id 升序 == waveform id 升
# 序——跨构建可复现的强口径，不依赖同进程双跑）；此处补同输入双跑的
# 逐字节一致性（对照库 wf_small_rerun 已在第一批建好）。


def _db_fingerprint(db):
    """db 正式对象逐字节指纹（__getstate__ 序列化字节）。"""
    parts = [load_waveform_summary(db).__getstate__()]
    for xp, yp in _all_partitions(db):
        parts.append(load_partition_waveform(db, xp, yp).__getstate__())
        parts.append(load_partition_activity(db, xp, yp).__getstate__())
    return parts


fp1 = _db_fingerprint(wf_small)
fp2 = _db_fingerprint(wf_small2)
# 原断言：assert len(fp1) == len(fp2)
record("s8/fingerprint_counts_equal", len(fp1) == len(fp2),
       detail=(len(fp1), len(fp2)))
differing = []
for i, (a, b) in enumerate(zip(fp1, fp2)):
    if a != b:
        differing.append(i)
# 原断言：assert a == b, f"object {i} bytes differ across identical runs"
record("s8/db_objects_byte_identical", not differing, detail=differing)
INFO(f"[DONE] determinism: {len(fp1)} db object(s) byte-identical")

# ═══ 场景 8b：混合模式（VCD + settings.activity.overrides 并存，裁定
# IV-2：覆盖条目建库期替换实测统计、waveform_id 保留、default 不生效、
# 覆盖计数入 summary；未解析条目 WFMG::0001 聚合口径）══════════════════

wf_mix = proj.build_waveform_db(
    name="wf_mix",
    vcd_files=[os.path.join(DATA, "small.vcd")],
    design_db=design_small,
    settings={"activity": {"overrides": {
        # 形态②直配（实例引脚层级路径名）
        "u_cnt0/Q": {"toggle_rate": 1.0e6, "static_probability": 0.3,
                     "rise_fraction": 0.6},
        # 形态①网名——经推导展开挂全部连接位
        "n_and": {"toggle_rate": 2.0e6, "static_probability": 0.8},
        # 未解析条目（聚合判定：全部分区都未命中才计）
        "ghost/x": {},
    }}})
# gzip 压缩输入库（场景 9a 的载体）与混合模式库同批提交、一次等待——
# gzip 样张 = small.vcd 的压缩副本
ERR_DIR = qa_tmp("waveform_err")
os.makedirs(ERR_DIR, exist_ok=True)
GZ_PATH = os.path.join(ERR_DIR, "packed.vcd.gz")
with open(os.path.join(DATA, "small.vcd"), "rb") as f:
    _raw = f.read()
with gzip.open(GZ_PATH, "wb") as f:
    f.write(_raw)
wf_gz = proj.build_waveform_db(
    name="wf_gz", vcd_files=[GZ_PATH], design_db=design_small)
_wait_dbs("wf_mix", "wf_gz")
INFO("[WAIT] waveform db batch 2 frozen (mix mode + gzip)")

s_mix = load_waveform_summary(wf_mix)
amap_mix = _activity_map(wf_mix)
# (1) 覆盖条目统计 == 用户值换算（T1 = p·D、T0 = (1−p)·D、rise = rate·
#     D·rf、fall = rate·D·(1−rf)、TX = TZ = 0——与 wf_small 实测 90/111
#     9/9 不同，证明替换发生）
p = _pin_activity(amap_mix, design_small, "u_cnt0", "Q")
# 原断言：assert (p.t1, p.t0, p.tx, p.tz) == \
record("s8b/override_q_times", (p.t1, p.t0, p.tx, p.tz))
# 原断言：assert (p.rise_count, p.fall_count) == (600_000, 400_000), \
record("s8b/override_q_toggles", (p.rise_count, p.fall_count))
# (2) waveform_id 引用保留（事件是实测数据——覆盖只换统计值）
p_plain = _pin_activity(amap, design_small, "u_cnt0", "Q")
# 原断言：assert p.waveform_id == p_plain.waveform_id != 0xFFFFFFFF, \
record("s8b/waveform_id_kept",
       p.waveform_id == p_plain.waveform_id != 0xFFFFFFFF,
       detail=(p.waveform_id, p_plain.waveform_id))
# (3) 未覆盖条目 == 实测（与 wf_small 同引脚逐项一致）
for inst_path, pin_name in (("u_cnt1", "D"), ("u_cnt3", "Q"),
                            ("u_and_c3a", "ZN")):
    a = _pin_activity(amap_mix, design_small, inst_path, pin_name)
    b = _pin_activity(amap, design_small, inst_path, pin_name)
    # 原断言：assert (a.t0, a.t1, a.tx, a.tz, a.rise_count, a.fall_count) == \
    record(f"s8b/uncovered_{inst_path}_{pin_name}_matches",
           (a.t0, a.t1, a.tx, a.tz, a.rise_count, a.fall_count)
           == (b.t0, b.t1, b.tx, b.tz, b.rise_count, b.fall_count),
           detail=(inst_path, pin_name))
# (4) net 名覆盖经形态①推导展开：n_and 连接位（u_and1/ZN driver 位）
#     统计全部换算（rise_fraction 缺省 0.5）
p = _pin_activity(amap_mix, design_small, "u_and1", "ZN")
# 原断言：assert (p.t1, p.t0) == (int(0.8 * MIX_D_FS), int(0.2 * MIX_D_FS)), \
record("s8b/override_nand_times", (p.t1, p.t0))
# 原断言：assert (p.rise_count, p.fall_count) == (1_000_000, 1_000_000)
record("s8b/override_nand_toggles", (p.rise_count, p.fall_count))
# (5) summary 覆盖条目计数（被替换 + 新建的 (inst, pin) 条目总数，>0）
#     + default 元数据缺省（VCD 模式不生效——仅元数据，未给即缺省值）
# 原断言：assert s_mix.override_entry_count > 0, s_mix.override_entry_count
record("s8b/has_override_entries", s_mix.override_entry_count > 0,
       detail=s_mix.override_entry_count)
# 原断言：assert s_mix.default_toggle_rate == 0.0
record("s8b/default_toggle_rate_zero", s_mix.default_toggle_rate == 0.0,
       detail=f"{s_mix.default_toggle_rate!r}")
# 原断言：assert s_mix.default_static_probability == 0.5
record("s8b/default_static_probability_half",
       s_mix.default_static_probability == 0.5,
       detail=f"{s_mix.default_static_probability!r}")
with open(os.path.join(LOG_DIR, "message.log"), errors="replace") as f:
    _messages = f.read()
# 原断言：assert "WFMG::0001" in _messages
record("s8b/wfmg0001_in_msg", "WFMG::0001" in _messages,
       detail=_messages[-2000:])
# 原断言：assert "1 settings activity override entr(y/ies) unresolved" in _messages, \
record("s8b/exactly_one_unresolved_override",
       "1 settings activity override entr(y/ies) unresolved" in _messages,
       detail=_messages[-2000:])
INFO("[DONE] mix mode: override replaces stats + waveform_id kept + "
     f"uncovered intact + override_entry_count={s_mix.override_entry_count}")

# ═══ 场景 9：错误流 ══════════════════════════════════════════════════

# 9a. gzip 压缩 VCD（裁定 IV-1）：文件入口魔数嗅探 → 解压副本进常规切
#     块链——建库成功，统计与明文版逐项一致；解压副本冻结前由汇总任务
#     清理（freeze 清理链单点）。库 wf_gz 已在第二批（场景 8b 末）建好


def _assert_summary_stats_equal(a, b):
    """summary 统计字段逐项一致（files 条目的 source_file 豁免——gzip
    库记录解压副本路径，路径本身不同属预期）。"""
    for field in ("total_duration_fs", "total_event_count",
                  "total_hit_count", "net_name_miss_count",
                  "inst_pin_miss_count", "dangling_net_count",
                  "pg_net_skip_count", "unplaced_instance_count",
                  "strip_miss_count", "unsupported_count",
                  "width_mismatch_count", "waveform_count",
                  "overlap_override_count", "time_anomaly_count",
                  "x_z_event_count", "multi_driver_net_count",
                  "failed_chunk_count", "invalid_binding_count",
                  "override_entry_count", "default_toggle_rate",
                  "default_static_probability", "duration_sec",
                  "x_z_handling"):
        # 原断言：assert getattr(a, field) == getattr(b, field), field
        record(f"s9a/summary_{field}_equal",
               getattr(a, field) == getattr(b, field), detail=field)
    # 原断言：assert len(a.files) == len(b.files)
    record("s9a/files_count_equal", len(a.files) == len(b.files),
           detail=(len(a.files), len(b.files)))
    source_diff = []
    file_field_mismatch = []
    for fa, fb in zip(a.files, b.files):
        if fa.source_file == fb.source_file:
            source_diff.append(fa.source_file)
        for field in ("timescale_sec", "t_start_fs", "t_end_fs",
                      "event_count", "hit_count", "net_name_miss_count",
                      "inst_pin_miss_count", "dangling_net_count",
                      "pg_net_skip_count", "unplaced_instance_count",
                      "strip_miss_count", "unsupported_count",
                      "width_mismatch_count", "failed_chunk_count"):
            if getattr(fa, field) != getattr(fb, field):
                file_field_mismatch.append(field)
    # 原断言：assert fa.source_file != fb.source_file
    record("s9a/source_files_differ", not source_diff, detail=source_diff)
    # 原断言：assert getattr(fa, field) == getattr(fb, field), field
    record("s9a/file_stats_equal", not file_field_mismatch,
           detail=file_field_mismatch)


# 原断言：assert _all_partitions(wf_gz) == _all_partitions(wf_small)
record("s9a/partition_list_equal",
       _all_partitions(wf_gz) == _all_partitions(wf_small),
       detail=(_all_partitions(wf_gz), _all_partitions(wf_small)))
wave_diff = []
act_diff = []
for xp, yp in _all_partitions(wf_gz):
    if load_partition_waveform(wf_gz, xp, yp).__getstate__() != \
            load_partition_waveform(wf_small, xp, yp).__getstate__():
        wave_diff.append((xp, yp))
    if load_partition_activity(wf_gz, xp, yp).__getstate__() != \
            load_partition_activity(wf_small, xp, yp).__getstate__():
        act_diff.append((xp, yp))
# 原断言：assert load_partition_waveform(wf_gz, xp, yp).__getstate__() == \
record("s9a/waveform_containers_byte_identical", not wave_diff,
       detail=wave_diff)
# 原断言：assert load_partition_activity(wf_gz, xp, yp).__getstate__() == \
record("s9a/activity_containers_byte_identical", not act_diff,
       detail=act_diff)
_assert_summary_stats_equal(load_waveform_summary(wf_gz),
                            load_waveform_summary(wf_small))
# 解压副本清理（汇总任务单点——库冻结时已删除）
# 原断言：assert not os.path.isdir(os.path.join(PROJ_PATH, "wf_gz", "wf_scratch")), \
record("s9a/gz_scratch_cleaned",
       not os.path.isdir(os.path.join(PROJ_PATH, "wf_gz", "wf_scratch")),
       detail=os.path.join(PROJ_PATH, "wf_gz", "wf_scratch"))
INFO("[DONE] gzip vcd: decompressed pipeline, byte-identical to plain, "
     "scratch cleaned")

# 9b. 破损 VCD（$scope 未闭合）→ 文件入口任务失败、库不冻结
BAD_PATH = os.path.join(ERR_DIR, "broken.vcd")
with open(BAD_PATH, "w") as f:
    f.write("$date\n\tnow\n$end\n$timescale 1ns $end\n"
            "$scope module wf_small $end\n$var wire 1 ! a $end\n"
            "#0\n0!\n")  # 缺 $upscope/$enddefinitions
wf_bad = proj.build_waveform_db(
    name="wf_bad", vcd_files=[BAD_PATH], design_db=design_small)
assert not proj.wait_frozen("wf_bad", timeout=300), \
    "broken vcd must not freeze"
reason = proj.db_failure_reason("wf_bad")
# 原断言：assert reason is not None, "failure reason must be observable"
record("s9b/failure_reason_observable", reason is not None)
# 非守卫：reason 无后续解引用（脚本到此不再读它）
INFO("[DONE] broken vcd: file-entry task failure, db not frozen")

# 9c. vcd_files=None 且无 settings.activity → 入口报错（不建库）
try:
    proj.build_waveform_db(name="wf_no_activity", vcd_files=None,
                           design_db=design_small)
    raise AssertionError("settings-only without activity must raise")  # 保留：否定性检查——未抛异常时让脚本直接失败
except ValueError as e:
    # 原断言：assert "settings.activity" in str(e), str(e)
    record("s9c/no_activity_msg", "settings.activity" in str(e),
           detail=str(e))
try:
    proj.build_waveform_db(name="wf_no_activity2", vcd_files=None,
                           design_db=design_small,
                           settings={"x_z_handling": "exclude"})
    raise AssertionError("settings-only without activity must raise")  # 保留：否定性检查——未抛异常时让脚本直接失败
except ValueError as e:
    # 原断言：assert "settings.activity" in str(e), str(e)
    record("s9c/no_activity2_msg", "settings.activity" in str(e),
           detail=str(e))
INFO("[DONE] settings-only without settings.activity: entry raises")

shutil.rmtree(ERR_DIR, ignore_errors=True)
shutil.rmtree(os.path.dirname(A2_RENAMED), ignore_errors=True)
shutil.rmtree(os.path.dirname(ROUTED_SMALL_DEF), ignore_errors=True)

print("[DONE] test_emir_waveform_flow")
