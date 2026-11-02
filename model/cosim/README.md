# model/cosim

Verilator co-simulation: steps the RTL and the C++ reference model together and
requires them to agree exactly. Owned by Lane C.

## Status: single-lane L0 slice

`run_l0` Verilates one `mac_lane` (`LANE_ID`, default 5) and checks it on the
clean datapath (`NOISE_EN = 0`) at two levels:

| Level | Compared | On mismatch prints |
|---|---|---|
| Every clock cycle | DUT accumulator vs a model-side mirror: products from `model_exact`'s stored weights, retired `LANE_PIPE_DEPTH` cycles after issue, RTL priority clear > accumulate > shift | test, pass, cycle, both values |
| Every pass | DUT `acc_out` vs `model_exact::compute()` vs `model_ideal` (fed the weights as written) | test, pass, all three values |

Tests: accumulator rails (+127 / −128 × 255 over 128 rows), whole-tile write
(lane select: 63 of 64 bus writes belong to other lanes), reread without
rewrite, random weights / active rows / idle gaps, shift load and hold, and
clear/accumulate/shift collisions.

The harness has been mutation-checked: seven injected `mac_lane` bugs (wrong
activation bit, accumulating a stage early, sign-extended activation, shift
beating accumulate, no lane decode, dropped pass bit, `acc_en` ignored) each
fail with the cycle named.

## Run

```bash
make -C model/cosim                  # build model/cosim/run_l0
./model/cosim/run_l0 [seed] [n]      # default seed 1, 200 random tests
make -C model/cosim clean && make -C model/cosim LANE_ID=63
make -C model/cosim VFLAGS=-Wno-fatal   # e.g. while an RTL edit has lint warnings
```

Needs Verilator ≥ 5 and a C++17 compiler. If the checkout path contains a
space (Verilator refuses to build there), the Makefile stages the sources to
`$TMPDIR/fecim-cosim-$USER`, builds there and copies `run_l0` back.

## Next

The `Mirror` class in `run_l0.cpp` is the stand-in for `ModelExact::step()` /
`lane()`. When the cycle-level model lands, the harness Verilates `mac_array`
(then `fecim_top`), calls `step()` once per edge and compares every lane's
`w_n`, `prod` and `acc` in that order, so the first divergence names the lane,
the cycle and the pipeline stage (model-verification-design §5).
