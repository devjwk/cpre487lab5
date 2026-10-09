# vivado -mode batch -source run_synth.tcl -tclargs <C_DATA_WIDTH: 8 | 4 | 2>
set width [expr {$argc > 0 ? [lindex $argv 0] : 8}]
open_project tc_piped_mac/tc_piped_mac.xpr
set_property generic "C_DATA_WIDTH=$width" [current_fileset]
reset_run synth_1
launch_runs synth_1 -jobs 4
wait_on_run synth_1
reset_run impl_1
launch_runs impl_1 -to_step route_design -jobs 4
wait_on_run impl_1
open_run impl_1
report_timing_summary -file timing_summary_${width}bit.rpt
report_utilization -file utilization_${width}bit.rpt
report_power -file power_${width}bit.rpt
exit
