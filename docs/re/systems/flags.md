# Flags: atlases, governments and drawing

Sources: this repo's sessions (2026-10-11, 2026-11-07); the RAM saving of DDS flag caches was found by the
user earlier by hand (convert the caches to DDS, keep the `.tga` names, set them read-only, restart until
the game stops trying to rewrite them).

## Summary

- The game draws flags from **one atlas per government** of `governments.txt`. A government's atlas is the
  file of its `flagType`: `gfx/flags/flagfiles.tga` without one, `flagfiles_<flagType>.tga` (+ `.txt` index)
  otherwise, built from the individual `gfx/flags/TAG[_<flagType>].tga` and cached in the user folder
  (`Documents/Paradox Interactive/Victoria II/<mod>/gfx/flags/`). **[C]**
- Governments that share a `flagType` each get their own, identical atlas: the cache file is written once
  per government under the same name, and every copy stays loaded. A mod with 18 governments and 11 flag
  types holds 18 atlases. **[C]** (`FlagAtlas_CreateAll`; `share_flag_atlases` below)
- A country can show another government's flag under its government: `govt_flag` (history and saves). **[C]**
- Every flag drawn goes through `Country_GetFlagTexture` (`0x0052FC10`), which returns an atlas index; the
  caller cuts the country's cell from that atlas. **[C]**
- Cache files: vanilla 5 of 2048×1024; TGC **11 of 4096×2048** (one per flag type), A8R8G8B8, 32 MB each **[C]** (file
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

## Governments and their atlases

- `g_governments` (`0x0131BD5C`): the `CGovernment*` of `governments.txt` in file order, vector at `+0x0C`.
  `CGovernment` (`CGovernment_ctor` `0x009705A0`): `+0x24` name, `+0x40` index, `+0x60` `flagType`
  (empty by default). `CNullGovernment` answers false to vt slot `0x2C`. **[C]**
- `FlagAtlas_CreateAll` (`0x007E0C00`), once at start, on `g_flagOwner` (`0x012589B8`), for each
  government: **[C]**
  1. a new 0x44-byte `FlagAtlas`;
  2. `FlagAtlas_BuildOrLoad` (call at `0x007E0DE1`): stdcall `(atlas, &"gfx/flags", g_countryManager,
     owner, &flagType)`, RET 0x14; it writes nothing but the atlas;
  3. the atlas is appended to `owner+0x6BD6C` (`vector<FlagAtlas*>`);
  4. the government's **name** (`+0x24`) maps to that index in `owner+0x6BDAC` (`CTernary<int>`: vt 4 find,
     vt 0x18 insert);
  5. its texture is registered in the texture manager (`owner+0x158`) under the atlas index.
- So the key is the government, not the flag type: two governments with `flagType = monarchy` build
  `flagfiles_monarchy` twice, and both textures stay loaded.
- The atlases are never freed one by one: the teardown (`0x007E0800`) frees only the vector's array. **[I]**

### `FlagAtlas` (0x44 bytes)

| offset | field | conf |
|---|---|---|
| +0x00 | `IDirect3DTexture9*` | C |
| +0x04..+0x10 | cell list used while building | I |
| +0x14 | cell table: rows of 16 bytes `{begin, end, ...}` of `{u, v}` float pairs | C |
| +0x24 | `vector<int>` by country index: 1 = the atlas has that country's cell | C |
| +0x34 / +0x38 | rows / columns: a cell is `1/columns` wide and `1/rows` high in UV | C |
| +0x3C / +0x40 | cell size in pixels, 93 × 64 | C |

The cell of country index `i` (`CCountry+0x20`, 0 read as 1) is entry `(i-1) % w` of row `(i-1) / w`,
with `w` = the number of entries of row 0 (`FlagAtlas_CellUV` `0x007E1D60`, `FlagQuad_Draw` `0x007DFB90`).
**[C]**

## Drawing a flag

`Country_GetFlagTexture` (`0x0052FC10`): EAX = `CCountry*`, result in EAX, plain RET. **[C]**

1. The government: `[country+0xBD0]`. When `country+0x154C[government+0x40]` is a real government (slot
   `0x2C` true), that one is taken instead (`govt_flag`).
2. Its name is looked up in `owner+0x6BDAC` → atlas index.
3. When the atlas has no cell for the country (`atlas+0x24[country+0x20] != 1`), it returns 0, the
   atlas of the governments without a `flagType`.

Every caller then reads `owner+0x6BD6C[index]` itself: the shields (`CShieldObject_SetAtlas`
`0x00817B40`, sprite vt `0x150`; vt `0x14C` takes the country), the map, the ledger, events, the chat.
83 calls, and the 29 files that read the atlas vector all take the index from this function. **[C]**

Textures by path: `TextureManager_GetOrLoad` (`0x009BCBF0`): EAX = `owner+0x158`, `(const std::string*
path, bool)`, RET 8, returns the entry index; entries of 0x2C bytes, texture at `+4`, width `+0x24`, height
`+0x28`, references `+0x2C`. The path is a PhysFS path: a mod's file is `mod/<name>/gfx/...`. **[C]**

## `govt_flag`: another government's flag

- `CCountry+0x154C`: `vector<CGovernment*>`, one entry per government, `CNullGovernment` by default (the
  country constructor `0x00503020`). Entry `g` is the government whose flag the country shows while it
  has government `g`. **[C]**
- It is set by:
  - `govt_flag = { government = X flag = Y }` in country history (`CGovernmentFlagChange_Apply`
    `0x005EBE20`, which can undo itself);
  - the same block in saves: `CCountry_OnKeyword` case `0x25F`, written back by `CCountry_WriteFields`
    for every real entry.
- The getter and setter: `Country_GetGovernmentFlag` `0x0053D810`, `Country_SetGovernmentFlag`
  `0x0053D7F0` (EAX = government, EDX = country). **[C]**
- There is no script effect for it. The country's name does not use it: nothing else reads `+0x154C`. **[C]**

## Extra flags (`change_flag`, Script Extender)

`change_flag = "USA/USA_51"` makes the country draw `gfx/flags/extra/USA/USA_51.tga` (or `.dds`) whatever its
government. `script_extender/src/features/change_flag.cpp`:

- **The effect** is a `set_country_flag` effect (token `0x40B`, `CSetCountryFlagEffect`, 0x3C bytes; its
  parse, slot 7 `0x008829B0`, copies the value into the name at `+0x20`) with the plugin's vtable
  (`se_effect_factory`).
- **Its Execute** keeps the choice as the country flag `se_flag_<path with / as __>`. It sets and clears
  the flag through `set_country_flag`'s and `clr_country_flag`'s own Execute, on an object holding only
  the name. They read nothing else but the scope's country. So the flag is saved with the country, and
  `has_country_flag = se_flag_USA__USA_51` tests it.
- **The hook** on `Country_GetFlagTexture` (`se_change_flag_texture`):
  - The thunk reads the country's set `se_flag_` flags, walking the `CFlags` tree as `CFlags_WriteSet`
    does: nodes `{entry, ?, left +0x08, next +0x0C, right +0x10}`.
  - The result is cached per country index. It is read again when the tree root changes, after the
    effect, and after a load (`se_change_flag_finalize`).
  - For a country with an extra flag, it returns the index of a **one-cell atlas**: the file's texture
    (`TextureManager_GetOrLoad`), 1 row, 1 column, and one row of 8192 cells all at `(0, 0)`. So any
    country index's cell is the whole texture.
  - That atlas is appended to `owner+0x6BD6C` on the game heap the first time the file is drawn, one per
    file.
  - Every other country goes to the original code.
- The texture is stretched over the cell like any flag, so any size works.
- The effect runs on the main thread, like drawing. It changes only a country flag, so it is the same on
  every machine.

## Patch: `compress_flag_cache`

`patch/src/features/memory/flag_cache.cpp` wraps three of the D3DX calls (`CALL [import]` -> `CALL wrapper; NOP`):

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

## Patch: `share_flag_atlases`

`patch/src/features/memory/flag_atlas_share.cpp` redirects the `CALL FlagAtlas_BuildOrLoad` at `0x007E0DE1`
(`cp_flag_atlas_share`):

- The first government of each `flagType` builds its atlas as before.
- A later one with the same `flagType` gets a copy of that atlas's 0x44 bytes: the same texture, cell
  table and per-country table. The texture gets an `AddRef`, because the loop registers it with the
  texture manager once more.
- Nothing frees an atlas alone, so the shared tables are never freed twice.

The pixels are the ones the government would have built, so this is display only. 18 governments with 11
flag types keep 11 textures instead of 18:
- with `compress_flag_cache` (DXT1, 4 MB each at 4096×2048), 28 MB less;
- without it (32 MB each), 224 MB less.

The duplicates' cache reads are skipped too.

## Open questions

1. Whether `g_flagOwner` (`0x012589B8`) and `g_renderDevice` (`0x01258A74`) are the same object. They
   have the same layout at `+0x158`. **[I]**
2. The `+0x04..+0x10` cell list of `FlagAtlas`. **[I]**
3. Whether a shield already on screen keeps its old atlas pointer (`CShieldObject+0x1A0`) until the
   window sets the country again, once `change_flag` runs.
