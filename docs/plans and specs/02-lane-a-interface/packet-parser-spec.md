# FeCIM Component Spec: Packet Parser and Response Generator

**Module:** `packet_parser`, `packet_tx`, `crc8`
**Owner:** Lane A (RTL), protocol frozen jointly with Lane C
**Depends on:** `uart_rx` / `uart_tx` (byte layer)
**Milestone:** Week 4
**Status:** draft — **freeze in week 1**, before Lane C writes the driver

---

## 1. Purpose and scope

Turns the byte stream from `uart_rx` into validated commands, and turns command results
back into a byte stream for `uart_tx`. Owns framing, length checking, CRC, timeout
recovery, and dispatch.

Does **not** own: the meaning of any command's payload beyond routing it, the config
register map's semantics, or anything about the MAC array.

This is the seam between all three work lanes. Lane C writes the host encoder against
this document, Lane B writes the equivalence tests against it, and Lane A implements it.
Changing it after week 1 costs three people a week.

---

## 2. Frame format

```
Host → FPGA:   [0xA5][CMD:1][LEN:2][PAYLOAD:LEN][CRC:1]
FPGA → Host:   [0x5A][STATUS:1][LEN:2][PAYLOAD:LEN][CRC:1]
```

- **Sync** — `0xA5` inbound, `0x5A` outbound. Distinct values make direction obvious on a
  logic analyzer. `0xA5` = `10100101`, chosen for its alternating pattern: it is unlikely
  to be produced by a stuck line or a half-shifted byte.
- **LEN** — payload length in bytes, **little-endian**, excludes header and CRC.
- **CRC** — CRC-8 over `CMD`, `LEN`, and `PAYLOAD`. The sync byte is excluded (it is a
  delimiter, not content).

### 2.1 Byte order

**Everything multi-byte is little-endian**, at every layer: `LEN`, config values, weight
addresses, result words. This matches the x86 host and Python's `struct` `'<'` prefix, so
no host-side swapping is ever needed.

Write this on the whiteboard. Endianness disagreement between the RTL and the driver is
the single most common week-4 integration bug, and it presents as plausible-looking
garbage rather than an obvious failure.

### 2.2 Size limits

```verilog
parameter MAX_PAYLOAD = 512;   // bytes
```

Any packet declaring `LEN > MAX_PAYLOAD` is rejected immediately at the `LEN1` state,
without waiting for the payload. `LEN` stays 16 bits for protocol headroom even though the
hardware enforces 512.

512 is chosen so a 128-column result readout (128 × 3 = 384 bytes) fits in one packet.
Weight transfers should use **256-byte chunks** — see §4.2.

---

## 3. Command set

| CMD | Name | Payload | Response payload | Class |
|---|---|---|---|---|
| `0x01` | `WRITE_WEIGHTS` | `[addr:2][data:N]` | — | bulk |
| `0x02` | `WRITE_ACT` | `[addr:2][data:N]` | — | bulk |
| `0x03` | `SET_CONFIG` | `[reg:1][value:4]` | — | control |
| `0x04` | `COMPUTE` | — | — | control |
| `0x05` | `READ_RESULT` | `[start:2][count:2]` | `count × 3` bytes | control |
| `0x06` | `IDENTIFY` | — | 16 bytes (see §3.1) | control |
| `0x07` | `GET_CONFIG` | `[reg:1]` | `[value:4]` | control |

### 3.1 `IDENTIFY` response

Sixteen bytes, fixed layout, little-endian:

| Offset | Width | Field |
|---|---|---|
| 0 | 2 | Magic `0xFEC1` |
| 2 | 1 | Protocol version |
| 3 | 1 | RTL build ID |
| 4 | 2 | `NUM_LANES` |
| 6 | 2 | `TILE_ROWS` |
| 8 | 2 | `TILE_COLS` |
| 10 | 2 | Framing error count (from `uart_rx`) |
| 12 | 2 | CRC error count |
| 14 | 2 | Timeout count |

The driver calls this on connect and refuses to proceed on a version mismatch, which
turns "the numbers are wrong" into "your bitstream is stale" — a much faster diagnosis.
The three error counters make the week-3 bring-up debuggable without a scope: if CRC
errors climb while framing errors stay at zero, the problem is your parser, not your
baud rate.

---

## 4. Payload handling: two classes

The core design decision. A 4 KB weight tile cannot be buffered on-chip before
validation, but a corrupted config write must never take effect. These need different
treatment, and conflating them produces either a memory-hungry design or a silent
failure mode.

### 4.1 Control commands — buffer and commit

Payload ≤ 8 bytes, held in a small register buffer. **No effect occurs until CRC
passes.** A corrupted `SET_CONFIG` is discarded whole; the host sees an error status and
retries.

This matters because config corruption is silent. A wrong noise sigma produces plausible
numbers that are simply wrong, and you would not discover it until a sweep looked
strange three weeks later.

### 4.2 Bulk commands — stream through

Payload is `[dest_addr:2]` followed by data bytes. The address is captured, then each
subsequent byte is emitted on the streaming interface with an auto-incrementing address
as it arrives. Nothing is buffered.

**CRC failure means the destination has already been partially written with bad data.**
That is acceptable, and deliberately so: weight and activation writes are idempotent, so
the host simply re-sends the chunk and the correct values overwrite the corrupt ones.

This is why bulk transfers are chunked at 256 bytes rather than sent as one 4 KB packet.
A CRC failure costs a 256-byte retry instead of a 4 KB one, and the host can retry a
single chunk without rebuilding the whole tile.

```
4 KB tile (64×64 × 8b) = 16 chunks × 256 bytes
Per chunk: [0xA5][0x01][0x02 0x01][addr:2][data:256][crc]
```

---

## 5. `packet_parser` state machine

```
        ┌──────────────────────────────────────────────┐
        │                                              │
        ▼                                              │
     ┌──────┐  rx=0xA5   ┌─────┐        ┌──────┐       │
     │ HUNT │───────────►│ CMD │───────►│ LEN0 │       │
     └──────┘            └─────┘        └──────┘       │
        ▲                                   │          │
        │                                   ▼          │
        │                               ┌──────┐       │
        │            LEN>MAX_PAYLOAD    │ LEN1 │       │
        │            ◄──────────────────└──────┘       │
        │                                   │          │
        │                                   ▼          │
        │                             ┌──────────┐     │
        │                             │ PAYLOAD  │◄─┐  │
        │                             └──────────┘  │  │
        │                                   │       │  │
        │                          count==LEN       │  │
        │                                   ▼       │  │
        │                               ┌─────┐     │  │
        │                               │ CRC │     │  │
        │                               └─────┘     │  │
        │                                   │       │  │
        │      CRC fail / timeout           ▼       │  │
        └───────────────────────────┌──────────┐    │  │
                                    │ DISPATCH │    │  │
                                    └──────────┘    │  │
                                          │         │  │
                                          ▼         │  │
                                    ┌──────────┐    │  │
                                    │ RESPOND  │────┴──┘
                                    └──────────┘
```

**Any state except `HUNT` can be aborted** by an inter-byte timeout, returning to `HUNT`.

### 5.1 Timeout — do not skip this

Without it, a `LEN` field corrupted from `0x0100` to `0xFF00` leaves the parser waiting
for 65,280 bytes that will never arrive. The link hangs and only a board reset recovers
it. Since `LEN` is covered by CRC but consumed *before* CRC is checked, this is not a
hypothetical.

```verilog
parameter TIMEOUT_CYCLES = 24'd5_000_000;   // 100 ms at 50 MHz
```

The counter resets on every received byte and only runs outside `HUNT`. On expiry:
increment the timeout counter, return to `HUNT`, send no response. The host's own read
timeout fires and it retries.

100 ms is ~1,150 byte-times at 115200 — generous enough never to trigger on a healthy
link, short enough that recovery is invisible to a human operator.

### 5.2 Resynchronization

`0xA5` will appear inside payload data — it is a valid weight value, and roughly one byte
in 256 will be one. Sync detection alone is therefore not sufficient to find frame
boundaries.

Recovery relies on the combination: a false sync produces a packet whose CRC fails
(probability 255/256) or whose length triggers a timeout, and the parser returns to
`HUNT` and tries again. This converges quickly in practice.

The host cooperates by **waiting one full timeout period after any error before
retransmitting**. This guarantees the parser is back in `HUNT` and the line is quiet,
rather than the host racing to send a new packet into a parser still consuming the
previous one's phantom payload. Document this in the driver — it is a protocol
requirement, not an implementation detail.

---

## 6. CRC-8

Polynomial `0x07` (`x⁸ + x² + x + 1`, CRC-8/ATM), init `0x00`, no reflection, no final
XOR. Computed byte-serially as each byte arrives, so no buffering is needed and the check
completes one cycle after the CRC byte.

```verilog
function [7:0] crc8_byte(input [7:0] crc, input [7:0] data);
    integer i;
    reg [7:0] c;
    begin
        c = crc ^ data;
        for (i = 0; i < 8; i = i + 1)
            c = c[7] ? ((c << 1) ^ 8'h07) : (c << 1);
        crc8_byte = c;
    end
endfunction
```

Unrolled to combinational logic, this is an XOR tree of roughly 30 LEs.

**Why CRC-8 is enough here.** The dominant error mode on a 15 cm cable at 115200 is not
line noise, it is parser desynchronization — and a desynced frame fails CRC-8 with
probability 255/256, same as any stronger polynomial. For genuine bit errors, CRC-8
catches all single-bit errors and all bursts up to 8 bits, which covers realistic UART
failure modes.

If you later run at 921600 over a longer cable and see CRC errors in the `IDENTIFY`
counters, upgrade to CRC-16-CCITT (poly `0x1021`, init `0xFFFF`) — about 20 more LEs and
one more byte per frame. Design the driver so this is a one-line change.

---

## 7. Interfaces

### 7.1 `packet_parser` ports

| Signal | Dir | Width | Description |
|---|---|---|---|
| `clk`, `rst_n` | in | 1 | |
| `rx_data` | in | 8 | from `uart_rx` |
| `rx_valid` | in | 1 | |
| **Bulk stream** | | | |
| `bulk_we` | out | 1 | one cycle per payload byte |
| `bulk_target` | out | 2 | `0`=weights, `1`=activations |
| `bulk_addr` | out | 16 | auto-incrementing |
| `bulk_data` | out | 8 | |
| **Config** | | | |
| `cfg_we` | out | 1 | pulse, after CRC passes |
| `cfg_addr` | out | 8 | |
| `cfg_wdata` | out | 32 | |
| `cfg_rdata` | in | 32 | for `GET_CONFIG` |
| **Control** | | | |
| `start_compute` | out | 1 | pulse |
| `compute_busy` | in | 1 | |
| **Readout** | | | |
| `rd_addr` | out | 16 | |
| `rd_data` | in | 24 | combinational or 1-cycle |
| **Response** | | | |
| `resp_*` | — | — | handshake to `packet_tx` |

### 7.2 Status codes

| Code | Meaning |
|---|---|
| `0x00` | OK |
| `0x01` | CRC error |
| `0x02` | Bad length (`> MAX_PAYLOAD`, or wrong for this command) |
| `0x03` | Unknown command |
| `0x04` | Busy (`compute_busy` asserted) |
| `0x05` | Address out of range |

**Every command produces a response**, including errors — except timeouts, which produce
silence by definition. This keeps the driver a simple request/response loop: send, wait
for a response or a read timeout, retry on either.

### 7.3 Busy handling

`compute_busy` is asserted for ~135 cycles (2.7 µs), while the next packet cannot arrive
for at least 86 µs at 115200. The busy path will never be exercised in normal operation.
Implement it anyway — it costs one state and one status code, and it stops being
unreachable the moment someone raises the baud rate or adds a streaming mode.

---

## 8. Resource estimate

| Block | LEs (est.) |
|---|---|
| CRC-8 combinational | 30 |
| Byte / length counters | 55 |
| Control payload buffer (8 B) | 70 |
| Parser FSM + dispatch | 60 |
| Timeout counter (24 b) | 50 |
| `packet_tx` FSM + mux | 80 |
| Error counters (3 × 16 b) | 55 |
| **Total** | **~400** |

Zero M9K, zero multipliers. Roughly 0.8% of the 10M50. Combined with the UART at ~140
LEs, the entire host interface is about 1% of the chip.

---

## 9. Verification

Lane B owns 1–6; Lane A owns 7–8. All of these run against the C++ reference model as
well, since the model must accept the same packets for the `sim` backend to be a genuine
drop-in.

1. **Golden packets.** A fixture file of hand-computed byte sequences with known-correct
   CRCs, shared between the RTL testbench and the Python driver tests. Both sides must
   agree byte-for-byte. This catches endianness disagreement immediately.
2. **CRC vector check.** RTL CRC-8 against a Python `crcmod` reference over 10,000 random
   payloads.
3. **Every command.** Correct dispatch, correct response, correct status for each of the
   seven commands.
4. **Corruption sweep.** For each byte position in a valid packet, flip one bit and
   confirm the parser rejects and recovers. Includes bit flips in `LEN`, which is the
   case that exercises the timeout path.
5. **Timeout.** Send a truncated packet, confirm the parser returns to `HUNT` after
   `TIMEOUT_CYCLES` and correctly accepts the next valid packet.
6. **False sync.** Send a bulk payload containing `0xA5` bytes and confirm the parser
   does not resynchronize mid-packet.
7. **Bulk streaming.** Write a full 4 KB tile in 16 chunks; read it back and compare.
   Then deliberately corrupt chunk 7 in transit, confirm the error status, retry that
   chunk alone, and confirm the tile is correct.
8. **Soak.** 100,000 random packets over hardware with error counters checked at the end.
   Run this overnight in week 4 — it finds the once-per-50,000 synchronizer bug that no
   directed test will.

---

## 10. Open questions for the freeze meeting

1. **`MAX_PAYLOAD` of 512 or 256?** 512 lets a 128-column readout come back in one packet;
   256 keeps buffers smaller and retries cheaper. Recommend 512, with the convention that
   writes chunk at 256 anyway.
2. **Should `GET_CONFIG` exist?** It is ~20 LEs and makes the driver able to verify what
   it set, which is worth it during calibration when you are sweeping parameters and need
   to be certain the board agrees with the host.
3. **Sequence numbers?** A one-byte counter would let the host detect a dropped response
   rather than inferring it from a timeout. Probably unnecessary given request/response
   lockstep, but decide now — retrofitting changes the frame.
4. **Does `COMPUTE` respond immediately or on completion?** Respond on completion. At
   2.7 µs the host cannot observe the difference, and it makes the driver's `compute()`
   naturally blocking with no polling loop.
