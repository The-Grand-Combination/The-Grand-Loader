# Ghidra workflow (headless)

How the analysis environment behind the documents in this repository is set up and queried. Reproducible from scratch in about 30 minutes.

## Requirements

| Component | Version | Location used here |
|---|---|---|
| Ghidra | 12.1.4 | `C:\Users\joel\Desktop\ghidra_12.1.4_PUBLIC` |
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

## 2. Query it with `tools/ghidra/V2Dump.java`

```bat
"<ghidra>\support\analyzeHeadless.bat" %USERPROFILE%\ghidra_projects v2game ^
    -process v2game.exe -noanalysis -readOnly ^
    -scriptPath "<repo>\tools\ghidra" -postScript V2Dump.java "<outDir>" <cmd> <args...>
```

Each invocation takes ~20 s (project open + script). Output: `<outDir>\V2Dump_<cmd>.txt`, plus one `<func>_<addr>.c` per function for `decomp`. Commands are listed in [tools/ghidra/README.md](../tools/ghidra/README.md).

Typical sequence for a new subsystem:

1. `grep -boa` the keyword strings in the exe (`.rdata` raw `0x888600` ↔ VA `0x00C8A000`) and `xref` them → parser / token-registration sites in `FUN_00c381c0`. The pattern `MOV EDX,<string> … MOV ECX,<id>` maps each keyword to its token id.
2. `syms` for the class name → vtable → read the slots from the raw file (`.rdata` offset formula above) → `decomp` the slot functions.
3. `up <addr> 8` from the function of interest → see whether it reaches the daily tick (`0x006859C0` / `0x005091A0`) or `Update_World_and_Advance_Time` (`0x00682EC0`), and through which gates.
4. `before <callsite> n` to read the arguments at a specific call (the decompiler often drops register-passed arguments).

## Pitfalls

- `analyzeHeadless.bat` passes script arguments through `cmd.exe`; a `|` in a regex breaks the call. Use `scan @file.re`.
- `disasm` needs an instruction start; use `before` when you only have a reference address.
- Addresses inside `.text` from a raw byte scan point at the immediate, not at the instruction (a `PUSH imm32` reference is at instruction+1).
- Running `python` on this machine opens the Microsoft Store alias and blocks the shell. Byte work is done with `od`, `dd` and PowerShell.
