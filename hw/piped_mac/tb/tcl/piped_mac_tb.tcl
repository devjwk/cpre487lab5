relaunch_sim

# ---------------------------------------------------------------------------
# Self-checking helper
# ---------------------------------------------------------------------------
proc check_result {test_name expected} {
    set actual [get_value -radix hex /piped_mac/MO_AXIS_TDATA]
    if {[string tolower $actual] == [string tolower $expected]} {
        puts "PASS: $test_name -> 0x$actual"
    } else {
        puts "FAIL: $test_name -> got 0x$actual, expected 0x$expected"
    }
}

# ---------------------------------------------------------------------------
# Clock and reset
# ---------------------------------------------------------------------------
add_force {/piped_mac/ACLK} -radix hex {0 0ns} {1 5000ps} -repeat_every 10000ps
add_force {/piped_mac/ARESETN} -radix hex {0 0ns}
run 30ns
add_force {/piped_mac/ARESETN} -radix hex {1 0ns}
add_force {/piped_mac/MO_AXIS_TREADY} -radix hex {1 0ns}
run 10ns

# ---------------------------------------------------------------------------
# 1. Single beat: 1 x 1 = 1
#    capture(1) + propagate(3) = 4 cycles total before checking
# ---------------------------------------------------------------------------
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TLAST}  -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TID}    -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TDATA}  -radix hex {0101 0ns}
add_force {/piped_mac/SD_AXIS_TUSER}  -radix hex {0 0ns}
run 10ns
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 30ns
check_result "Test1_SingleOp" "00000001"
run 40ns

# ---------------------------------------------------------------------------
# 2. Two beats, one group: 1x1 + 1x1 = 2
# ---------------------------------------------------------------------------
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TLAST}  -radix hex {0 0ns}
add_force {/piped_mac/SD_AXIS_TID}    -radix hex {2 0ns}
add_force {/piped_mac/SD_AXIS_TDATA}  -radix hex {0101 0ns}
add_force {/piped_mac/SD_AXIS_TUSER}  -radix hex {0 0ns}
run 10ns
add_force {/piped_mac/SD_AXIS_TLAST}  -radix hex {1 0ns}
run 10ns
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 30ns
check_result "Test2_DoubleOp" "00000002"
run 40ns

# ---------------------------------------------------------------------------
# 3a/3b. Two INDEPENDENT single-beat groups, zero gap between them.
#    staged_mac needed 1 bubble cycle here; piped_mac must NOT need one.
# ---------------------------------------------------------------------------
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TLAST}  -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TID}    -radix hex {3 0ns}
add_force {/piped_mac/SD_AXIS_TDATA}  -radix hex {0203 0ns}
add_force {/piped_mac/SD_AXIS_TUSER}  -radix hex {0 0ns}
run 10ns
add_force {/piped_mac/SD_AXIS_TID}    -radix hex {4 0ns}
add_force {/piped_mac/SD_AXIS_TDATA}  -radix hex {0202 0ns}
run 10ns
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 20ns
check_result "Test3a_ZeroGapGroup1" "00000006"
run 10ns
check_result "Test3b_ZeroGapGroup2" "00000004"
run 40ns

# ---------------------------------------------------------------------------
# 4. Initial load (bias) then one MAC: bias=1, 3x3=9 -> 10
# ---------------------------------------------------------------------------
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TLAST}  -radix hex {0 0ns}
add_force {/piped_mac/SD_AXIS_TID}    -radix hex {5 0ns}
add_force {/piped_mac/SD_AXIS_TDATA}  -radix hex {0001 0ns}
add_force {/piped_mac/SD_AXIS_TUSER}  -radix hex {1 0ns}
run 10ns
add_force {/piped_mac/SD_AXIS_TID}    -radix hex {6 0ns}
add_force {/piped_mac/SD_AXIS_TLAST}  -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TDATA}  -radix hex {0303 0ns}
add_force {/piped_mac/SD_AXIS_TUSER}  -radix hex {0 0ns}
run 10ns
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 30ns
check_result "Test4_InitialLoad" "0000000a"
run 40ns

# ---------------------------------------------------------------------------
# 5. Multi-beat group with gaps between beats: 1+2+6 = 9
# ---------------------------------------------------------------------------
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TLAST}  -radix hex {0 0ns}
add_force {/piped_mac/SD_AXIS_TID}    -radix hex {7 0ns}
add_force {/piped_mac/SD_AXIS_TDATA}  -radix hex {0101 0ns}
add_force {/piped_mac/SD_AXIS_TUSER}  -radix hex {0 0ns}
run 10ns
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 10ns
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TID}    -radix hex {8 0ns}
add_force {/piped_mac/SD_AXIS_TDATA}  -radix hex {0102 0ns}
run 10ns
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 30ns
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TLAST}  -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TID}    -radix hex {9 0ns}
add_force {/piped_mac/SD_AXIS_TDATA}  -radix hex {0203 0ns}
run 10ns
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 30ns
check_result "Test5_MultiOp" "00000009"
run 40ns

# ---------------------------------------------------------------------------
# 6. Back pressure: MO_AXIS_TREADY held low across the whole transaction.
#    Result must appear correctly and then STAY HELD (not overwritten/lost).
# ---------------------------------------------------------------------------
add_force {/piped_mac/MO_AXIS_TREADY} -radix hex {0 0ns}
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TLAST}  -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TID}    -radix hex {A 0ns}
add_force {/piped_mac/SD_AXIS_TDATA}  -radix hex {0204 0ns}
add_force {/piped_mac/SD_AXIS_TUSER}  -radix hex {0 0ns}
run 10ns
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 30ns
run 20ns
check_result "Test6a_BackPressureHeld" "00000008"
add_force {/piped_mac/MO_AXIS_TREADY} -radix hex {1 0ns}
run 10ns
check_result "Test6b_BackPressureReleased" "00000008"
run 40ns

# ---------------------------------------------------------------------------
# 7. conv1-scale streaming: bias=5, then N=75 MACs of 2x3=6, EVERY cycle,
#    zero gaps for the entire group (the real point of pipelining).
#    Expected = 5 + 75*6 = 455 = 0x1C7
# ---------------------------------------------------------------------------
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TLAST}  -radix hex {0 0ns}
add_force {/piped_mac/SD_AXIS_TID}    -radix hex {B 0ns}
add_force {/piped_mac/SD_AXIS_TDATA}  -radix hex {0005 0ns}
add_force {/piped_mac/SD_AXIS_TUSER}  -radix hex {1 0ns}
run 10ns
add_force {/piped_mac/SD_AXIS_TID}    -radix hex {C 0ns}
add_force {/piped_mac/SD_AXIS_TUSER}  -radix hex {0 0ns}
for {set i 1} {$i <= 75} {incr i} {
    if {$i == 75} {
        add_force {/piped_mac/SD_AXIS_TLAST} -radix hex {1 0ns}
    }
    add_force {/piped_mac/SD_AXIS_TDATA} -radix hex {0203 0ns}
    run 10ns
}
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 30ns
check_result "Test7_PipelinedStream_N75" "000001c7"
run 40ns

# ---------------------------------------------------------------------------
# 8-11. int8 edge cases (same values as staged_mac, for direct comparison)
# ---------------------------------------------------------------------------
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TLAST}  -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TID}    -radix hex {D 0ns}
add_force {/piped_mac/SD_AXIS_TDATA}  -radix hex {7F7F 0ns}
add_force {/piped_mac/SD_AXIS_TUSER}  -radix hex {0 0ns}
run 10ns
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 30ns
check_result "Test8_MaxPositive" "00003f01"
run 40ns

add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TLAST}  -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TID}    -radix hex {E 0ns}
add_force {/piped_mac/SD_AXIS_TDATA}  -radix hex {8080 0ns}
add_force {/piped_mac/SD_AXIS_TUSER}  -radix hex {0 0ns}
run 10ns
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 30ns
check_result "Test9_MaxNegative" "00004000"
run 40ns

add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TLAST}  -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TID}    -radix hex {F 0ns}
add_force {/piped_mac/SD_AXIS_TDATA}  -radix hex {0202 0ns}
add_force {/piped_mac/SD_AXIS_TUSER}  -radix hex {0 0ns}
run 10ns
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 30ns
check_result "Test10_MinimalGroup" "00000004"
run 40ns

add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TLAST}  -radix hex {1 0ns}
add_force {/piped_mac/SD_AXIS_TID}    -radix hex {10 0ns}
add_force {/piped_mac/SD_AXIS_TDATA}  -radix hex {7F80 0ns}
add_force {/piped_mac/SD_AXIS_TUSER}  -radix hex {0 0ns}
run 10ns
add_force {/piped_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 30ns
check_result "Test11_NegativeResult" "ffffc080"
run 40ns