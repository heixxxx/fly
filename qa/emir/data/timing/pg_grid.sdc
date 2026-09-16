create_clock -name clk -period 1.000 [get_ports clk]
set_input_delay 0.050 -rise -clock clk [get_ports d]
set_input_delay 0.050 -fall -clock clk [get_ports d]
set_output_delay 0.050 -clock clk [get_ports q]
