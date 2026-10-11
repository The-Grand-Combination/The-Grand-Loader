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

## V2Symbols.java

Syncs the project with `re/symbols/functions.csv` and `globals.csv`:

```
analyzeHeadless <projectDir> v2game -process v2game.exe -noanalysis [-readOnly] ^
    -scriptPath <this folder> -postScript V2Symbols.java <repo>\re\symbols <outDir> check|import|export
```

`check` and `export` work read-only; `import` needs the project writable. Details: [re/ghidra.md](../../re/ghidra.md).

## V2DecompAll.java

Decompiles every function into `<outDir>` (one `.c` per 64 KB block + `index.tsv` with size, callers,
callees and call depth from the daily tick and from the frame). ~64 k functions; run `-readOnly`.
Details: [re/ghidra.md](../../re/ghidra.md).

## V2WriteScan.java

What a set of functions can write outside their stack: from each root (a file of hex addresses, one
per line, optional label), follows direct calls up to a depth and lists writes to absolute addresses
(globals, statics, lazy-init guards) and calls to the RNGs. Virtual calls are not followed.

```
analyzeHeadless <projectDir> v2game -process v2game.exe -noanalysis -readOnly ^
    -scriptPath <this folder> -postScript V2WriteScan.java <outDir> <roots.txt> <depth>
```

Used for [re/systems/parallel-triggers.md](../../re/systems/parallel-triggers.md).
