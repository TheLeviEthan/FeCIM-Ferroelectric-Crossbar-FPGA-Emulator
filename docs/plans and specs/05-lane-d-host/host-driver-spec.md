# FeCIM Component Spec: Host Driver and API

**Module:** `sw/fecim/` — `transport.py`, `codec.py`, `backends/`, `crossbar.py`
**Owner:** Lane D (Natalie Poche)
**Depends on:** `docs/protocol.md` (frozen contract), `model_exact` via pybind11
**Milestone:** codec week 2, board communication week 3, dual backend week 4
**Status:** draft

---

## 1. Purpose and scope

Everything between a user's Python call and bytes on the wire: serial transport, packet
encode/decode, retry semantics, the dual-backend abstraction, the public API, and
packaging.

Does **not** cover weight mapping, the demo network, or sweep orchestration — those are in
`application-spec.md`.

### 1.1 The governing constraint

Every result this driver produces must be **bit-identical** to the board at a matched
seed, whichever backend is in use. That is only possible if the `sim` backend is the same
C++ `model_exact` the RTL was verified against — never an independent Python
reimplementation, which could drift from what was validated.

---

## 2. Layering

Four layers, each independently testable. The separation exists so that the codec can be
tested exhaustively with no hardware and no C++ build.

```
  crossbar.py      public API — units, validation, register pairing
       │
  backends/        FpgaBackend | SimBackend — same interface
       │
  transport.py     serial framing, timeouts, retry      (fpga only)
       │
  codec.py         pure functions, bytes in / bytes out — no I/O
```

**`codec.py` must contain no I/O and no state.** It is the layer the golden fixtures test,
and those fixtures are what catch protocol drift on day one.

---

## 3. Codec

```python
def encode(cmd: int, payload: bytes = b"") -> bytes: ...
def decode(frame: bytes) -> tuple[int, bytes]: ...      # (status, payload)
def crc8(data: bytes) -> int: ...
```

`encode` prepends `0xA5`, the command byte, and little-endian length, then appends CRC-8
over command, length, and payload — the sync byte is excluded.

`decode` validates the sync byte, recomputes the CRC, and raises rather than returning a
malformed result.

**Every multi-byte field uses `struct` format `'<'`.** No manual byte assembly anywhere;
`struct.pack('<H', addr)` rather than `bytes([addr & 0xFF, addr >> 8])`. Manual assembly is
where endianness bugs live.

Constants come from generated `protocol.py` where possible (see `rtl-conventions.md` §3.1),
not retyped.

---

## 4. Transport

### 4.1 Read exactly, never read-until-timeout

A response has a known structure, so read it in two stages:

```python
head = self._read_exact(4)              # sync, status, len_lo, len_hi
n = struct.unpack('<H', head[2:4])[0]
body = self._read_exact(n + 1)          # payload + CRC
```

Reading until timeout works but costs the full timeout on every transaction, which at
thousands of images per sweep is the difference between minutes and hours.

### 4.2 Timeouts

| Setting | Value | Reason |
|---|---|---|
| `pyserial` inter-byte timeout | 0.5 s | Must exceed the board's 100 ms parser timeout plus the longest response — a 128-word `READ_RESULT` is 518 bytes, about 45 ms at 115200 |
| Post-error settle | 0.1 s | **Protocol requirement**, `protocol.md` §7 |
| Retry attempts | 3 | Then raise |

### 4.3 The post-error wait is not optional

After any CRC failure, timeout, or bad sync, **sleep 100 ms before retransmitting.** This
guarantees the board's parser has returned to its idle hunt state and the line is quiet.
Retransmitting immediately races a parser still consuming the previous packet's phantom
payload, and the retry fails for a different reason than the original error.

```python
def _transact(self, cmd, payload=b""):
    for attempt in range(3):
        try:
            self.port.write(encode(cmd, payload))
            status, resp = decode(self._read_response())
            if status != ST_OK:
                raise status_to_exception(status)
            return resp
        except (CrcError, SerialTimeout, FramingError):
            self.port.reset_input_buffer()
            time.sleep(0.1)
    raise TransportError(f"command 0x{cmd:02x} failed after 3 attempts")
```

`reset_input_buffer()` matters — stale bytes from a partial response would corrupt the
retry.

### 4.4 Connect sequence

1. Open port at 115200 (the board's reset baud)
2. `IDENTIFY`
3. Verify magic `0xFEC1` and protocol version; **raise `VersionMismatch` on either**
4. Record `NUM_LANES`, `TILE_ROWS`, `TILE_COLS`
5. Derive `ACC_USED_W = ceil(log2(254 × 255 × TILE_ROWS)) + 1`
6. Optionally raise the baud rate via `BAUD_INC`

Refusing to operate on a version mismatch converts "the numbers are wrong" into "your
bitstream is stale," which is a far faster diagnosis than debugging a datapath that is
fine.

### 4.5 Sequence tracking

Read `result_seq` from `STATUS` after each `COMPUTE`. If it has not advanced by exactly
one, the compute did not happen or a response was lost — **raise, do not read results.**

Without this, a dropped `COMPUTE` response followed by a retry returns results belonging to
the previous input: one plausible wrong answer in several thousand, invisible in testing,
and enough to make a whole sweep irreproducible.

### 4.6 Exceptions

```
FecimError
├── TransportError          retries exhausted
├── CrcError
├── FramingError            bad sync byte
├── VersionMismatch         IDENTIFY mismatch
├── BoardError              a non-OK status
│   ├── BusyError           0x04
│   ├── AddressRangeError   0x05
│   ├── BadLengthError      0x02
│   └── UnknownCommandError 0x03
└── SequenceError           result_seq did not advance
```

---

## 5. Backend abstraction

```python
class Backend(Protocol):
    def identify(self) -> DeviceInfo: ...
    def set_config(self, reg: int, value: int) -> None: ...
    def get_config(self, reg: int) -> int: ...
    def write_weights(self, addr: int, data: bytes) -> None: ...
    def write_activations(self, addr: int, data: bytes) -> None: ...
    def compute(self) -> None: ...
    def read_results(self, start: int, count: int) -> np.ndarray: ...
    def read_argmax(self) -> ArgmaxResult: ...
```

`FpgaBackend` implements these over `transport`. `SimBackend` calls into `model_exact`
through pybind11.

**`SimBackend` must honour the same register semantics**, including reset defaults, the
`QUANT_LEVELS`/`QUANT_MULT` relationship, the `NOISE_EN` bit positions, and seed handling.
It is not a convenience mock — it is the same computation the RTL performs, and a
difference is a bug in one of them.

`SimBackend` should also report a modelled cycle count so throughput plots work without a
board. It cannot reproduce the 7.8 µs wall-clock time and should not pretend to.

### 5.1 Chunking belongs above the backend

`write_weights` takes bytes and an address; splitting a 16 KB tile into 256-byte chunks is
the API layer's job. Both backends then receive identical calls, which is what makes
cross-backend comparison meaningful.

---

## 6. Public API

The surface a user touches. Two principles: **physical units in, and no way to leave
dependent registers inconsistent.**

```python
from fecim import Crossbar

xbar = Crossbar(backend="fpga", port="/dev/ttyUSB0")
xbar = Crossbar(backend="sim")                          # no hardware needed

xbar.set_quantization(levels=8)
xbar.set_variation(sigma_lsb=12.8)
xbar.set_read_noise(sigma_lsb=3.0)
xbar.set_faults(rate=0.01, mode="zero")
xbar.set_adc(bits=6)
xbar.set_ir_drop(kappa=0.05)            # or set_ir_drop(coeffs=alpha_per_row)
xbar.enable(["quant", "d2d", "read"])
xbar.seed(0xACE1)

xbar.load_weights(W)                    # float array, quantized internally
y = xbar.matvec(x)                      # float array in, float array out
cls = xbar.classify(x)                  # ArgmaxResult: index, value, margin
```

### 6.1 Physical units, not register units

`set_variation(sigma_lsb=12.8)` rather than `set_config(0x03, 31)`. The driver applies the
conversion from `protocol.md` §6.3:

```python
D2D_SIGMA  = round(2.450 * sigma_lsb)
READ_SIGMA = round(6.93  * sigma_lsb)
```

Better still, accept device units directly, since that is what a user reading a paper
actually has:

```python
xbar.set_variation(sigma_vth_mv=50, memory_window_v=1.0)
# σ_LSB = 255 × 0.050 / 1.0 = 12.8
```

Both forms should exist. The register-unit escape hatch stays available as
`set_config()` for debugging, but nothing in the documented workflow uses it.

**Range-check and warn.** Both sigma registers use 8 bits (protocol §6.3): `D2D_SIGMA` tops out
at 255, σ ≈ 104 LSB, and `READ_SIGMA` at 255, σ ≈ 36.8 LSB. The two scale factors differ. Silently
clamping a requested value produces a sweep that flattens at the top for no visible reason.

### 6.2 Dependent registers are written together

`QUANT_LEVELS` and `QUANT_MULT` are mathematically linked. **There is no public setter for
either alone.**

```python
def set_quantization(self, levels: int) -> None:
    if not 2 <= levels <= 255:
        raise ValueError(f"levels must be 2..255, got {levels}")
    mult = round(255 * 256 / (levels - 1))
    self._backend.set_config(REG_QUANT_LEVELS, levels)
    self._backend.set_config(REG_QUANT_MULT, mult)
```

Letting them drift gives quantization to the wrong step size with no error anywhere.

### 6.3 `NOISE_EN` by name

```python
xbar.enable(["quant", "d2d"])        # not enable(0b000011)
```

Unknown names raise. Six bits with meaningful positions is exactly the kind of thing that
gets transposed when written as a literal, and the failure is silent.

### 6.4 ADC bits, not shift

`set_adc(bits=6)` computes `shift = ACC_USED_W - bits` using `ACC_USED_W` derived at
connect. Reject `bits` outside 4…`ACC_USED_W`, and note that `bits == ACC_USED_W` is an
exact no-op — useful as a sanity check.

### 6.5 IR drop

`set_ir_drop(coeffs)` takes one attenuation factor α per row in (0, 1] and writes
`min(255, round(256·α))` through `WRITE_ATTEN` (protocol §4.2b) — a 128-byte bulk write, not
128 register writes. `set_ir_drop(kappa=...)` is a convenience that builds the first-order
profile `α_i = 1 − κ·i/M` from `cell-physics-derivation.md` §4.5. Writing coefficients does
not enable the effect; `enable(["ir"])` does. `SimBackend` must keep the same per-row table.

### 6.6 Seeds

`seed(n)` writes `NOISE_SEED` and then pulses `CTRL[2]` so the LFSRs reload. 0 is a valid seed
and is not remapped (protocol §6.6); neither backend may special-case it.

---

## 7. Packaging

### 7.1 Maps onto M0's build requirement

M0 wants a source distribution buildable with a single command *and* binary distributions
for supported platforms. The C++/pybind11 setup provides both directly:

- **Source:** `pip install .` compiles the extension. Needs a C++17 compiler — present by
  default on Ubuntu and macOS, requires MSVC Build Tools on Windows. **Document this
  prominently in the README**; it is the one friction point.
- **Binary:** `cibuildwheel` in GitHub Actions produces wheels for Windows, Ubuntu, and
  macOS on every tagged release. An instructor with no compiler installs the wheel.

Say this mapping explicitly in the submission rather than letting it look incidental.

### 7.2 Dependencies

Runtime: `numpy`, `pyserial`. Build: `pybind11`, `scikit-build-core`.

Keep the C++ core dependency-free (C++17 standard library only). Eigen or Boost would turn
a one-command build into a support ticket.

`matplotlib` belongs in an optional `[plot]` extra — a user running inference should not
need a plotting library.

### 7.3 Graceful degradation

`import fecim` must succeed and `Crossbar(backend="fpga")` must work even if the C++
extension failed to build. Import `model_exact` lazily inside `SimBackend.__init__` and
raise a clear message there, so a missing compiler doesn't break board-based use.

---

## 8. Verification

Tests 1–3 need no hardware and no C++ build, so they run in CI from week 1.

1. **Golden fixtures.** Encode each command and compare byte-for-byte against
   `tb/golden/*.hex`. **Committed files, hand-computed, shared with Lane C's C++
   testbench.** A fixture generated by the encoder cannot catch the encoder being wrong.
2. **CRC vectors.** `crc8()` against `tb/golden/crc_vectors.csv` across a range of lengths.
3. **Round-trip.** `decode(encode(cmd, payload))` recovers the payload for random payloads
   up to `MAX_PAYLOAD`.
4. **Negative values.** `read_results` on a response containing negative `int32` values.
   The only test that catches a sign or endianness error in the result path.
5. **Loopback fake.** A fake serial object that echoes valid responses — exercises retry
   and timeout paths deterministically, before hardware exists.
6. **Error injection.** Fake port returns a bad CRC, a truncated frame, a wrong sync byte.
   Assert the correct exception and that the 100 ms settle occurred.
7. **Version mismatch.** Fake `IDENTIFY` with a wrong version raises `VersionMismatch`.
8. **Sequence detection.** Fake backend where `result_seq` fails to advance raises
   `SequenceError`.
9. **Register pairing.** `set_quantization(5)` writes both registers with correct values;
   confirm no public path sets one alone.
10. **Unit conversion.** `set_variation(sigma_vth_mv=50, memory_window_v=1.0)` produces
    `D2D_SIGMA = 31`. Directed test for §6.1.
11. **Cross-backend equivalence.** Same weights, same input, same seed, both backends —
    **bit-identical results required.** Needs hardware; this is the L4 gate.
12. **Clean install.** `pip install .` from a fresh checkout on Windows and Ubuntu, then
    run the demo on the `sim` backend. M0 build requirement.

---

## 9. Open questions

1. **Should `matvec` return float or raw `int32`?** Float is friendlier; raw accumulators
   are needed for equivalence tests and any non-classification plot. Suggest `matvec()`
   returns float with descaling applied, and `matvec_raw()` returns accumulators.
2. **Does the driver cache config state?** Caching avoids redundant writes during sweeps,
   but goes stale if anything else touches the board. Suggest cache with a
   `resync()` that re-reads everything via `GET_CONFIG`.
3. **Baud negotiation at connect.** Raising to 921600 requires talking at 115200 first,
   then re-opening the port. Worth automating, but needs a fallback if the higher rate
   fails — otherwise a marginal cable leaves the board unreachable.
4. **Should `SimBackend` simulate transport errors?** Useful for testing retry logic
   without hardware, but it makes the two backends non-identical by design. Probably keep
   it clean and test errors with the fake port from test 5.
