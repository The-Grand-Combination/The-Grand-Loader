# Ghidra workflow (headless)

How the analysis environment behind the documents in this repository is set up and queried. Reproducible from scratch in about 30 minutes.

## Requirements

| Component | Version | Location used here |
|---|---|---|
| Ghidra | 12.1.4 | `C:\Program Files (x86)\ghidra_12.1.4_PUBLIC` |
| JDK | 21 (Ghidra 12 requires ≥ 21) | portable Temurin zip unpacked to `%USERPROFILE%\.jdks\jdk-21.0.12.1+1` — no installer, no admin |
| Target | `v2game.exe` 3.04, 12,294,656 bytes, SHA-256 `62D48C204364DD706584777C2E2B3C7AB3C5F1DD0170872554943575D53D6648` | Steam install |

Ghidra does not find a portable JDK by itself; set `JAVA_HOME` (and prepend `%JAVA_HOME%\bin` to `PATH`) in the shell that runs `analyzeHeadless.bat`.

## 1. Create and analyze the project (once)

```bat
set JAVA_HOME=%USERPROFILE%\.jdks\jdk-21.0.12.1+1
set PATH=%JAVA_HOME%\bin;%PATH%
"<ghidra>\support\analyzeHeadless.bat" %USERPROFILE%\ghidra_projects v2game ^
    -import "<game dir>\v2game.exe" -loader PeLoader -analysisTimeoutPerFile 7200 -max-cpu 8
```

Full auto-analysis takes ~25 minutes. The result is `%USERPROFILE%\ghidra_projects\v2game.gpr` with the program `/v2game.exe`. MSVC RTTI is recovered automatically, so many vtables carry their real class name (`CCasusBelliType::vftable`, `CAndTrigger::vftable`, `CEventScope::vftable`, ...). These names are the fastest way into a subsystem: list them with the `syms` command below.

## 2. Load the names from `docs/re/symbols/` (and keep them in step)

The Ghidra project is disposable: the names live in `docs/re/symbols/*.csv`. After creating a project, apply
them with `docs/tools/ghidra/V2Symbols.java` (no `-readOnly`, so the renames are saved):

```bat
"<ghidra>\support\analyzeHeadless.bat" %USERPROFILE%\ghidra_projects v2game ^
    -process v2game.exe -noanalysis ^
    -scriptPath "<repo>\docs\tools\ghidra" -postScript V2Symbols.java "<repo>\docs\re\symbols" "<outDir>" import
```

| cmd | needs write | result in `<outDir>\V2Symbols_<cmd>.txt` |
|---|---|---|
| `check` | no (`-readOnly`) | `NOFUNC` rows whose address is not a function start, `NAME`/`LABEL` rows that differ from Ghidra |
| `import` | yes | renames functions, labels globals (RTTI `vftable` names are left alone) |
| `export` | no | user-named functions/labels the CSVs do not have yet, as CSV rows to paste |

Workflow: name things in the Ghidra GUI while exploring, run `export`, move the rows you trust into the
CSVs with a confidence and a note, commit. The CSV diff is the record of what was learned.

## 2b. Ghidra Server (shared program)

The analysed `v2game.exe` is also kept on a Ghidra Server, repository `VictoriaII`, so everyone works on
the same names. The CSVs stay the source of truth; the server is a shared copy synced from them.

- **Repository names must not contain spaces.** `analyzeHeadless` cannot open
  `ghidra://host/Name%20With%20Space`: the `.bat` launchers expand `%2`, and Ghidra rejects the decoded
  space. A first repository called "Victoria II" was replaced by `VictoriaII` for that reason.
- **Sync:** `docs/tools/scripts/ghidra_sync.cmd check|export|import`. It reads the server URL and user
  from `ghidra_sync.local.cmd` next to it (git-ignored) and the password from `GHIDRA_PW`, or asks for it.

| step | command | writes to the server |
|---|---|---|
| before changing the CSVs | `ghidra_sync.cmd export`: names added on the server that the CSVs lack | no |
| before committing CSV changes | `ghidra_sync.cmd check` | no |
| after the CSV commit | `ghidra_sync.cmd import`: applies the CSVs, commits `docs/re/symbols @ <sha>` | yes, a new version |

`import` overwrites a function name that differs from its CSV row and leaves every other name alone, so
run `export` first and move the rows worth keeping into the CSVs. Each call downloads the program (URL
mode); for many quick `V2Dump` queries use the local project of section 1.

The program on the server was created with the import and auto-analysis of section 1 followed by
`V2Symbols import`, in one headless run with `-commit`.

## 3. Full decompilation into `docs/re/decomp/` (local, not committed)

`docs/tools/ghidra/V2DecompAll.java` writes the decompiled C of all ~63 800 functions into `docs/re/decomp/`,
one file per 64 KB block (`0068.c` holds `0x00680000..0x0068FFFF`), plus `index.tsv`. It is git-ignored:
it can be regenerated, and it is derived from the game binary, so it must not be published. Quote short
excerpts in `systems/` pages instead. Regenerate it after an `import` so the code uses the current names.

```bat
"<ghidra>\support\analyzeHeadless.bat" %USERPROFILE%\ghidra_projects v2game ^
    -process v2game.exe -noanalysis -readOnly ^
    -scriptPath "<repo>\docs\tools\ghidra" -postScript V2DecompAll.java "<repo>\docs\re\decomp" 60
```

Each function starts with a header that can be grepped:

```
// ==== 0x006859c0 DayTick  bytes=... callers=1 callees=... day=0 frame=...
// callers: Update_World_and_Advance_Time@00683ab1
```

Functions too large for the 60 s timeout were redone with `V2Dump.java decomp` (120 s) into `docs/re/decomp/large/`; `index.tsv` points at them. `Render_MapFrame` (`0x006592F0`, 31 KB) does not decompile at all: read it with `disasm`.

`day` / `frame` are the call depth from `DayTick` / `CInGameIdler_Update` over direct calls
(-1 = not reached). Virtual calls (triggers, effects, vtables) are not followed, so a function at -1 can
still run every day. Typical queries (Git Bash):

```sh
rg -n '\+ 0xb0c\)' docs/re/decomp                                    # who reads the date
rg -n 'CBManager_IsAnyCBUsable' docs/re/decomp                         # every call site, with context
awk -F'\t' '$6 >= 0' docs/re/decomp/index.tsv | sort -t$'\t' -k3,3nr | head -50   # largest functions reached from the day tick
```

## 4. Query it with `docs/tools/ghidra/V2Dump.java`

```bat
"<ghidra>\support\analyzeHeadless.bat" %USERPROFILE%\ghidra_projects v2game ^
    -process v2game.exe -noanalysis -readOnly ^
    -scriptPath "<repo>\docs\tools\ghidra" -postScript V2Dump.java "<outDir>" <cmd> <args...>
```

Each invocation takes ~20 s (project open + script). Output: `<outDir>\V2Dump_<cmd>.txt`, plus one `<func>_<addr>.c` per function for `decomp`. Commands are listed in [docs/tools/ghidra/README.md](../tools/ghidra/README.md).

Typical sequence for a new subsystem:

1. `grep -boa` the keyword strings in the exe (`.rdata`: VA = file offset + `0x401A00`; see [README.md](README.md) for all sections) and `xref` them → parser / token-registration sites in `FUN_00c381c0`. The pattern `MOV EDX,<string> … MOV ECX,<id>` maps each keyword to its token id.
2. `syms` for the class name → vtable → read the slots from the raw file (`.rdata` offset formula above) → `decomp` the slot functions.
3. `up <addr> 8` from the function of interest → see whether it reaches the daily tick (`0x006859C0` / `0x005091A0`) or `Update_World_and_Advance_Time` (`0x00682EC0`), and through which gates.
4. `before <callsite> n` to read the arguments at a specific call (the decompiler often drops register-passed arguments).

## Pitfalls

- `analyzeHeadless.bat` passes script arguments through `cmd.exe`; a `|` in a regex breaks the call. Use `scan @file.re`.
- `disasm` needs an instruction start; use `before` when you only have a reference address.
- Addresses inside `.text` from a raw byte scan point at the immediate, not at the instruction (a `PUSH imm32` reference is at instruction+1).
- Running `python` on this machine opens the Microsoft Store alias and blocks the shell. Byte work is done with `od`, `dd` and PowerShell.
