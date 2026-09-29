# FeCIM Cross-Document Consistency Audit

**Date:** week 1
**Scope:** all 22 design documents plus `fecim_pkg.sv`
**Method:** mechanical grep for shared constants, formulas, and command/register tables,
followed by manual reading of every cross-reference.

**Result: one blocking hole, five stale values, six open decisions closed.**

---

## 1. Blocking — fixed in this pass

### F1. `atten_rom` had no write mechanism

`mac-array-spec.md` §8 states `atten_rom` is "host-writable." Nothing in `protocol.md`,
`fecim_pkg.sv`, or any other document provided a way to write it. A 32-bit config register
cannot carry a 128-entry table, and no bulk target existed for it.

IR drop would have been unusable — a whole `NOISE_EN` bit with no path to configure it,
discoverable only when someone tried to run that sweep in week 9.

**Fixed:** `protocol.md` rev 3 adds `WRITE_ATTEN` (`0x09`) and `TGT_ATTEN = 2`, reusing the
existing bulk streaming path. `fecim_pkg.sv` rev 3 adds both enums. Reset default is all
`255` (unity), so the host need not write it unless `NOISE_EN[4]` is set.

**Follow-on required:** `host-driver-spec.md` needs a `set_ir_drop(coeffs)` API entry, and
`mac-array-spec.md` §8 should reference `TGT_ATTEN` rather than saying "host-writable"
without a mechanism.

---

## 2. Stale values — edits required

These are documents that disagree with the current contract. Each is a one-line fix.

### F2. `mac-array-spec.md` §9 — ADC shift formula is wrong

```systemverilog
assign sh = ACC_W - adc_bits;          // WRONG — ACC_W is the container width
```

Must be:

```systemverilog
assign sh = ACC_USED_W - adc_bits;     // 24 at TILE_ROWS = 128
```

`ACC_W` is 32 but the accumulator never exceeds ±8,290,560 (24 signed bits). Using the
container width places full scale at ±2³¹, so at 8-bit ADC every result collapses into one
or two codes. The ADC would appear catastrophically destructive at every setting and
suspicion would fall on the rounding logic.

Found by `cell-physics-derivation.md` §6.1, already correct in `protocol.md` §6.5 and
`host-driver-spec.md` §6.4. `ACC_USED_W` is now a package parameter with an assertion.

**Also add** the verification case: `adc_bits == ACC_USED_W` must be an exact no-op.

### F3. `mac-array-spec.md` §5.3 — `READ_SIGMA` scaling described inconsistently

The code shifts by 9; the comment says Q1.8, which implies 8. A factor of two.

**Keep the shift at 9** — it places maximum noise 3.4σ from the clamp rather than 1.7σ,
avoiding clip distortion at high settings. **Fix the prose:** `READ_SIGMA` is
`sigma9/512`, not Q1.8, giving a range of 0 to 0.498 and σ_max ≈ 36.8 LSB.

`hardware-verification-errata.md` §2.3 repeats the same error and needs the same fix.
`protocol.md` §6.3 (`0.144 × reg`) and `cell-physics-derivation.md` §4.3 already assume
shift 9 and are correct.

This one matters beyond documentation: if `model_exact` and the RTL pick different halves
of the contradiction, L1 equivalence fails with no obvious cause.

### F4. `packet-parser-spec.md` §3 — command table is stale

Two problems:
- `READ_RESULT` response listed as `count × 3` bytes. Correct is **`count × 4`** — results
  became 32-bit when `ACC_W` changed from 24.
- Missing `READ_ARGMAX` (`0x08`) and now `WRITE_ATTEN` (`0x09`).

### F5. `control-fsm-spec.md` — register map and display

- Register map omits `QUANT_MULT` (`0x0C`) and the reserved range `0x10`–`0x1F`.
- §8 does not state that the HEX displays are **active-low** and 8 bits wide. Listed in
  `hardware-verification-errata.md` §3.1 but never applied here.
- §8 should note `KEY0`/`KEY1` are hardware-debounced.

### F6. Geometry examples predate the 128×128 / 64-lane target

`control-fsm-spec.md`, `activation-buffer-spec.md`, `packet-parser-spec.md`, and
`result-buffer-spec.md` use 64×64 and 32 lanes in worked examples and cycle counts.

**Not a correctness problem** — every spec is parameterized and the formulas are right. But
label those sections as the **week-6 MVP configuration** so nobody reads 200 cycles /
4.0 µs as the semester-1 figure. The semester-1 target is 392 cycles / 7.8 µs.

---

## 3. Open decisions — now closed

The six items left open in `protocol.md` rev 2 §6, resolved in rev 3.

| Decision | Resolution | Rationale |
|---|---|---|
| How is `atten_rom` written? | `TGT_ATTEN = 2` bulk target, `WRITE_ATTEN` = `0x09` | Reuses the streaming path; an indexed register pair would need two writes per coefficient |
| Reserved register space | `0x10`–`0x1F` | Retention, per-column gain/offset, differential-pair enable, independent seeds |
| Unimplemented address | `ST_ADDR_RANGE` | A silent zero hides a typo in a register constant |
| Write to read-only register | `ST_ADDR_RANGE` | Same code, no new status value needed |
| `ADC_BITS` vs `ADC_SHIFT` | Keep `ADC_BITS`; host derives shift from `ACC_USED_W` | Simpler for the host; `ACC_USED_W` is derivable from `TILE_ROWS` in `IDENTIFY` |
| Must `active_cols` divide `NUM_LANES`? | No — sequencer rounds up, argmax masks | Already required by the argmax guard in `result-buffer-spec.md` §5.1 |

---

## 4. Verified consistent

Checked and in agreement across every document that mentions them:

| Item | Value | Documents |
|---|---|---|
| `LANE_PIPE_DEPTH` | 3 | pkg, activation-buffer §4 (authoritative table), control-fsm, mac-array, top-level |
| `ACC_W` | 32 | pkg, mac-array §3, result-buffer §2, protocol §4.4 |
| Noise clamp | ±127 | pkg, mac-array §5.4, cell-physics §4.3 |
| `w_n` width | 9 signed | pkg, mac-array §2, errata §2.3 |
| `MAX_PAYLOAD` | 512 | pkg, protocol §3.2, packet-parser §2.2 |
| CRC-8 poly / init | `0x07` / `0x00` | pkg, protocol §3.1, packet-parser §6 |
| Sync bytes | `0xA5` / `0x5A` | pkg, protocol §3, packet-parser §2 |
| Parser timeout | 100 ms | pkg, protocol §3.2, packet-parser §5.1 |
| `IDENTIFY` length | 16 bytes | protocol §4.5, packet-parser §3.1 |
| `READ_ARGMAX` length | 10 bytes | protocol §4.6, result-buffer §6.1 |
| Status codes | `0x00`–`0x05` | pkg, protocol §5, packet-parser §7.2 |
| Multiplier budget | 72 of 144 | errata §2.5, mac-array §11, top-level §9, plan §3.8 |
| σ conversions | 2.450 / 6.93 | protocol §6.3, cell-physics §4, device-model §5, host-driver §6.1 |
| Cell address | `col × TILE_ROWS + row` | write-path §5, protocol §4.1 |
| Argmax tie rule | lowest index | protocol §4.6, result-buffer §5.1, application §4.4 |
| Pin assignments | AB5 / AB6 / P11 / B8 / A7 | uart §2.3, top-level §4, hardware §3 |

Address decode chains verified end to end: host weight index → `bulk_addr` → lane select
and local address → `seq_cnt` → M9K address. All wire slices, all consistent, and all
dependent on `TILE_ROWS` and `NUM_LANES` being powers of two — which the package now
asserts at elaboration.

---

## 5. Document dependency map

Read order for someone new, and what breaks if a document changes.

```
protocol.md  +  fecim_pkg.sv          ← THE CONTRACT. Changing either
     │                                   invalidates work in 3 lanes.
     ├──────────────┬──────────────┬──────────────┐
     ▼              ▼              ▼              ▼
 Lane A          Lane B         Lane C         Lane D
 uart            write-path     model-verif    host-driver
 packet-parser   activation                    application
 control-fsm     mac-array
 top-level       result-buffer
     │              │              │              │
     └──────────────┴──────────────┴──────────────┘
                    │
     cell-physics-derivation ──► device-model
     (conversion math)          (chosen values + provenance)

 hardware-verification-errata ← why the 9-bit rule and active-low HEX exist
 hardware.md                  ← replication, M0 requirement
 fecim-plan.md                ← schedule, lanes, risk
 repo-setup.md                ← structure, CI, conventions
```

**Single-writer rule:** a constant appears normatively in exactly one place. `fecim_pkg.sv`
for anything RTL uses; `protocol.md` for anything crossing the wire. Every other document
**references** rather than restating. Where a spec currently restates a value, that is the
mechanism by which F2–F5 happened.

---

## 6. Edit checklist

Apply before RTL work begins in earnest.

- [ ] `mac-array-spec.md` §9: `ACC_W` → `ACC_USED_W` in the ADC shift **(F2, blocking)**
- [ ] `mac-array-spec.md` §9: add the `adc_bits == ACC_USED_W` no-op test case
- [ ] `mac-array-spec.md` §5.3: correct the Q1.8 prose to `sigma9/512` **(F3, blocking)**
- [ ] `mac-array-spec.md` §8: reference `TGT_ATTEN` for coefficient loading
- [ ] `hardware-verification-errata.md` §2.3: same Q1.8 correction **(F3)**
- [ ] `packet-parser-spec.md` §3: `count × 3` → `count × 4`; add `0x08`, `0x09` **(F4)**
- [ ] `control-fsm-spec.md` §6: add `QUANT_MULT`, reserved range, access policy **(F5)**
- [ ] `control-fsm-spec.md` §8: HEX active-low, 8 bits, buttons pre-debounced **(F5)**
- [ ] `control-fsm-spec.md`, `activation-buffer-spec.md`, `result-buffer-spec.md`: label
      64×64 / 32-lane figures as the week-6 MVP configuration **(F6)**
- [ ] `host-driver-spec.md` §6: add `set_ir_drop(coeffs)` **(F1 follow-on)**
- [ ] `control-fsm-spec.md`: handle `TGT_ATTEN` in the bulk write decode **(F1 follow-on)**

---

## 7. Standing risk

**How F1 happened, and how to stop the next one.** `mac-array-spec.md` asserted a
capability ("host-writable") that no other document implemented. Nothing detected it because
no document owned the question "can every configurable thing actually be configured?"

Two cheap defenses:

**Trace every `NOISE_EN` bit to a write path.** Six bits; each needs at least one register
or bulk target that can set its parameters. Run that check whenever a bit is added — it is
the exact check that would have caught F1 in week 1.

**Assert numeric relationships in the package rather than in prose.** `ACC_USED_W ≤ ACC_W`
and `QUANT_LEVELS_MAX ≤ 255` are now elaboration assertions, so a future widening fails at
compile time. Prose constraints drift; assertions do not.
