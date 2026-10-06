# FeCIM Literature Survey — Extraction Notes

**Owner:** Lane C (Ethan Ruddell), with Lane D (Natalie Poche) on parameter sets
**Feeds:** `device-model.md` (chosen values), `sw/fecim/parameter_sets/*.yaml` (the values
themselves), `cell-physics-derivation.md`, `model-verification-design.md`, R1 prior art
**Status:** in progress — week 2. §2 extracted.

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

## 2. Dual-port FeFET variability (parameter Set A)

S. Chatterjee, S. Thomann, K. Ni, Y. S. Chauhan, and H. Amrouch, "Comprehensive variability
analysis in dual-port FeFET for reliable multi-level-cell storage," *IEEE Trans. Electron
Devices*, vol. 69, no. 9, pp. 5316–5323, Sep. 2022, doi: 10.1109/TED.2022.3192808.

Parameter-set file: `sw/fecim/parameter_sets/dualport_fefet_mlc.yaml`.

**Read this first: the values are TCAD simulation, not measurement.** Sentaurus TCAD,
calibrated to a measured 22 nm FDSOI transistor and to measured Q–V data from an MFM
capacitor (their ref. [23]); §II. Variation is modelled as random placement of polarized
domains (5 nm domains, 400 per device) plus conventional RDF, WFV, and LER via the impedance
field method, combined by adding variances (Fig. 6). The only measured variability shown is
Fig. 3, reproduced from Jiang et al., VLSI 2022 (their ref. [14]), on 500 × 500 nm devices.
**That reference is the candidate for a measured version of this set.**

**What it does not give us:** cycle-to-cycle read noise. §II, last paragraph: the framework
captures static variation only; stochastic switching and endurance-cycle variation are
excluded. So it supplies `D2D_SIGMA` and `QUANT_LEVELS` but not `READ_SIGMA`. The earlier
expectation that it reports both sigmas was wrong.

### 2.1 Extracted values

Raw quantities. Device: asymmetric double-gate (dual-port) HfO2 FeFET, Lg = Wg = 100 nm;
write on the front gate (FG), read on either FG or back gate (BG).

| Quantity | FG, t_FE 10 nm | BG, t_FE 10 nm | FG, t_FE 3 nm | **BG, t_FE 3 nm (Set A)** | Location |
|---|---|---|---|---|---|
| Write voltage | ±4 V | ±4 V | ±1.8 V | **±1.8 V** | §II; Figs. 4, 10 |
| Memory window | 1.8 V | 18.5 V | 0.22 V | **2.7 V** | Fig. 4; Fig. 10, §III-B text |
| σ_HVT, conventional sources only | 18.75 mV | 45.62 mV | 18.95 mV | **48.15 mV** | Fig. 8; Fig. 12 |
| σ_LVT, conventional sources only | 19.65 mV | 106.05 mV | 19.20 mV | **78.75 mV** | Fig. 8; Fig. 12 |
| Total σ, worst state | ≈ 45 mV (50 %) | ≈ 0.52 V (70 %) | ≈ 20 mV | **78.75 mV (LVT end)** | Figs. 9b–c, 13b–c, 14; Set A from Fig. 12b, see §2.2 |
| Max σ / MW | ≈ 0.025 | ≈ 0.028 | ≈ 0.095 | **≈ 0.03** | Figs. 9d, 13d; §III-B text for BG 3 nm |
| States at P(error) < 1 % | 8 (3 bit) | 8 (3 bit) | 2 (1 bit) | **8 (3 bit)** | §III-C, Fig. 15 |
| Cycle-to-cycle / read σ | — | — | — | **not reported** | §II |
| Stuck / failed cells | — | — | — | **not reported** | — |

Values marked ≈ are read off plots. Every Set A value in the YAML is a stated number:
MW 2.7 V (§III-B text, Fig. 10b), 8 states (§III-C, Fig. 15d), σ_LVT 78.75 mV (Fig. 12b).

**Why BG at 3 nm for Set A:** it is the paper's headline configuration, it has the most
complete numbers in the text itself, and its σ/MW is nearly flat across states (Fig. 13d),
so a single `D2D_SIGMA` describes it well. At 10 nm the variability is strongly
state-dependent (bell-shaped σ/MW in Fig. 9d), which a single σ represents less faithfully.

### 2.2 Conditions and mapping notes

- **σ is per state, not pooled.** It peaks at 50 % (FG) or 70 % (BG) switched domains at
  10 nm; at 3 nm σ/MW rises monotonically from ≈ 0.02 to ≈ 0.03 toward 100 % P_FE+
  (Fig. 13d). FeCIM has one `D2D_SIGMA` for all levels, so the YAML uses the **worst state**
  — conservative.
- **Why 78.75 mV rather than 0.03 × MW = 81 mV.** At 3 nm the worst state is the fully
  switched (100 % P_FE+, lowest V_TH) end — Fig. 13d is monotonic and Fig. 13a puts the lowest
  mean V_TH there. §III-A states end states have no domain-placement term, so total σ there is
  the conventional σ_LVT = 78.75 mV (Fig. 12b). That is a stated value, not a ratio read as
  "about 0.03", so the confidence is `direct`. Check: 78.75 / 2700 = 0.029.
- **Subscript typo in §II.** One sentence reads "variations at HVT (σ_LVT) and LVT (σ_HVT)".
  The LVT assignment above follows the figures, not that sentence.
- **Levels are a lower bound.** §III-C only evaluates 2, 4, 8, and 16 states; 8 meets
  P(error) < 1 %, 16 does not (Fig. 15d). The true maximum lies between 8 and 15.
  `QUANT_LEVELS` accepts any N (`write-path-spec.md` §4.2); 8 understates the device.
- **Distribution shape:** the paper assumes Gaussian V_TH distributions for its error
  analysis (§III-C). FeCIM's write-path variation is triangular (`device-model.md` §8). Same
  σ, different tails — note it when comparing accuracy against their 3-bit result.
- **Dominant source differs by configuration:** domain randomness dominates at 10 nm;
  conventional sources (RDF for BG read) dominate at 3 nm (§III-B). Worth one sentence in the
  report, because it is why σ/MW flattens.
- **MW and σ come from the same simulated device.** That satisfies the single-source rule,
  but the confidence column must say simulated. Consider adding a `basis: measured |
  simulated` field to the `device-model.md` §4 schema so this cannot be lost.

### 2.3 Sanity check after conversion

Formulas: `cell-physics-derivation.md` §4. Only σ/MW matters, because MW cancels.

- σ_w (D2D) = 255 × 0.07875 / 2.7 = **7.44 LSB** → `D2D_SIGMA` = round(2.450 × 7.44) = **18**
  (well within 0–255)
- `QUANT_LEVELS` = 8 → `QUANT_MULT` = round(255 × 256 / 7) = **9326**
- `READ_SIGMA`: no source value; sweep.
- For scale: the 10 nm BG worst state (σ/MW ≈ 0.028) gives σ_w ≈ 7.1 LSB, almost the same.
  The paper's own conclusion — larger MW brings proportionally larger variation — shows up
  directly as a near-constant `D2D_SIGMA`.

### 2.4 Is this paper representative? Cross-check against other literature

The register value depends only on σ/MW and the level count, so those are what to compare.
Sources found in the week-2 search; locations are as reported by each paper.

**Device-to-device σ, normalized to memory window**

| Source | Basis | Device | MW | σ_Vth (D2D) | σ/MW | → `D2D_SIGMA` |
|---|---|---|---|---|---|---|
| **Set A** — Chatterjee et al., TED 2022 | TCAD | dual-port, 100 × 100 nm, BG read, t_FE 3 nm | 2.7 V | 78.75 mV | 0.029 | 18 |
| Same paper, other configurations | TCAD | FG/BG read, t_FE 10 nm | 1.8 / 18.5 V | worst-state, Figs. 9b–c | ≈ 0.025 / 0.028 | 16 / 17 |
| Soliman et al., *Nat. Commun.* 14, 6348 (2023) | **measured**, 32 × 32 array | 28 nm HKMG, 450 × 450 nm cell, 8–10 nm HfO2 | ≥ 1.2 V (4 target V_TH spanning 0.2–1.4 V, Fig. 5) | ≤ 38 mV after write-verify (Results) | ≤ 0.032 | ≤ 20 |
| Duan et al., IEDM 2024 | measured MW; σ **swept** in simulation | 28 nm, 1FeFET-1C, 128 × 128 | 2.5 V (Fig. 2f) | 0.05–0.3 V swept (Fig. 9b–c) | 0.02–0.12 | 12–75 |
| De et al., *Front. Nanotechnol.* 2022; arXiv:2008.10363 | measured, calibrated model | HZO Fe-FinFET, L ≈ 40–70 nm | not stated | 578 mV, Gaussian pooled over all states | — | not comparable |
| Manna et al. (Ni group), arXiv:2312.15444 | measured | 28 nm HKMG, W/L 1/1 down to 0.24/0.24 µm | — | "D2D increases drastically with device scaling" (Fig. 2b, graphical) | — | trend only |

**Levels**

| Source | Levels | Criterion |
|---|---|---|
| **Set A** — Chatterjee et al. | ≥ 8 | P(error) < 1 % between Gaussian states (simulated) |
| Soliman et al. 2023 | 4 | demonstrated in a measured array, with write-verify |
| Duan et al. 2024 | 4 | demonstrated MLC (Fig. 2g) |
| De et al. 2022 (Fe-FinFET) | 26–50 per device, 27 in arrays | analog synaptic states for training, not a reliable-read criterion |
| Manna et al. | ~100 programmable | 20 mV programming steps, not distinguishable states |

**Read / cycle-to-cycle noise** (Set A has none)

| Source | Basis | Value |
|---|---|---|
| De et al., arXiv:2008.10363 | measured | C2C σ_Vth = 17.7 mV (during read and write over training) |
| De et al., arXiv:2103.13302 (Fe-FinFET) | measured | flicker noise σ_Id/I_d ≈ 0.7 %; C2C σ_Id ≈ 1.2 % |

**Verdict**

- **D2D σ: representative.** Set A's σ/MW (0.029) agrees within about 10 % with the only
  measured array value found (Soliman, ≤ 0.032), and with the same paper's other
  configurations (0.025–0.028). The two sources disagree on method — TCAD versus measured
  with write-verify — and still land within 2 `D2D_SIGMA` codes of each other.
- **But it represents ~100 nm-class devices or write-verified cells only.** Nanoscale FinFETs
  and unverified scaled devices are far worse (De: 578 mV pooled; Manna: drastic increase with
  scaling). The `D2D_SIGMA` sweep must therefore extend well above 18 — at least to the
  σ/MW ≈ 0.12 that Duan et al. sweep (`D2D_SIGMA` ≈ 75).
- **Levels: plausible, on the optimistic side of measured arrays.** Measured arrays demonstrate 4
  levels; Set A's 8 is a simulated reliability limit, consistent with the 4-level demos being
  conservative choices rather than physical limits. Sweep N rather than leaning on 8.
- **Read noise: still unsourced for Set A.** A measured C2C of ≈ 17.7 mV (De) would map to
  `READ_SIGMA` ≈ 12 at MW 2.7 V, but it comes from a different device. It informs the **sweep
  range** only; putting it in Set A would make the set composite (`device-model.md` §2).

**Strongest next sources:** Soliman et al. 2023 as **Set B** (measured; need the MW and
per-level σ from the full paper's figures) and Jiang et al., VLSI 2022 (measured dual-port
counterpart to Set A, the paper's ref. [14]).

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
| Chatterjee et al., TED 2022 (§2) | 22 nm FDSOI, TCAD | yes | yes | no | yes (≥ 8) | Set A — simulated; C2C missing |
| Soliman et al., *Nat. Commun.* 2023 | 28 nm HKMG, measured array | partial (V_TH targets) | yes (≤ 38 mV) | no | yes (4) | **Set B candidate** — extract MW and per-level σ from figures |
| De et al., *Front. Nanotechnol.* 2022 | HZO Fe-FinFET, measured | ? | pooled only | yes (17.7 mV) | analog | read-noise sweep range only |
| Jiang et al., VLSI 2022 (their ref. [14]) | measured dual-port FeFET | ? | yes (Fig. 3 of §2 paper) | ? | ? | check — measured counterpart to Set A |
| | | | | | | |
| | | | | | | |
