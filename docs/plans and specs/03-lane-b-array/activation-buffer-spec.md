# FeCIM Component Spec: Activation Buffer

**Module:** `act_buffer`
**Owner:** Lane A (RTL)
**Depends on:** `packet_parser` (bulk stream), `control_fsm` (`act_addr`)
**Feeds:** all lanes via broadcast
**Milestone:** Week 4
**Status:** draft

---

## 1. Purpose and scope

Holds the input vector `a[0..R-1]`. During `COMPUTE` the sequencer reads one element per
cycle and broadcasts it to all 32 lanes simultaneously.

Small module, three real design questions: what it is made of, how it stays aligned with
the weight read, and whether it should hold more than one vector.

---

## 2. Activations are unsigned, and that is physical

`ACT_W = 8`, **unsigned**.

In a real crossbar the activation is the voltage applied to a wordline. Voltage magnitude
is what drives current through the cell, and a negative activation would require either
negative supply rails or a second differential scheme layered on top of the one already
used for signed weights. Real designs avoid this.

It also happens to match ReLU networks exactly — post-ReLU activations are non-negative
by construction. The physical constraint and the algorithmic reality agree, which is worth
one sentence in the report.

**The first layer is the exception.** Normalized pixel inputs are typically signed. Handle
this on the host: shift inputs to unsigned and fold the correction into the layer's bias
term. This is standard practice in quantized inference and costs nothing in hardware.
Document it in the weight-mapping tool so it does not look like an oversight.

---

## 3. M9K, not registers

64 × 8 bits is 512 bits. As registers that is ~512 LEs; as an M9K it is one block out of
182, with the rest of the block spare for growth to 128 or 256 rows.

The stronger argument is **latency matching**. Weights come out of M9K one cycle after the
address is presented. If activations came from registers they would be available
combinationally, and someone would have to insert a delay register to realign them —
another place for the pipeline to drift out of step. Reading both from M9K makes alignment
automatic and free.

Use M9K. Configure as 1024×8 like the lane weight memories, so the two instantiations
match.

---

## 4. Pipeline alignment — the authoritative version

Earlier specs described the lane pipeline as "BRAM read → noise inject → multiply →
accumulate" without pinning down which operations share a cycle. That ambiguity has to be
resolved here, because `LANE_PIPE_DEPTH` depends on it and the control FSM's hold count
depends on that.

| Cycle | Weight path | Activation path |
|---|---|---|
| `N` | `seq_cnt` → lane M9K address | `seq_cnt[5:0]` → act M9K address |
| `N+1` | M9K output → alignment register | M9K output → broadcast register |
| `N+2` | `+ LFSR read noise`, then multiply (shared cycle), result registered | broadcast valid at all lanes |
| `N+3` | accumulate | — |

The last address is issued at cycle `N`; the accumulator holds its final value at `N+3`.

**`LANE_PIPE_DEPTH = 3`** — the control FSM holds in `ACC` for three cycles after issuing
the last row. The value in `fecim_pkg.sv` is correct; this table is now the definition it
refers to.

### 4.1 The alignment register on the weight path

The activation needs a register after the M9K for fanout (§5). The weight does not — each
lane reads its own local memory with no fanout problem.

**Add the register to the weight path anyway.** It costs 32 × 8 = 256 flip-flops and keeps
the two paths structurally symmetric. Without it the activation is one cycle behind the
weight and every lane needs a compensating delay, which is the same flip-flops arranged
in a way that is easier to get wrong.

### 4.2 The timing risk in cycle N+2

Cycle `N+2` does an 8-bit add and a 9×9 multiply in one 20 ns period. On a `C7` speed
grade this should close — the DSP block contributes roughly 5–6 ns and an 8-bit adder
about 3–4 ns plus routing — but it is the tightest path in the design and the first place
to look if timing fails.

**Fallback if it does not close:** register between the noise add and the multiplier,
making `LANE_PIPE_DEPTH = 4`. Because the FSM reads the depth from the package, that fix
is a one-line change with no other edits. This is precisely why the constant lives in
`fecim_pkg.sv` rather than being written as a literal `3` in the sequencer.

---

## 5. Broadcast fanout

The activation output drives all 32 lanes — 8 bits × 32 loads on a net that spans the
array physically. Register it and let Quartus duplicate the register during fitting; the
tool handles this well when the source is a plain register with high fanout.

Watch this when scaling to 64 lanes. If it becomes a problem, the fix is a small fanout
tree — one register per group of 16 lanes, adding a pipeline stage — but do not build that
speculatively for 32 lanes.

---

## 6. Write path

Activations arrive from the parser with `bulk_target == TGT_ACT` and are written
**directly to the M9K with no transform**. Activations are drive voltage, not stored
conductance; none of the write-path device effects apply. See write-path spec §1.2 and its
test 9.

### 6.1 Stale data

If `active_rows` is reduced mid-session, entries above the new limit are never read, so
stale values are harmless. If it is later increased without a fresh `WRITE_ACT`, stale
values from an older vector are silently included.

Two defenses, use both: `CTRL[1]` zeroes the activation buffer, and the Python driver
always writes the full vector for the configured row count. The driver-side rule is the
one that actually matters; the hardware bit is for interactive debugging.

---

## 7. Batching: analyzed and rejected

The obvious extension is storing many activation vectors on-chip so the host can upload a
batch and trigger many computes. Working the numbers shows this is the wrong optimization,
and the arithmetic is worth recording because it points at the right one.

### 7.1 Per-image cost, single-vector mode at 115200

| Transaction | Bytes |
|---|---|
| `WRITE_ACT` (6 framing + 2 addr + 64 data) | 72 |
| `COMPUTE` request + response | 12 |
| `READ_RESULT` request | 10 |
| `READ_RESULT` response (6 + 64×3) | 198 |
| **Total** | **292** |

At 115200 8N1 that is 11,520 bytes/s, so **25.3 ms per image**. A 1,000-image test set
takes 25 s, and a 20×20 two-dimensional sweep takes **2.8 hours**.

That is a genuine schedule risk for week 9, and it is worth knowing in week 4 while the
memory map is still open.

### 7.2 Where the bytes actually are

The result readout is 198 of 292 bytes — **68% of the traffic**. Batching activations does
nothing about it, because batching reduces per-transaction overhead while the payload
stays the same size.

Compare the three fixes on a 400-point sweep of 1,000 images:

| Configuration | Bytes/image | 400-point sweep |
|---|---|---|
| Baseline, 115200 | 292 | 2.8 h |
| Argmax readout, 115200 | 101 | 58 min |
| Argmax readout, 921600 | 101 | **7.3 min** |
| Argmax + 921600 + 64-image batching | 67 | 4.9 min |

**Argmax readout plus a higher baud rate gets a 23× speedup. Adding batching on top buys
another 1.5× for four more M9Ks, a batch index in the command set, a result buffer
holding many outputs, and a more complex sequencer.**

Not worth it. Keep the activation buffer as a single vector.

### 7.3 The actual recommendation

Add a `READ_ARGMAX` command returning the index of the largest accumulator — one byte
instead of 192. On-chip argmax is a 24-bit comparator and an index register in the drain
shift chain, roughly 60 LEs, since the results already stream past one per cycle during
`DRAIN`.

This belongs in the **result buffer / readout spec**, not here. Flagging it in this
document because the bandwidth analysis that motivates it fell out of the activation
buffer design, and because the register map needs room for the command now.

Keep full `READ_RESULT` as well — you need raw accumulator values for the model
equivalence tests and for any plot that is not a classification accuracy number.

---

## 8. Resource estimate

| Block | LEs | M9K |
|---|---|---|
| Activation M9K + control | 40 | 1 |
| Broadcast register + fanout | 20 | 0 |
| Address decode | 10 | 0 |
| **Total** | **~70** | **1** |

Running design total: ~2,450 LEs, 2 M9K, 7 multipliers — about 5% of the 10M50 before the
lane array.

---

## 9. Verification

1. **Write-read equivalence.** Write all 64 activations across the full 0–255 range, read
   back via a debug path or by computing against an identity matrix. Exact match required.
2. **No transform applied.** With every `NOISE_EN` bit set, write activations and confirm
   exact recovery. Directed test for §6; duplicates write-path test 9 deliberately,
   because the two modules could each pass in isolation while the routing between them is
   wrong.
3. **Pipeline alignment.** Load a one-hot activation vector (`a[k] = 1`, rest 0) and an
   identity weight matrix. The result must be non-zero in exactly column `k`. Sweep `k`
   across all 64 positions — an off-by-one in either the weight or activation path shifts
   the non-zero column and this test localizes it immediately.
4. **Last-row inclusion.** One-hot at `k = 63` specifically. If the FSM's `LANE_PIPE_DEPTH`
   hold is short, this row is dropped and the result is all zeros. Pairs with control FSM
   test 2.
5. **Broadcast integrity.** A weight matrix that is all ones makes every column's result
   equal the sum of activations. All 32 lanes must agree. Catches a broken or skewed
   fanout net, which would otherwise show up as a few columns being subtly wrong.
6. **Stale data.** Write a full vector, reduce `active_rows`, restore it without
   rewriting, and confirm the driver-side rule prevents stale contributions. Documents
   the §6.1 hazard as much as it tests it.

---

## 10. Open questions for the freeze meeting

1. **Reserve address space for batching anyway?** The analysis rejects batching, but
   reserving upper bits in the activation address costs nothing now and avoids a
   protocol change if week 9 needs it. Recommend reserving.
2. **Should `READ_ARGMAX` return the value as well as the index?** Five bytes instead of
   one, and it gives you classification confidence for free — useful if you want to show
   that noise degrades confidence before it degrades accuracy, which is a more interesting
   plot than accuracy alone.
3. **Is the 921600 baud path validated?** The sweep-time analysis assumes it works. Add it
   to the week-3 gate rather than discovering in week 9 that the cable will not carry it.
