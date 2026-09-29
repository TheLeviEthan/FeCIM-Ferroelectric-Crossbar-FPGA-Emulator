# FeCIM Component Spec: Application and Sweep Harness

**Module:** `sw/fecim/mapping.py`, `sw/fecim/sweep.py`, `sw/examples/`
**Owner:** Lane D (Natalie Poche)
**Depends on:** `host-driver-spec.md`, `docs/device-model.md` (parameter sets)
**Milestone:** demo week 4, calibrated sweeps week 7, headline plots week 9
**Status:** draft

---

## 1. Purpose and scope

Turns a trained neural network into something the array can execute, runs it, and turns
the results into the plots that are the project's output.

Three parts: **weight mapping** (float network → int8 tile), the **demo network**, and the
**sweep harness** with its provenance and plotting.

---

## 2. Weight mapping

The part with real design content, and the part most likely to produce a silently wrong
answer.

### 2.1 Orientation

A PyTorch or sklearn linear layer has weight shape `(out_features, in_features)`. The tile
is indexed `[row, col]` where rows are inputs and columns are outputs, so the matrix must
be **transposed**:

```python
tile = W.T          # (in_features, out_features) = (rows, cols)
```

Getting this backwards produces a result of the right shape and entirely wrong values.
Test 3 in §6 catches it.

### 2.2 Weight scaling — per-column

Weights are `int8`. Two options for choosing the scale:

**Per-tensor:** one scale for the whole matrix, `s = max|W| / 127`. Simple, and loses
resolution when output neurons have very different weight magnitudes.

**Per-column (recommended):** one scale per output neuron.

```python
s_w = np.abs(tile).max(axis=0) / 127.0      # shape (cols,)
s_w[s_w == 0] = 1.0                          # guard against a dead column
q = np.round(tile / s_w).astype(np.int8)
```

Per-column is standard practice in quantized inference and costs nothing in hardware —
each column's accumulator is descaled independently on the host. The `s_w == 0` guard
matters: a layer with a zero column is not exotic after pruning, and it produces a
divide-by-zero.

### 2.3 Activation scaling

Activations are `uint8`, and for post-ReLU layers they are non-negative by construction:

```python
s_a = x.max() / 255.0
x_q = np.round(x / s_a).astype(np.uint8)
```

### 2.4 The first layer needs a correction

Post-ReLU activations are non-negative, but **raw inputs generally are not** — normalized
pixels are typically signed. Shift them into unsigned range and correct the result
analytically:

$$x_q = x' + 128 \quad\Longrightarrow\quad \sum_i w_{ij}\,x_q = \sum_i w_{ij}\,x'_i + 128\sum_i w_{ij}$$

So subtract a per-column constant from the accumulator:

```python
offset = 128 * q.sum(axis=0).astype(np.int64)      # shape (cols,), computed once
y_int = acc - offset
```

This is exact, not an approximation, and it costs one precomputed vector. **It is the
detail most likely to be forgotten**, and forgetting it produces an output with a constant
per-column bias — which looks like a bias-initialization bug and can persist for weeks.

### 2.5 Descaling

```python
y = y_int * s_w * s_a + bias
```

Bias is applied on the host. The array computes a matrix-vector product and nothing else —
there is no bias term in hardware, and adding one would be modelling a peripheral feature
no crossbar has.

### 2.6 Layers larger than one tile

At 128×128 most real layers do not fit. Semester 1 restricts the demo to layers that do;
semester 2 adds tiling:

$$y_j = \sum_{t} \text{tile}_t \cdot x_t$$

Input split across tiles, partial accumulators summed on the host. Reserve the API shape
now — `load_weights` should accept a list of tiles even if it currently rejects more than
one — so the semester-2 work is not a signature change.

### 2.7 Weight reload is the slow path

Loading a 128×128 tile is 16,384 bytes: **1.4 s at 115200, 178 ms at 921600.**

A multi-layer network therefore reloads weights between layers, so a two-layer forward pass
costs two tile loads plus two 7.8 µs computes. The compute is four orders of magnitude
faster than the load.

This is worth stating in the report rather than hiding: it is exactly the pressure that
motivates multi-tile support, and it is an honest limitation of a single-tile emulator
rather than a property of crossbar accelerators generally.

---

## 3. Demo network

### 3.1 MVP — week 4

**sklearn `load_digits`**: 8×8 images, 64 features, 10 classes, 1,797 samples.

Single linear layer, 64 → 10, mapping onto a 64-row × 10-column region of the 64×64 MVP
tile. No tiling, no hidden layer, no downsampling. Baseline accuracy around 95% with
logistic regression, which is enough headroom to see degradation clearly.

This choice is deliberate: the smallest thing that is still a real classifier. Week 4 is
about proving the pipeline end to end, not about the network.

### 3.2 Semester 1 target — week 8

Two options, both fitting a 128×128 tile:

**Downsampled MNIST.** 28×28 → 11×11 = 121 features, 121 → 10. Single tile, recognizable
dataset, ~92% baseline.

**Two-layer MLP on digits.** 64 → 32 → 10, both layers on the tile, loaded sequentially
with ReLU applied on the host between them. Slower per inference (two tile loads) but
demonstrates multi-layer execution, which is a stronger result.

Recommend the MLP. It exercises activation requantization between layers — the place where
the non-negativity assumption in §2.3 actually holds — and it makes the weight-reload cost
visible, which motivates semester 2.

### 3.3 Training happens off-array

Networks are trained in float on the host with standard tooling, then quantized and
mapped. **No on-chip training**, which is out of scope permanently and would be a poor fit
for FeFET endurance anyway.

Keep a float reference forward pass alongside every demo — the accuracy gap between float
and array is the primary result, so the float number must be computed the same way every
time.

---

## 4. Sweep harness

### 4.1 Shape

```python
from fecim import sweep

results = sweep.run(
    xbar,
    network=net,
    dataset=test_set,
    axis=sweep.Axis("read_sigma", values=np.linspace(0, 20, 21)),
    enable=["quant", "d2d", "read"],
    seeds=[0xACE1, 0xBEEF, 0xC0DE],
    parameter_set="gf28_hkmg",
)
```

Two-dimensional sweeps take two axes. Multiple seeds per point give error bars — a single
seed reports one draw from a distribution and presents it as a measurement.

### 4.2 Reseed between points, not within

Hold the seed fixed across all images at a sweep point, and change it only between seed
repetitions. Otherwise the device-variation pattern changes per image, which models a new
die per inference — not a physical situation, and it averages away exactly the
spatial-correlation effect worth measuring.

### 4.3 Use `READ_ARGMAX`

Per `result-buffer-spec.md` §6.2, argmax readout is 166 bytes per image against 676 for
full results — a 400-point sweep over 1,000 images runs in about **12 minutes** at 921600
instead of 49.

Keep a `full_results=True` option. Raw accumulators are needed for equivalence tests and
any plot that is not a classification metric.

### 4.4 Record per-class accuracy, always

Not just the aggregate. At low ADC resolution, ties across columns become common — 128
columns spread across 16 codes at 4 bits averages eight columns per code. The tie rule
resolves to the lowest index, which produces a **systematic bias toward low-numbered
classes**.

On a digit classifier that appears as suspiciously good accuracy on 0 and poor accuracy on
9. It is invisible in the aggregate number and obvious in the breakdown, and it is a real
finding about coarse quantization rather than a bug.

### 4.5 Provenance

Every result file carries enough metadata to regenerate it:

```json
{
  "git_commit": "a3f9c21",
  "rtl_build_id": 3,
  "identify": {"magic": "0xFEC1", "proto": 2, "lanes": 64,
               "rows": 128, "cols": 128},
  "backend": "fpga",
  "parameter_set": "gf28_hkmg",
  "config": {"quant_levels": 8, "d2d_sigma": 31, "read_sigma": 12,
             "adc_bits": 6, "noise_en": ["quant", "d2d", "read"]},
  "seeds": [44257, 48879, 49374],
  "dataset": "digits_test", "n_samples": 360,
  "timestamp": "2026-11-14T15:22:09Z"
}
```

Data as Parquet or CSV; metadata as a JSON sidecar with the same stem.

**A plot that cannot be regenerated is not evidence.** In week 12 someone will ask why one
curve looks odd, and the answer will be in the metadata or nowhere.

### 4.6 Parameter sets are version-controlled data

One file per published source in `sw/fecim/parameter_sets/`, with the citation inside:

```yaml
name: gf28_hkmg
source: "Author et al., IEEE TED 2022, doi:10.1109/..."
confidence: direct
memory_window_v: 1.0
quant_levels: 8
sigma_vth_mv: 50
read_sigma_vth_mv: 3
stuck_rate: null        # not reported — sweep as a band
```

Never values typed into a script. `confidence` and `null` for unreported parameters carry
the provenance discipline from `model-verification-design.md` §7.3 into the code, so a
missing value forces a sweep rather than a silent guess.

---

## 5. Plots

Three that matter:

**Accuracy versus parameter.** The obvious one. Flat, then a cliff.

**Decision margin versus parameter.** The better one. Margin — `max_val − second_val` from
`READ_ARGMAX` — degrades continuously while accuracy is a step function, so this plot shows
the *mechanism* rather than just the outcome. It is what makes the accuracy cliff
explicable.

**Per-class accuracy at low ADC resolution.** Exposes the tie bias in §4.4.

Plot median with an inter-seed band, not a single trace. `matplotlib` only, as an optional
extra.

---

## 6. Verification

1. **Round-trip mapping.** Quantize a float matrix, descale, compare against the original.
   Error must be bounded by the quantization step.
2. **Against float reference.** With all noise disabled, array accuracy must match the
   float network to within quantization error. **This is the gate for the whole
   application layer** — if it fails, nothing downstream is interpretable.
3. **Orientation.** A deliberately asymmetric matrix (distinct row and column sums)
   mapped, computed, and compared against `numpy`. Catches a missing transpose, which
   symmetric test data would hide.
4. **First-layer offset.** Signed inputs through the shift-and-correct path, compared
   against the float result. Then confirm that omitting the offset produces a constant
   per-column error. Directed test for §2.4.
5. **Per-column scaling.** A matrix with columns of wildly different magnitude; confirm
   per-column beats per-tensor in accuracy, and that a zero column does not raise.
6. **Cross-backend.** Full demo on `sim` and `fpga` with matched seeds — identical
   predictions required.
7. **Sweep reproducibility.** Run a short sweep twice with the same seeds; results must be
   identical. Then confirm changing the seed changes them.
8. **Provenance completeness.** Every field in §4.5 present and non-empty in generated
   output.
9. **Tie bias.** At `adc_bits=4`, confirm per-class accuracy skews toward low indices.
   Documents §4.4 as much as it tests it.

---

## 7. Open questions

1. **Per-column or per-tensor scaling by default?** Per-column is better but means the
   comparison against published results depends on which they used. Suggest per-column
   default with per-tensor available, and state which is used in every plot.
2. **How many seeds per sweep point?** Three gives a crude band; five is better; each
   multiplies runtime. Suggest three for exploration, five for the final figures.
3. **Which dataset for the week-8 target?** §3.2 recommends the two-layer MLP on digits
   over downsampled MNIST. MNIST is more recognizable to a general audience; the MLP is a
   stronger engineering result. Decide before week 7, since calibration sweeps should run
   against the final network.
4. **Should activation quantization be modelled as a non-ideality?** Currently activations
   are quantized on the host as a mapping step, not injected as a device effect — correct,
   since activations are drive voltage rather than stored state. But DAC resolution on the
   wordline drivers is a real peripheral limit that nothing currently models. Candidate for
   semester 2.
