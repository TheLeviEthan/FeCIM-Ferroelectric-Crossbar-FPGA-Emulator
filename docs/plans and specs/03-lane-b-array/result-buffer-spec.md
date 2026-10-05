# FeCIM Component Spec: Result Buffer and Readout

**Module:** `result_buffer`, `argmax_unit`, `readout_ser`
**Owner:** Lane A (RTL), response format jointly with Lane C
**Depends on:** `mac_array` (drain chain), `control_fsm`, `packet_tx`
**Milestone:** Week 4 (buffer), Week 6 (argmax)
**Status:** draft

---

## 1. Purpose and scope

Captures column results as they emerge from the drain shift chain, holds them until the
next `COMPUTE`, and serializes them into response packets.

Also computes on-chip argmax. That started as a bandwidth optimization — the activation
buffer analysis showed result readout is 68% of sweep traffic — but it turns into the more
interesting measurement, for reasons in §5.

---

## 2. Storage

`TILE_COLS × 32` bits. For the MVP that is 64 × 32 = 2,048 bits; one M9K in 256×32 mode
holds 256 results, leaving room for a 256-column tile.

Simple dual-port: write port for the drain, read port for readout. The two phases never
overlap, so this is a convenience rather than a requirement, but it costs nothing and
removes the need to think about it.

`ACC_W` is 32 per the MAC array spec §2 correction, which is exactly the M9K's native word
width in this mode. Convenient, not a coincidence — 24-bit would have wasted the block or
needed packing.

---

## 3. Write during `DRAIN`

Lane `l` holds the accumulator for column `c = p·L + l`. The chain output is tapped from
lane 0.

| `drain_cnt` | Chain output holds | Write address |
|---|---|---|
| 0 | lane 0 (no shift yet) | `p·L + 0` |
| 1 | lane 1, shifted in | `p·L + 1` |
| … | … | … |
| `L−1` | lane `L−1` | `p·L + L−1` |

So `result_addr = {pass_idx, drain_cnt}` — concatenation, no arithmetic, because `L` is a
power of two. `shift_en` asserts from cycle 1 onward; lane 0's own value is captured on
cycle 0 before any shifting.

Values pass through the shared ADC quantizer (MAC array spec §9) before being written, so
the buffer holds post-ADC values.

---

## 4. Result validity

Two small counters, both worth their area:

**`results_valid`** — cleared by `CTRL[1]` and at the start of each `COMPUTE`, set on
`CTRL_DONE`. Exposed in `STATUS`. A `READ_RESULT` against an invalid buffer still returns
data, but the host can tell it is stale.

**`result_seq`** — an 8-bit counter incremented on every `COMPUTE` completion, exposed in
`STATUS[15:8]`. It is **not** echoed in readout responses — `READ_RESULT` returns exactly
`count × 4` bytes (protocol §4.4). The driver reads `STATUS` via `GET_CONFIG` after each
`COMPUTE` (`host-driver-spec.md` §4.5).

The sequence number earns its ten LEs the first time a `COMPUTE` response is lost and the
driver retries. Without it, the host cannot distinguish "the compute happened and the
response was dropped" from "the compute never ran," and it will happily read results
belonging to the previous vector. That failure produces a plausible accuracy number that
is silently wrong for one image in however-many-thousand — invisible in testing, and
exactly the kind of thing that makes a sweep irreproducible.

Results persist until the next `COMPUTE`, so a retried `READ_RESULT` is always safe.

---

## 5. Argmax, and why it should track second place too

### 5.1 The basic unit

Comparison happens as values are written during `DRAIN` — they already stream past one per
cycle, so no extra pass is needed.

```systemverilog
always_ff @(posedge clk) begin
    if (compute_start) begin
        max_val  <= 32'sh8000_0000;
        max_idx  <= '0;
    end else if (result_we && (result_addr < active_cols)) begin
        if ($signed(adc_out) > max_val) begin
            max_val <= $signed(adc_out);
            max_idx <= result_addr;
        end
    end
end
```

**The `active_cols` guard is required, not defensive.** Control FSM open question 3 proposed
rounding `active_cols` up to a multiple of `L` and discarding extras on the host. That works
for `READ_RESULT`, but argmax runs on-chip and would happily select a garbage column. This
gate resolves that question: round up in the sequencer, mask in the argmax.

**Tie-breaking uses strict `>`,** which keeps the lowest index on a tie. This matches
`numpy.argmax`. The C++ model must use the same rule — with quantized ADC output, ties are
not rare, and a mismatched tie rule produces sporadic disagreements that look like noise
model bugs.

### 5.2 Track the runner-up as well

Add a second comparator holding the second-largest value. About 70 more LEs.

The reason is not bandwidth. **Decision margin — the gap between the best and second-best
column — degrades continuously as noise increases, while classification accuracy is a step
function that only moves when the margin crosses zero.**

An accuracy-versus-noise plot is flat, flat, flat, then falls off a cliff. A
margin-versus-noise plot shows the mechanism eroding smoothly the whole time, and makes the
accuracy cliff explicable rather than merely observed. For the fatigue-cycling result in
week 9 — the headline plot of the project — that is the difference between "accuracy drops
after 10⁵ cycles" and "margin decays continuously from cycle one and crosses the decision
threshold at 10⁵."

The second is a much stronger claim, and it costs one comparator.

It also has diagnostic value during bring-up: if margins are large but accuracy is poor,
your weight mapping is wrong. If margins are near zero everywhere, your activation scaling
is wrong. Accuracy alone cannot distinguish those.

### 5.3 ADC before argmax

Quantization is applied before the comparison, per MAC array spec open question 2.

Physically the ADC precedes any digital post-processing, so this is the correct order. It
also means coarse ADC settings can flip the winner when two columns are close, which is
real behaviour and shows up in the margin plot as a quantization floor.

---

## 6. Readout serialization

Results are 32-bit; UART is 8-bit. The serializer presents a byte stream to `packet_tx`,
which computes CRC over it as it goes.

**Little-endian**, two's complement, matching `struct.unpack('<i')` on the host.

| Signal | Dir | Description |
|---|---|---|
| `rd_go` | in | start, with range latched |
| `rd_start` | in | first word index |
| `rd_count` | in | number of words |
| `rd_data` | out | byte stream |
| `rd_valid` | out | byte available |
| `rd_next` | in | consumer took the byte |

The FSM reads one 32-bit word, emits four bytes with handshake, then reads the next. At
~4,340 clock cycles per UART byte, the single-cycle M9K read latency is invisible; do not
build a prefetch.

### 6.1 Response formats

**`READ_RESULT`** — payload `[start:2][count:2]`, response is `count × 4` bytes.
`MAX_PAYLOAD = 512` allows 128 words per packet, so a 128-column tile needs one packet and
a 256-column tile needs two.

**`READ_ARGMAX`** — no payload, response is 10 bytes:

| Offset | Width | Field |
|---|---|---|
| 0 | 2 | `max_idx` |
| 2 | 4 | `max_val` (signed) |
| 6 | 4 | `second_val` (signed) |

Ten bytes rather than one. The bandwidth difference against the 1-byte version is
negligible — 102 bytes per image versus 93 — and the margin comes free with it.

### 6.2 Updated sweep cost

| Transaction | Bytes |
|---|---|
| `WRITE_ACT` | 72 |
| `COMPUTE` request + response | 12 |
| `READ_ARGMAX` request + response | 18 |
| **Total** | **102** |

Versus 292 for full readout. At 921600 baud a 400-point sweep over 1,000 images runs in
about **7.4 minutes**, against 2.8 hours for the naive path. This is the number that makes
week 9 feasible.

Keep full `READ_RESULT` — the equivalence tests need raw accumulators, and any plot that
is not a classification metric needs the actual values.

---

## 7. Register and command additions

| CMD | Name | Payload | Response |
|---|---|---|---|
| `0x08` | `READ_ARGMAX` | — | 10 bytes (§6.1) |

`STATUS` (`0x0A`) gains the following; the normative layout is `protocol.md` §6.4:

| Bits | Field |
|---|---|
| `[3:0]` | FSM state |
| `[4]` | busy |
| `[5]` | last error |
| `[6]` | `results_valid` |
| `[15:8]` | `result_seq` |

---

## 8. Resource estimate

| Element | LEs | M9K |
|---|---|---|
| Result M9K + write address | 25 | 1 |
| Argmax comparator + index | 70 | 0 |
| Second-place comparator | 70 | 0 |
| Readout FSM + byte serializer | 90 | 0 |
| `result_seq`, `results_valid` | 15 | 0 |
| **Total** | **~270** | **1** |

Design running total: see `top-level-spec.md` §9 (the whole-design rollup at 64 lanes).

---

## 9. Verification

1. **Drain-to-buffer mapping.** Distinct known value per column; confirm each lands at the
   correct buffer address. An off-by-one in `drain_cnt` shifts every column by one; a
   reversed shift chain mirrors them. Both are silent in aggregate metrics.
2. **Multi-pass addressing.** Confirm pass 1 writes to addresses 32–63 and does not
   overwrite pass 0. Use values that make ordering errors obvious.
3. **Argmax against NumPy.** 10,000 random result vectors, RTL against `numpy.argmax`,
   including deliberately constructed ties. Exact match on both index and value.
4. **Argmax masking.** Set `active_cols` to a non-multiple of 32 (e.g. 48), leave garbage in
   columns 48–63, confirm argmax ignores them. Directed test for §5.1.
5. **Second-place correctness.** Confirm `second_val` against a sorted reference, including
   the case where the top two values are equal.
6. **Byte order.** Read a known result through the full serializer and unpack with
   `struct.unpack('<i')` in Python. Include negative values — a sign error only shows on
   those.
7. **Persistence and retry.** Compute, read results, read again without recomputing,
   require identical output. Then confirm `result_seq` did not change.
8. **Stale detection.** Compute, discard the response, compute again, and confirm
   `result_seq` advanced by two so the host can detect the gap.
9. **Range checking.** `READ_RESULT` with `start + count > TILE_COLS` must return
   `ST_ADDR_RANGE`, not wrapped data.

---

## 10. Open questions for the freeze meeting

1. **Should `READ_ARGMAX` also return `results_valid` and `result_seq`?** They are in
   `STATUS`, but including them costs 3 bytes and saves a round trip per image during
   sweeps — roughly 3% of sweep time. Marginal; decide on driver ergonomics.
2. **Top-k rather than top-2?** Top-5 accuracy is a standard metric and would need a small
   sorting network. Probably not worth it for a 10-class digit task, but if the demo grows
   to a larger label set it becomes relevant.
3. **Should the buffer hold results from the previous compute as well?** Two banks would let
   the host compare consecutive runs on-chip — e.g. the same input with noise on and off,
   returning only the difference. One extra M9K, and it would cut noise-sweep traffic
   further. Interesting, but the current numbers do not justify it.
4. **Does `CTRL[1]` need to physically zero the buffer,** or is `results_valid` sufficient?
   Zeroing costs a 64-cycle sweep state in the FSM. The flag is probably enough, but
   zeroing makes debugging less confusing when stale data appears in a waveform.
