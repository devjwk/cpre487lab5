relaunch_sim

proc check_result {test_name expected} {
    set actual [get_value -radix hex /staged_mac/MO_AXIS_TDATA]
    if {[string tolower $actual] == [string tolower $expected]} {
        puts "PASS: $test_name -> 0x$actual"
    } else {
        puts "FAIL: $test_name -> got 0x$actual, expected 0x$expected"
    }
}
# Clock and reset
add_force {/staged_mac/ACLK} -radix hex {0 0ns} {1 5000ps} -repeat_every 10000ps
add_force {/staged_mac/ARESETN} -radix hex {0 0ns}
run 30ns
add_force {/staged_mac/ARESETN} -radix hex {1 0ns}
# No back pressure
add_force {/staged_mac/MO_AXIS_TREADY} -radix hex {1 0ns}


# 1. Basic Single Operation
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TLAST} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TID} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TDATA} -radix hex {0101 0ns}
add_force {/staged_mac/SD_AXIS_TUSER} -radix hex {0 0ns}
run 10ns
check_result "Test1_SingleOp" "00000001"
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 50ns
# Expected:
#   0x0000_0001

# 2. Basic Double Operation
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TLAST} -radix hex {0 0ns}
add_force {/staged_mac/SD_AXIS_TID} -radix hex {2 0ns}
add_force {/staged_mac/SD_AXIS_TDATA} -radix hex {0101 0ns}
add_force {/staged_mac/SD_AXIS_TUSER} -radix hex {0 0ns}
run 10ns
add_force {/staged_mac/SD_AXIS_TLAST} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TDATA} -radix hex {0101 0ns}
run 10ns
check_result "Test2_DoubleOp" "00000002"
add_force {/staged_mac/SD_AXIS_TLAST} -radix hex {0 0ns}
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 50ns
# Expected:
#   0x0000_0002


# 3. Basic Single Operation back-to-back
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TLAST} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TID} -radix hex {3 0ns}
add_force {/staged_mac/SD_AXIS_TDATA} -radix hex {0203 0ns}
add_force {/staged_mac/SD_AXIS_TUSER} -radix hex {0 0ns}
run 10ns
check_result "Test3a_BackToBack1" "00000006"
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TLAST} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TID} -radix hex {4 0ns}
add_force {/staged_mac/SD_AXIS_TDATA} -radix hex {0202 0ns}
add_force {/staged_mac/SD_AXIS_TUSER} -radix hex {0 0ns}
run 10ns
run 10ns
check_result "Test3b_BackToBack2" "00000004"
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 50ns
# Expected:
#   0x0000_0006
#   0x0000_0004


# 4. Inital Load Single Operation ********
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TLAST} -radix hex {0 0ns}
add_force {/staged_mac/SD_AXIS_TID} -radix hex {5 0ns}
add_force {/staged_mac/SD_AXIS_TDATA} -radix hex {0001 0ns}
add_force {/staged_mac/SD_AXIS_TUSER} -radix hex {1 0ns}
run 10ns
add_force {/staged_mac/SD_AXIS_TID} -radix hex {6 0ns}
add_force {/staged_mac/SD_AXIS_TLAST} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TDATA} -radix hex {0303 0ns}
add_force {/staged_mac/SD_AXIS_TUSER} -radix hex {0 0ns}
run 10ns
check_result "Test4_InitialLoad" "0000000a"
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 50ns
# Expected:
#   0x0000_000A *****


# 5. Basic Multi Operation
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TLAST} -radix hex {0 0ns}
add_force {/staged_mac/SD_AXIS_TID} -radix hex {7 0ns}
add_force {/staged_mac/SD_AXIS_TDATA} -radix hex {0101 0ns}
add_force {/staged_mac/SD_AXIS_TUSER} -radix hex {0 0ns}
run 10ns
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 10ns
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TID} -radix hex {8 0ns}
add_force {/staged_mac/SD_AXIS_TDATA} -radix hex {0102 0ns}
run 10ns
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 30ns
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TLAST} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TID} -radix hex {9 0ns}
add_force {/staged_mac/SD_AXIS_TDATA} -radix hex {0203 0ns}
run 10ns
check_result "Test5_MultiOp" "00000009"
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 50 ns
# Expected:
#   0x0000_0009


# Apply Back Pressure
add_force {/staged_mac/MO_AXIS_TREADY} -radix hex {1 0ns}
run 10ns

# 6. Basic Single Operation
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TLAST} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TID} -radix hex {A 0ns}
add_force {/staged_mac/SD_AXIS_TDATA} -radix hex {0204 0ns}
add_force {/staged_mac/SD_AXIS_TUSER} -radix hex {0 0ns}
run 10ns
check_result "Test6_BackPressure" "00000008"
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 30ns
# Aleaviate Back Pressure
add_force {/staged_mac/MO_AXIS_TREADY} -radix hex {0 0ns}
run 50ns
# Expected:
#   0x0000_0008

# Release back pressure from test 6 before continuing
add_force {/staged_mac/MO_AXIS_TREADY} -radix hex {1 0ns}
run 10ns

# 7. Conv1-scale accumulation test (N = 5*5*3 = 75)
# bias = 5, each product = 2 x 3 = 6
# Expected accumulator = 5 + 75*6 = 455 = 0x1C7

# Load bias via TUSER
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TLAST}  -radix hex {0 0ns}
add_force {/staged_mac/SD_AXIS_TID}    -radix hex {B 0ns}
add_force {/staged_mac/SD_AXIS_TDATA}  -radix hex {0005 0ns}
add_force {/staged_mac/SD_AXIS_TUSER}  -radix hex {1 0ns}
run 10ns
add_force {/staged_mac/SD_AXIS_TUSER}  -radix hex {0 0ns}

# 75 multiply-accumulate pairs
for {set i 1} {$i <= 75} {incr i} {
    if {$i == 75} {
        add_force {/staged_mac/SD_AXIS_TLAST} -radix hex {1 0ns}
    }
    add_force {/staged_mac/SD_AXIS_TDATA} -radix hex {0203 0ns}
    run 10ns
}
check_result "Test7_Conv1_N75" "000001C7"
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 50ns
# Expected:
#   0x0000_01C7

# 8. Max positive product: 127 x 127 = 16129 (0x3F01)
# No TUSER load -> accumulator starts at 0, single pair, TLAST=1 immediately
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TLAST}  -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TID}    -radix hex {C 0ns}
add_force {/staged_mac/SD_AXIS_TDATA}  -radix hex {7F7F 0ns}
add_force {/staged_mac/SD_AXIS_TUSER}  -radix hex {0 0ns}
run 10ns
check_result "Test8_MaxPositive" "00003f01"
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 50ns
# Expected:
#   0x0000_3F01

# 9. Min negative product: -128 x -128 = 16384 (0x4000)
# 0x80 in two's complement (8-bit) = -128
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TLAST}  -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TID}    -radix hex {D 0ns}
add_force {/staged_mac/SD_AXIS_TDATA}  -radix hex {8080 0ns}
add_force {/staged_mac/SD_AXIS_TUSER}  -radix hex {0 0ns}
run 10ns
check_result "Test9_MaxNegative" "00004000"
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 50ns

# Expected:
#   0x0000_4000

# 10. Minimal group: single pair, no TUSER, TLAST asserted same cycle as data
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TLAST}  -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TID}    -radix hex {E 0ns}
add_force {/staged_mac/SD_AXIS_TDATA}  -radix hex {0202 0ns}
add_force {/staged_mac/SD_AXIS_TUSER}  -radix hex {0 0ns}
run 10ns
check_result "Test10_MinimalGroup" "00000004"
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 50ns
# Expected:
#   0x0000_0004

# 11. Negative result: 127 x -128 = -16256 (0xFFFF_C080 as 32-bit signed)
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TLAST}  -radix hex {1 0ns}
add_force {/staged_mac/SD_AXIS_TID}    -radix hex {F 0ns}
add_force {/staged_mac/SD_AXIS_TDATA}  -radix hex {7F80 0ns}
add_force {/staged_mac/SD_AXIS_TUSER}  -radix hex {0 0ns}
run 10ns
check_result "Test11_MaxNegative" "ffffc080"
add_force {/staged_mac/SD_AXIS_TVALID} -radix hex {0 0ns}
run 50ns
# Expected:
#   0xFFFF_C080

