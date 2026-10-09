# Rebuild the Lab 3 block-design project with a 8/4/2-bit MAC and export the XSA plus system-level reports.
# vivado -mode batch -source build_xsa.tcl -tclargs <project.xpr> <C_DATA_WIDTH> <output name, e.g. staged_mac_4bit>
lassign $argv proj width out
open_project $proj
open_bd_design [get_files *.bd]
set_property CONFIG.C_DATA_WIDTH $width [get_bd_cells -filter {VLNV =~ "*_mac:*"}]
# The FIFO sends 32-bit words; keep the low bytes that hold the packed pair (2-bit: TDATA is 4 bits, the
# MAC port takes the low nibble of the byte)
set bytes [expr {($width * 2 + 7) / 8}]
set_property -dict [list CONFIG.M_TDATA_NUM_BYTES $bytes CONFIG.TDATA_REMAP "tdata\[[expr {$bytes * 8 - 1}]:0\]"] \
    [get_bd_cells -filter {VLNV =~ "*axis_subset_converter*"}]
validate_bd_design
save_bd_design
reset_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1
open_run impl_1
report_timing_summary -file ${out}_system_timing.rpt
report_utilization -hierarchical -file ${out}_system_utilization.rpt
report_power -hierarchical_depth 3 -file ${out}_system_power.rpt
write_hw_platform -fixed -include_bit -force ${out}.xsa
exit
