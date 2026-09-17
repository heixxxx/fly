create_clock -name clk_core -period 1.000 [get_ports clk_core]
set_input_delay 0.050 -clock clk_core [get_ports a0]
set_output_delay 0.050 -clock clk_core [get_ports y0]
