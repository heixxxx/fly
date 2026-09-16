# pg_grid_twf.tcl —— 独立 OpenSTA 驱动：从布线后网表产出 pg_grid.twf（权威单一路径）
#
# 用法（cwd = 本数据目录）：
#   sta -no_init -exit pg_grid_twf.tcl
#   （sta = 独立 OpenSTA 3.1.0，/root/project/opensta/build/sta；不是 OpenROAD
#    预编译包内嵌引擎——根因见下）
#
# 输入：NangateOpenCellLibrary_typical.lib + pg_grid_routed.v（OpenROAD 流程
#       pg_grid_flow.tcl 的布线后网表产物）+ pg_grid.sdc
# 产物：pg_grid.twf（网络维度；传播时钟——网表内含 CTS 时钟树；经 twf_gen.tcl
#       参数化复用，跳过读入段）
#
# 为何不走 OpenROAD 内嵌引擎产出 TWF（2026-09-15 根因实测）：预编译包
# （LiteX-Hub 构建 f12e2f47）内嵌的 OpenSTA 为旧版，引脚级属性缺失——查
# arrival/slew 报「pin objects do not have a arrival_min_rise property」，产出
# 条目值全为 *（空）；其 report_checks 路径报告本身正常，布图/PDN/布局/CTS/
# 布线能力不受影响。故分工：OpenROAD 流程只产 pg_grid.def + pg_grid_routed.v，
# TWF 由属性面完整的独立 OpenSTA 从网表产出。
#
# 数值语义：无寄生估计（独立 OpenSTA 不读物理 DEF——单元弧时序 + 零线负载，
# 窗口值确定可复现）；传播时钟按网表时钟树逐单元传播。确定性链：pg_grid.def
# 流程双跑逐字节一致（2026-09-15 实证）→ pg_grid_routed.v 随流程确定 →
# pg_grid.twf 驱动双跑逐字节一致。

read_liberty NangateOpenCellLibrary_typical.lib
read_verilog pg_grid_routed.v
link_design pg_grid
read_sdc pg_grid.sdc
set_propagated_clock [all_clocks]
set ::env(TWF_PREFIX) pg_grid
set ::env(TWF_TOP) pg_grid
set ::env(TWF_SKIP_READ) 1
set ::env(TWF_ONLY) NET
source twf_gen.tcl
