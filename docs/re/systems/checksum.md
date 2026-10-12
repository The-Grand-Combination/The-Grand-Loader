# Checksums

Sources: `Engine/checksum.md` in
[maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering).
Patch bytes re-checked on 3.04 here; the file offset in the source was wrong (see below).

## Summary

There are two unrelated checksums.

1. **File checksum** (the one in the main menu, "Checksum is ..."): `HistoryDb_Init` (`0x006377A0`) ->
   `HistoryDb_LoadDirRecursive` (`0x009DA4B0`) -> `File_Checksum` (`0x009DA400`) ->
   `FileStream_ChecksumFletcher` (`0x009939A0`). Fletcher-style over every byte read through PhysFS:
   `s1 = (s1 + b) % 0xFFFF; s2 = (s2 + s1) % 0xFFFF; result = s1 << 16 | s2` (assembled at
   `0x00993A64`, `OR EAX,EBX`). **[C]**
2. **Runtime state checksum**: compared inside `Update_World_and_Advance_Time` ("Checksum: ",
   `0x00E0AA1C`; vector compare `Vector_Equals` `0x0068F340`, mismatch branch at `0x006831A0`).

## Resign -> results -> EXIT changes the checksum

The constructor of `CBackEndIdler` / `CReloadDispatcher` (loads `interface/backend.gui`) increments a
counter in the checksum context on every load:

```asm
005F8262  MOV ECX,[EBP+0x14]
005F8265  MOV EAX,[ECX+0x30]
005F8268  INC EAX              ; 40
005F8269  MOV [ECX+0x30],EAX
```

Every transition that reloads that screen (resign, results, back to the menu) bumps the value. The
minimal fix is `40 -> 90` at `0x005F8268` (patch site `checksum_backend_counter`, original byte
verified). Its **file offset is `0x1F7668`** (`.text` is VA − `0x400C00` in this exe); the source gave
`0x1F8268` (VA − `0x400000`), which would patch the wrong byte.

Forcing the file checksum to 0 (`checksum_file_zero`) and skipping the mismatch branch
(`checksum_mismatch_branch`) did not stop the change: they act on the other system or after the
fact.

## Open questions

1. Whether the checksum is fully stable with the patch, or other fields also change on transitions.
2. The concrete target of the `RESIGN` command (`ResignCommand_Handle` `0x00676500` calls
   `[this+8]->vt[0x114/4]`).
3. Address of the `CBackEndIdler` constructor (contains `0x005F8262`).
