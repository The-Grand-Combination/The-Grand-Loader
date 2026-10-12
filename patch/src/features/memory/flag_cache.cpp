#include "features/memory/flag_cache.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04, FUN_007E3900 (flag atlas builder/loader, "/flags/flagfiles.tga"; re/systems/flags.md):
//
//   0x007E55CC  CALL [D3DXCreateTextureFromFileInMemoryEx]  individual flags (TAG_gov.tga) read to build the
//                                                           atlas: left alone (small, not multiples of 4)
//   0x007E629C  CALL [D3DXCreateTexture]                    copy of the built atlas, A8R8G8B8, MANAGED
//   0x007E6367  CALL [D3DXSaveTextureToFileInMemory]        cache written as D3DXIFF_TGA (2)
//   0x007E6492  CALL [D3DXCreateTextureFromFileInMemoryEx]  the cache file read from disk (PhysFS), after a
//                                                           rebuild and on every later start; Format UNKNOWN
//                                                           (taken from the file content), Pool MANAGED
//
// Each `CALL dword ptr [import]` (6 bytes) becomes `CALL wrapper; NOP`. The D3DX functions are stdcall
// exports, so the wrappers are ordinary stdcall functions with the same signatures; they change one
// argument and forward to the real import.

namespace cp::features {

namespace {

constexpr uint32_t kIatSaveTextureToFileInMemory = 0x88A418;
constexpr uint32_t kIatCreateTexture = 0x88A41C;
constexpr uint32_t kIatCreateTextureFromFileInMemoryEx = 0x88A424;

constexpr uint32_t kCreateCopySite = 0x3E629C;
constexpr uint32_t kSaveCacheSite = 0x3E6367;
constexpr uint32_t kReloadCacheSite = 0x3E6492;

constexpr DWORD kFormatUnknown = 0;
constexpr DWORD kFormatA8R8G8B8 = 21;
// DXT1: 4 bits per pixel, the same colour encoding as DXT5 but 1-bit alpha. Flag atlases are fully opaque
// (every 4x4 block of TGC's DXT5 atlases had alpha 255/255), so nothing is lost against DXT5 at half the size.
constexpr DWORD kFormatDxt1 = 0x31545844;  // D3DFMT_DXT1, FOURCC 'DXT1'
constexpr DWORD kFileFormatTga = 2;  // D3DXIFF_TGA
constexpr DWORD kFileFormatDds = 4;  // D3DXIFF_DDS

using CreateTextureFn = HRESULT(WINAPI*)(void* device, UINT width, UINT height, UINT mips, DWORD usage, DWORD format,
                                         DWORD pool, void** texture);
using SaveTextureFn = HRESULT(WINAPI*)(void** buffer, DWORD file_format, void* texture, const void* palette);
using LoadTextureFn = HRESULT(WINAPI*)(void* device, const void* data, UINT size, UINT width, UINT height, UINT mips,
                                       DWORD usage, DWORD format, DWORD pool, DWORD filter, DWORD mip_filter,
                                       DWORD color_key, void* src_info, void* palette, void** texture);

CreateTextureFn g_create_texture = nullptr;
SaveTextureFn g_save_texture = nullptr;
LoadTextureFn g_load_texture = nullptr;
int g_loads = 0;
int g_converted = 0;
int g_saves = 0;

// True when `data` is a DDS file whose pixel format is already DXT1 (FOURCC at offset 84).
bool is_dxt1_dds(const void* data, UINT size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    if (!p || size < 128 || memcmp(p, "DDS ", 4) != 0) {
        return false;
    }
    uint32_t fourcc;
    memcpy(&fourcc, p + 84, 4);
    return fourcc == kFormatDxt1;
}

HRESULT WINAPI create_copy(void* device, UINT width, UINT height, UINT mips, DWORD usage, DWORD format, DWORD pool,
                           void** texture) {
    // D3DXLoadSurfaceFromSurface then compresses the built atlas into this texture.
    if (format == kFormatA8R8G8B8 && width % 4 == 0 && height % 4 == 0) {
        format = kFormatDxt1;
    }
    return g_create_texture(device, width, height, mips, usage, format, pool, texture);
}

HRESULT WINAPI save_cache(void** buffer, DWORD file_format, void* texture, const void* palette) {
    if (file_format == kFileFormatTga) {
        file_format = kFileFormatDds;  // a TGA cannot hold DXT data; the .tga name is kept
        g_saves++;
        logf("compress_flag_cache: flag atlas %d cached as DDS (DXT1)", g_saves);
    }
    return g_save_texture(buffer, file_format, texture, palette);
}

HRESULT WINAPI load_cache(void* device, const void* data, UINT size, UINT width, UINT height, UINT mips, DWORD usage,
                          DWORD format, DWORD pool, DWORD filter, DWORD mip_filter, DWORD color_key, void* src_info,
                          void* palette, void** texture) {
    if (format == kFormatUnknown && !is_dxt1_dds(data, size)) {
        // An uncompressed (or earlier DXT5) cache is compressed here on every start until it is rebuilt;
        // a DXT1 cache loads as it is.
        HRESULT hr = g_load_texture(device, data, size, width, height, mips, usage, kFormatDxt1, pool, filter,
                                    mip_filter, color_key, src_info, palette, texture);
        if (SUCCEEDED(hr)) {
            g_loads++;
            g_converted++;
            if (g_converted == 1) {
                logf("compress_flag_cache: an uncompressed flag cache was converted while loading; delete the "
                     "flagfiles* caches once to have them rewritten as DXT1 and skip this on every start");
            }
            return hr;
        }
        logf("compress_flag_cache: DXT1 load failed (%08lX), loading the atlas unchanged", static_cast<unsigned long>(hr));
    } else if (format == kFormatUnknown) {
        g_loads++;
    }
    if (g_loads == 1 || g_loads % 10 == 0) {
        logf("compress_flag_cache: %d flag atlas(es) loaded as DXT1 (%d converted on load)", g_loads, g_converted);
    }
    return g_load_texture(device, data, size, width, height, mips, usage, format, pool, filter, mip_filter, color_key,
                          src_info, palette, texture);
}

// Replaces `CALL dword ptr [exe+iat_rva]` at `site_rva` with `CALL wrapper; NOP`.
bool hook_import_call(uint32_t site_rva, uint32_t iat_rva, void* wrapper) {
    uintptr_t site = hooks::exe_addr(site_rva);
    uint8_t expected[6] = {0xFF, 0x15};
    uint32_t iat = static_cast<uint32_t>(hooks::exe_addr(iat_rva));
    memcpy(expected + 2, &iat, 4);
    if (!hooks::expect_bytes(site, expected, sizeof(expected))) {
        return false;
    }
    uint8_t patch[6] = {0xE8, 0, 0, 0, 0, 0x90};
    int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(wrapper) - (site + 5));
    memcpy(patch + 1, &rel, 4);
    return hooks::write_bytes(site, patch, sizeof(patch));
}

}  // namespace

void install_flag_cache(const Config& config) {
    if (!config.compress_flag_cache) {
        logf("compress_flag_cache: disabled");
        return;
    }
    g_create_texture = *reinterpret_cast<CreateTextureFn*>(hooks::exe_addr(kIatCreateTexture));
    g_save_texture = *reinterpret_cast<SaveTextureFn*>(hooks::exe_addr(kIatSaveTextureToFileInMemory));
    g_load_texture = *reinterpret_cast<LoadTextureFn*>(hooks::exe_addr(kIatCreateTextureFromFileInMemoryEx));
    if (!g_create_texture || !g_save_texture || !g_load_texture) {
        logf("compress_flag_cache: D3DX imports not resolved, feature off");
        return;
    }
    struct Site {
        uint32_t site;
        uint32_t iat;
        void* wrapper;
    } sites[] = {
        {kCreateCopySite, kIatCreateTexture, reinterpret_cast<void*>(&create_copy)},
        {kSaveCacheSite, kIatSaveTextureToFileInMemory, reinterpret_cast<void*>(&save_cache)},
        {kReloadCacheSite, kIatCreateTextureFromFileInMemoryEx, reinterpret_cast<void*>(&load_cache)},
    };
    // Check every site before patching any: a partial set (DXT copy saved as TGA) would write a broken cache.
    for (const Site& s : sites) {
        uint8_t expected[6] = {0xFF, 0x15};
        uint32_t iat = static_cast<uint32_t>(hooks::exe_addr(s.iat));
        memcpy(expected + 2, &iat, 4);
        if (!hooks::expect_bytes(hooks::exe_addr(s.site), expected, sizeof(expected))) {
            logf("compress_flag_cache: call site %08X not recognised, feature off", s.site + 0x400000);
            return;
        }
    }
    for (const Site& s : sites) {
        hook_import_call(s.site, s.iat, s.wrapper);
    }
    logf("compress_flag_cache: active (flag atlases created, cached and loaded as DXT1)");
}

}  // namespace cp::features
