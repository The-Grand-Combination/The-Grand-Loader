#pragma once

#include "config.h"

namespace cp::features {

// `compress_flag_cache`: the game builds one flag atlas per government type (4096x2048 in TGC) and keeps
// each as an uncompressed A8R8G8B8 texture in D3DPOOL_MANAGED, so ~32 MB of process memory per atlas
// stays resident. With the option on, the atlases are created, cached and loaded as DXT1 (4 MB each; they
// are fully opaque, so DXT1 loses nothing against DXT5):
// the cache is written as DDS data (under the same .tga name; D3DX reads the format from the content) and
// existing uncompressed or DXT5 caches are converted when loaded. See docs/re/systems/flags.md.
void install_flag_cache(const Config& config);

}  // namespace cp::features
