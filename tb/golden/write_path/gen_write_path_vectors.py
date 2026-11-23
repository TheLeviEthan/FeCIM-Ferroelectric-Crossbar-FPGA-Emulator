#!/usr/bin/env python3
"""Generate the golden vectors for the write path (quantize, D2D, stuck-at).

    python3 tb/golden/write_path/gen_write_path_vectors.py           # (re)write files
    python3 tb/golden/write_path/gen_write_path_vectors.py --check   # CI: fail if stale

Lane C owns this script; Lane B's write_path testbench consumes its output and
model/tests/test_write_path_vectors.cpp replays it through model_exact. The
vectors are committed files, not generated at test time
(model-verification-design §8), so the RTL testbench and the C++ model assert
against the same bytes.

What a case is: one weight byte through the write path. Given a register
configuration (NOISE_EN, QUANT_LEVELS, QUANT_MULT, D2D_SIGMA, NOISE_SEED,
STUCK_RATE), the global cell address and the written weight, the expected
value is what lands in the lane weight memory (write-path-spec §3-5):

    w = w_in
    if NOISE_EN.quant: w = quantize(w)          stage 1, §4.1
    if NOISE_EN.d2d:   w = d2d(w, addr)         stage 2, §5.1-5.2 (saturating)
    if NOISE_EN.stuck: w = stuck(w, addr)       stage 3, §5.3 (overrides)

Deliberately dependency-free and independent of model/ (plain Python ints,
whose >> is a floor shift exactly like SV's >>> on a signed value; its own
PRNG), so a bug in model_exact cannot leak into the golden values. Constants
are read from rtl/fecim_pkg.sv so the vectors cannot drift from the package.

See README.md in this directory for the file format and how to drive a case.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
PKG = REPO / "rtl" / "fecim_pkg.sv"

SEED = 0xFEC1_0003          # stimulus PRNG seed; recorded, never implicit (§8)
N_RANDOM = 48               # random (addr, w) pairs per configuration
NL = "\r\n"                 # repo convention; readers must accept CR (README)

CFG_WORDS = 6               # noise_en quant_levels quant_mult d2d_sigma noise_seed stuck_rate
CASE_WORDS = 4              # cfg  addr  w_in  w_expected
FNV_OFFSET = 0x811C9DC5     # checksum over a full 65,536-entry hash sweep
FNV_PRIME = 0x01000193

# NOISE_EN bit order (protocol.md §6, REG_NOISE_EN)
QUANT, D2D, READ, STUCK, IR, ADC = (1 << i for i in range(6))
RAIL = 1 << 16              # STUCK_RATE[16]: 0 = stuck-at-zero, 1 = stuck-at-rail


# ---------------------------------------------------------------------------
# Package constants -- read from rtl/fecim_pkg.sv
# ---------------------------------------------------------------------------

def read_pkg() -> dict[str, int]:
    text = PKG.read_text(encoding="utf-8-sig")
    out: dict[str, int] = {}
    for name in ["TILE_ROWS", "TILE_COLS", "NUM_LANES", "WEIGHT_W"]:
        m = re.search(rf"parameter\s+int\s+{name}\s*=\s*(\d[\d_]*)\s*;", text)
        if not m:
            sys.exit(f"cannot find literal 'parameter int {name}' in {PKG}")
        out[name] = int(m.group(1).replace("_", ""))
    for name in ["HASH_C0", "HASH_C1", "SEED_D2D", "SEED_STUCK"]:
        m = re.search(rf"parameter\s+logic\s*\[15:0\]\s*{name}\s*=\s*16'h([0-9A-Fa-f_]+)\s*;", text)
        if not m:
            sys.exit(f"cannot find 16'h literal for {name} in {PKG}")
        out[name] = int(m.group(1).replace("_", ""), 16)
    return out


P = read_pkg()
ROWS, COLS, LANES = P["TILE_ROWS"], P["TILE_COLS"], P["NUM_LANES"]
CELLS = ROWS * COLS
if P["WEIGHT_W"] != 8:
    sys.exit("generator assumes WEIGHT_W = 8")


# ---------------------------------------------------------------------------
# Reference arithmetic, straight from write-path-spec §4-5
# ---------------------------------------------------------------------------

def cell_hash16(addr: int, seed: int) -> int:
    h = (addr ^ seed) & 0xFFFF
    h = (h * P["HASH_C0"]) & 0xFFFF
    h ^= h >> 7
    h = (h * P["HASH_C1"]) & 0xFFFF
    h ^= h >> 9
    return h


def h_d2d(addr: int, noise_seed: int) -> int:
    return cell_hash16(addr, (noise_seed & 0xFFFF) ^ P["SEED_D2D"])


def h_stuck(addr: int, noise_seed: int) -> int:
    return cell_hash16(addr, (noise_seed & 0xFFFF) ^ P["SEED_STUCK"])


def quant_mult_for(n: int) -> int:
    return (255 * 256 + (n - 1) // 2) // (n - 1)


def quantize(w: int, n: int, mult: int) -> int:
    u = w + 128                                  # int8 -> 0..255
    level = (u * n) >> 8
    scaled = (level * mult) >> 8
    return (scaled & 0xFF) - 128                 # keep scaled[7:0], as the RTL does


def d2d(w: int, addr: int, noise_seed: int, sigma: int) -> int:
    h = h_d2d(addr, noise_seed)
    ih9 = (h & 0xFF) + (h >> 8) - 255            # triangular, -255..+255
    s = w + ((ih9 * sigma) >> 8)                 # Python >> floors, like SV >>>
    return max(-128, min(127, s))                # saturate, never wrap


def stuck(w: int, addr: int, noise_seed: int, rate: int) -> int:
    if h_stuck(addr, noise_seed) >= (rate & 0xFFFF):
        return w
    if not (rate & RAIL):
        return 0
    # Rail sign: h_d2d[15] = 1 -> -127 (write-path-spec §5.3 convention).
    return -127 if h_d2d(addr, noise_seed) & 0x8000 else 127


@dataclass
class Cfg:
    name: str
    noise_en: int
    levels: int = 255
    mult: int = 257
    sigma: int = 0
    seed: int = 0
    rate: int = 0
    purpose: str = ""


def write_path(c: Cfg, addr: int, w: int) -> int:
    if c.noise_en & QUANT:
        w = quantize(w, c.levels, c.mult)
    if c.noise_en & D2D:
        w = d2d(w, addr, c.seed, c.sigma)
    if c.noise_en & STUCK:
        w = stuck(w, addr, c.seed, c.rate)
    return w


# ---------------------------------------------------------------------------
# PRNG: splitmix64 (same as the mac_lane generator), not Python's random
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
        return self.next() % n


# ---------------------------------------------------------------------------
# Configurations
# ---------------------------------------------------------------------------

def q(n: int) -> dict[str, int]:
    return {"levels": n, "mult": quant_mult_for(n)}


R1, R10 = 655, 6554          # ~1 %, ~10 % of 65,536
EDGE_CELL = 0 * ROWS + 43    # cell (row 43, col 0): h_stuck = 77 at seed 0
EDGE_RATE = 77               # checked against the hash in build()
SET_A = {"levels": 8, "mult": quant_mult_for(8), "sigma": 18}   # device-model.md, set A

CONFIGS = [
    Cfg("bypass_lossy_regs", 0, levels=2, mult=quant_mult_for(2), sigma=255,
        seed=0xDEADBEEF, rate=0xFFFF | RAIL,
        purpose="all enables clear: pure pass-through despite destructive registers (L0 gate)"),
    Cfg("read_ir_adc_only", READ | IR | ADC, levels=2, mult=quant_mult_for(2), sigma=255,
        rate=0xFFFF | RAIL,
        purpose="read-time enables only: the write path must ignore bits 2, 4, 5"),
    # stage 1
    *[Cfg(f"quant_n{n}", QUANT, **q(n), purpose=f"quantize to {n} levels")
      for n in (2, 3, 5, 8, 255)],
    Cfg("quant_mismatched_pair", QUANT, levels=3, mult=257,
        purpose="N=3 with QUANT_MULT=257: wraps modulo 256, not rejected"),
    # stage 2
    *[Cfg(f"d2d_s{s}", D2D, sigma=s, purpose=f"D2D only, D2D_SIGMA={s}")
      for s in (0, 1, 8, 18, 32, 255)],
    Cfg("d2d_s255_seed1234", D2D, sigma=255, seed=0x1234, purpose="different chip"),
    Cfg("d2d_s255_seedABCD1234", D2D, sigma=255, seed=0xABCD1234,
        purpose="same as seed 0x1234: only NOISE_SEED[15:0] reaches the write path"),
    Cfg("d2d_s255_seedFFFFFFFF", D2D, sigma=255, seed=0xFFFFFFFF, purpose="all-ones seed"),
    # stage 3
    *[Cfg(f"stuck0_r{r}", STUCK, rate=r, purpose=f"stuck-at-zero, rate {r}/65536")
      for r in (0, R1, R10, 0xFFFF)],
    *[Cfg(f"stuckR_r{r}", STUCK, rate=r | RAIL, purpose=f"stuck-at-rail, rate {r}/65536")
      for r in (0, R1, R10, 0xFFFF)],
    Cfg("stuckR_r10pct_seedBEEF", STUCK, rate=R10 | RAIL, seed=0xBEEF,
        purpose="rail, different seed"),
    Cfg("stuck0_edge_r77", STUCK, rate=EDGE_RATE,
        purpose="threshold: cell with h_stuck == rate is NOT stuck (strict <)"),
    Cfg("stuck0_edge_r78", STUCK, rate=EDGE_RATE + 1,
        purpose="threshold: same cell, rate one higher, IS stuck"),
    # combinations, in the RTL stage order
    Cfg("setA_quant8_d2d18", QUANT | D2D, **SET_A, purpose="calibrated set A, no faults"),
    Cfg("setA_stuck0_1pct", QUANT | D2D | STUCK, **SET_A, rate=R1,
        purpose="set A + 1 % stuck-at-zero"),
    Cfg("setA_stuckR_1pct", QUANT | D2D | STUCK, **SET_A, rate=R1 | RAIL,
        purpose="set A + 1 % stuck-at-rail"),
    Cfg("quant2_d2d255", QUANT | D2D, **q(2), sigma=255, seed=0x0F0F,
        purpose="quantized to the rails, then full D2D: saturation from both ends"),
    Cfg("d2d255_stuckR_10pct", D2D | STUCK, sigma=255, seed=0x5EED, rate=R10 | RAIL,
        purpose="stuck overrides a large D2D offset"),
    Cfg("all_three_n3_s100_rail", QUANT | D2D | STUCK, **q(3), sigma=100,
        seed=0xFFFFFFFF, rate=R10 | RAIL, purpose="odd N, mid sigma, rail"),
    Cfg("all_six_bits", 0x3F, **q(5), sigma=40, seed=0xC0FFEE, rate=R10 | RAIL,
        purpose="every NOISE_EN bit set: write path applies quant, d2d, stuck only"),
]


# ---------------------------------------------------------------------------
# Cases
# ---------------------------------------------------------------------------

EDGE_W = (-128, -127, -1, 0, 1, 126, 127)
EDGE_ADDR = (0, 1, ROWS - 1, ROWS, CELLS // 2 - 1, CELLS // 2, CELLS - ROWS, CELLS - 1)


def interesting_addrs(c: Cfg) -> list[int]:
    """Cells that exercise the corners of this configuration's seed."""
    out = []
    # The first cells that are stuck at ~1 % for this seed (so even a low-rate
    # config has hits in the file), and at the config's own rate.
    rate = c.rate & 0xFFFF
    out += [a for a in range(CELLS) if h_stuck(a, c.seed) < R1][:6]
    if rate:
        out += [a for a in range(CELLS) if h_stuck(a, c.seed) < rate][:6]
        # The threshold itself: h_stuck == rate must NOT be stuck, and
        # h_stuck == rate - 1 must be. Catches `<=` in place of `<`.
        out += [a for a in range(CELLS) if h_stuck(a, c.seed) in (rate - 1, rate)][:4]
    # Cells with the largest D2D draws in each direction (saturation).
    def ih9(a: int) -> int:
        h = h_d2d(a, c.seed)
        return (h & 0xFF) + (h >> 8) - 255
    out += [a for a in range(CELLS) if ih9(a) >= 230][:3]
    out += [a for a in range(CELLS) if ih9(a) <= -230][:3]
    # A stuck cell with each rail sign.
    st = [a for a in range(CELLS) if h_stuck(a, c.seed) < R10]
    out += [a for a in st if h_d2d(a, c.seed) & 0x8000][:2]
    out += [a for a in st if not h_d2d(a, c.seed) & 0x8000][:2]
    seen, uniq = set(), []
    for a in out:
        if a not in seen:
            seen.add(a)
            uniq.append(a)
    return uniq


@dataclass
class Case:
    cfg: int
    addr: int
    w_in: int
    w_out: int


def build() -> list[Case]:
    if h_stuck(EDGE_CELL, 0) != EDGE_RATE:
        sys.exit("EDGE_RATE no longer matches h_stuck(EDGE_CELL, 0); update both")
    rnd = SplitMix64(SEED)
    cases: list[Case] = []
    for ci, c in enumerate(CONFIGS):
        pairs = [(a, w) for a in EDGE_ADDR for w in EDGE_W]
        pairs += [(a, w) for a in interesting_addrs(c) for w in (-128, -50, 0, 50, 127)]
        pairs += [(rnd.below(CELLS), rnd.below(256) - 128) for _ in range(N_RANDOM)]
        for a, w in pairs:
            cases.append(Case(ci, a, w, write_path(c, a, w)))
    return cases


def checksums() -> list[tuple[int, int, int]]:
    """FNV-1a style fold of cell_hash16 over all 65,536 addresses.

    cell_hash16 is a bijection on 16 bits, so a plain sum or XOR over every
    address is the same for ANY bijection and would catch nothing. The fold is
    order-dependent, so a single wrong output at a single address changes it.
    """
    out = []
    for noise_seed in (0x0000, 0x0001, 0x1234, 0x8000, 0xBEEF, 0xC0FF, 0xFFFE, 0xFFFF):
        for salt in (P["SEED_D2D"], P["SEED_STUCK"]):
            c = FNV_OFFSET
            for a in range(1 << 16):
                c = ((c ^ cell_hash16(a, noise_seed ^ salt)) * FNV_PRIME) & 0xFFFF_FFFF
            out.append((noise_seed, salt, c))
    return out


# ---------------------------------------------------------------------------
# Emit
# ---------------------------------------------------------------------------

def h32(v: int) -> str:
    return f"{v & 0xFFFF_FFFF:08X}"


HEADER = [
    "// GENERATED by tb/golden/write_path/gen_write_path_vectors.py -- do not edit.",
    f"// seed 0x{SEED:08X}; package: TILE_ROWS={ROWS} TILE_COLS={COLS} NUM_LANES={LANES} "
    f"HASH_C0=0x{P['HASH_C0']:04X} HASH_C1=0x{P['HASH_C1']:04X} "
    f"SEED_D2D=0x{P['SEED_D2D']:04X} SEED_STUCK=0x{P['SEED_STUCK']:04X}",
]


def emit(cases: list[Case], sums: list[tuple[int, int, int]]) -> dict[str, str]:
    files: dict[str, str] = {}

    lines = HEADER + [
        "// Word 0: number of configs. Then 6 words per config, the register values:",
        "//   noise_en  quant_levels  quant_mult  d2d_sigma  noise_seed  stuck_rate",
        h32(len(CONFIGS)),
    ]
    for i, c in enumerate(CONFIGS):
        lines.append(f"{h32(c.noise_en)} {h32(c.levels)} {h32(c.mult)} {h32(c.sigma)} "
                     f"{h32(c.seed)} {h32(c.rate)}  // {i:2d} {c.name}")
    files["configs.hex"] = NL.join(lines) + NL

    lines = HEADER + [
        "// Word 0: number of cases. Then 4 words per case:",
        "//   cfg  addr  w_in  w_expected",
        "// cfg indexes configs.hex. addr is the global cell address col*TILE_ROWS+row",
        "// (= bulk_addr). w_in and w_expected are int8, two's complement in [7:0].",
        h32(len(cases)),
    ]
    prev = -1
    for c in cases:
        if c.cfg != prev:
            lines.append(f"// config {c.cfg} {CONFIGS[c.cfg].name}")
            prev = c.cfg
        lines.append(f"{h32(c.cfg)} {h32(c.addr)} {h32(c.w_in & 0xFF)} {h32(c.w_out & 0xFF)}")
    files["cases.hex"] = NL.join(lines) + NL

    lines = HEADER + [
        f"// Word 0: number of entries ({CELLS}). Then one word per cell address 0..{CELLS - 1}:",
        "//   [31:16] h_stuck   [15:0] h_d2d     at NOISE_SEED = 0",
        "// i.e. cell_hash16(addr, SEED_STUCK) and cell_hash16(addr, SEED_D2D).",
        h32(CELLS),
    ]
    for a in range(CELLS):
        lines.append(h32((h_stuck(a, 0) << 16) | h_d2d(a, 0)))
    files["hash_seed0.hex"] = NL.join(lines) + NL

    lines = HEADER + [
        "// Word 0: number of entries. Then 3 words per entry:",
        "//   noise_seed_lo16  salt  checksum",
        "// checksum: c = 0x811C9DC5; for addr = 0 .. 65535:",
        "//   c = ((c ^ cell_hash16(addr, noise_seed_lo16 ^ salt)) * 0x01000193) mod 2^32",
        h32(len(sums)),
    ]
    for s, salt, c in sums:
        lines.append(f"{h32(s)} {h32(salt)} {h32(c)}")
    files["hash_checksums.hex"] = NL.join(lines) + NL

    n_cfg, n_case = len(CONFIGS), len(cases)
    lines = [
        "// GENERATED by tb/golden/write_path/gen_write_path_vectors.py -- do not edit.",
        "// Sizes and field offsets for $readmemh of the write_path vector files.",
        "`ifndef WRITE_PATH_VECTORS_SVH",
        "`define WRITE_PATH_VECTORS_SVH",
        f"localparam int WPV_N_CONFIGS    = {n_cfg};",
        f"localparam int WPV_N_CASES      = {n_case};",
        f"localparam int WPV_N_HASH       = {CELLS};",
        f"localparam int WPV_N_SUMS       = {len(sums)};",
        f"localparam int WPV_CFG_WORDS    = {CFG_WORDS};",
        f"localparam int WPV_CASE_WORDS   = {CASE_WORDS};",
        "localparam int WPV_CFG_DEPTH    = 1 + WPV_N_CONFIGS * WPV_CFG_WORDS;",
        "localparam int WPV_CASE_DEPTH   = 1 + WPV_N_CASES * WPV_CASE_WORDS;",
        "localparam int WPV_HASH_DEPTH   = 1 + WPV_N_HASH;",
        "localparam int WPV_SUMS_DEPTH   = 1 + WPV_N_SUMS * 3;",
        "// config i:  cfg[1 + i*6 + k],  k = 0 noise_en, 1 quant_levels, 2 quant_mult,",
        "//                               3 d2d_sigma, 4 noise_seed, 5 stuck_rate",
        "// case j:    cs [1 + j*4 + k],  k = 0 cfg, 1 addr, 2 w_in, 3 w_expected",
        "// hash a:    hs [1 + a] = {h_stuck, h_d2d} at NOISE_SEED = 0",
        "// sum k:     sm [1 + k*3 + m],  m = 0 noise_seed_lo16, 1 salt, 2 checksum",
        "`endif",
    ]
    files["write_path_vectors.svh"] = NL.join(lines) + NL

    per_cfg = [sum(1 for c in cases if c.cfg == i) for i in range(n_cfg)]
    hits = [sum(1 for c in cases if c.cfg == i and c.w_in != c.w_out) for i in range(n_cfg)]
    lines = [
        "GENERATED by gen_write_path_vectors.py -- do not edit.",
        f"{n_cfg} configs, {n_case} cases, stimulus seed 0x{SEED:08X}.",
        "changed = cases whose stored weight differs from the written one.",
        "",
        f"{'#':>3}  {'name':<24} {'en':>4} {'N':>3} {'mult':>5} {'sigma':>5} "
        f"{'seed':>10} {'rate':>7}  {'cases':>5} {'changed':>7}  purpose",
    ]
    for i, c in enumerate(CONFIGS):
        lines.append(f"{i:>3}  {c.name:<24} {c.noise_en:>#4x} {c.levels:>3} {c.mult:>5} "
                     f"{c.sigma:>5} {c.seed:>#10x} {c.rate:>#7x}  {per_cfg[i]:>5} "
                     f"{hits[i]:>7}  {c.purpose}")
    files["configs.txt"] = NL.join(lines) + NL
    return files


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if the committed files differ from a fresh generation")
    args = ap.parse_args()

    files = emit(build(), checksums())
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
            print("stale write_path vectors (rerun gen_write_path_vectors.py and commit): "
                  + ", ".join(stale))
            return 1
        print(f"write_path vectors up to date ({len(files)} files)")
        return 0
    print("wrote " + ", ".join(files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
