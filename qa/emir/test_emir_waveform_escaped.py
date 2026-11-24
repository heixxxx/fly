"""E2E test: 波形库转义标识符（2026-11-24 真实数据兼容裁定）。

Verilog 转义标识符以反斜杠起头、以空白结尾，名字本身不含那个反斜杠；
仿真器（Icarus Verilog）在值变化转储文件的 `$var` 行与 `$scope` 行原样
写出「反斜杠 + 名字」（真实网表里综合工具展平层次生成的名字都是这种
形态，如 gcd 的 `\\dpath.a_lt_b$in1[9]`）。波形解析器在解析边界去掉名
字开头的一个反斜杠（名字中间的反斜杠属于名字本身，不动；向量声明的
位区间 token 不动）。

样张在用例内从 qa/emir/data/waveform/small.vcd 派生（同
test_emir_waveform_flow.py 里派生带布线副本/改名副本的手法，不新增数据
文件）。派生规则只有三条：
  1. `$timescale` 的 `1ns` 改成 `1ps`，全部时间戳（`#` 开头的数）乘
     1000；
  2. 每个 `$var` 行里，标识符编码之后的名字前加一个反斜杠（`\\n_xor`；
     向量声明是 `\\count [3:0]`——位区间那一个 token 不动）；
  3. 每个 `$scope module` 行的模块名前加一个反斜杠（`\\wf_small`、
     `\\u_and1` 等）。

值变化部分一个字节不动。断言（escaped 库 vs 读原样 small.vcd 的库）：
  - 未解析类计数与原样库逐项相等（small.vcd 的 var 名含实例/引脚形态
    名，形态①跳过计数是两库共有的固有口径——转义处理未引入任何新
    miss 即为修复生效的口径）；未放置实例 0、悬空网 0（两库同为 0）；
  - 汇总里与时间单位无关的统计字段（命中数、各类翻转计数、波形数、悬
    空网数等）逐项相等；时间相关的量物理值相等（派生规则 = 单位 1ns→
    1ps 且时间戳 ×1000，两者抵消——窗口 fs 值逐项相等）；
  - 抽三个实例引脚（u_and1/ZN、u_cnt0/Q、u_cnt1/D——三个不同实例的引
    脚形态，覆盖标量网端点 / 向量拆位端点 / 经实例作用域转储的引脚轨
    道）对比翻转统计一致。
"""
import os
import shutil

from log import INFO

from fly import get_config, launch_workers, wait_tasks
from fly.runtime import get_agent
from emir import EMIRProject

from test import qa_tmp

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(SCRIPT_DIR, "data", "waveform")
DESIGN_DATA = os.path.join(DATA, "design")
TIMING_DATA = os.path.join(SCRIPT_DIR, "data", "timing")
TECH_LEF = os.path.join(TIMING_DATA, "NangateOpenCellLibrary.tech.lef")
MACRO_LEF = os.path.join(TIMING_DATA, "NangateOpenCellLibrary.macro.lef")
LIBERTY = os.path.join(TIMING_DATA, "NangateOpenCellLibrary_typical.lib")

LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "emir_waveform_escaped")

NS = 1_000_000  # fs 每 ns（small.vcd 1ns 档）
PS = 1_000      # fs 每 ps（派生样张 1ps 档）

from emir.waveform import (  # noqa: E402  读库 API（模块函数层）
    iter_waveform_partition,
    load_partition_activity,
    load_waveform_summary,
)


def cleanup():
    if os.path.isdir(PROJ_PATH):
        shutil.rmtree(PROJ_PATH, ignore_errors=True)


cleanup()

launch_workers([{}, {}])
assert get_agent().wait_workers_registered(timeout=60), "workers connect"

proj = EMIRProject(PROJ_PATH)
lib_db = proj.build_lib_db(name="wf_esc_lib", lib_paths=[LIBERTY])
assert proj.wait_frozen("wf_esc_lib", timeout=1200), "lib db should freeze"


def _routed_def(def_name):
    """派生带 ROUTED 几何的 DEF 副本（同 test_emir_waveform_flow 的手法
    ——design db 的分区 NETS 对象跟随网几何副本，波形形态①net 名连接位
    展开的载体）。"""
    src = os.path.join(DESIGN_DATA, def_name)
    out_dir = qa_tmp("waveform_escaped_def")
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
            stripped = compact[:-1].rstrip() + \
                " + ROUTED metal1 ( 1000 4000 ) ( 2000 4000 ) ;"
        lines.append(stripped)
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")
    return out


# ── 派生转义样张（三条规则；值变化部分一个字节不动）─────────────────
def _escaped_vcd():
    out_dir = qa_tmp("waveform_escaped_vcd")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, "small_escaped.vcd")
    with open(os.path.join(DATA, "small.vcd")) as f:
        lines = f.read().splitlines(keepends=True)
    out_lines = []
    in_header = True
    for line in lines:
        stripped = line.strip()
        if in_header:
            # 规则 1：$timescale 的 1ns → 1ps（Icarus 两行形态——数值在
            # $timescale 行的下一行；时间戳在事件段成倍换算）
            if stripped in ("1ns", "$timescale 1ns"):
                indent = line[:len(line) - len(line.lstrip())]
                out_lines.append(f"{indent}1ps\n")
                continue
            if stripped.startswith("$var"):
                # 规则 2：标识符编码之后的名字前加反斜杠（位区间 token
                # 不动——名字 token 拼接后才遇到 '[' 开头的区间）
                indent = line[:len(line) - len(line.lstrip())]
                tokens = stripped.split()
                # $var <type> <width> <id> <name...> $end
                assert tokens[0] == "$var" and tokens[-1] == "$end"
                name_at = 4
                # 名字 token 序（可含空格容差形态）——首个非位区间 token
                # 前加反斜杠；位区间 token 原样
                new_tokens = tokens[:name_at]
                rest = tokens[name_at:-1]
                for i, tok in enumerate(rest):
                    if not tok.startswith("["):
                        rest[i] = "\\" + tok
                        break
                new_tokens.extend(rest)
                new_tokens.append("$end")
                out_lines.append(indent + " ".join(new_tokens) + "\n")
                continue
            if stripped.startswith("$scope"):
                # 规则 3：模块名前加反斜杠
                indent = line[:len(line) - len(line.lstrip())]
                tokens = stripped.split()
                assert tokens[0] == "$scope" and tokens[-1] == "$end"
                tokens[2] = "\\" + tokens[2]
                out_lines.append(indent + " ".join(tokens) + "\n")
                continue
            if stripped.startswith("$enddefinitions"):
                in_header = False
            out_lines.append(line)
            continue
        # 事件段：时间戳 ×1000（规则 1），值变化行一个字节不动
        if stripped.startswith("#"):
            indent = line[:len(line) - len(line.lstrip())]
            out_lines.append(f"{indent}#{int(stripped[1:]) * 1000}\n")
            continue
        out_lines.append(line)
    with open(out, "w") as f:
        f.writelines(out_lines)
    return out


ROUTED_DEF = _routed_def("wf_small.def")
ESCAPED_VCD = _escaped_vcd()

design = proj.build_design_db(
    name="wf_esc_design",
    def_paths=[ROUTED_DEF],
    lef_paths=[TECH_LEF, MACRO_LEF],
    lib_db=lib_db)
wf_plain = proj.build_waveform_db(
    name="wf_esc_plain",
    vcd_files=[os.path.join(DATA, "small.vcd")],
    design_db=design)
wf_esc = proj.build_waveform_db(
    name="wf_esc_escaped",
    vcd_files=[ESCAPED_VCD],
    design_db=design)
assert wait_tasks(timeout=1800), "submitted flows must complete"
for n in ("wf_esc_design", "wf_esc_plain", "wf_esc_escaped"):
    assert proj.wait_frozen(n, timeout=60), f"{n} should freeze"
INFO("[WAIT] design + both waveform dbs frozen")

# ── 解析面：转义样张头部还原（spot check 经活动统计间接验证）─────────

s_plain = load_waveform_summary(wf_plain)
s_esc = load_waveform_summary(wf_esc)

# 转义样张解析成功：与读原样 small.vcd 的库逐项一致——small.vcd 的 var
# 名含大量实例/引脚形态名（u_cnt0/CK 等），它们本来就不进网名空间、形
# 态①跳过计数（WFMG::0001/0002）是两库共有的固有口径（两边同为
# 55/5）；转义不去掉的话网名 miss 会再多出全部转义网（16 个）、悬空与
# 未放置同时出现——逐项相等即锁定「转义处理未引入任何新 miss 且修复
# 生效」。未放置实例两库同为 0（端口条目不是实例——2026-11-24 端口角
# 色裁定后的口径）。
assert s_esc.net_name_miss_count == s_plain.net_name_miss_count, \
    (s_esc.net_name_miss_count, s_plain.net_name_miss_count)
assert s_esc.inst_pin_miss_count == s_plain.inst_pin_miss_count, \
    (s_esc.inst_pin_miss_count, s_plain.inst_pin_miss_count)
assert s_esc.unplaced_instance_count == 0, s_esc.unplaced_instance_count
assert s_esc.unplaced_instance_count == s_plain.unplaced_instance_count
assert s_esc.dangling_net_count == 0, s_esc.dangling_net_count
assert s_esc.dangling_net_count == s_plain.dangling_net_count
INFO("[OK] escaped vcd: counters identical to plain small.vcd build "
     "(no new misses introduced), 0 unplaced, 0 dangling")

# 时间单位：1ps 档（原样 1ns）
assert abs(s_plain.files[0].timescale_sec - 1e-9) < 1e-15
assert abs(s_esc.files[0].timescale_sec - 1e-12) < 1e-15

# 与时间无关的统计字段逐项相等（事件/命中/翻转/波形/悬空/多驱动等）
for field in ("total_event_count", "total_hit_count",
              "net_name_miss_count", "inst_pin_miss_count",
              "dangling_net_count", "pg_net_skip_count",
              "unplaced_instance_count", "strip_miss_count",
              "unsupported_count", "width_mismatch_count",
              "waveform_count", "overlap_override_count",
              "time_anomaly_count", "x_z_event_count",
              "multi_driver_net_count", "failed_chunk_count",
              "invalid_binding_count"):
    a = getattr(s_esc, field)
    b = getattr(s_plain, field)
    assert a == b, f"{field}: escaped={a} plain={b}"
INFO("[OK] time-independent summary fields identical")

# 时间相关的量：派生规则 = 单位 1ns→1ps（×1/1000）且时间戳 ×1000——
# 物理时长精确不变（窗口 / 逐文件时间范围 fs 值逐项相等；timescale_sec
# 差 1000 倍）。翻转时间点的物理位置不变。
assert s_esc.total_duration_fs == s_plain.total_duration_fs, \
    (s_esc.total_duration_fs, s_plain.total_duration_fs)
assert s_esc.files[0].t_start_fs == s_plain.files[0].t_start_fs
assert s_esc.files[0].t_end_fs == s_plain.files[0].t_end_fs
assert abs(s_esc.files[0].timescale_sec * 1000
           - s_plain.files[0].timescale_sec) < 1e-24
assert s_esc.files[0].event_count == s_plain.files[0].event_count
assert s_esc.files[0].hit_count == s_plain.files[0].hit_count
INFO("[OK] time-related fields physically identical (1ns→1ps and "
     "timestamps ×1000 cancel out)")


def _activity_map(db):
    result = {}
    for xp, yp in iter_waveform_partition(db):
        act = load_partition_activity(db, xp, yp)
        for iid in act.instance_ids:
            inst = act.instance_at(iid)
            result[iid] = {p.pin_id: p for p in inst.pins}
    return result


def _pin(amap, db, inst_path, pin_name):
    gid = db.convert_to_id("inst", inst_path)
    pid = db.convert_to_id("pin", pin_name)
    return amap[gid][pid]


amap_plain = _activity_map(wf_plain)
amap_esc = _activity_map(wf_esc)

# 抽三个网（标量 n_and / 向量 count[0] 的一位 / 实例作用域转储的引脚）
# 对比翻转统计与时间（换算后）一致
SPOT = (("u_and1", "ZN"), ("u_cnt0", "Q"), ("u_cnt1", "D"))
for inst_path, pin_name in SPOT:
    p_plain = _pin(amap_plain, design, inst_path, pin_name)
    p_esc = _pin(amap_esc, design, inst_path, pin_name)
    assert (p_plain.rise_count, p_plain.fall_count) == \
        (p_esc.rise_count, p_esc.fall_count), \
        f"{inst_path}/{pin_name}: rise/fall differ"
    assert (p_plain.t0, p_plain.t1, p_plain.tx, p_plain.tz) == \
        (p_esc.t0, p_esc.t1, p_esc.tx, p_esc.tz), \
        f"{inst_path}/{pin_name}: time window differ"
INFO(f"[OK] spot nets physically identical: {SPOT}")

shutil.rmtree(os.path.dirname(ROUTED_DEF), ignore_errors=True)
shutil.rmtree(os.path.dirname(ESCAPED_VCD), ignore_errors=True)

get_agent().stop()
print("[PASS] test_emir_waveform_escaped")
