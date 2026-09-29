# FeCIM Component Spec: Control FSM and Config Register File

**Module:** `control_fsm`, `config_regs`, `result_buffer`, `display_driver`
**Owner:** Lane A (RTL), register map frozen jointly with Lanes B and C
**Depends on:** `packet_parser` (command dispatch), `mac_array` (lane control)
**Milestone:** Week 4
**Status:** draft — **register map freezes in week 1**

---

## 1. Purpose and scope

The sequencer for the whole design. Owns the config register file, drives the MVM
sequence across the lane array, arbitrates weight memory access, buffers results, and
runs the on-board display.

Everything with a state that outlives a single packet lives here. The parser is
stateless between packets; the array is a pure datapath; this module is where the machine
remembers what it is doing.

---

## 2. Correction to the earlier latency estimate

The project plan quoted 2.7 µs for a 64×64 MVM based on 128 issue cycles. Working through
the actual sequence, that number was too low — it counted only the accumulate cycles and
ignored pipeline drain and accumulator readout.

| Phase | Cycles | Note |
|---|---|---|
| Clear accumulators | 1 | per pass |
| Issue rows | 64 | per pass, R cycles |
| Pipeline drain | 3 | lane pipeline depth |
| Drain accumulators | 32 | per pass, L cycles |
| **Per pass** | **100** | |
| **Two passes (64 cols / 32 lanes)** | **200** | |

**200 cycles at 50 MHz = 4.0 µs.** Use this number in the report, not 2.7 µs.

The conclusion is unchanged and if anything strengthened: the array is still four orders
of magnitude faster than the 360 ms UART weight load, so end-to-end timing is entirely
host-bound. But the report should quote a number that was derived rather than one that
was estimated, and the derivation belongs in the documentation.

---

## 3. Column-to-lane mapping

The mapping choice determines the address decode, so fix it before anything else.

**Lane `l` owns columns `{l, l+L, l+2L, ...}`** — interleaved, not blocked. On pass `p`,
lane `l` computes column `c = p·L + l`.

Weights live in each lane's private M9K at local address `{p, r}` — pass index in the
upper bits, row index in the lower.

### 3.1 The address decode is free

Because the row count is a power of two, the sequence counter *is* the weight address.
With `R = 64` and `L = 32`:

```
seq_cnt : 0 .. (passes·R - 1)              // 0..127 for a 64×64 tile

row_idx     = seq_cnt[5:0]                 // wire slice
pass_idx    = seq_cnt[6]                   // wire slice
weight_addr = seq_cnt                      // identity — no logic at all
act_addr    = seq_cnt[5:0]                 // wire slice
```

One counter drives the entire compute sequence. No multipliers, no adders, no decode
logic.

The same property makes the bulk write decode free. Given `bulk_addr = c·R + r` from the
host, sending weights column-major:

```
r          = bulk_addr[5:0]
c          = bulk_addr[15:6]
lane_sel   = c[4:0]                        // c mod 32
p          = c[15:5]                       // c div 32
local_addr = {p, r}
```

All wire slicing. **This is why `R` and `L` must stay powers of two.** If someone proposes
a 100-row tile to match a dataset, the cost is a divider in the address path and a
multiplier in the sequencer. Pad to 128 instead and mask the unused rows.

---

## 4. Compute sequence

```
   IDLE
     │  start_compute
     ▼
   CLEAR ─────────────────┐
     │  1 cycle           │
     ▼                    │
    ACC                   │  pass_idx < passes-1
     │  R cycles issue    │
     │  + 3 drain         │
     ▼                    │
   DRAIN ─────────────────┘
     │  L cycles
     │  pass_idx == passes-1
     ▼
   DONE ──► IDLE   (resp_done to packet_tx)
```

### 4.1 `CLEAR`

Zero all lane accumulators. One cycle, broadcast.

*Optimization, if you want it:* fold this into `ACC` by muxing the accumulator input to
select `0` instead of the running sum on `row_idx == 0`. Saves one cycle per pass and one
state. Costs a 24-bit 2:1 mux per lane. Worth doing only if you are chasing cycles, which
you are not — implement `CLEAR` as its own state first and optimize later if ever.

### 4.2 `ACC`

Issue one row per cycle: drive `weight_addr` to all lanes, `act_addr` to the activation
buffer, assert accumulate enable. Run for `active_rows` cycles.

Then hold for **3 more cycles** before leaving the state. The lane pipeline is BRAM read →
noise inject → multiply → accumulate, so the last issued row's contribution has not
reached the accumulator when the counter finishes. Leaving `ACC` early silently drops the
last three rows from every result — a bug that produces plausible numbers and is
miserable to find. Make the drain count a named parameter tied to the lane pipeline depth,
so that if Lane A adds a pipeline stage the constant follows.

```verilog
localparam LANE_PIPE_DEPTH = 3;   // must track mac_lane.v
```

### 4.3 `DRAIN`

Move `L` accumulator values into the result buffer, one per cycle.

**Use a shift chain, not a multiplexer.** The obvious implementation is a 32-to-1 mux of
24-bit values, but that is 744 LEs with five levels of logic delay on a path that spans
the whole array physically. Instead, add a 2:1 mux on each accumulator input selecting
between "accumulate" and "take the neighbor's value", and shift the whole array toward
lane 0 one position per cycle.

```
 lane31 ──► lane30 ──► ... ──► lane1 ──► lane0 ──► result_buffer
```

Same area (~768 LEs), but every connection is lane-to-adjacent-lane, so placement is
local and timing is trivial. This matters more than it looks: the multiplexer version is
the most likely thing in the design to fail timing when you scale to 64 lanes.

### 4.4 `DONE`

Assert completion to the parser, which sends the `COMPUTE` response. Latch the cycle
count into `CYCLE_CNT` for the host to read.

---

## 5. Weight memory arbitration — there isn't any

M9K blocks are true dual-port. Bulk writes use port A of the selected lane; compute reads
use port B of every lane. The two never contend.

Mutual exclusion also holds at the protocol level: the host is in request/response
lockstep, so a `WRITE_WEIGHTS` packet has been fully acknowledged before a `COMPUTE`
packet is sent. The FSM still rejects `start_compute` outside `IDLE` with status `0x04`,
because relying on host politeness for correctness is how you get a bug that only appears
when someone writes a threaded driver.

Since writes and reads never overlap, M9K read-during-write behavior is irrelevant here
and needs no attention in the instantiation.

---

## 6. Config register map

Little-endian, 32 bits per register, byte-addressed by index. **Freeze this in week 1** —
Lane B's model and Lane C's driver both encode it.

| Addr | Name | Access | Description |
|---|---|---|---|
| `0x00` | `CTRL` | W | `[0]` soft reset, `[1]` clear results, `[2]` reseed LFSRs |
| `0x01` | `TILE_CFG` | RW | `[15:0]` active rows, `[31:16]` active cols |
| `0x02` | `QUANT_LEVELS` | RW | conductance levels, 2–256 |
| `0x03` | `D2D_SIGMA` | RW | device-to-device variation scale (Q8.8) |
| `0x04` | `READ_SIGMA` | RW | cycle-to-cycle read noise scale (Q8.8) |
| `0x05` | `NOISE_SEED` | RW | LFSR seed, 0 is remapped to `0xACE1` |
| `0x06` | `NOISE_EN` | RW | `[0]` quant `[1]` d2d `[2]` read `[3]` stuck `[4]` IR `[5]` ADC |
| `0x07` | `ADC_BITS` | RW | output truncation width, 4–24 |
| `0x08` | `STUCK_RATE` | RW | stuck-at injection rate |
| `0x09` | `BAUD_INC` | RW | UART fractional divider increment |
| `0x0A` | `STATUS` | R | `[3:0]` FSM state, `[4]` busy, `[5]` last error |
| `0x0B` | `CYCLE_CNT` | R | cycles taken by the last `COMPUTE` |

### 6.1 `NOISE_EN` is the most important register in the design

Every non-ideality is individually maskable. This is what makes the whole project
scientifically legible rather than a black box: you can run the identical weights and
activations with each effect isolated, and attribute accuracy loss to a specific physical
mechanism.

It is also what makes verification tractable. Lane B's equivalence tests run with
`NOISE_EN = 0` to prove the clean datapath is bit-exact against the model, then enable one
bit at a time. Without this register you would be debugging six interacting effects
simultaneously.

### 6.2 `TILE_CFG` and power-of-two addressing

`active_rows` and `active_cols` limit how many rows the sequencer issues and how many
columns are drained, letting you sweep tile sizes without a rebuild.

They do **not** change the address stride. Weight addressing always uses the physical
`R_MAX = 64` stride, so a 48-row configuration uses addresses 0–47 of each 64-entry
region and leaves 48–63 unread. Non-power-of-two active sizes are therefore free; the
physical tile stays a power of two and the wire-slice decode of §3.1 is preserved.

### 6.3 Reset defaults

On reset: `TILE_CFG` = full tile, `NOISE_EN` = 0, `QUANT_LEVELS` = 256, all sigmas = 0,
`BAUD_INC` = 115200.

**Reset means an ideal, noiseless crossbar.** A freshly configured board computes exact
integer MVMs, so if the first thing you see after programming is wrong, the problem is the
datapath and not a noise setting someone left enabled.

---

## 7. Result buffer

`C × 24` bits — 64 × 24 for the MVP. One M9K in 256×32 mode holds 256 results with room
for a 128-column tile and 32-bit accumulators.

Written one word per cycle during `DRAIN`, read by the parser during `READ_RESULT`.
Single-port is sufficient since the two phases never overlap.

Results persist until the next `COMPUTE`, so the host can re-read them without
recomputing — useful when a response packet is lost and the driver retries.

---

## 8. Display

Not decorative. This is the physical interface M0 asks for in an Infrastructure project,
and it is the only debug channel that keeps working when the serial link is the thing
that is broken.

| Element | Shows |
|---|---|
| `HEX5`–`HEX4` | FSM state (hex) |
| `HEX3`–`HEX0` | `result[0]`, or RX byte count, selected by `SW[0]` |
| `LEDR[9]` | busy |
| `LEDR[8]` | error latched since reset |
| `LEDR[7:0]` | `pass_idx` and row counter activity |

During week-3 bring-up, a HEX display that increments on every received byte tells you the
UART works before the parser exists. Build the display driver early — it is 150 LEs and it
pays for itself the first evening the link misbehaves.

`KEY0` is asynchronous reset, synchronized to `clk` with a two-flop deassert. `KEY1` is a
manual `COMPUTE` trigger using whatever is already loaded, which makes a
host-independent demo possible if the laptop link dies mid-presentation.

---

## 9. Resource estimate

| Block | LEs (est.) | M9K |
|---|---|---|
| Config register file (12 × 32b) | 384 | 0 |
| Sequence counter + decode | 60 | 0 |
| FSM + dispatch | 50 | 0 |
| Drain shift-chain muxes (32 × 24b) | 768 | 0 |
| Result buffer control | 40 | 1 |
| Display driver | 150 | 0 |
| **Total** | **~1,450** | **1** |

Running total for the design so far: UART ~140, parser ~400, control ~1,450 — about
2,000 LEs, roughly 4% of the 10M50, before the lane array.

---

## 10. Verification

1. **Sequence trace.** Assert that over one `COMPUTE`, `weight_addr` visits every value
   in `0..passes·R-1` exactly once, in order, and `act_addr` cycles correctly.
2. **Pipeline drain.** Load a weight matrix whose last three rows are the only non-zero
   ones. If the FSM leaves `ACC` early, the result is zero. This is a directed test for
   the specific bug that is otherwise nearly invisible.
3. **Drain ordering.** Load a matrix producing known distinct per-column values, confirm
   the shift chain deposits them in the correct result buffer addresses. Off-by-one in
   the shift chain reverses column order — check for exactly that.
4. **Multi-pass.** Confirm pass 1 results do not overwrite pass 0. Use column values that
   make an ordering error obvious rather than random data.
5. **Register file.** Write and read back all registers; confirm read-only registers
   ignore writes; confirm reset defaults match §6.3.
6. **`TILE_CFG` sweep.** Run 8×8, 32×32, 48×48, 64×64 and confirm each matches the model
   with the correct number of rows contributing.
7. **`NOISE_EN` isolation.** With all bits clear, RTL must be bit-exact against the
   reference model over 1,000 random matrices. This is the single most important test in
   the project — everything downstream assumes the clean datapath is exact.
8. **Busy rejection.** Assert `start_compute` during `ACC`, confirm status `0x04` and no
   corruption of the in-flight computation.

---

## 11. Open questions for the freeze meeting

1. **Should `CYCLE_CNT` count the whole compute or just `ACC`?** Whole compute is more
   honest for the report; `ACC`-only is the number you would quote for array throughput.
   Suggest recording total and computing the rest on the host.
2. **Reseed on every `COMPUTE`, or only on `CTRL[2]`?** Explicit reseed makes runs
   reproducible, which matters for the model equivalence tests. Automatic reseeding makes
   noise statistics independent across runs, which matters for sweeps. Recommend explicit,
   with the host reseeding between sweep points.
3. **Does `active_cols` need to be a multiple of `L`?** If not, the final pass drains
   partially and the shift chain needs a variable count. Simplest answer: round up and
   discard the extra columns on the host.
4. **Do we want a `SELF_TEST` command** that loads a known matrix from a ROM, computes,
   and compares against a stored expected result — a one-packet health check for the
   demo? About 100 LEs and one M9K, and it makes "is the board working" answerable in one
   second on stage.
