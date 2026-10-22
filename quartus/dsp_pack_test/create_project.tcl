# Recreates the dsp_pack_test project from scratch.
# Usage: quartus_sh -t create_project.tcl
project_new dsp_pack_test -overwrite

set_global_assignment -name FAMILY "MAX 10"
set_global_assignment -name DEVICE 10M50DAF484C7G
set_global_assignment -name TOP_LEVEL_ENTITY dsp_pack_test
set_global_assignment -name SYSTEMVERILOG_FILE ../../rtl/fecim_pkg.sv
set_global_assignment -name SYSTEMVERILOG_FILE dsp_pack_test.sv
set_global_assignment -name SDC_FILE dsp_pack_test.sdc
set_global_assignment -name PROJECT_OUTPUT_DIRECTORY output_files

set_location_assignment PIN_P11 -to clk
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to clk
foreach p {rst_n w_n0[*] w_n1[*] act[*] prod0[*] prod1[*]} {
    set_instance_assignment -name VIRTUAL_PIN ON -to $p
}

project_close