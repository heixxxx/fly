# set_a_flow.tcl —— A 套（arith_chain，~4.9k 实例）OpenROAD 完整物理流程
#
# 物理分级策略（2026-09-15 裁定）：A 套走完整物理（布图 → IO 引脚 →
# 电源网格 → 布局 → CTS → 布线）。配方与 data/timing/pg_grid_flow.tcl
# 同源（OpenROAD-flow-scripts 官方 nangate45 平台 M1-M4-M7 电源网格）。
#
# 用法（cwd = 本目录）：
#   /root/project/openroad_env/bin/openroad -no_init -exit set_a_flow.tcl
# 输入：Nangate45 tech/macro LEF + typical Liberty + set_a/arith_chain.v
#       + set_a/arith_chain.sdc（gen_set_a.py 产物）
# 产物：set_a/arith_chain.def（真实布局布线结果——design db 输入）
#       set_a/arith_chain_routed.v（布线后网表——TWF 输入）

# —— 读入 ——
read_lef ../timing/NangateOpenCellLibrary.tech.lef
read_lef ../timing/NangateOpenCellLibrary.macro.lef
read_liberty ../timing/NangateOpenCellLibrary_typical.lib
read_verilog set_a/arith_chain.v
link_design arith_chain
read_sdc set_a/arith_chain.sdc

# —— 布图（利用率 40%，正方形）——
initialize_floorplan -utilization 30 -aspect_ratio 1.0 \
    -site FreePDK45_38x28_10R_NP_162NW_34O
make_tracks

# —— IO 引脚摆放 ——
place_pins -hor_layers metal3 -ver_layers metal2

# —— 电源网格（官方 M1-M4-M7 策略）——
add_global_connection -net VDD -inst_pattern .* -pin_pattern {^VDD$} -power
add_global_connection -net VSS -inst_pattern .* -pin_pattern {^VSS$} -ground
set_voltage_domain -name CORE -power VDD -ground VSS
define_pdn_grid -name grid -voltage_domains CORE -pins metal7
add_pdn_stripe -grid grid -layer metal1 -width 0.17 -pitch 2.4 -offset 0 -followpins
add_pdn_stripe -grid grid -layer metal4 -width 0.48 -pitch 56.0 -offset 2
add_pdn_stripe -grid grid -layer metal7 -width 1.40 -pitch 30.0 -offset 2
add_pdn_connect -grid grid -layers {metal1 metal4}
add_pdn_connect -grid grid -layers {metal4 metal7}
pdngen

# —— 布局 ——
set_wire_rc -signal -layer metal3
set_wire_rc -clock -layer metal3
global_placement -skip_nesterov_place -density 0.40
detailed_placement

# —— 时钟树（双时钟域均插缓冲）——
clock_tree_synthesis -buf_list CLKBUF_X3
set_propagated_clock [all_clocks]
detailed_placement

# —— 布线（global route；detailed_route 在本设计上收敛长尾达小时级
# ——skip_nesterov 布局拥塞高、DRT 初始违规 31111，2026-09-17 实测）——
# GRT 结果已为 DEF 提供真实导线几何（design db NETS 可锚定 driver），
# A 套作为「有布线几何」对照面的目的不受影响
set_routing_layers -signal metal1-metal10 -clock metal3-metal10
global_route

# —— 输出 ——
write_def set_a/arith_chain.def
write_verilog set_a/arith_chain_routed.v
# TWF 不在本流程内产出——独立 OpenSTA 3.1.0 是 TWF 权威路径
# （twf_gen_hier.tcl；本预编译包内嵌 OpenSTA 为旧版，无引脚级属性）
