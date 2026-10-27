# quartus/

Quartus projects, constraints, fitter reports, and bitstream releases for the DE10-Lite
(MAX 10 `10M50DAF484C7G`).

## Toolchain

| | |
|---|---|
| Synthesis / fit | Quartus Prime Lite **25.1std.0 Build 1129**, Linux, in WSL (Ubuntu 24.04) |
| Device support | MAX 10 only (`max10-25.1std.0.1129.qdz`) |
| Board programming | Windows Quartus Programmer, opening the `.sof` from `\\wsl.localhost\...` |

Everyone builds with this exact version. Synthesis results, including multiplier packing, can
change between Quartus releases.

### Linux Quartus setup (WSL)

1. From the Quartus Prime Lite **25.1std.0** download page (Linux, *Individual Files*), download
   `QuartusLiteSetup-25.1std.0.1129-linux.run` and `max10-25.1std.0.1129.qdz`.
2. Put both in one folder **outside the repo**. The installer picks up the `.qdz` automatically:

   ```bash
   mkdir -p ~/installers/quartus-25.1std.0
   cp /mnt/c/Users/<you>/Downloads/QuartusLiteSetup-25.1std.0.1129-linux.run ~/installers/quartus-25.1std.0/
   cp /mnt/c/Users/<you>/Downloads/max10-25.1std.0.1129.qdz ~/installers/quartus-25.1std.0/
   cd ~/installers/quartus-25.1std.0
   chmod +x QuartusLiteSetup-25.1std.0.1129-linux.run
   ./QuartusLiteSetup-25.1std.0.1129-linux.run --mode unattended --accept_eula 1 \
       --installdir ~/intelFPGA_lite/25.1std
   ```

3. Add to `~/.bashrc`. This form is safe to re-source without duplicating PATH entries:

   ```bash
   case ":$PATH:" in
       *":$HOME/intelFPGA_lite/25.1std/quartus/bin:"*) ;;
       *) export PATH="$HOME/intelFPGA_lite/25.1std/quartus/bin:$PATH" ;;
   esac
   ```

4. Fix the `setlocale` warning (one time):

   ```bash
   sudo apt install -y locales && sudo locale-gen en_US.UTF-8 && sudo update-locale LANG=en_US.UTF-8
   ```

5. Open a new terminal and verify:

   ```bash
   type -a quartus_sh      # only ~/intelFPGA_lite/25.1std/... ; no aliases
   quartus_sh --version    # 25.1std.0 Build 1129
   ```

**Pitfalls**

- **A `TBBmalloc ... ucrtbase.dll` line means you're running the Windows build.** Check
  `type -a quartus_sh` for an alias pointing at `/mnt/c/...` and remove it. `which` does not
  show aliases.
- **Windows Quartus on a `\\wsl.localhost` path is unreliable.** Run builds with Linux Quartus.
- **A script that "succeeds" but writes nothing is probably empty.** Check `ls -la` for
  0-byte files; unsaved VS Code tabs are the usual cause.

## dsp_pack_test/: 9×9 multiplier packing check

**Why it exists.** Each MAX 10 embedded multiplier block is either one 18×18 or two 9×9
multipliers. The 64-lane array fits (72/144 blocks) only if two 9×9 **signed** multiplies pack into
one block (`docs/plans and specs/01-contract/hardware-verification-errata.md` §2). This experiment confirms that in week 1,
before any array RTL depends on it.

**What it builds.** Two registered multiplies shaped like `mac_lane`: per-lane `w_n` (9-bit
signed) × broadcast `act` (8-bit unsigned, zero-extended to 9-bit signed-positive). Widths come
from `rtl/fecim_pkg.sv`. Data ports are virtual pins. `clk` is on `PIN_P11` with a 50 MHz
constraint.

| File | Purpose |
|---|---|
| `dsp_pack_test.sv` | Test module (registered inputs and outputs, `multstyle = "dsp"`) |
| `dsp_pack_test.sdc` | 50 MHz clock constraint |
| `create_project.tcl` | Generates `.qpf`/`.qsf`; errors if FAMILY is not MAX 10 |
| `check_families.tcl` | Lists installed device families |

`.qpf`/`.qsf` are generated and gitignored, so `create_project.tcl` is the source of truth.

**Run**

```bash
cd quartus/dsp_pack_test
quartus_sh -t check_families.tcl        # expect MAX 10 in the list
quartus_sh -t create_project.tcl        # expect FAMILY = MAX 10
quartus_sh --flow compile dsp_pack_test
grep -i -A12 "Fitter DSP Block Usage Summary" output_files/dsp_pack_test.fit.rpt
```

**Result**, from Quartus 25.1std.0, with reports in `reports/dsp_pack_test.fit.*`:

| Statistic | Used |
|---|---|
| Simple Multipliers (9-bit) | 2 |
| Simple Multipliers (18-bit) | 0 |
| **Embedded Multiplier Blocks** | **1** |
| Signed Embedded Multipliers | 2 |
| Mixed Sign Embedded Multipliers | 0 |

**Pass: pairing confirmed.** 64 MAC lanes cost 32 blocks, not 64.

**Reading a failure**

| Blocks | 9-bit elements | Cause |
|---|---|---|
| 2 | 2 | Not paired: check signedness (Mixed Sign > 0) or mismatched clock/reset |
| 2 | 4 | An operand wider than 9 bits was promoted to 18×18 |
| 0 | 0 | Logic optimized away or built in LEs: check virtual pins / `multstyle` |

Keep every multiply signed × signed. Making any operand unsigned looks free, but it prevents
pairing.

**Limitation.** This shows packing for two isolated multiplies. Re-check the same table when the
32-lane array synthesizes (week 4): expect 32 nine-bit multipliers in 16 blocks for the MACs alone.

## reports/

Fitter reports copied from `output_files/`, which is gitignored. Copy, don't link: the
committed report is the evidence.