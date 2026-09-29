# FeCIM Host Protocol

**Version:** 2
**Status:** FROZEN as of week 1. Changes require agreement from all four members and a
version bump.

This document is the contract between the host (Lane D) and the board (Lane A). It defines
what bytes cross the wire and what they mean. It says nothing about how either side is
implemented — parser internals, FSM states, and pipeline structure live in the component
specs and may change freely, provided the bytes on the wire do not.

---

## 1. Physical layer

| Property | Value |
|---|---|
| Interface | UART, CP2102/CH340 dongle on the Arduino header |
| FPGA RX | `ARDUINO_IO[0]`, `PIN_AB5`, 3.3-V LVTTL |
| FPGA TX | `ARDUINO_IO[1]`, `PIN_AB6`, 3.3-V LVTTL |
| Framing | 8N1 — 1 start, 8 data LSB-first, 1 stop, no parity |
| Baud on reset | 115200 |
| Baud, runtime | settable via `REG_BAUD_INC` |

---

## 2. Byte order

**Every multi-byte field is little-endian, at every layer**, including `LEN`, addresses,
config values, and result words. This matches `struct` format `'<'` on the host, so no
byte swapping is ever required.

Endianness disagreement is the most common integration failure on a project like this, and
it presents as plausible-looking garbage rather than an obvious error. The golden fixtures
in §9 exist to catch it on day one.

---

## 3. Frame format

```
Host → FPGA:   [0xA5][CMD:1][LEN:2][PAYLOAD:LEN][CRC:1]
FPGA → Host:   [0x5A][STATUS:1][LEN:2][PAYLOAD:LEN][CRC:1]
```

| Field | Notes |
|---|---|
| Sync | `0xA5` inbound, `0x5A` outbound |
| `LEN` | Payload length in bytes, little-endian, excludes header and CRC |
| `CRC` | CRC-8 over `CMD`/`STATUS`, `LEN`, and `PAYLOAD`. **The sync byte is excluded.** |

### 3.1 CRC-8

Polynomial `0x07` (CRC-8/ATM), init `0x00`, no reflection, no final XOR.

Reference implementation for cross-checking:

```python
def crc8(data: bytes) -> int:
    crc = 0x00
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc
```

### 3.2 Limits

| Constant | Value |
|---|---|
| `MAX_PAYLOAD` | 512 bytes |
| Inter-byte timeout | 100 ms (5,000,000 cycles at 50 MHz) |

A packet declaring `LEN > MAX_PAYLOAD` is rejected at the length field, without waiting for
the payload.

---

## 4. Commands

| CMD | Name | Request payload | Response payload |
|---|---|---|---|
| `0x01` | `WRITE_WEIGHTS` | `[addr:2][data:N]` | — |
| `0x02` | `WRITE_ACT` | `[addr:2][data:N]` | — |
| `0x03` | `SET_CONFIG` | `[reg:1][value:4]` | — |
| `0x04` | `COMPUTE` | — | — |
| `0x05` | `READ_RESULT` | `[start:2][count:2]` | `count × 4` bytes |
| `0x06` | `IDENTIFY` | — | 16 bytes, §4.5 |
| `0x07` | `GET_CONFIG` | `[reg:1]` | `[value:4]` |
| `0x08` | `READ_ARGMAX` | — | 10 bytes, §4.6 |

**Every command produces a response**, including errors. The only silence is a timeout.

### 4.1 `WRITE_WEIGHTS`

Weight index is **column-major**: `addr = col × TILE_ROWS + row`, `int8` signed values.

Chunk transfers at **256 data bytes** per packet. Bulk writes stream to memory as they
arrive, so a CRC failure leaves that chunk partially written — the host re-sends the chunk
and the correct values overwrite the corrupt ones. Chunking bounds the cost of a retry.

A full 128×128 tile is 16,384 bytes in 64 chunks.

### 4.2 `WRITE_ACT`

`addr` is the row index, values are `uint8`. The host always writes the full vector for the
configured row count; partial writes leave stale entries.

### 4.3 `COMPUTE`

No payload. **Responds on completion**, not on acceptance — so `compute()` is naturally
blocking with no polling. Returns `ST_BUSY` if a compute is already running.

### 4.4 `READ_RESULT`

`start` and `count` are in **words**, not bytes. Each result is `int32` little-endian, so
the response is `count × 4` bytes. `MAX_PAYLOAD` allows 128 words per packet, which covers
a full 128-column tile in one packet.

Returns `ST_ADDR_RANGE` if `start + count > TILE_COLS`.

Results persist until the next `COMPUTE`, so a retried read is always safe.

### 4.5 `IDENTIFY` response

16 bytes, little-endian:

| Offset | Width | Field |
|---|---|---|
| 0 | 2 | Magic `0xFEC1` |
| 2 | 1 | Protocol version (currently `0x02`) |
| 3 | 1 | RTL build ID |
| 4 | 2 | `NUM_LANES` |
| 6 | 2 | `TILE_ROWS` |
| 8 | 2 | `TILE_COLS` |
| 10 | 2 | UART framing error count |
| 12 | 2 | CRC error count |
| 14 | 2 | Timeout count |

**The driver calls this on connect and refuses to proceed on a version or magic mismatch.**
That turns "the numbers are wrong" into "your bitstream is stale," which is a much faster
diagnosis.

The three error counters are cumulative since reset and make bring-up debuggable without a
scope: CRC errors climbing while framing errors stay at zero means the problem is framing
logic, not baud rate.

### 4.6 `READ_ARGMAX` response

10 bytes, little-endian:

| Offset | Width | Field |
|---|---|---|
| 0 | 2 | `max_idx` — winning column index |
| 2 | 4 | `max_val` — `int32` |
| 6 | 4 | `second_val` — `int32`, runner-up |

Ties resolve to the **lowest index**, matching `numpy.argmax`. With coarse ADC settings
ties are common, so both sides must apply this rule identically.

If `active_cols == 1`, `second_val` is undefined and the host must not use it.

---

## 5. Status codes

| Code | Name | Meaning |
|---|---|---|
| `0x00` | `ST_OK` | |
| `0x01` | `ST_CRC_ERR` | CRC mismatch |
| `0x02` | `ST_BAD_LEN` | `LEN > MAX_PAYLOAD`, or wrong length for this command |
| `0x03` | `ST_UNKNOWN_CMD` | Unrecognized opcode |
| `0x04` | `ST_BUSY` | Compute in progress |
| `0x05` | `ST_ADDR_RANGE` | Address or range outside the configured tile |

---

## 6. Config register map

All registers are 32 bits, accessed by index via `SET_CONFIG` and `GET_CONFIG`.

| Addr | Name | Access | Reset | Description |
|---|---|---|---|---|
| `0x00` | `CTRL` | W | — | `[0]` soft reset, `[1]` clear results, `[2]` reseed LFSRs |
| `0x01` | `TILE_CFG` | RW | full tile | `[15:0]` active rows, `[31:16]` active cols |
| `0x02` | `QUANT_LEVELS` | RW | 255 | Conductance levels N, range 2–255 |
| `0x03` | `D2D_SIGMA` | RW | 0 | Device-to-device variation, 9 bits |
| `0x04` | `READ_SIGMA` | RW | 0 | Cycle-to-cycle read noise, 9 bits |
| `0x05` | `NOISE_SEED` | RW | — | Global seed; 0 is remapped internally |
| `0x06` | `NOISE_EN` | RW | 0 | `[0]` quant `[1]` d2d `[2]` read `[3]` stuck `[4]` IR `[5]` adc |
| `0x07` | `ADC_BITS` | RW | 32 | Output resolution, 4–32 |
| `0x08` | `STUCK_RATE` | RW | 0 | `[15:0]` rate out of 65536, `[16]` mode |
| `0x09` | `BAUD_INC` | RW | 2416 | UART fractional divider increment |
| `0x0A` | `STATUS` | R | — | See §6.4 |
| `0x0B` | `CYCLE_CNT` | R | — | Cycles taken by the last `COMPUTE` |
| `0x0C` | `QUANT_MULT` | RW | — | `round(255·256/(N−1))`, Q8.8 |

### 6.1 Reset is an ideal crossbar

With `NOISE_EN = 0` the datapath computes exact integer matrix-vector products, bit-identical
to a NumPy `int32` `matmul`. If the first result after programming is wrong, the problem is
the datapath, not a leftover noise setting.

### 6.2 Paired registers

`QUANT_LEVELS` and `QUANT_MULT` must be written together and must stay consistent. The
driver exposes a single `set_quantization(n_levels)` that writes both; they are never set
independently.

### 6.3 Sigma scaling

Both sigma registers are unsigned, 9 bits used. The resulting noise standard deviation in
weight LSBs:

| Register | σ in LSB | Register value for a target σ |
|---|---|---|
| `D2D_SIGMA` | `0.408 × reg` | `round(2.450 × σ)` |
| `READ_SIGMA` | `0.144 × reg` | `round(6.93 × σ)` |

Converting a device measurement to LSB: `σ_LSB = 255 × σ_Vth / MW`, where `MW` is the
memory window in volts. Derivation in `docs/device-model.md`.

### 6.4 `STATUS` layout

| Bits | Field |
|---|---|
| `[3:0]` | Control FSM state |
| `[4]` | Busy |
| `[5]` | Last error |
| `[6]` | `results_valid` |
| `[15:8]` | `result_seq` — increments on each completed `COMPUTE` |

`result_seq` lets the host detect a dropped `COMPUTE` response. Without it, a lost response
followed by a retry can return results belonging to the previous input — a plausible wrong
answer for one image in several thousand, invisible in testing and fatal to reproducibility.

### 6.5 `ADC_BITS` full scale

The ADC quantizes over the accumulator's **used** range, not its container width:

```
ACC_USED_W = ceil(log2(254 × 255 × TILE_ROWS)) + 1      # 24 at TILE_ROWS = 128
shift      = ACC_USED_W - ADC_BITS
```

The host derives `ACC_USED_W` from `TILE_ROWS` reported by `IDENTIFY`. Quantization is
round-to-nearest. `ADC_BITS = ACC_USED_W` is an exact no-op.

---

## 7. Error handling and retry

The host runs a strict request/response loop: send, wait for a response or a read timeout,
retry on either.

**After any error or timeout, the host waits one full inter-byte timeout period (100 ms)
before retransmitting.** This is a protocol requirement, not an implementation detail. It
guarantees the board's parser has returned to its idle hunt state and the line is quiet —
otherwise the host races a parser still consuming the previous packet's phantom payload.

Recommended retry limit: three attempts, then surface the error.

Because `0xA5` occurs naturally inside payload data, sync detection alone cannot find frame
boundaries. Recovery relies on a false sync failing CRC or timing out, after which the
parser resynchronizes. The host-side wait above is what makes this converge.

---

## 8. Versioning

`IDENTIFY` reports the protocol version. The driver refuses to operate on a mismatch rather
than attempting compatibility.

Bump the version for any change to frame format, command opcodes, payload layouts, status
codes, or register addresses. Adding a new command at an unused opcode, or a new register at
an unused address, does **not** require a bump — older hosts simply never use them.

---

## 9. Golden fixtures

`tb/golden/` holds hand-computed byte sequences with hand-computed CRCs, one file per
command, committed to the repo rather than generated at test time.

**Both the C++ testbench and the Python driver tests assert against the same files.** Either
side drifting from this document fails immediately, in both places, with an obvious diff —
instead of surfacing in week 4 as results that look almost right.

Minimum fixture set for the week-1 freeze:

- `identify_request.hex` / `identify_response.hex`
- `set_config_quant.hex` — a `SET_CONFIG` write with a known value
- `write_weights_chunk.hex` — a 256-byte chunk with a known address
- `read_result_response.hex` — including at least one negative value, which is the only
  case that catches a sign error
- `crc_vectors.csv` — input bytes and expected CRC over a range of lengths
