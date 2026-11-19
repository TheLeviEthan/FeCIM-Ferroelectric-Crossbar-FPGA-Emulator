# Recreates or configures the UART loopback FPGA project.
# Usage: quartus_sh -t create_project.tcl
if {[project_exists top_level]} {
    project_open top_level
} else {
    project_new top_level
}

set_global_assignment -name FAMILY "MAX 10"
set_global_assignment -name DEVICE 10M50DAF484C7G
set_global_assignment -name TOP_LEVEL_ENTITY top_level
set_global_assignment -name PROJECT_OUTPUT_DIRECTORY output_files

set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/fecim_pkg.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/uart/baud_gen.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/uart/uart_tx.sv
set_global_assignment -name SYSTEMVERILOG_FILE ../rtl/uart/uart_rx.sv
set_global_assignment -name SYSTEMVERILOG_FILE uart_board_logic.sv
set_global_assignment -name SYSTEMVERILOG_FILE top_level.sv
set_global_assignment -name SDC_FILE top_level.sdc

# Apply pin assignments
source pins.tcl

project_close
