open_project tc_staged_mac/tc_staged_mac.xpr
reset_run synth_1
launch_runs synth_1 -jobs 4
wait_on_run synth_1
reset_run impl_1
launch_runs impl_1 -to_step route_design -jobs 4
wait_on_run impl_1
open_run impl_1
report_timing_summary -file timing_summary.rpt
report_utilization -file utilization.rpt
exit
