# FeCIM: Project Plan

**Ferroelectric crossbar MAC emulator on DE10-Lite**
CEN 4907C / 4908C — four-person team, two semesters

**Revision 4.** Device parameters now sourced from published FeFET characterization rather
than in-house capacitor measurements; radiation effects removed from scope. Resource figures
verified against the DE10-Lite User Manual v1.6 and the MAX 10 Embedded Multipliers User
Guide — see `docs/hardware-verification.md`.

---

## 1. What this is

A digital emulator of an analog ferroelectric crossbar tile, implemented in SystemVerilog on
a MAX 10 FPGA, with inherent device non-idealities injected into the datapath at the points
where they physically occur, calibrated against published FeFET device characterization.

**It is not a simulator.** The distinction is not cosmetic — it determines whether this reads
as a software project with an FPGA attached or as hardware engineering. The RTL performs the
same matrix-vector multiply a physical crossbar performs, on real silicon, with a host API
and a physical interface.

**Project type under M0: Infrastructure.** Engineers are the users, it facilitates
development of other artifacts, it has both an API and a physical interface, and the sample
application is digit classification running on the array.

### 1.1 What the contribution is

The emulator itself is not novel — device-aware crossbar simulation is a mature field. Two
things make this work worth doing:

**A digital hardware implementation with independently switchable non-idealities.** Every
effect can be isolated, so accuracy loss is attributable to a specific physical mechanism
rather than reported as a single aggregate number.

**Calibration across multiple published processes rather than one.** Device parameters vary
substantially between fabs and process nodes. Calibrating against several published FeFET
datasets and reporting the resulting accuracy spread answers a question a single-device study
cannot: how much does device variability *alone* change network accuracy?

Every parameter is traceable to a citation, so every result is independently reproducible.

---

## 2. Target hardware

Terasic DE10-Lite, Intel MAX 10 `10M50DAF484C7G`.

| Resource | Available |
|---|---|
| Logic elements | 49,760 |
| M9K memory blocks | 182 (1,638 Kbit) |
| Embedded multiplier blocks | **144** |
| PLLs | 4 |
| Clock | 50 MHz (`MAX10_CLK1_50`, `PIN_P11`) |

### 2.1 The 9-bit rule

The single most important constraint in the design. Each multiplier block is **either** one
18×18 multiplier **or** two 9×9 multipliers. Any operand of 10 bits or more consumes a whole
block.

Keeping every multiply at 9×9 signed is what makes the 64-lane array fit. An earlier revision
used 10-bit operands and would have needed 128 blocks for the array alone. The package
carries elaboration assertions so a future widening fails at compile time.

A block shares one `signa`/`signb` pair between its two multipliers, so paired multiplies must
agree on signedness. Everything here is signed × signed; unsigned quantities are zero-extended
to signed-positive. Converting any of them to an unsigned multiply looks like a free
optimization and silently prevents pairing.

### 2.2 The board has no USB-UART bridge

The mini-USB port is USB-Blaster (JTAG) only. A CP2102 or CH340 dongle connects to the
Arduino header:

| Dongle | DE10-Lite | Pin |
|---|---|---|
| TXD | `ARDUINO_IO[0]` → `rxd` | `PIN_AB5` |
| RXD | `ARDUINO_IO[1]` ← `txd` | `PIN_AB6` |
| GND | GND | — |

**The header is 3.3 V.** Many dongles ship jumpered for 5 V, which will damage a MAX 10 I/O
pin. Check the jumper and meter the idle TX line before connecting anything.

Writing the UART ourselves rather than using JTAG-to-Avalon is deliberate: it is real hardware
work, and it yields a clean `pyserial` driver instead of TCL scripting.

---

## 3. Architecture

### 3.1 Top level

```
   CP2102          ┌──────────────────────────────────────────────┐
   dongle          │  MAX 10                                      │
     │             │  ┌──────┐  ┌──────────┐  ┌───────────────┐   │
  TX─┼────────────►│  │ UART │─►│  Packet  │─►│  Control FSM  │   │
  RX◄┼─────────────│  │      │◄─│  Parser  │◄─│  + Config     │   │
     │             │  └──────┘  └────┬─────┘  └───┬───────┬───┘   │
                   │                 │            │       │       │
                   │          ┌──────▼──────┐ ┌───▼─────┐ │       │
                   │          │ Write Path  │ │ Act Buf │ │       │
                   │          │ quant / D2D │ │ + IR    │ │       │
                   │          │ / stuck-at  │ │ atten   │ │       │
                   │          └──────┬──────┘ └───┬─────┘ │       │
                   │                 │            │ a[r]  │       │
                   │  ┌──────────────▼────────────▼───────▼────┐  │
                   │  │        MAC LANE ARRAY (×64)            │  │
                   │  │   M9K → +noise → 9×9 → ACC32 → shift   │  │
                   │  └────────────────┬───────────────────────┘  │
                   │                   │                          │
                   │          ┌────────▼────────┐   ┌──────────┐  │
                   │          │ ADC → Result    │   │ HEX / LED│  │
                   │          │ Buffer + Argmax │   │ (active  │  │
                   │          └─────────────────┘   │  low)    │  │
                   └──────────────────────────────────────────────┘
```

### 3.2 Geometry and timing

**128×128 tile, 64 lanes, 2 passes.** Lane `l` owns columns `{l, l+64, …}`; on pass `p` it
computes column `p·64 + l`. Because rows and lanes are both powers of two, the sequence
counter *is* the weight address and every decode is a wire slice.

| Phase | Cycles per pass |
|---|---|
| Clear accumulators | 1 |
| Issue rows | 128 |
| Pipeline drain (`LANE_PIPE_DEPTH`) | 3 |
| Drain accumulators to result buffer | 64 |
| **Per pass** | **196** |

**392 cycles for two passes = 7.8 µs at 50 MHz.**

Derived, not estimated. An earlier figure of 2.7 µs counted only issue cycles and omitted
pipeline drain and accumulator readout.

### 3.3 Non-idealities, split by timescale

Three injection sites, because each effect must be recomputed at a different rate. This is the
organizing principle of the whole design.

| Timescale | Site | Effects |
|---|---|---|
| **Permanent** — fixed when the cell is written | Write path | quantization, device-to-device variation, stuck-at faults |
| **Per access** — changes on every read | Lane read path | cycle-to-cycle read noise |
| **Per operation** — depends on the data | Accumulation | IR drop, ADC quantization |

A stuck cell is a property of the die and must be identical on every access, so the write path
derives it from a hash of the cell address. Read noise must differ on every access, so each
lane carries its own LFSR. IR drop depends on simultaneous column current, so it can only be
applied during accumulation.

Every effect is individually maskable via `NOISE_EN`. That register is what makes the project
scientifically legible — accuracy loss can be attributed to one physical mechanism — and what
makes verification tractable, since the clean datapath can be proven bit-exact before any
noise is enabled.

**Scope note:** all modelled effects are *inherent* device and circuit behaviour —
finite programmable states, fabrication spread, sensing noise, defective cells, wire
resistance, and converter resolution. Radiation effects are out of scope.

### 3.4 Two optimizations worth knowing

**IR drop costs one multiplier, not sixty-four.** Since `Σ atten[r]·(w·a)` equals
`Σ w·(atten[r]·a)`, attenuation applies to the activation once before broadcast. This works
*because* the model is row-position-dependent; a rigorous treatment depends on total column
current and breaks the factorization.

**Argmax on chip, not on the host.** Result readout would otherwise dominate sweep traffic.
Comparison happens as values stream past during `DRAIN`, so it costs one comparator. Tracking
*second* place as well costs one more and yields decision margin — which degrades continuously
while accuracy is a step function. That makes the degradation plots show a mechanism rather
than just a cliff.

### 3.5 Host bandwidth

Weight load is the slow path: 16,384 bytes at 115200 baud is 1.4 s, or 178 ms at 921600.
Weights load once and many activation vectors stream against them, which is exactly the reuse
pattern crossbar accelerators exist to exploit.

| Per-image cost | Bytes | 400-point × 1000-image sweep @ 921600 |
|---|---|---|
| Full `READ_RESULT` | 676 | ~49 min |
| `READ_ARGMAX` | 166 | **~12 min** |

Keep both: raw accumulators are needed for equivalence tests and any non-classification plot.

### 3.6 Scope

**Semester 1 target:** 128×128 tile, 64 lanes, all six non-idealities, calibrated against at
least one published FeFET parameter set, digit classifier demo.

**Week 6 MVP checkpoint:** 64×64 tile, 32 lanes, quantization + D2D + stuck-at + read noise.
A presentable project halfway through.

**Semester 2, committed deliverables:**
- Multi-tile support with host-side partitioning
- Explicit differential-pair modelling
- **Retention drift** — state decay toward depolarized over time (see §3.7)
- **Cross-process comparison** — calibrate against three published FeFET datasets and report
  the accuracy spread
- A second application

**Out of scope permanently:** radiation effects, on-chip training, SDRAM, a soft processor,
analog behavioural modelling in RTL.

### 3.7 Retention drift

Retention is the natural semester-2 addition. FeFET states decay toward the depolarized
condition over time, it is heavily characterized in public literature, and it is cheap to
model: one host-supplied Q1.8 factor applied in the write path.

```
w_retained = (w_programmed × retention_factor) >> 8
```

The host computes `retention_factor` from a published decay model at a chosen elapsed time,
so the sweep axis is "time since programming" — a practical question with an obvious plot.
Cost is one 9×9 multiply in the shared write path, not per lane.

It also fills the gap left by removing radiation, and produces a comparable result: a
degradation mechanism with a measurable accuracy threshold.

### 3.8 Resource budget

| Resource | Week-6 MVP (32 lanes) | Semester 1 target (64 lanes) | Available |
|---|---|---|---|
| Logic elements | ~10,400 (21%) | ~18,300 (37%) | 49,760 |
| M9K blocks | 34 (19%) | 66 (36%) | 182 |
| Multiplier blocks | 40 (28%) | **72 (50%)** | 144 |

Multipliers are the tightest resource. This matters for the semester-2 differential-pair work,
which doubles the write path — verify the budget in week 17 before committing.

### 3.9 The modelling assumption that must be stated

Every calibration rests on one, and naming it is what makes the work credible.

Published FeFET characterization reports conductance directly, so the polarization-to-
conductance bridge that in-house capacitor data would have required **does not apply here**.
That is a meaningful rigor improvement and worth stating as such.

The assumption that replaces it is **parameter-set provenance**. Published parameters come
from different papers, processes, nodes, and measurement conditions. Taking `QUANT_LEVELS`
from one source and `D2D_SIGMA` from another silently assumes they describe the same device.

The rule: **each complete parameter set traces to a single source where possible**, mixing is
flagged explicitly in `docs/device-model.md`, and a set assembled from multiple papers is
labelled as composite rather than presented as a characterized device.

---

## 4. Team structure

Four lanes. Everyone writes RTL or model code; nobody is a manager.

### Lane A — Host interface RTL and integration
`uart_rx`, `uart_tx`, `baud_gen`, `packet_parser`, `packet_tx`, `crc8`, `control_fsm`,
`config_regs`, `readout_ser`, `display_driver`, top level, pin assignments, Quartus project
and bitstream releases.

Owns the **week-3 hardware gate**, the project's highest-risk milestone.

### Lane B — Array RTL
`write_path`, `cell_hash`, `act_buffer`, `mac_lane`, `mac_array`, `read_noise`, `adc_quant`,
`result_buffer`, `argmax_unit`, array timing closure, lane scaling.

### Lane C — Model and verification
`model_ideal`, `model_exact`, Verilator co-simulation harness, virtual UART, CI, coverage
matrix, verification report. Co-owns calibration with D.

### Lane D — Host stack and application
Python driver, packet encode/decode, public API, dual backends, `pip` packaging and
`cibuildwheel` releases, weight mapping and quantization tooling, demo networks, sweep
harness, plotting. Co-owns calibration with C.

### 4.1 The A/B seam

The RTL splits at an interface the component specs already define precisely:

| Direction | Signals |
|---|---|
| A → B | `seq_cnt`, `act_addr`, `acc_clear`, `acc_en`, `shift_en`, `noise_en`, all config values, `bulk_*` |
| B → A | result buffer read port, argmax outputs, `compute_busy` |

A memory plus control strobes — the cleanest kind of boundary, testable from both sides in
isolation.

Two rules: the signal list freezes in week 1 with the register map, and **A and B jointly write
the integration testbench**. A seam nobody owns is a seam nobody tests, and that is the classic
four-person failure mode.

### 4.2 Calibration is co-owned

C implements the parameter mathematics, D runs the sweeps that consume it, **both sign the
write-up**.

With public data this is more naturally shared than it would have been otherwise — no single
member holds privileged access, so literature review and parameter extraction split cleanly.
Assign specific papers to specific people and record it in the issue tracker.

### 4.3 The dual-backend requirement

M0 requires Infrastructure build specifications to be **universal regardless of target
platform** — awkward for anything FPGA-bound. One API, two backends:

```python
xbar = Crossbar(backend="fpga", port="/dev/ttyUSB0")   # real board
xbar = Crossbar(backend="sim")                          # C++ reference model
# identical API, bit-identical results at matched seed
```

The `sim` backend must be the same C++ that verified the RTL, exposed through pybind11 — not
an independent Python reimplementation, which could drift from what was validated.

This maps onto M0's build requirement exactly: `pip install .` is the source distribution;
`cibuildwheel` in GitHub Actions produces binary wheels for Windows, Ubuntu, and macOS. Say so
explicitly in the submission.

The reference model does four jobs: RTL oracle, universal build target, fault-tolerant demo
backup, and calibration tooling.

---

## 5. Semester 1 schedule

| Wk | A — interface RTL | B — array RTL | C — model / verif | D — host / app |
|---|---|---|---|---|
| 1 | Quartus project, pins, blink | `fecim_pkg.sv`, M9K study | C++ skeleton, CI | Python package skeleton |
| | **All: freeze protocol, register map, A/B seam. Order dongle. Assign literature survey.** | | | |
| 2 | UART RX/TX in simulation | Single `mac_lane`, clean | `model_ideal` clean MVM | Encoder vs loopback fake |
| 3 | **Hardware gate: loopback, `IDENTIFY`, 921600** | Weight M9K + write decode | `model_exact` clean path | Driver talks to board |
| 4 | Parser, control FSM, full command set | 32-lane array, drain chain | **Verilator co-sim, L0 gate** | Backend abstraction, dataset |
| 5 | Config regs, readout, display | Write path, read noise, ADC | L1 / L2 matrix | Sweep harness |
| 6 | **All: 64×64 MVP demo — classifier on hardware** | | | |
| 7 | Timing closure | Scale toward 64 lanes | **Parameter extraction + `device-model.md`** | Calibrated baseline sweeps |
| 8 | Error handling, recovery | 128×128 tile, memory map | Matrix rerun at new geometry | Weight tiling |
| 9 | Robustness, soak testing | IR drop, argmax + second place | L3 statistical validation | **Degradation sweeps — headline plots** |
| 10 | Bitstream release process | Timing closure at 64 lanes | Coverage matrix closed | Packaging, wheels, clean-machine test |
| 11 | Schematics, replication guide | Utilization report | Verification report | User guide, API docs |
| 12 | **All: deck, demo video, first rehearsal** | | | |
| 13 | **All: two timed rehearsals, code freeze, merge to main** | | | |
| 14 | **All: present, peer review, final repo state** | | | |

**Gates:** week 3 (hardware link), week 6 (MVP demo), week 7 (calibration documented).

Week 3 is the one to protect. If Python cannot get a response from the board by end of week 4,
escalate to the whole team. The fallback is JTAG-to-Avalon via System Console, which costs the
clean driver and about a week.

The literature survey starts in week 1 rather than week 7. Finding usable parameter sets is a
search problem with an uncertain completion time, and it is the one task with no hardware
dependency — so it should run in the background from the start rather than becoming a week-7
scramble.

---

## 6. Semester 2 schedule

| Wks | Focus |
|---|---|
| 15–17 | Multi-tile architecture: tile addressing, host partitioning, sequencer extension |
| 18–20 | Differential-pair modelling — doubles the write path; **verify multiplier budget in week 17 first** |
| 21–23 | Retention drift; cross-process comparison across three published parameter sets, with uncertainty bands |
| 24–25 | Second application; level-dependent variation if the literature supports it |
| 26–28 | Final documentation, rehearsal, presentation |

Week 18 is the semester-2 gate. If the multiplier budget will not take differential pairs, that
needs to be known before three weeks go into it.

---

## 7. Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| UART bring-up slips | Medium | High | Week-3 gate, dedicated owner, JTAG fallback |
| Dongle shipping delay | Medium | High | Order week 1; hardware loopback test needs no dongle |
| A/B seam untested by either side | Medium | High | Joint integration testbench; interface frozen week 1 |
| **Published data insufficient for a full parameter set** | **Medium** | **Medium** | Start literature survey week 1; sweep uncertain parameters as a band rather than guessing a point |
| **Parameters mixed across incompatible sources** | **Medium** | **Medium** | Single-source rule; composite sets labelled as such in `device-model.md` |
| Multiplier budget blocks differential pairs | Medium | Medium | Week-17 check; shared-multiplier factorization available |
| Second-semester momentum loss | Medium | High | Semester-2 items are committed deliverables with a week-18 gate |
| Live demo fails on stage | Medium | High | `sim` backend + recorded video, both rehearsed; `KEY1` triggers compute without a host |

The lab-data clearance risk is removed. Two literature-sourcing risks replace it — lower
impact, but they need the early start noted in §5.

---

## 8. Repository layout

```
fecim/
├── docs/
│   ├── protocol.md              # frozen week 1
│   ├── device-model.md          # parameter sets, sources, provenance
│   ├── hardware-verification.md # errata, manual cross-check
│   ├── hardware.md              # schematics, pinout, replication guide
│   └── meetings/                # weekly logs — M0 requirement
├── rtl/
│   ├── fecim_pkg.sv             # single source of truth
│   ├── uart/ packet/ control/   # Lane A
│   └── array/ write/ result/    # Lane B
├── model/
│   ├── src/ tests/              # C++ reference, Lane C
│   └── cosim/                   # Verilator harness
├── sw/
│   ├── fecim/                   # Python package, dual backend, Lane D
│   ├── examples/ tests/
├── tb/golden/                   # shared fixtures, hand-computed CRCs
├── quartus/                     # project, constraints, .sof releases
└── results/                     # sweep data, plots, proofs of work
```

M0 requires that *any* contribution be committed, including planning documents, and that each
contributor commits their own work. Do not let one person push on behalf of the team — the
commit trail is the evidence of equitable contribution.

---

## Appendix A — Pre-production pitch, 60 seconds

Four speakers, ~15 s and ~38 words each. Each speaks to their own lane.

**A (interface):** FeCIM is a ferroelectric compute-in-memory accelerator emulated in RTL on a
DE10-Lite. We write the UART, packet protocol, and control path ourselves — the board has no
serial bridge, so that's real hardware work, not library assembly.
*Visual:* block diagram color-coded by lane owner.

**B (array):** The array is sixty-four parallel MAC lanes. We've specified it to the register
level: thirty-seven percent of logic, fifty percent of DSP blocks. A matrix-vector multiply
takes 7.8 microseconds — derived, after our first estimate turned out wrong.
*Visual:* resource table against verified device capacity.

**C (verification):** Every device imperfection is independently switchable, so accuracy loss
can be attributed to one physical mechanism. The RTL is proven bit-exact against a C++ model
running inside the same test binary.
*Visual:* `NOISE_EN` with six labelled bits, beside the two-level equivalence chain.

**D (application):** Device parameters come from published FeFET measurements across three
fabrication processes — so we can show how much device variability alone moves network
accuracy. Every number traces to a citation, and every result reruns.
*Visual:* three-process parameter table, then the accuracy-spread plot.

**Delivery notes.** B's correction is the most important fifteen words in the pitch —
instructors have heard many confident scope estimates, and a team that revises its own numbers
is one that will notice problems in week 9 rather than week 13. D's close now rests on
reproducibility and cross-process comparison rather than data exclusivity; the claim is weaker
as a differentiator but stronger as science, and it should be delivered as a scientific choice
rather than a fallback. If given more than 60 seconds, expand C. Never say "simulator."
