# FeCIM Component Spec: Top Level and Integration

**Module:** `fecim_top`, `display_driver`, `reset_sync`
**Owner:** Lane A (Zhiting Li); §7 jointly owned with Lane B
**Milestone:** skeleton week 1, integration week 4, constraints and docs week 11
**Status:** draft

---

## 1. Purpose and scope

The module that instantiates everything, the physical pin contract, the clock and reset
scheme, the timing constraints, the on-board display, and the integration testbench that
spans the A/B seam.

This is the only module that touches device pins.

---

## 2. Port list

```systemverilog
module fecim_top import fecim_pkg::*; (
    input  logic        MAX10_CLK1_50,
    input  logic [1:0]  KEY,
    input  logic [9:0]  SW,
    output logic [9:0]  LEDR,
    output logic [7:0]  HEX0, HEX1, HEX2, HEX3, HEX4, HEX5,
    input  logic        ARDUINO_IO0,     // rxd
    output logic        ARDUINO_IO1      // txd
);
```

Unused pins stay unassigned. Quartus defaults unused pins to input tri-stated with a weak
pull-up, which is the safe setting — do not change it globally.

---

## 3. Clock and reset

### 3.1 One clock domain

Single 50 MHz domain from `MAX10_CLK1_50`. **No PLL.**

The device has four PLLs and a second 50 MHz input on `PIN_N14`. Neither is used: the
fractional baud generator removes the only reason to synthesize a UART-friendly clock, and
a single domain removes every clock-domain-crossing concern except the one in §3.3.

A PLL becomes relevant only if the `N+2` datapath path fails timing and the array clock has
to drop below 50 MHz. That is a fallback, not a plan.

### 3.2 Reset

`KEY0` is active-low and **already hardware-debounced** on the board with a Schmitt-trigger
input, so no debounce logic is needed. Synchronize it anyway — debouncing and metastability
protection are different problems and the board solves only the first.

```systemverilog
logic [1:0] rst_pipe;
logic       rst_n;

always_ff @(posedge clk or negedge KEY[0]) begin
    if (!KEY[0]) rst_pipe <= '0;
    else         rst_pipe <= {rst_pipe[0], 1'b1};
end
assign rst_n = rst_pipe[1];
```

Asynchronous assert, synchronous deassert. Assert immediately on the button, release in
step with the clock so no register sees a reset edge mid-cycle.

**Reset values that matter:** `txd` must reset to 1 and the RX synchronizer must reset to
all-ones. The line idles high, so resetting either low presents a falling edge on release —
indistinguishable from a start bit, producing one garbage byte after every reset.

### 3.3 The only clock domain crossing

`ARDUINO_IO0` (`rxd`) arrives from the dongle's unrelated clock. Three-flop synchronizer in
`uart_rx` per `uart-spec.md` §4.2. Constrain it as a false path (§5.3) so the fitter does
not attempt to time it.

`KEY` and `SW` are asynchronous but not timing-critical; two-flop synchronizers suffice.

---

## 4. Pin assignments

Verified against DE10-Lite User Manual v1.6.

| Signal | Pin | I/O standard | Notes |
|---|---|---|---|
| `MAX10_CLK1_50` | `PIN_P11` | 3.3-V LVTTL | 50 MHz |
| `KEY[0]` | `PIN_B8` | 3.3 V Schmitt Trigger | reset, debounced |
| `KEY[1]` | `PIN_A7` | 3.3 V Schmitt Trigger | manual compute |
| `ARDUINO_IO0` | `PIN_AB5` | 3.3-V LVTTL | `rxd`, **weak pull-up ON** |
| `ARDUINO_IO1` | `PIN_AB6` | 3.3-V LVTTL | `txd` |
| `SW[9:0]` | see `hardware.md` | 3.3-V LVTTL | display select |
| `LEDR[9:0]` | see `hardware.md` | 3.3-V LVTTL | status |
| `HEX0`–`HEX5` | see `hardware.md` | 3.3-V LVTTL | 8 bits each, **active low** |

The weak pull-up on `rxd` is not cosmetic: with the dongle unplugged the line floats, and a
floating input produces spurious start-bit detections.

Full `.qsf` assignments live in `quartus/fecim.qsf` and are reproduced in `hardware.md` for
replication.

---

## 5. Timing constraints

`quartus/fecim.sdc`:

```tcl
create_clock -name clk -period 20.000 [get_ports MAX10_CLK1_50]
derive_clock_uncertainty

# Asynchronous inputs — no timing relationship to clk
set_false_path -from [get_ports ARDUINO_IO0]
set_false_path -from [get_ports {KEY[*]}]
set_false_path -from [get_ports {SW[*]}]

# Outputs with no external timing requirement
set_false_path -to [get_ports {LEDR[*]}]
set_false_path -to [get_ports {HEX0[*] HEX1[*] HEX2[*] HEX3[*] HEX4[*] HEX5[*]}]
set_false_path -to [get_ports ARDUINO_IO1]
```

`txd` is a false path because the receiver samples at 16× oversampling with majority
voting — a nanosecond of output skew is irrelevant against an 8.7 µs bit period.

### 5.1 The path to watch

Cycle `N+2` in each MAC lane: a 9-bit add followed by a 9×9 signed multiply, in one 20 ns
period. On a `C7` speed grade this should close with the DSP block contributing roughly
5–6 ns, but it is the tightest path in the design.

**If it fails:** register between the add and the multiply and set
`LANE_PIPE_DEPTH = 4` in `fecim_pkg.sv`. The control FSM reads the constant from the
package, so nothing else changes.

### 5.2 Check at every milestone

Commit the timing report to `quartus/reports/` on every tagged release. Slack that is
shrinking as lanes are added is a signal worth catching at 32 lanes rather than 64.

---

## 6. Display driver

Not decorative. This is the physical interface M0 asks for in an Infrastructure project,
and the only debug channel that still works when the serial link is the thing that is
broken.

### 6.1 The displays are active-low

Common anode: a segment turns **on** when driven low. Written the natural way you get a
photographic negative — every segment inverted, which is legible enough to be confusing
rather than obviously broken.

```systemverilog
assign HEX0 = ~{dp0, seg_pattern(nibble0)};   // bit 7 is the decimal point
```

Eight bits per display, bit 7 being the decimal point. Six displays, 48 pins.

### 6.2 Assignments

| Element | Shows |
|---|---|
| `HEX5`–`HEX4` | Control FSM state (hex), from `ctrl_state_e` |
| `HEX3`–`HEX0` | Selected by `SW[1:0]`: `00` RX byte count · `01` `result[0]` low 16 bits · `10` argmax index · `11` error counters |
| `HEX0[7]` (DP) | Heartbeat — toggles once per second |
| `LEDR[9]` | Compute busy |
| `LEDR[8]` | Error latched since reset |
| `LEDR[7]` | UART RX activity (stretched to be visible) |
| `LEDR[6:0]` | `pass_idx` and row counter activity |

The heartbeat on the decimal point costs almost nothing and distinguishes "hung" from
"idle" at a glance — worth more than it sounds during week-3 bring-up.

### 6.3 `KEY1` — host-independent compute

Pressing `KEY1` triggers a `COMPUTE` on whatever weights and activations are loaded, and
displays the argmax. No host involved.

This is the demo fallback M0 requires. If the laptop link dies mid-presentation, a
preloaded board still computes and displays a result.

---

## 7. Module hierarchy and the A/B seam

```
fecim_top                          ← Lane A
├── reset_sync                     ← Lane A
├── baud_gen                       ← Lane A
├── uart_rx / uart_tx              ← Lane A
├── packet_parser / packet_tx      ← Lane A
├── control_fsm                    ← Lane A
├── config_regs                    ← Lane A
├── readout_ser                    ← Lane A
├── display_driver                 ← Lane A
│   ─────────── A/B seam ───────────
├── write_path                     ← Lane B
├── act_buffer                     ← Lane B
├── mac_array                      ← Lane B
│   └── mac_lane × NUM_LANES
├── adc_quant                      ← Lane B
└── result_buffer / argmax_unit    ← Lane B
```

### 7.1 Seam signal list — frozen week 1

| Direction | Signals |
|---|---|
| A → B | `seq_cnt[SEQ_AW-1:0]`, `act_addr[ROW_AW-1:0]`, `acc_clear`, `acc_en`, `shift_en`, `noise_en` (packed struct), `bulk_we`, `bulk_target[1:0]`, `bulk_addr[15:0]`, `bulk_data[7:0]`, all config register values |
| B → A | `result_rdata[ACC_W-1:0]`, `result_raddr[COL_AW-1:0]`, `argmax_idx`, `argmax_val`, `argmax_second`, `results_valid` |

A memory plus control strobes — the cleanest kind of boundary, testable from both sides in
isolation.

### 7.2 The integration testbench is jointly owned

Zhiting and Megan **write this together**. It is the one artifact neither lane owns alone,
and a seam nobody owns is a seam nobody tests — the classic four-person failure.

`tb/sv/tb_seam.sv` drives the A side with a scripted sequence and checks the B side's
response, and vice versa with B stubbed:

1. **A alone, B stubbed.** A memory model responds to `bulk_we`; assert addresses increment
   correctly across a chunk boundary.
2. **B alone, A stubbed.** Drive `seq_cnt` and strobes directly from the testbench; assert
   the accumulators and drain ordering.
3. **Both, clean.** One-hot activation with an identity matrix; assert the non-zero column
   matches. Sweep the one-hot position across all rows.
4. **Both, full.** Random matrix and vector; compare against the C++ model through
   Verilator.

Test 3 localizes an off-by-one to a specific side in a way test 4 cannot.

---

## 8. Build and release

### 8.1 Toolchain

Quartus Prime Lite **v16.0 or later** for MAX 10 support. Pin the exact version in the
README so all four members build identically.

### 8.2 Release artifacts

On each tag:

| Artifact | Destination |
|---|---|
| `fecim.sof` | GitHub Release, not the repo |
| Fitter report | `quartus/reports/` |
| Timing report | `quartus/reports/` |
| Resource summary | README table |
| Python wheels | GitHub Release |

Bump `BUILD_ID` in `fecim_pkg.sv` on every release so `IDENTIFY` reports which bitstream is
running. This is how "the numbers are wrong" becomes "your bitstream is stale."

### 8.3 Check the fitter report for DSP packing

Look for `Embedded Multiplier 9-bit elements`. At 64 lanes the expected count is 72 blocks.
**If it reports roughly 136, pairing failed** — check operand widths and signedness before
investigating anything else.

---

## 9. Resource rollup

| Block | LEs | M9K | Mult blocks |
|---|---|---|---|
| UART + baud gen | 140 | — | — |
| Packet parser + tx | 400 | — | — |
| Control FSM + config regs | 1,450 | — | — |
| Readout serializer | 90 | — | — |
| Display driver | 180 | — | — |
| `reset_sync` + top glue | 60 | — | — |
| Write path | 380 | — | 6 |
| Activation buffer | 70 | 1 | — |
| MAC array (64 lanes) | 15,296 | 64 | 64 |
| ADC quantizer | 160 | — | — |
| IR attenuation | 80 | — | 1 |
| Result buffer + argmax | 270 | 1 | — |
| **Total** | **~18,580 (37%)** | **66 (36%)** | **71 (49%)** |

Against 49,760 LEs, 182 M9K, 144 multiplier blocks.

---

## 10. Verification

1. **Reset behaviour.** Assert `KEY0` mid-compute; confirm clean recovery, `txd` high
   throughout, and no spurious byte on release.
2. **Synchronizer constraints.** Confirm the SDC false paths are recognized in the timing
   report, not silently ignored due to a name mismatch.
3. **Display polarity.** Drive a known pattern and photograph the board. The only test that
   catches inverted segments, and worth doing once in week 1.
4. **`KEY1` standalone compute.** Load weights and activations over serial, disconnect the
   dongle, press `KEY1`, confirm the argmax appears on HEX. This is the demo fallback —
   rehearse it, do not assume it.
5. **Seam tests 1–4** from §7.2.
6. **Timing closure** at 32 and 64 lanes, with reports committed.
7. **Full-chip soak.** 100,000 random packets overnight in week 4; check error counters at
   the end. Finds the once-per-50,000 synchronizer bug no directed test will.

---

## 11. Open questions

1. **Should `SW[9:2]` do anything?** Eight free switches. Candidates: force `NOISE_EN` bits
   without a host, select a preset parameter set, or trigger self-test. Cheap, and useful if
   the demo has to run standalone.
2. **`SELF_TEST` command?** Load a known matrix from a ROM, compute, compare against a
   stored expected result. About 100 LEs plus one M9K, and it makes "is the board working"
   answerable in one second while standing in front of the class.
3. **Second clock domain for the UART?** Not needed with the fractional baud generator, but
   would isolate the serial interface if the array clock ever has to change. Adds a real CDC
   and should be avoided unless forced.
