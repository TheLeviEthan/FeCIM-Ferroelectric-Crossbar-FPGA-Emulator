#!/usr/bin/env bash
# Build and run tb_mac_lane under Verilator. Exit code != 0 on any failure.
set -euo pipefail
cd "$(dirname "$0")/.."

rm -rf obj_dir
verilator --binary --timing -Wall -Wno-fatal --timescale 1ns/1ps \
    -Itb/golden/mac_lane \
    --top-module tb_mac_lane \
    rtl/fecim_pkg.sv rtl/array/mac_lane.sv tb/sv/tb_mac_lane.sv

./obj_dir/Vtb_mac_lane | tee obj_dir/tb_mac_lane.log
grep -Eq '^PASS [0-9]+/[0-9]+ golden vectors' obj_dir/tb_mac_lane.log
grep -q  '^PASS tb_mac_lane' obj_dir/tb_mac_lane.log
