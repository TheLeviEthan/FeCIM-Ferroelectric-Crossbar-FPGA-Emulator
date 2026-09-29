# FeCIM Hardware Documentation

**Owner:** Lane A (Zhiting Li)
**Purpose:** M0 requires hardware documentation sufficient to replicate the device exactly.
This is that document.
**Milestone:** skeleton week 3, complete week 11
**Status:** draft

---

## 1. Bill of materials

| Item | Qty | Notes |
|---|---|---|
| Terasic DE10-Lite (MAX 10 `10M50DAF484C7G`) | 1 | Provided by the department |
| CP2102 or CH340 USB-UART module | 1 | **Must support 3.3 V logic** |
| Female-to-female jumper wires | 3 | RX, TX, GND |
| USB-A to mini-B cable | 1 | For USB-Blaster / programming |
| USB cable for the dongle | 1 | Type depends on the module |

Total added cost: roughly $8. Order two dongles — the week-3 bring-up gate is the project's
highest-risk milestone and a dead unit would stall it.

---

## 2. Wiring

### 2.1 Voltage check first

**The Arduino header I/O bank on the DE10-Lite is 3.3 V. Applying 5 V can permanently
damage a MAX 10 I/O pin.**

Many CP2102 and CH340 modules ship configured for 5 V logic or carry a solder jumper
selecting it. Before connecting anything:

1. Set the module's jumper to 3.3 V.
2. Power the module from USB, with nothing connected to the FPGA.
3. Measure its idle TX pin against its GND. **It must read approximately 3.3 V, not 5 V.**
4. Only then connect the jumpers.

### 2.2 Connections

| Dongle pin | DE10-Lite | FPGA signal | Pin |
|---|---|---|---|
| TXD | `ARDUINO_IO[0]` | `rxd` (input) | `PIN_AB5` |
| RXD | `ARDUINO_IO[1]` | `txd` (output) | `PIN_AB6` |
| GND | any header GND | — | — |
| VCC | **do not connect** | — | — |

Note the crossover: the dongle's transmit goes to the FPGA's receive. The header carries the
`RXD`/`TXD` labels in Arduino convention, which confirms the orientation.

Leave the dongle's VCC disconnected. It draws power from USB, and tying two supplies
together buys nothing but risk.

```
   ┌──────────────┐                    ┌─────────────────────┐
   │  CP2102      │                    │  DE10-Lite          │
   │              │                    │  Arduino header     │
   │  TXD ────────┼────────────────────┼──► IO0  (PIN_AB5)   │
   │  RXD ◄───────┼────────────────────┼─── IO1  (PIN_AB6)   │
   │  GND ────────┼────────────────────┼─── GND              │
   │  VCC   (nc)  │                    │                     │
   └──────┬───────┘                    └──────┬──────────────┘
          │ USB to host                       │ mini-USB (JTAG only)
```

**The mini-USB port is USB-Blaster (JTAG) only** — there is no USB-UART bridge on the
board. That is the entire reason the dongle is needed.

---

## 3. Pin assignments

Reproduced here for replication. Authoritative source is `quartus/fecim.qsf`.

### 3.1 Clock, buttons, serial

| Signal | Pin | I/O standard |
|---|---|---|
| `MAX10_CLK1_50` | `PIN_P11` | 3.3-V LVTTL |
| `KEY[0]` (reset) | `PIN_B8` | 3.3 V Schmitt Trigger |
| `KEY[1]` (manual compute) | `PIN_A7` | 3.3 V Schmitt Trigger |
| `ARDUINO_IO0` (`rxd`) | `PIN_AB5` | 3.3-V LVTTL, weak pull-up **ON** |
| `ARDUINO_IO1` (`txd`) | `PIN_AB6` | 3.3-V LVTTL |

`KEY[0]` and `KEY[1]` are hardware-debounced on the board via Schmitt-trigger inputs — no
RTL debounce needed, though metastability synchronizers are still required.

### 3.2 Switches, LEDs, displays

`SW[9:0]`, `LEDR[9:0]`, and `HEX0`–`HEX5` assignments are copied verbatim from the
DE10-Lite User Manual v1.6 pin tables into `fecim.qsf`. **Copy them from the manual rather
than from memory** — the HEX tables in particular are long and easy to transpose.

**The 7-segment displays are common anode and active-low:** a segment turns on when driven
low. Each is 8 bits wide, with bit 7 being the decimal point.

---

## 4. Building the bitstream

### 4.1 Toolchain

Quartus Prime Lite **v16.0 or later** (MAX 10 support). The exact version used is pinned in
the repository README; all team members build with the same one.

### 4.2 Steps

```
1. Open quartus/fecim.qpf in Quartus Prime Lite
2. Processing → Start Compilation          (or: quartus_sh --flow compile fecim)
3. Confirm zero timing violations in the TimeQuest summary
4. Output: output_files/fecim.sof
```

### 4.3 Verify the fitter report

Two checks that catch the design's two riskiest assumptions:

**DSP packing.** Search the fitter report for `Embedded Multiplier 9-bit elements`. At 64
lanes the expected count is **72 blocks**. If it reports roughly 136, two 9×9 multiplies
failed to share a block — check operand widths and signedness before investigating
anything else.

**Timing slack.** The tightest path is cycle `N+2` in each MAC lane (9-bit add plus 9×9
multiply). Positive slack required at 50 MHz. If negative, apply the
`LANE_PIPE_DEPTH = 4` fallback from `top-level-spec.md` §5.1.

Commit both reports to `quartus/reports/` on every tagged release.

---

## 5. Programming the board

1. Connect the mini-USB cable to the host.
2. Tools → Programmer, confirm **USB-Blaster** appears as the hardware.
3. Add `output_files/fecim.sof`, check Program/Configure, Start.

Command line:

```bash
quartus_pgm -m jtag -o "p;output_files/fecim.sof"
```

`.sof` is volatile and clears on power cycle. For a persistent demo, convert to `.pof` and
program the configuration flash — worth doing before the presentation so the board comes up
ready.

---

## 6. Bring-up checklist

In order. Each step isolates one failure mode, so do not skip ahead.

- [ ] **1. Blink.** A bitstream toggling `LEDR[0]` at 1 Hz. Confirms clock, programming
      flow, and pin assignment mechanics.
- [ ] **2. HEX pattern.** Display a known digit on `HEX0`. Photograph it. Catches inverted
      polarity — the only test that does.
- [ ] **3. Hardware loopback, no dongle.** Jumper `ARDUINO_IO[0]` directly to
      `ARDUINO_IO[1]`. Send a byte from the internal UART and confirm it returns. **Validates
      pin assignment and I/O standard without involving the dongle or the host**, so it can
      be done while waiting on shipping.
- [ ] **4. Dongle voltage check.** §2.1, before connecting.
- [ ] **5. Loopback through the dongle.** Short the dongle's own TX and RX; confirm the host
      sees its own bytes. Proves the dongle and its driver work.
- [ ] **6. `IDENTIFY` round trip.** Python sends the command, the board responds with magic
      `0xFEC1`. **This is the week-3 gate.**
- [ ] **7. Soak.** 10,000 random bytes echoed, zero errors. Repeat at every baud rate
      intended for use, including 921600.

---

## 7. Troubleshooting

| Symptom | Likely cause |
|---|---|
| No response at all | TX/RX not crossed over; or dongle TX wired to `IO1` instead of `IO0` |
| Garbage bytes, consistent pattern | Baud mismatch — check `BAUD_INC` against the host setting |
| One garbage byte after every reset | `txd` or the RX synchronizer resetting low instead of high |
| Occasional corrupted bytes | Missing or shallow RX synchronizer; ground not connected |
| Every display segment inverted | HEX driver not inverted — they are active-low |
| CRC errors rising, framing errors zero | Parser or CRC logic, not baud rate — read the `IDENTIFY` counters |
| Framing errors rising | Baud rate or cable; try 115200 |
| Works at 115200, fails at 921600 | Cable length or dongle limit — fall back and note it |
| `VersionMismatch` from the driver | Stale bitstream; reprogram and check `BUILD_ID` |
| Board unresponsive after a bad packet | Parser timeout not implemented; press `KEY0` |

The three error counters in the `IDENTIFY` response are the primary debug instrument and
make most of the above diagnosable without a scope.

---

## 8. Replication statement

Reproducing this device requires: one DE10-Lite, one 3.3 V USB-UART module, three jumper
wires, the `.sof` from a tagged release (or a build from `quartus/fecim.qpf`), and the
Python package from the same release.

Every pin assignment is in `quartus/fecim.qsf` and reproduced in §3. Every timing
constraint is in `quartus/fecim.sdc`. The host protocol is fully specified in
`docs/protocol.md`, including the CRC reference implementation and golden byte fixtures in
`tb/golden/`.

No custom PCB, no soldering, and no components beyond those listed in §1.

---

## 9. Open questions

1. **Convert to `.pof` for the demo?** Programming the configuration flash means the board
   comes up ready after a power cycle, which removes a failure mode on presentation day.
   Worth doing in week 12.
2. **Second dongle as a spare or a second board?** If a second DE10-Lite is available,
   having a fully assembled backup is stronger insurance than a spare dongle.
3. **Photograph the wiring for the report.** M0 wants proofs of work; a labelled photo of
   the assembled setup is better replication documentation than the ASCII diagram in §2.2.
