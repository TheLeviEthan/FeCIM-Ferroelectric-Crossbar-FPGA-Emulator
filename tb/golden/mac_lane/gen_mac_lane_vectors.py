#!/usr/bin/env python3
"""Generate the clean-datapath golden vectors for a single mac_lane.

    python3 tb/golden/mac_lane/gen_mac_lane_vectors.py           # (re)write files
    python3 tb/golden/mac_lane/gen_mac_lane_vectors.py --check   # CI: fail if stale

Lane C owns this script; Lane B's mac_lane testbench consumes its output.
The vectors are committed files, not generated at test time
(model-verification-design §8), so the RTL testbench, the C++ model test
(model/tests/test_mac_lane_vectors.cpp) and any later Python test all assert
against the same bytes.

Scope: NOISE_EN = NOISE_NONE, read_sigma = 0. Every expected value is the
plain integer dot product sum(w[r] * a[r]) with w signed 8-bit and a unsigned
8-bit. Read noise, ADC and IR drop are week-5 vectors, not these.

Deliberately dependency-free and independent of model/ (plain Python ints, its
own PRNG), so a bug in model_exact cannot leak into the golden values -- the
C++ test cross-checks the two.

See README.md in this directory for the file format and the timing contract.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
PKG = REPO / "rtl" / "fecim_pkg.sv"

SEED = 0xFEC1_0002          # recorded, never implicit (model-verification-design §8)
N_RANDOM = 96

FLAG_WRITE = 0x1            # TB writes this case's weights before computing

# Fixed-width words: every field is one 32-bit readmemh word, 8 hex digits.
CASE_WORDS = 5              # row_base, num_rows, pass, flags, expected
ROW_WORDS = 3               # weight (int8), act (uint8), running sum (int32)
NL = "\r\n"                 # repo convention; readers must accept CR (README)


# ---------------------------------------------------------------------------
# Package constants -- read from rtl/fecim_pkg.sv so the vectors cannot drift
# ---------------------------------------------------------------------------

def read_pkg() -> dict[str, int]:
    text = PKG.read_text(encoding="utf-8-sig")
    want = ["TILE_ROWS", "TILE_COLS", "NUM_LANES", "WEIGHT_W", "ACT_W",
            "ACC_W", "LANE_PIPE_DEPTH"]
    out: dict[str, int] = {}
    for name in want:
        m = re.search(rf"parameter\s+int\s+{name}\s*=\s*(\d[\d_]*)\s*;", text)
        if not m:
            sys.exit(f"cannot find literal 'parameter int {name}' in {PKG}")
        out[name] = int(m.group(1).replace("_", ""))
    out["PASSES"] = out["TILE_COLS"] // out["NUM_LANES"]
    return out


P = read_pkg()
ROWS = P["TILE_ROWS"]
PASSES = P["PASSES"]
W_MIN, W_MAX = -(1 << (P["WEIGHT_W"] - 1)), (1 << (P["WEIGHT_W"] - 1)) - 1
A_MAX = (1 << P["ACT_W"]) - 1
ACC_MIN, ACC_MAX = -(1 << (P["ACC_W"] - 1)), (1 << (P["ACC_W"] - 1)) - 1
LANE_WORDS = ROWS * PASSES  # lane-local weight memory depth actually used


# ---------------------------------------------------------------------------
# PRNG: splitmix64, so the vectors do not depend on CPython's random module
# ---------------------------------------------------------------------------

class SplitMix64:
    def __init__(self, seed: int) -> None:
        self.s = seed & 0xFFFF_FFFF_FFFF_FFFF

    def next(self) -> int:
        self.s = (self.s + 0x9E37_79B9_7F4A_7C15) & 0xFFFF_FFFF_FFFF_FFFF
        z = self.s
        z = ((z ^ (z >> 30)) * 0xBF58_476D_1CE4_E5B9) & 0xFFFF_FFFF_FFFF_FFFF
        z = ((z ^ (z >> 27)) * 0x94D0_49BB_1331_11EB) & 0xFFFF_FFFF_FFFF_FFFF
        return z ^ (z >> 31)

    def below(self, n: int) -> int:
        return self.next() % n          # bias is irrelevant for test stimulus

    def rng(self, lo: int, hi: int) -> int:
        return lo + self.below(hi - lo + 1)


# ---------------------------------------------------------------------------
# Case model
# ---------------------------------------------------------------------------

@dataclass
class Case:
    name: str
    purpose: str
    weights: list[int]
    acts: list[int]
    pass_idx: int = 0
    write: bool = True
    # filled in by build()
    row_base: int = 0
    running: list[int] = field(default_factory=list)

    @property
    def n(self) -> int:
        return len(self.weights)

    @property
    def expected(self) -> int:
        return self.running[-1]


def ramp_weights(n: int = ROWS) -> list[int]:
    """Distinct, non-zero weights: -64..-1 then 1..64. A one-row misalignment
    returns a neighbour's value, never the right one and never zero."""
    return [r - 64 if r < 64 else r - 63 for r in range(n)]


def directed(rnd: SplitMix64) -> list[Case]:
    cases: list[Case] = []
    add = cases.append

    # -- 1. single products: one row each, so a wrong value names the product
    products = [
        (0, 0, "zero x zero"),
        (3, 5, "smoke test"),
        (-1, 1, "negative weight"),
        (1, 255, "act MSB set: must zero-extend (sign-extended gives -1)"),
        (-1, 255, "negative weight x act MSB"),
        (1, 128, "act = 0x80: sign-extension reads it as -128"),
        (-2, 200, "act MSB set, negative weight"),
        (127, 255, "largest positive clean product (+32,385)"),
        (-128, 255, "largest-magnitude clean product (-32,640)"),
        (-128, 128, "weight 0x80 x act 0x80"),
        (127, 128, "positive weight max x act 0x80"),
        (-128, 1, "weight 0x80 is legal (bypass write path)"),
        (64, 2, "plain positive"),
    ]
    for w, a, why in products:
        add(Case(f"prod_{w}_x_{a}", why, [w], [a]))

    # -- 2. zeros
    add(Case("zero_weights", "w = 0 everywhere, acts random: result 0",
             [0] * ROWS, [rnd.rng(1, A_MAX) for _ in range(ROWS)]))
    add(Case("zero_acts", "acts = 0 everywhere, weights random: result 0",
             [rnd.rng(W_MIN, W_MAX) or 1 for _ in range(ROWS)], [0] * ROWS))

    # -- 3. accumulator magnitude and sign extension, full length
    add(Case("max_neg_full", "-128 x 255 every row: -4,177,920, upper acc bits all 1",
             [W_MIN] * ROWS, [A_MAX] * ROWS))
    add(Case("max_pos_full", "+127 x 255 every row: +4,145,280",
             [W_MAX] * ROWS, [A_MAX] * ROWS))
    alt_w = [W_MAX if r % 2 == 0 else W_MIN for r in range(ROWS)]
    add(Case("alternating_sign", "running sum crosses zero every row: carries/borrows "
             "through all 32 bits", alt_w, [A_MAX] * ROWS))
    swing_w = [W_MIN] * 64 + [W_MAX] * 64
    add(Case("swing_neg_then_pos", "descends to -2,088,960, climbs back to -16,320: "
             "long borrow chains", swing_w, [A_MAX] * ROWS))

    # -- 4. pipeline edges: only three rows non-zero, at the start or the end of
    #       the pass, so a dropped or shifted row cannot hide in a large sum.
    def edge_case(name: str, rows: list[int], n: int, why: str) -> Case:
        w = [0] * n
        a = [0] * n
        for r, (wv, av) in zip(rows, ((1, 1), (10, 2), (100, 3))):
            w[r], a[r] = wv, av
        return Case(name, why, w, a)

    add(edge_case("first3_full", [0, 1, 2], ROWS,
                  "rows 0..2 only (w 1,10,100 x a 1,2,3): first rows and enable delay"))
    add(edge_case("last3_full", [ROWS - 3, ROWS - 2, ROWS - 1], ROWS,
                  "rows 125..127 only: low result = last rows dropped (acc_en not pipelined)"))
    add(edge_case("last3_n37", [34, 35, 36], 37,
                  "same as last3 with active_rows = 37"))
    add(edge_case("last3_n4", [1, 2, 3], 4,
                  "same as last3 with active_rows = 4"))
    add(edge_case("rows_n3", [0, 1, 2], 3,
                  "active_rows = LANE_PIPE_DEPTH: pipeline never fills"))

    # -- 5. short and partial passes
    for n in (1, 2, 4, 5, 64, 127):
        rw = ramp_weights(ROWS)[:n]
        add(Case(f"partial_n{n}", f"active_rows = {n}, ramp weights x ramp acts",
                 rw, [r + 1 for r in range(n)]))

    # -- 6. alignment checksum, then the one-hot sweep (spec test 3 / act-buffer
    #       test 3). The sweep writes once and then reads without rewriting.
    rw = ramp_weights()
    add(Case("ramp_x_ramp", "w = ramp, a = r+1: full-tile alignment checksum",
             rw, [r + 1 for r in range(ROWS)]))
    for k in range(ROWS):
        acts = [0] * ROWS
        acts[k] = 1
        add(Case(f"onehot_{k}", f"act one-hot at row {k}: result is ramp[{k}] = {rw[k]}"
                 + ("; first row" if k == 0 else "")
                 + ("; last row, the LANE_PIPE_DEPTH test" if k == ROWS - 1 else ""),
                 rw, acts, write=(k == 0)))

    # -- 7. clear between computes
    cw = [rnd.rng(1, W_MAX) for _ in range(ROWS)]
    ca = [rnd.rng(1, A_MAX) for _ in range(ROWS)]
    add(Case("clear_repeat_a", "large positive result", cw, ca))
    add(Case("clear_repeat_b", "identical to clear_repeat_a, no rewrite: same value "
             "again; double = acc_clear not taking", cw, ca, write=False))

    # -- 8. weight-memory addressing across passes (seq_cnt bit 7)
    if PASSES > 1:
        wa = [rnd.rng(W_MIN, W_MAX) for _ in range(ROWS)]
        wb = [rnd.rng(W_MIN, W_MAX) for _ in range(ROWS)]
        xa = [rnd.rng(0, A_MAX) for _ in range(ROWS)]
        ya = [rnd.rng(0, A_MAX) for _ in range(ROWS)]
        add(Case("addr_p0_write", "pass 0 weights A", wa, xa, pass_idx=0))
        add(Case("addr_p1_write", "pass 1 weights B, addresses 128..255", wb, xa, pass_idx=1))
        add(Case("addr_p0_reread", "no write: pass-1 writes must not have clobbered "
                 "pass 0", wa, xa, pass_idx=0, write=False))
        add(Case("addr_p1_reread", "no write, new acts: reads select by seq_cnt[7]",
                 wb, ya, pass_idx=1, write=False))
    return cases


def random_cases(rnd: SplitMix64) -> list[Case]:
    out = []
    for i in range(N_RANDOM):
        n = ROWS if i % 2 == 0 else rnd.rng(1, ROWS)
        p = rnd.below(PASSES)
        out.append(Case(f"rand_{i:03d}", f"random, active_rows = {n}, pass {p}",
                        [rnd.rng(W_MIN, W_MAX) for _ in range(n)],
                        [rnd.rng(0, A_MAX) for _ in range(n)], pass_idx=p))
    return out


# ---------------------------------------------------------------------------
# Build: running sums, and replay the lane memory so no-write cases are valid
# ---------------------------------------------------------------------------

def build() -> list[Case]:
    rnd = SplitMix64(SEED)
    cases = directed(rnd) + random_cases(rnd)
    mem: list[int | None] = [None] * LANE_WORDS
    base = 0
    for i, c in enumerate(cases):
        assert 1 <= c.n <= ROWS, c.name
        assert len(c.acts) == c.n, c.name
        assert all(W_MIN <= w <= W_MAX for w in c.weights), c.name
        assert all(0 <= a <= A_MAX for a in c.acts), c.name
        assert 0 <= c.pass_idx < PASSES, c.name
        addrs = [c.pass_idx * ROWS + r for r in range(c.n)]
        if c.write:
            for ad, w in zip(addrs, c.weights):
                mem[ad] = w
        else:
            got = [mem[ad] for ad in addrs]
            if got != c.weights:
                sys.exit(f"case {i} {c.name}: no-write case but lane memory holds "
                         f"different weights -- fix the case order")
        acc = 0
        c.running = []
        for w, a in zip(c.weights, c.acts):
            acc += w * a
            assert ACC_MIN <= acc <= ACC_MAX, c.name
            c.running.append(acc)
        c.row_base = base
        base += c.n
    return cases


# ---------------------------------------------------------------------------
# Emit
# ---------------------------------------------------------------------------

def h32(v: int) -> str:
    return f"{v & 0xFFFF_FFFF:08X}"


def h8(v: int) -> str:
    return f"{v & 0xFF:02X}"


HEADER = [
    "// GENERATED by tb/golden/mac_lane/gen_mac_lane_vectors.py -- do not edit.",
    f"// seed 0x{SEED:08X}; package: TILE_ROWS={ROWS} PASSES={PASSES} "
    f"LANE_PIPE_DEPTH={P['LANE_PIPE_DEPTH']}",
    "// Clean datapath only: noise_en = NOISE_NONE, read_sigma = 0.",
]


def emit(cases: list[Case]) -> dict[str, str]:
    n_rows = sum(c.n for c in cases)
    files: dict[str, str] = {}

    lines = HEADER + [
        "// Word 0: number of cases. Then 5 words per case:",
        "//   row_base  num_rows  pass  flags  expected",
        "// row_base indexes rows.hex records. flags[0] = WRITE (write this case's",
        "// weights before computing; clear means the lane memory already holds them).",
        "// expected is the int32 accumulator, two's complement.",
        h32(len(cases)),
    ]
    for i, c in enumerate(cases):
        lines.append(f"{h32(c.row_base)} {h32(c.n)} {h32(c.pass_idx)} "
                     f"{h32(FLAG_WRITE if c.write else 0)} {h32(c.expected)}"
                     f"  // {i:3d} {c.name}")
    files["cases.hex"] = NL.join(lines) + NL

    lines = HEADER + [
        "// Word 0: number of row records. Then 3 words per row:",
        "//   weight  act  running",
        "// weight is int8 (two's complement in [7:0]), act is uint8, running is the",
        "// int32 accumulator after this row's product is added. A case's rows are",
        "// contiguous from its row_base; row r sits at lane address pass*TILE_ROWS + r.",
        h32(n_rows),
    ]
    for i, c in enumerate(cases):
        lines.append(f"// case {i} {c.name}")
        for w, a, s in zip(c.weights, c.acts, c.running):
            lines.append(f"{h8(w)} {h8(a)} {h32(s)}")
    files["rows.hex"] = NL.join(lines) + NL

    lines = [
        "// GENERATED by tb/golden/mac_lane/gen_mac_lane_vectors.py -- do not edit.",
        "// Sizes and field offsets for $readmemh of cases.hex / rows.hex.",
        "`ifndef MAC_LANE_VECTORS_SVH",
        "`define MAC_LANE_VECTORS_SVH",
        f"localparam int MACV_N_CASES     = {len(cases)};",
        f"localparam int MACV_N_ROWS      = {n_rows};",
        f"localparam int MACV_CASE_WORDS  = {CASE_WORDS};",
        f"localparam int MACV_ROW_WORDS   = {ROW_WORDS};",
        "localparam int MACV_CASES_DEPTH = 1 + MACV_N_CASES * MACV_CASE_WORDS;",
        "localparam int MACV_ROWS_DEPTH  = 1 + MACV_N_ROWS * MACV_ROW_WORDS;",
        "// case i fields:  cases[1 + i*5 + k],  k = 0 row_base, 1 num_rows, 2 pass,",
        "//                                      3 flags, 4 expected",
        "// row j fields:   rows[1 + j*3 + k],   k = 0 weight, 1 act, 2 running",
        "localparam logic [31:0] MACV_FLAG_WRITE = 32'h1;",
        "`endif",
    ]
    files["mac_lane_vectors.svh"] = NL.join(lines) + NL

    lines = [
        "GENERATED by gen_mac_lane_vectors.py -- do not edit.",
        f"{len(cases)} cases, {n_rows} rows, seed 0x{SEED:08X}. Run in this order; "
        "W = weights written before compute.",
        "",
        f"{'#':>4}  {'name':<22} {'pass':>4} {'rows':>4}  W  {'expected':>11}  purpose",
    ]
    for i, c in enumerate(cases):
        lines.append(f"{i:>4}  {c.name:<22} {c.pass_idx:>4} {c.n:>4}  "
                     f"{'W' if c.write else '-'}  {c.expected:>11}  {c.purpose}")
    files["cases.txt"] = NL.join(lines) + NL
    return files


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if the committed files differ from a fresh generation")
    args = ap.parse_args()

    files = emit(build())
    stale = []
    for name, text in files.items():
        path = HERE / name
        if args.check:
            old = path.read_bytes().replace(b"\r\n", b"\n") if path.exists() else None
            if old != text.replace(NL, "\n").encode("ascii"):
                stale.append(name)
        else:
            path.write_bytes(text.encode("ascii"))
    if args.check:
        if stale:
            print("stale mac_lane vectors (rerun gen_mac_lane_vectors.py and commit): "
                  + ", ".join(stale))
            return 1
        print(f"mac_lane vectors up to date ({len(files)} files)")
        return 0
    print("wrote " + ", ".join(files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
