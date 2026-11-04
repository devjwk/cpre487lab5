# vivado -mode batch -source run_synth.tcl
read_vhdl ../hdl/variable_mac.vhd
synth_design -top variable_mac -part xc7z020clg484-1
create_clock -period 5.00 -name main -waveform {0.000 2.500} [get_ports ACLK]
opt_design
place_design
phys_opt_design
route_design
report_timing_summary -file timing_summary.rpt
report_utilization -file utilization.rpt
report_power -file power.rpt
exit
