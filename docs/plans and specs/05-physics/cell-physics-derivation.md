# FeCIM: Per-Cell Physics and Parameter Derivation

**Purpose:** the chain from FeFET device physics to the integer arithmetic in the RTL, and
the conversion formulas for turning published device numbers into register values.
**Owner:** Lane C, with Lane D
**Destination:** `docs/device-model.md`, derivation appendix

---

## 1. Notation

| Symbol | Meaning | Units |
|---|---|---|
| $N$ | distinguishable conductance levels per cell | — |
| $k$ | level index, $0 \ldots N-1$ | — |
| $P_r$ | remanent polarization | µC/cm² |
| $t_{fe}$, $\varepsilon_{fe}$ | ferroelectric thickness, permittivity | nm, — |
| $V_{th}$ | threshold voltage | V |
| $MW$ | memory window, $V_{th}^{max} - V_{th}^{min}$ | V |
| $\beta$ | $\mu C_{ox} W/L$, transconductance parameter | S/V |
| $G$ | channel conductance at read bias | S |
| $V_{read}$ | gate read bias | V |
| $\sigma_{V_{th}}$ | device-to-device $V_{th}$ spread | V |
| $\nu_{ij}(t)$ | instantaneous $V_{th}$ fluctuation from trap charging | V |
| $R_w$ | bitline resistance per cell pitch | Ω |
| $M$ | rows per column | — |

---

## 2. The single-cell chain

Four transductions, each contributing one modelled non-ideality.

### 2.1 Pulse → switched domain fraction → polarization

HZO is polycrystalline, so coercive fields are distributed across grains. A pulse
insufficient to switch every domain switches a fraction, giving intermediate net
polarization:

$$P_r(k) = P_{sat}\left(\frac{2k}{N-1} - 1\right), \qquad k = 0 \ldots N-1$$

$N$ is set by how precisely the switched fraction can be controlled against the variability
*in* that fraction. **This is `QUANT_LEVELS`.**

### 2.2 Polarization → threshold voltage

The bound charge at the ferroelectric/interfacial-layer boundary must be screened by the
channel, shifting $V_{th}$. The ideal relation is

$$\Delta V_{th} = \frac{2 P_r t_{fe}}{\varepsilon_0 \varepsilon_{fe}}$$

which for HZO at $P_r \approx 20\ \mu\text{C/cm}^2$, $t_{fe} \approx 10$ nm,
$\varepsilon_{fe} \approx 30$ predicts about **15 V**. Measured memory windows are near
**1 V**.

**Do not use the ideal expression for calibration.** The order-of-magnitude gap is absorbed
by the series interfacial layer, the depolarization field, and interface-trap compensation.
Use the reported $MW$ directly:

$$V_{th}(k) = V_{th}^{c} - \frac{MW}{2}\left(\frac{2k}{N-1} - 1\right) + \delta_{ij} + \nu_{ij}(t)$$

The sign is negative for an n-channel device: polarization toward the substrate enhances
inversion and lowers $V_{th}$.

Two perturbations enter here, and they are physically distinct:

- $\delta_{ij}$ — **static**, per-device, from grain orientation, size, and defect
  distribution. Fixed once programmed. **This is `D2D_SIGMA`.**
- $\nu_{ij}(t)$ — **dynamic**, changes on every read, from interface traps charging and
  discharging (1/f and RTN). **This is `READ_SIGMA`.**

That both originate at $V_{th}$ and differ only in timescale is why the RTL splits injection
between the write path and the lane read path. Same physical quantity, two correlation times.

### 2.3 Threshold voltage → conductance

In the linear region at small $V_{ds}$:

$$I_d = \beta\left(V_{read} - V_{th} - \frac{V_{ds}}{2}\right)V_{ds}
\quad\Longrightarrow\quad
G = \frac{\partial I_d}{\partial V_{ds}} \approx \beta\left(V_{read} - V_{th}\right)$$

So $G$ is **linear** in $V_{th}$, and $V_{th}$ is linear in polarization — which is the
entire reason the crossbar computes a multiply at all. The full conductance range is

$$G_{max} - G_{min} = \beta \cdot MW$$

The $-V_{ds}/2$ term is the nonlinearity the model currently ignores; see §6.2.

### 2.4 Stuck cells

Pinned domains, dead grains, and gate-stack defects give cells that ignore programming:

$$G_{ij} = \begin{cases} G_{min} \text{ or } G_{max} & \text{with probability } p_{stuck} \\ G(k) & \text{otherwise} \end{cases}$$

**This is `STUCK_RATE`.**

---

## 3. The column sum

Ideally, by KCL on the bitline:

$$I_j = \sum_{i=0}^{M-1} G_{ij} V_i$$

Note that no DC current flows in the gate lines — a FeFET gate is capacitive, so wordline IR
drop is a settling-time issue, not a static error. **The DC drop is on the bitline**, where
current accumulates.

Cell $i$'s current traverses the bitline segments between it and the sense amplifier, so the
source-node voltage is elevated by the accumulated current through those segments:

$$\Delta V_i = R_w \sum_{i' } c(i,i')\, I_{i'}$$

This is a coupled problem — the drop depends on every cell's current, which depends on the
data. The modelled approximation replaces the actual currents with a nominal $\bar{I}$ and
keeps only row-position dependence:

$$\alpha_i \approx 1 - \frac{R_w \bar{I}}{V_{ds}} f(i), \qquad f(i) \approx \frac{i(2M-i+1)}{2}$$

$$I_j \approx \sum_i \alpha_i G_{ij} V_i$$

**This is `atten_rom`.** The factorization in MAC array spec §8 — applying $\alpha_i$ to the
activation once rather than to each product — is valid precisely because $\alpha$ was reduced
to a function of $i$ alone. A rigorous treatment is input-dependent and breaks it.

Then the ADC:

$$D_j = Q_b(I_j), \qquad b = \texttt{ADC\_BITS}$$

---

## 4. Correspondence to the RTL

Normalized weight $w \in [-128, 127]$ maps to conductance by

$$G_{ij} = G_{min} + \left(G_{max} - G_{min}\right)\frac{u_q}{255}, \qquad u_q = w_q + 128$$

so one weight LSB corresponds to $(G_{max}-G_{min})/255 = \beta\, MW/255$, and equivalently
to a $V_{th}$ shift of $MW/255$.

**That last equivalence is the key calibration identity:**

$$\boxed{\;\Delta w \,[\text{LSB}] = \frac{255}{MW}\,\Delta V_{th}\,[\text{V}]\;}$$

### 4.1 Quantization

| Physics | RTL |
|---|---|
| $k = \text{round}\!\left(\frac{u}{255}(N-1)\right)$ | `level = (u * QUANT_LEVELS) >> 8` |
| $G(k)$, re-expressed in weight LSBs | `u_q = (level * QUANT_MULT) >> 8` |

$$\texttt{QUANT\_LEVELS} = N, \qquad
\texttt{QUANT\_MULT} = \text{round}\!\left(\frac{255 \cdot 256}{N-1}\right)$$

*Check, $N = 8$:* `QUANT_MULT` = 9326. For $u = 255$: `level` = 7, `u_q` = 255. For
`level` = 3: `u_q` = 109, against $3 \times 255/7 = 109.3$. ✓

### 4.2 Device-to-device variation

The RTL forms a triangular variate from two hash bytes:

```
ih_sum = h_d2d[7:0] + h_d2d[15:8]        // 0..510
ih9    = ih_sum - 255                    // ±255,  σ = 104.5
d2d    = (ih9 * D2D_SIGMA) >> 8
```

Each byte is uniform on $[0,255]$ with variance $(256^2-1)/12 = 5461$, so
$\sigma_{ih9} = \sqrt{2 \times 5461} = 104.5$. Therefore
$\sigma_{d2d} = 104.5 \cdot \texttt{D2D\_SIGMA}/256$, giving

$$\boxed{\;\texttt{D2D\_SIGMA} = \text{round}\!\left(2.450 \cdot \sigma_w\right)\;}
\qquad \sigma_w = \frac{255\,\sigma_{V_{th}}}{MW}$$

**Worked example.** A paper reports $\sigma_{V_{th}} = 50$ mV and $MW = 1.0$ V:

$$\sigma_w = 255 \times 0.05 / 1.0 = 12.8 \text{ LSB}
\quad\Longrightarrow\quad \texttt{D2D\_SIGMA} = \text{round}(2.450 \times 12.8) = 31$$

Maximum representable: $\texttt{D2D\_SIGMA} = 255 \Rightarrow \sigma_w = 104$ LSB, about 41%
of full scale. Ample.

### 4.3 Read noise

Four LFSR bytes, halved to fit 9 bits:

```
ih_sum = lfsr[7:0]+lfsr[15:8]+lfsr[23:16]+lfsr[31:24]   // 0..1020
ih_c   = ih_sum - 510                                    // ±510, σ = 147.8
ih9    = ih_c >>> 1                                      // ±255, σ = 73.9
noise  = (ih9 * READ_SIGMA) >>> 9
```

$\sigma_{noise} = 73.9 \cdot \texttt{READ\_SIGMA}/512 = 0.1443\,\texttt{READ\_SIGMA}$, so

$$\boxed{\;\texttt{READ\_SIGMA} = \text{round}\!\left(6.93 \cdot \sigma_w\right)\;}$$

Maximum: $\texttt{READ\_SIGMA} = 255 \Rightarrow \sigma_w = 36.8$ LSB, which sits at
$127/36.8 = 3.4\sigma$ from the clamp — so clipping is negligible even at maximum setting.
Realistic read noise is a few LSB, so the range is roughly ten times what is needed.

### 4.4 Stuck cells

$$\texttt{STUCK\_RATE} = \text{round}\!\left(65536 \cdot p_{stuck}\right)$$

Resolution is $1/65536 = 0.0015\%$.

### 4.5 IR drop

$$\texttt{atten\_rom}[i] = \text{round}\!\left(256\,\alpha_i\right)$$

A first-order linear model, adequate for a parameter sweep, is
$\alpha_i \approx 1 - \kappa\, i/M$ with $\kappa$ the worst-case fractional drop at the far
row. Sweep $\kappa$; it is not a device parameter and should not be presented as one.

---

## 5. Full parameter summary

| Register | Physical origin | Conversion |
|---|---|---|
| `QUANT_LEVELS` | controllable switched-domain fraction | $N$ directly |
| `QUANT_MULT` | — (arithmetic) | $\text{round}(255\cdot256/(N-1))$ |
| `D2D_SIGMA` | grain statistics → static $V_{th}$ spread | $2.450 \cdot 255\sigma_{V_{th}}/MW$ |
| `READ_SIGMA` | interface traps → dynamic $V_{th}$ fluctuation | $6.93 \cdot 255\sigma_{V_{th},rms}/MW$ |
| `STUCK_RATE` | pinned domains, gate-stack defects | $65536\,p_{stuck}$ |
| `atten_rom[i]` | bitline resistance (circuit, not device) | $256\,\alpha_i$ |
| `ADC_BITS` | converter design choice | $b$ directly |
| retention factor | depolarization field, trap compensation | $256\,P_r(t)/P_r(0)$ |

Only the first five are device properties. `atten_rom` and `ADC_BITS` are circuit design
parameters and should be labelled as such in the confidence column — presenting them as
measured device data would be misleading.

---

## 6. Two problems this derivation found

### 6.1 The ADC shift is wrong

MAC array spec §9 computes `sh = ACC_W - adc_bits` with `ACC_W = 32`. But the accumulator's
**actual** range is

$$254 \times 255 \times 128 = 8{,}290{,}560 \approx 2^{23}$$

so real values occupy 24 signed bits, not 32. With $b = 8$, the spec gives `sh = 24`,
quantizing over $\pm 2^{31}$ — a range the signal never approaches. **Every result would
collapse into one or two ADC codes.** The ADC would appear catastrophically destructive at
any setting, and the obvious suspicion would fall on the rounding logic rather than the
full-scale reference.

Fix: quantize over the used range, not the container width. Add to the package

```systemverilog
parameter int ACC_USED_W = $clog2(ACC_MAX_MAG) + 1;   // 24 at 128 rows
```

and use `sh = ACC_USED_W - adc_bits`. Better still, make `ADC_SHIFT` a host-written register
computed from the actual expected accumulator range for the loaded network — real ADC
full-scale is a design choice, and exposing it makes that explicit and sweepable.

Add a verification case: with `adc_bits = ACC_USED_W`, the ADC must be an exact no-op.

### 6.2 `READ_SIGMA` scaling is documented inconsistently

MAC array spec §5.3 describes `sigma9` as Q1.8 spanning 0 to 0.996, but the code shifts by 9,
making the effective range 0 to 0.498. The text and the arithmetic disagree.

**Keep the shift at 9** — it is the better choice, since it places maximum noise at 3.4σ from
the clamp instead of 1.7σ, avoiding clip distortion at high settings. Correct the prose
instead: `READ_SIGMA` is `sigma9/512`, not Q1.8. The conversion in §4.3 above assumes the
shift of 9.

This matters beyond documentation: `model_exact` must use the same shift, and a factor-of-two
disagreement between model and RTL would fail L1 equivalence with no obvious cause.

---

## 7. Known gap: I–V nonlinearity

The model assumes an exact integer multiply, which corresponds to a linearized read scheme —
either pulse-width-encoded activations at fixed $V_{ds}$, or a series resistor dominating the
cell conductance as in the 1FeFET-1R construction.

If activations are instead encoded as $V_{ds}$ amplitude, the $-V_{ds}/2$ term in §2.3 makes
the product nonlinear in the activation, and driving the gate harder eventually leaves the
linear region entirely.

**State this as a known gap in the report**, since NeuroSim and AIHWKit both model it. It is a
strong semester-2 addition and cheap: a lookup table on the activation broadcast path, one
shared instance, structurally identical to `atten_rom`. Adding it would also let the tool
answer a question the linearized assumption hides — how much accuracy is bought by the series
resistor in a 1FeFET-1R cell.
