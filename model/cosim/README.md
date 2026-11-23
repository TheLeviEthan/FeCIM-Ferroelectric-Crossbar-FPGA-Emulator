# model/cosim

Verilator co-simulation: steps the RTL and the C++ reference model together and
requires them to agree exactly. Owned by Lane C.

Two harnesses:

| Harness | DUT | Model side | Status |
|---|---|---|---|
| `run_l0_array` | `mac_array`: 64 lanes and the drain chain | `ModelExact::step()`, every cycle | **L0 gate, full array** |
| `run_l0` | one `mac_lane` (`LANE_ID`, default 5) | stored weights through a small mirror, every cycle | single-lane slice, kept for lane-level edge cases |

## `run_l0_array`: the full-array L0 gate

Runs the Verilated `mac_array` in lockstep with `ModelExact::step()` on the clean datapath
(`NOISE_EN = 0`):

| Level | Compared | On mismatch prints |
|---|---|---|
| Every cycle, every lane | `w_n`, then `prod`, then `acc` (upstream first), plus `chain_out` | matrix, cycle, FSM state, `seq_cnt`, lane, signal, both values |
| Every COMPUTE | results drained from the RTL chain == `ModelExact::results()` == `model_ideal` (fed the weights as written) | matrix, column, all three values |

How it works:

- **The harness plays `control_fsm` and `act_buffer`.** It drives `seq_cnt`, `acc_clear`,
  `acc_en` and `shift_en` from `ModelExact::ctrl()` before each edge. It drives
  `act_bcast` itself, `kActDelay = 2` edges after the row was issued
  (activation-buffer-spec §4), and `0xA5` on every other cycle, which the lanes must
  ignore. So it tests the array, not the FSM or the activation buffer; that is C5. Don't
  read a green L0 as "the full design works".
- **Lane internals** are read through Verilator's `--public-flat-rw` scope table
  (`TOP.mac_array.g_lane[l].u_lane` → `w_n`, `prod`, `acc`). The RTL has no probe ports.
  If a signal or the generate block is renamed, the harness stops at start-up and names
  it.
- **Every matrix rewrites the whole physical tile** through `lane_we` / `lane_sel` /
  `lane_waddr` (16,384 writes), whatever the active size. The cells outside the active
  region then hold junk that must not leak in.
- **The stimulus** is:
  - rails: ±127/−128 × 255 over every row, alternating sign by column;
  - a column ramp: a distinct value per column, to expose drain order and lane-select
    slips;
  - then random tiles. Every fourth random tile keeps the weights and changes only the
    activations, to check retention.

### Run

```bash
make -C model/cosim run_l0_array
./model/cosim/run_l0_array [seed] [n] [rows] [cols]   # default: seed 1, 50 random, 128 x 128
./model/cosim/run_l0_array 1 1000                     # the C4 gate
./model/cosim/run_l0_array 7 100 48 48                # any active tile size
```

**Runtime** (Verilator 5.020, `-O2`, one core): build about 9 s. The full gate,
`run_l0_array 1 1000`, takes 12 s: 1,002 computes, 16.8 M cycles and 76.5 M checks. Most
of those cycles are the weight writes. 100 matrices at 8×8, 48×48 or 64×64 take about 1 s
each. CI runs 50 matrices at 128×128 and at 48×100.

### Mutation check

Nine bugs were injected one at a time into a scratch copy of the RTL, then
`run_l0_array 1 20` was run. Each was caught at the first cycle where it shows:

| Injected bug | Reported at |
|---|---|
| activation sign-extended (`act9 = {act[7], act}`) | `rails`, ACC, seq_cnt 3: lane 0 `prod` |
| `lane_sel` bits rotated in `mac_array`'s write address | `rails`, settle after programming: lane 1 `w_n` |
| pipeline one stage short (accumulate on `v2`) | `rails`, first DRAIN cycle: lane 0 `acc` |
| drain chain reversed | `rails`, DRAIN: lane 0 `acc` |
| pass bit dropped in the lane's write decode | `rails`, ACC pass 1 (seq_cnt 130): lane 0 `w_n` |
| weight alignment register skipped (`w_n` from `w_rd`) | `column ramp`, ACC: lane 0 `w_n` |
| product sampled on `v1` | `rails`, ACC seq_cnt 2: lane 0 `prod` |
| drain chain tail wraps instead of shifting in 0 | `rails`, DRAIN: lane 63 `acc` |
| lane decode off by one | `rails`, settle after programming: lane 0 `w_n` |

## `run_l0`: single lane

Verilates one `mac_lane` and checks it on the clean datapath at two levels:

| Level | Compared | On mismatch prints |
|---|---|---|
| Every clock cycle | DUT accumulator vs a model-side mirror: products from `model_exact`'s stored weights, retired `LANE_PIPE_DEPTH` cycles after issue, RTL priority clear > accumulate > shift | test, pass, cycle, both values |
| Every pass | DUT `acc_out` vs `model_exact::compute()` vs `model_ideal` (fed the weights as written) | test, pass, all three values |

What `run_l0_array` cannot reach lives here. The array harness drives the lane through a
real sequencer, so the following never happen there:

- clear/accumulate/shift collisions;
- idle gaps between issued rows;
- a shift load followed by a hold.

Tests:

- accumulator rails;
- a whole-tile write;
- a reread without rewriting;
- random weights, active rows and idle gaps;
- shift load and hold;
- the priority collisions above.

It was mutation-checked against seven injected `mac_lane` bugs; each failed with the
cycle named.

```bash
make -C model/cosim run_l0
./model/cosim/run_l0 [seed] [n]      # default seed 1, 200 random tests
make -C model/cosim clean && make -C model/cosim run_l0 LANE_ID=63
```

## Build notes

- `make -C model/cosim` builds both. `make -C model/cosim VFLAGS=-Wno-fatal` keeps
  building while an RTL edit has lint warnings.
- Needs Verilator ≥ 5 and a C++17 compiler.
- If the checkout path contains a space (Verilator refuses to build there), the Makefile
  stages the sources to `$TMPDIR/fecim-cosim-$USER/<target>`, builds there and copies the
  binary back. Both harnesses do this.

## Next

- **B4:** when `read_noise` lands in `mac_lane`, add a noisy mode, with `NOISE_EN.read`
  and the LFSR compared per lane through `ModelExact::lane().lfsr`. `run_l0.cpp` needs
  updating in the same PR if the lane ports change.
- **C5:** Verilate `fecim_top`, so the real `control_fsm` and `act_buffer` drive the
  array, and compare `ModelExact::ctrl()` against them too.
