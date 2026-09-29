# FeCIM Component Spec: UART Transceiver

**Module:** `uart_tx`, `uart_rx`, `baud_gen`
**Owner:** Lane A (RTL)
**Milestone:** Week 2 (simulation), Week 3 (hardware gate)
**Status:** draft — freeze before week 2

---

## 1. Purpose and scope

Byte-level serial transport between the host PC and the FPGA. This module knows nothing
about packets, commands, or CRC — it moves bytes in and bytes out. All framing above the
byte level belongs to `packet_parser`.

Deliberately excluded: FIFOs, flow control (RTS/CTS), parity, interrupt logic, multi-byte
buffering.

---

## 2. Physical layer

### 2.1 Wiring

The DE10-Lite mini-USB port is USB-Blaster (JTAG) only — there is no USB-UART bridge on
the board. A CP2102 or CH340 breakout connects to the Arduino Uno R3 expansion header.

| Dongle pin | DE10-Lite | Signal | Notes |
|---|---|---|---|
| TXD | `ARDUINO_IO[0]` | `rxd` (FPGA input) | crossover — dongle TX to FPGA RX |
| RXD | `ARDUINO_IO[1]` | `txd` (FPGA output) | crossover |
| GND | GND | — | **required**, common reference |
| VCC | *do not connect* | — | power the dongle from USB |

### 2.2 Voltage — read before wiring

The Arduino header I/O bank on DE10-Lite is **3.3 V**. Many CP2102 and CH340 breakouts
ship configured for 5 V logic or have a solder jumper selecting it. Applying 5 V to a
MAX 10 I/O pin can damage it permanently.

Before connecting anything: set the dongle jumper to 3.3 V, then confirm with a meter
that its idle TX line sits at ~3.3 V, not ~5 V. Leave the dongle's VCC pin
disconnected — it draws power from USB, and tying two supplies together buys nothing but
risk.

### 2.3 Pin assignments

```tcl
# Verify against the DE10-Lite User Manual pin table before building —
# do not trust these from memory.
set_location_assignment PIN_AB5 -to rxd    ;# ARDUINO_IO[0]
set_location_assignment PIN_AB6 -to txd    ;# ARDUINO_IO[1]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to rxd
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to txd
set_instance_assignment -name WEAK_PULL_UP_RESISTOR ON -to rxd
```

The weak pull-up on `rxd` matters: with the dongle unplugged the line floats, and a
floating input will produce spurious start-bit detections. Pulled high, a disconnected
line reads as idle.

### 2.4 Frame format

8N1 — one start bit (low), eight data bits LSB first, one stop bit (high), no parity.
Idle state is high. Ten bit-times per byte.

---

## 3. Baud generation

### 3.1 Why not a plain integer divider

The obvious approach — count `f_clk / (16 × baud)` cycles per oversample tick — works
acceptably at 115200 and badly everywhere else:

| Target baud | Integer divisor | Actual baud | Error |
|---|---|---|---|
| 115,200 | 27 | 115,741 | +0.47% |
| 230,400 | 14 | 223,214 | −3.1% |
| 460,800 | 7 | 446,429 | −3.1% |
| 921,600 | 3 | 1,041,667 | **+13%** |

Anything past 0.47% error starts eating the sampling margin, and 921600 is unusable. Since
you'll want to raise the baud rate in week 8 to cut the 360 ms weight-load time, design
for it now.

### 3.2 Fractional accumulator

A 16-bit phase accumulator, incremented every clock, emitting a tick on overflow:

```verilog
reg [16:0] acc;                       // 17 bits: 16 phase + carry out
wire       tick_16x = acc[16];

always @(posedge clk or negedge rst_n)
    if (!rst_n) acc <= 17'd0;
    else        acc <= {1'b0, acc[15:0]} + baud_inc;
```

The increment is `baud_inc = round(2^16 × 16 × baud / f_clk)`:

| Target baud | `baud_inc` | Actual baud | Error |
|---|---|---|---|
| 115,200 | 2416 | 115,204 | +0.003% |
| 230,400 | 4832 | 230,408 | +0.003% |
| 460,800 | 9664 | 460,816 | +0.003% |
| 921,600 | 19327 | 921,583 | −0.002% |

Cost is one 16-bit adder and register, roughly 35 LEs, in exchange for essentially exact
baud at any rate. `baud_inc` becomes a config register, so the host can renegotiate speed
at runtime without a rebuild.

**Jitter.** The fractional divider means individual tick intervals vary by up to one clock
(20 ns) while the long-run average is exact. Since ticks are only counted to sixteen and
the sample lands mid-bit, the jitter never accumulates. Harmless.

### 3.3 Shared generator, independent phase

One free-running `baud_gen` feeds both TX and RX. RX keeps its own modulo-16 phase
counter that it **resets on start-bit detection**, so its sampling phase aligns to the
incoming byte rather than to the free-running tick. Without that reset you inherit up to
one tick (6.25% of a bit) of arbitrary phase error on every byte.

---

## 4. `uart_rx`

### 4.1 Ports

| Signal | Dir | Width | Description |
|---|---|---|---|
| `clk` | in | 1 | 50 MHz |
| `rst_n` | in | 1 | async assert, sync deassert |
| `tick_16x` | in | 1 | from `baud_gen` |
| `rxd` | in | 1 | **asynchronous** — from pin |
| `rx_data` | out | 8 | valid when `rx_valid` |
| `rx_valid` | out | 1 | single-cycle strobe |
| `rx_frame_err` | out | 1 | single-cycle, stop bit was low |

### 4.2 Input synchronization

`rxd` crosses from an unrelated clock domain (the dongle's) and **must** be synchronized
before use. Three flops, not two — the extra stage is nearly free and the first stage of a
2-FF synchronizer on a slow asynchronous input is the classic source of intermittent
byte corruption that shows up as a 1-in-10,000 failure rate two weeks before demo day.

```verilog
reg [2:0] rx_sync;
always @(posedge clk) rx_sync <= {rx_sync[1:0], rxd};
wire rx_bit = rx_sync[2];
```

Mark this path with a false-path or `set_max_delay` constraint in the SDC so the fitter
does not try to time it.

### 4.3 Sampling with majority voting

Each bit spans sixteen ticks. Rather than sampling once at tick 8, sample at ticks 7, 8,
and 9 and take the majority. This is what 16550-family UARTs do, and it rejects
single-tick glitches from cable noise for the cost of about 10 LEs.

```
tick:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
                              ^  ^  ^
                              └──┴──┴── majority vote
```

### 4.4 State machine

```
  IDLE ──(rx_bit falls)──► START ──(vote@mid = 0)──► DATA
    ▲                        │                        │
    │                        │ (vote@mid = 1)         │ (8 bits)
    │                        │  false start           ▼
    │◄───────────────────────┘                      STOP
    │                                                 │
    └──── rx_valid / rx_frame_err ◄───────────────────┘
```

- **IDLE** — wait for a high-to-low transition. Reset phase counter on detection.
- **START** — sample at mid-bit. If the line has returned high it was a glitch, not a
  start bit; return to IDLE without error. This rejects noise on an idle line.
- **DATA** — eight bits, LSB first, shifted into `rx_data` from the MSB end.
- **STOP** — sample mid-bit. High means a clean frame, assert `rx_valid`. Low means a
  framing error: assert `rx_frame_err`, and still return to IDLE rather than hunting for
  the next edge. Deliberately simple — the packet layer's CRC catches corruption, and
  the parser resynchronizes on the sync byte.

### 4.5 Error budget

A byte takes ten bit-times. The last bit is sampled 9.5 bit-times after the start edge, so
sampling stays inside the correct bit as long as cumulative timing error stays under half
a bit — about 5% per bit. With 0.003% baud error and 6.25% worst-case initial phase error
from tick quantization, total worst case is roughly 6.3%, comfortably inside the window.
The 3% baud error of the naive integer divider at 230400 would have consumed most of that
margin.

---

## 5. `uart_tx`

### 5.1 Ports

| Signal | Dir | Width | Description |
|---|---|---|---|
| `clk` | in | 1 | 50 MHz |
| `rst_n` | in | 1 | |
| `tick_16x` | in | 1 | from `baud_gen` |
| `tx_data` | in | 8 | captured on `tx_start` |
| `tx_start` | in | 1 | single-cycle, ignored while busy |
| `tx_busy` | out | 1 | high from load to stop-bit completion |
| `txd` | out | 1 | to pin, idle high |

### 5.2 Operation

On `tx_start` with `tx_busy` low, load a 10-bit shift register with
`{1'b1, tx_data[7:0], 1'b0}` — stop bit, data, start bit — and shift out LSB first, one
bit every sixteen ticks. `txd` is the shift register LSB. Assert `tx_busy` throughout.

`txd` must reset to 1. A reset that drives it low looks like a start bit to the host and
will produce a spurious byte on every reset.

### 5.3 No FIFO

At 115200 baud a byte occupies 86.8 µs — 4,340 clock cycles. Every consumer and producer
in this design operates in single-digit cycles, so a simple `tx_busy` handshake is
sufficient and a FIFO would add area and a second failure mode for no benefit. The
readout FSM waits on `tx_busy` between bytes.

Revisit only if you later add a streaming mode that must sustain back-to-back bytes with
zero inter-byte gap. You will not need this.

---

## 6. Resource and timing

| Block | LEs (est.) | M9K | Mult |
|---|---|---|---|
| `baud_gen` | 35 | 0 | 0 |
| `uart_rx` | 60 | 0 | 0 |
| `uart_tx` | 45 | 0 | 0 |
| **Total** | **~140** | **0** | **0** |

About 0.3% of the 10M50. Longest combinational path is the 16-bit accumulator carry
chain — trivially inside a 20 ns period. Expect first-pass timing closure; if this module
fails timing, something is wrong elsewhere.

---

## 7. Verification

Lane B owns items 1–5; Lane A owns 6–7.

1. **Bit-banged RX.** Testbench drives `rxd` at exactly 115200 with a known byte; check
   `rx_data` and single-cycle `rx_valid`. Sweep all 256 values.
2. **TX waveform.** Capture `txd`, measure bit periods, confirm 8N1 framing and LSB-first
   ordering.
3. **Loopback.** Wire `txd` to `rxd` in simulation, stream 1,000 random bytes, assert
   exact recovery.
4. **Baud tolerance.** Drive `rxd` at ±2%, ±3%, ±4% of nominal. Should pass through ±3%
   at minimum. This test is what proves the error budget in §4.5 is real rather than
   arithmetic on paper.
5. **Fault injection.** (a) Single-tick glitch mid-bit — majority vote must reject it.
   (b) Short low pulse on an idle line — must be rejected as a false start, no
   `rx_valid`. (c) Stop bit driven low — `rx_frame_err` asserts, `rx_valid` does not.
6. **Hardware loopback.** Jumper `ARDUINO_IO[0]` to `ARDUINO_IO[1]`, no dongle. Python
   is not involved. Confirms pin assignment and I/O standard independently of the host,
   which is worth doing *before* the dongle arrives.
7. **Hardware round trip.** Real dongle, 10,000 random bytes echoed through Python,
   zero errors. Repeat at every baud rate you intend to support.

---

## 8. Week 3 gate

The project's first hard checkpoint. All of the following must hold:

- [ ] Dongle wired, verified at 3.3 V logic levels
- [ ] Hardware loopback (test 6) passes with a jumper
- [ ] Python `pyserial` sends a byte; the board echoes it
- [ ] 10,000-byte round trip at 115200 with zero errors
- [ ] HEX display shows RX byte count, LEDR shows activity
- [ ] Framing errors counted and visible

**If this has not passed by end of week 4, escalate to the whole team.** Everything
downstream — parser, config registers, weight loading, every sweep — depends on this
link working. The fallback is JTAG-to-Avalon via System Console, which costs you the
clean Python driver and a week of TCL, so it is worth several days of debugging to avoid.

---

## 9. Open questions for the freeze meeting

1. Is `baud_inc` a config register (runtime-settable) or a synthesis parameter? Runtime
   is more flexible but creates a chicken-and-egg problem — the host must already be
   talking at the old rate to change it. Suggest: parameter for the default, register for
   overrides, with a fixed 115200 fallback on reset.
2. Should `rx_frame_err` increment a counter readable over `IDENTIFY`? Cheap, and very
   useful when debugging cable and baud problems in week 3.
3. Do we want a break-detect (line low longer than one frame) as a hardware resynchronize
   trigger? Probably unnecessary given CRC and sync-byte recovery at the packet layer.
