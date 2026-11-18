# tb/golden/mvm

Whole matrix–vector computes with any mix of the six non-idealities: write a tile,
write activations, compute, compare every column. Owned by Lane C. These are the
end-to-end check that the read path, write path and pass/lane mapping fit together.
Consumed by `model/tests/test_read_path_vectors.cpp` (every case through one
`ModelExact`), and later by the full-array co-simulation and the system testbench.

## Files

| File | What |
|---|---|
| `cases.hex` | 17 cases, 12 words each: the register configuration, flags and an offset into `data.hex`. The header comment lists every case and what it targets. |
| `data.hex` | 34,192 words: the weights, activations, attenuation coefficients and expected results the cases point to. |

## Running them

Run all 17 cases **in order on one design**, reset once at the start and never between
cases. Several cases check what carries over: LFSR state and stored weights. For each
case:

1. Write the configuration registers: `ACTIVE_ROWS`, `ACTIVE_COLS`, `NOISE_EN`,
   `QUANT_LEVELS`, `QUANT_MULT`, `D2D_SIGMA`, `READ_SIGMA`, `NOISE_SEED`, `ADC_BITS`,
   `STUCK_RATE`, exactly as listed. They are register values; nothing is recomputed.
2. Starting at `data_base` in `data.hex`, take, in order:
   - if `flags[1]`: `active_rows × active_cols` weights, column-major (column outer, row
     inner), each written at `bulk_addr = col · TILE_ROWS + row`;
   - if `flags[2]`: `active_rows` activations;
   - if `flags[3]`: 128 attenuation coefficients.
3. If `flags[0]`: pulse reseed (`CTRL[2]`).
4. Compute.
5. The next `active_cols` words are the expected int32 results. Exact, no tolerance.

Weights pass through the write path when written, so the write-time settings in effect
*at write time* decide what is stored.

## The cases

| # | Case | Checks |
|---|---|---|
| 0 | `clean_16x100` | Clean, two passes. The LFSRs advance anyway. |
| 1–3 | `read_s64_*` | Read noise continues from where the clean compute left it, differs on an identical repeat, and reseed reproduces a fresh chip. |
| 4 | `read_s255_seedBEEF_48x48` | Full-strength noise, other seed, one pass. |
| 5–6 | `ir_*` | IR ramp, and the reset table (255 is not the identity). |
| 7–12 | `adc*` | 8, 4, 12 and 31 bits. Disabled ADC ignores `ADC_BITS`. 32 reaches the 5-bit port as 0. |
| 13 | `d2d_without_reload` | Changing a write-time register without rewriting weights changes nothing. |
| 14 | `setA_all_six_64x48` | Calibration set A with read noise, IR, ADC and 1 % stuck cells. |
| 15–16 | `all_six_128x128*` | The full tile with everything on, twice without reseed. |

Before writing anything, the generator asserts the relationships the names promise:

- case 2 ≠ case 1, and case 3 ≠ case 1;
- cases 10, 11 and 13 are all equal to each other;
- case 12 ≠ case 10;
- case 16 ≠ case 15.

## Conventions

These are the conventions of `read_noise/`, `ir_drop/`, `adc/` and `write_path/`
together, plus two mapping rules:

- **Pass-major order.** `passes = ceil(ACTIVE_COLS / 64)`. Pass `p`, lane `l` serves
  column `p · 64 + l`. All rows of pass 0 run before pass 1, and one lane's LFSR runs
  continuously through both.
- **Unused lanes still run.** Lanes beyond `ACTIVE_COLS` advance their LFSRs and
  accumulate. Only their results are discarded.

## Regenerating

See `tb/golden/read_noise/README.md`: one generator,
`tb/golden/gen_read_path_vectors.py`, writes all four read-path folders.
