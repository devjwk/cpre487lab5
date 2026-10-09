#!/bin/sh
# Build the vector file from the framework's board code, then simulate variable_mac with NVC.
# Usage: ./run_sim.sh   (from hw/variable_mac/tb; needs a C++ compiler and nvc)
set -e
c++ -std=c++11 -O2 -DZEDBOARD -DQUANT_VARIABLE -Ifake_xil -I../../../sw/framework/src gen_vectors.cpp -o gen_vectors
./gen_vectors vectors.txt
nvc --std=2008 --work=work -a ../hdl/variable_mac.vhd variable_mac_tb.vhd -e variable_mac_tb -r
