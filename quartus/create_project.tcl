# Recreates or configures the FeCIM DE10-Lite FPGA project.
# Usage: quartus_sh -t create_project.tcl
if {[project_exists top_level]} {
    project_open top_level
} else {
    project_new top_level
}

set_global_assignment -name FAMILY "MAX 10"
set_global_assignment -name DEVICE 10M50DAF484C7G
set_global_assignment -name TOP_LEVEL_ENTITY fecim_board_top
set_global_assignment -name PROJECT_OUTPUT_DIRECTORY output_files

set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/fecim_pkg.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/fecim_pkg_checks.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/uart/baud_gen.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/uart/uart_tx.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/uart/uart_rx.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/packet/packet_parser.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/packet/packet_tx.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/control/config_regs.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/control/control_fsm.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/control/readout_ser.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/control/display_driver.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/control/reset_sync.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/array/mac_lane.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/array/mac_array.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/array/read_noise.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/array/act_buffer.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/array/adc_quant.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/result/result_buffer.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/result/argmax_unit.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/write/write_path.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/fecim_top.sv
set_global_assignment -name SYSTEMVERILOG_FILE fecim_board_top.sv
set_global_assignment -name SDC_FILE top_level.sdc

# Apply pin assignments
source pins.tcl

project_close
