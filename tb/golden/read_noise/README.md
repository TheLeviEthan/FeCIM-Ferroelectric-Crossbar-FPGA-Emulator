# tb/golden/read_noise

Golden vectors for read noise: the per-lane LFSR, the sample arithmetic and the noise
inside a MAC lane (mac-array-spec §5). Owned by Lane C. Consumed by Lane B's
`read_noise` and `mac_lane` testbenches, and by
`model/tests/test_read_path_vectors.cpp`, which replays every file through the C++
model. If the RTL disagrees with these files, the RTL is wrong: the Python generator and
`model_exact` were written independently and agree.

Generated with the IR-drop, ADC and whole-compute vectors by
`tb/golden/gen_read_path_vectors.py`. See [Regenerating](#regenerating).

## Files

| File | Drives | What |
|---|---|---|
| `noise_q.hex` | `read_noise` (combinational part) | 1,588 `(lfsr, read_sigma) → noise_q` entries: every clamp edge, both floor shifts on negative values, σ = 0, 1, 255, and random states. |
| `sequences.hex` | `read_noise` | 9 sequences × 256 steps: the LFSR state after each advance and the sample from it, for lanes 0, 1, 5, 63 and seeds 0, `0xFFFFFFFF`, `0xBEEF`, …. |
| `lane_cases.hex` | `mac_lane` | 21 cases on lanes 0, 5 and 63, run back to back without reset. |
| `lane_rows.hex` | `mac_lane` | 1,698 rows for those cases: weight, activation, the noise sample, `w_n` and the running accumulator. |
| `read_noise_vectors.svh` | both | Array sizes and field offsets. |

Each `.hex` lists its own field layout in its header comment. Word 0 is always the record
count, fields are one hex word each, `//` comments are allowed.

## Running them

**`noise_q.hex`.** For each entry, force the LFSR state to `lfsr` (or drive the
combinational stage directly), set `sigma = {1'b0, read_sigma[7:0]}` and check `noise_q`
one cycle later. Exact.

**`sequences.hex`.** Instantiate `read_noise` with the sequence's `LANE_ID` (it sets
`LANE_SALT`). Set `noise_seed`, pulse `reseed`, then pulse `advance` once per step and
check the state and `noise_q` against each step's pair. Leave idle cycles between some
advances: the sequence must not change (§5.2).

**`lane_cases.hex` with `lane_rows.hex`.** One `mac_lane` per lane ID (0, 5, 63). Run
that lane's cases in file order **without resetting between them**: several cases test
that the LFSR carries on from the previous one. For each case:

1. Set `noise_seed` and `read_sigma`, and set `NOISE_EN.read` from `flags[1]`.
2. If `flags[0]` is set, pulse `reseed`.
3. Write the case's rows' weights at `{pass, row}`, then issue the rows with `acc_en`,
   driving each row's activation on `act_bcast` with the lane's pipeline alignment.
4. Check `acc_out` against `expected`. For debugging, `lane_rows.hex` gives the expected
   `noise_q`, `w_n` and running sum for every row.

## Conventions the RTL must match

These are written into mac-array-spec §5.2:

- **Sample after the step.** A row's sample comes from the LFSR state *after* that
  row's advance. The first accumulate after a reseed uses `lfsr_step(seed_eff)`.
- **Advance without the enable.** The LFSR advances on every `acc_en` even when
  `NOISE_EN.read` is clear (`reseed_off_50` then `carry_on_s64_50` checks this).
- **Only reset and `reseed` reload.** A new `NOISE_SEED` with no reseed changes nothing.
- **Floor shifts, not truncation.** `ih9 = (ih_sum − 510) >>> 1` and
  `(ih9 · σ) >>> 9` are arithmetic shifts (round toward −∞). `/ 2` or `/ 512` in C
  rounds toward zero and fails this file.
- **Clamp ±127.** With these shifts `scaled` stays in −128 … 127, so the clamp only
  ever changes −128 (state `0x00000001` at σ = 255). `w_n` spans −255 … +254 because
  weights reach −128. That is still 9-bit signed.

## Known property: the noise is not zero-mean

The two floor shifts bias every sample by about **−0.5 LSB**, at every σ. Measured from
the spec arithmetic over 200,000 samples of one lane:

| `READ_SIGMA` | mean | sd | target sd `0.144·σ` |
|---|---|---|---|
| 1 | −0.497 | 0.500 | 0.144 |
| 12 | −0.495 | 1.758 | 1.728 |
| 64 | −0.43 | 9.26 | 9.22 |
| 255 | −0.469 | 36.8 | 36.7 |

At large σ the spread matches the spec. At small σ the bias dominates: at σ = 1 every
sample is 0 or −1 with equal odds. Summed over 128 rows at activation 255, the bias is
about −16,000 accumulator LSB per column, the same on every column. That is small next to
the clean signal, but the spec (§5.3) calls the noise zero-mean, which it is not.

Rounding once instead, `(x · σ + 512) >>> 10` on the unhalved centred sum, gives mean
≈ 0 for the same hardware cost. That is a Lane B + Lane C decision. These vectors follow
the spec as written, and `read_noise_mean_bias_is_minus_half_lsb_by_spec` in
`model/tests/test_read_path.cpp` pins the bias so the decision stays visible. If the
spec changes, update the model, rerun the generator, and that test is the one to edit.

## Mutation check

The model was broken 17 ways and the C++ tests were rerun. Every mutation was caught,
and all but one were caught by the replay of these vectors and the `mvm/` vectors alone:

- LFSR stepping only when read noise is enabled
- sample taken from the state before the step
- only lanes inside `ACTIVE_COLS` stepping
- clamp removed
- `>> 8` instead of `>> 9`
- truncating halving
- three bytes summed instead of four
- no lane salt
- LFSR reset on every compute
- `set_config` reseeding
- rows issued in reverse

The remaining six mutations (IR and ADC) are listed in `tb/golden/ir_drop/` and
`tb/golden/adc/`.

## Regenerating

```
python3 tb/golden/gen_read_path_vectors.py           # rewrite read_noise/, ir_drop/, adc/, mvm/
python3 tb/golden/gen_read_path_vectors.py --check   # what CI runs
```

Standard library only. Constants (`LFSR_TAPS`, `LANE_SALT_C`, `NOISE_CLAMP`,
`ACC_USED_W`, geometry) are read from `rtl/fecim_pkg.sv`. The stimulus seed is fixed.
Generated files are never hand-edited.
