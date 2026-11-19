# =============================================================================
# run_questa.do -- Run FeCIM SystemVerilog testbenches in QuestaSim / ModelSim
# Usage in Questa transcript console:
#   cd C:/Users/zhiti/OneDrive/FeCIM-Ferroelectric-Crossbar-FPGA-Emulator
#   do tb/run_questa.do
# =============================================================================

# Create work library if it does not exist
if {![file exists work]} {
    vlib work
}

# Compile package first
vlog -sv -timescale "1ns/1ps" rtl/fecim_pkg.sv

# Compile RTL & Testbenches
vlog -sv -timescale "1ns/1ps" rtl/packet/packet_parser.sv tb/tb_packet_parser.sv
vlog -sv -timescale "1ns/1ps" rtl/packet/packet_tx.sv tb/tb_packet_tx.sv

# Run tb_packet_parser
puts "\n=== Running tb_packet_parser ==="
vsim work.tb_packet_parser
run -all

# Run tb_packet_tx
puts "\n=== Running tb_packet_tx ==="
vsim work.tb_packet_tx
run -all

# Compile & Run tb_control_fsm (Control FSM)
vlog -sv -timescale "1ns/1ps" rtl/control/control_fsm.sv tb/tb_control_fsm.sv
puts "\n=== Running tb_control_fsm ==="
vsim work.tb_control_fsm
run -all

# Compile & Run tb_ab_integration (A/B frozen seam integration)
vlog -sv -timescale "1ns/1ps" tb/integration/lane_a_stub.sv tb/integration/lane_b_stub.sv tb/integration/tb_ab_integration.sv
puts "\n=== Running tb_ab_integration ==="
vsim work.tb_ab_integration
run -all
