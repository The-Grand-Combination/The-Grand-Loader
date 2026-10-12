# `compress_flag_cache`

## Source

`patch/src/features/memory/flag_cache.cpp`. Research: [re/systems/flags.md](re/systems/flags.md).

## Why

The game draws flags from one atlas per government type (`flagfiles*.tga`), built from the individual
flags and cached in the user folder as uncompressed TGA (A8R8G8B8). TGC has 11 atlases of 4096×2048,
32 MB each, all loaded as `D3DPOOL_MANAGED` textures, which keep a system-memory copy inside the 32-bit
process for the whole session (~350 MB).

## How

Three of the D3DX calls of `FlagAtlas_BuildOrLoad` (`0x007E3900`) are wrapped:

1. the copy made for saving (`0x007E629C`) is created as DXT1, so the atlas is compressed into it;
2. the cache is saved as DDS instead of TGA (`0x007E6367`), keeping the `.tga` name;
3. the cache load (`0x007E6492`) asks for DXT1 unless the file already is DXT1.

The game reads the cache with "format from the file", so a DDS named `.tga` loads fine. Flags have no
alpha, so DXT1 loses nothing over DXT5 (4 MB instead of 32 MB per TGC atlas). Individual flags are left
alone. Delete the old `flagfiles*` caches once so they are rebuilt compressed.
