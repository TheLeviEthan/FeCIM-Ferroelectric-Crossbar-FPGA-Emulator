# FeCIM Device Model and Parameter Provenance

**Owner:** Lane C (Ethan Ruddell) and Lane D (Natalie Poche), co-signed
**Depends on:** `cell-physics-derivation.md` for the conversion mathematics
**Milestone:** first complete set week 7; three sets by week 23
**Status:** **skeleton — values are placeholders pending literature survey**

---

## 1. Purpose

Every register value that models a device property, with the measurement it came from, the
reasoning in between, and an honest confidence label.

`cell-physics-derivation.md` gives the mathematics that converts a measurement into a
register value. This document holds the **chosen values and where they came from**. The
working notes behind them — what each paper reports and on which page, figure, or table — are
in `literature-survey.md`. It is
the project's actual scientific contribution, and it is the table a skeptical reader will
check first.

---

## 2. The provenance rule

Every calibration rests on one load-bearing assumption, and naming it is what makes the
work credible.

Because parameters come from published FeFET characterization rather than in-house
measurement, the polarization-to-conductance bridge does not arise — published work reports
conductance or drain current directly. **That is a rigor improvement over what in-house
capacitor data would have permitted, and worth stating as such.**

The assumption that replaces it is **parameter-set provenance**: values from different
papers describe different devices, processes, nodes, and measurement conditions. Taking
`QUANT_LEVELS` from one source and `D2D_SIGMA` from another silently assumes they describe
the same device, which is often false and always invisible in the final number.

The rule:

- **Each complete parameter set traces to a single source where possible.** Name it.
- **Composite sets are labelled composite**, with each parameter's source listed, and are
  never presented as characterizing a real device.
- **A parameter with no usable published value is swept, not guessed** (§6).

---

## 3. Confidence labels

| Label | Meaning |
|---|---|
| `direct` | Reported explicitly in the cited source |
| `derived` | Computed from reported quantities via `cell-physics-derivation.md` |
| `estimated` | Not reported; taken from a different source or a plausible range |
| `design` | Not a device property — a circuit or architecture choice |
| `swept` | Deliberately not fixed; results presented as a band |

**Label honestly.** A reviewer who sees `estimated` next to one row trusts the `direct`
rows more, not less. Presenting every parameter with equal authority undersells the ones
that are genuinely well sourced.

---

## 4. Parameter set template

One YAML file per source in `sw/fecim/parameter_sets/`. Values below are **placeholders**.

**This section is the normative schema.** `application-spec.md` §4.6 and the
`parameter_sets/README.md` refer here rather than restating it. Confidence is recorded **per
parameter**, not once per file — a single paper routinely reports some values directly,
leaves others to be derived, and omits others entirely.

```yaml
name: <short_id>
source: "<authors>, <venue> <year>, doi:<...>"
process: "<node and integration, e.g. 28 nm HKMG>"
composite: false
notes: "<measurement conditions, caveats>"

memory_window_v:        null     # MW, volts
quant_levels:           null     # N distinguishable states
sigma_vth_mv:           null     # device-to-device V_th spread
read_sigma_vth_mv:      null     # cycle-to-cycle V_th fluctuation
stuck_rate:             null     # fraction of non-programmable cells
retention_decay:        null     # semester 2

confidence:
  memory_window_v:      direct
  quant_levels:         direct
  sigma_vth_mv:         direct
  read_sigma_vth_mv:    estimated
  stuck_rate:           swept
  retention_decay:      swept
```

Store **raw device quantities** (volts, mV, counts, fractions), never register values. The
driver converts at load time using §5, so a conversion change never leaves stale register
values in a data file.

**State whether a source is measured or simulated** in `notes`. TCAD-derived values are
legitimate inputs but must never be presented as measured device data.

`null` is meaningful: it forces the sweep path in §6 rather than permitting a silent guess.

---

## 5. Derivation chain, per parameter

Full mathematics in `cell-physics-derivation.md` §4. Summary:

| Register | Source quantity | Conversion | Typical confidence |
|---|---|---|---|
| `QUANT_LEVELS` | reported multilevel states | direct, N | `direct` |
| `QUANT_MULT` | — | `round(255·256/(N−1))` | exact arithmetic |
| `D2D_SIGMA` | $\sigma_{V_{th}}$, $MW$ | $2.450 \times 255\sigma_{V_{th}}/MW$ | `derived` |
| `READ_SIGMA` | $\sigma_{V_{th},rms}$, $MW$ | $6.93 \times 255\sigma_{V_{th}}/MW$ | `derived` |
| `STUCK_RATE` | yield / failed-bit data | $65536\,p_{stuck}$ | often `swept` |
| `atten_rom[i]` | wire resistance, array size | $256\,\alpha_i$ | `design` |
| `ADC_BITS` | converter choice | direct | `design` |
| retention factor | retention vs time | $256\,P_r(t)/P_r(0)$ | `direct` (sem. 2) |

The single identity that makes all of this work:

$$\Delta w\,[\text{LSB}] = \frac{255}{MW}\,\Delta V_{th}\,[\text{V}]$$

Transconductance cancels, so $\beta$, $\mu$, $C_{ox}$, and the aspect ratio are never
needed — only the memory window and the $V_{th}$ spread, both of which papers report.

### 5.1 Worked example, format only

$\sigma_{V_{th}} = 50$ mV with $MW = 1.0$ V:

$$\sigma_w = 255 \times 0.050 / 1.0 = 12.8\ \text{LSB}
\quad\Longrightarrow\quad \texttt{D2D\_SIGMA} = \text{round}(2.450 \times 12.8) = 31$$

**These numbers are illustrative.** Replace with cited values in week 7.

---

## 6. Unreported parameters are swept, not guessed

`STUCK_RATE` is the usual case — yield data is frequently omitted from device papers.

Where a parameter is unreported, sweep it across a plausible range and present the result
as a band:

> "Accuracy stays above 90% for stuck-cell rates below 2%."

That is stronger and more honest than "accuracy is 93.2% at our assumed fault rate," and it
survives a reader disagreeing with the assumption.

---

## 7. Target: three parameter sets

Three complete sets from distinct processes. **The comparison across them is the result**;
any single set is just a configuration.

| Slot | Process | Status |
|---|---|---|
| Set A | `dualport_fefet_mlc` — Chatterjee et al., IEEE TED 2022: dual-port HfO2 FeFET, 22 nm FDSOI, t_FE = 3 nm, BG read. **TCAD-simulated.** MW 2.7 V, ≥ 8 levels, σ_Vth 78.75 mV (→ `D2D_SIGMA` 18); read noise not reported (swept). σ/MW cross-checked against measured 28 nm arrays (`literature-survey.md` §2.4). Notes: `literature-survey.md` §2 | values extracted |
| Set B | TBD | survey in progress |
| Set C | TBD | survey in progress |

A source is usable if it reports enough parameters to fill most of a set, states
measurement conditions, and gives **spread rather than only a mean**. A paper reporting "8
levels achieved" with no variance data is a partial source at best.

Specific papers are assigned to specific people as issues, starting week 1 — this is a
search with uncertain duration and no hardware dependency, so it runs in the background
rather than becoming a week-7 scramble.

**If only one complete set turns out to be available,** the semester-2 cross-process
comparison needs replacing. Know that by week 3, not week 20.

---

## 8. Known modelling assumptions

State these plainly in the report rather than letting them be discovered.

**Differential pair modelled as one signed quantity.** A real pair with N levels per cell
yields up to 2N−1 distinguishable differential states, not N. The model is therefore
conservative — it understates achievable resolution, which is the right direction to err,
but it is a choice. Explicit modelling is a semester-2 item.

**Write-path variation is triangular, not Gaussian.** The 16-bit cell hash yields two bytes,
and the sum of two uniforms is triangular. Zero-mean and bounded, which is what matters for
accuracy impact — and arguably more physical than a Gaussian with infinite tails. Do not
claim Gaussian.

**Read noise is bell-shaped but not true Irwin–Hall.** Four bytes from a single LFSR state
are linearly related rather than independent. Report measured moments from
`mac-array-spec.md` test 4; do not assume a theoretical form the construction lacks.

**Quantization is applied before variation.** Physically the variation is *what creates* the
level limit rather than being layered on top of it. The model's ordering is an engineering
abstraction — a materials reviewer will notice, and acknowledging it reads far better than
having it pointed out.

**I–V nonlinearity is not modelled.** The exact integer multiply corresponds to a linearized
read scheme: pulse-width-encoded activations at fixed $V_{DS}$, or a series resistor
dominating cell conductance as in 1FeFET-1R. NeuroSim and AIHWKit both model the
nonlinearity (NeuroSim V1.5 specifics: `literature-survey.md` §1.2). Known gap, and a strong semester-2 addition — structurally identical to
`atten_rom`.

**Single-conversion readout.** The model quantizes the full column sum once. Bit-serial
readout quantizes once per bit plane and shift-adds, which produces a different accuracy
curve at the same `ADC_BITS`. Documented alternative, not modelled (NeuroSim V1.5 models it:
`literature-survey.md` §1.2).

**IR drop is row-position-dependent only.** A rigorous treatment depends on total column
current and is input-dependent. The approximation is what permits the one-multiplier
factorization in `mac-array-spec.md` §8. The coefficients represent the combined bitline
**and source-line** drop, not bitline alone.

**Sense-amplifier non-idealities are not modelled.** Clamp voltage mismatch between columns
would appear as a per-column gain error, and op-amp offset as a per-column additive error.
Both would be cheap to add as per-column constants alongside `atten_rom`.

---

## 9. Verification

This is the **L3** level from `model-verification-design.md` §3 — statistical, not
bit-exact. It can only fail by being unphysical.

1. Measured mean and variance of each injected effect match the target values from the
   parameter set.
2. Stuck-cell fraction across the array matches `STUCK_RATE / 65536` within statistical
   error.
3. Distribution shapes match the documented forms — triangular for write-path variation,
   the measured moments for read noise.
4. Every value in every parameter set has a citation and a confidence label. **A parameter
   set with a blank source field fails this test.**
5. Results carry the parameter set name in their provenance metadata
   (`application-spec.md` §4.5), so any plot can be traced to its device numbers.
