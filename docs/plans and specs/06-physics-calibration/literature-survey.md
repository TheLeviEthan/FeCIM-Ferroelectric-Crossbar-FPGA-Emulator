# FeCIM Literature Survey — Extraction Notes

**Owner:** Lane C (Ethan Ruddell), with Lane D (Natalie Poche) on parameter sets
**Feeds:** `device-model.md` (chosen values), `sw/fecim/parameter_sets/*.yaml` (the values
themselves), `cell-physics-derivation.md`, `model-verification-design.md`, R1 prior art
**Status:** in progress — week 2

---

## 0. How to use this file

This is the **working record** of what each paper says and where it says it. It is not where
numbers are kept.

- **Numeric device parameters go in a YAML file** in `sw/fecim/parameter_sets/`, one per
  source, in the schema from `device-model.md` §4. That file is the single normative copy;
  this document only records where each value came from.
- **Record raw quantities, not register values.** Memory window in volts, σ in mV. The
  driver applies the conversions in `cell-physics-derivation.md` §4. A pre-converted register
  value silently goes stale if a conversion ever changes.
- **Every claim gets a location** — page, figure, table, or equation. "Fig. 4b" is enough;
  "somewhere in §3" is not. A reviewer checking provenance should never have to reread a paper.
- **Label confidence** with the `device-model.md` §3 vocabulary: `direct`, `derived`,
  `estimated`, `design`, `swept`.
- **Quote sparingly.** Paraphrase and cite the location; keep direct quotes to a sentence
  where exact wording matters (e.g. how a paper positions itself).

---

## 1. NeuroSim V1.5

J. Read, M.-Y. Lee, W.-H. Huang, Y.-C. Luo, A. Lu, and S. Yu, "NeuroSim V1.5: Improved
software backbone for benchmarking compute-in-memory accelerators with device and
circuit-level non-idealities," arXiv:2505.02314, May 2025.
https://arxiv.org/abs/2505.02314 · open access

**Why it matters:** the closest software tool to what FeCIM does. The R1 prior-art section and
the "why not just use NeuroSim" question both depend on this section.

### 1.1 How they parameterize non-idealities

| Non-ideality | Their parameter(s) and units | Where in paper | Our equivalent register | Notes |
|---|---|---|---|---|
| Conductance levels / quantization | | | `QUANT_LEVELS` | |
| Device-to-device variation | | | `D2D_SIGMA` | |
| Cycle-to-cycle / read noise | | | `READ_SIGMA` | |
| Stuck-at faults | | | `STUCK_RATE` | |
| IR drop / wire resistance | | | `atten_rom` | |
| ADC precision | | | `ADC_BITS` | |
| Retention / drift | | | reserved `0x10`–`0x1F` (sem. 2) | |

Questions to answer: Do they specify variation as σ of conductance, of V_th, or of weight? Is
it Gaussian? Is noise applied per read or per inference? Can effects be enabled one at a time?

### 1.2 What they model that FeCIM does not

`device-model.md` §8 already lists these as known gaps. Record the specifics here, then add the
citation there.

- **I–V nonlinearity:** how modelled, which read scheme assumed (location: ___)
- **Bit-serial readout:** bits per cycle, how partial sums are combined, effect on accuracy
  versus single-conversion at the same ADC bits (location: ___)
- **Anything else** they model that we idealize (sense amp offset, peripheral energy/area,
  ...):

### 1.3 Positioning — "why not just use NeuroSim"

- How they describe their own purpose and scope (quote, location):
- Simulation speed / scale they report (location):
- What they do **not** claim (hardware timing, real-time operation, bit-exact hardware
  reference, ...):
- **Draft framing for FeCIM** (one or two sentences, to reuse in R1 and the final report):

---

## 2. Dual-port FeFET variability (parameter Set A candidate)

*Comprehensive Variability Analysis in Dual-Port FeFET for Reliable Multi-Level-Cell Storage.*
Authors, venue, year, DOI: **TODO — take from the publisher page, not ResearchGate.**
ResearchGate entry: https://www.researchgate.net/publication/361925449

**Why it matters:** it reports both variability terms *and* the memory window from one
process, which is exactly what the single-source provenance rule (`device-model.md` §2)
needs. Target parameter-set file: `sw/fecim/parameter_sets/dualport_fefet_mlc.yaml`.

### 2.1 Extracted values

Raw quantities only. Copy each into the YAML once it is filled in and checked.

| Quantity | Value | Units | Location in paper | YAML key | Confidence |
|---|---|---|---|---|---|
| Memory window | | V | | `memory_window_v` | |
| Distinguishable MLC levels | | count | | `quant_levels` | |
| Device-to-device V_th spread (σ) | | mV | | `sigma_vth_mv` | |
| Cycle-to-cycle / read V_th fluctuation (σ) | | mV | | `read_sigma_vth_mv` | |
| Stuck / failed-cell fraction | | fraction | | `stuck_rate` | `swept` if unreported |
| Retention (sem. 2) | | | | `retention_decay` | |

### 2.2 Conditions to record

These go in the YAML `notes` and `process` fields. Without them the values cannot be
compared across sets.

- Process / node / ferroelectric stack:
- Device dimensions:
- Number of devices measured (for σ) and number of cycles (for read noise):
- Program/erase pulse scheme:
- Read conditions (V_GS, V_DS, which port is read):
- Temperature:
- **Is σ reported per level or pooled across levels?** If per level, record each and note
  which one goes in the YAML.
- **Dual-port specifics:** is the V_th that is reported the one seen by the read port? Does the
  separate write port change how C2C noise should map to `READ_SIGMA`?

### 2.3 Sanity check after conversion

Fill in once the values are in. Formulas: `cell-physics-derivation.md` §4.

- σ_w (D2D) = 255 × σ_Vth / MW = ___ LSB → `D2D_SIGMA` ≈ 2.450 × σ_w = ___ (must be ≤ 255)
- σ_w (read) = 255 × σ_Vth,rms / MW = ___ LSB → `READ_SIGMA` ≈ 6.93 × σ_w = ___ (must be ≤ 255)
- If either exceeds 255, the paper's spread is larger than the emulator can represent. Record
  that here rather than letting the driver clamp it silently.

---

## 3. Accurate emulation of memristive crossbar arrays

*Accurate Emulation of Memristive Crossbar Arrays for In-Memory Computing.* arXiv:2004.03073.
https://arxiv.org/abs/2004.03073 · open access. Authors and venue: **TODO.**

**Why it matters:** the feasibility argument (`07-coursework/feasibility-response-cited.md`,
reference [1]) rests on this paper. That reference summarizes it as a PCM emulator on a
Kintex UltraScale FPGA, capturing conductance drift and 1/f noise, validated against a
~400,000-device PCM prototype. **Verify each of those claims against the paper and note the
location.**

### 3.1 How they validated against silicon

- What hardware they compared to, and how many devices:
- What they compared (distributions, inference accuracy, per-device traces):
- Agreement they report, and the metric used (location):

### 3.2 What they modelled versus idealized

| Effect | Modelled? | How | Location |
|---|---|---|---|
| Programming noise / D2D | | | |
| Read noise (1/f) | | | |
| Conductance drift | | | |
| IR drop | | | |
| ADC / peripheral | | | |
| Nonlinearity | | | |

### 3.3 How they positioned the work

- Stated purpose (quote, location):
- Claimed advantage over software simulation:

### 3.4 How FeCIM differs

Fill in once §3.1–3.3 are done. The obvious axes to check:

- **Device technology:** PCM versus FeFET — different physics, different dominant
  non-idealities.
- **Validation target:** they validated against their own silicon; FeCIM has no in-house
  silicon, so it validates RTL ≡ model bit-exactly (L0–L2, L4) and model ≡ published
  statistics (L3). State this difference plainly; it is the main thing a reviewer will ask.
- **Platform and cost:** their FPGA versus a DE10-Lite MAX 10.
- **Per-effect isolation:** can they switch individual non-idealities on and off?
- **Openness / reproducibility:** is their emulator available?

---

## 4. Candidate sources for Sets B and C

`device-model.md` §7 needs three complete sets from distinct processes. A source is usable if
it reports most of the set, states conditions, and gives spread, not just a mean.

| Paper | Process | Reports MW? | D2D σ? | C2C σ? | Levels? | Usable? |
|---|---|---|---|---|---|---|
| Dual-port FeFET MLC (§2) | | | | | | Set A candidate |
| | | | | | | |
| | | | | | | |
