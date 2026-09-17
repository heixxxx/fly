create_clock -name clk_cfg -period 4.000 [get_ports clk_cfg]
set_input_delay 0.050 -clock clk_cfg [get_ports cmd0]
set_output_delay 0.050 -clock clk_cfg [get_ports status0]
