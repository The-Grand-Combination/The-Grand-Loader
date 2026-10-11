# Ghidra headless helper

`V2Dump.java` is a GhidraScript used to pull facts out of an analyzed `v2game.exe` project without opening the GUI.

```
analyzeHeadless <projectDir> <projectName> -process v2game.exe -noanalysis -readOnly ^
    -scriptPath <this folder> -postScript V2Dump.java <outDir> <cmd> <args...>
```

Output goes to `<outDir>/V2Dump_<cmd>.txt` (and `<func>_<addr>.c` for `decomp`).

| cmd | args | result |
|---|---|---|
| `xref` | addr... | references to each address + callers of the containing function |
| `callers` | addr... | call sites of the function containing each address |
| `callees` | addr... | functions called by the containing function |
| `up` | addr depth | BFS over callers up to `depth`, one sample path per function |
| `decomp` | addr... | decompiled C of the containing function |
| `disasm` | addr len ... | instructions from `addr` (must be an instruction start) for `len` bytes |
| `before` | addr n ... | the `n` instructions ending at `addr` |
| `scan` | regex | every instruction whose text matches (`@file` reads the regex from a file; the `.bat` launcher eats `\|`) |
| `syms` | regex | symbols whose name matches (RTTI `vftable` names are useful) |

Requirements: Ghidra 12.x, JDK 21 (`JAVA_HOME` set). Project creation:

```
analyzeHeadless <projectDir> v2game -import "<game dir>\v2game.exe" -loader PeLoader
```
