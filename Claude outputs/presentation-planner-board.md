# FeCIM — Presentation Planner Board

**Plan name:** `FeCIM — Capstone` (add these buckets to the existing plan)
**Window:** Mon 16 Nov → **Tue 1 Dec 2026 (presentation)**, with final repo state on Wed 2 Dec (last day of classes)
**Built from:** `main` @ `95a7e97` (PR #14), plus unmerged `origin/Zhiting` @ `1e6abd5` and `origin/Natalie` @ `e7d03cb` (which already contains Zhiting's commits)

> **Date assumption:** the presentation is in class on **Tue 1 Dec**, the same slot as the pre-alpha. If it's actually Mon 30 Nov, pull every date in Buckets 7–8 back by one day; nothing earlier changes.

## Calendar at a glance

| Date | What happens |
|---|---|
| Mon 16 – Wed 18 | Merge outstanding branches, implement the remaining leaf RTL, model stages 2–3 |
| **Thu 19 Nov** | **Last in-person meeting with both boards before the dry run.** Hardware gate (`IDENTIFY`) demo, integration go/no-go |
| Fri 20 – Sun 22 | Full-design integration in simulation; first Quartus fit of `fecim_top` |
| Mon 23 – Sat 28 | **Thanksgiving break.** Only light async work (slides, recordings, sweeps on the sim backend) is due, by Wed 25. Nothing is due Thu 26 – Sat 28 |
| **Sun 29 Nov, 18:00** | **Merge cutoff.** `main` frozen; presentation tier decided |
| **Mon 30 Nov** | In-person dry run from a clean clone, both boards |
| **Tue 1 Dec** | **Presentation** |
| Wed 2 Dec | Final repo state, tag, meeting logs |

## Where we actually are (16 Nov)

What's done and on `main`: UART, packet parser and transmitter, `mac_lane`, `mac_array` wiring, 9×9 multiplier packing evidence, the full `fecim_top` hierarchy, the A/B seam testbench, `model_ideal`, `model_exact` clean path plus quantization (58/58 tests), single-lane L0 co-sim, golden vectors and the host codec and loopback transport.

On a branch, not yet merged: Zhiting's `config_regs` implementation, the runtime `baud_inc` fix and the reworked `tb_uart`.

Still skeletons, with outputs tied to zero: `control_fsm`, `write_path`, `act_buffer`, `result_buffer`, `argmax_unit`, `readout_ser`, `read_noise`, `adc_quant` and `display_driver`. In the model, D2D, stuck-at, read noise, IR drop and ADC still throw.

**The full design has never computed an MVM, in simulation or on hardware, and has never been through Quartus.** The board has only ever run the UART echo. This board is built around closing that gap first, then adding non-idealities only if time remains.

## What "presentation-ready" means

We present the **highest tier that works from a clean clone at the Sun 29 cutoff.** Each tier contains everything in the tiers below it, so slipping one tier doesn't wreck the talk.

| Tier | What we show | Must-have pieces |
|---|---|---|
| **1 (goal)** | `load_digits` classifier (64→10) running **on the board**, accuracy vs conductance levels (and vs D2D and stuck-at if they land), board results bit-exact to `model_exact` | Everything below, plus hardware bring-up and the app |
| **2** | A clean MVM computed **on the board** and matched bit-for-bit against `model_exact`; the digits classifier on the sim backend | Full-design fit, `COMPUTE` / `READ_RESULT` on hardware |
| **3 (floor)** | `IDENTIFY` and config round-trip on hardware; a full-design MVM in **Verilator** matching `model_exact`; digits on the sim backend | Leaf RTL, full-top co-sim, driver |

Tier 3 is the **floor**: every task in Buckets 1–6 marked **Must** exists to guarantee it. **Should** tasks take us to Tier 2, and **Stretch** tasks to Tier 1 and extra non-idealities. As before, a Stretch task only starts once every Must in your bucket is Completed.

| Lane | Owner | Must | Should | Stretch |
|---|---|---|---|---|
| A | Zhiting Li | Branch merged · `IDENTIFY` on board · `control_fsm` · readout path · `fecim_top` Quartus build | Fit + timing at 50 MHz · `COMPUTE` on hardware | HEX display driver |
| B | Megan Mendez | `act_buffer` · `write_path` stage 1 (quant) · `result_buffer` + `argmax_unit` · A/B testbench on real modules | `write_path` stages 2–3 (D2D, stuck-at) | `read_noise` RTL |
| C | Ethan Ruddell | `model_exact` stages 2–3 + golden vectors · full-array model · full-top co-sim (L1) · CI | Hardware-vs-model check (L2) · calibration documented | Read-noise model |
| D | Natalie Poche | Packaging · full command set in the driver · digits app on the sim backend | Digits on hardware · accuracy sweeps | Parameter set YAML → registers in one call |

---

## Labels

Same seven as the pre-alpha.

| Color | Label | Meaning |
|---|---|---|
| Red | **Gate** | Blocks other people's work — do first |
| Orange | **All-hands** | Needs sign-off from all four |
| Yellow | **External** | Waiting on someone outside the team |
| Green | **M0 evidence** | Required coursework artifact, must be committed |
| Blue | **Verify** | Validates a design assumption |
| Purple | **Setup** | One-time environment work |
| Pink | **Demo** | Shown live on Tue 1 Dec |

---

## Bucket 1 — Kickoff and Merge (Gate)

Everything here blocks integration. Target **Tue 17 Nov**. Merge order matters: Zhiting's PR goes first, then Natalie's. Natalie's branch already contains Zhiting's commits, so it shrinks to just the README change once Zhiting's is merged.

| Task | Owner | Due | Priority | Labels |
|---|---|---|---|---|
| Agree presentation scope, tiers and demo list | Ethan | Tue 17 Nov | Urgent | All-hands, M0 evidence |
| PR: clean up and merge `Zhiting` branch (`config_regs`, `baud_inc`, `tb_uart`) | Zhiting | Tue 17 Nov | Urgent | Gate |
| PR: merge `Natalie` branch (README bug log) | Natalie | Tue 17 Nov | Important | Gate |
| Confirm skeleton port lists against specs before implementing | Zhiting | Tue 17 Nov | Urgent | Gate, All-hands |
| Board and dongle rota through Tue 1 Dec (incl. Thanksgiving) | Zhiting | Tue 17 Nov | Urgent | Gate |

**"Agree presentation scope, tiers and demo list"**
> Post the tier table above in the Teams channel on Monday and get a thumbs-up from all four by Tuesday night. Re-confirm it at the Thursday meeting and mark it Completed there. This puts the scope cut on the record before anyone starts building: the semester-1 target (128×128, all six non-idealities) is out of reach this term, and IR drop, ADC quantization and read noise are deferred unless they land early. The report and slides need to say this honestly, the same way we disclosed the multiplier-budget change.

Checklist:
- [ ] Tier table posted Mon 16
- [ ] All four agree by Tue 17
- [ ] Each owner names their Must demo, its exact command and what "pass" looks like
- [ ] Re-confirmed Thu 19, decision recorded in the meeting log

**"PR: clean up and merge `Zhiting` branch"**
> `origin/Zhiting` holds the real `config_regs` (211 lines), the runtime `baud_inc` wiring in `uart_board_logic` (the pre-alpha blocker) and the reworked `tb_uart`. It also adds **four zero-byte junk files at the repo root: `ACC`, `CLEAR`, `DONE,` and `DRAIN`**. They look like a shell line where a `>` was read as a redirect. Delete them before the PR. It also touches five Lane B files (`act_buffer`, `adc_quant`, `mac_array`, `mac_lane`, `read_noise`) and `fecim_pkg.sv`; Megan reviews those hunks before the merge.

Checklist:
- [ ] `git rm ACC CLEAR 'DONE,' DRAIN`
- [ ] Rebase on `main` (`95a7e97`); resolve against PR #14's `mac_array` fix
- [ ] Megan approves the Lane B hunks; Ethan approves the `fecim_pkg.sv` hunk
- [ ] `tb/run_sims.sh` passes, including `tb_uart`, `tb_uart_edge` and `tb_ab_integration`
- [ ] CI green; delete the `continue-on-error` lines for `packet_parser` / `packet_tx` lint if `-Wall` is now clean
- [ ] PR URL in the task comments

**"PR: merge `Natalie` branch"**
> After Zhiting's merge, rebase so the diff is only the README "Troubleshooting & Resolved Bugs" section. The first item is marked `[]` but describes a resolved bug: tick it `[x]` or reword it.

**"Confirm skeleton port lists against specs before implementing"**
> Every skeleton header says "port list is provisional — confirm against the spec and the frozen A/B seam (top-level-spec §7.1)". Do that **once, together**, before anyone writes logic, so nobody has to rewire `fecim_top` mid-week. Zhiting owns the check because `fecim_top` is Lane A; each leaf owner confirms their own module.

Checklist:
- [ ] `control_fsm`, `readout_ser`, `display_driver` (Zhiting)
- [ ] `write_path`, `act_buffer`, `result_buffer`, `argmax_unit`, `read_noise` (Megan)
- [ ] Any port changes go into `fecim_top.sv` in one commit, and full-design lint is still clean
- [ ] Confirm the two `write-path-spec` errata flagged in October (zero-extension, 9-bit product truncation) are fixed in the spec

**"Board and dongle rota"**
> The integration owner needs a board all week and over the break, because hardware time is the scarcest resource we have. Proposal: **Zhiting keeps board 1 + dongle 1** for `IDENTIFY`, `fecim_top` bring-up and the first `COMPUTE`. **Natalie gets board 2 + dongle 2 from Megan at the Thu 19 meeting** for the driver and the digits app; Megan's remaining work is simulation-only. Both boards come to the Mon 30 dry run and the Tue 1 presentation.

Checklist:
- [ ] Board 2 + dongle 2 handed to Natalie Thu 19
- [ ] Natalie's dongle jumper checked at 3.3 V before first connection
- [ ] Who has which board over Thanksgiving, written in the task
- [ ] Both boards confirmed for Mon 30 and Tue 1

---

## Bucket 2 — Lane A · Zhiting Li

| Task | Due | Priority | Labels |
|---|---|---|---|
| A1: `IDENTIFY` and `GET_CONFIG` on hardware at 921600 (hardware gate) | Thu 19 Nov | Urgent | Gate, Demo, Verify |
| A2: Implement `control_fsm` + `tb_control_fsm` | Fri 20 Nov | Urgent | Gate |
| A3: Readout path — `readout_ser` → `packet_tx` for `READ_RESULT` / `READ_ARGMAX` | Sat 21 Nov | Urgent | Gate |
| A4: Quartus project for `fecim_top` + fit report | Sun 22 Nov | Important | Verify, M0 evidence |
| A5: Timing closure at 50 MHz | Wed 25 Nov | Important | Verify, M0 evidence |
| A6: First `COMPUTE` on hardware | Sun 29 Nov | Important | Demo, Verify |
| Stretch A7: `display_driver` on HEX | Sun 29 Nov | Medium | Demo |

**"A1: `IDENTIFY` and `GET_CONFIG` on hardware"**
> This is the original **week-3 hardware gate**, and it's still open. A board-level top (a new `quartus/fecim_board_top.sv` or similar) wraps `fecim_top` with the DE10-Lite pins from `pins.tcl`; the parser, `config_regs` and `packet_tx` are real, and the array side can still be skeletons. Natalie's driver sends `IDENTIFY` and gets back 16 bytes: magic `0xFEC1`, `PROTO_VER = 0x02`, `BUILD_ID`. **Show it at the Thu 19 meeting with Natalie (D2).** If it doesn't work by Thursday, that's the signal to fall back to Tier 3 early, not on Sun 29.

Checklist:
- [ ] Board top maps `MAX10_CLK1_50`, `KEY[0]` reset, `KEY[1]` manual compute, `ARDUINO_IO[0/1]` UART, `LEDR`, `HEX`
- [ ] Reset via `reset_sync`; KEYs are hardware-debounced, so no debounce RTL
- [ ] `BAUD_INC` resets to 2416 (115200) per the register map; driver bumps it to 19327 (921600) with `SET_CONFIG`, then reconnects
- [ ] `IDENTIFY` decodes on Natalie's driver; magic and version printed
- [ ] `SET_CONFIG` → `GET_CONFIG` round-trip on every RW register
- [ ] Bad-CRC packet returns `ST_CRC_ERR` from the board
- [ ] 30-s backup recording

**"A2: Implement `control_fsm`"**
> `IDLE → CLEAR → ACC → DRAIN → DONE` per `control-fsm-spec.md` §3–4. One counter drives `seq_cnt` / `act_addr` across `PASSES` (2 at 64 lanes). Hold in ACC for `LANE_PIPE_DEPTH` (3) cycles after the last row, so the final product lands before the drain. Honor `active_rows` / `active_cols` from `TILE_CFG`, which is how we run a 64×64 problem on the full tile without changing the package. `cycle_cnt` feeds the `CYCLE_CNT` register, which gives us a measured latency for the slides: the corrected prediction for a full 128×128 tile is 7.8 µs, about 390 cycles at 50 MHz. Delete the lint waivers when done.

Checklist:
- [ ] `start_compute` (packet) and `manual_compute` (KEY1) both start a run; a start during busy is ignored and the parser answers `ST_BUSY`
- [ ] `acc_clear` once per pass, `acc_en` for exactly `active_rows` rows plus the pipeline hold
- [ ] `shift_en` / `drain_we` / `drain_addr` cover `active_cols` across both passes
- [ ] `compute_done` and `run_start` are 1-cycle pulses
- [ ] `tb_control_fsm` checks the signal timeline for 64×64 and 128×128, plus back-to-back runs
- [ ] Added to `tb/run_sims.sh` and CI
- [ ] Lint waivers removed, `-Wall` clean

**"A3: Readout path"**
> `READ_RESULT [start:2][count:2]` returns `count × 4` bytes from `result_buffer`; `READ_ARGMAX` returns the 10-byte §4.6 record from `argmax_unit`. `readout_ser` turns 32-bit words into bytes for `packet_tx`. Until Megan's B3 lands, test it against a stub result buffer pre-filled with known words.

Checklist:
- [ ] Byte order matches `protocol.md` and Natalie's decoder
- [ ] `count` past the active columns returns `ST_ADDR_RANGE`
- [ ] Testbench: known words in, exact response frame and CRC out
- [ ] Re-run with Megan's real `result_buffer` once B3 merges

**"A4: Quartus project for `fecim_top` + fit report"**
> The first full compile only means something once A2 and B1 land, because with the skeletons tied off Quartus prunes the multipliers away. Script it (`quartus/fecim/create_project.tcl`, like `dsp_pack_test`) so anyone can rebuild it. Commit the `.fit.summary` to `quartus/reports/`. That summary gives the resource-table slide: LEs, M9K blocks, and multiplier blocks against the 144 available.

Checklist:
- [ ] Project script committed, builds from a clean clone
- [ ] Fit succeeds at 64 lanes; multiplier blocks ≤ 144, with two 9×9s packed per block as in the DSP test
- [ ] **If it doesn't fit:** rebuild with `NUM_LANES = 32` (the week-6 MVP config), tell Ethan the same day so the model and co-sim match, and record why
- [ ] `.fit.summary` committed to `quartus/reports/`

**"A5: Timing closure at 50 MHz"**
> Add an `.sdc` with a 20 ns clock on `MAX10_CLK1_50` and cut the async inputs (KEYs, UART RX goes through a synchronizer). Report worst slack. If it fails, the usual suspects are the accumulator add chain and the `cell_hash16` path in `write_path`. Pipeline those before lowering the clock.

Checklist:
- [ ] `.sdc` committed
- [ ] Setup and hold slack ≥ 0 at the slow corner, or the failing paths listed
- [ ] `.sta.summary` committed

**"A6: First `COMPUTE` on hardware"**
> With A4 programmed: write a known weight and activation pattern, `COMPUTE`, `READ_RESULT`, and compare against `model_exact`. Ethan's C5 script does the comparison. Do this the moment the bitstream exists; it doesn't have to wait for the break to end. **This is the Tier 2 line.**

Checklist:
- [ ] Identity-like weights first (easy to eyeball), then random
- [ ] 64×64 via `TILE_CFG`, then full 128×128
- [ ] `CYCLE_CNT` recorded and compared to the predicted latency
- [ ] 30-s backup recording

**"Stretch A7: `display_driver`"**
> Show the FSM state or the last argmax on HEX0–HEX5. Remember the HEX displays are **active-low** (common anode). It's a nice visual during the live demo, but it's the first thing to drop.

---

## Bucket 3 — Lane B · Megan Mendez

| Task | Due | Priority | Labels |
|---|---|---|---|
| B1: `act_buffer` (`WRITE_ACT` → M9K → broadcast) | Thu 19 Nov | Urgent | Gate |
| B2: `write_path` stage 1 — quantize + lane-select decode | Fri 20 Nov | Urgent | Gate, Verify |
| B3: `result_buffer` + `argmax_unit` | Sat 21 Nov | Urgent | Gate |
| B4: `tb_ab_integration` on real modules (replace stubs) | Sun 22 Nov | Important | Verify, M0 evidence |
| B5: `write_path` stages 2–3 — D2D + stuck-at | Sun 22 Nov | Important | Verify |
| Stretch B6: `read_noise` RTL (per-lane LFSR) | Sun 29 Nov | Medium | Verify |

**"B1: `act_buffer`"**
> `WRITE_ACT [addr:2][data:N]` fills the activation M9K; during ACC it broadcasts `act_bcast` to every lane, indexed by `act_addr` from `control_fsm`. **Match the timing the co-sim already uses: the activation arrives two cycles behind the weight address.** That's what `mac_lane` and `run_l0` assume, so a different latency breaks the bit-exact check.

Checklist:
- [ ] Activations unsigned 0..255 (`ACT_W = 8`)
- [ ] Read latency matches `mac_lane`'s expectation (check `run_l0.cpp`)
- [ ] Unit testbench, added to `run_sims.sh`
- [ ] Lint waivers removed

**"B2: `write_path` stage 1"**
> Bulk stream from the parser → `quantize_weight()` → lane select / address → lane weight memory port A. Use exactly the integer recipe `model_exact` already implements: `u = w ^ 0x80; level = (u * N) >> 8; w_q = ((level * QUANT_MULT) >> 8) - 128`. It's gated by `NOISE_EN[0]`; with quant off, weights pass straight through. **Ethan's write-path golden vectors (C1) are the pass criterion.**

Checklist:
- [ ] `QUANT_MULT` taken from the register, not recomputed in RTL
- [ ] Lane select: column → lane and pass per `write-path-spec` and `mac-array-spec`
- [ ] Passes every golden vector in `tb/golden/write_path/` (quant on, quant off)
- [ ] Multiplies stay 9-bit; no new multiplier blocks beyond the spec's count

**"B3: `result_buffer` + `argmax_unit`"**
> Drain writes from `control_fsm` (`drain_we` / `drain_addr`) land 32-bit accumulators; `READ_RESULT` reads them back through A3. `argmax_unit` tracks the max over **active columns only**, so on the digits demo it must ignore columns 10–127. Ties go to the lowest index, matching `model_ideal`.

Checklist:
- [ ] `ACC_W = 32` throughout. Don't narrow to 24: it overflows with noise on
- [ ] `CTRL[1]` clears results
- [ ] Argmax over `active_cols` only; tie → lowest index
- [ ] Unit testbenches for both, in `run_sims.sh`

**"B4: `tb_ab_integration` on real modules"**
> The A/B testbench currently drives `lane_a_stub` and `lane_b_stub`. Swap in the real `control_fsm`, `act_buffer`, `write_path`, `mac_array` and `result_buffer`, and check one full MVM end to end at the seam. It's the joint contract between A and B, so Zhiting reviews it.

Checklist:
- [ ] Stubs kept in the repo for reference, but no longer used by the default target
- [ ] One 64×64 and one 128×128 clean MVM checked against expected sums
- [ ] Zhiting approves the PR

**"B5: `write_path` stages 2–3"**
> **D2D:** a fixed per-cell offset from `cell_hash16()` on the **global** cell address, triangular distribution (n=2), scaled by `D2D_SIGMA` (reg/256, Q1.8), **saturating** at ±127 instead of wrapping. **Stuck-at:** a cell is stuck when its hash is below `STUCK_RATE[15:0]`; `[16]` picks zero vs rail (±127), and it overrides stages 1–2. Both are pass/fail against Ethan's golden vectors from C1. Triangular n=2 replacing Irwin-Hall n=4 is a documented tradeoff, so leave the comment that says so.

Checklist:
- [ ] Global address hash, not per-lane local address
- [ ] Saturation, no wrap
- [ ] Stuck-at-zero and stuck-at-rail both covered
- [ ] Passes all D2D and stuck-at golden vectors
- [ ] Re-run the fit (A4) after merging: the hash adds logic

**"Stretch B6: `read_noise`"**
> Per-lane LFSR seeded from `NOISE_SEED` and the lane index, scaled by `READ_SIGMA` (reg/512), clamped ±127, added to the weight after the memory read. Only start this after B1–B5, and only if Ethan has the matching `read_noise_q` model (C7). Otherwise it can't be verified, and we won't present it.

---

## Bucket 4 — Lane C · Ethan Ruddell

| Task | Due | Priority | Labels |
|---|---|---|---|
| C1: `model_exact` stages 2–3 (D2D, stuck-at) + write-path golden vectors | Wed 18 Nov | Urgent | Gate, Verify |
| C2: Full-array model — `ModelExact::step()` and the MVM sequencer | Fri 20 Nov | Urgent | Gate |
| C3: Full-top co-simulation (L1): Verilated `fecim_top` vs `model_exact` | Sun 22 Nov | Urgent | Verify, Demo, M0 evidence |
| C4: CI — full-top sim job, retire non-blocking lint | Sun 22 Nov | Important | Setup |
| C5: Hardware-vs-model check (L2) | Wed 25 Nov | Important | Verify, Demo |
| C6: Calibration documented — parameter set A → register values | Sun 22 Nov | Important | M0 evidence |
| Stretch C7: `read_noise_q` model + vectors | Sun 22 Nov | Medium | Verify |

**"C1: `model_exact` stages 2–3 + golden vectors"**
> Replace the two `TODO(lane C, week 3)` stubs in `model_exact.cpp`. D2D uses `cell_hash16` on the **global** cell address, a triangular (n=2) offset scaled by `D2D_SIGMA`, and saturates at ±127. Stuck-at selects cells by hash vs `STUCK_RATE`, with zero/rail mode, and overrides the earlier stages. Then emit `tb/golden/write_path/*.hex`, the per-weight in/out vectors Megan tests B2 and B5 against. **This is the Gate for Megan's B5**, so it ships Wednesday even if the unit tests are still growing.

Checklist:
- [ ] Unit tests: distribution shape (triangular, mean 0), saturation, determinism by seed, stuck-rate accuracy within tolerance, rail vs zero
- [ ] Mutation check: a wrapping add and a local-address hash both fail the tests
- [ ] Golden vectors for quant only, D2D only, stuck only, and all three combined
- [ ] Regeneration check added to CI, like the `mac_lane` vectors
- [ ] `NOISE_EN` bits outside 0/1/3 still throw. Don't silently return clean results

**"C2: Full-array model"**
> `ModelExact::step()` and an MVM call that mirrors `control_fsm` exactly: the pass loop, `active_rows` / `active_cols` and drain order, so a full-top run can be compared result for result. It also lets the model predict `CYCLE_CNT`, which turns the latency claim into a checked number.

Checklist:
- [ ] Same pass/lane/column mapping as `mac-array-spec`
- [ ] Respects `TILE_CFG` active region
- [ ] Predicted cycle count matches the plan's latency figure for 128×128
- [ ] Tests at 64×64 and 128×128; `model_exact` == `model_ideal` with noise off

**"C3: Full-top co-simulation (L1)"**
> Verilate `fecim_top` and drive it **with real protocol bytes into the parser**, bypassing the UART bit timing so it runs fast: `SET_CONFIG`, `WRITE_WEIGHTS`, `WRITE_ACT`, `COMPUTE`, `READ_RESULT`. Compare every result word, and `READ_ARGMAX`, against `model_exact`. **This is the Tier 3 floor demo, and the most important simulation in the project.** Build the harness against skeletons now, so it's ready to go green the moment the leaves land. Reuse Natalie's `protocol.py` to generate the byte streams so the host and sim share one encoder.

Checklist:
- [ ] `model/cosim/run_l1` target in the existing Makefile
- [ ] Random MVMs: 100 at 64×64, 100 at 128×128, quant on and off
- [ ] D2D and stuck-at configs once B5 lands
- [ ] On failure, prints the command, column and expected vs actual
- [ ] Re-run the live-bug demo (one-character sign-extension) against the full top
- [ ] 30-s backup recording

**"C4: CI"**
> Add the `run_l1` job. Delete each `continue-on-error: true` line as its owner's fix lands (the comment in `ci.yml` says to). `main` must be green at the Sun 29 cutoff, since the CI badge is a closing beat of the presentation.

Checklist:
- [ ] `run_l1` job added (allowed to fail until C3 is green, then blocking)
- [ ] `mac_array`, `packet_parser`, `packet_tx` lint steps blocking again
- [ ] All new unit testbenches from A and B are in the sim job
- [ ] Green on `main` at cutoff, screenshot saved for slides

**"C5: Hardware-vs-model check (L2)"**
> A script (`sw/examples/check_hw_vs_model.py` or a `fecim` CLI command) that takes one config, weight matrix and activation vector, runs it through the driver on the board and through `model_exact`, and diffs every column. Natalie and Zhiting run it on their boards; you don't need one. **It's what turns "the board computed something" into "the board is bit-exact."**

Checklist:
- [ ] Same seed and config to both sides
- [ ] Bit-exact pass/fail plus a per-column diff table
- [ ] Runs on the sim backend too, so it can be tested before the board works
- [ ] Usage in the README

**"C6: Calibration documented"**
> This is the week-7 calibration gate, co-owned with Natalie. Write down how parameter set A (`dualport_fefet_mlc`, Chatterjee et al., TED 2022) becomes register values: `QUANT_LEVELS = 8`, the D2D σ conversion to `D2D_SIGMA`, and the stuck rate if the source gives one. **State the parameter-provenance assumption explicitly**, since it replaced the polarization-to-conductance bridge. Add set B (`gf28_hkmg`, Soliman et al., *Nat. Commun.* 2023) only if its YAML exists.

Checklist:
- [ ] `device-model.md` or `literature-survey.md` §2.2 updated with the mapping and its arithmetic
- [ ] YAML values match the documented mapping
- [ ] Assumptions and missing values (no C2C σ in set A) listed

**"Stretch C7: `read_noise_q` model"**
> Only if Megan is going for B6. Implement the LFSR, scale and clamp exactly as in `read-noise` / `mac-array-spec` and emit vectors. Read noise is the one effect that has to live in RTL, so without a model it isn't presentable.

---

## Bucket 5 — Lane D · Natalie Poche

| Task | Due | Priority | Labels |
|---|---|---|---|
| D1: Packaging — `pyproject.toml`, `pip install -e ./sw` | Tue 17 Nov | Urgent | Setup, Gate |
| D2: Full command set in the driver | Thu 19 Nov | Urgent | Gate, Demo |
| D3: Sim backend — `Crossbar(backend="sim")` | Sun 22 Nov | Important | Gate |
| D4: Digits classifier app (`load_digits`, 64→10) | Sun 22 Nov | Urgent | Demo |
| D5: Accuracy sweeps + plots | Wed 25 Nov | Important | Demo, M0 evidence |
| D6: Digits on hardware | Sun 29 Nov | Important | Demo, Verify |
| Stretch D7: Parameter set YAML → registers in one call | Sun 29 Nov | Medium | |

**"D1: Packaging"**
> CI's "Check Python package exists" step and everyone's demo commands assume `pip install -e ./sw`. Also delete the two empty files `sw/fecim/code.py` and `sw/fecim/crossbar.py` (0 bytes), or give `crossbar.py` the real `Crossbar` class in D3.

Checklist:
- [ ] `pyproject.toml` with deps (`pyserial`, `numpy`, `scikit-learn`, `pyyaml`, `matplotlib`)
- [ ] Clean venv install works on Windows and in WSL
- [ ] `pytest sw/tests` green in CI

**"D2: Full command set in the driver"**
> Every opcode in `protocol.md` §4 wraps into a method, and every non-OK status maps to an exception. `WRITE_WEIGHTS` and `WRITE_ACT` chunk at `MAX_PAYLOAD = 512`. Baud switching (`SET_CONFIG BAUD_INC` → reopen the port at 921600) is a driver method, not a manual step. **Show `IDENTIFY` with Zhiting's A1 at the Thu 19 meeting.**

Checklist:
- [ ] `identify()`, `set_config()` / `get_config()`, `write_weights()`, `write_act()`, `compute()`, `read_result()`, `read_argmax()`
- [ ] `WRITE_ATTEN` present but documented as unused this semester
- [ ] Chunking tested with a full 128×128 weight load
- [ ] Each status code → its own exception; tests via the loopback fake
- [ ] Works on a COM port and on `/dev/ttyUSB*`

**"D3: Sim backend"**
> `Crossbar(backend="sim")` must run the exact same app code against `model_exact`. The plan calls for pybind11; **if that isn't working by Fri 20, ship a subprocess wrapper** around a small `fecim_cli` binary (ask Ethan, it's ~50 lines on top of the model). The digits app and the sweeps need this to exist even if the board never computes.

Checklist:
- [ ] Same `Crossbar` API for `backend="sim"` and `backend="serial"`
- [ ] Bit-exact with C5's model path for the same seed
- [ ] Fast enough for a sweep: time 1,797 inferences and record it

**"D4: Digits classifier app"**
> Per `application-spec.md` §3.1: sklearn `load_digits`, logistic regression trained off-array (~95% float baseline), weights mapped onto a 64-row × 10-column region with per-column weight scaling and activation scaling (§2.2–2.5). Run it on the sim backend first. Report float, ideal-crossbar and quantized accuracy side by side, **plus per-class accuracy** (§4.4).

Checklist:
- [ ] Fixed train/test split and seed, recorded
- [ ] Float vs `model_ideal` vs `model_exact` (quant on) accuracy table
- [ ] Uses `READ_ARGMAX`, not host-side argmax (§4.3)
- [ ] One command: `python sw/examples/digits.py --backend sim`
- [ ] 30-s backup recording

**"D5: Accuracy sweeps + plots"**
> On the **sim backend** (works over the break with no board), sweep accuracy vs `QUANT_LEVELS` (2, 3, 4, 8, 16, 255), then vs D2D σ and stuck rate once C1 lands. Mark parameter set A (N = 8, its σ) on each curve. Reseed between points, not within (§4.2), and write provenance with every result file (§4.5). **These plots are the core results slides.**

Checklist:
- [ ] Accuracy vs levels, with the set-A point marked
- [ ] Accuracy vs D2D σ and vs stuck rate (once C1 lands)
- [ ] ≥ 5 seeds per point with error bars
- [ ] CSV + provenance JSON in `results/`, PNGs exported for slides

**"D6: Digits on hardware"**
> Same command with `--backend serial` on board 2, once A6 shows a matching `COMPUTE`. Run C5 on a few inputs first so we can say "bit-exact" on stage. **This is Tier 1.**

Checklist:
- [ ] Hardware accuracy == sim-backend accuracy for the same config (it should be identical)
- [ ] Inference throughput printed (UART-bound; expect weight load to dominate)
- [ ] Repeated with set-A config if B5 has landed
- [ ] 30-s backup recording

**"Stretch D7: YAML → registers"**
> `xbar.load_parameter_set("dualport_fefet_mlc")` reads the YAML and issues the `SET_CONFIG` calls, using the C6 mapping. It's a nice "one line to switch device technologies" beat, but only if everything else is done.

---

## Bucket 6 — Integration Checkpoints (Gate)

These are the go/no-go points. Each has one owner who checks it and posts the result in Teams.

| Task | Owner | Due | Priority | Labels |
|---|---|---|---|---|
| Thu 19 meeting: hardware gate + integration go/no-go | Ethan | Thu 19 Nov | Urgent | All-hands, M0 evidence |
| Checkpoint: full-top L1 co-sim green (Tier 3 locked) | Ethan | Sun 22 Nov | Urgent | Gate, Verify |
| Checkpoint: fit + first hardware `COMPUTE` (Tier 2) | Zhiting | Sun 29 Nov | Important | Gate, Verify |

**"Thu 19 meeting"**
> The last time all four of us and both boards are in a room before the dry run, so use it for hardware.

Agenda checklist:
- [ ] A1 + D2: `IDENTIFY` live on the board (the hardware gate)
- [ ] Scope and tiers re-confirmed (Bucket 1)
- [ ] Port-list confirmation done; any `fecim_top` changes merged
- [ ] Status of A2, B1, B2, C1, C2: anything at risk gets a fallback now
- [ ] Board 2 handed to Natalie; Thanksgiving board locations agreed
- [ ] Assign slide sections (Bucket 7)

**"Checkpoint: full-top L1 co-sim green"**
> By Sunday night, C3 passes clean MVMs with the real leaves. If it isn't green, the next three days go to that alone: no stretch work and no hardware until the floor is locked.

**"Checkpoint: fit + first hardware `COMPUTE`"**
> If A6 hasn't matched the model by the cutoff, we present Tier 3 with the hardware gate, and say plainly that on-board MVM is the next milestone. That's a respectable result. A half-working live hardware demo isn't.

---

## Bucket 7 — Presentation

| Task | Owner | Due | Priority | Labels |
|---|---|---|---|---|
| Check rubric, time limit, format and peer-eval due dates | Ethan | Tue 17 Nov | Urgent | External |
| Slide deck draft | Ethan | Wed 25 Nov | Important | M0 evidence |
| Results slides: plots, resource table, latency, verification chain | Natalie | Sun 29 Nov | Important | M0 evidence |
| Honest-scope and design-corrections slide | Ethan | Wed 25 Nov | Important | M0 evidence |
| Demo run sheet + backup recordings | Natalie | Sun 29 Nov | Urgent | Demo, M0 evidence |
| Remote rehearsal (slides only, timed) | Ethan | Sun 29 Nov | Important | All-hands |

**"Check rubric, time limit, format and peer-eval due dates"**
> Before anyone writes slides: confirm on Canvas the time limit, whether a live demo is expected or allowed, required sections, and whether peer evaluations or a written deliverable are due with it. If anything here changes the dates on this board, change them Tuesday.

**"Slide deck draft"**
> Start from the Design Draft deck (its twelve slides already have speaker notes) and the pre-alpha "how it works" bullets. Suggested flow: problem → why emulate a ferroelectric crossbar → architecture → follow one packet from host to array (same data-flow order as the pre-alpha demo: D → A → B → C) → verification chain → results → honest scope → semester 2. One owner per section, assigned Thu 19.

Checklist:
- [ ] Section owners assigned Thu 19
- [ ] Draft in the shared deck by Wed 25
- [ ] Every number on a slide traces to a committed file (fit report, results CSV, CI run)

**"Results slides"**
> Four slides at most: (1) accuracy vs conductance levels with set A marked, (2) D2D and stuck-at curves if C1 and B5 landed, (3) a resource table from the `fecim_top` fit summary against 144 multiplier blocks and the M9K count, (4) measured `CYCLE_CNT` against the predicted 7.8 µs for a full tile. Use the C3 or C5 bit-exact result as the caption for whichever level we reached.

**"Honest-scope and design-corrections slide"**
> One slide, stated as evidence of rigor and not as an apology. Cover:
> - **Corrections:** MVM latency 2.7 → 4.0 / 7.8 µs; 144 multiplier *blocks*, not 288 multipliers, which forced 9-bit operands, ±127 noise clamp, Q1.8 σ, 16-bit hash and triangular n=2 D2D; `ACC_W` 24 → 32.
> - **Data and scope changes:** published FeFET data replaced in-house HZO; radiation dropped, retention drift added for semester 2.
> - **What moved to semester 2:** IR drop, ADC quantization and (unless B6/C7 land) read noise. Also the 128×128 MLP.

**"Demo run sheet + backup recordings"**
> Same format as the pre-alpha: one row per demo with owner, laptop, board (Y/N), exact command, expected output, backup recording link and time slot. Committed to `docs/coursework/`. Every live demo gets a 30-s recording **as soon as it first works**, not on Sunday night.

Checklist:
- [ ] Run sheet committed
- [ ] Recordings: A1 `IDENTIFY`, C3 L1 co-sim + live bug, D4 digits (sim), plus A6 / D6 if reached
- [ ] Recordings linked from the run sheet

**"Remote rehearsal"**
> Teams call, slides only, timed against the limit. Cut until it fits with 20% to spare for demo hiccups.

---

## Bucket 8 — Demo Day and Close-out

| Task | Owner | Due | Priority | Labels |
|---|---|---|---|---|
| Merge cutoff — `main` frozen, tier decided | Ethan | Sun 29 Nov | Urgent | Gate, All-hands |
| Dry run from clean clone, both boards | Ethan | Mon 30 Nov | Urgent | All-hands |
| Presentation | Ethan | Tue 1 Dec | Urgent | All-hands, Demo, M0 evidence |
| README current state + tag `v0.2.0-alpha` | Ethan | Wed 2 Dec | Important | M0 evidence |
| Meeting logs through Tue 1 Dec | Natalie | Wed 2 Dec | Medium | M0 evidence |

**"Merge cutoff"**
> **Sun 29 Nov, 18:00.** Anything not on `main` then isn't in the presentation. At the same time, decide the tier from what passes on `main`, not what passes on someone's branch, and post it in Teams.

**"Dry run from clean clone"**
> Mon 30 Nov, both boards. Every demo runs from a fresh `git clone` of the cutoff commit on a laptop that isn't the author's, with both bitstreams programmed from the committed Quartus project. Anything that fails falls back to its recording on Tuesday.

Checklist:
- [ ] A1 `IDENTIFY` · A6 `COMPUTE` (if Tier 2)
- [ ] C3 L1 co-sim + live bug · CI green screenshot
- [ ] D4 digits (sim) · D6 digits (hardware, if Tier 1)
- [ ] Full talk timed with demos
- [ ] Commit hash recorded for the tag

**"README current state + tag"**
> Update the status table to what's actually on `main` after the presentation, and tag `v0.2.0-alpha` on the commit the dry run used. That's what we point M0 reviewers at.

**"Meeting logs"**
> Thu 19 meeting, Sun 29 rehearsal, Mon 30 dry run and Tue 1 presentation, committed to `docs/meetings/` (the folder the plan specifies; it doesn't exist yet). **The pre-alpha run sheet and meeting logs were never committed either,** and M0 measures against the repo, so add those in the same PR.

Checklist:
- [ ] `docs/meetings/` created
- [ ] Pre-alpha logs + run sheet backfilled
- [ ] Logs for 19 Nov, 29 Nov, 30 Nov, 1 Dec

---

## Bucket 9 — After the presentation (parked)

Somewhere to put things that come up these two weeks without cluttering the active board.

| Task | Owner |
|---|---|
| IR drop: `WRITE_ATTEN` coefficient ROM + `ir_attenuate` model | Megan / Ethan |
| ADC quantization: `adc_quant` RTL + `adc_quantize` model | Megan / Ethan |
| Read noise, if B6/C7 didn't land | Megan / Ethan |
| Two-layer MLP (64→32→10) on the full tile | Natalie |
| Parameter sets B and C (`gf28_hkmg`, then a third process) | Ethan / Natalie |
| Retention drift (semester-2 replacement for radiation) | Ethan |
| Differential pairs — verify the multiplier budget first (week-17 check) | Megan |
| pybind11 sim backend, if D3 shipped the subprocess fallback | Natalie |

---

## Conventions (unchanged from week 1)

**Every task has exactly one assignee.** Shared work gets a primary owner; the others go in the checklist or notes.

**Paste the PR URL into task comments.** Planner doesn't link to GitHub, and M0 measures contribution against the commit history.

**Gate tasks come first.** If you're blocked on your own work, clear a Gate rather than starting a Stretch.

**Nothing is due Thu 26 – Sat 28 Nov.** Anything that slips into the break gets re-dated to Sun 29 with a reason in the task comments, not quietly carried.
