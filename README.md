# FeCIM: Ferroelectric Crossbar FPGA Emulator

A digital RTL emulator of an analog ferroelectric (FeFET) compute-in-memory crossbar, built on a Terasic DE10-Lite (Intel MAX 10). It performs the same matrix-vector multiply a physical crossbar performs, with each device non-ideality injected into the datapath where it physically occurs and calibrated against published FeFET characterization data.

Computer Engineering capstone, University of Florida (CEN 4907C / 4908C), Fall 2026 – Spring 2027.

> **Status: early development (semester 1).** The architecture, protocol and component specs are frozen, the shared package and C++ reference model are under active development, and the RTL hierarchy is wired with skeleton modules. See [Current state](#current-state) for detail.

---

## Why

Analog crossbars compute a matrix-vector product in place, in the memory array, which avoids the data movement that bottlenecks von Neumann accelerators for neural-network inference. Real ferroelectric devices are imperfect: they hold a finite number of states, vary from cell to cell, read back noisily and sometimes fail stuck.

Device-aware crossbar *simulation* is a mature field. This project contributes two things on top of it:

- **A hardware implementation with independently switchable non-idealities.** Every effect can be enabled or masked on its own through the `NOISE_EN` register, so accuracy loss can be attributed to a specific physical mechanism instead of reported as one aggregate number.
- **Calibration across multiple published processes.** Running the same network against parameter sets from different fabs and nodes measures how much device variability alone changes accuracy. Every parameter traces to a citation.

## Architecture

```
   CP2102          ┌──────────────────────────────────────────────┐
   dongle          │  MAX 10                                      │
     │             │  ┌──────┐  ┌──────────┐  ┌───────────────┐   │
  TX─┼────────────►│  │ UART │─►│  Packet  │─►│  Control FSM  │   │
  RX◄┼─────────────│  │      │◄─│  Parser  │◄─│  + Config     │   │
     │             │  └──────┘  └────┬─────┘  └───┬───────┬───┘   │
                   │                 │            │       │       │
                   │          ┌──────▼──────┐ ┌───▼─────┐ │       │
                   │          │ Write Path  │ │ Act Buf │ │       │
                   │          │ quant / D2D │ │ + IR    │ │       │
                   │          │ / stuck-at  │ │ atten   │ │       │
                   │          └──────┬──────┘ └───┬─────┘ │       │
                   │                 │            │ a[r]  │       │
                   │  ┌──────────────▼────────────▼───────▼────┐  │
                   │  │        MAC LANE ARRAY (×64)            │  │
                   │  │   M9K → +noise → 9×9 → ACC32 → shift   │  │
                   │  └────────────────┬───────────────────────┘  │
                   │                   │                          │
                   │          ┌────────▼────────┐   ┌──────────┐  │
                   │          │ ADC → Result    │   │ HEX / LED│  │
                   │          │ Buffer + Argmax │   │ display  │  │
                   │          └─────────────────┘   └──────────┘  │
                   └──────────────────────────────────────────────┘
```

**Geometry.** 128×128 tile, 64 MAC lanes, 2 passes. Lane `l` computes column `p·64 + l` on pass `p`. One full matrix-vector multiply takes 392 cycles, **7.8 µs at 50 MHz** (derived from clear, issue, pipeline drain and readout cycles, not issue cycles alone).

**Non-idealities, organized by timescale.** Each effect is injected where it has to be recomputed:

| Timescale | Injection site | Effects |
|---|---|---|
| Permanent (fixed when a cell is written) | Write path | Conductance quantization, device-to-device variation, stuck-at faults |
| Per access | Lane read path | Cycle-to-cycle read noise (per-lane LFSR) |
| Per operation | Accumulation | IR drop, ADC quantization |

The reset state is an ideal crossbar (`NOISE_EN = 0`), so the clean datapath can be proven bit-exact before any noise is turned on.

**The 9-bit rule.** Each MAX 10 multiplier block is either one 18×18 or two 9×9 multipliers. Every multiply in the design is kept at 9×9 signed so two lanes share a block; that is what makes 64 lanes fit in half the device's 144 blocks. The consequences (9-bit operands, read noise clamped to ±127, 16-bit cell-hash rounds, triangular rather than Gaussian D2D variation) are documented in [`fecim_pkg.sv`](rtl/fecim_pkg.sv) and enforced by elaboration checks in [`fecim_pkg_checks.sv`](rtl/fecim_pkg_checks.sv).

**Projected resources (semester 1 target, 64 lanes):** ~18,300 LEs (37%), 66 M9K (36%), 72 multiplier blocks (50%).

## Current state

As of late October 2026:

| Area | Status |
|---|---|
| Plan, protocol, RTL conventions, component specs | **Done.** Plan rev 4; register map reconciled across specs, package, RTL and model |
| `rtl/fecim_pkg.sv` (shared parameters, types, protocol enums, hash/CRC functions) | **Done** (rev 4.1) |
| RTL top level (`fecim_top.sv`) | **Wired.** Full hierarchy instantiated, A/B seam connected |
| RTL leaf modules (UART, parser, FSM, write path, MAC lanes, result buffer, …) | **Skeletons.** Ports defined, outputs tied off; lint-clean under Verilator `-Wall` |
| C++ reference model | **In progress.** Primitives (cell hash, CRC-8, LFSR, lane seeding), `model_ideal`, and `model_exact` clean path + conductance quantization implemented; **41/41 unit tests pass**. Other non-idealities throw if enabled rather than silently returning clean results |
| Golden vectors, single `mac_lane` (clean datapath) | **Done.** 261 directed and random cases, cross-checked against `model_exact`, regeneration checked in CI |
| Device calibration | **In progress.** Parameter set A extracted (dual-port HfO₂ FeFET, Chatterjee et al., IEEE TED 2022); candidates for sets B and C identified |
| Verilator co-simulation harness | Not started |
| Python host package (`fpga` / `sim` backends) | Not started (parameter-set schema and data only) |
| Quartus project and pin constraints | Not yet in repo |

**Next milestones**

1. **Hardware gate:** UART loopback and `IDENTIFY` response from the board at 921600 baud.
2. **L0 co-simulation gate:** Verilated RTL and `model_exact` agree cycle for cycle on the clean datapath.
3. **MVP demo:** 64×64 tile, 32 lanes, quantization + D2D + stuck-at + read noise, digit classifier running on hardware.
4. **Semester 1 target:** full 128×128 tile with all six non-idealities, calibrated against at least one published parameter set.
5. **Semester 2:** multi-tile support, differential-pair modelling, retention drift, and a cross-process comparison across three published FeFET datasets.

## Repository layout

```
├── rtl/
│   ├── fecim_pkg.sv        single source of truth: parameters, types, encodings
│   ├── fecim_top.sv        top level, the only module that touches pins
│   ├── uart/ packet/ control/   host interface (Lane A)
│   └── array/ write/ result/    crossbar datapath (Lane B)
├── model/                  C++17 reference model, no dependencies (Lane C)
│   ├── include/fecim/      params, primitives, write path, model_ideal, model_exact
│   ├── src/  tests/
│   └── cosim/              Verilator co-simulation harness (planned)
├── tb/golden/              shared test fixtures, consumed by RTL, C++ and Python tests
├── sw/                     Python host package (Lane D)
│   └── fecim/parameter_sets/   one YAML per published device source
├── quartus/                Quartus project, constraints, reports
├── docs/plans and specs/   plan, protocol, per-lane specs, calibration notes
└── .github/workflows/ci.yml
```

## Building and testing

**C++ reference model** (requires CMake ≥ 3.16 and a C++17 compiler):

```bash
cmake -S model -B build
cmake --build build
./build/model_tests
```

**Golden vectors** (Python 3, standard library only):

```bash
python3 tb/golden/mac_lane/gen_mac_lane_vectors.py --check   # verify committed files are current
python3 tb/golden/mac_lane/gen_mac_lane_vectors.py           # regenerate
```

**RTL lint** (Verilator; the package must be listed first):

```bash
verilator --lint-only -Wall --top-module fecim_top \
  rtl/fecim_pkg.sv $(find rtl -name '*.sv' -not -name 'fecim_pkg.sv')
```

CI runs all of the above on every push and pull request. The workflow also defines co-simulation and Python test jobs; those are expected to fail until `model/cosim/` and the `sw/` package exist.

## Hardware setup

- **Board:** Terasic DE10-Lite, MAX 10 `10M50DAF484C7G`, single 50 MHz clock domain.
- **Serial link:** the DE10-Lite's USB port is JTAG only, so a CP2102 or CH340 USB-UART dongle connects to the Arduino header:

  | Dongle | DE10-Lite | FPGA pin |
  |---|---|---|
  | TXD | `ARDUINO_IO[0]` (FPGA rxd) | `PIN_AB5` |
  | RXD | `ARDUINO_IO[1]` (FPGA txd) | `PIN_AB6` |
  | GND | GND | — |

  > ⚠️ **The header is 3.3 V.** Many dongles ship jumpered for 5 V, which can damage a MAX 10 I/O pin. Check the jumper and meter the idle TX line before connecting.

- **On-board controls:** `KEY[0]` reset, `KEY[1]` triggers a compute without a host; state is shown on the HEX displays and LEDs.

## Host protocol

8N1 UART, 115200 baud on reset, raisable at runtime (up to 921600). All multi-byte fields are little-endian.

```
Host → FPGA:  [0xA5][CMD:1][LEN:2][PAYLOAD:LEN][CRC-8]
FPGA → Host:  [0x5A][STATUS:1][LEN:2][PAYLOAD:LEN][CRC-8]
```

CRC-8/ATM (poly `0x07`, init `0x00`) over everything except the sync byte. Commands: `WRITE_WEIGHTS`, `WRITE_ACT`, `WRITE_ATTEN`, `SET_CONFIG`, `GET_CONFIG`, `COMPUTE`, `READ_RESULT`, `READ_ARGMAX`, `IDENTIFY`. The full specification, including the config register map, is in [`protocol.md`](docs/plans%20and%20specs/01-contract/protocol.md).

The planned Python API exposes the board and the C++ model through the same interface, with bit-identical results at a matched seed:

```python
xbar = Crossbar(backend="fpga", port="/dev/ttyUSB0")   # real board
xbar = Crossbar(backend="sim")                          # C++ reference model via pybind11
```

## Team

| Lane | Area | Owner |
|---|---|---|
| A | Host interface RTL: UART, packet parser, control FSM, integration | Zhiting Li |
| B | Array RTL: write path, MAC lanes, result buffer | Megan Mendez |
| C | Model and verification: C++ reference model, co-simulation, calibration | Ethan Ruddell |
| D | Host stack and application: Python driver, sweep harness, calibration | Natalie Poche |

## Documentation

- [Project plan](docs/plans%20and%20specs/00-plan/fecim-plan.md): architecture, schedule, risk register
- [Host protocol](docs/plans%20and%20specs/01-contract/protocol.md) and [RTL conventions](docs/plans%20and%20specs/01-contract/rtl-conventions.md)
- [Hardware verification errata](docs/plans%20and%20specs/01-contract/hardware-verification-errata.md): design figures checked against the DE10-Lite and MAX 10 manuals
- Component specs: [Lane A](docs/plans%20and%20specs/02-lane-a-interface), [Lane B](docs/plans%20and%20specs/03-lane-b-array)
- [Model and verification design](docs/plans%20and%20specs/04-lane-c-verification/model-verification-design.md)
- [Host driver](docs/plans%20and%20specs/05-lane-d-host/host-driver-spec.md) and [application](docs/plans%20and%20specs/05-lane-d-host/application-spec.md) specs
- Calibration: [device model](docs/plans%20and%20specs/06-physics-calibration/device-model.md), [cell physics derivation](docs/plans%20and%20specs/06-physics-calibration/cell-physics-derivation.md), [literature survey](docs/plans%20and%20specs/06-physics-calibration/literature-survey.md)

## License

[MIT](LICENSE) © 2026 Ethan Ruddell
