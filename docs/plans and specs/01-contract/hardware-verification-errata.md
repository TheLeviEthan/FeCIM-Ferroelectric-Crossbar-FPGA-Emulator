# FeCIM: Hardware Verification Report and Errata

**Verified against:** DE10-Lite User Manual v1.6 (Terasic, 5 June 2020) and the Intel MAX 10
FPGA Device Architecture / Embedded Multipliers User Guide
**Date:** week 1
**Status:** one finding requires a design change; several minor corrections

---

## 1. Summary

Nine claims checked against primary sources. **Seven confirmed, two wrong.** One of the two
is significant: as specified, the 64-lane configuration would not have fit the device's
multiplier budget.

| # | Claim | Result |
|---|---|---|
| 1 | Device is 10M50DAF484C7G, ~50K LEs | ✅ confirmed |
| 2 | 182 M9K blocks (1,638 Kbit) | ✅ confirmed |
| 3 | 288 multipliers available | ❌ **wrong — see §2** |
| 4 | `ARDUINO_IO[0]` = `PIN_AB5`, `IO[1]` = `PIN_AB6` | ✅ confirmed |
| 5 | Arduino header is 3.3 V | ✅ confirmed |
| 6 | `MAX10_CLK1_50` = `PIN_P11`, 50 MHz | ✅ confirmed |
| 7 | 6 HEX displays, 10 LEDR, 2 KEY, 10 SW | ✅ confirmed, with corrections in §3 |
| 8 | No on-board USB-UART bridge | ✅ confirmed |
| 9 | 7-segment drive polarity | ❌ **wrong — see §3.1** |

---

## 2. The multiplier finding

### 2.1 What the documents say

<cite index="16-1">MAX 10 devices support up to 144 embedded multiplier blocks, and each block supports one individual 18 × 18-bit multiplier or two individual 9 × 9-bit multipliers.</cite> The DE10-Lite manual lists 144 18 × 18 multipliers for this device.

So "288 multipliers" is only true if **every operand is 9 bits or narrower**. An operand of
10 bits or more consumes an entire 18×18 block.

### 2.2 Why that breaks the current design

The MAC array spec sizes two multiplies per lane, and **both exceed 9 bits**:

| Multiply | Operands | Blocks per lane |
|---|---|---|
| MAC | `w_n` 10-bit signed × `act` 9-bit signed | **1 full 18×18** |
| Noise scaling | `ih_centered` 11-bit × `read_sigma` 16-bit | **1 full 18×18** |

At 64 lanes that is **128 blocks**, before the write path and IR attenuation. Against a
budget of 144, the design does not fit — and the resource table claiming 47% utilization
was wrong by roughly a factor of two.

This would have surfaced at the week-8 lane-scaling milestone, after the 32-lane design was
already built around 10-bit operands.

### 2.3 The fix: bring every operand to 9 bits

**Change 1 — clamp read noise at ±127 instead of ±255.**

```systemverilog
assign noise_q = (scaled >  127) ?  8'sd127 :
                 (scaled < -127) ? -8'sd127 : scaled[7:0];
```

Then `w_n = w(±127) + noise(±127) = ±254`, which is **9 bits signed**. With `act`
zero-extended to 9-bit signed, the MAC becomes a 9×9 signed multiply — two per block.

The clamp is still absurdly generous. At maximum sigma the noise standard deviation is
about 57% of full weight scale; realistic read noise is a few percent.

**Change 2 — narrow the noise-scaling multiply to 9×9.**

```systemverilog
logic signed [8:0]  ih9;        // was 11-bit ih_centered
logic signed [8:0]  sigma9;     // was 16-bit read_sigma, now Q1.8
logic signed [17:0] scaled;

assign ih9    = ih_centered >>> 1;              // ±510 → ±255
assign scaled = (ih9 * sigma9) >>> 9;           // → ±127
```

Halving `ih_centered` is a pure scale change absorbed into sigma; no distribution is lost.
`sigma9` as Q1.8 spans 0 to 0.996 with a resolution of 1/256, which gives noise-σ
granularity of about 0.29 LSB — finer than any sweep needs.

**Change 3 — the cell hash must use 16-bit arithmetic.**

The write-path spec wrote `h * 32'h9E3779B1`. A 32×32 multiply costs **four** 18×18 blocks,
so two rounds across two hash instances would have consumed 16 blocks. Use 16×16 rounds
instead — one block each, four blocks total for both hashes.

Consequence: each hash yields 16 bits rather than 32, so device-to-device variation uses
Irwin–Hall n = 2 (triangular) instead of n = 4. For a one-time-per-cell programming offset
this is acceptable; document the distribution as triangular rather than claiming Gaussian.

**Change 4 — `QUANT_MULT` becomes Q8.8, 16-bit.**

`round(255 · 2¹⁶/(N−1))` reaches 16.7 M at N = 2 — a 24-bit constant. Redefine as
`round(255 · 256/(N−1))`, which peaks at 65,280 and fits 16 bits.

### 2.4 One constraint worth knowing

<cite index="19-1">Each embedded multiplier block has only one signa and one signb signal to control the sign representation of the input data to the block.</cite> Two 9×9 multipliers sharing a block must therefore agree on operand signedness.

Every multiply in the corrected design is signed × signed — `act` and `sigma9` are
zero-extended to signed-positive — so packing is always legal. **Do not "optimize" any of
them to unsigned**; it would prevent pairing and silently double the block count.

### 2.5 Corrected multiplier budget

| Block | 32 lanes | 64 lanes |
|---|---|---|
| MAC (9×9, 2/block) | 16 | 32 |
| Noise scaling (9×9, 2/block) | 16 | 32 |
| Cell hash ×2 (16×16) | 4 | 4 |
| Quantization | 2 | 2 |
| D2D scaling (9×9) | 1 | 1 |
| IR attenuation (9×9) | 1 | 1 |
| **Total blocks** | **40 / 144 (28%)** | **72 / 144 (50%)** |

Both configurations now fit with real margin.

### 2.6 `ACC_W` stays 32

With the narrower operands the worst case is 254 × 255 × 128 = 8,290,560, which fits
24-bit signed (max 8,388,607) — by 1.2%. **That margin is too thin to rely on**, and it
disappears entirely at 256 rows. Keep `ACC_W = 32`; the M9K's native 256×32 mode makes it
free in the result buffer anyway.

---

## 3. Board-level corrections

### 3.1 The 7-segment displays are active-low

The manual is explicit: the displays are common-anode, and a segment turns **on** when
driven low and **off** when driven high.

The control FSM spec's display section did not state polarity. A driver written with the
natural "1 means lit" assumption produces a photographic negative — every segment inverted,
which is legible enough to be confusing rather than obviously broken.

```systemverilog
assign HEX0 = ~seg_pattern;   // active-low, common anode
```

### 3.2 HEX displays are 8 bits, not 7

Each is `HEXn[7:0]`, where bit 7 is the decimal point. Six displays, 48 pins. The decimal
points are available — useful as a heartbeat indicator during bring-up, which is worth more
than it sounds when you are trying to tell "hung" from "idle."

### 3.3 The push-buttons are already debounced

`KEY0` and `KEY1` use 3.3 V Schmitt-trigger inputs with hardware debouncing on the board.
**No RTL debounce logic is needed** — that is roughly 40 LEs and a counter you do not have
to write.

A two-flop synchronizer is still required. Debouncing and metastability protection are
different problems, and the board solves only the first.

### 3.4 Pin assignments confirmed

| Signal | Pin | I/O standard |
|---|---|---|
| `rxd` ← `ARDUINO_IO[0]` | `PIN_AB5` | 3.3-V LVTTL |
| `txd` → `ARDUINO_IO[1]` | `PIN_AB6` | 3.3-V LVTTL |
| `clk` ← `MAX10_CLK1_50` | `PIN_P11` | 3.3-V LVTTL |
| `rst_n` ← `KEY0` | `PIN_B8` | 3.3 V Schmitt Trigger |
| `KEY1` (manual compute) | `PIN_A7` | 3.3 V Schmitt Trigger |

The Arduino header's `IO0`/`IO1` carry the `RXD`/`TXD` labels in Arduino convention, which
confirms the crossover wiring in the UART spec: dongle TX to `IO0`, dongle RX to `IO1`.

Three GND pins are available on the header. `ARDUINO_RESET_N` is `PIN_F16` — leave it
unassigned.

### 3.5 Resources noted but unused

Worth knowing they exist, and worth **not** using them:

- **64 MB SDRAM.** Tempting for large weight storage. Resist — an SDRAM controller is a
  project in itself, and 182 M9K blocks are ample.
- **Four PLLs.** Available if the `N+2` timing path fails and you want to drop the array
  clock below 50 MHz. The fractional baud generator already removes the other reason to
  want one.
- **Second 50 MHz clock** on `PIN_N14`.
- **On-die 12-bit ADC** on the Arduino analog pins. Not relevant, but if anyone asks
  whether you could sample a real device in the loop — yes, in principle.
- **VGA output.** A live heatmap of the array would demo beautifully. Out of scope; note it
  as a second-semester possibility if the schedule allows.

### 3.6 Toolchain

Quartus II v16.0 or later is required for MAX 10 support. Pin the version in the README so
all four team members build identically.

---

## 4. Errata by document

| Document | Change |
|---|---|
| `fecim_pkg.sv` | **Regenerated** — see accompanying file |
| `uart-spec.md` | §2.3 pin table confirmed; add `KEY0`/`KEY1` pins and hardware-debounce note |
| `packet-parser-spec.md` | No change — no hardware dependencies |
| `control-fsm-spec.md` | §8: HEX is active-low and 8 bits wide; no RTL debounce needed; add `QUANT_MULT` Q8.8 to register map |
| `write-path-spec.md` | §5 hash uses 16×16 rounds; D2D becomes Irwin–Hall n = 2 (triangular); §4.1 `QUANT_MULT` redefined as Q8.8; §8 resource table corrected |
| `activation-buffer-spec.md` | §8 resource table only |
| `mac-array-spec.md` | §4.3 noise clamp ±127; `w_n` is 9-bit; `sigma9` is Q1.8; §5 multiply is 9×9; §10 resource table corrected; add the shared-signedness constraint from §2.4 |
| `result-buffer-spec.md` | No change |
| `model-verification-design.md` | `model_exact` must mirror the ±127 clamp, the `>>> 9` shift, and the 16-bit hash. Add a corner case for `adc_bits` at the new widths. |
| `fecim-plan-v2.md` | §2.3 resource table: multipliers are 50% at 64 lanes, not 47% of a wrong denominator |

---

## 5. Corrected whole-design resource summary

| Resource | 32 lanes / 64×64 | 64 lanes / 128×128 | Available |
|---|---|---|---|
| Logic elements | ~10,400 (21%) | ~18,300 (37%) | 49,760 |
| M9K blocks | 34 (19%) | 66 (36%) | 182 |
| Multiplier blocks | 40 (28%) | **72 (50%)** | 144 |

The 64-lane target remains viable. Multipliers are now correctly identified as the tightest
resource at 50%, which matters for the semester-2 differential-pair work — that doubles the
write path and may need the shared-multiplier factorization from MAC array spec §7.

---

## 6. What this exercise is worth saying in the report

The 288-versus-144 error is a good thing to document rather than quietly fix. It is a
concrete example of a specification being checked against primary sources and a design
change following from it, three weeks before any RTL depended on it.

Two of the three most consequential numbers in this project — MVM latency and multiplier
budget — were wrong on first estimate and corrected by derivation. That pattern is the
argument that the team's remaining numbers can be trusted.
