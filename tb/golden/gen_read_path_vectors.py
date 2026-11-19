#!/usr/bin/env python3
"""Generate the golden vectors for the read-time non-idealities.

    python3 tb/golden/gen_read_path_vectors.py           # (re)write files
    python3 tb/golden/gen_read_path_vectors.py --check   # CI: fail if stale

Writes four folders, each with its own README:

    tb/golden/read_noise/   per-lane LFSR + noise sample (mac-array-spec §5)
    tb/golden/ir_drop/      activation attenuation (mac-array-spec §8)
    tb/golden/adc/          output quantizer (mac-array-spec §9)
    tb/golden/mvm/          whole matrix-vector computes with all six effects

Lane C owns this script. Lane B's act_buffer, mac_lane and adc_quant
testbenches consume the first three; model/tests/test_read_path_vectors.cpp
replays all four through model_exact; the full-design co-simulation and the
A/B integration testbench can use mvm/.

Deliberately dependency-free and independent of model/: plain Python ints
(whose >> floors exactly like SV's >>> on a signed value), its own PRNG,
constants read from rtl/fecim_pkg.sv. The write-time effects in mvm/ come from
the write-path generator's reference functions (also independent of model/).
"""

from __future__ import annotations

import argparse
import importlib.util
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
PKG = REPO / "rtl" / "fecim_pkg.sv"

SEED = 0xFEC1_0004          # stimulus PRNG seed; recorded, never implicit
NL = "\r\n"                 # repo convention; readers must accept CR
FNV_OFFSET, FNV_PRIME = 0x811C9DC5, 0x01000193

# Write-path reference (cell_hash16, quantize, d2d, stuck) and the PRNG.
_spec = importlib.util.spec_from_file_location(
    "wpv", HERE / "write_path" / "gen_write_path_vectors.py")
wpv = importlib.util.module_from_spec(_spec)
sys.modules["wpv"] = wpv                # dataclasses look their module up here
_spec.loader.exec_module(wpv)          # type: ignore[union-attr]
SplitMix64 = wpv.SplitMix64


# ---------------------------------------------------------------------------
# Package constants
# ---------------------------------------------------------------------------

def read_pkg() -> dict[str, int]:
    text = PKG.read_text(encoding="utf-8-sig")
    out: dict[str, int] = {}
    for name in ["TILE_ROWS", "TILE_COLS", "NUM_LANES", "NOISE_CLAMP", "LANE_PIPE_DEPTH"]:
        m = re.search(rf"parameter\s+int\s+{name}\s*=\s*(\d[\d_]*)\s*;", text)
        if not m:
            sys.exit(f"cannot find literal 'parameter int {name}' in {PKG}")
        out[name] = int(m.group(1).replace("_", ""))
    for name in ["LFSR_TAPS", "LANE_SALT_C"]:
        m = re.search(rf"parameter\s+logic\s*\[31:0\]\s*{name}\s*=\s*32'h([0-9A-Fa-f_]+)\s*;", text)
        if not m:
            sys.exit(f"cannot find 32'h literal for {name} in {PKG}")
        out[name] = int(m.group(1).replace("_", ""), 16)
    # ACC_USED_W = $clog2(254 * 255 * TILE_ROWS) + 1. Check the formula is
    # still the one in the package, then evaluate it.
    if not re.search(r"ACC_MAX_MAG\s*=\s*254\s*\*\s*255\s*\*\s*TILE_ROWS\s*;", text) or \
       not re.search(r"ACC_USED_W\s*=\s*\$clog2\(ACC_MAX_MAG\)\s*\+\s*1\s*;", text):
        sys.exit("ACC_MAX_MAG / ACC_USED_W formula changed in fecim_pkg.sv; update the generator")
    out["ACC_MAX_MAG"] = 254 * 255 * out["TILE_ROWS"]
    out["ACC_USED_W"] = (out["ACC_MAX_MAG"] - 1).bit_length() + 1
    return out


P = read_pkg()
ROWS, COLS, LANES = P["TILE_ROWS"], P["TILE_COLS"], P["NUM_LANES"]
CLAMP, USED_W, MAX_MAG = P["NOISE_CLAMP"], P["ACC_USED_W"], P["ACC_MAX_MAG"]
QUANT, D2D, READ, STUCK, IR, ADC = (1 << i for i in range(6))
RAIL = 1 << 16
M32 = 0xFFFF_FFFF


# ---------------------------------------------------------------------------
# Reference arithmetic, straight from mac-array-spec §5, §8, §9
# ---------------------------------------------------------------------------

def lfsr_step(s: int) -> int:
    return ((s >> 1) ^ P["LFSR_TAPS"]) if (s & 1) else (s >> 1)


def lane_seed(noise_seed: int, lane: int) -> int:
    return ((noise_seed ^ (P["LANE_SALT_C"] * lane)) & M32) | 1


def noise_q(lfsr: int, sigma: int) -> int:
    ih_sum = (lfsr & 0xFF) + ((lfsr >> 8) & 0xFF) + ((lfsr >> 16) & 0xFF) + (lfsr >> 24)
    ih9 = (ih_sum - 510) >> 1                      # floor, as SV >>>
    scaled = (ih9 * sigma) >> 9                    # READ_SIGMA / 512
    return max(-CLAMP, min(CLAMP, scaled))


def ir_attenuate(act: int, coeff: int) -> int:
    return (act * coeff) >> 8


def s32(v: int) -> int:
    v &= M32
    return v - (1 << 32) if v & 0x8000_0000 else v


def adc_quantize(acc: int, bits: int) -> int:
    bits &= 0x1F                                   # the RTL port is adc_bits[4:0]
    sh = 0 if bits >= USED_W else USED_W - bits
    if sh == 0:
        return acc
    t = s32(acc + (1 << (sh - 1)))                 # 32-bit wrapping add
    return s32((t >> sh) << sh)


def ramp(kappa: float) -> list[int]:
    """First-order IR profile: alpha_i = 1 - kappa * i / ROWS, coeff = min(255, round(256 alpha))."""
    return [min(255, max(0, int(256 * (1 - kappa * i / ROWS) + 0.5))) for i in range(ROWS)]


# ---------------------------------------------------------------------------
# Full model: what model_exact must compute (for mvm/)
# ---------------------------------------------------------------------------

@dataclass
class Cfg:
    rows: int = ROWS
    cols: int = COLS
    noise_en: int = 0
    levels: int = 255
    mult: int = 257
    d2d_sigma: int = 0
    read_sigma: int = 0
    seed: int = 0
    adc_bits: int = USED_W
    stuck_rate: int = 0

    def words(self) -> list[int]:
        return [self.rows, self.cols, self.noise_en, self.levels, self.mult,
                self.d2d_sigma, self.read_sigma, self.seed, self.adc_bits, self.stuck_rate]


class Model:
    def __init__(self) -> None:
        self.cfg = Cfg()
        self.w = [[0] * COLS for _ in range(ROWS)]
        self.act = [0] * ROWS
        self.atten = [0xFF] * ROWS
        self.reseed()

    def reseed(self) -> None:
        self.lfsr = [lane_seed(self.cfg.seed, lane) for lane in range(LANES)]

    def write_weight(self, r: int, c: int, w: int) -> None:
        wc = wpv.Cfg("x", self.cfg.noise_en, self.cfg.levels, self.cfg.mult,
                     self.cfg.d2d_sigma, self.cfg.seed, self.cfg.stuck_rate)
        self.w[r][c] = wpv.write_path(wc, c * ROWS + r, w)

    def compute(self) -> list[int]:
        c = self.cfg
        passes = (c.cols + LANES - 1) // LANES
        y = [0] * c.cols
        for p in range(passes):
            acc = [0] * LANES
            for r in range(c.rows):
                a = ir_attenuate(self.act[r], self.atten[r]) if c.noise_en & IR else self.act[r]
                for lane in range(LANES):
                    self.lfsr[lane] = lfsr_step(self.lfsr[lane])
                    wn = self.w[r][p * LANES + lane]
                    if c.noise_en & READ:
                        wn += noise_q(self.lfsr[lane], c.read_sigma)
                    acc[lane] += wn * a
            for lane in range(LANES):
                col = p * LANES + lane
                if col < c.cols:
                    y[col] = adc_quantize(acc[lane], c.adc_bits) if c.noise_en & ADC else acc[lane]
        return y


# ---------------------------------------------------------------------------
# Emit helpers
# ---------------------------------------------------------------------------

def h32(v: int) -> str:
    return f"{v & M32:08X}"


def header(gen_for: str) -> list[str]:
    return [
        "// GENERATED by tb/golden/gen_read_path_vectors.py -- do not edit.",
        f"// seed 0x{SEED:08X}; package: TILE_ROWS={ROWS} NUM_LANES={LANES} "
        f"NOISE_CLAMP={CLAMP} ACC_USED_W={USED_W} LFSR_TAPS=0x{P['LFSR_TAPS']:08X} "
        f"LANE_SALT_C=0x{P['LANE_SALT_C']:08X}",
        f"// {gen_for}",
    ]


def text(lines: list[str]) -> str:
    return NL.join(lines) + NL


# ---------------------------------------------------------------------------
# read_noise/
# ---------------------------------------------------------------------------

SIGMAS_EDGE = (0, 1, 2, 64, 127, 128, 254, 255)
STATES_EDGE = (
    0x0000_0001,   # ih_sum = 1 -> ih9 = -255: the one input the clamp changes (sigma 255)
    0x0000_0000,   # unreachable from a seed, but the function is defined there
    0xFFFF_FFFF,   # ih_sum = 1020 -> ih9 = +255
    0x8080_8080,   # ih_sum = 512 -> +1
    0x7F7F_7F7F,   # ih_sum = 508 -> -1
    0x7F7F_7F80,   # ih_sum = 509 -> (-1) >>> 1 = -1 (floor, not 0)
    0x7F7F_7F81,   # ih_sum = 510 -> 0
    0x7F7F_7F82,   # ih_sum = 511 -> 0
    0x0100_0000,   # one high byte
    0xFF00_0000,
    P["LFSR_TAPS"],
)
LANES_SEQ = (0, 1, 5, 31, 63)
SEQ_STEPS = 2 * ROWS       # one full two-pass compute


def gen_read_noise(rnd: SplitMix64) -> dict[str, str]:
    files: dict[str, str] = {}

    # noise_q.hex: the combinational sample, directed + random.
    rows = [(s, g) for s in STATES_EDGE for g in SIGMAS_EDGE]
    rows += [(rnd.next() & M32, rnd.below(256)) for _ in range(1500)]
    lines = header("read_noise_q(lfsr, read_sigma): mac-array-spec sec. 5.3") + [
        "// Word 0: number of entries. Then 3 words per entry:",
        "//   lfsr  read_sigma  noise_q      (noise_q is int8, two's complement in [7:0])",
        h32(len(rows)),
    ] + [f"{h32(s)} {h32(g)} {h32(noise_q(s, g) & 0xFF)}" for s, g in rows]
    files["noise_q.hex"] = text(lines)

    # sequences.hex: per-lane LFSR + sample, from reseed, for one full compute.
    seqs = []
    for lane in LANES_SEQ:
        seqs.append((lane, 0x0000_0000, 255))
    seqs += [(5, 0xDEAD_BEEF, 64), (63, 0xFFFF_FFFF, 128), (31, 0x0000_0001, 255),
             # noise_seed chosen so seed ^ LANE_SALT == 0: the | 1 guard is what keeps it alive
             (5, (P["LANE_SALT_C"] * 5) & M32, 255)]
    lines = header("per-lane LFSR sequences from reseed: mac-array-spec sec. 5.1-5.2") + [
        "// Word 0: number of sequences. Each sequence is 4 header words then",
        f"// {SEQ_STEPS} x 2 words, one pair per accumulate (acc_en cycle):",
        "//   lane_id  noise_seed  read_sigma  n_steps",
        "//   lfsr_after_step  noise_q      (repeated n_steps times)",
        "// Step k's lfsr is the state AFTER the k-th advance; noise_q is computed from it.",
        h32(len(seqs)),
    ]
    for lane, seed, sigma in seqs:
        lines.append(f"// lane {lane}, seed 0x{seed:08X}, sigma {sigma}, "
                     f"seed_eff 0x{lane_seed(seed, lane):08X}")
        lines.append(f"{h32(lane)} {h32(seed)} {h32(sigma)} {h32(SEQ_STEPS)}")
        s = lane_seed(seed, lane)
        for _ in range(SEQ_STEPS):
            s = lfsr_step(s)
            lines.append(f"{h32(s)} {h32(noise_q(s, sigma) & 0xFF)}")
    files["sequences.hex"] = text(lines)

    # lane_cases.hex / lane_rows.hex: one mac_lane, read noise on/off, LFSR carried.
    @dataclass
    class LC:
        name: str
        lane: int
        pss: int
        reseed: bool
        read: bool
        seed: int
        sigma: int
        rows: list[tuple[int, int]] = field(default_factory=list)
        purpose: str = ""

    def rand_rows(n: int) -> list[tuple[int, int]]:
        return [(rnd.below(256) - 128, rnd.below(256)) for _ in range(n)]

    cases: list[LC] = []
    for lane in (0, 5, 63):
        cases += [
            LC("reseed_s255_p0", lane, 0, True, True, 0, 255, rand_rows(ROWS),
               "full-strength noise from reseed, 128 rows"),
            LC("carry_s255_p1", lane, 1, False, True, 0, 255, rand_rows(ROWS),
               "no reseed: continues the sequence; pass 1 address"),
            LC("reseed_off_50", lane, 0, True, False, 0, 255, rand_rows(50),
               "noise disabled: result is clean, but the LFSR still advances 50 times"),
            LC("carry_on_s64_50", lane, 0, False, True, 0, 64, rand_rows(50),
               "noise from the state AFTER the disabled case's 50 advances"),
            LC("reseed_extremes", lane, 0, True, True, 0, 255,
               [(w, 255) for w in (-128, 127, -128, 127, -1, 0, 1) * 6],
               "w = -128/127 with max noise and act 255: w_n reaches -255 .. +254"),
            LC("reseed_seedFFFFFFFF_s128", lane, 1, True, True, 0xFFFF_FFFF, 128,
               rand_rows(ROWS), "different seed"),
            LC("reseed_s0", lane, 0, True, True, 0x1234, 0, rand_rows(40),
               "sigma 0: noise is exactly 0, result is clean"),
        ]
    lines_c = header("one mac_lane with read noise: mac-array-spec sec. 5") + [
        "// Word 0: number of cases. Then 8 words per case:",
        "//   lane_id  pass  flags  noise_seed  read_sigma  row_base  num_rows  expected",
        "// flags[0] = RESEED before the case (set NOISE_SEED first, then pulse reseed)",
        "// flags[1] = NOISE_EN.read for the case.",
        "// Run each lane's cases in file order, on the mac_lane instance with that",
        "// LANE_ID, WITHOUT resetting between cases: the LFSR state carries over.",
    ]
    lines_r = header("rows of tb/golden/read_noise/lane_cases.hex") + [
        "// Word 0: number of rows. Then 5 words per row:",
        "//   weight  act  noise_q  w_n  running",
        "// noise_q is the sample for this row (the lane computes it even when",
        "// NOISE_EN.read is clear; w_n then ignores it). w_n is 9-bit signed, in",
        "// [8:0] sign-extended to 32 bits. running is the accumulator after this row.",
    ]
    body_c, body_r = [], []
    states = {lane: lane_seed(0, lane) for lane in (0, 5, 63)}
    for i, c in enumerate(cases):
        if c.reseed:
            states[c.lane] = lane_seed(c.seed, c.lane)
        base = len(body_r)
        acc = 0
        for w, a in c.rows:
            states[c.lane] = lfsr_step(states[c.lane])
            q = noise_q(states[c.lane], c.sigma)
            wn = w + (q if c.read else 0)
            acc += wn * a
            body_r.append(f"{h32(w & 0xFF)} {h32(a)} {h32(q & 0xFF)} {h32(wn)} {h32(acc)}")
        flags = (1 if c.reseed else 0) | (2 if c.read else 0)
        body_c.append(f"// {i:2d} lane {c.lane} {c.name}: {c.purpose}")
        body_c.append(f"{h32(c.lane)} {h32(c.pss)} {h32(flags)} {h32(c.seed)} {h32(c.sigma)} "
                      f"{h32(base)} {h32(len(c.rows))} {h32(acc)}")
    files["lane_cases.hex"] = text(lines_c + [h32(len(cases))] + body_c)
    files["lane_rows.hex"] = text(lines_r + [h32(len(body_r))] + body_r)

    lines = [
        "// GENERATED by tb/golden/gen_read_path_vectors.py -- do not edit.",
        "`ifndef READ_NOISE_VECTORS_SVH",
        "`define READ_NOISE_VECTORS_SVH",
        f"localparam int RNV_N_NOISEQ     = {len(rows)};",
        f"localparam int RNV_N_SEQ        = {len(seqs)};",
        f"localparam int RNV_SEQ_STEPS    = {SEQ_STEPS};",
        f"localparam int RNV_N_CASES      = {len(cases)};",
        f"localparam int RNV_N_ROWS       = {len(body_r)};",
        "localparam int RNV_NOISEQ_DEPTH = 1 + RNV_N_NOISEQ * 3;",
        "localparam int RNV_SEQ_WORDS    = 4 + RNV_SEQ_STEPS * 2;",
        "localparam int RNV_SEQ_DEPTH    = 1 + RNV_N_SEQ * RNV_SEQ_WORDS;",
        "localparam int RNV_CASES_DEPTH  = 1 + RNV_N_CASES * 8;",
        "localparam int RNV_ROWS_DEPTH   = 1 + RNV_N_ROWS * 5;",
        "// noise_q k : nq[1 + k*3 + {0 lfsr, 1 sigma, 2 noise_q}]",
        "// seq s     : sq[1 + s*RNV_SEQ_WORDS + {0 lane, 1 seed, 2 sigma, 3 n}];",
        "//             step t: sq[1 + s*RNV_SEQ_WORDS + 4 + t*2 + {0 lfsr, 1 noise_q}]",
        "// case i    : cs[1 + i*8 + {0 lane, 1 pass, 2 flags, 3 seed, 4 sigma, 5 base, 6 n, 7 expected}]",
        "// row j     : rw[1 + j*5 + {0 weight, 1 act, 2 noise_q, 3 w_n, 4 running}]",
        "`endif",
    ]
    files["read_noise_vectors.svh"] = text(lines)
    return files


# ---------------------------------------------------------------------------
# ir_drop/
# ---------------------------------------------------------------------------

EDGE8 = (0, 1, 2, 127, 128, 129, 254, 255)


def gen_ir(rnd: SplitMix64) -> dict[str, str]:
    files: dict[str, str] = {}
    pairs = [(a, k) for a in EDGE8 for k in EDGE8]
    pairs += [(rnd.below(256), rnd.below(256)) for _ in range(512)]
    lines = header("ir_attenuate(act, coeff) = (act * coeff) >> 8: mac-array-spec sec. 8") + [
        "// Word 0: number of entries. Then 3 words per entry:  act  coeff  act_atten",
        h32(len(pairs)),
    ] + [f"{h32(a)} {h32(k)} {h32(ir_attenuate(a, k))}" for a, k in pairs]
    files["atten.hex"] = text(lines)

    c = FNV_OFFSET
    for a in range(256):
        for k in range(256):
            c = ((c ^ ir_attenuate(a, k)) * FNV_PRIME) & M32
    lines = header("exhaustive check of ir_attenuate over all 65,536 (act, coeff) pairs") + [
        "// One word: c = 0x811C9DC5; for act = 0..255 { for coeff = 0..255 {",
        "//   c = ((c ^ ir_attenuate(act, coeff)) * 0x01000193) mod 2^32 } }",
        h32(c),
    ]
    files["atten_checksum.hex"] = text(lines)

    # Activation-buffer profiles: what act_bcast must be, row by row.
    profiles = [
        ("reset_coeffs_ir_on", True, [0xFF] * ROWS, "reset table, IR on: 255/256, NOT the identity"),
        ("reset_coeffs_ir_off", False, [0xFF] * ROWS, "IR off: exact pass-through"),
        ("ramp_k0.25_ir_on", True, ramp(0.25), "first-order profile, kappa 0.25"),
        ("ramp_k0.5_ir_on", True, ramp(0.5), "first-order profile, kappa 0.5"),
        ("random_ir_on", True, [rnd.below(256) for _ in range(ROWS)], "random coefficients"),
        ("random_ir_off", False, [rnd.below(256) for _ in range(ROWS)],
         "random coefficients but IR off: must be ignored"),
        ("extremes_ir_on", True, [(0, 1, 128, 255)[i % 4] for i in range(ROWS)],
         "0, 1, 128, 255 cycling"),
    ]
    lines = header("act_buffer broadcast with IR drop: activation-buffer-spec sec. 6.2") + [
        "// Word 0: number of profiles. Then per profile 1 + 3 x TILE_ROWS words:",
        "//   ir_enable",
        "//   coeff[0..TILE_ROWS-1]      (write each with WRITE_ATTEN, bulk_addr = row)",
        "//   act[0..TILE_ROWS-1]        (write each with WRITE_ACT,   bulk_addr = row)",
        "//   bcast[0..TILE_ROWS-1]      (expected act_bcast when act_addr = row)",
        h32(len(profiles)),
    ]
    for name, en, coeffs, purpose in profiles:
        acts = [rnd.below(256) for _ in range(ROWS)]
        acts[0], acts[1], acts[2] = 255, 0, 128
        lines.append(f"// {name}: {purpose}")
        lines.append(h32(1 if en else 0))
        lines += [" ".join(h32(k) for k in coeffs[i:i + 16]) for i in range(0, ROWS, 16)]
        lines += [" ".join(h32(a) for a in acts[i:i + 16]) for i in range(0, ROWS, 16)]
        out = [ir_attenuate(a, k) if en else a for a, k in zip(acts, coeffs)]
        lines += [" ".join(h32(v) for v in out[i:i + 16]) for i in range(0, ROWS, 16)]
    files["profiles.hex"] = text(lines)

    lines = [
        "// GENERATED by tb/golden/gen_read_path_vectors.py -- do not edit.",
        "`ifndef IR_DROP_VECTORS_SVH",
        "`define IR_DROP_VECTORS_SVH",
        f"localparam int IRV_N_ATTEN      = {len(pairs)};",
        f"localparam int IRV_N_PROFILES   = {len(profiles)};",
        "localparam int IRV_ATTEN_DEPTH  = 1 + IRV_N_ATTEN * 3;",
        f"localparam int IRV_PROFILE_WORDS = 1 + 3 * {ROWS};",
        "localparam int IRV_PROFILE_DEPTH = 1 + IRV_N_PROFILES * IRV_PROFILE_WORDS;",
        "// entry k   : at[1 + k*3 + {0 act, 1 coeff, 2 act_atten}]",
        f"// profile p : pr[1 + p*IRV_PROFILE_WORDS + 0] = ir_enable;",
        f"//             coeff[r] at +1+r, act[r] at +1+{ROWS}+r, bcast[r] at +1+{2 * ROWS}+r",
        "`endif",
    ]
    files["ir_drop_vectors.svh"] = text(lines)
    return files


# ---------------------------------------------------------------------------
# adc/
# ---------------------------------------------------------------------------

def gen_adc(rnd: SplitMix64) -> dict[str, str]:
    cases: list[tuple[int, int, int]] = []        # acc, bits, en
    for bits in range(32):
        sh = 0 if bits >= USED_W else USED_W - bits
        accs = {0, 1, -1, MAX_MAG, -MAX_MAG, MAX_MAG - 1, -MAX_MAG + 1}
        if sh:
            half, step = 1 << (sh - 1), 1 << sh
            for v in (half - 1, half, half + 1, step - 1, step, step + half, 3 * half):
                accs |= {v, -v}
        accs = {v for v in accs if -MAX_MAG <= v <= MAX_MAG}
        accs |= {rnd.below(2 * MAX_MAG + 1) - MAX_MAG for _ in range(24)}
        cases += [(v, bits, 1) for v in sorted(accs)]
    # Outside the used range: defines the RTL's 32-bit wrap (not physical).
    for v in (0x7FFF_FFFF, -0x8000_0000, 0x7FFF_FF00, -0x7FFF_FF00):
        for bits in (0, 4, 8, 23):
            cases.append((v, bits, 1))
    # Disabled: pass-through whatever ADC_BITS says.
    for _ in range(64):
        cases.append((rnd.below(2 * MAX_MAG + 1) - MAX_MAG, rnd.below(32), 0))
    lines = header("adc_quantize(acc, adc_bits): mac-array-spec sec. 9") + [
        "// Word 0: number of cases. Then 4 words per case:",
        "//   acc  adc_bits  adc_enable  expected",
        "// acc and expected are int32. adc_bits is the 5-bit port value (0..31).",
        "// Cases with |acc| > ACC_MAX_MAG are outside the physical range; they pin",
        "// the RTL's 32-bit wrapping add and are listed last before the disabled ones.",
        h32(len(cases)),
    ] + [f"{h32(v)} {h32(b)} {h32(e)} {h32(adc_quantize(v, b) if e else v)}" for v, b, e in cases]
    files = {"cases.hex": text(lines)}
    files["adc_vectors.svh"] = text([
        "// GENERATED by tb/golden/gen_read_path_vectors.py -- do not edit.",
        "`ifndef ADC_VECTORS_SVH",
        "`define ADC_VECTORS_SVH",
        f"localparam int ADCV_N_CASES = {len(cases)};",
        "localparam int ADCV_DEPTH   = 1 + ADCV_N_CASES * 4;",
        "// case k : cs[1 + k*4 + {0 acc, 1 adc_bits, 2 adc_enable, 3 expected}]",
        "`endif",
    ])
    return files


# ---------------------------------------------------------------------------
# mvm/
# ---------------------------------------------------------------------------

F_RESEED, F_WEIGHTS, F_ACTS, F_ATTEN = 1, 2, 4, 8


def gen_mvm(rnd: SplitMix64) -> dict[str, str]:
    setA = dict(levels=8, mult=wpv.quant_mult_for(8), d2d_sigma=18)

    def rw() -> int:
        return rnd.below(256) - 128

    def ra() -> int:
        return rnd.below(256)

    plan = [
        ("clean_16x100", Cfg(rows=16, cols=100), F_WEIGHTS | F_ACTS,
         "clean, two passes, LFSRs from reset (they advance anyway)"),
        ("read_s64_no_reseed", Cfg(rows=16, cols=100, noise_en=READ, read_sigma=64), 0,
         "same data; noise continues from the state the clean compute left"),
        ("read_s64_again", Cfg(rows=16, cols=100, noise_en=READ, read_sigma=64), 0,
         "identical inputs, no reseed: must differ from the previous case"),
        ("read_s64_reseed", Cfg(rows=16, cols=100, noise_en=READ, read_sigma=64), F_RESEED,
         "reseed with seed 0: the sequence a freshly reset chip would give"),
        ("read_s255_seedBEEF_48x48", Cfg(rows=48, cols=48, noise_en=READ, read_sigma=255,
                                         seed=0xBEEF), F_RESEED | F_WEIGHTS | F_ACTS,
         "one pass, full-strength noise"),
        ("ir_ramp_k0.3_64x64", Cfg(rows=64, cols=64, noise_en=IR),
         F_WEIGHTS | F_ACTS | F_ATTEN, "IR only"),
        ("ir_reset_coeffs_64x64", Cfg(rows=64, cols=64, noise_en=IR), F_ATTEN,
         "IR on with the reset table (all 255): not the identity"),
        ("adc8_32x128", Cfg(rows=32, cols=128, noise_en=ADC, adc_bits=8),
         F_WEIGHTS | F_ACTS, "ADC only, 8 bits"),
        ("adc4_32x128", Cfg(rows=32, cols=128, noise_en=ADC, adc_bits=4), 0, "4 bits"),
        ("adc12_32x128", Cfg(rows=32, cols=128, noise_en=ADC, adc_bits=12), 0, "12 bits"),
        ("adc31_32x128", Cfg(rows=32, cols=128, noise_en=ADC, adc_bits=31), 0,
         "bits >= ACC_USED_W: exact no-op"),
        ("adc4_but_disabled", Cfg(rows=32, cols=128, noise_en=0, adc_bits=4), 0,
         "ADC_BITS = 4 with NOISE_EN.adc clear: must be ignored"),
        ("adc32_reaches_port_as_0", Cfg(rows=32, cols=128, noise_en=ADC, adc_bits=32), 0,
         "ADC_BITS = 32 is outside the driver's range; the 5-bit port sees 0 (one bit)"),
        ("d2d_without_reload", Cfg(rows=32, cols=128, noise_en=D2D, d2d_sigma=255), 0,
         "write-time setting changed but weights not rewritten: no effect"),
        ("setA_all_six_64x48", Cfg(rows=64, cols=48, noise_en=0x3F, **setA, read_sigma=12,
                                   seed=0xC0FFEE, adc_bits=10, stuck_rate=655 | RAIL),
         F_RESEED | F_WEIGHTS | F_ACTS | F_ATTEN, "set A + read noise + IR + ADC + 1 % rail"),
        ("all_six_128x128", Cfg(noise_en=0x3F, levels=5, mult=wpv.quant_mult_for(5),
                                d2d_sigma=32, read_sigma=40, seed=0x2026, adc_bits=12,
                                stuck_rate=6554),
         F_RESEED | F_WEIGHTS | F_ACTS | F_ATTEN, "full tile, every effect on"),
        ("all_six_128x128_again", Cfg(noise_en=0x3F, levels=5, mult=wpv.quant_mult_for(5),
                                      d2d_sigma=32, read_sigma=40, seed=0x2026, adc_bits=12,
                                      stuck_rate=6554), 0,
         "no reseed: second compute on the same chip"),
    ]
    atten_for = {"ir_ramp_k0.3_64x64": ramp(0.3), "ir_reset_coeffs_64x64": [0xFF] * ROWS,
                 "setA_all_six_64x48": ramp(0.1), "all_six_128x128": ramp(0.2)}

    m = Model()
    case_lines, data = [], []
    results: dict[str, list[int]] = {}
    for i, (name, cfg, flags, purpose) in enumerate(plan):
        base = len(data)
        m.cfg = cfg
        if flags & F_WEIGHTS:
            for c in range(cfg.cols):                 # column-major, as the bulk stream
                for r in range(cfg.rows):
                    w = rw()
                    data.append(w & 0xFF)
                    m.write_weight(r, c, w)
        if flags & F_ACTS:
            for r in range(cfg.rows):
                a = ra()
                data.append(a)
                m.act[r] = a
        if flags & F_ATTEN:
            for r in range(ROWS):
                data.append(atten_for[name][r])
                m.atten[r] = atten_for[name][r]
        if flags & F_RESEED:
            m.reseed()
        y = m.compute()
        results[name] = y
        data += y
        case_lines.append(f"// {i:2d} {name}: {purpose}")
        case_lines.append(" ".join(h32(v) for v in cfg.words()) + f" {h32(flags)} {h32(base)}")

    # Each case must test what its name says. Fail generation, not silently.
    r = results
    assert r["read_s64_again"] != r["read_s64_no_reseed"], "LFSR state must persist"
    assert r["read_s64_reseed"] != r["read_s64_no_reseed"], "reseed must matter"
    assert r["read_s64_no_reseed"] != r["clean_16x100"], "noise must change results"
    assert r["adc31_32x128"] == r["d2d_without_reload"] == r["adc4_but_disabled"], \
        "no-op ADC / no-reload D2D / disabled ADC"
    assert r["adc32_reaches_port_as_0"] != r["adc31_32x128"], "5-bit port"
    assert r["adc8_32x128"] != r["adc31_32x128"] != r["adc4_32x128"]
    assert r["all_six_128x128_again"] != r["all_six_128x128"]
    assert len({v for v in r["adc4_32x128"]}) < len({v for v in r["adc12_32x128"]})

    lines_c = header("whole computes, all six non-idealities: model_exact end to end") + [
        "// Word 0: number of cases. Then 12 words per case:",
        "//   active_rows active_cols noise_en quant_levels quant_mult d2d_sigma",
        "//   read_sigma noise_seed adc_bits stuck_rate  flags  data_base",
        "// Run in order on ONE fresh model/design (reset once at the start; nothing",
        "// is reset between cases). Per case: apply the config; then, at data_base in",
        "// data.hex, if flags[1] write active_rows x active_cols weights (column-major:",
        "// col outer, row inner, bulk_addr = col*TILE_ROWS + row); if flags[2] write",
        "// active_rows activations; if flags[3] write TILE_ROWS attenuation",
        "// coefficients; if flags[0] reseed; COMPUTE; then active_cols int32 results",
        "// follow and must match exactly.",
        h32(len(plan)),
    ] + case_lines
    lines_d = header("data for tb/golden/mvm/cases.hex") + [
        "// Word 0: number of words. Weights int8 and acts/coeffs uint8 in [7:0];",
        "// results int32.",
        h32(len(data)),
    ] + [" ".join(h32(v) for v in data[i:i + 8]) for i in range(0, len(data), 8)]
    return {"cases.hex": text(lines_c), "data.hex": text(lines_d)}


# ---------------------------------------------------------------------------

def generate() -> dict[Path, str]:
    out: dict[Path, str] = {}
    # One PRNG per folder, so adding a case to one folder never reshuffles
    # another folder's vectors.
    for folder, fn, salt in (("read_noise", gen_read_noise, 1), ("ir_drop", gen_ir, 2),
                             ("adc", gen_adc, 3), ("mvm", gen_mvm, 4)):
        for name, body in fn(SplitMix64(SEED ^ (salt << 32))).items():
            out[HERE / folder / name] = body
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if the committed files differ from a fresh generation")
    args = ap.parse_args()
    files = generate()
    stale = []
    for path, body in files.items():
        if args.check:
            old = path.read_bytes().replace(b"\r\n", b"\n") if path.exists() else None
            if old != body.replace(NL, "\n").encode("ascii"):
                stale.append(str(path.relative_to(HERE)))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(body.encode("ascii"))
    if args.check:
        if stale:
            print("stale read-path vectors (rerun gen_read_path_vectors.py and commit): "
                  + ", ".join(stale))
            return 1
        print(f"read-path vectors up to date ({len(files)} files)")
        return 0
    print("wrote " + ", ".join(str(p.relative_to(HERE)) for p in files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
