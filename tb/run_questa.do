# =============================================================================
# run_questa.do -- Run FeCIM SystemVerilog testbenches in QuestaSim / ModelSim
# Usage in Questa transcript console:
#   cd C:/Users/zhiti/OneDrive/FeCIM-Ferroelectric-Crossbar-FPGA-Emulator
#   do tb/run_questa.do
# =============================================================================

# Ensure any active simulation is ended
catch {quit -sim}

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
vsim -voptargs=+acc work.tb_packet_parser
run -all
catch {quit -sim}

# Run tb_packet_tx
puts "\n=== Running tb_packet_tx ==="
vsim -voptargs=+acc work.tb_packet_tx
run -all
catch {quit -sim}

# Compile & Run tb_control_fsm (Control FSM)
vlog -sv -timescale "1ns/1ps" rtl/control/control_fsm.sv tb/tb_control_fsm.sv
puts "\n=== Running tb_control_fsm ==="
vsim -voptargs=+acc work.tb_control_fsm
run -all
catch {quit -sim}

# Compile & Run tb_ab_integration (A/B frozen seam integration)
vlog -sv -timescale "1ns/1ps" tb/integration/lane_a_stub.sv tb/integration/lane_b_stub.sv tb/integration/tb_ab_integration.sv
puts "\n=== Running tb_ab_integration ==="
vsim -voptargs=+acc work.tb_ab_integration
run -all
catch {quit -sim}

# Compile & Run tb_hardware_gate (Hardware gate at 921600 baud)
vlog -sv -timescale "1ns/1ps" \
    rtl/fecim_pkg.sv rtl/fecim_pkg_checks.sv \
    rtl/uart/baud_gen.sv rtl/uart/uart_tx.sv rtl/uart/uart_rx.sv \
    rtl/packet/packet_parser.sv rtl/packet/packet_tx.sv \
    rtl/control/config_regs.sv rtl/control/control_fsm.sv \
    rtl/control/readout_ser.sv rtl/control/display_driver.sv \
    rtl/control/reset_sync.sv \
    rtl/array/mac_lane.sv rtl/array/mac_array.sv rtl/array/read_noise.sv \
    rtl/array/act_buffer.sv rtl/array/adc_quant.sv \
    rtl/result/result_buffer.sv rtl/result/argmax_unit.sv \
    rtl/write/write_path.sv \
    rtl/fecim_top.sv quartus/fecim_board_top.sv \
    tb/tb_hardware_gate.sv
puts "\n=== Running tb_hardware_gate ==="
vsim -voptargs=+acc work.tb_hardware_gate
run -all
catch {quit -sim}
