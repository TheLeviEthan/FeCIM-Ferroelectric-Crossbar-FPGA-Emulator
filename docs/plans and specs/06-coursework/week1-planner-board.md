# FeCIM — Week 1 Planner Board

**Plan name:** `FeCIM — Capstone`
**Week 1:** Wed 30 Sep → Wed 7 Oct 2026

| Lane | Owner | Scope |
|---|---|---|
| **A** — Host interface RTL | Zhiting Li | UART, packet parser, control FSM, config registers, readout, display, top level, Quartus |
| **B** — Array RTL | Megan Mendez | Write path, MAC lanes, read noise, ADC, result buffer, argmax, timing closure |
| **C** — Model and verification | Ethan Ruddell | C++ golden model, Verilator co-sim, CI, coverage; co-owns calibration |
| **D** — Host stack | Natalie Poche | Python driver, API, packaging, weight mapping, demo, sweeps; co-owns calibration |

Buckets are workstreams, not status — Planner tracks Not started / In progress / Completed
separately. Don't make "To Do / Doing / Done" buckets.

---

## Labels

| Color | Label | Meaning |
|---|---|---|
| Red | **Gate** | Blocks other people's work — do first |
| Orange | **All-hands** | Needs sign-off from all four |
| Yellow | **External** | Waiting on someone outside the team |
| Green | **M0 evidence** | Required coursework artifact, must be committed |
| Blue | **Verify** | Validates a design assumption |
| Purple | **Setup** | One-time environment work |

---

## Bucket 1 — Freeze (Week 1)

Everything here blocks work in other buckets. Target **Fri 2 Oct**.

| Task | Owner | Due | Priority | Labels |
|---|---|---|---|---|
| Design lanes assigned | Ethan | Wed 30 Sep | Urgent | All-hands, M0 evidence |
| Freeze `docs/protocol.md` | Zhiting (w/ Natalie) | Fri 2 Oct | Urgent | Gate, All-hands, M0 evidence |
| Freeze config register map | Zhiting (w/ Ethan) | Fri 2 Oct | Urgent | Gate, All-hands |
| Freeze A/B seam signal list | Megan (w/ Zhiting) | Fri 2 Oct | Urgent | Gate, All-hands |
| Approve `fecim_pkg.sv` | Megan (all four review) | Fri 2 Oct | Urgent | Gate, All-hands |

**"Design lanes assigned"** — mark Completed at the kickoff. It exists so the decision and
its date are on the record for M0.

**"Freeze `docs/protocol.md`"**
> Packet framing, command set, status codes, byte order. Little-endian everywhere.
> Natalie encodes against this; Zhiting parses it. Changes after Friday cost three people
> a week.

Checklist:
- [ ] Packet framing (sync, cmd, len, payload, CRC)
- [ ] Command set and opcodes
- [ ] Status codes
- [ ] Byte order stated explicitly
- [ ] `MAX_PAYLOAD` decided
- [ ] Committed via reviewed PR

**"Freeze A/B seam signal list"**
> Zhiting → Megan: `seq_cnt`, `act_addr`, `acc_clear`, `acc_en`, `shift_en`, `noise_en`,
> config values, `bulk_*`. Megan → Zhiting: result buffer read port, argmax outputs,
> `compute_busy`.
> Zhiting and Megan jointly own the integration testbench — a seam nobody owns is a seam
> nobody tests.

**"Approve `fecim_pkg.sv`"**
> Single source of truth for parameters, types, and encodings. Carries elaboration
> assertions on the 9-bit multiplier constraint and power-of-two geometry. Future changes
> need review from all four, enforced via CODEOWNERS.

---

## Bucket 2 — Setup and Admin

| Task | Owner | Due | Priority | Labels |
|---|---|---|---|---|
| Order CP2102 dongles (×2) | Zhiting | Wed 30 Sep | Urgent | Gate, External |
| Confirm public repo permitted | Ethan | Thu 1 Oct | Important | External |
| Set weekly meeting time | Ethan | Wed 30 Sep | Important | M0 evidence |
| Repo protection, CODEOWNERS, CI lint | Ethan | Fri 2 Oct | Important | Setup, Gate |
| Toolchain installed — Zhiting | Zhiting | Fri 2 Oct | Important | Setup |
| Toolchain installed — Megan | Megan | Fri 2 Oct | Important | Setup |
| Toolchain installed — Ethan | Ethan | Fri 2 Oct | Important | Setup |
| Toolchain installed — Natalie | Natalie | Fri 2 Oct | Important | Setup |
| First commit — all four | one task each | Fri 2 Oct | Important | M0 evidence |
| Assign FeFET literature survey | Ethan (w/ Natalie) | Fri 2 Oct | Medium | — |
| First meeting log committed | Natalie | Wed 30 Sep | Medium | M0 evidence |

Toolchain is split into four tasks deliberately — one shared task with four assignees
shows Completed when one person finishes, which hides the three who haven't.

**"Order CP2102 dongles"**
> Two, not one. The board has no USB-UART bridge and week 3 is a hard gate — a shipping
> delay or dead unit stalls the critical path. **Must be 3.3 V capable.** Many ship
> jumpered for 5 V, which will damage a MAX 10 I/O pin.

**"Toolchain installed"** checklist (same for each person):
- [ ] Quartus Prime Lite (version pinned in README)
- [ ] Verilator
- [ ] Python 3.11
- [ ] C++17 compiler (MSVC Build Tools on Windows)
- [ ] Repo cloned, CI passing locally

**"Assign FeFET literature survey"**
> Two to three papers per person, each filed as its own task. Goal is complete parameter
> sets — `QUANT_LEVELS`, `D2D_SIGMA`, `READ_SIGMA`, `STUCK_RATE` — from a single source
> where possible. Starting now, not week 7: it's a search with uncertain duration and no
> hardware dependency, and if only one usable set exists we need to know while the
> semester-2 plan can still change.

---

## Bucket 3 — Lane A · Zhiting Li

| Task | Due | Priority | Labels |
|---|---|---|---|
| Quartus project builds clean | Fri 2 Oct | Important | Setup |
| Pin assignments committed | Mon 5 Oct | Important | — |
| Blink bitstream on hardware | Tue 6 Oct | Important | Verify |
| HEX counter (active-low segments) | Tue 6 Oct | Medium | Verify |

**"Pin assignments committed"**
> Verified against DE10-Lite User Manual v1.6:
> `clk` = PIN_P11 · `rxd` = PIN_AB5 (ARDUINO_IO[0]) · `txd` = PIN_AB6 (ARDUINO_IO[1])
> `KEY0` = PIN_B8 · `KEY1` = PIN_A7 · all 3.3-V LVTTL
> Enable weak pull-up on `rxd` — a floating line causes spurious start bits.

**"HEX counter"**
> Displays are **common anode, active-low** — driving high turns a segment off. Written
> the natural way you get a photographic negative. Eight bits per display; bit 7 is the
> decimal point.
> `KEY0`/`KEY1` are already hardware-debounced with Schmitt triggers — no debounce logic
> needed, but still synchronize for metastability.

---

## Bucket 4 — Lane B · Megan Mendez

| Task | Due | Priority | Labels |
|---|---|---|---|
| Commit `fecim_pkg.sv` | Thu 1 Oct | Urgent | Gate |
| **Confirm 9×9 DSP packing** | Mon 5 Oct | Urgent | Verify |
| Confirm 1024×8 dual-port infers M9K | Tue 6 Oct | Important | Verify |

**"Confirm 9×9 DSP packing"**
> **The riskiest assumption in the design, validated in week 1 instead of week 8.**
> Write two 9×9 signed multiplies, synthesize, read the fitter report. They must share one
> embedded multiplier block — look for `Embedded Multiplier 9-bit elements`. Two blocks
> means pairing failed; check operand widths and signedness before anything else.
> Why it matters: the MAX 10 has 144 blocks, each either one 18×18 or two 9×9. At 64 lanes
> this is 50% versus 100% device utilization. Every multiply is signed × signed so pairing
> is always legal — do not "optimize" any to unsigned.

Checklist:
- [ ] Two 9×9 signed multiplies written
- [ ] Synthesized
- [ ] Fitter report shows one block, not two
- [ ] Report committed to `quartus/reports/`

---

## Bucket 5 — Lane C · Ethan Ruddell

| Task | Due | Priority | Labels |
|---|---|---|---|
| C++ skeleton builds under CMake | Fri 2 Oct | Important | Setup |
| Quantization function + unit tests | Mon 5 Oct | Important | — |
| CI: lint + model tests green | Mon 5 Oct | Important | Gate, Setup |
| Verilate a trivial module | Tue 6 Oct | Medium | Verify |

**"Quantization function + unit tests"**
> `model_exact` must be **integer throughout** — no floating point in the datapath or it
> will never match the RTL bit for bit. Test N ∈ {2, 3, 5, 8, 255}; the non-powers of two
> exercise the reciprocal path that powers of two would hide.

**"CI: lint + model tests green"**
> Verilator with `-Wall`. Width-mismatch warnings are how you catch a 32-bit accumulator
> silently truncated somewhere — a bug producing plausible small numbers that survives
> casual testing.

---

## Bucket 6 — Lane D · Natalie Poche

| Task | Due | Priority | Labels |
|---|---|---|---|
| `pip install -e` works | Fri 2 Oct | Important | Setup |
| Packet encoder against frozen protocol | Mon 5 Oct | Important | — |
| Golden fixtures in `tb/golden/` | Tue 6 Oct | Important | Gate, Verify |

**"Golden fixtures"** — co-owned with Ethan
> Hand-computed byte sequences with hand-computed CRCs, committed as files. Both the C++
> testbench and the Python driver tests assert against the **same file**.
> This is what catches endianness or encoding drift on day one rather than in week 4,
> where it shows up as plausible-looking garbage rather than an obvious failure.

---

## Bucket 7 — Week 2 (parked)

Create it now, leave it mostly empty — somewhere to put things that surface during week 1
without cluttering the active board.

| Task | Owner | Due |
|---|---|---|
| UART RX/TX in simulation | Zhiting | Week 2 |
| Single `mac_lane`, clean datapath | Megan | Week 2 |
| `model_ideal` clean MVM | Ethan | Week 2 |
| Encoder tested against loopback fake | Natalie | Week 2 |

---

## Conventions to state at the kickoff

**Every task has exactly one assignee.** Shared work gets a primary owner plus the others
as checklist entries or in the notes. An unassigned task is nobody's.

**Paste the PR URL into task comments.** Planner doesn't link to GitHub. M0 measures
contribution against third-party logs, and a board that diverges from the commit history
is worse than no board.

**Week-1 review Wed 7 Oct.** Anything not Completed gets re-dated with a reason in the
comments, not silently carried.

**Red Gate tasks come first.** If you're blocked on your own work, clear a gate rather
than starting week-2 items early.
