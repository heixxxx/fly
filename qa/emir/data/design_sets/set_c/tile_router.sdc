create_clock -name clk_noc -period 1.600 [get_ports clk_noc]
set_input_delay 0.050 -clock clk_noc [get_ports in_n0]
set_output_delay 0.050 -clock clk_noc [get_ports out_n0]
