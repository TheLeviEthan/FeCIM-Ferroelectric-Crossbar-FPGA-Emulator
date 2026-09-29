# FeCIM: Revised Plan — Four-Person Team, Two Semesters

**Supersedes:** the team structure, scope, and timeline sections of `fecim-plan.md`
**Unchanged:** all seven component specs, except the geometry parameters in §2.3
**Status:** draft

---

## 1. What the fourth member changes

Three things, in order of importance.

**The RTL splits cleanly in two.** It was always the largest lane by a wide margin, and it
has a natural seam that the existing specs already define precisely: the config register
map and the bulk-write stream on one side, the datapath on the other. Splitting it is not
make-work.

**The critical path gets a dedicated owner.** The week-3 UART bring-up gate sits entirely
in the host-interface half. With three people, whoever owned RTL owned both the critical
path *and* the array, so array work stalled while the link was debugged. Now they proceed
in parallel.

**Scope should grow.** M0 is explicit that tools and team capacity mean projects go
further, not that they require less effort. A four-person year-long project that ships the
same 64×64 single-tile emulator a three-person semester would have is under-scoped.

---

## 2. Revised scope

### 2.1 Semester 1 target

**128×128 tile, 64 MAC lanes**, all six non-idealities, calibrated against HZO
measurements, with a working classifier demo.

The 64×64 / 32-lane configuration becomes the **week-6 MVP milestone** rather than the
final deliverable — you still get a presentable project halfway through, but it is a
checkpoint, not the destination.

### 2.2 Semester 2 targets

These become committed deliverables rather than stretch goals:

- **Multi-tile support** — weight tiling across several 128×128 tiles with host-side
  partitioning, enabling genuinely larger networks
- **Explicit differential-pair modelling** — two cells per weight with independent
  quantization and variation, differenced at read. This is write-path open question 1, and
  it lets you demonstrate that differential encoding partially cancels correlated drift.
- **Level-dependent variation** — if the HZO data supports it (write-path open question 2)
- **The full device study** — fatigue and radiation sweeps with uncertainty bands, which is
  the publishable core of the project
- **A second application** beyond digit classification

### 2.3 Resource check at the new geometry

128×128 tile, 64 lanes, 2 passes:

| Resource | Used | Available | % |
|---|---|---|---|
| Logic elements | ~18,640 | 49,760 | 37% |
| M9K blocks | 66 | 182 | 36% |
| 18×18 multipliers | 136 | 288 | 47% |

**MVM latency:** 2 passes × (1 clear + 128 rows + 3 pipeline + 64 drain) = 392 cycles =
**7.8 µs** at 50 MHz.

Still comfortably within the device, and still four orders of magnitude below the UART
load time. Multipliers become the tightest resource at 47%, which is the constraint to
watch if semester 2 adds per-lane arithmetic for differential pairs — that work may need
the shared-multiplier factorization trick from MAC array spec §7.

**Package changes:** `TILE_ROWS = 128`, `TILE_COLS = 128`, `NUM_LANES = 64`. Everything
else in `fecim_pkg.sv` is unchanged, and every component spec holds without edit — the
specs were parameterized for exactly this.

---

## 3. Four lanes

### Lane A — Host interface RTL and integration

`uart_rx`, `uart_tx`, `baud_gen`, `packet_parser`, `packet_tx`, `crc8`, `control_fsm`,
`config_regs`, `readout_ser`, `display_driver`, top level, pin assignments, Quartus project
and build releases.

Owns the **week-3 hardware gate**, which is the project's single highest-risk milestone.

### Lane B — Array RTL

`write_path`, `cell_hash`, `act_buffer`, `mac_lane`, `mac_array`, `read_noise`,
`adc_quant`, `result_buffer`, `argmax_unit`, array timing closure and lane scaling.

### Lane C — Model and verification

`model_ideal`, `model_exact`, the Verilator co-simulation harness, virtual UART, CI, the
coverage matrix, the verification report. Co-owns calibration with D.

### Lane D — Host stack and application

Python driver, packet encode/decode, public API, dual backends, `pip` packaging and
`cibuildwheel` releases, weight mapping and quantization tooling, demo networks, sweep
harness, plotting. Co-owns calibration with C.

### 3.1 The A/B seam

Both halves of the RTL meet at interfaces the component specs already define:

| Direction | Signals |
|---|---|
| A → B | `seq_cnt`, `act_addr`, `acc_clear`, `acc_en`, `shift_en`, `noise_en`, all config register values, `bulk_we`/`bulk_addr`/`bulk_data`/`bulk_target` |
| B → A | `result_buffer` read port, `argmax` outputs, `compute_busy` contribution |

The seam is a **memory and a set of control strobes**, which is the cleanest kind of
boundary — testable from both sides in isolation.

Two rules: the interface list above is frozen in week 1 alongside the register map, and A
and B **jointly write the integration testbench** rather than each testing only their own
side. A seam nobody owns is a seam nobody tests.

### 3.2 Calibration is deliberately co-owned

Earlier drafts flagged the risk that calibration becomes one person's solo lane because
only one team member has lab access. With four people there is a clean answer: **C
implements the parameter mathematics, D runs the sweeps that consume it, and both sign the
calibration write-up.**

Whoever holds the lab data supplies measurements and domain knowledge regardless of lane.
That is a genuine contribution, but it must not be the *only* contribution attached to the
calibration work, or the commit history will show one author on the project's most
distinctive artifact. M0 measures contribution against third-party logs; make sure the logs
reflect the shared work.

---

## 4. Revised timeline

### Semester 1

| Week | A (interface) | B (array) | C (model/verif) | D (host/app) |
|---|---|---|---|---|
| 1 | Quartus project, pin assignments, blink | `fecim_pkg.sv`, M9K instantiation study | C++ skeleton, CI | Python package skeleton |
| | **All: freeze protocol, register map, A/B seam. Order dongle. Clear lab data with Dr. Nino.** | | | |
| 2 | UART RX/TX in sim | Single `mac_lane`, clean | `model_ideal` clean MVM | Encoder against loopback fake |
| 3 | **Hardware gate: loopback, IDENTIFY** | Weight M9K + write decode | `model_exact` clean path | Driver talks to board |
| 4 | Parser, control FSM, full command set | 32-lane array, drain chain | **Verilator co-sim, L0 gate** | Backend abstraction, dataset |
| 5 | Config regs, readout, display | Write path, read noise, ADC | L1/L2 matrix | Sweep harness |
| 6 | **All: 64×64 MVP demo — digit classifier on hardware** | | | |
| 7 | Timing closure | Scale to 64 lanes | **Calibration + `device-model.md`** | Calibrated baseline sweeps |
| 8 | Error handling, recovery | 128×128 tile, memory map | Matrix rerun at new geometry | Weight tiling, 921600 baud |
| 9 | Robustness, soak testing | IR drop, argmax + second place | L3 statistical validation | **Fatigue sweep — headline plot** |
| 10 | Bitstream release process | Timing closure at 64 lanes | Coverage matrix closed | Packaging, wheels, clean-machine test |
| 11 | Schematics, replication guide | Resource/utilization report | Verification report | User guide, API docs |
| 12 | **All: deck, demo video, first rehearsal** | | | |
| 13 | **All: two timed rehearsals, code freeze, merge to main** | | | |
| 14 | **All: present, peer review, final repo state** | | | |

### Semester 2

| Weeks | Focus |
|---|---|
| 15–17 | Multi-tile architecture: tile addressing, host partitioning, sequencer extension |
| 18–20 | Differential-pair modelling — doubles write path and weight memory; check the multiplier budget first |
| 21–23 | Full device study: fatigue and radiation sweeps with uncertainty bands |
| 24–25 | Second application; level-dependent variation if data supports it |
| 26–28 | Final documentation, rehearsal, presentation |

Weeks 3, 6, and 7 remain the semester-1 gates. Week 18 is the semester-2 gate — if the
multiplier budget will not accommodate differential pairs, you need to know before three
weeks go into it.

---

## 5. Revised risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| UART bring-up slips | Medium | High | Week 3 gate, dedicated owner (A), JTAG fallback |
| A/B seam untested by either side | **Medium** | **High** | Joint integration testbench, frozen interface list |
| Multiplier budget blocks differential pairs | Medium | Medium | Check in week 17; shared-multiplier factorization available |
| Calibration concentrates on one member | Medium | High | C+D co-own, both sign the write-up |
| Four-way merge conflicts on shared files | **Medium** | Low | `fecim_pkg.sv` changes require a PR review from all four |
| Second semester loses momentum | **Medium** | **High** | Semester-2 items are committed deliverables on the timeline, not stretch goals |
| Lab data blocked for public repo | Low | Medium | Clear week 1; synthetic fallback parameters |

The last one is new and specific to a year-long project. Teams that ship a working demo in
December frequently coast in January. Putting multi-tile and differential pairs on the
timeline as committed work, with a week-18 gate, is the structural defense.

---

## 6. Pre-production pitch — 60 seconds, four speakers

Roughly 15 seconds and ~38 words each. Each person speaks to their own lane, which is what
M0 wants and what makes the claims credible.

### Speaker A — Lane A (host interface)

> FeCIM is a ferroelectric compute-in-memory accelerator emulated in RTL on a DE10-Lite.
> We write the UART, packet protocol, and control path ourselves — the board has no serial
> bridge, so that's real hardware work, not library assembly.

*Visual:* Block diagram color-coded by lane owner. The four-way split should be visible
before anyone mentions it.

### Speaker B — Lane B (array)

> The array is sixty-four parallel MAC lanes. We've specified it to the register level:
> thirty-seven percent of logic, thirty-six percent of block RAM. A matrix-vector multiply
> takes 7.8 microseconds — derived, after our first estimate turned out wrong.

*Visual:* Resource table against the 10M50's actual capacity. Keep the correction — it
says the team computes rather than asserts, which is the single most credible thing you can
demonstrate in a pre-production pitch.

### Speaker C — Lane C (model and verification)

> Every device imperfection is independently switchable, so accuracy loss can be attributed
> to one physical mechanism. The RTL is proven bit-exact against a C++ model running inside
> the same test binary.

*Visual:* The `NOISE_EN` register with six labelled bits, beside the two-level equivalence
chain diagram.

### Speaker D — Lane D (host and application)

> First semester ends with a calibrated emulator and a working classifier. The year is for
> multi-tile scaling and the full device study — using hafnium-zirconium-oxide capacitors
> measured in our own lab. No other team can run it.

*Visual:* Two-semester timeline with the week-3, week-6, and week-18 gates marked, then cut
to your own P–E hysteresis loop.

### 6.1 Delivery notes

**Speaker B's correction is the most important fifteen words in the pitch.** Instructors
have heard many confident scope estimates. A team that revises its own numbers is a team
that will notice problems in week 9 rather than week 13.

**Close with the lab data, do not open with it.** Opening sounds like a claim. Closing,
after three speakers have shown the engineering is real, sounds like a conclusion.

**If you get more than 60 seconds**, expand Speaker C. Verification is the least
intuitive part to an audience and the part that most distinguishes a real engineering
project from a demo — and it is the hardest thing to compress into 38 words.

**Do not say "simulator."** You built hardware; keep the language on that side throughout.
