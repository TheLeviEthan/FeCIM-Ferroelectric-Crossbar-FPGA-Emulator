# FeCIM Component Spec: MAC Lane and Lane Array

**Module:** `mac_lane`, `mac_array`, `read_noise`, `adc_quant`
**Owner:** Lane B (array RTL), model equivalence jointly with Lane C
**Depends on:** `act_buffer` (broadcast), `write_path` (weight writes), `control_fsm`
**Milestone:** Weeks 4–5
**Revision:** 2 — corrected against the MAX 10 Embedded Multipliers User Guide

---

## 1. Purpose and scope

The computational core. Sixty-four lanes, each owning one crossbar column per pass, each
holding its own weight memory and read-noise generator, all accumulating in parallel while
a shared activation broadcasts one row per cycle.

Contains the **read-time** non-ideality (cycle-to-cycle noise) and the **accumulation-time**
ones (IR drop, ADC quantization). Programming-time effects are already baked into the
stored weights by the write path.

---

## 2. The 9-bit rule

Every width decision in this document follows from one device fact, so it goes first.

The MAX 10 has **144 embedded multiplier blocks**. Each block is *either* one 18×18
multiplier *or* two 9×9 multipliers. An operand of 10 bits or more consumes a whole block.

Revision 1 of this spec used a 10-bit noisy weight and a 16-bit sigma. Both exceeded 9
bits, so each lane consumed two full blocks — 128 blocks at 64 lanes, against a budget of
144, before the write path. **The design did not fit.**

Every multiply is now 9×9 signed, two per block:

| Multiply | Operands | Blocks (64 lanes) |
|---|---|---|
| MAC | `w_n` 9-bit signed × `act` 9-bit signed | 32 |
| Noise scaling | `ih9` 9-bit signed × `sigma9` 9-bit signed | 32 |

### 2.1 Signedness must stay uniform

A block has **one** `signa` and **one** `signb` signal shared by both of its 9×9
multipliers, so two multiplies packed into one block must agree on operand signedness.

Every multiply here is signed × signed — `act` and `sigma9` are unsigned quantities
zero-extended into signed-positive form. **Do not convert any of them to unsigned
multiplies.** It looks like a free optimization and it silently prevents pairing, doubling
the block count.

`fecim_pkg.sv` carries elaboration assertions on `W_N_W` and `SIGMA_W` so a future widening
fails at compile time rather than at fitting.

---

## 3. Accumulator width

Worst case with the corrected widths:

| Quantity | Range | Bits |
|---|---|---|
| Stored weight | ±127 | 8 signed |
| Read noise (clamped, §5.3) | ±127 | 8 signed |
| Noisy weight `w_n` | ±254 | **9 signed** |
| Activation | 0…255 | 8 unsigned |
| Product | ±64,770 | 18 signed |
| Sum over 128 rows | ±8,290,560 | 24 signed |

24-bit signed holds up to 8,388,607, so the worst case fits — **by 1.2%**.

That margin is not worth taking. It vanishes entirely at 256 rows, and it leaves nothing
for IR-drop fractional scaling. **`ACC_W = 32`.** The result buffer M9K runs natively in
256×32 mode, so the extra width costs nothing where it is stored, and about 256 LEs across
the array where it is accumulated.

---

## 4. Lane structure

```
                 seq_cnt                    act_bcast (shared, all lanes)
                    │                              │
                    ▼                              │
         ┌──────────────────┐                      │
         │  Weight M9K      │  port A ◄── write_path
         │  1024 × 8        │                      │
         │  (dual port)     │                      │
         └────────┬─────────┘                      │
                  │ w_raw[7:0]                     │
                  ▼                                │
           ┌─────────────┐                         │
           │ align reg   │                         │
           └──────┬──────┘                         │
                  │                                │
       ┌──────────▼───────────┐                    │
       │   + (9-bit, no sat)  │◄── noise_q[7:0]    │
       └──────────┬───────────┘    (registered)    │
                  │ w_n[8:0]                       │
                  ▼                                │
           ┌─────────────┐                         │
           │  9×9 signed │◄────────────────────────┘
           │  multiply   │
           └──────┬──────┘
                  │ prod[17:0]              ┌──────────────┐
                  ▼                         │  read_noise  │
           ┌─────────────┐                  │  LFSR → IH   │
           │  ACC (32b)  │                  │  → scale     │
           │  + shift mux│                  │  → clamp     │
           └──────┬──────┘                  └──────────────┘
                  │
    shift_in ◄────┴────► acc_out ──► lane[l-1]
    from lane[l+1]
```

---

## 5. Read noise

The read-time non-ideality. Unlike write-path effects, it must produce a **different value
on every access** — the same cell read twice gives two different answers, because the noise
is in the sensing, not the stored state.

### 5.1 Per-lane LFSR, decorrelated at elaboration

A shared generator would make noise identical across all columns simultaneously, which is
physically wrong — read noise is independent per sense amplifier, and correlated noise
partially cancels in an argmax, understating its impact.

```systemverilog
localparam logic [31:0] LANE_SALT = LANE_SALT_C * LANE_ID;   // elaboration-time

logic [31:0] seed_eff;
assign seed_eff = (noise_seed ^ LANE_SALT) | 32'd1;          // never all-zero

always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)      lfsr <= seed_eff;
    else if (reseed) lfsr <= seed_eff;
    else if (acc_en) lfsr <= lfsr[0] ? ((lfsr >> 1) ^ LFSR_TAPS)
                                     :  (lfsr >> 1);
end
```

`LANE_SALT` is computed from the generate index at elaboration, so decorrelation costs zero
hardware. The `| 32'd1` guard matters: a Galois LFSR seeded to zero stays at zero forever,
and `NOISE_SEED = 0` is what a user types first.

### 5.2 Advance on `acc_en`, not on `clk`

**The LFSR must advance only when an accumulate actually occurs.** If it free-runs, the
noise sequence depends on how many idle cycles elapsed between computes, so two runs with
identical seeds and inputs produce different results depending on host timing.

That destroys reproducibility, which destroys the RTL-versus-model equivalence tests, which
are the foundation the whole verification plan rests on.

### 5.3 Uniform to bell-shaped, within 9 bits

```systemverilog
logic [9:0]         ih_sum;
logic signed [10:0] ih_centered;
logic signed [8:0]  ih9;

assign ih_sum      = lfsr[7:0] + lfsr[15:8] + lfsr[23:16] + lfsr[31:24];
assign ih_centered = $signed({1'b0, ih_sum}) - 11'sd510;    // 4 × 255 / 2
assign ih9         = ih_centered >>> 1;                     // ±510 → ±255
```

The halving is a pure scale change absorbed into sigma — no distribution is lost, and it
brings the operand to 9 bits.

**Be honest about the distribution in the report.** The four bytes are slices of a single
LFSR state, so they are linearly related rather than independent, and this is not a true
Irwin–Hall distribution. It is zero-mean, bell-shaped, bounded, and cheap, which is what
the noise model needs. Verify the empirical moments (test 4) and report those rather than
claiming a theoretical form the construction does not have.

Scale and clamp:

```systemverilog
logic signed [17:0] scaled;
logic signed [7:0]  noise_q;                   // registered, cycle N+1

assign scaled  = (ih9 * $signed({1'b0, sigma9})) >>> 9;      // Q1.8
assign noise_q = (scaled >  NOISE_CLAMP) ?  8'sd127 :
                 (scaled < -NOISE_CLAMP) ? -8'sd127 : scaled[7:0];
```

`sigma9` is Q1.8 spanning 0 to 0.996 in steps of 1/256, giving noise-σ granularity of about
0.29 LSB — finer than any sweep needs. At maximum sigma the noise standard deviation is
roughly 57% of full weight scale, which is far beyond any realistic device.

### 5.4 The clamp value is structural, not arbitrary

±127 is chosen so that `w_n = w(±127) + noise(±127) = ±254` stays within **9 bits signed**.

Two consequences, both load-bearing:

1. The MAC multiply remains 9×9 and packs two per block. Raising the clamp to ±255 makes
   `w_n` 10 bits and doubles the block cost of the entire array.
2. **The add in cycle `N+2` needs no saturation logic** — the sum cannot overflow 9 bits.
   That removes a comparator chain from the tightest path in the design.

### 5.5 Why scaling happens in cycle N+1

Cycle `N+2` already carries an add and the main multiply in one 20 ns period. Putting the
scaling multiply there too would make it multiply → add → multiply, which will not close.

The LFSR state is available a cycle early — it advances when the address is issued at
`N` — so the Irwin–Hall sum, scaling multiply, and clamp all complete in `N+1` and register
into `noise_q`.

---

## 6. Multiplier

9-bit signed `w_n` × 9-bit signed `act` (zero-extended from 8-bit unsigned):

```systemverilog
logic signed [17:0] prod;

always_ff @(posedge clk)
    if (acc_en) prod <= $signed(w_n) * $signed({1'b0, act_bcast});
```

Let the block's own output register serve as the pipeline stage rather than adding an LE
register after it — Quartus infers this from the `always_ff` and it improves timing.

Two lanes' multipliers share one block. Quartus performs the packing; your job is to keep
the widths and signedness eligible for it. Check the fitter report for `Embedded Multiplier
9-bit elements` to confirm pairing actually happened.

---

## 7. Accumulator and drain shift chain

```systemverilog
always_ff @(posedge clk) begin
    if      (acc_clear) acc <= '0;
    else if (acc_en)    acc <= acc + $signed(prod);
    else if (shift_en)  acc <= shift_in;
end
```

The third branch is the drain path — during `DRAIN` each lane loads its neighbour's value,
walking results toward lane 0 one per cycle. The mux is 32 bits × 64 lanes, but every
connection is lane-to-adjacent-lane, so placement stays local and timing is trivial. A
64-to-1 multiplexer would have six levels of logic spanning the array physically.

Priority order matters: `acc_clear` outranks `acc_en` so the first row of a pass overwrites
rather than accumulating onto stale data.

---

## 8. IR drop: one multiplier, not sixty-four

The obvious implementation puts an attenuation multiply in each lane's accumulate path —
64 extra blocks the design cannot afford.

The attenuation coefficient depends only on row position, and the activation is indexed by
row, so:

```
Σ_r  atten[r] · (w[r,c] · a[r])   ≡   Σ_r  w[r,c] · (atten[r] · a[r])
```

Attenuating the activation once before broadcast is algebraically identical and costs **one
shared 9×9 multiply**.

```systemverilog
// in the activation broadcast path, not in the lane
logic [7:0] act_atten;
assign act_atten = noise_en.ir ? ((act_raw * atten_rom[row_idx]) >> 8)
                               :   act_raw;
```

`atten_rom` is a `TILE_ROWS` × 8-bit coefficient table in logic, host-writable, defaulting
to all-ones.

This works precisely **because** the model is a row-position-dependent approximation. A
rigorous IR-drop treatment depends on total column current, which differs per column and
breaks the factorization — that version genuinely needs per-lane multipliers and an
iterative solve besides. Say so in the report: the simplification follows from the stated
approximation rather than being independent of it.

Implementation note: this block lives in the activation broadcast path, so it amends the
activation buffer spec rather than sitting inside `mac_lane`.

---

## 9. ADC quantization: one shared instance

Results pass through the shift chain one per cycle during `DRAIN`, so quantization is a
**single block at the chain output** rather than 64 per-lane instances.

```systemverilog
logic [4:0]         sh;
logic signed [31:0] adc_out;

assign sh = ACC_W - adc_bits;
assign adc_out = noise_en.adc
               ? (($signed(acc_chain_out) + (32'sd1 <<< (sh-1))) >>> sh) <<< sh
               :   acc_chain_out;
```

**Round, do not truncate.** Adding half an LSB before shifting makes this round-to-nearest,
which is what a real ADC does. Plain truncation introduces a systematic negative bias that
grows with the number of accumulated terms — a consistent downward offset easily misread as
a weight-mapping error.

One 32-bit barrel shifter, ~160 LEs, once. Guard `sh == 0` so `adc_bits == ACC_W` is an
exact no-op rather than a shift by −1.

---

## 10. Weight memory write decode

Port A of each lane's M9K, write enable qualified by the lane select:

```systemverilog
assign wr_en = bulk_we && (bulk_target == TGT_WEIGHTS)
                       && (lane_sel == LANE_ID[LANE_AW-1:0]);
```

Reads use port B with `seq_cnt` as the address. Writes and compute never overlap (control
FSM spec §5), so read-during-write behaviour is unspecified and does not matter.

At 128 rows and 2 passes each lane stores 256 weights, using 256 of the 1024 words
available in 1024×8 mode. Room for 512 rows before the memory map changes.

---

## 11. Resource estimate

**Per lane:**

| Element | LEs | Mult blocks | M9K |
|---|---|---|---|
| Weight M9K + decode | 15 | — | 1 |
| LFSR (32b) | 32 | — | — |
| Irwin–Hall adders + halve | 32 | — | — |
| Noise scale + clamp | 35 | ½ | — |
| Alignment registers | 17 | — | — |
| 9-bit add (no saturation) | 9 | — | — |
| Multiplier + product reg | 20 | ½ | — |
| Accumulator (32b) + shift mux | 64 | — | — |
| Control | 15 | — | — |
| **Per lane** | **~239** | **1** | **1** |

**Array totals:**

| | LEs | Mult blocks | M9K |
|---|---|---|---|
| 64 lanes | 15,296 | 64 | 64 |
| ADC quantizer (shared) | 160 | — | — |
| IR attenuation (shared) | 80 | 1 | — |
| **Array total** | **~15,536** | **65** | **64** |

**Whole design, 64 lanes / 128×128:**

| Resource | Used | Available | % |
|---|---|---|---|
| Logic elements | ~18,300 | 49,760 | 37% |
| M9K blocks | 66 | 182 | 36% |
| **Multiplier blocks** | **72** | **144** | **50%** |

At 32 lanes / 64×64 the figures are ~10,400 LEs (21%), 34 M9K (19%), 40 blocks (28%).

Multipliers are the tightest resource. This matters for the semester-2 differential-pair
work, which doubles the write path — check the budget before committing to it.

---

## 12. Verification

Test 2 is the foundation; everything else assumes it passes.

1. **Single lane, clean.** One lane, `NOISE_EN = 0`, random weights and activations,
   compared against a NumPy dot product. Exact.
2. **Full array, clean, bit-exact.** All 64 lanes, `NOISE_EN = 0`, 1,000 random 128×128
   matrices, RTL versus C++ model under Verilator, stepping both and comparing accumulators
   every cycle. **Zero tolerance — any mismatch blocks all downstream work.**
3. **LFSR reproducibility.** Same seed, same inputs, two runs separated by a variable number
   of idle cycles. Results must be identical. Directed test for §5.2.
4. **Noise distribution.** Collect 10⁶ samples of `ih9`, confirm zero mean, measure σ and
   kurtosis, check for periodicity. Report measured moments; do not assume Gaussian.
5. **Lane decorrelation.** Cross-correlate noise sequences between lane pairs. Near zero
   required. Failure means `LANE_SALT` is not spreading seeds.
6. **Clamp boundary.** Maximum sigma with `ih9` at both extremes; assert `noise_q` saturates
   at exactly ±127 and that `w_n` never exceeds 9 bits signed. Directed test for §5.4 — an
   assertion on `w_n` width belongs in the testbench permanently.
7. **Accumulator headroom.** Maximum weights, maximum activations, maximum noise, all 128
   rows, same sign throughout. Assert no overflow. Construct it to hit the worst case
   exactly rather than hoping random stimulus finds it.
8. **Drain ordering.** Distinct known value per column; confirm the shift chain deposits
   them at the right result-buffer addresses. An off-by-one reverses column order.
9. **ADC rounding.** Sweep `adc_bits` from 4 to 32; confirm round-to-nearest, zero mean
   error, and that `adc_bits == ACC_W` is an exact no-op.
10. **IR factorization.** Verify that attenuating the activation gives bit-identical results
    to attenuating each product, against a reference that does it the expensive way. Proves
    the §8 optimization is exact rather than approximate.
11. **Multiplier packing.** Read the Quartus fitter report and confirm the multiply count
    matches §11. If it reports 128 blocks instead of 64, pairing failed — check operand
    widths and signedness before doing anything else.
12. **Timing closure.** Confirm the `N+2` path meets 50 MHz with positive slack. If not,
    register between the add and the multiply and set `LANE_PIPE_DEPTH = 4`.

---

## 13. Open questions

1. **Should read noise scale with the stored weight?** Real sense amplifiers show
   signal-dependent noise. A multiplicative component costs one more 9×9 per lane — 32 more
   blocks at 64 lanes, taking utilization to 72%. Check whether the HZO measurements
   support it before committing.
2. **ADC before or after argmax?** After is cheaper; before is physically correct and lets
   coarse quantization flip near-ties, which is realistic and shows up as a floor in the
   margin plot. Recommend before.
3. **`atten_rom` host-writable or synthesis constant?** Host-writable costs 128 register
   writes at setup and allows sweeping IR severity without a rebuild. Recommend
   host-writable.
4. **Per-lane accumulator saturation?** 32 bits cannot overflow within the stated range, so
   there is nothing to saturate. If someone later raises the tile to 512 rows that stops
   being true — the package assertion catches it at compile time.
