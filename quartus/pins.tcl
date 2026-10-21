# ============================================================
# DE10-Lite Pin Assignments
# Project: UART FPGA Loopback
# Top-Level Module: uart_fpga_top
# ============================================================

# ------------------------------------------------------------
# 50 MHz Clock
# ------------------------------------------------------------
set_location_assignment PIN_P11 -to MAX10_CLK1_50
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to MAX10_CLK1_50

# ------------------------------------------------------------
# KEY0 - Active-Low Reset
# ------------------------------------------------------------
set_location_assignment PIN_B8 -to KEY[0]
set_instance_assignment -name IO_STANDARD "3.3 V SCHMITT TRIGGER" -to KEY[0]

# ------------------------------------------------------------
# HEX0 - 7 Segment Display
# Active Low
# Currently unused by uart_fpga_top
# ------------------------------------------------------------
set_location_assignment PIN_C14 -to HEX0[0]
set_location_assignment PIN_E15 -to HEX0[1]
set_location_assignment PIN_C15 -to HEX0[2]
set_location_assignment PIN_C16 -to HEX0[3]
set_location_assignment PIN_E16 -to HEX0[4]
set_location_assignment PIN_D17 -to HEX0[5]
set_location_assignment PIN_C17 -to HEX0[6]

set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to HEX0[0]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to HEX0[1]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to HEX0[2]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to HEX0[3]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to HEX0[4]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to HEX0[5]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to HEX0[6]

# ------------------------------------------------------------
# Red LEDs (LEDR0 - LEDR9)
# LEDR[7:0] = Received UART Data
# LEDR[8]   = UART Framing Error
# LEDR[9]   = OFF
# ------------------------------------------------------------
set_location_assignment PIN_A8  -to LEDR[0]
set_location_assignment PIN_A9  -to LEDR[1]
set_location_assignment PIN_A10 -to LEDR[2]
set_location_assignment PIN_B10 -to LEDR[3]
set_location_assignment PIN_D13 -to LEDR[4]
set_location_assignment PIN_C13 -to LEDR[5]
set_location_assignment PIN_E14 -to LEDR[6]
set_location_assignment PIN_D14 -to LEDR[7]
set_location_assignment PIN_A11 -to LEDR[8]
set_location_assignment PIN_B11 -to LEDR[9]

set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to LEDR[0]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to LEDR[1]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to LEDR[2]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to LEDR[3]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to LEDR[4]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to LEDR[5]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to LEDR[6]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to LEDR[7]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to LEDR[8]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to LEDR[9]

# ------------------------------------------------------------
# UART Pin-Level Loopback
# DE10-Lite Arduino Header
# Connect Arduino IO1 to IO0 with one jumper wire
# ------------------------------------------------------------

# UART RX - Arduino IO0
set_location_assignment PIN_AB5 -to ARDUINO_IO[0]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to ARDUINO_IO[0]
set_instance_assignment -name WEAK_PULL_UP_RESISTOR ON -to ARDUINO_IO[0]

# UART TX - Arduino IO1
set_location_assignment PIN_AB6 -to ARDUINO_IO[1]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to ARDUINO_IO[1]

# ============================================================
# End of Pin Assignments
# ============================================================