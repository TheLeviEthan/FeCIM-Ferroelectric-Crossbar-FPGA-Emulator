# FeCIM: Response to Scope and Feasibility Review

**Purpose:** structured response to reviewer concerns about analog CIM viability
**Status:** preparation notes — not a submitted document

> **Verify before citing.** References in §10 were assembled from search results and abstracts.
> Author lists, page numbers, and volumes should be confirmed against the actual papers before
> any of this appears in a submitted document. Claims below are paraphrased from abstracts and
> summaries; read the full papers before relying on a specific number in front of reviewers.

---

## 1. The central reframe

The four concerns are about **analog compute-in-memory as a technology**. The deliverable is a
**digital emulator of one**, implemented in synchronous RTL on an FPGA.

| | Analog CIM chip | FeCIM |
|---|---|---|
| Arithmetic | analog current summation | 9-bit signed integer multiply-accumulate |
| Noise | physical, unavoidable | injected deliberately, switchable per mechanism |
| Determinism | no | yes — bit-exact and reproducible at a fixed seed |
| Scaling limit | device pitch, IR drop, ADC cost | logic elements, M9K blocks, multiplier blocks |

Nothing in the FPGA is analog. It does not accumulate physical noise, it does not fight device
pitch, and it scales like any digital design — which is why the resource budget is a known
quantity rather than a physics question.

**Lead with this.** It is not a rebuttal; it is a correction of what is being evaluated, and
the reviewers explicitly left room for it.

---

## 2. The stronger point: concerns 1 and 2 *are* the research question

> "if you're doing matrix operations in analog space, noise is going to add up very quickly"
> "we assume that models trained in digital space aren't very resilient to noise"

Both are hypotheses with quantitative answers, and neither is easy to obtain for FeFET devices
specifically. Restated as measurements:

- *How fast* does noise accumulate — as a function of conductance levels, device-to-device
  variation, read noise, array size, and ADC resolution?
- *How much* accuracy does a digitally-trained network lose, and **which** non-ideality
  dominates that loss?

The second question is what `NOISE_EN` exists to answer. Every mechanism is independently
maskable, so accuracy loss is attributable to a specific physical cause rather than reported
as one aggregate number.

Frame the response this way: the reviewers have stated the project's motivation more concisely
than the original pitch did.

---

## 3. What to concede

Conceding the true parts makes the corrections credible. Do this early and without hedging.

**Concern 1 is substantively correct.** Noise does accumulate and can be severe. Work on
tile-circuit modelling for analog in-memory computing [6] reports that unmitigated IR drop can
degrade BERT accuracy to roughly chance level. That is a published failure mode, not a
hypothetical.

**Concern 3 is partly correct.** Array size is genuinely limited by IR drop, sneak paths, and
ADC cost. Demonstrated FeFET CIM macros are on the order of 64×64 [9] — which is why the MVP
tile is 64×64, the semester-1 target is 128×128, and multi-tile partitioning is deferred to
semester 2. The scope was chosen to match what the field actually builds.

**FeFET is not problem-free.** Recent macro work [9] investigates charge trapping and its
effect on retention and endurance. Acknowledging this is stronger than claiming FeFET solves
everything, and it reinforces that the project measures limitations rather than advertising a
technology.

---

## 4. The one factual correction: FeFET scaling

> "you're fighting the physics of having more analog nodes which can't be easily made smaller
> as compared to digital nodes"

True for two-terminal memristive devices — RRAM, PCM, memcapacitors. **Not true for FeFETs,
and the distinction is why this project targets them.**

A FeFET *is* a transistor: a MOSFET with a ferroelectric gate dielectric. It scales with the
CMOS process rather than against it.

The evidence is direct. FeFET crossbar arrays have been fabricated in GlobalFoundries' standard
28 nm HKMG process, with access transistors, current-limiter transistors, and a current-mode
ADC integrated on the same wafer, reporting full MAC-operation yield across a 300 mm wafer [7].
Co-integration is also non-destructive in both directions — dense co-integration of FeFETs and
nFETs sharing active area is reported to leave both the FeFET's switching behaviour and the
baseline CMOS unaffected [7].

So the accurate statement is: these are CMOS transistors at a production node, fabricated
alongside their own periphery, at a commercial foundry. The scaling objection applies to a
different device family.

**Delivery note:** this is the one place to be firm rather than accommodating, but frame it as
a distinction between device families rather than as an error. "That's the standard concern for
memristive crossbars, and it's why we chose FeFET instead" concedes the general point while
correcting the specific one.

---

## 5. Concern 2 has a direct published answer

> "we assume that models trained in digital space aren't very resilient to noise"

Two independent lines of evidence.

**Hardware-aware retraining closes the gap.** A Nature Communications study on hardware-aware
training [5] reports that larger-scale networks — convolutional, recurrent, and transformer —
can be retrained to match floating-point accuracy, and further finds that non-idealities
perturbing inputs or outputs have greater impact than those perturbing weights. That second
finding is exactly the kind of attribution result FeCIM is built to produce.

The same body of work [6] reports that hardware-aware training combined with per-column
post-training calibration restored BERT to near its software baseline, recovering the
near-chance IR-drop case cited in §3.

**In some FeFET cases no retraining is needed.** A multi-level-cell FeFET crossbar macro at
28 nm [8] reports 96.6% on handwriting recognition and 91.5% on image classification without
additional training.

The honest position: the assumption is a reasonable default, it has been tested, and the answer
is "less fragile than expected, but strongly dependent on which non-ideality dominates."
Determining which one dominates, for a given device, is the tool's purpose.

---

## 6. Feasibility precedent: FPGA emulation of CIM crossbars

The approach is established. The FeFET instance appears to be the gap.

| Ref | Work | Device | Platform |
|---|---|---|---|
| [1] | Accurate Emulation of Memristive Crossbar Arrays for In-Memory Computing | PCM | Kintex UltraScale FPGA |
| [2] | RRAMulator | RRAM | FPGA |
| [3] | HyRPF: Hybrid RRAM Prototyping on FPGA | RRAM | FPGA |
| [4] | GENIEx | memristive | NN-based model (not FPGA) |
| [10] | Decomposition-Based Memristive Crossbar Solver | memristive | FPGA-accelerated |

**The single most useful citation is [1].** It is an FPGA hardware emulator for phase-change
memory that captures conductance drift and 1/f noise, and was validated experimentally against
a prototype array of roughly 400,000 PCM devices in a deep-learning inference experiment. The
authors present it as a tool for exploring in-memory computing, describe it as scalable to
larger networks, and note it is not restricted to neural network inference.

Same architecture, same purpose, same justification as FeCIM — different device. Use it as the
feasibility argument: the approach is published, silicon-validated, and endorsed as
methodology. The open question is not whether FPGA emulation of a CIM crossbar works, but
whether anyone has done it for FeFET.

### 6.1 Positioning against software tools

Expect "why not NeuroSim or AIHWKit?" Do not claim a speed advantage — a GPU running the same
model would likely beat a 12-minute sweep. Two defensible claims:

**The digital periphery comes with the answer.** A software tool reports that accuracy holds at
6 ADC bits. It does not give you the accumulator width, drain logic, or argmax unit that make
6 bits work. FeCIM produces both, verified bit-exact against each other.

**It is a Computer Engineering project.** Reimplementing a software simulator would not satisfy
M0's requirement to demonstrate understanding of both software and hardware. The FPGA
implementation is the accreditation-relevant part, not an odd choice.

---

## 7. Scope defensibility

Point at the numbers, since they exist and were derived rather than asserted:

- 64 lanes, 128×128 tile: **37% logic, 36% M9K, 50% multiplier blocks** on the MAX 10
- MVM latency **7.8 µs**, derived from the cycle-level sequence
- Week-6 MVP at 64×64 / 32 lanes — matching demonstrated FeFET macro size [9]
- Semester 2 adds multi-tile, differential pairs, retention, cross-process comparison

And the credibility move: two headline numbers were **wrong on first estimate and corrected by
derivation** — MVM latency (2.7 → 7.8 µs) and multiplier availability (288 → 144, which forced
a datapath change to 9-bit operands). A team that catches its own specification errors is a
team that will catch them in week 9 rather than week 13.

---

## 8. Suggested response structure

Keep it short. Four moves, in this order.

1. **Accept the opening.** "You may have read this as a proposal to build analog hardware — it
   isn't. The deliverable is a digital emulator in RTL." One sentence, no defensiveness.
2. **Concede concerns 1 and 3.** Noise accumulation is real and severe; array size is genuinely
   limited. Cite the IR-drop result [6]. Note the tile size was chosen to match demonstrated
   macros [9].
3. **Correct concern 4 as a device-family distinction.** FeFETs are CMOS transistors fabricated
   at 28 nm HKMG with their own periphery on the same wafer [7].
4. **Reframe concerns 1 and 2 as the deliverable.** Quantitative questions with no convenient
   FeFET-specific answer. Cite [1] as precedent that the approach works, and note the FeFET
   case appears open.

Close on the resource numbers rather than on enthusiasm.

---

## 9. Questions to expect

- **"Why not just use AIHWKit?"** — §6.1. Prior AIHWKit experience is what makes the limitation
  visible, not a reason to avoid the tool.
- **"What if the published device data isn't good enough?"** — Literature survey starts week 1,
  not week 7. Under-reported parameters are swept as a band rather than guessed.
- **"Isn't this just a simulator on an FPGA?"** — The digital periphery is part of the design,
  not a wrapper around a model. The argmax unit, accumulator widths, and ADC quantizer are the
  architecture a real chip would need.
- **"How do you know your emulator is right?"** — Bit-exact equivalence against a C++ golden
  model under Verilator, with non-idealities disabled, as a CI gate. That test either passes
  exactly or blocks the build.

---

## 10. References

**FPGA emulation of CIM crossbars**

[1] *Accurate Emulation of Memristive Crossbar Arrays for In-Memory Computing.*
arXiv:2004.03073. https://arxiv.org/abs/2004.03073
— PCM cell and crossbar emulator on Kintex UltraScale; captures conductance drift and 1/f
noise; validated against a ~400,000-device PCM prototype array. **Primary feasibility
precedent.**

[2] Wen, J., Vargas, F., Zhu, F., Reiser, D., Baroni, A., Fritscher, M., Perez, E.,
Reichenbach, M., Wenger, C., Krstić, M. *RRAMulator: An efficient FPGA-based emulator for RRAM
crossbar with device variability and energy consumption evaluation.* Microelectronics
Reliability, vol. 168, art. 115630, May 2025. doi:10.1016/j.microrel.2025.115630

[3] Reiser, D., Knödtel, J., Almeeva, L., Wen, J., Baroni, A., Krstić, M., Reichenbach, M.
*HyRPF: Hybrid RRAM Prototyping on FPGA.* In Embedded Computer Systems: Architectures,
Modeling, and Simulation (SAMOS 2024), pp. 199–215. doi:10.1007/978-3-031-78377-7_14

[4] *GENIEx: A Generalized Approach to Emulating Non-Ideality in Memristive Xbars using Neural
Networks.* arXiv:2003.06902. https://arxiv.org/abs/2003.06902

[10] *A Decomposition-Based Memristive Crossbar Solver and FPGA-Accelerated Hardware
Implementation.* Proc. Great Lakes Symposium on VLSI (GLSVLSI) 2025.
doi:10.1145/3716368.3735282

**Noise resilience and hardware-aware training**

[5] *Hardware-aware training for large-scale and diverse deep learning inference workloads
using in-memory computing-based accelerators.* Nature Communications, 2023.
https://www.nature.com/articles/s41467-023-40770-4
— Retraining to floating-point iso-accuracy across convnets, RNNs, and transformers; finds
input/output non-idealities dominate weight non-idealities. Often cited as Rasch et al. 2023;
confirm the author list.

[6] *Rapid yet accurate Tile-circuit and device modeling for Analog In-Memory Computing.*
arXiv:2506.00004. https://arxiv.org/abs/2506.00004
— IR-drop severity (BERT to near-chance) and recovery via hardware-aware training plus
per-column calibration. **Source for the concession in §3.**

[11] Nair, L., Bunandar, D. *Sensitivity-Aware Finetuning for Accuracy Recovery on Deep
Learning Hardware.* Lightmatter. arXiv:2306.03076. https://arxiv.org/abs/2306.03076

[12] *Improving the Accuracy of Analog-Based In-Memory Computing Accelerators Post-Training.*
arXiv:2401.09859. https://arxiv.org/abs/2401.09859

[13] *Memory Is All You Need: An Overview of Compute-in-Memory Architectures for Accelerating
Large Language Model Inference.* arXiv:2406.08413. https://arxiv.org/abs/2406.08413
— Useful survey for background framing.

**FeFET crossbar demonstrations**

[7] *Demonstration of Multiply-Accumulate Operation With 28 nm FeFET Crossbar Array.* IEEE
Electron Device Letters.
— GlobalFoundries 28 nm HKMG; access transistors, current limiters, and current-mode ADC on
the same wafer; full MAC yield on a 300 mm wafer; 8×8 segments. **Source for the §4
correction.** Confirm authors and volume/issue.

[8] Soliman, T., Chatterjee, S., Laleni, N., et al. *First demonstration of in-memory computing
crossbar using multi-level Cell FeFET.* Nature Communications, October 2023.
https://www.nature.com/articles/s41467-023-42110-y
— 1FeFET-1R multi-level cell; 96.6% handwriting / 91.5% image classification without extra
training; 885 TOPS/W.

[9] *A 28-nm FeFET Compute-in-Memory Macro With 64×64 Array Size and On-Chip 4-Bit Flash ADC.*
— 4 kb macro in GlobalFoundries 28 nm HKMG; 64×64 array with eight 4-bit flash ADCs; examines
charge trapping effects on retention and endurance. **Source for array-size and
FeFET-limitations points.** Confirm venue and year.

[14] De, S., Müller, M., et al. *28 nm HKMG-Based Current Limited FeFET Crossbar-Array for
Inference Application.* IEEE Transactions on Electron Devices, 2022.
— MLP inference with experimentally obtained device-to-device variation; retention degradation
analysis.
