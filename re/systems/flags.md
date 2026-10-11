# Flag atlases (`flagfiles*.tga`)

Sources: this repo's sessions (2026-10-11); the RAM saving of DDS flag caches was found by the user
earlier by hand (convert the caches to DDS, keep the `.tga` names, set them read-only, restart until the
game stops trying to rewrite them).

## Summary

- The game draws flags from one atlas per government type: `gfx/flags/flagfiles.tga`,
  `flagfiles_<government>.tga` (+ `.txt` index), built from the individual `gfx/flags/TAG[_gov].tga` and
  cached in the user folder (`Documents/Paradox Interactive/Victoria II/<mod>/gfx/flags/`).
- Vanilla: 5 atlases of 2048×1024. TGC: **11 atlases of 4096×2048**, A8R8G8B8, 32 MB each. **[C]** (file
  headers)
- `FlagAtlas_BuildOrLoad` (`0x007E3900`): **[C]**

| site | call | arguments |
|---|---|---|
| `0x007E55CC` | `D3DXCreateTextureFromFileInMemoryEx` | individual flags: size `-2` (non-pow2), mips from file, Format UNKNOWN, Pool MANAGED |
| `0x007E5ADA` | `D3DXCreateTexture` | the atlas being built: A8R8G8B8 (`0x15`), 1 mip, MANAGED |
| `0x007E629C` | `D3DXCreateTexture` | a copy for saving: A8R8G8B8, MANAGED |
| `0x007E62CC` | `D3DXLoadSurfaceFromSurface` | atlas -> copy |
| `0x007E6367` | `D3DXSaveTextureToFileInMemory` | `D3DXIFF_TGA` (2); the map cache (`FUN_00803210`, `0x00803A52`) saves `D3DXIFF_DDS` (4) |
| `0x007E6492` | `D3DXCreateTextureFromFileInMemoryEx` | the cache file read through PhysFS (after a rebuild and on every later start): Format UNKNOWN, Pool MANAGED |

- **Why it costs RAM:** `D3DPOOL_MANAGED` keeps a system-memory copy of every texture inside the
  process, so each TGC atlas pins 32 MB of the ~3.5 GB address space for the whole session (~350 MB for
  11). **[I]** (D3D9 behaviour; the user measured the saving)
- **Why the manual trick worked:** the load passes Format UNKNOWN, so D3DX takes the format from the file
  *content*, not the extension: a DDS/DXT file named `.tga` loads as a compressed texture.

## Patch: `compress_flag_cache`

`patch/src/features/flag_cache.cpp` wraps three of the D3DX calls (`CALL [import]` -> `CALL wrapper; NOP`):

1. the copy (`0x007E629C`) is created as DXT1 instead of A8R8G8B8, so `D3DXLoadSurfaceFromSurface`
   compresses the atlas into it;
2. the save (`0x007E6367`) writes DDS instead of TGA (same `.tga` name);
3. the cache load (`0x007E6492`) asks for DXT1 unless the file already is DXT1, so an old cache is converted on load and a
   DXT1 cache loads as it is.

The individual flags (`0x007E55CC`) are left alone: they are small and not multiples of 4.
DXT1 is half a byte per pixel: 4 MB instead of 32 MB per TGC atlas, ~300 MB less for 11 atlases (sysmem
copies; VRAM too).

Open: confirm the RAM saving and that flags look right (DXT1 is lossy in colour, like DXT5); load time with old caches (one
compression per atlas per start) against rewritten caches (delete `flagfiles*` once).

## DXT1 rather than DXT5 (2026-10-11)

The first build used DXT5 and rebuilt TGC's 11 atlases at 8 MB each (`DDS`, FOURCC `DXT5`, 4096×2048).
Every one of the 524 288 4×4 blocks of those files had alpha 255/255 (fully opaque), and the user
confirmed flags carry no alpha. DXT1 encodes colour exactly like DXT5 (the DXT5 colour block *is* a DXT1
block) with 1-bit alpha, so it loses nothing here and halves the size again: 4 MB per atlas. (DXT3 would
not help: same 1 byte per pixel as DXT5, only a different alpha encoding.)
