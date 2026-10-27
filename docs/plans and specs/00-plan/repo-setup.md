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


# Development Environment Setup

This guide walks through setting up a Windows machine to work on the FeCIM Ferroelectric Crossbar FPGA Emulator. All development (simulation, Python, C++, Git) happens inside **WSL 2 (Ubuntu 24.04)**, with VS Code on Windows connected to it.

**Target toolchain**

| Tool | Version |
|---|---|
| Verilator | 5.x (Ubuntu 24.04 apt provides 5.020) |
| Python | 3.11 |
| C++ compiler | g++ 13 with C++17 support |
| Git | any recent version |

> **Why WSL?** Verilator does not run well natively on Windows, CI runs on Linux, and keeping Python, Verilator, and the compiler in one environment avoids version drift. Windows-only FPGA tools (Quartus) can still run on Windows for synthesis and programming the board.

---

## 1. Install WSL and Ubuntu 24.04

Open **PowerShell as Administrator** (right-click Start → *Terminal (Admin)*) and run:

```powershell
wsl --install -d Ubuntu-24.04
```

Restart your computer when it finishes.

If you see `ERROR_ALREADY_EXISTS`, Ubuntu 24.04 is already installed. Skip ahead.

### Confirm the install

In PowerShell (no admin needed):

```powershell
wsl -l -v
```

`Ubuntu-24.04` should be listed with **VERSION 2**. If it shows 1:

```powershell
wsl --set-version Ubuntu-24.04 2
```

### Set Ubuntu 24.04 as the default

If another distro (such as `docker-desktop`) has the `*`, change the default:

```powershell
wsl --set-default Ubuntu-24.04
```

---

## 2. First launch of Ubuntu

Enter Ubuntu from PowerShell:

```powershell
wsl -d Ubuntu-24.04
```

Your prompt should change to something like `username@MACHINE:~$`. You are now in Linux.

On first launch, Ubuntu asks you to create a username and password. The password does not appear as you type; that is normal. You will need it for `sudo`.

> **Note:** Linux commands (`sudo`, `apt`, `&&`) only work inside Ubuntu, not in PowerShell.

### Forgot your Ubuntu password?

From PowerShell:

```powershell
wsl -d Ubuntu-24.04 -u root
```

Then, at the `#` prompt:

```bash
ls /home              # shows your username
passwd <username>     # set a new password
exit
```

### Update Ubuntu

```bash
cd ~
sudo apt update && sudo apt upgrade -y
```

---

## 3. Configure Git and SSH

WSL has its own Git, separate from Git on Windows, so configure it here.

```bash
git config --global user.name "Your Name"
git config --global user.email "your-github-email@example.com"
```

### Create an SSH key

Check for an existing key first:

```bash
ls ~/.ssh
```

If `id_ed25519` and `id_ed25519.pub` are not there, generate one:

```bash
ssh-keygen -t ed25519 -C "your-github-email@example.com"
```

Press Enter to accept the default location. A passphrase is optional; pressing Enter twice skips it.

### Add the key to GitHub

```bash
cat ~/.ssh/id_ed25519.pub
```

Copy the entire line starting with `ssh-ed25519` (use right-click or Ctrl+Shift+C in the terminal). Only copy the `.pub` file; never share the private key.

On GitHub: **Profile picture → Settings → SSH and GPG keys → New SSH key**. Give it a title (e.g. "Laptop WSL"), choose *Authentication Key*, paste, and save.

### Test the connection

```bash
ssh -T git@github.com
```

Type `yes` at the fingerprint prompt. A successful setup prints:

```
Hi <username>! You've successfully authenticated, but GitHub does not provide shell access.
```

---

## 4. Clone the repository

Clone into the **Linux filesystem** (`~`), not into `/mnt/c/...`. Working across the Windows/Linux boundary is significantly slower for builds and simulation.

```bash
mkdir -p ~/projects && cd ~/projects
git clone git@github.com:TheLeviEthan/FeCIM-Ferroelectric-Crossbar-FPGA-Emulator.git
cd FeCIM-Ferroelectric-Crossbar-FPGA-Emulator
```

Verify:

```bash
git status       # "Your branch is up to date with 'origin/main'"
git remote -v    # should show the git@github.com:... URL
```

> To push branches, you must be added as a collaborator on the repository.

---

## 5. Connect VS Code to WSL

1. Install **VS Code on Windows** (not inside Ubuntu).
2. In VS Code, install the **WSL** extension by Microsoft.
3. From the Ubuntu terminal, inside the repo folder, run:

   ```bash
   code .
   ```

The first run prints `Installing VS Code Server` and takes a minute or two. VS Code then opens on Windows with the project loaded.

**Check the bottom-left corner:** it should say **WSL: Ubuntu-24.04**. If it does not, you are editing on the Windows side and the Linux tools will not be found.

Inside this window:

- The integrated terminal (`` Ctrl+` ``) is a bash shell in the project folder.
- When installing extensions (Python, C/C++, Verilog/SystemVerilog), click **Install in WSL**.

If `code .` does nothing or errors, open VS Code on Windows, click the **><** button in the bottom-left corner, choose **Connect to WSL using Distro → Ubuntu-24.04**, then **File → Open Folder** and open `~/projects/FeCIM-Ferroelectric-Crossbar-FPGA-Emulator`.

Once VS Code is connected, you can close PowerShell and the separate Ubuntu window.

---

## 6. Install the toolchain

Run these in the VS Code terminal (WSL):

```bash
sudo apt update
sudo apt install -y build-essential git verilator software-properties-common

# Ubuntu 24.04 ships Python 3.12; the deadsnakes PPA provides 3.11
sudo add-apt-repository -y ppa:deadsnakes/ppa
sudo apt update
sudo apt install -y python3.11 python3.11-venv python3.11-dev
```

`build-essential` provides `g++` and `make`.

### Verify

```bash
verilator --version && python3.11 --version && g++ --version
```

Expected output (versions may be slightly newer):

```
Verilator 5.020 2024-01-01 rev (Debian 5.020-1)
Python 3.11.x
g++ (Ubuntu 13.3.0-...) 13.3.0
```

Quick C++17 check:

```bash
echo 'int main(){ if constexpr (true) return 0; }' > /tmp/t.cpp \
  && g++ -std=c++17 /tmp/t.cpp -o /tmp/t && echo "C++17 OK"
```

---

## 7. Python virtual environment

From the repo root:

```bash
python3.11 -m venv .venv
source .venv/bin/activate
python --version     # Python 3.11.x
```

In VS Code: **Ctrl+Shift+P → Python: Select Interpreter → `.venv`**. New terminals will then activate it automatically.

Keep `.venv` out of commits. If `git status` lists it as untracked:

```bash
echo ".venv/" >> .git/info/exclude
```

### Python dependencies

The repo does not currently include a `requirements.txt`. To see which third-party packages the Python code uses:

```bash
grep -rhE "^\s*(import|from) \w+" --include="*.py" . --exclude-dir=.venv | sort -u
```

Install anything outside the standard library with `pip install <package>` while the venv is active. Once dependencies are known, a `requirements.txt` can be generated for the team with:

```bash
pip freeze > requirements.txt
```

---

## 8. Running CI checks locally

Check whether the repo defines CI workflows:

```bash
ls -a
ls .github/workflows
```

If workflows exist, open each `.yml` file and run the commands listed under its `run:` steps, in order, from the repo root. If they all complete without errors, CI passes locally.

If no workflows exist, follow the build and test instructions in `README.md` and `docs/`.

---

## 9. Daily workflow

### Opening the project

Open VS Code → **File → Open Recent** → choose the entry ending in **[WSL: Ubuntu-24.04]**. No PowerShell or Ubuntu window is needed.

### Working on a branch

```bash
git checkout -b <your-name>/<feature-name>
git push -u origin <your-name>/<feature-name>
```

After the first push, `git push` and `git pull` work normally on that branch. Branches can also be managed from the VS Code Source Control panel and the branch name in the status bar.

Stay in sync with `main` periodically:

```bash
git fetch origin
git merge origin/main
```

---

## Troubleshooting

| Problem | Fix |
|---|---|
| `verilator` / `python3.11` "not recognized" | You are in PowerShell. Enter WSL (`wsl -d Ubuntu-24.04`) or use the VS Code terminal while connected to WSL. |
| `The token '&&' is not a valid statement separator` | Same cause: a Linux command was run in PowerShell. |
| `ERROR_ALREADY_EXISTS` on `wsl --install` | Ubuntu 24.04 is already installed; skip to step 2. |
| Error `0x80370102` / virtualization not enabled | Enable Intel VT-x or AMD-V/SVM in BIOS (usually F2, F10, or Del at boot). |
| `wsl --install` only prints help text | Run `wsl --update`, then retry. |
| Forgot Ubuntu password | See "Forgot your Ubuntu password?" in step 2. |
| `Permission denied (publickey)` | The key on GitHub doesn't match `~/.ssh/id_ed25519.pub`. Re-copy the full line and re-add it. |
| `git push` permission denied | Ask the repo owner to add you as a collaborator. |
| `code .` not recognized | Install the WSL extension in VS Code, then close and reopen the Ubuntu terminal. |
| Bottom-left of VS Code doesn't say WSL | Reopen the folder through **Open Recent → [WSL: Ubuntu-24.04]**. |