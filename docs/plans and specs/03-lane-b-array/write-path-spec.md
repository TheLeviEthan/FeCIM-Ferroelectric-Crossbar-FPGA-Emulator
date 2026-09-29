# FeCIM Component Spec: Write-Path Transform

**Module:** `write_path`, `cell_hash`
**Owner:** Lane B (array RTL), model equivalence jointly with Lane C
**Depends on:** `packet_parser` (bulk stream), `config_regs`
**Feeds:** lane weight memories (M9K port A)
**Milestone:** Week 5
**Revision:** 2 — corrected against the MAX 10 Embedded Multipliers User Guide

---

## 1. Purpose and scope

Injects **programming-time** non-idealities into weights as they are written to the lane
memories: conductance quantization, device-to-device variation, and stuck-at faults.

These three share a defining property — they are *fixed properties of a cell*. The same cell
programmed with the same value must produce the same perturbed result every time, in the
way a physical device has a permanent programming offset. That determinism requirement
shapes the entire design.

Cycle-to-cycle read noise is **not** here. It changes on every access and belongs in the
lane read path.

### 1.1 One instance, not sixty-four

Bulk writes arrive one byte at a time over UART, so exactly one lane is written per
transaction. The write path is a **single shared instance** upstream of the lane memory
write ports.

This matters for area: the arithmetic below costs about 390 LEs and 7 multiplier blocks
once, rather than 25,000 LEs and 448 blocks sixty-four times over. At 144 blocks total,
the per-lane version is not merely wasteful — it is impossible.

### 1.2 Weights only

`bulk_target` selects weights or activations. **The transform applies only to
`TGT_WEIGHTS`.** Activations are input drive voltage, not stored conductance, and carry
none of these device effects.

Getting this wrong makes activation quantization look like weight quantization in the
sweeps, producing an accuracy curve that is wrong in an entirely plausible-looking way.

---

## 2. Supersedes: the fault map RAM

The original plan described stuck-at faults as "a bitmask in a fault map RAM." **Do not
build that.** It costs an M9K, needs a separate host upload path, and the host would have
to generate the map anyway.

Instead, derive every per-cell property from a **combinational hash of the cell address**.
The offset for cell `(row, col)` is a pure function of address and global seed —
deterministic, reproducible, zero storage, and instantly re-randomized by writing a new
`NOISE_SEED`. "Simulate a different physical die" becomes one register write.

---

## 3. Order of operations

The stages mirror the physical programming sequence, and the order is not arbitrary.

```
   w_ideal (int8 from host)
        │
        ▼
   ┌─────────────────┐
   │ 1. QUANTIZE     │   cell holds only N conductance levels
   └────────┬────────┘
            ▼
   ┌─────────────────┐
   │ 2. D2D OFFSET   │   programmed conductance misses the target level
   └────────┬────────┘
            ▼
   ┌─────────────────┐
   │ 3. STUCK-AT     │   cell does not respond at all — overrides everything
   └────────┬────────┘
            ▼
      w_stored → M9K
```

Quantization comes first because the programming circuit *targets* a discrete level.
Variation then describes how far the cell lands from that target. Stuck-at is applied last
because a stuck cell ignores whatever was requested.

Applying variation before quantization would model a noisy *target* rather than a noisy
*outcome*, and would partly disappear when re-quantized.

---

## 4. Signed weights and the differential pair

An 8-bit signed weight cannot map onto a single conductance, since conductance is strictly
positive. The standard construction is a **differential pair**: two cells per weight, with
the effective weight proportional to `G⁺ − G⁻`.

This spec models the pair as a single signed quantity, quantizing across the full signed
range.

**State this assumption explicitly in the report.** A real differential pair with N levels
per cell yields up to 2N−1 distinguishable differential states, not N. The model is
therefore conservative — it understates achievable resolution, which is the right direction
to err — but it is a modelling choice and must not be presented as a measurement.

### 4.1 Quantization arithmetic

```systemverilog
logic [8:0]  u, level;
logic [16:0] scaled;
logic [7:0]  u_q;

assign u      = {1'b0, w_ideal} + 9'd128;              // 0..255
assign level  = (u * quant_levels) >> 8;               // 9×9 → ½ block
assign scaled = (level * quant_mult)  >> 8;            // 9×16 → 1 block
assign u_q    = scaled[7:0];
// w_q = u_q - 128
```

**`QUANT_MULT` is Q8.8, not Q16.16.** Revision 1 defined it as `round(255·2¹⁶/(N−1))`,
which reaches 16.7 million at N = 2 — a 24-bit constant needing two multiplier blocks.
Redefined as:

```
QUANT_MULT = round(255 · 256 / (N - 1))
```

This peaks at 65,280 for N = 2 and bottoms at 257 for N = 255, fitting 16 bits.

**`QUANT_LEVELS` is capped at 255**, not 256, so the first multiply stays within 9 bits and
packs. The cap costs nothing physically — nobody has 256 distinguishable conductance states
— and the no-quantization case is handled by the bypass in §6.1, which is exact.

### 4.2 Arbitrary level counts, deliberately

`QUANT_LEVELS` is **not** restricted to powers of two. Partial polarization switching in
HZO gives whatever number of stable, distinguishable states the material and programming
scheme actually produce — frequently five or six, not eight. Forcing a power of two would
make the calibration fit the hardware rather than the hardware fit the measurement.

The host precomputes `QUANT_MULT`, so no divider is needed. Expose a single
`set_quantization(n_levels)` in the Python API that writes both registers together; they
must never go out of sync.

---

## 5. Cell hash

```systemverilog
// 16-bit rounds: each multiply is 16×16 and costs one block.
// A 32×32 multiply would cost FOUR blocks — 16 for both hashes.
function automatic logic [15:0] cell_hash16(
    input logic [15:0] addr,
    input logic [15:0] seed
);
    logic [15:0] h;
    h = addr ^ seed;
    h = h * HASH_C0;            // 16'h9E37
    h = h ^ (h >> 7);
    h = h * HASH_C1;            // 16'h85EB
    h = h ^ (h >> 9);
    return h;
endfunction
```

Instantiated twice with decorrelated seeds:

```systemverilog
logic [15:0] h_d2d, h_stuck;
assign h_d2d   = cell_hash16(cell_addr, noise_seed[15:0] ^ SEED_D2D);
assign h_stuck = cell_hash16(cell_addr, noise_seed[15:0] ^ SEED_STUCK);
```

`cell_addr` is the **global** cell address (`col · TILE_ROWS + row`), not the lane-local
address. Two cells in different lanes must not collide.

Four multiplier blocks total for both hashes.

### 5.1 Uniform to bell-shaped

Each hash yields 16 bits, so the variation sum uses **two** bytes rather than four:

```systemverilog
logic [8:0]        ih_sum;
logic signed [8:0] ih9;

assign ih_sum = h_d2d[7:0] + h_d2d[15:8];              // 0..510
assign ih9    = $signed({1'b0, ih_sum}) - 9'sd255;     // ±255
```

**This is a triangular distribution, not Gaussian.** Revision 1 used four bytes from a
32-bit hash and described the result as Irwin–Hall; with 16-bit hashes only two bytes are
available, and the sum of two uniforms is triangular.

Document it as triangular. For a one-time-per-cell programming offset this is acceptable —
what matters for accuracy impact is the variance and the bounded support, and a triangular
distribution has both. It is also, like the bounded read-noise distribution, arguably more
physical than a Gaussian with infinite tails.

If the HZO measurements show a clearly Gaussian spread and matching the shape matters, the
fix is a third hash instance (two more blocks) to get a third byte. Decide after
calibration, not before.

### 5.2 Scaling and saturating add

```systemverilog
logic signed [17:0] d2d_scaled;
logic signed [10:0] w_sum;
logic signed [7:0]  w_var;

assign d2d_scaled = (ih9 * $signed({1'b0, d2d_sigma9})) >>> 8;   // Q1.8, 9×9 → ½ block

assign w_sum = $signed({{3{w_q[7]}}, w_q}) + d2d_scaled[10:0];
assign w_var = (w_sum >  127) ?  8'sd127 :
               (w_sum < -128) ? -8'sd128 : w_sum[7:0];
```

**Must saturate, never wrap.** A wrapping add turns a +127 weight with positive variation
into −128 — a sign inversion, the most destructive error possible in an MVM, and physically
absurd besides. A cell programmed slightly too conductive does not become maximally
conductive in the opposite polarity.

Unlike the read-noise add in the lane (which needs no saturation because the width covers
the range), this one genuinely saturates: the stored weight is 8 bits by definition. That
is fine here — the write path has thousands of cycles of slack, so a comparator chain costs
nothing.

### 5.3 Stuck-at

```systemverilog
logic stuck_hit;
assign stuck_hit = (h_stuck < stuck_rate[15:0]);
```

Fault rate is `stuck_rate / 65536`, resolving to about 0.0015% — well below realistic
device failure rates.

Two physically distinct modes, selected by `REG_STUCK_RATE[16]`:

| Mode | Value forced | Physical picture |
|---|---|---|
| `0` — stuck-at-zero | `w = 0` | Cell fully depolarized; both arms of the pair at G_min. The dominant radiation-damage and fatigue endpoint. |
| `1` — stuck-at-rail | `w = ±127`, sign from `h_d2d[15]` | One arm pinned at maximum conductance. Rarer, far more destructive per fault. |

Default to mode 0. The rail sign is drawn from `h_d2d` rather than `h_stuck` because every
bit of `h_stuck` is consumed by the threshold comparison; reusing one would correlate the
sign with how far below threshold the cell fell.

Sweeping both modes and comparing is a good result — rail faults degrade accuracy far
faster per fault.

---

## 6. Pipeline and bypass

Four stages: hash → quantize → variation → stuck/saturate/write.

**Timing is a non-issue.** Bytes arrive every ~4,340 clock cycles at 115200 baud, so the
write path has four orders of magnitude of slack. Pipeline generously.

The lane memory write occurs four cycles after `bulk_we`, so `bulk_addr` must be carried
down the pipeline alongside the data. Do not use the parser's live address at the write
port.

### 6.1 Bypass must be latency-matched

When `noise_en.quant`, `noise_en.d2d`, and `noise_en.stuck` are all clear, the write path is
a **pure pass-through** — `w_stored == w_ideal`, bit for bit.

Implement bypass as muxes at each stage, keeping the four-cycle latency identical in both
modes. A separate shorter path would produce address/data skew appearing only when noise is
enabled — exactly the configuration where you would blame the noise model.

This bypass makes the L0 equivalence gate possible, and that gate is the foundation
everything else rests on.

---

## 7. Register map additions

| Addr | Name | Access | Description |
|---|---|---|---|
| `0x0C` | `QUANT_MULT` | RW | `round(255·256/(N−1))`, Q8.8, host-computed |

And `REG_STUCK_RATE` (`0x08`) is refined:

| Bits | Field |
|---|---|
| `[15:0]` | fault rate threshold, out of 65536 |
| `[16]` | stuck mode: 0 = stuck-at-zero, 1 = stuck-at-rail |

`REG_D2D_SIGMA` (`0x03`) uses 9 bits, Q1.8, matching `REG_READ_SIGMA`.

---

## 8. Resource estimate

| Block | LEs | Mult blocks |
|---|---|---|
| `cell_hash16` ×2 (2 multiplies each) | 110 | 4 |
| Quantization | 45 | 1½ |
| Triangular sum + centering | 25 | — |
| D2D scaling | 20 | ½ |
| Saturating add | 40 | — |
| Stuck compare + mux | 30 | — |
| Pipeline + address carry + bypass | 110 | — |
| **Total** | **~380** | **6** |

Six of 144 blocks. Revision 1's 32-bit hash arithmetic would have cost 16 blocks for the
hashes alone.

---

## 9. Verification

The hash is the crux. **RTL and the C++ model must produce bit-identical hash output**, or
nothing downstream can be compared. Test this before anything else in the module.

1. **Hash equivalence.** All 65,536 addresses × 8 seeds, RTL against C++, exact match.
   Exhaustive and fast — no reason to sample. Verify the C++ uses `uint16_t` arithmetic so
   overflow wraps identically.
2. **Hash distribution.** χ² uniformity on hash output; confirm no visible structure when
   plotted against address. A weak hash produces spatially correlated variation, which looks
   like a systematic gradient and could be mistaken for an IR-drop effect.
3. **Bypass exactness.** All enables clear, write and read back every cell across the full
   int8 range; exact equality required. Gate for L0.
4. **Quantization levels.** For N ∈ {2, 3, 5, 6, 8, 16, 255}, confirm the output takes
   exactly N distinct values with correct spacing. N = 3 and N = 5 matter most — they
   exercise the reciprocal path that powers of two would hide.
5. **`QUANT_MULT` range.** Confirm no overflow at N = 2 (`QUANT_MULT` = 65,280) and correct
   behaviour at N = 255 (`QUANT_MULT` = 257). Directed test for §4.1.
6. **Determinism.** Program the same cell with the same value 100 times; identical stored
   result required. Change `NOISE_SEED` and require it to change.
7. **Distribution shape.** Confirm the variation offset is triangular with the expected
   variance — and confirm the C++ model produces the same shape. Documents §5.1 as much as
   it tests it.
8. **Saturation.** Weights at ±127 with maximum positive and negative variation. Assert no
   sign inversion ever occurs.
9. **Stuck-at rate.** Sweep `stuck_rate`; confirm the measured fraction matches
   `stuck_rate/65536` within statistical error across the full array.
10. **Monotone fault accumulation.** With `NOISE_SEED` held fixed, confirm that raising
    `stuck_rate` only ever *adds* faults and never removes one. The fatigue study depends on
    this property — see §10, question 4.
11. **Independence.** Confirm stuck-at faults and large D2D offsets are uncorrelated across
    the array, and that the rail sign is uncorrelated with the stuck decision.
12. **Activation passthrough.** Write activations with all noise enabled; exact values
    required. Directed test for §1.2.

---

## 10. Open questions

1. **Model the differential pair explicitly?** Two cells with separate quantization and
   variation, differenced at read, is more physical and would show that differential
   encoding partially cancels correlated drift. It roughly doubles the write path and needs
   twice the weight memory. This is the semester-2 item — check the multiplier budget in
   week 17 before committing, since the array already sits at 50%.
2. **Level-dependent variation?** Real multilevel cells often show larger variation at
   intermediate states, since those rely on partial domain switching. If the HZO data shows
   this, a level-dependent sigma costs one small lookup table and is a genuinely distinctive
   result. Check the measurements before deciding.
3. **Independent seeds per effect?** Currently one global seed derives both hashes.
   Independent seeds would let you hold the fault pattern fixed while re-randomizing
   variation, useful for isolating causes. Two more registers.
4. **Do stuck cells accumulate?** For a fatigue study, faults should grow monotonically with
   cycle count. Hold `NOISE_SEED` fixed and raise `stuck_rate`: the same cells stay below a
   rising threshold, so the fault set grows monotonically. That property is free, but it is
   only true if the hash is stable — test 10 confirms it, and the fatigue result depends on
   it.
