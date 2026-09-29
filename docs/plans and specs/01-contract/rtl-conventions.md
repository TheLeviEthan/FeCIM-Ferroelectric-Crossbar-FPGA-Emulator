# FeCIM RTL Conventions

**Language:** SystemVerilog (IEEE 1800), synthesizable subset
**Applies to:** all files under `rtl/` and `tb/`
**Owner:** Lane A, with Lane B on the testbench side
**Status:** amends the UART, packet parser, and control FSM specs

---

## 1. Toolchain support, and the constraint it imposes

Quartus Prime Lite synthesizes a *subset* of SystemVerilog. The subset is generous enough
for a design this size, but it is worth knowing where the edges are before someone spends
an afternoon on a construct that will not fit.

### 1.1 Use freely

| Construct | Notes |
|---|---|
| `logic` | replaces both `reg` and `wire`; use everywhere |
| `always_ff` / `always_comb` | intent-declaring; Quartus checks them |
| `typedef enum` | for all state machines and protocol codes |
| packed `struct` | for bit-field registers |
| `package` / `import` | the single-source-of-truth mechanism — see §3 |
| `unique case` / `priority case` | synthesis hints plus simulation assertions |
| `int`, `bit`, `$clog2` | parameter arithmetic |
| `'0`, `'1`, `'x` | width-agnostic fills |
| `.*` and `.name` port connections | fewer positional-connection bugs |
| `for (int i = ...)` in `generate` | lane array instantiation |

### 1.2 Avoid

| Construct | Reason |
|---|---|
| `interface` / `modport` | supported but historically inconsistent in Lite; a design this small does not need it, and a synthesis surprise in week 4 is expensive |
| unpacked `struct` on ports | poor synthesis support |
| `always_latch` | you do not want latches; if one appears it is a bug |
| classes, `rand`, constrained random | simulation only, and see §2.2 on tool limits |
| `reg` / `wire` | not wrong, but mixing them with `logic` defeats the point |

### 1.3 Required style

- `always_ff` for every sequential block, `always_comb` for every combinational one.
  Never bare `always`. `always_comb` errors on incomplete assignment, which turns
  accidental latches into compile failures instead of silent hardware.
- Async assert, sync deassert reset: `always_ff @(posedge clk or negedge rst_n)`.
- One `always_ff` block may drive a given signal. `logic` enforces this — a second driver
  is a compile error rather than an X in simulation.
- Explicit enum encodings (as in `fecim_pkg.sv`), so the `STATUS` register and HEX display
  show documented values instead of synthesizer-chosen ones.
- Every `case` on an enum gets a `default`. Unassigned enum values propagate X in
  simulation and the failure appears far from its cause.

---

## 2. Simulation toolchain

The language choice constrains this, so decide it now.

### 2.1 Verilator for Lane B

**Icarus Verilog has weak SystemVerilog support and should not be used.** Verilator has
good coverage of the synthesizable subset and — the reason it is the right pick here — it
compiles RTL into C++.

That means the golden reference model and the RTL can be linked into a *single C++ test
binary*, stepping both and comparing accumulators cycle by cycle rather than only
comparing final results over a serial link. The C++ reference model was already on the
plan for the `sim` backend and the demo fallback; Verilator makes it the verification
harness too, which is the fourth job that model is now doing.

```
tb/
├── cpp/
│   ├── model.cpp          # golden reference (Lane B)
│   ├── tb_mac_array.cpp   # steps Verilated RTL + model together
│   └── golden_packets.h   # generated fixtures, shared with Python
└── sv/
    └── tb_uart.sv         # directed waveform tests (Questa)
```

### 2.2 Questa Intel Starter Edition for waveforms

Bundled with Quartus, good SystemVerilog simulation support, and the right tool when you
need to *look* at a waveform — UART bit timing especially.

One limitation worth planning around: the Starter Edition does not include full
constrained-random generation or functional coverage. Those are Questa Prime features.
Lane B's verification plan should therefore rest on directed tests plus randomized
stimulus driven from C++ under Verilator, not on SystemVerilog constrained-random. The
test lists in the component specs are already written this way.

---

## 3. The package is the coordination mechanism

`rtl/fecim_pkg.sv` holds every parameter, type, and protocol encoding that crosses a
module or work-lane boundary. Import it everywhere:

```systemverilog
import fecim_pkg::*;
```

For a three-person team this is the highest-leverage file in the repository. The classic
failure on a project like this is Lane A changing `CMD_READ_RESULT` from `0x05` to `0x06`,
Lane C not noticing, and two days disappearing into a bug that looks like a datapath
problem. One declaration site makes that impossible on the RTL side.

### 3.1 The remaining gap, and how to close it

The package fixes RTL-to-RTL drift. It does not fix RTL-to-Python drift — Lane C's driver
still encodes `0x05` independently.

Close it by making `docs/protocol.yaml` the source and generating both:

```
docs/protocol.yaml
    ├─(gen)─► rtl/fecim_pkg_gen.svh      imported by fecim_pkg.sv
    └─(gen)─► sw/fecim/protocol.py       imported by the driver
```

The generator is about forty lines of Python and runs in CI, failing the build if the
generated files are stale. It eliminates an entire category of integration bug, and it is
a clean, self-contained task for whoever finishes their week-1 work first.

It also happens to be exactly the kind of artifact M0 wants in the repository — a
planning document that turned into tooling, with a commit trail showing why.

---

## 4. Corrected snippets

These replace the Verilog fragments in the earlier component specs.

### 4.1 `baud_gen` (UART spec §3.2)

```systemverilog
module baud_gen
    import fecim_pkg::*;
(
    input  logic                  clk,
    input  logic                  rst_n,
    input  logic [BAUD_ACC_W-1:0] baud_inc,
    output logic                  tick_16x
);

    logic [BAUD_ACC_W:0] acc;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) acc <= '0;
        else        acc <= {1'b0, acc[BAUD_ACC_W-1:0]} + baud_inc;
    end

    assign tick_16x = acc[BAUD_ACC_W];

endmodule : baud_gen
```

### 4.2 RX synchronizer (UART spec §4.2)

```systemverilog
logic [2:0] rx_sync;
logic       rx_bit;

always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) rx_sync <= '1;                     // idle HIGH
    else        rx_sync <= {rx_sync[1:0], rxd};
end

assign rx_bit = rx_sync[2];
```

**Reset to `'1`, not `'0`.** The line idles high, so a synchronizer that resets low
presents a falling edge to the state machine on release — indistinguishable from a start
bit, producing one garbage byte after every reset. This is a two-character fix and a
genuinely confusing bug to chase, because it only appears on the first byte and looks like
a host-side framing problem.

### 4.3 CRC-8 (parser spec §6)

Moved into `fecim_pkg.sv` as `crc8_byte()` so the parser and the response generator share
one definition. `function automatic` matters — a static function with internal state is
not reentrant and will produce wrong results if called from two places in the same
timestep.

### 4.4 Lane pipeline depth (control FSM spec §4.2)

Now `fecim_pkg::LANE_PIPE_DEPTH`. The control FSM's hold count in `ACC` references the
package parameter directly, so adding a pipeline stage to `mac_lane` updates the sequencer
automatically. This is exactly the coupling the earlier spec flagged as
silently-drops-the-last-rows if it drifts.

---

## 5. FSM template

Every state machine in the design follows this shape:

```systemverilog
ctrl_state_e state, next_state;

// state register
always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) state <= CTRL_IDLE;
    else        state <= next_state;
end

// next-state logic
always_comb begin
    next_state = state;                    // default: hold
    unique case (state)
        CTRL_IDLE  : if (start_compute) next_state = CTRL_CLEAR;
        CTRL_CLEAR :                    next_state = CTRL_ACC;
        CTRL_ACC   : if (acc_done)      next_state = CTRL_DRAIN;
        CTRL_DRAIN : if (drain_done)    next_state = (last_pass) ? CTRL_DONE
                                                                : CTRL_CLEAR;
        CTRL_DONE  :                    next_state = CTRL_IDLE;
        default    :                    next_state = CTRL_IDLE;
    endcase
end
```

Two-block style with a defaulted `next_state` — the default assignment means a forgotten
branch holds state rather than inferring a latch or falling to X. `unique case` adds a
simulation assertion that the states are mutually exclusive and gives Quartus a
one-hot hint.

---

## 6. Lane array instantiation

The one place `generate` earns its keep:

```systemverilog
genvar l;
generate
    for (l = 0; l < NUM_LANES; l++) begin : g_lane
        mac_lane u_lane (
            .clk        (clk),
            .rst_n      (rst_n),
            .w_addr     (seq_cnt),
            .act        (act_bcast),
            .noise_en   (noise_en),
            .acc_clear  (acc_clear),
            .acc_en     (acc_en),
            .shift_en   (shift_en),
            .shift_in   (lane_acc[l+1]),        // drain shift chain
            .acc_out    (lane_acc[l])
        );
    end
endgenerate

assign lane_acc[NUM_LANES] = '0;                // chain tail
```

Named generate blocks (`g_lane`) matter — they produce readable hierarchical paths in
Questa and in Quartus timing reports. Without the label you get `genblk1[7]` and lose an
hour cross-referencing.

Note the shift chain wiring: lane `l` takes its shifted input from lane `l+1`, with the
tail tied to zero, so results emerge from lane 0. Getting this backwards reverses column
order in the result buffer — see control FSM spec, test 3.

---

## 7. Lint

Run Verilator in lint mode on every commit, in CI:

```bash
verilator --lint-only -Wall -Wno-fatal \
    --top-module fecim_top \
    rtl/fecim_pkg.sv rtl/**/*.sv
```

`-Wall` catches width mismatches, unused signals, and incomplete sensitivity — the width
mismatch warnings in particular are worth heeding. A 24-bit accumulator silently truncated
to 16 bits somewhere is a bug that produces plausible small numbers and survives casual
testing all the way to the noise sweeps in week 9.
