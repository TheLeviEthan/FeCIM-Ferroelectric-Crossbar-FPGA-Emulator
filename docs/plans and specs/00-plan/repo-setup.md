# FeCIM: Repository Setup

Week-1 setup. Two audiences: the M0 grader (needs contribution evidence) and a hiring
engineer (needs to see engineering in the first screen). Both are served by putting the
credibility artifacts in the README and the coursework artifacts one level down.

---

## 1. Decisions to make first

**Name it `fecim`.** Short, searchable, memorable. Not `cen4907c-capstone` — course codes
date the work and mean nothing outside UF.

**Public from day one**, not private-then-flipped. The commit history showing the project
evolve is an asset, and flipping later gains nothing. Two things to check first: whether
the capstone course permits public repos (some prohibit it over academic integrity), and
whether UF claims IP on capstone work (usually not for coursework, but ask before
licensing).

**Single repo, not four.** One system with an internal A/B seam. Splitting by lane would
fragment the story and make the joint integration testbench awkward.

**License: MIT.** Simple, permissive, and appropriate for a tool meant to be used. Apache-2.0
if you want explicit patent language.

---

## 2. Directory structure

```
fecim/
├── README.md                   ← carries all portfolio weight
├── LICENSE
├── .gitignore
├── .github/
│   ├── workflows/ci.yml
│   ├── ISSUE_TEMPLATE/
│   └── pull_request_template.md
├── rtl/
│   ├── fecim_pkg.sv            ← single source of truth
│   ├── uart/ packet/ control/  ← Lane A
│   └── array/ write/ result/   ← Lane B
├── model/
│   ├── src/ include/ tests/    ← C++ golden model, Lane C
│   └── cosim/                   ← Verilator harness
├── sw/
│   ├── fecim/                   ← Python package, Lane D
│   ├── examples/ tests/
│   └── pyproject.toml
├── tb/
│   ├── sv/                      ← SystemVerilog testbenches, SVA
│   └── golden/                  ← shared byte fixtures with hand-computed CRCs
├── quartus/
│   ├── fecim.qpf / .qsf
│   └── reports/                 ← COMMIT THESE (see §5)
├── docs/
│   ├── protocol.md
│   ├── device-model.md
│   ├── hardware.md
│   ├── hardware-verification.md
│   └── course/                  ← meeting logs, M0 deliverables, planning docs
└── results/                     ← sweep data, plots
```

`docs/course/` is where M0's meeting logs and planning documents live. They are committed
and auditable, they just aren't the first thing a visitor sees.

---

## 3. The README is the whole game

An engineer decides what they think of you from this page. Order matters — put the numbers
above the fold.

```markdown
# FeCIM

Digital emulator of a ferroelectric compute-in-memory MAC accelerator, in
SystemVerilog on a DE10-Lite (Intel MAX 10). Device non-idealities are injected
numerically and individually switchable, so accuracy loss can be attributed to a
specific physical mechanism.

![board running inference](docs/img/board.jpg)

## Status
[![CI](badge)]() — RTL verified bit-exact against the C++ reference model.

## Resource utilization (10M50DAF484C7G)

| Configuration | Logic elements | M9K | DSP blocks | MVM latency |
|---|---|---|---|---|
| 64×64, 32 lanes  | 10,400 (21%) | 34 (19%) | 40 (28%) | 4.0 µs |
| 128×128, 64 lanes | 18,300 (37%) | 66 (36%) | 72 (50%) | 7.8 µs |

Every multiply is 9×9 signed so two share one DSP block — see
[docs/hardware-verification.md](docs/hardware-verification.md).

## Try it without hardware

    pip install fecim
    python -m fecim.examples.digits --backend sim

The `sim` backend is the same C++ model the RTL is verified against, so results
are bit-identical to the board at a matched seed.

## Architecture
[diagram]

## Docs
- Protocol and register map
- Device model and parameter provenance
- Hardware verification and errata
```

Three things doing the work here: **a photo of the board** (signals real hardware
instantly), **a resource table** (checkable numbers, immediately), and **a quick start
that runs with no board** (almost no student repo lets a visitor actually execute
anything).

---

## 4. `.gitignore`

Quartus generates an enormous amount of junk. Getting this wrong on the first commit is
the most common way a hardware repo looks unprofessional.

```gitignore
# Quartus
db/
incremental_db/
simulation/
greybox_tmp/
hc_output/
*.qws
*.rpt.bak
output_files/*
!output_files/*.sof
!output_files/*.rpt

# Verilator / C++
obj_dir/
build/
*.o
*.a
*.vcd
*.fst

# Python
__pycache__/
*.egg-info/
.pytest_cache/
dist/
build/
.venv/

# Data (fetch by script, never commit)
results/raw/
sw/fecim/datasets/

# Editor
.vscode/
.idea/
*.swp
.DS_Store
```

**Never commit datasets.** Write a fetch script. A repo with MNIST checked in reads as
carelessness.

---

## 5. What to commit that you might not think to

**Quartus fitter and timing reports**, in `quartus/reports/`, one per tagged release.
These are your credibility artifacts and reconstructing them in March is miserable. The
fitter report is also where you verify DSP packing actually happened — look for
`Embedded Multiplier 9-bit elements`; if it reports 128 blocks instead of 64, pairing
failed.

**Bitstreams go in GitHub Releases, not the repo.** Same for Python wheels. This satisfies
M0's binary-distribution requirement and keeps the repo small. Tag `v0.1-mvp` at week 6.

**Waveform screenshots from bring-up**, in `docs/img/`. Especially a failure and its fix.
Nobody does this and it's disproportionately convincing.

---

## 6. Branching and PRs

M0 requires that items be "merged within the repository system directly to create a clear
trail," which means pull requests, not local merges.

- `main` protected: no direct pushes, one approving review required
- Branches named by lane: `a/uart-rx`, `b/mac-lane`, `c/cosim`, `d/driver`
- **`rtl/fecim_pkg.sv` requires review from all four.** Add a `CODEOWNERS` file so GitHub
  enforces it rather than relying on memory.

**Use merge commits, not squash.** Squashing is the industry default and gives cleaner
history, but it collapses individual commits — and M0 measures contribution against
third-party logs. Granular history is better evidence. This is the one place to prefer the
coursework requirement over the industry convention.

```
# .github/CODEOWNERS
/rtl/fecim_pkg.sv    @you @megan @zhiting @natalie
/docs/protocol.md    @you @megan @zhiting @natalie
```

---

## 7. CI

A green badge is worth more than it looks. Quartus cannot run in Actions (licensed, huge),
so CI covers lint, simulation, the C++ model, and Python. Synthesis results are committed
manually per §5.

```yaml
name: CI
on: [push, pull_request]

jobs:
  lint:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y verilator
      - run: verilator --lint-only -Wall --top-module fecim_top
             rtl/fecim_pkg.sv $(find rtl -name '*.sv' -not -name 'fecim_pkg.sv')

  model:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: cmake -S model -B build && cmake --build build
      - run: ./build/model_tests

  cosim:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y verilator
      - run: make -C model/cosim
      - run: ./model/cosim/run_l0        # bit-exact gate, clean datapath

  python:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with: { python-version: '3.11' }
      - run: pip install -e ./sw[test]
      - run: pytest sw/tests
```

**`-Wall` on the lint job matters.** Width-mismatch warnings are exactly how you'd catch a
32-bit accumulator silently truncated somewhere — a bug that produces plausible small
numbers and survives casual testing.

The `cosim` L0 gate is the important one. If it goes red, feature work stops.

Add `cibuildwheel` on tags later for the binary distributions.

---

## 8. Issues and project board

M0 requires an issue tracker with task assignment, actually used. GitHub Issues plus a
Projects board satisfies it and doubles as organization evidence.

Labels: `lane-a` `lane-b` `lane-c` `lane-d`, plus `gate` for the week-3/6/7 milestones and
`blocked`.

Two habits that make the board real rather than decorative: **every issue has an assignee**,
and **PRs reference their issue** (`Closes #42`). An empty board with a full commit log is
worse than no board — it suggests the process was performative.

Create the week-1 freeze items as issues immediately: protocol, register map, A/B seam
signal list, dongle ordering, literature survey assignments.

---

## 9. Commit hygiene

Prefix by area — `rtl:`, `model:`, `sw:`, `docs:`, `ci:` — and write what changed, not
"fix."

Two patterns that read badly to anyone reviewing the history:

**Silence then a flood.** Three weeks of nothing followed by 400 commits in one day says
the work happened elsewhere. Commit as you go, including partial work on a branch.

**One author.** If the log shows you authored 80% of commits, that is what an M0 reviewer
sees and what a recruiter sees. Push back on teammates who batch their work through you.

---

## 10. Week-1 checklist

- [ ] Repo created, named `fecim`, public (after confirming the course allows it)
- [ ] MIT `LICENSE`, `.gitignore` from §4
- [ ] README skeleton with the resource table stubbed — fill numbers as they land
- [ ] Directory structure from §2, with `.gitkeep` in empty dirs
- [ ] `main` protected, one-review requirement, `CODEOWNERS` added
- [ ] CI running and green (lint job alone is enough to start)
- [ ] All four members have pushed at least one commit
- [ ] Week-1 freeze items filed as assigned issues
- [ ] `docs/course/meetings/` with the first meeting log
