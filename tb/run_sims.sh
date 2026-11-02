#!/usr/bin/env bash
# tb/run_sims.sh -- build and run every self-checking SystemVerilog testbench
# under Verilator, the same way CI does.
#
#   tb/run_sims.sh              run all
#   tb/run_sims.sh tb_mac_lane  run one (or several) by name
#
# A testbench passes only if it builds, exits 0, prints at least one line
# containing "PASS", and prints nothing containing "FAIL" or "%Error".
# Requires Verilator >= 5 (for --timing); CI pins 5.020 via ubuntu-24.04.
set -uo pipefail

cd "$(dirname "$0")/.."
OUT="${SIM_OUT:-build/sim}"
mkdir -p "$OUT"

PKG=rtl/fecim_pkg.sv
UART="rtl/uart/baud_gen.sv rtl/uart/uart_rx.sv rtl/uart/uart_tx.sv"

# name | sources (package first)
declare -A SRC=(
  [tb_uart]="$PKG $UART tb/tb_uart.sv"
  [tb_uart_edge]="$PKG $UART tb/tb_uart_edge.sv"
  [tb_packet_parser]="$PKG rtl/packet/packet_parser.sv tb/tb_packet_parser.sv"
  [tb_packet_tx]="$PKG rtl/packet/packet_tx.sv tb/tb_packet_tx.sv"
  [tb_mac_lane]="$PKG rtl/array/mac_lane.sv tb/sv/tb_mac_lane.sv"
  [tb_ab_integration]="$PKG tb/integration/lane_a_stub.sv tb/integration/lane_b_stub.sv tb/integration/tb_ab_integration.sv"
)
ORDER=(tb_uart tb_uart_edge tb_packet_parser tb_packet_tx tb_mac_lane tb_ab_integration)
[ $# -gt 0 ] && ORDER=("$@")

fails=0
for tb in "${ORDER[@]}"; do
  if [ -z "${SRC[$tb]+x}" ]; then echo "unknown testbench: $tb"; fails=$((fails+1)); continue; fi
  obj="$OUT/obj_$tb"; log="$OUT/$tb.log"
  # Testbench-only lint classes are relaxed; the RTL itself is linted -Wall separately.
  # shellcheck disable=SC2086
  if ! verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -j 0 \
         --top-module "$tb" --Mdir "$obj" ${SRC[$tb]} > "$log.build" 2>&1; then
    printf '%-20s BUILD FAILED (see %s)\n' "$tb" "$log.build"; fails=$((fails+1)); continue
  fi
  timeout 600 "$obj/V$tb" > "$log" 2>&1; rc=$?
  if [ $rc -eq 0 ] && grep -q PASS "$log" && ! grep -qE 'FAIL|%Error' "$log"; then
    printf '%-20s PASS  %s\n' "$tb" "$(grep PASS "$log" | tail -1 | sed 's/^ *//')"
  else
    printf '%-20s FAIL  (exit %d, log %s)\n' "$tb" "$rc" "$log"
    grep -E 'FAIL|%Error|Error' "$log" | head -5 | sed 's/^/    /'
    fails=$((fails+1))
  fi
done

echo "----"
if [ $fails -eq 0 ]; then echo "all ${#ORDER[@]} testbenches passed"; else echo "$fails testbench(es) failed"; fi
exit $(( fails > 0 ))
