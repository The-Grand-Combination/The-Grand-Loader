# Writing a plugin for The Grand Loader

The loader (`dinput8.dll`) loads any number of plugins. A plugin is a 32-bit Windows DLL with one
export; it gets a small host API and patches the game from there.

## Layout in the game folder

```
v2game.exe
dinput8.dll                      the loader
mod/<name>.ini                   your plugin's settings (its presence enables the plugin)
mod/<name>/<name>.dll            your plugin
```

`<name>` is the same in both places. Delete the `.ini` (or the DLL) to disable the plugin.

## The contract (`include/tgl/plugin_api.h`)

```c
int TGL_PluginInit(const TGL_HostApi* host);   // exported, C linkage; return 0 on success
```

`TGL_HostApi` gives the plugin:

| field | |
|---|---|
| `api_version`, `struct_size` | check `api_version == TGL_PLUGIN_API_VERSION` (or at least the fields you use) |
| `plugin_name` | `<name>` |
| `game_dir`, `config_file`, `plugin_dir` | absolute paths (wide strings, no trailing separator) |
| `exe_base`, `exe_size` | where `v2game.exe` is loaded and how large it is |
| `log(host, message)` | one line in `the_grand_loader.log`, tagged with your plugin name |

The struct and its strings are owned by the loader and stay valid for the whole process.

## Rules

- **`TGL_PluginInit` runs inside the loader's `DllMain`**, with the loader lock held, before any game
  code. Hooking, patching code and reading files are fine; creating threads that you wait for, or
  waiting on any synchronisation object, is not. Start threads later (e.g. from your first hook).
- **The executable is relocated (ASLR).** `v2game.exe` 3.04 has a relocation table and `DYNAMIC_BASE`;
  never use listing addresses directly. Runtime address = `exe_base + (VA − 0x00400000)`.
- **Check bytes before patching.** Compare the original bytes at every site before writing, and leave
  the site alone (and log it) if they differ: another plugin may have patched it, or the executable is a
  different version.
- **Keep registers when hooking.** The game is built with whole-program optimisation; callers may rely
  on registers beyond the documented calling convention. Naked thunks that save everything are safest.
- **Several plugins can be loaded together.** Patch distinct sites; when you redirect a `CALL`, check
  that it still calls the expected target.

## Building

32-bit (x86) only, any compiler that can produce a Windows DLL. With CMake, consume the header through
the `tgl::plugin_api` interface target of this repository, or add `include/` from the SDK package to
your include path. Link the C runtime statically if you can (`/MT`), so the plugin needs no
redistributable.

The Victoria II Performance Patch in the source repository (`patch/`) is a complete example.
