# Build one MAC variant in the Lab 3 block-design project (PS7 + AXI FIFO + MAC) and export its XSA and
# system-level reports into the current directory.
# vivado -mode batch -source build_xsa.tcl -tclargs <project.xpr> <staged|piped> <C_DATA_WIDTH: 8|4|2>
lassign $argv proj mac width
set out ${mac}_mac_${width}bit
set hw [file dirname [file normalize [info script]]]

open_project $proj
open_bd_design [get_files staged_mac_bd.bd]

# The Lab 3 debug ILA is not part of the design being measured
set ila [get_bd_cells -quiet -filter {VLNV =~ "*system_ila*"}]
if {[llength $ila]} { delete_bd_objs $ila }

# Swap the MAC module if the block design holds the other one (both have the same ports)
if {![llength [get_bd_cells -quiet -filter "VLNV =~ \"*:${mac}_mac:*\""]]} {
    set old [get_bd_cells -filter {VLNV =~ "*_mac:*"}]
    set intf {}
    foreach p [get_bd_intf_pins -of_objects $old] {
        foreach q [get_bd_intf_pins -quiet -of_objects [get_bd_intf_nets -quiet -of_objects $p]] {
            if {$q ne $p} { lappend intf [get_property NAME $p] $q }
        }
    }
    set pins {}
    foreach p [get_bd_pins -of_objects $old] {
        set others [lsearch -all -inline -not -exact [get_bd_pins -quiet -of_objects [get_bd_nets -quiet -of_objects $p]] $p]
        if {[llength $others]} { lappend pins [get_property NAME $p] [lindex $others 0] }
    }
    delete_bd_objs $old
    add_files -norecurse $hw/${mac}_mac/hdl/${mac}_mac.vhd
    update_compile_order -fileset sources_1
    set new [create_bd_cell -type module -reference ${mac}_mac ${mac}_mac_0]
    foreach {name q} $intf { connect_bd_intf_net [get_bd_intf_pins $new/$name] [get_bd_intf_pins $q] }
    foreach {name q} $pins { connect_bd_net [get_bd_pins $new/$name] [get_bd_pins $q] }
}

set_property CONFIG.C_DATA_WIDTH $width [get_bd_cells -filter {VLNV =~ "*_mac:*"}]
# The FIFO sends 32-bit words; keep the low bytes that hold the packed pair (2-bit: TDATA is 4 bits, the
# MAC port takes the low nibble of the byte)
set bytes [expr {($width * 2 + 7) / 8}]
set_property -dict [list CONFIG.M_TDATA_NUM_BYTES $bytes CONFIG.TDATA_REMAP "tdata\[[expr {$bytes * 8 - 1}]:0\]"] \
    [get_bd_cells -filter {VLNV =~ "*axis_subset_converter*"}]
validate_bd_design
save_bd_design

reset_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} { error "impl_1 did not finish: [get_property STATUS [get_runs impl_1]]" }
open_run impl_1
report_timing_summary -file ${out}_system_timing.rpt
report_utilization -hierarchical -file ${out}_system_utilization.rpt
report_power -hierarchical_depth 3 -file ${out}_system_power.rpt
write_hw_platform -fixed -include_bit -force -file ${out}.xsa
exit
