# tb/golden/adc

Golden vectors for the shared ADC stage at the drain-chain output (mac-array-spec §9).
Owned by Lane C. Consumed by Lane B's `adc_quant` testbench and by
`model/tests/test_read_path_vectors.cpp`.

```
sh      = adc_bits >= ACC_USED_W ? 0 : ACC_USED_W - adc_bits        // ACC_USED_W = 24
adc_out = (noise_en.adc && sh != 0) ? ((acc + (1 << (sh-1))) >>> sh) <<< sh : acc
```

## Files

| File | What |
|---|---|
| `cases.hex` | 1,330 cases, 4 words each: `acc  adc_bits  adc_enable  expected`. |
| `adc_vectors.svh` | Array size and field offsets. |

The cases are:

- For every `adc_bits` from 0 to 31: 0, ±1, ±`ACC_MAX_MAG`, and values at
  half-step − 1, half-step, half-step + 1, step − 1, step and 1.5 steps, both signs. Then
  24 random values in range.
- Out-of-range accumulators (`0x7FFFFFFF`, `0x80000000`, …) at 0, 4, 8 and 23 bits. These
  pin the RTL's 32-bit wrapping add. The driver never produces them.
- 64 cases with the ADC disabled and random `adc_bits`: the output must equal the input.

## Running them

`adc_quant` is combinational. Drive `acc_in`, `adc_bits = cases[k].adc_bits[4:0]`,
`noise_en.adc = adc_enable`, and check `adc_out`. Exact.

## Conventions the RTL must match

- **Round half up, both signs.** `+half` then an arithmetic shift: at 20 bits, 8 → 16
  but −8 → 0, and −9 → −16.
- **Full scale is `ACC_USED_W`, not `ACC_W`.** `adc_bits ≥ 24` is an exact no-op, not a
  negative shift.
- **`adc_bits` is 5 bits.** The port is `[4:0]`, so a register value of 32 arrives as 0
  (one bit) and 40 as 8. The verification matrix lists "32 bits" as a sweep point: on
  this hardware that is the 0-bit case. Use 24 to 31 for "ADC off by resolution".
  `mvm/` case `adc32_reaches_port_as_0` pins this end to end.
- **32-bit wrap.** `acc + half` wraps like the RTL adder; nothing saturates.

## Mutation check

These vectors and the `mvm/` replay caught three of the four ADC mutations of the model:

- quantizing over `ACC_W` (32) instead of `ACC_USED_W`;
- truncating instead of rounding;
- the ADC enable ignored.

The fourth, dropping the 5-bit mask inside the C++ `adc_quantize`, is caught only by the
unit test `adc_uses_only_five_bits_of_the_register`. That mask guards the C++ API
(`model_exact` masks before calling), and these vectors carry the 5-bit port value,
which is all the RTL can see.

## Regenerating

See `tb/golden/read_noise/README.md`: one generator,
`tb/golden/gen_read_path_vectors.py`, writes all four read-path folders.
