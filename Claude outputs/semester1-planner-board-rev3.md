# FeCIM — Semester 1 Completion Planner Board (rev 3)

**Plan name:** `FeCIM — Capstone` (add these buckets to the existing plan; replaces rev 1 and rev 2; rev 3 schedules work through Thanksgiving break)
**Window:** Mon 16 Nov → **Tue 1 Dec 2026 (presentation)**, with the final repo state on Wed 2 Dec (last day of classes)
**Built from:** `main` @ `95a7e97` (PR #14), plus unmerged `origin/Zhiting` @ `1e6abd5` and `origin/Natalie` @ `e7d03cb` (which already contains Zhiting's commits)

> **Date assumption:** the presentation is in class on **Tue 1 Dec**, the same slot as the pre-alpha. If it's actually Mon 30 Nov, pull Buckets 7–8 back by one day.

## The deliverable

This is the semester-1 target from `fecim-plan.md` §3.6, all of it, by presentation day:

- **128×128 tile, 64 MAC lanes**, running on the DE10-Lite at 50 MHz
- **All six non-idealities** in RTL and the model, each independently switchable through `NOISE_EN`:
  1. Quantization
  2. D2D variation
  3. Stuck-at faults
  4. Read noise
  5. IR drop
  6. ADC quantization
- **Calibrated against at least one published FeFET parameter set** (set A, `dualport_fefet_mlc`), with provenance documented
- **Digit classifier demo**: the 64→10 linear classifier first, then the 64→32→10 MLP that `application-spec.md` §3.2 recommends for the semester-1 target
- **Verified at all five levels:** L0, L1, L2 and L4 are bit-exact, and L3 is statistical (`model-verification-design.md` §3). The verification matrix (§6.1) is closed
- The plan's weeks 10–11 documentation:
  - Utilization report
  - Verification report
  - User guide and API docs
  - Replication guide
  - Bitstream release

No tier system this time: **every task on Buckets 1–8 is Must.** The schedule is compressed because AI tools write the first draft of most modules and tests. That only works if the **golden vectors exist before the RTL** and every module is held to them, so Lane C's model work is front-loaded as a Gate for everyone else.

## Calendar at a glance

| Date | What happens |
|---|---|
| Mon 16 – Tue 17 | Merge outstanding branches; confirm port lists; model for all six non-idealities + golden vectors |
| Wed 18 – Fri 20 | Leaf RTL drafted against golden vectors. **Thu 19: last in-person meeting with both boards before the dry run.** Hardware gate (`IDENTIFY`) live |
| Sat 21 – Mon 23 | Full-array L0 (Sat), first full Quartus fit (Sun), **L1 + L2 green in full-top co-sim** and **calibration gate** (Mon) |
| Tue 24 – Wed 25 | Timing closure, **L4 on hardware** (Wed), linear digits on the board, sim sweeps, slide draft |
| Thu 26 | **Thanksgiving Day — nothing due.** Work if you like |
| Fri 27 – Sat 28 | **Break work days.** Soak test, L3, MLP on the board, hardware sweeps, docs. **Fri 27: virtual team sync.** **Sat 28: every Must task Completed** |
| **Sun 29, 18:00** | Buffer morning, remote rehearsal, then **merge cutoff** |
| **Mon 30** | In-person dry run from a clean clone, both boards |
| **Tue 1 Dec** | **Presentation** |
| Wed 2 Dec | Final repo state, `v1.0.0` tag, meeting logs, peer review |

## Where we actually are (16 Nov)

**Done and on `main`:**
- RTL: UART, packet parser/transmitter (including `WRITE_ATTEN` routing to `TGT_ATTEN`), `mac_lane` (clean datapath), `mac_array` wiring, the full `fecim_top` hierarchy
- 9×9 multiplier packing evidence
- Verification: the A/B seam testbench, single-lane L0 co-sim, `mac_lane` golden vectors
- Model: `model_ideal`, and `model_exact` clean path + quantization (58/58 tests)
- Host: codec, loopback transport, scaffolding
- Set A and set B YAML extracted

**On branches, not merged:** `config_regs` (Zhiting), the runtime `baud_inc` fix, and the reworked `tb_uart`.

**Still skeletons:**
- RTL: `control_fsm`, `write_path`, `act_buffer`, `result_buffer`, `argmax_unit`, `readout_ser`, `read_noise`, `adc_quant`, `display_driver`
- Model: D2D, stuck-at, read noise, IR drop and ADC all still throw
- `ModelExact::step()` doesn't exist yet
- No `fecim_top` Quartus project
- No host packaging or sim backend

**The full design has never computed an MVM, in simulation or on hardware.** Most of the semester-1 work still lies ahead. The timeline below fits it in by running the four lanes fully in parallel against frozen golden vectors.

## Lane summary

| Lane | Owner | Delivers |
|---|---|---|
| A | Zhiting Li | Branch merge · `IDENTIFY` on board · `control_fsm` · readout path · `argmax_unit` (moved from B) · `display_driver` · board top + Quartus project · timing closure · error handling + soak · bitstream release + replication guide |
| B | Megan Mendez | `act_buffer` + IR attenuation · `write_path` stages 1–3 · `read_noise` in `mac_lane` · `adc_quant` · `result_buffer` · A/B testbench on real modules · mac-array-spec §12 tests · utilization report |
| C | Ethan Ruddell | Model: all six non-idealities + golden vectors · `ModelExact::step()` · full-array L0 · full-top L1/L2 with virtual UART · L4 script · L3 statistics · calibration (primary) · coverage matrix · verification report · CI |
| D | Natalie Poche | Packaging · full driver · sim backend · parameter set → registers (calibration co-owner) · linear and MLP digits apps · sweep harness · headline plots · wheels + clean-machine test · user guide + API docs |

**Rebalance note:** `argmax_unit` moves from Lane B to Lane A, with Megan reviewing. Lane B already has six modules plus the IR and read-noise integration, while Lane A's remaining RTL is lighter. Confirm the move in the Bucket 1 scope task.

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

Everything here blocks the parallel push. Target **Tue 17 Nov**. Merge order: Zhiting's PR first, then Natalie's (hers already contains Zhiting's commits).

| Task | Owner | Due | Priority | Labels |
|---|---|---|---|---|
| Agree semester-1 completion plan, owners, rebalance and async check-ins | Ethan | Tue 17 Nov | Urgent | All-hands, M0 evidence |
| PR: clean up and merge `Zhiting` branch (`config_regs`, `baud_inc`, `tb_uart`) | Zhiting | Tue 17 Nov | Urgent | Gate |
| PR: merge `Natalie` branch (README bug log) | Natalie | Tue 17 Nov | Important | Gate |
| Confirm every skeleton port list against its spec, in one `fecim_top` commit | Zhiting | Tue 17 Nov | Urgent | Gate, All-hands |
| Board and dongle rota through Tue 1 Dec (incl. Thanksgiving) | Zhiting | Tue 17 Nov | Urgent | Gate |
| Check rubric, time limit, format, peer-eval and written-report due dates | Ethan | Tue 17 Nov | Urgent | External |

**"Agree semester-1 completion plan"**
> Post this board in the Teams channel on Monday and get a thumbs-up from all four by Tuesday night. We only meet in person once more before the dry run (Thu 19), so agree a **daily async check-in** too: one Teams message per person per day with yesterday's PRs, today's target and blockers. Re-confirm at the Thursday meeting and record the decision.

Checklist:
- [ ] Board posted Mon 16; all four agree by Tue 17
- [ ] `argmax_unit` moved to Lane A (Megan reviews) — agreed or rejected
- [ ] Daily check-in thread created in Teams
- [ ] AI-workflow rule agreed (see Conventions): no module PR without its golden-vector or spec-test evidence
- [ ] Re-confirmed Thu 19, recorded in the meeting log

**"PR: clean up and merge `Zhiting` branch"**
> `origin/Zhiting` has the real `config_regs` (211 lines), runtime `baud_inc` wiring in `uart_board_logic` (the pre-alpha blocker) and the reworked `tb_uart`. It also adds **four zero-byte junk files at the repo root: `ACC`, `CLEAR`, `DONE,` and `DRAIN`**, which look like a shell line where a `>` was read as a redirect. Delete them before the PR. It also touches five Lane B files and `fecim_pkg.sv`; Megan reviews the Lane B hunks and Ethan reviews the package hunk.

Checklist:
- [ ] `git rm ACC CLEAR 'DONE,' DRAIN`
- [ ] Rebased on `main` (`95a7e97`), resolved against PR #14's `mac_array` fix
- [ ] Megan approves the Lane B hunks; Ethan approves the `fecim_pkg.sv` hunk
- [ ] `tb/run_sims.sh` passes, including `tb_uart`, `tb_uart_edge` and `tb_ab_integration`
- [ ] Delete the `continue-on-error` lines for `packet_parser` / `packet_tx` lint if `-Wall` is now clean
- [ ] CI green, PR URL in the task comments

**"PR: merge `Natalie` branch"**
> After Zhiting's merge, rebase so the diff is only the README "Troubleshooting & Resolved Bugs" section. The first item is marked `[]` but describes a resolved bug: tick it or reword it.

**"Confirm every skeleton port list against its spec"**
> Every skeleton says "port list is provisional". Fix them all **once, in one `fecim_top` commit**, so nobody rewires the top mid-week. The full non-ideality set adds three things the skeletons may not carry yet:
> - **IR drop:** the attenuation multiply sits in `act_buffer`'s broadcast stage. It needs `TGT_ATTEN` bulk writes and `noise_en.ir` (activation-buffer-spec §6.2, mac-array-spec §8).
> - **Read noise:** the noise lives *inside* each lane. `mac_lane` needs `read_sigma`, `noise_seed`, `reseed` and `noise_en.read` inputs, and per-lane `LANE_SALT` (mac-array-spec §5).
> - **ADC quantization:** one shared `adc_quant` on the drain path, before `result_buffer`, driven by `ADC_BITS` (mac-array-spec §9).

Checklist:
- [ ] `control_fsm`, `readout_ser`, `display_driver`, `argmax_unit` (Zhiting)
- [ ] `write_path`, `act_buffer` (incl. `atten_rom` write port), `mac_lane` read-noise ports, `read_noise`, `adc_quant`, `result_buffer` (Megan)
- [ ] `fecim_top` wiring updated in one commit; full-design `-Wall` lint still clean
- [ ] The two `write-path-spec` errata flagged in October (zero-extension, 9-bit product truncation) confirmed fixed in the spec
- [ ] Ethan confirms the model's `step()` interface will mirror the agreed signal timeline

**"Board and dongle rota"**
> Hardware time is the scarcest resource. **Zhiting keeps board 1 + dongle 1** for bring-up, timing and soak testing. **Natalie gets board 2 + dongle 2 from Megan at Thu 19**: she needs it for the driver, the digits apps and the hardware sweeps, and Megan's work is simulation-only. Both boards stay with Zhiting and Natalie through Thanksgiving, since they do most of the hardware work that week (soak test, MLP on the board, sweeps). If either travels without their board, it goes to a teammate who's staying, and the task notes who. Both come to the Mon 30 dry run and the Tue 1 presentation.

Checklist:
- [ ] Board 2 + dongle 2 handed to Natalie Thu 19; jumper checked at 3.3 V before first connection
- [ ] Thanksgiving board locations written in the task
- [ ] Both boards confirmed for Mon 30 and Tue 1

**"Check rubric, time limit, format, peer-eval and written-report due dates"**
> On Canvas: time limit, whether a live demo is expected, required sections, and whether peer evaluations or a written final report are due with the presentation. If anything changes a date on this board, change it Tuesday.

---

## Bucket 2 — Lane A · Zhiting Li

| Task | Due | Priority | Labels |
|---|---|---|---|
| A1: `control_fsm` + `tb_control_fsm` | Wed 18 Nov | Urgent | Gate |
| A2: `IDENTIFY`, `SET_CONFIG` / `GET_CONFIG` on hardware at 921600 (hardware gate) | Thu 19 Nov | Urgent | Gate, Demo, Verify |
| A3: Readout path — `readout_ser` → `packet_tx` for `READ_RESULT` / `READ_ARGMAX` | Sat 21 Nov | Urgent | Gate |
| A4: `argmax_unit` with runner-up (moved from Lane B) | Sat 21 Nov | Urgent | Gate |
| A5: Quartus project for `fecim_top` + first fit | Sun 22 Nov | Urgent | Gate, Verify, M0 evidence |
| A6: `display_driver` on HEX | Wed 25 Nov | Important | Demo |
| A7: Timing closure at 50 MHz, 64 lanes, all non-idealities | Tue 24 Nov | Urgent | Verify, M0 evidence |
| A8: Error handling, recovery and soak test | Fri 27 Nov | Important | Verify |
| A9: Bitstream release process + replication guide | Sat 28 Nov | Important | M0 evidence |

**"A1: `control_fsm`"**
> `IDLE → CLEAR → ACC → DRAIN → DONE` per `control-fsm-spec.md` §3–4. One counter drives `seq_cnt` / `act_addr` across `PASSES` (2 at 64 lanes). Hold in ACC for `LANE_PIPE_DEPTH` cycles after the last row. Honor `active_rows` / `active_cols` from `TILE_CFG`, **including widths that aren't a multiple of 64** (protocol §6.3b). `cycle_cnt` feeds `CYCLE_CNT`; the corrected prediction for a full 128×128 tile is 7.8 µs, about 390 cycles. `acc_en` must be exact, because **the read-noise LFSRs advance only on `acc_en`**: a stray cycle desynchronises the model.

Checklist:
- [ ] `start_compute` and `manual_compute` (KEY1) both start a run; a start while busy → parser answers `ST_BUSY`
- [ ] `acc_clear` once per pass; `acc_en` exactly `active_rows` cycles per pass
- [ ] Drain covers `active_cols` across both passes, in result-buffer address order
- [ ] `compute_done` and `run_start` are 1-cycle pulses
- [ ] `tb_control_fsm`: 8×8, 48×48, 64×64, 128×128 and back-to-back runs
- [ ] Cycle count matches Ethan's `step()` model prediction
- [ ] Lint waivers removed, `-Wall` clean, added to `run_sims.sh` and CI

**"A2: Hardware gate"**
> The original week-3 gate, still open. A board-level top (`quartus/fecim_board_top.sv`) wraps `fecim_top` with the DE10-Lite pins from `pins.tcl`. The parser, `config_regs` and `packet_tx` are real; the array side can still be skeletons. **Show it live with Natalie's driver (D2) at Thu 19.**

Checklist:
- [ ] Board top maps `MAX10_CLK1_50`, `KEY[0]` reset, `KEY[1]` manual compute, `ARDUINO_IO[0/1]` UART, `LEDR`, `HEX0–5`
- [ ] Reset through `reset_sync`; KEYs are hardware-debounced, so no debounce RTL
- [ ] Boots at 115200 (`BAUD_INC = 2416`); driver switches to 19327 (921600) and reconnects
- [ ] `IDENTIFY` returns magic `0xFEC1`, `PROTO_VER = 0x02`, `BUILD_ID`
- [ ] `SET_CONFIG` → `GET_CONFIG` round-trip on every RW register
- [ ] Bad CRC → `ST_CRC_ERR` from the board
- [ ] 30-s backup recording

**"A3: Readout path"**
> `READ_RESULT [start:2][count:2]` returns `count × 4` bytes from `result_buffer`; `READ_ARGMAX` returns the 10-byte §4.6 record from `argmax_unit`. All of it is little-endian. Test against a pre-filled stub buffer until Megan's B6 lands.

Checklist:
- [ ] Byte order matches protocol §2 and Natalie's decoder
- [ ] `start + count` beyond `active_cols` → `ST_ADDR_RANGE`
- [ ] Testbench: known words in, exact frame and CRC out
- [ ] Re-run with the real `result_buffer` and `argmax_unit`

**"A4: `argmax_unit`"**
> Tracks `max_idx`, `max_val` and `second_val` as the drain writes the result buffer, over **active columns only**. Ties go to the lowest index, matching `numpy.argmax`; with coarse ADC settings ties are common, so this matters. `second_val` is undefined when `active_cols == 1`. Megan reviews it, because it sits on her drain path.

Checklist:
- [ ] Mask: columns ≥ `active_cols` ignored (the 64→10 digits layer depends on it)
- [ ] Tie → lowest index; tested with deliberately tied values
- [ ] Runner-up correct when the max is in column 0 and when it's the last active column
- [ ] Sees post-ADC values (the same values `READ_RESULT` returns)
- [ ] Unit testbench in `run_sims.sh`

**"A5: Quartus project for `fecim_top` + first fit"**
> Script it (`quartus/fecim/create_project.tcl`, like `dsp_pack_test`) so anyone can rebuild from a clean clone. The fit only means something once A1 and the Lane B datapath (B2, B4) are in, since tied-off skeletons let Quartus prune the multipliers. The plan's budget at 64 lanes is ~18,300 LEs, 66 M9K and **72 multiplier blocks of 144**. If the report shows ~128 blocks, the 9×9 pairing failed (mac-array-spec §12 test 11): check operand widths and signedness before anything else.

Checklist:
- [ ] Project script committed; builds from a clean clone
- [ ] Fits at 64 lanes, 128×128, all non-idealities enabled in RTL
- [ ] Multiplier count matches mac-array-spec §11; pairing confirmed
- [ ] `.fit.summary` committed to `quartus/reports/`; numbers sent to Megan for B9

**"A6: `display_driver`"**
> HEX shows the FSM state while computing and the last `max_idx` after `DONE`, so the digit classifier's answer appears on the board. The HEX displays are **active-low** (common anode).

Checklist:
- [ ] Segment encoding active-low; verified on the board
- [ ] Shows the digit 0–9 after a digits inference
- [ ] Lint waivers removed

**"A7: Timing closure"**
> `.sdc` with a 20 ns clock on `MAX10_CLK1_50`, async inputs cut. The known risk is the `N+2` lane stage (noise add, then multiply). The spec's fix: register between the add and the multiply and set `LANE_PIPE_DEPTH = 4` (mac-array-spec §12 test 12). **If you change the pipeline depth, tell Ethan the same hour**, because the model and co-sim must change with it. Also watch the IR multiply in `act_buffer` and the `cell_hash16` path in `write_path`.

Checklist:
- [ ] `.sdc` committed
- [ ] Positive setup and hold slack at the slow corner
- [ ] Any pipeline change mirrored in `fecim_pkg.sv`, the model and the co-sim, with L2 re-run green
- [ ] `.sta.summary` committed

**"A8: Error handling, recovery and soak test"**
> This is the plan's weeks 8–9 robustness work. A dropped byte, a host crash mid-packet, or a `COMPUTE` while busy must never leave the board needing a power cycle.

Checklist:
- [ ] Parser timeout (100 ms) recovers from a truncated packet
- [ ] `CTRL[0]` soft reset returns every module to idle without losing the baud setting
- [ ] `ST_BUSY` and `ST_ADDR_RANGE` paths exercised on hardware
- [ ] Soak: ≥ 1 hour of random commands + compute with periodic L4 checks, zero hangs, log committed
- [ ] Recovery behaviour documented in `hardware.md`

**"A9: Bitstream release process + replication guide"**
> A GitHub release with the `.sof` / `.pof`, its commit hash and fit/timing summaries, plus a replication guide: DE10-Lite wiring to the CP2102 (with the 3.3 V jumper warning), Quartus version, programming steps, and the first command to run. **Someone outside the team should be able to reproduce the demo from it.**

Checklist:
- [ ] Release script or documented manual steps; `BUILD_ID` bumped per release
- [ ] Wiring diagram (photo + schematic) in `docs/img/`
- [ ] Replication guide tested by a teammate on their own laptop
- [ ] Release created for the cutoff commit

---

## Bucket 3 — Lane B · Megan Mendez

Every module here has a golden-vector or spec-test pass criterion from Lane C or mac-array-spec §12. Draft with AI, then iterate until the vectors pass; don't open a PR before they do.

| Task | Due | Priority | Labels |
|---|---|---|---|
| B1: `write_path` stage 1 — quantize + lane-select decode | Wed 18 Nov | Urgent | Gate, Verify |
| B2: `act_buffer` + IR attenuation (`atten_rom`, `WRITE_ATTEN`) | Fri 20 Nov | Urgent | Gate, Verify |
| B3: `write_path` stages 2–3 — D2D + stuck-at | Sat 21 Nov | Urgent | Verify |
| B4: `read_noise` integrated into `mac_lane` | Sat 21 Nov | Urgent | Gate, Verify |
| B5: `adc_quant` (shared, round-to-nearest) | Fri 20 Nov | Urgent | Verify |
| B6: `result_buffer` | Fri 20 Nov | Urgent | Gate |
| B7: `tb_ab_integration` on real modules | Mon 23 Nov | Important | Verify, M0 evidence |
| B8: mac-array-spec §12 tests 2–10, all passing | Wed 25 Nov | Important | Verify, M0 evidence |
| B9: Utilization report | Sat 28 Nov | Important | M0 evidence |

**"B1: `write_path` stage 1"**
> Bulk stream → `quantize_weight()` → lane select / address → lane weight memory port A. Use the exact integer recipe `model_exact` implements: `u = w ^ 0x80; level = (u * N) >> 8; w_q = ((level * QUANT_MULT) >> 8) - 128`, gated by `NOISE_EN[0]`. `QUANT_MULT` comes from the register. **Pass criterion: Ethan's `tb/golden/write_path/` quant vectors.**

Checklist:
- [ ] Column → lane/pass mapping per `write-path-spec` and `mac-array-spec`
- [ ] Quant on and off, N ∈ {2, 3, 5, 8, 255}, against golden vectors
- [ ] Shared write-path multiplies stay 9-bit
- [ ] Lint waivers removed

**"B2: `act_buffer` + IR attenuation"**
> `WRITE_ACT` fills the activation M9K. During ACC it broadcasts to all 64 lanes, with the activation arriving **two cycles behind the weight address** (the timing `run_l0` already assumes). IR drop goes here, not in the lanes: `act_atten = noise_en.ir ? (act_raw * atten_rom[row]) >> 8 : act_raw`, one multiplier total (activation-buffer-spec §6.2, mac-array-spec §8). `atten_rom` is 128 × 8-bit Q0.8, written by `WRITE_ATTEN` (`TGT_ATTEN = 2`, `bulk_addr` = row), and reset to all 255. **It must fit inside the existing stage, so `LANE_PIPE_DEPTH` doesn't change.**

Checklist:
- [ ] Activations unsigned 0..255; broadcast latency matches `mac_lane`
- [ ] `atten_rom` reset to 255, write via `TGT_ATTEN`, readback tested
- [ ] IR on/off against Ethan's IR golden vectors
- [ ] Broadcast integrity test: all-ones weights → every column identical (activation-buffer-spec)
- [ ] Unit testbench in `run_sims.sh`; lint waivers removed

**"B3: `write_path` stages 2–3"**
> **D2D:** per-cell offset from `cell_hash16()` on the **global** cell address, triangular (n=2), scaled by `D2D_SIGMA` (reg/256), **saturating** at ±127. **Stuck-at:** the cell's hash vs `STUCK_RATE[15:0]`, with `[16]` selecting zero or rail (±127); it overrides stages 1–2. Keep the comment explaining that triangular n=2 replaced Irwin-Hall n=4 deliberately (the multiplier budget), since the report has to disclose it.

Checklist:
- [ ] Global address hash, not per-lane local
- [ ] Saturation, never wrap
- [ ] All D2D, stuck-zero, stuck-rail and combined golden vectors pass
- [ ] Re-run A5's fit after merging

**"B4: `read_noise` integrated into `mac_lane`"**
> Per mac-array-spec §5:
> - **LFSR:** a per-lane Galois LFSR seeded from `NOISE_SEED` and `LANE_SALT`, with the `| 32'd1` zero guard. **It advances only on `acc_en`.**
> - **Distribution:** a bell-shaped `ih9` built from the four LFSR bytes, scaled by `READ_SIGMA` (reg/512) and clamped to ±127, then added to the weight after the memory read to give 9-bit `w_n`.
>
> `mac_lane` currently passes golden vectors on the clean path, so **the clean vectors and single-lane L0 must still pass with noise off** after this change. Ethan's read-noise vectors (C2) are the pass criterion with noise on.

Checklist:
- [ ] Noise-off: 261/261 clean `mac_lane` vectors + `run_l0` still green
- [ ] Noise-on: Ethan's read-noise vectors pass for lanes 0, 5, 63
- [ ] `CTRL[2]` reseed works; same seed → same results
- [ ] `w_n` width assertion permanent in the testbench (§12 test 6)
- [ ] Re-run A5's fit: one more multiply per lane is the budget risk

**"B5: `adc_quant`"**
> One shared instance on the drain path (mac-array-spec §9). An arithmetic shift by `sh = ACC_USED_W - adc_bits` with **round-to-nearest**, not truncation; truncation adds a negative bias. Guard `sh == 0`, so `ADC_BITS ≥ ACC_USED_W` is an exact no-op, including values up to 31.

Checklist:
- [ ] Bits 4..`ACC_USED_W` against Ethan's ADC golden vectors
- [ ] Zero-mean error demonstrated
- [ ] `ADC_BITS` 24–31 are exact no-ops

**"B6: `result_buffer`"**
> Drain writes land post-ADC 32-bit values; `READ_RESULT` reads them back through A3. `CTRL[1]` clears it. `ACC_W` stays 32 throughout: 24 overflows with noise on.

Checklist:
- [ ] Drain order: distinct known value per column lands at the right address (§12 test 8)
- [ ] Clear works; read-during-compute → `ST_BUSY`
- [ ] Unit testbench

**"B7: `tb_ab_integration` on real modules"**
> Swap `lane_a_stub` / `lane_b_stub` for the real `control_fsm`, `act_buffer`, `write_path`, `mac_array`, `adc_quant`, `result_buffer` and `argmax_unit`, and check full MVMs at the seam. This is the A/B contract, so Zhiting reviews.

Checklist:
- [ ] 48×48, 64×64 and 128×128 clean MVMs vs expected sums
- [ ] One all-non-idealities run vs model output
- [ ] Stubs kept for reference; no longer the default target
- [ ] Zhiting approves

**"B8: mac-array-spec §12 tests 2–10"**
> The spec already lists the tests that prove the array. Test 2 (full array, clean, bit-exact, 1,000 random 128×128 matrices) is shared with Ethan's C4, so reference his run rather than duplicating it. The rest are yours: LFSR reproducibility across idle gaps, noise moments from 10⁶ samples (report measured σ and kurtosis, **don't claim Gaussian or true Irwin-Hall**), lane decorrelation, clamp boundary, accumulator headroom at worst case, drain ordering, ADC rounding and IR factorization.

Checklist:
- [ ] Tests 3–10 each have a named testbench or script, in CI where fast enough
- [ ] Measured noise moments recorded for the report
- [ ] Results table committed to `docs/` and linked from the verification report (C10)

**"B9: Utilization report"**
> From A5/A7's final fit: LEs, M9K, multiplier blocks and registers, per module where Quartus reports it, against the plan's §3.8 budget and the 144-block limit. Explain any gap from the prediction. This gives the resource slide and an M0 artifact.

---

## Bucket 4 — Lane C · Ethan Ruddell

The model is the reference for every RTL module, so **C1–C2 are the critical path for the whole team**. Ship vectors first, then grow the tests.

| Task | Due | Priority | Labels |
|---|---|---|---|
| C1: `model_exact` D2D + stuck-at + write-path golden vectors | Tue 17 Nov | Urgent | Gate, Verify |
| C2: Read noise, IR drop, ADC in the model + golden vectors | Wed 18 Nov | Urgent | Gate, Verify |
| C3: `ModelExact::step()` + full-array MVM sequencer | Fri 20 Nov | Urgent | Gate |
| C4: Full-array L0: Verilated `mac_array` vs model, 1,000 × 128×128 | Sat 21 Nov | Urgent | Gate, Verify |
| C5: Full-top co-sim with virtual UART — L1 and L2 | Mon 23 Nov | Urgent | Verify, Demo, M0 evidence |
| C6: Calibration — set A documented, register mapping, provenance (week-7 gate) | Mon 23 Nov | Urgent | M0 evidence, All-hands |
| C7: CI — full-top job, L0 blocking, all lint blocking | Mon 23 Nov | Important | Setup |
| C8: L4 script — hardware ≡ `model_exact` at matched seed | Tue 24 Nov | Urgent | Verify, Demo |
| C9: L3 statistical validation | Fri 27 Nov | Important | Verify, M0 evidence |
| C10: Coverage matrix closed + verification report | Sat 28 Nov | Important | M0 evidence |

**"C1: D2D + stuck-at + write-path golden vectors"**
> Replace the two `TODO(lane C, week 3)` stubs in `model_exact.cpp`:
> - **D2D:** `cell_hash16` on the global cell address, a triangular (n=2) offset scaled by `D2D_SIGMA`, saturating at ±127.
> - **Stuck-at:** chosen by hash vs `STUCK_RATE`, with zero or rail mode; it overrides the earlier stages.
>
> Then emit `tb/golden/write_path/*.hex` for Megan: quant only, D2D only, stuck-zero, stuck-rail, all combined. **This gates B1 and B3**, so the vectors ship Tuesday even if the unit tests are still growing.

Checklist:
- [ ] Unit tests: triangular shape and zero mean, saturation, seed determinism, stuck-rate accuracy, rail vs zero
- [ ] Mutation check: a wrapping add and a local-address hash both fail the tests
- [ ] Vectors + regeneration check in CI, like the `mac_lane` vectors

**"C2: Read noise, IR drop, ADC in the model + vectors"**
> Replace the three throwing stubs in `read_path.cpp`:
> - **`read_noise_q`:** Galois LFSR, `LANE_SALT`, zero guard, advance-on-`acc_en`, the four-byte `ih9` construction, `READ_SIGMA` (reg/512) scale and ±127 clamp, exactly as mac-array-spec §5.
> - **`ir_attenuate`:** `(a * atten[row]) >> 8` before the multiply.
> - **`adc_quantize`:** round-to-nearest shift with the `sh == 0` guard.
>
> Emit golden vectors for each: per-lane noise sequences for lanes 0, 5 and 63, IR on a ramp profile, and ADC at bits 4/8/16/24/31. **This gates B2, B4 and B5.**

Checklist:
- [ ] Each effect alone matches its spec section; combined order matches the RTL pipeline order
- [ ] Read-noise vectors include a reseed and an idle gap (§12 test 3)
- [ ] IR factorization proof: activation-side == per-product attenuation, bit-exact (§12 test 10, reference side)
- [ ] `NOISE_EN` no longer throws for any bit

**"C3: `ModelExact::step()` + full-array sequencer"**
> `model_exact.h` already promises that `compute()` equals `start_compute()`, then `step()` until `!busy()`, then `results()`. Build `step()` to mirror the agreed `control_fsm` timeline (pass loop, `active_rows` / `active_cols`, pipeline hold, drain order) so co-sim can compare per cycle and the model predicts `CYCLE_CNT`.

Checklist:
- [ ] `compute()` == the `step()` path for every config in the matrix
- [ ] Non-multiple-of-64 widths (48) and the argmax mask covered
- [ ] Predicted cycle count for 128×128 shared with Zhiting for A1

**"C4: Full-array L0"**
> mac-array-spec §12 test 2, the gate everything else assumes: all 64 lanes, `NOISE_EN = 0`, 1,000 random 128×128 matrices, Verilated RTL vs model, accumulators compared every cycle. **Zero tolerance.** On mismatch, compare `w_n`, then `prod`, then `acc`; the first mismatch upstream explains the rest (verification design §5).

Checklist:
- [ ] `model/cosim/run_l0_array` target; extends the single-lane harness
- [ ] 1,000 matrices pass; runtime recorded
- [ ] A short version in CI as blocking

**"C5: Full-top co-sim with virtual UART — L1 and L2"**
> Verilate `fecim_top` and feed it **protocol bytes directly into the parser** (verification design §5.1), generated with Natalie's `protocol.py` so host and RTL share one encoder. **L1** runs each non-ideality alone and **L2** runs all six together, every result word and `READ_ARGMAX` compared bit-exact against `model_exact`. This is the strongest simulation demo, and the reason L4 can be a wiring exercise.

Checklist:
- [ ] `model/cosim/run_top` target
- [ ] L1: every row of the §6.1 matrix in simulation, directed + random
- [ ] L2: all enabled, 100+ random configs and seeds
- [ ] Tile sizes 8×8, 32×32, 48×48, 128×128
- [ ] The live-bug demo (one-character sign-extension) re-run against the full top
- [ ] 30-s backup recording

**"C6: Calibration — set A documented (week-7 gate)"**
> Co-owned with Natalie; you're primary for the physics, and she owns the code path (D4). In `device-model.md`, take set A (`dualport_fefet_mlc`, Chatterjee et al., TED 2022) to register values: `QUANT_LEVELS = 8`, D2D σ → `D2D_SIGMA`, stuck rate if reported. Fill the gaps set A doesn't cover, and **label the result composite** per plan §3.9:
> - **Read noise:** set A has no C2C σ; use De et al. (17.7 mV) as a sweep range.
> - **IR drop:** a coefficient profile from a stated wire-resistance assumption (Lambertini et al. as the benchmark reference), disclosed as an approximation of a full resistive-network solve.
> - **ADC bits:** a typical CIM range, swept.
>
> State the provenance assumption plainly: it replaced the polarization-to-conductance bridge.

Checklist:
- [ ] Each register value with its source, figure and conversion arithmetic
- [ ] Composite parts flagged; sweep bands for anything the literature doesn't pin down (verification design §7.5)
- [ ] Set A YAML updated to match; Natalie confirms D4 loads it
- [ ] Sign-off from all four (it's a design gate)

**"C7: CI"**
> Add the `run_l0_array` (short) and `run_top` jobs. Delete every `continue-on-error: true` as its fix lands. **`main` is green at the Sun 29 cutoff**, and CI is a closing beat of the talk.

Checklist:
- [ ] All new unit testbenches from A and B in the sim job
- [ ] Full-array L0 (short) and top-level L2 (short) blocking
- [ ] No `continue-on-error` left; green screenshot saved

**"C8: L4 script — hardware ≡ model"**
> `fecim check-hw`: same config, seed, weights and activations to the board (via Natalie's driver) and to `model_exact`; diff every column and the argmax record. Run it on board holders' machines. It also runs against the sim backend, so it's testable before the board computes. **First L4 run with Zhiting on Wed 25, as soon as A7's bitstream exists.**

Checklist:
- [ ] Bit-exact pass/fail plus per-column diff table
- [ ] Runs every §6.1 matrix row in a loop (the "Hardware" column)
- [ ] Usage in the README

**"C9: L3 statistical validation"**
> Unlike L0–L2 and L4, this checks statistics, not bit-exactness, and the report must keep it separate. Show that `model_exact` (and therefore the hardware) reproduces set A's published behaviour: the D2D distribution's σ vs the paper's, level spacing, stuck fraction, read-noise σ vs the chosen C2C range. Include uncertainty bands for the sweeps (plan week 9).

Checklist:
- [ ] Histograms model-vs-published for each calibrated parameter
- [ ] Pass criteria stated before running (e.g. σ within the paper's reported spread)
- [ ] Figures in `results/` with provenance; summary table for slides

**"C10: Coverage matrix closed + verification report"**
> Fill `model-verification-design.md` §6.1 with links to the actual runs: Directed, Random ×1000, Corner and Hardware for every row. Then write the verification report (plan week 11): the L0–L4 hierarchy, what each level proved, the coverage matrix, the B8 spec-test table, L3 results, the bugs caught (with the mutation evidence), and known limitations.

Checklist:
- [ ] Every matrix cell ✓ with a link, or an explicit, explained gap
- [ ] §6.2 corner cases each mapped to a test
- [ ] Report committed to `docs/`; Megan and Zhiting review their sections

---

## Bucket 5 — Lane D · Natalie Poche

| Task | Due | Priority | Labels |
|---|---|---|---|
| D1: Packaging — `pyproject.toml`, `pip install -e ./sw` | Tue 17 Nov | Urgent | Setup, Gate |
| D2: Full command set in the driver | Thu 19 Nov | Urgent | Gate, Demo |
| D3: Sim backend — `Crossbar(backend="sim")` | Sat 21 Nov | Urgent | Gate |
| D4: Parameter set → registers + IR profile generation | Mon 23 Nov | Urgent | Gate, Verify |
| D5: Sweep harness with provenance | Mon 23 Nov | Urgent | Verify |
| D6: Linear digits classifier (64→10) — sim, then board | Wed 25 Nov | Urgent | Demo |
| D7: MLP digits classifier (64→32→10) — sim, then board | Fri 27 Nov | Urgent | Demo |
| D8: Headline plots: calibrated baseline + degradation sweeps on hardware | Sat 28 Nov | Urgent | Demo, M0 evidence |
| D9: Wheels, clean-machine test, user guide + API docs | Sat 28 Nov | Important | M0 evidence |

**"D1: Packaging"**
> CI's "Check Python package exists" step and every demo command assume `pip install -e ./sw`. Delete the empty `sw/fecim/code.py`, and give `crossbar.py` (also 0 bytes) the real `Crossbar` class in D3.

Checklist:
- [ ] Deps: `pyserial`, `numpy`, `scikit-learn`, `pyyaml`, `matplotlib`
- [ ] Clean venv install on Windows and WSL
- [ ] `pytest sw/tests` green in CI

**"D2: Full command set in the driver"**
> Every opcode in `protocol.md` §4 as a method, and every non-OK status as its own exception. `WRITE_WEIGHTS` / `WRITE_ACT` / `WRITE_ATTEN` chunk at `MAX_PAYLOAD = 512`. Baud switching is one driver call. **Show `IDENTIFY` live with Zhiting's A2 on Thu 19.**

Checklist:
- [ ] `identify`, `set_config` / `get_config`, `write_weights`, `write_act`, `write_atten`, `compute`, `read_result`, `read_argmax`
- [ ] Full 128×128 weight load timed (expect ~178 ms at 921600)
- [ ] Every status exception tested via the loopback fake and the virtual-UART co-sim (C5)
- [ ] COM port and `/dev/ttyUSB*`

**"D3: Sim backend"**
> `Crossbar(backend="sim")` runs the exact same app code against `model_exact`. Use pybind11 per the plan; **if it isn't working by Thu 19, switch to a subprocess wrapper** around a small `fecim_cli` binary (Ethan adds it next to `fecim_demo`). Every sweep and app runs on sim first, so this gates D5–D8.

Checklist:
- [ ] Identical API for `sim` and `serial`
- [ ] Bit-exact with C8's model path for the same seed
- [ ] Throughput timed: a full 1,797-image `load_digits` pass

**"D4: Parameter set → registers + IR profile"**
> Calibration co-owner: the code side of C6. `xbar.load_parameter_set("dualport_fefet_mlc")` reads the YAML and issues every `SET_CONFIG`, including the paired `QUANT_LEVELS` / `QUANT_MULT` write (protocol §6.2). It also generates the 128-entry IR attenuation profile from the documented model and sends it with `WRITE_ATTEN`. **One call to switch device technologies.**

Checklist:
- [ ] YAML schema validated; unknown keys rejected
- [ ] `QUANT_MULT` computed host-side to match the RTL's expectation
- [ ] IR profile generator matches C6's documented formula; plotted for the slides
- [ ] Round-trip: load, `GET_CONFIG` every register, compare

**"D5: Sweep harness with provenance"**
> Per `application-spec.md` §4: reseed between points, not within (§4.2); use `READ_ARGMAX` (§4.3, about 4× less traffic); **always record per-class accuracy** (§4.4); write provenance with every result (§4.5): git hash, `BUILD_ID`, parameter set, seed and backend.

Checklist:
- [ ] One sweep definition runs on `sim` or `serial` unchanged
- [ ] CSV + provenance JSON in `results/`
- [ ] Resumable: a sweep interrupted overnight continues where it stopped

**"D6: Linear digits classifier"**
> `application-spec.md` §3.1: sklearn `load_digits`, logistic regression trained off-array (~95% float), mapped onto 64 rows × 10 columns with per-column weight scaling and activation scaling (§2.2–2.5), using `TILE_CFG` for the active region. Report float, ideal-crossbar, clean-hardware and set-A accuracy, with per-class breakdown. Then run C8 on a few inputs, so "bit-exact" is true on stage.

Checklist:
- [ ] Fixed split and seed recorded
- [ ] `--backend sim` by Sun 22; `--backend serial` on board 2 by Wed 25
- [ ] Hardware accuracy == sim accuracy for the same config and seed
- [ ] Digit appears on HEX (A6)
- [ ] 30-s backup recording

**"D7: MLP digits classifier"**
> The semester-1 target application (§3.2): 64→32→10, both layers on the tile, loaded sequentially, with ReLU and activation requantization on the host between them. It shows multi-layer execution and makes the weight-reload cost visible, which motivates semester 2.

Checklist:
- [ ] Trained off-array; float baseline recorded
- [ ] Requantization between layers matches §2.3–2.5; documented
- [ ] Sim by Tue 24; board by Fri 27
- [ ] Per-inference time split: weight reload vs compute vs readout
- [ ] 30-s backup recording

**"D8: Headline plots"**
> The plan's week-9 "degradation sweeps": the results slides. **Calibrated baseline:** set A on hardware vs ideal. **Degradation:** accuracy vs each non-ideality alone (levels, D2D σ, stuck rate, read σ, IR strength, ADC bits), with set A marked on each curve, ≥ 5 seeds and error bars from C9's bands. Run on sim by Tue 24 to fix the axes, then on the boards Wed 25 – Sat 28. A 400-point × 1,000-image sweep with `READ_ARGMAX` is ~12 minutes at 921600, so the long multi-seed runs can go overnight during the break.

Checklist:
- [ ] Sim versions by Tue 24, axes agreed with Ethan
- [ ] Hardware versions by Sat 28, from committed result files
- [ ] Sim and hardware curves overlaid (they should coincide; that's an L4 result in itself)
- [ ] PNGs exported for slides

**"D9: Wheels, clean-machine test, user guide + API docs"**
> Plan weeks 10–11 for Lane D: build a wheel and install it on a machine that has never seen the repo. Document the API (docstrings → generated reference) and write a user guide: install, connect, load a parameter set, run digits, run a sweep.

Checklist:
- [ ] Wheel builds in CI
- [ ] Clean-machine test on a teammate's laptop, logged
- [ ] User guide + API reference committed to `docs/`

---

## Bucket 6 — Integration Checkpoints (Gate)

One owner checks each and posts the result in Teams. **A missed checkpoint triggers an all-hands swarm the next day:** anyone whose own Gate tasks are done helps the owner.

| Task | Owner | Due | Priority | Labels |
|---|---|---|---|---|
| Thu 19 meeting: hardware gate, vectors delivered, port lists frozen | Ethan | Thu 19 Nov | Urgent | All-hands, M0 evidence |
| Checkpoint: full-array L0 green | Ethan | Sat 21 Nov | Urgent | Gate, Verify |
| Checkpoint: L1 + L2 green in full-top co-sim; first fit done | Ethan | Mon 23 Nov | Urgent | Gate, Verify |
| Checkpoint: L4 green on hardware (all six, bit-exact), timing met | Zhiting | Wed 25 Nov | Urgent | Gate, Verify |
| Fri 27 virtual sync (Teams): L4, MLP, plots, slides | Ethan | Fri 27 Nov | Important | All-hands, M0 evidence |
| Checkpoint: semester-1 complete — every Must task Completed | Ethan | Sat 28 Nov | Urgent | Gate, All-hands |

**"Thu 19 meeting"**
> The last time all four of us and both boards are in a room before the dry run, so use it for hardware.

Agenda checklist:
- [ ] A2 + D2: `IDENTIFY` live on the board
- [ ] C1/C2 vectors delivered; B1 passing; B2–B6 status
- [ ] A1 `control_fsm` merged; C3 cycle prediction matches it
- [ ] Board 2 to Natalie; Thanksgiving board plan
- [ ] Slide section owners assigned (Bucket 7)
- [ ] Any task at risk gets a named helper today
- [ ] Fri 27 virtual sync time agreed

**"Fri 27 virtual sync"**
> This replaces the Thursday meeting we lose to Thanksgiving. It's a Teams call, 30–45 minutes, with the board holders sharing their screens.

Agenda checklist:
- [ ] L4 matrix status on hardware; soak test (A8) running or done
- [ ] MLP on the board (D7) — live screen share
- [ ] Hardware sweep plots reviewed (D8); axes and set-A markers final
- [ ] Slide draft walked section by section; gaps assigned for Sat 28
- [ ] Any Must task at risk for Sat 28 gets a named helper
- [ ] Notes go into the meeting log

**"Checkpoint: semester-1 complete"**
> Every Must task Completed by Sat 28 night, leaving Sunday morning as a true buffer before the rehearsal and the 18:00 cutoff. If something's still open Saturday night, the swarm rule applies Sunday morning.

**"Checkpoint: L4 green on hardware"**
> C8 passes on the board for every §6.1 matrix row, and A7 shows positive slack. Everything after this is applications, plots and documentation, which can all proceed in parallel and over the break.

---

## Bucket 7 — Presentation

| Task | Owner | Due | Priority | Labels |
|---|---|---|---|---|
| Slide deck draft (section owners) | Ethan | Wed 25 Nov | Important | M0 evidence |
| Results slides: plots, resources, latency, verification | Natalie | Sat 28 Nov | Urgent | M0 evidence |
| Design-corrections and modelling-assumptions slide | Ethan | Wed 25 Nov | Important | M0 evidence |
| Demo video + run sheet + backup recordings | Natalie | Sat 28 Nov | Urgent | Demo, M0 evidence |
| Remote rehearsal, timed (slides + recorded demos) | Ethan | Sun 29 Nov | Important | All-hands |

**"Slide deck draft"**
> Start from the Design Draft deck (its twelve slides have speaker notes) and the pre-alpha "how it works" bullets. Flow:
> 1. The problem
> 2. Why emulate a ferroelectric crossbar
> 3. Architecture
> 4. Follow one packet from the host to the array (D → A → B → C, the pre-alpha order)
> 5. The six non-idealities and where each is injected
> 6. The verification hierarchy
> 7. Calibration
> 8. Results
> 9. Semester 2
>
> One owner per section, assigned Thu 19.

Checklist:
- [ ] Draft in the shared deck by Wed 25
- [ ] Every number traces to a committed file (fit report, results CSV, CI run, L4 log)

**"Results slides"**
> Five slides at most:
> 1. Accuracy vs each non-ideality, with set A marked (D8)
> 2. Linear and MLP digits: float vs ideal vs set-A hardware accuracy, per class
> 3. Resource table from the final fit vs 144 multiplier blocks (B9)
> 4. Measured `CYCLE_CNT` vs the predicted 7.8 µs for a full tile
> 5. The coverage matrix, all green, with L3 histograms beside it

**"Design-corrections and modelling-assumptions slide"**
> Stated as evidence of rigor:
> - **Corrections:** MVM latency 2.7 → 4.0 / 7.8 µs; 144 multiplier *blocks*, not 288, which forced 9-bit operands, ±127 clamp, Q1.8 σ, 16-bit hash and triangular n=2 D2D; `ACC_W` 24 → 32.
> - **Data source:** published FeFET data replaced in-house HZO. This removed the polarization-to-conductance bridge, and parameter provenance became the new stated assumption.
> - **Modelling approximations to disclose:** IR drop as a row-indexed approximation, not a resistive-network solve; read noise as bell-shaped but not true Irwin-Hall; the composite parts of set A.
> - **Scope:** radiation is out permanently; retention drift is in for semester 2.

**"Demo video + run sheet + backup recordings"**
> The plan's week-12 demo video: a 2–3 minute edited recording covering hardware gate → digits on board → live sweep → co-sim live bug. Plus the pre-alpha-style run sheet (owner, laptop, board Y/N, command, expected output, recording link, time slot) committed to `docs/coursework/`. Record each demo **the day it first works**.

Checklist:
- [ ] Demo video exported and linked
- [ ] Run sheet committed
- [ ] Recordings: A2, C5 + live bug, C8 L4, D6, D7, a live D8 point

**"Remote rehearsal"**
> Teams call, timed against the limit, with recorded demos standing in for live ones. Cut until there's 20% spare. This is the first of the plan's two timed rehearsals; the dry run is the second.

---

## Bucket 8 — Demo Day and Close-out

| Task | Owner | Due | Priority | Labels |
|---|---|---|---|---|
| Merge cutoff — `main` frozen | Ethan | Sun 29 Nov | Urgent | Gate, All-hands |
| Dry run from clean clone, both boards (second timed rehearsal) | Ethan | Mon 30 Nov | Urgent | All-hands |
| Presentation | Ethan | Tue 1 Dec | Urgent | All-hands, Demo, M0 evidence |
| README current state + tag `v1.0.0` + GitHub release | Ethan | Wed 2 Dec | Important | M0 evidence |
| Meeting logs (incl. pre-alpha backfill) | Natalie | Wed 2 Dec | Medium | M0 evidence |
| Peer evaluations (if assigned) | Ethan | Wed 2 Dec | Medium | External |

**"Merge cutoff"**
> **Sun 29 Nov, 18:00.** Anything not on `main` then isn't presented. Any Must task that hasn't landed gets a one-line honest disclosure on the corrections slide, rather than a half-working live demo.

**"Dry run from clean clone"**
> Mon 30, both boards. Every demo from a fresh `git clone` of the cutoff commit on a laptop that isn't the author's, with bitstreams from the A9 release. Anything that fails falls back to its recording on Tuesday.

Checklist:
- [ ] A2 `IDENTIFY` · C8 L4 run · digits linear + MLP on board · one live sweep point
- [ ] C5 co-sim + live bug · CI green screenshot
- [ ] Full talk timed with demos
- [ ] Commit hash recorded for the tag

**"README current state + tag"**
> Status table rewritten for semester-1 completion; tag `v1.0.0` on the dry-run commit, pointing at the A9 release. This is what M0 reviewers see.

**"Meeting logs"**
> Thu 19, Fri 27 (virtual), Sun 29, Mon 30 and Tue 1, committed to `docs/meetings/` (the folder the plan specifies; it doesn't exist yet). **The pre-alpha run sheet and logs were never committed**, so backfill them in the same PR.

---

## Bucket 9 — Semester 2 (parked)

Only semester-2 scope belongs here now. Anything new that comes up during these two weeks either gets a Must task or a line here with a reason.

| Task | Owner |
|---|---|
| Retention drift (replaces radiation; one Q1.8 factor in the write path) | Ethan / Megan |
| Multi-tile support with host-side partitioning | Natalie / Zhiting |
| Differential-pair modelling — verify the multiplier budget first (week-17 check) | Megan |
| Cross-process comparison: sets B (`gf28_hkmg`) and C, accuracy spread | Ethan / Natalie |
| Second application | Natalie |
| pybind11 sim backend, if D3 shipped the subprocess fallback | Natalie |

---

## Conventions

**Every task has exactly one assignee.** Shared work gets a primary owner; the others go in the checklist or notes.

**Paste the PR URL into task comments.** Planner doesn't link to GitHub, and M0 measures contribution against the commit history.

**Gate tasks come first.** If you're blocked, clear someone else's Gate before polishing your own non-Gate work.

**AI-drafted code is held to the same bar as hand-written code.** No RTL PR without its golden-vector or spec-§12 test passing in CI. No model PR without unit tests plus a mutation check. A human teammate reviews every PR. The commit author is the person who submits it.

**Thanksgiving break is working time; Thanksgiving Day is not.** Tasks are due Mon 23 – Sat 28, except Thu 26. Post the daily check-in over the break too, even if it's one line. Anything that slips past Sat 28 is re-dated to Sun 29 morning with a reason in the task comments; nothing new merges after the 18:00 cutoff.
