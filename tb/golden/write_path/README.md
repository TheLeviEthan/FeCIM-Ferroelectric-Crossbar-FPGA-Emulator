# tb/golden/write_path

Golden vectors for the write path: conductance quantization, device-to-device (D2D)
variation and stuck-at faults (write-path-spec §3–5). Owned by Lane C; consumed by
Lane B's `write_path` testbench and by `model/tests/test_write_path_vectors.cpp`,
which replays every case through `model_exact`. If the RTL disagrees with these
files, the RTL is wrong — the Python generator and the C++ model already agree.

**Scope:** what lands in the lane weight memory for one written weight byte, given
the write-time registers. Read noise, IR drop and ADC act at read time and get their
own vectors.

## Files

| File | What |
|---|---|
| `gen_write_path_vectors.py` | Generator. Stdlib only, fixed seed, reads constants from `rtl/fecim_pkg.sv`. |
| `configs.hex` | One record per register configuration. `$readmemh`-loadable. |
| `cases.hex` | One record per written weight. `$readmemh`-loadable. |
| `hash_seed0.hex` | `cell_hash16` for every cell address, both salts, `NOISE_SEED = 0`. |
| `hash_checksums.hex` | Order-dependent checksum of `cell_hash16` over all 65,536 addresses, 8 seeds × 2 salts. |
| `write_path_vectors.svh` | Array sizes and field offsets for an SV testbench. |
| `configs.txt` | Human index: each configuration, its registers, case count, how many cases change the weight, and what it targets. |

Generated files are never hand-edited. Change the script, rerun it, commit both.
CI runs `gen_write_path_vectors.py --check` and fails if they have drifted.

## Format

Every field is one hex word, whitespace-separated, `//` comments allowed, CRLF
tolerated. Word 0 of each file is its record count.

```
configs.hex          1 + 6 words per config:  noise_en  quant_levels  quant_mult  d2d_sigma  noise_seed  stuck_rate
cases.hex            1 + 4 words per case:    cfg  addr  w_in  w_expected
hash_seed0.hex       1 + 1 word per address:  {h_stuck[15:0], h_d2d[15:0]}
hash_checksums.hex   1 + 3 words per entry:   noise_seed_lo16  salt  checksum
```

- Config fields are the **register values** exactly as the host writes them
  (protocol §6): `quant_mult` is not recomputed from `quant_levels`, `d2d_sigma` is
  the 8-bit register (the RTL port is `{1'b0, reg[7:0]}`), `noise_seed` is all 32
  bits (only `[15:0]` may affect the result), `stuck_rate` is the 17-bit register
  (`[16]` = mode).
- `cfg` indexes `configs.hex`. Cases are grouped by config, in config order.
- `addr` is the **global** cell address `col * TILE_ROWS + row`, the same value as
  `bulk_addr` in the weight stream.
- `w_in` and `w_expected` are int8, two's complement in bits `[7:0]`.

## Running a case

1. Drive the write path's config inputs from the case's config record: `noise_en`,
   `quant_levels`, `quant_mult`, `d2d_sigma = {1'b0, cfg[3][7:0]}`, `noise_seed`,
   `stuck_rate = cfg[5][16:0]`. Change them only between cases, never while a write
   is in the pipeline.
2. Pulse `bulk_we` for one cycle with `bulk_target = TGT_WEIGHTS`,
   `bulk_addr = addr`, `bulk_data = w_in`. Back-to-back writes on consecutive cycles
   are legal and worth testing; the co-simulation feeds bytes much faster than a
   UART does.
3. When `lane_we` asserts for that write, check:
   - `lane_wdata == w_expected[7:0]`
   - `lane_sel   == addr[12:7]` (column mod 64)
   - `lane_waddr == {addr[13], addr[6:0]}` (pass, row)

   Exact; there is no tolerance. The address checks are not in the file because
   they are a pure function of `addr`.

A minimal loader:

```systemverilog
`include "write_path_vectors.svh"
logic [31:0] cfg [WPV_CFG_DEPTH];
logic [31:0] cs  [WPV_CASE_DEPTH];
initial begin
    $readmemh("tb/golden/write_path/configs.hex", cfg);
    $readmemh("tb/golden/write_path/cases.hex",   cs);
    // config i:  cfg[1 + i*6 + {0 noise_en, 1 levels, 2 mult, 3 sigma, 4 seed, 5 rate}]
    // case j:    cs [1 + j*4 + {0 cfg, 1 addr, 2 w_in, 3 w_expected}]
end
```

## Check the hash first

If the RTL `cell_hash16` differs from the C++ one in a single bit, nothing after it
can match, and the case failures will look random. So before running `cases.hex`:

1. Compare `fecim_pkg::cell_hash16(a, SEED_D2D)` and `(a, SEED_STUCK)` against
   `hash_seed0.hex` for every `a` in `0 .. 16383`.
2. For each entry of `hash_checksums.hex`, fold over all 65,536 addresses and compare:

   ```systemverilog
   logic [31:0] c = 32'h811C9DC5;
   for (int a = 0; a < 65536; a++)
       c = (c ^ {16'h0, cell_hash16(a[15:0], seed_lo16 ^ salt)}) * 32'h01000193;
   ```

   A plain sum or XOR would not work here: `cell_hash16` is a bijection on 16 bits,
   so every correct *or incorrect* bijection sums to the same value. The fold is
   order-dependent, so one wrong output at one address changes it.

## What the configurations target

See `configs.txt` for the full list. In short:

| Group | Catches |
|---|---|
| `bypass_lossy_regs`, `read_ir_adc_only` | Any stage active when its enable is clear; reacting to the read-time enable bits. With every write-time enable clear the path must be a pure pass-through (the L0 gate). |
| `quant_*` | Stage 1, N ∈ {2, 3, 5, 8, 255}, plus a mismatched `QUANT_LEVELS` / `QUANT_MULT` pair that must wrap, not be rejected. |
| `d2d_s*` | Scale `>>> 8` (not `>> 9`), floor rounding on negatives (not truncation), two hash bytes (not one), **saturation at +127 / −128 (never wrap)**, σ = 0 exactly the identity. |
| `d2d_s255_seed*` | Seed handling; `0xABCD1234` must equal `0x1234` because only `NOISE_SEED[15:0]` reaches the write path. |
| `stuck0_*`, `stuckR_*` | Rate compare is **strict `<`** (`stuck0_edge_r77` / `_r78`: the cell with `h_stuck == rate` is not stuck, one more and it is); mode bit; rail value ±127; separate salts for the two hashes. |
| `setA_*` and other combinations | Stage order quant → D2D → stuck, and stuck overriding everything before it. |

Every case set includes edge weights (−128, −127, −1, 0, 1, 126, 127) at edge
addresses (first and last row, first and last column, both passes), cells with the
largest D2D draws in each direction, the first stuck cells for the config's seed,
one stuck cell of each rail sign, and 48 random pairs.

## Conventions the RTL must match

These are settled by the model and written into write-path-spec §5:

- **Rail sign:** `h_d2d[15] = 1` → −127, `h_d2d[15] = 0` → +127. The D2D hash is
  computed for the rail sign even when `NOISE_EN.d2d` is clear.
- **Saturation limits:** D2D saturates to +127 / −128. Stuck-at-rail forces ±127.
- **Hash address:** the global cell address (`bulk_addr`), not the lane-local
  `{pass, row}`.
- **Hash seeds:** `noise_seed[15:0] ^ SEED_D2D` and `noise_seed[15:0] ^ SEED_STUCK`.
- **Write-time only:** stages use the registers in effect when the weight is written.
  Changing `NOISE_SEED`, `D2D_SIGMA` or `STUCK_RATE` later does not change stored
  weights; the host reloads.

## Mutation check

The C++ tests and these vectors were run against 13 deliberately broken versions of
the model. Every one was caught by the vectors alone: wrapping add, lane-local
address, `>> 9`, truncating division, one hash byte, `<=` threshold, rail sign from
`h_stuck`, inverted rail sign, mode bit ignored, stuck before D2D, shared salt,
upper seed bits leaking in, and D2D gated by the wrong enable bit.

## Regenerating

```
python3 tb/golden/write_path/gen_write_path_vectors.py           # rewrite
python3 tb/golden/write_path/gen_write_path_vectors.py --check   # what CI runs
```

The stimulus seed is fixed in the script. Changing it, the configuration list, or
the hash constants in `fecim_pkg.sv` changes the files; commit them together.
