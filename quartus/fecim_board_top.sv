//=============================================================================
// fecim_board_top.sv -- Lane A
//
// Board-level top module for Terasic DE10-Lite (10M50DAF484C7G).
// Wraps fecim_top with device pins per quartus/pins.tcl:
//   - MAX10_CLK1_50:  50 MHz on-board oscillator (PIN_P11)
//   - KEY[0]:         Active-low push button, reset through reset_sync (PIN_B8)
//   - KEY[1]:         Active-low push button, manual compute trigger (PIN_A7)
//   - ARDUINO_IO[0]:  UART RX input (PIN_AB5, weak pull-up enabled)
//   - ARDUINO_IO[1]:  UART TX output (PIN_AB6)
//   - LEDR[9:0]:      Status LEDs
//   - HEX0[6:0]:      Active-low 7-segment display
//=============================================================================

`timescale 1ns/1ps

module fecim_board_top
    import fecim_pkg::*;
(
    input  wire        MAX10_CLK1_50,
    input  wire [1:0]  KEY,
    output wire [9:0]  LEDR,
    output wire [6:0]  HEX0,
    inout  wire [15:0] ARDUINO_IO
);

    wire uart_rxd;
    wire uart_txd;
    wire [7:0] hex0_full;

    // ARDUINO_IO[0] is UART RX (input from host / USB-UART dongle)
    assign uart_rxd         = ARDUINO_IO[0];

    // ARDUINO_IO[1] is UART TX (output to host / USB-UART dongle)
    assign ARDUINO_IO[1]    = uart_txd;

    // Remaining Arduino header pins left undriven (tri-stated)
    assign ARDUINO_IO[15:2] = 14'bz;

    // HEX0[6:0] connects active-low 7 segments (ignoring DP bit 7)
    assign HEX0 = hex0_full[6:0];

    // Core FeCIM design instantiation
    fecim_top u_fecim_top (
        .MAX10_CLK1_50 (MAX10_CLK1_50),
        .KEY           (KEY),
        .SW            (10'd0),
        .LEDR          (LEDR),
        .HEX0          (hex0_full),
        .HEX1          (),
        .HEX2          (),
        .HEX3          (),
        .HEX4          (),
        .HEX5          (),
        .ARDUINO_IO0   (uart_rxd),
        .ARDUINO_IO1   (uart_txd)
    );

endmodule : fecim_board_top
