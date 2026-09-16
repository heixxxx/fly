# pg_grid_flow.tcl —— OpenROAD 自动布图/PDN/布局/CTS/布线流程（途径二放大设计）
#
# 用法（cwd = 本数据目录）：
#   openroad -no_init -exit pg_grid_flow.tcl
# 输入：Nangate45 tech/macro LEF + typical Liberty + pg_grid.v + pg_grid.sdc
#       （pg_grid.v 由 gen_pg_grid.py 生成）
# 产物：pg_grid.def（真实自动布局布线结果：metal1 followpins + metal4/metal7
#       电源网格条带 + 层间过孔、时钟树、信号网多层布线几何）
#       pg_grid_routed.v（布线后网表——TWF 的输入）
# TWF：不在本流程产出，见 pg_grid_twf.tcl
#
# 电源网格 = OpenROAD-flow-scripts 官方 nangate45 平台配方
# grid_strategy-M1-M4-M7.tcl 的 CORE 网格部分（2026-09-15 取自 master）。

# —— 读入 ——
read_lef NangateOpenCellLibrary.tech.lef
read_lef NangateOpenCellLibrary.macro.lef
read_liberty NangateOpenCellLibrary_typical.lib
read_verilog pg_grid.v
link_design pg_grid
read_sdc pg_grid.sdc

# —— 布图（利用率 30%，正方形）——
initialize_floorplan -utilization 30 -aspect_ratio 1.0 \
    -site FreePDK45_38x28_10R_NP_162NW_34O
make_tracks

# —— IO 引脚摆放 ——
place_pins -hor_layers metal3 -ver_layers metal2

# —— 电源网格（官方 M1-M4-M7 策略：followpins + 两层条带 + 层间连接）——
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
global_placement -density 0.40
detailed_placement

# —— 时钟树 ——
clock_tree_synthesis -buf_list CLKBUF_X3
set_propagated_clock [all_clocks]
detailed_placement

# —— 布线 ——
set_routing_layers -signal metal1-metal10 -clock metal3-metal10
global_route
detailed_route

# —— 输出 ——
write_def pg_grid.def
write_verilog pg_grid_routed.v
# TWF 不在本流程内产出——权威路径见 pg_grid_twf.tcl（独立 OpenSTA 读
# pg_grid_routed.v：本预编译包内嵌 OpenSTA 为旧版，无引脚级
# arrival/slew/slack 属性——2026-09-15 实测「pin objects do not have a
# arrival_min_rise property」）
