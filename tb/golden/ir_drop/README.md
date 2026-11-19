# tb/golden/ir_drop

Golden vectors for IR drop: the row-dependent attenuation applied to each activation
before it is broadcast to the lanes (mac-array-spec §8, activation-buffer-spec §6.2). Owned
by Lane C. Consumed by Lane B's `act_buffer` testbench and by
`model/tests/test_read_path_vectors.cpp`.

```
act_bcast = NOISE_EN.ir ? (act * atten[row]) >> 8 : act      // 8 × 8 unsigned, floor
```

## Files

| File | What |
|---|---|
| `atten.hex` | 576 `(act, coeff) → act_atten` entries: all edges (0, 1, 127, 128, 254, 255 against each other) plus random pairs. |
| `atten_checksum.hex` | One word: an order-dependent FNV-1a fold of `(act · coeff) >> 8` over all 65,536 pairs. Proves the multiplier exhaustively in one comparison. |
| `profiles.hex` | 7 whole-buffer profiles. Each has the IR enable, 128 coefficients, 128 activations and the 128 expected `act_bcast` values. |
| `ir_drop_vectors.svh` | Array sizes and field offsets. |

## Running them

**`atten.hex` and `atten_checksum.hex`.** Test the attenuation expression on its own.
For the checksum, fold in the same order the header comment gives (`act` outer, `coeff`
inner) and compare the one word.

**`profiles.hex`.** For each profile:

1. Write the 128 coefficients with `bulk_we`, `bulk_target = TGT_ATTEN`,
   `bulk_addr = row`.
2. Write the 128 activations with `bulk_target = TGT_ACT`.
3. Set `noise_en.ir` from the profile's enable word.
4. Step `act_addr` through 0 … 127 and check `act_bcast` against `bcast[row]` with the
   buffer's read latency (activation-buffer-spec §4).

The profiles are:

- the reset table (all 255) with IR on, then with IR off;
- linear ramps at κ = 0.25 and 0.5;
- random coefficients with IR on, then with IR off;
- a 0 / 1 / 128 / 255 cycle.

## Conventions the RTL must match

- **255 is not the identity.** The table resets to 255 (Q0.8 0.996), so turning IR on
  with an unwritten table already scales every activation by 255/256: 255 becomes 254,
  1 becomes 0. That is what `reset_coeffs_ir_on` checks.
- **Disabled means ignored.** With `NOISE_EN.ir` clear the coefficients must have no
  effect at all, whatever was written.
- **Floor, unsigned.** `>> 8` on the 16-bit product, no rounding.

## Spec inconsistency: §12 test 10 cannot pass as written

mac-array-spec §8 says attenuating the activation is "algebraically identical" to
attenuating each product, and §12 test 10 asks for bit-identical results against a
per-product reference. That holds for real numbers but not once the `>> 8` floor is
included: `w · ((a · k) >> 8)` and `(w · a · k) >> 8` differ by up to one `w` per row.
In a quick check of 300 random 128-row columns, 299 differed, by up to 1,276 accumulator
LSB.

The design is fine. Per-activation is what the RTL and model both do, and these vectors
define it. Test 10 should be rewritten to check against a reference that attenuates the
activation, or to bound the difference.

## Mutation check

These vectors, through the `mvm/` replay, caught both IR mutations of the model:
attenuation applied per product instead of to the activation, and the IR enable
ignored.

## Regenerating

See `tb/golden/read_noise/README.md`: one generator,
`tb/golden/gen_read_path_vectors.py`, writes all four read-path folders.
