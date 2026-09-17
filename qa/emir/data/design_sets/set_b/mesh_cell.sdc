create_clock -name clk_noc -period 1.200 [get_ports clk_noc]
create_clock -name clk_cfg -period 3.000 [get_ports clk_cfg]
set_input_delay 0.050 -clock clk_noc [get_ports flit_in_loc0]
set_output_delay 0.050 -clock clk_noc [get_ports flit_out_loc0]
