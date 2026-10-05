# FeCIM: Model and Verification Design

**Owner:** Lane C
**Consumers:** Lanes A and B (RTL oracle), Lane D (`sim` backend and sweeps), the report
**Milestone:** model core weeks 2–5, calibration week 7, coverage complete week 10
**Revision:** 2 — calibration reworked for published FeFET parameter sources

---

## 1. The structural decision: two models, not one

The most common way this goes wrong is writing one "reference model," discovering it does
not match the RTL, and being unable to tell whether the RTL is buggy or the model is.

There are actually two different questions, and they need two different artifacts:

| | `model_ideal` | `model_exact` |
|---|---|---|
| Question it answers | What *should* a perfect crossbar compute? | What does *this* hardware compute? |
| Arithmetic | float64 / int64, NumPy | integer, mirrors RTL bit for bit |
| Non-idealities | none | all of them |
| Speed | fast, vectorized | slower, cycle-faithful |
| Role | validates `model_exact` in the clean case | oracle for the RTL |

The verification chain is two links, not one:

```
   NumPy ideal  ──(clean case only)──►  model_exact  ──(every case)──►  RTL
```

`model_ideal` cannot check the noisy cases — it has no notion of noise. `model_exact` cannot
check itself. Together they pin down both ends: if RTL and `model_exact` disagree, one has a
bug; if they agree but `model_ideal` disagrees in the clean case, the bug is in
`model_exact`'s arithmetic.

---

## 2. Bit-exactness constrains how the model is written

`model_exact` must reproduce the RTL **exactly**, not approximately. That forbids things
that would otherwise be natural:

- **No floating point anywhere in the datapath.** Sigma scaling must be an integer multiply
  and arithmetic shift matching the RTL — `>>> 9` for `READ_SIGMA`, `>>> 8` for `D2D_SIGMA`
  (protocol §6.3) — not a float multiply and round. Neither register is Q1.8.
- **The hash must be identical**, including overflow. Use `uint16_t` so the 16-bit rounds
  wrap the way the RTL does; a wider type silently changes results.
- **The LFSR advances on the same events** — on accumulate, not on every step. Model a
  clock-gated LFSR, not a random number generator.
- **Clamp where the RTL clamps.** Read noise clamps at ±127; the write-path add saturates at
  ±127/−128; the lane add does *not* saturate because 9 bits cover the range. Adding a
  "safety" clamp the RTL does not have breaks equivalence.
- **Rounding direction must match.** ADC round-to-nearest is `(x + half) >> sh`, not
  `round(x / 2^sh)`, which differs on negative halves.
- **Distribution shapes must match the construction, not the intent.** Read noise sums four
  LFSR bytes; write-path variation sums two hash bytes and is triangular. The model
  reproduces those exact sums, not an idealized Gaussian.

### 2.1 One thing that is free

Integer addition is associative and commutative, so the RTL's sequential row-by-row
accumulation and NumPy's `matmul` produce identical sums regardless of order.

Worth appreciating: had the datapath been floating point, accumulation order would matter and
bit-exact comparison against a vectorized reference would be impossible. The entire
verification strategy rests on the design being integer.

---

## 3. The equivalence hierarchy

Five levels, in dependency order. Each is a different kind of claim needing a different kind
of test.

| Level | Claim | Type | Gate |
|---|---|---|---|
| **L0** | Clean RTL ≡ clean `model_exact` ≡ `model_ideal` | exact | week 4 |
| **L1** | Each non-ideality alone: RTL ≡ `model_exact` | exact | week 5 |
| **L2** | All non-idealities together: RTL ≡ `model_exact` | exact | week 5 |
| **L3** | `model_exact` statistics match published device behaviour | **statistical** | week 7 |
| **L4** | Hardware ≡ `model_exact` at matched seed | exact | week 5 |

**L3 is categorically different and must not be conflated with the others.** L0–L2 and L4 are
bit-exactness claims about whether the implementation is correct. L3 asks whether the
implementation reproduces the physics the literature reports — measured mean, variance, and
fault rate against published values. It can only fail by being unphysical, never by being off
by one.

Keeping them separate matters because they fail differently and different people fix them. An
L2 failure is an RTL bug. An L3 failure is a calibration problem. Reporting them in the same
suite invites confusion about which is which.

### 3.1 L0 is the load-bearing gate

Nothing downstream means anything until clean RTL matches clean model exactly over thousands
of random matrices. Run it in CI on every commit. If it goes red, stop feature work.

---

## 4. Model architecture

### 4.1 One implementation, three consumers

```
                     ┌──────────────────────┐
                     │   model_exact (C++)  │
                     └──┬────────┬───────┬──┘
                        │        │       │
         ┌──────────────┘        │       └──────────────┐
         ▼                       ▼                      ▼
  Verilator co-sim        pybind11 module        calibration tools
  (RTL oracle)            (sim backend)          (parameter fitting)
```

The `sim` backend must be **the same code** that verified the RTL. An independent Python
reimplementation could drift, and then the "universal build target" would be running something
never validated against hardware.

### 4.2 Packaging maps onto M0's build requirement

M0 asks for a source distribution buildable with a single command *and* binary distributions
for supported platforms. The C++/pybind11 setup satisfies both directly:

- **Source distribution:** `pip install .` compiles the extension. Needs a C++17 compiler —
  present by default on Ubuntu and macOS, needs MSVC Build Tools on Windows. Document this
  prominently; it is the one friction point.
- **Binary distribution:** `cibuildwheel` in GitHub Actions produces wheels for Windows,
  Ubuntu, and macOS on every tagged release.

Keep the C++ core **dependency-free** (C++17 standard library only). pybind11 is header-only
and pip-installable. Anything more — Eigen, Boost — turns a one-command build into a support
ticket.

---

## 5. Co-simulation harness

The highest-value infrastructure Lane C builds, and the reason Verilator was chosen over
Icarus. Verilator compiles the RTL to C++, so model and DUT run in one process with full
visibility into internal signals:

```cpp
for (int cycle = 0; cycle < n_cycles; cycle++) {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
    model.step();

    for (int l = 0; l < NUM_LANES; l++) {
        if (dut->rootp->fecim_top__DOT__acc[l] != model.acc[l]) {
            report_mismatch(cycle, l, dut_value, model_value);
            return FAIL;
        }
    }
}
```

**The failure message says "lane 17 diverged at cycle 412," not "the answer is wrong."** That
difference is worth days over a semester.

Compare at three depths, in this order — the first mismatch upstream explains all the ones
downstream:

1. `w_n` (post-noise weight) per lane
2. `prod` per lane
3. `acc` per lane

### 5.1 Virtual UART

For integration tests, drive the Verilated top level through the same byte stream the real
board would receive. Lane D's packet encoder is then tested against the actual RTL parser in
CI before hardware exists — which is how the week-3 bring-up becomes a wiring exercise rather
than a protocol debugging session.

---

## 6. Coverage without coverage tools

Questa Intel Starter Edition has no functional coverage, so coverage is defined and tracked
manually — as an explicit matrix in the repo, not as a feeling about how much testing has
happened.

### 6.1 The verification matrix

| Config | Directed | Random ×1000 | Corner | Hardware |
|---|---|---|---|---|
| Clean, 128×128 | ✓ | ✓ | ✓ | ✓ |
| Quant only, N ∈ {2,3,5,8,255} | ✓ | ✓ | ✓ | ✓ |
| D2D only, σ ∈ {0, 8, 32, 255} | ✓ | ✓ | ✓ | ✓ |
| Read noise only | ✓ | ✓ | ✓ | ✓ |
| Stuck only, rate ∈ {0, 1%, 10%} | ✓ | ✓ | ✓ | ✓ |
| ADC only, bits ∈ {4, 8, 16, 32} | ✓ | ✓ | ✓ | ✓ |
| IR only | ✓ | ✓ | ✓ | ✓ |
| All enabled | ✓ | ✓ | ✓ | ✓ |
| Tile 8×8, 32×32, 48×48, 128×128 | ✓ | ✓ | — | ✓ |

Non-power-of-two quantization levels (3, 5) and non-multiple-of-64 tile widths (48) are there
deliberately — they exercise the reciprocal path and the argmax mask that powers of two would
hide.

### 6.2 Corner cases, enumerated

Random stimulus will not find these:

- All weights `+127`, all activations `255`, maximum positive noise — accumulator headroom
- Same with `−128` — negative overflow and sign handling
- All weights zero — argmax tie across every column
- Exactly two columns tied at maximum — tie-break rule
- One-hot activation at row 127 — last-row pipeline inclusion
- One-hot activation at row 0 — first-row clear-versus-accumulate priority
- `stuck_rate = 0xFFFF` — every cell stuck
- `QUANT_LEVELS = 2` — binary weights
- `adc_bits = ACC_USED_W` — ADC enabled but lossless, must be an exact no-op (and any larger value)
- `NOISE_SEED = 0` — valid, not remapped; every lane LFSR must still advance (`| 1` guard)
- Register reset state — `QUANT_LEVELS` = 255 with `QUANT_MULT` = 257, `ADC_BITS` = `ACC_USED_W`
- Maximum read noise with weights at ±127 — assert `w_n` never exceeds 9 bits signed

The "enabled but lossless" case is a good bug detector: a path that changes the answer when it
should not means the shift arithmetic is wrong.

---

## 7. Calibration: from published measurements to register values

This is a different activity from everything above. Verification asks "is the implementation
correct." Calibration asks "are the numbers right."

### 7.1 The source change, and what it buys

Parameters come from **published FeFET device characterization**, not in-house capacitor
measurements. Three consequences worth stating plainly in the report:

**The polarization-to-conductance bridge is gone.** In-house P–E data on capacitors would have
required assuming polarization state → threshold voltage shift → channel conductance. That was
the weakest link in the chain. Published FeFET work reports conductance or current directly,
so the assumption does not arise. **This is a rigor improvement, not a compromise.**

**One process becomes several.** A single lab's data gives one point. Published data gives a
distribution across fabs, nodes, and programming schemes — which enables a question a
single-device study cannot answer: how much does device variability *alone* move network
accuracy?

**Everything is reproducible.** Every parameter traces to a citation, so any reader can rerun
the study.

### 7.2 The chain, per parameter

Every register value gets a documented derivation in `docs/device-model.md`:

| Register | Extract from | Chain | Typical confidence |
|---|---|---|---|
| `QUANT_LEVELS` | reported multilevel states | distinguishable conductance levels, usually stated directly | direct |
| `QUANT_MULT` | computed from `QUANT_LEVELS` | host arithmetic, no measurement | exact |
| `D2D_SIGMA` | device-to-device spread | conductance variance across cells | direct |
| `READ_SIGMA` | cycle-to-cycle variation | repeated-read spread on one cell | direct |
| `STUCK_RATE` | yield / failed-bit data | fraction of non-programmable cells | varies — often under-reported |
| `ADC_BITS` | peripheral circuit assumptions | design choice, not a device property | design parameter |
| `atten_rom` | wire resistance and array size | circuit-level estimate | **estimated** |
| retention factor (sem. 2) | retention measurements vs time | state decay toward depolarized | direct |

**Mark the confidence column honestly.** A reviewer who sees "estimated" next to `atten_rom`
will trust the "direct" entries more, not less. Presenting every parameter with equal authority
undersells the ones that are genuinely well-sourced.

### 7.3 Provenance is the new load-bearing assumption

Every calibration rests on one. With public data it is **parameter-set provenance**.

Published parameters come from different papers, processes, nodes, and measurement conditions.
Taking `QUANT_LEVELS` from one source and `D2D_SIGMA` from another silently assumes they
describe the same device — which is frequently false and always invisible in the final number.

The rule:

- **Each complete parameter set traces to a single source where possible.** Name it.
- **Composite sets are labelled composite**, with each parameter's source listed, and are not
  presented as characterizing a real device.
- **A parameter with no usable published value is swept, not guessed** (§7.5).

`docs/device-model.md` is structured as one table per parameter set, with columns for
parameter, value, source citation, and confidence. That table is what a skeptical reader will
actually check.

### 7.4 Target: three parameter sets

Aim for three complete sets from distinct processes. The comparison across them is the result;
any one of them alone is just a configuration.

Assign specific papers to specific people in week 1 and record it in the issue tracker. Finding
usable sets is a search problem with uncertain completion time and no hardware dependency, so
it should run in the background from the start rather than becoming a week-7 scramble.

What makes a source usable: it reports enough parameters to fill most of a set, states
measurement conditions, and gives spread rather than only a mean. A paper reporting a single
"8 levels achieved" with no variance data is a partial source at best.

### 7.5 Sweep what the literature does not pin down

Where a parameter is poorly reported — `STUCK_RATE` especially, since yield data is often
omitted — do not pick a point estimate. Sweep it across a plausible range and present a band.

"Accuracy stays above 90% for stuck-cell rates below 2%" is a stronger and more honest claim
than "accuracy is 93.2% at our assumed fault rate," and it survives someone disagreeing with
the assumption.

---

## 8. Reproducibility

Every result in the report must be regenerable from the repo:

- **Seeds recorded, never implicit.** Every sweep writes its seed set to the results file. A
  plot that cannot be regenerated is not evidence.
- **Golden fixtures in the repo**, not generated at test time. `tb/golden/` holds byte
  sequences with hand-computed CRCs, shared between the C++ testbench and the Python driver
  tests, so encoding drift fails immediately in both places.
- **Parameter sets are version-controlled data files**, not values typed into a script. One
  file per source, with the citation in the file.
- **Results carry provenance.** Each sweep output records git commit, RTL build ID, the
  `IDENTIFY` string from the board, the parameter set used, and full config.
- **CI runs L0 on every push.** Feature work stops if it goes red.

With public data, reproducibility stops being merely good practice and becomes the project's
main claim to credibility — a reader with no special access can rerun everything.

---

## 9. Lane C schedule

| Week | Deliverable |
|---|---|
| 1 | C++ skeleton, quantization only, unit test harness, CI green. **Literature survey begins.** |
| 2 | `model_ideal` clean MVM; `model_exact` clean path; L0 in simulation |
| 3 | Hash, LFSR, all non-ideality math; candidate parameter sources shortlisted |
| 4 | Verilator co-sim harness; **L0 gate on real RTL**; virtual UART |
| 5 | L1 and L2 across the matrix; **L4 on hardware** |
| 6 | Regression suite stable; supports the first end-to-end demo |
| 7 | **First parameter set complete**; `docs/device-model.md` with provenance and confidence |
| 8 | Matrix rerun at 128×128 / 64 lanes |
| 9 | L3 statistical validation; uncertainty bands for sweeps |
| 10 | Coverage matrix closed; verification report drafted |
| 11 | Verification report final, including known gaps |

Weeks 4 and 7 are the hard gates: the co-simulation harness and the first documented parameter
set.

---

## 10. Open questions

1. **Cycle-accurate or transaction-accurate `model_exact`?** Cycle-accurate makes the co-sim
   comparison per-cycle and localizes bugs precisely; transaction-accurate is faster for
   sweeps. Recommend cycle-accurate core with a fast path that skips intermediate state when
   only the final result is needed.
2. **Should the `sim` backend report a modelled cycle count** so throughput plots work without
   a board? Cheap, and it makes the demo fallback more complete.
3. **How many published sources are realistically available** with enough parameters to fill a
   complete set? This determines whether the three-process comparison is achievable. Answer it
   in weeks 1–3, not week 7 — and if only one usable set exists, the semester-2 plan needs to
   change while there is still time.
4. **Do any sources report level-dependent variation?** Larger spread at intermediate states is
   physically expected, since those rely on partial domain switching. If a source reports it,
   modelling it costs one small lookup table and is a genuinely distinctive result.
