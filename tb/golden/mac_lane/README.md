# tb/golden/mac_lane

Golden vectors for one `mac_lane`, clean datapath. Owned by Lane C; consumed by
Lane B's `mac_lane` testbench and by `model/tests/test_mac_lane_vectors.cpp`, which
replays every case through `model_exact`. If the RTL disagrees with these files,
the RTL is wrong — the Python generator and the C++ model already agree.

**Scope:** `noise_en = NOISE_NONE`, `read_sigma = 0`. Every expected value is the
integer dot product Σ w[r]·a[r] (w int8, a uint8). Read noise, ADC and IR drop get
their own vectors in week 5.

## Files

| File | What |
|---|---|
| `gen_mac_lane_vectors.py` | Generator. Stdlib only, fixed seed, reads geometry from `rtl/fecim_pkg.sv`. |
| `cases.hex` | One record per case. `$readmemh`-loadable. |
| `rows.hex` | One record per row of every case. `$readmemh`-loadable. |
| `mac_lane_vectors.svh` | Array sizes and field offsets for an SV testbench. |
| `cases.txt` | Human index: name, pass, rows, expected, what each case targets. |

Generated files are never hand-edited. Change the script, rerun it, commit both.
CI runs `gen_mac_lane_vectors.py --check` and fails if they have drifted.

## Format

Every field is one hex word, whitespace-separated, `//` comments allowed, CRLF
tolerated. Word 0 of each file is its record count.

```
cases.hex   1 + 5 words per case:  row_base  num_rows  pass  flags  expected
rows.hex    1 + 3 words per row:   weight    act       running
```

- `weight` is int8 in bits [7:0]; `act` is uint8; `expected` and `running` are
  int32, two's complement. `running` is the accumulator after that row is added.
- A case's rows are `rows[row_base .. row_base + num_rows - 1]`. Row `r` lives at
  lane address `pass * TILE_ROWS + r`.
- `flags[0]` = **WRITE**: write the case's weights before computing. When clear, the
  lane memory already holds them from an earlier case — the one-hot sweep and the
  `addr_*_reread` cases deliberately read without rewriting.

## Running a case

Run cases **in file order without resetting the weight memory** between them.

1. Tie `noise_en = NOISE_NONE`, `read_sigma = 0`, `reseed = 0`, and
   `lane_sel = LANE_ID` of the instance under test.
2. If WRITE: one `lane_we` pulse per row, `lane_waddr = pass*TILE_ROWS + r`,
   `lane_wdata = weight`.
3. Pulse `acc_clear` for one cycle (the control FSM's `CLEAR` state).
4. Issue rows back to back: `w_addr = pass*TILE_ROWS + r`, `acc_en = 1`, one row
   per cycle, then `acc_en = 0` and hold `LANE_PIPE_DEPTH` cycles — the same
   sequence as `issue_rows` in `tb/integration/lane_a_stub.sv`.
5. Drive `act` **two cycles behind** `w_addr` for the same row. In the array,
   `act_bcast` comes out of the activation M9K plus its broadcast register
   (activation-buffer-spec §4), so the lane sees row `r`'s activation two cycles
   after it saw row `r`'s address. Drive something non-zero (e.g. `8'hA5`) when no
   row is valid, so a misaligned lane picks up garbage rather than a harmless 0.
6. After the hold, `acc_out` must equal `expected`. Exact; there is no tolerance.

**Per-row check (optional, for debugging).** With signals driven on the falling
edge, the spec's cycle table puts row `r`'s contribution in the accumulator on the
`LANE_PIPE_DEPTH`-th rising edge after the edge that sampled row `r`'s address.
Sampling `acc_out` on the falling edge `LANE_PIPE_DEPTH + 1` cycles after driving
row `r` should give `running[r]`. The final-value check is the requirement; the
per-row check tells you *which* row went wrong.

A minimal loader:

```systemverilog
`include "mac_lane_vectors.svh"
logic [31:0] cases [MACV_CASES_DEPTH];
logic [31:0] rows  [MACV_ROWS_DEPTH];
initial begin
    $readmemh("tb/golden/mac_lane/cases.hex", cases);
    $readmemh("tb/golden/mac_lane/rows.hex",  rows);
    // case i:  cases[1 + i*5 + {0 row_base, 1 num_rows, 2 pass, 3 flags, 4 expected}]
    // row  j:  rows [1 + j*3 + {0 weight, 1 act, 2 running}]
end
```

## What the cases target

| Group | Cases | Catches |
|---|---|---|
| Single products | `prod_*` | Activation sign-extended instead of zero-extended (`act ≥ 0x80`); weight zero-extended; `w = -128` |
| Zeros | `zero_weights`, `zero_acts` | Stuck or leaking accumulate |
| Magnitude | `max_neg_full`, `max_pos_full`, `alternating_sign`, `swing_neg_then_pos` | Product zero-extended into the accumulator; accumulator too narrow; sign flips every row |
| Pipeline edges | `first3_full`, `last3_*`, `rows_n3`, `partial_n*` | First or last rows dropped; `acc_en` not delayed with the data; passes shorter than the pipeline |
| Alignment | `ramp_x_ramp`, `onehot_0` … `onehot_127` | Weight and activation one cycle apart; `onehot_127` is the last-row test |
| Clear | `clear_repeat_a/b` | `acc_clear` not taking — the second result doubles |
| Addressing | `addr_p0_write` … `addr_p1_reread` | `seq_cnt[7]` dropped from the write or read address |
| Random | `rand_000` … `rand_095` | Anything the directed cases did not anticipate |

## What these vectors cannot catch

- **Product bits above 16.** With noise off, `|w| ≤ 128` and `a ≤ 255`, so no product
  exceeds 32,640 and a multiplier truncated to 16 bits passes. The 17-bit products
  need read noise (`w_n` up to ±255) — mac-array-spec tests 6 and 7, week 5.
- **The drain shift chain.** At one lane it is just `acc <= shift_in`; the real test
  is column ordering across lanes (mac-array-spec test 8), which belongs to the array.
- Anything with noise, ADC or IR drop enabled.

## Regenerating

```
python3 tb/golden/mac_lane/gen_mac_lane_vectors.py           # rewrite
python3 tb/golden/mac_lane/gen_mac_lane_vectors.py --check   # what CI runs
```

The seed is fixed in the script. Changing it, the case list, or `TILE_ROWS` /
`NUM_LANES` in `fecim_pkg.sv` changes the files; commit them together.
