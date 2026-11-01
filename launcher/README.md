# The Grand Loader (`dinput8.dll`)

A plugin loader for Victoria II 3.04 (`v2game.exe`). On its own it changes nothing in the game: it
loads the plugins installed next to it.

## Install

Copy `dinput8.dll` into the game folder, next to `v2game.exe`. To uninstall, delete it.

The game imports DirectInput 8 statically, so Windows loads this `dinput8.dll` first. It forwards every
DirectInput call to the system library, so input works as before.

## What it loads

Plugins are declared by the mods ticked in the launcher. Each `.mod` file the game receives with `-mod=`
may contain any number of `load = "<path>"` lines, with the path relative to `mod/`:

```
name = "Performance Patch"
load = "performance_patch/performance_patch.dll"
```

The loader loads each DLL and calls its `TGL_PluginInit` export, in the order of the `-mod=` arguments
and of the lines in each file. A DLL named by several mods (a plugin's own `.mod` and a content mod
that ships it) is loaded once, at its first mention. The plugin's settings are `<name>.ini` next to its
DLL. The game ignores the `load` lines, as it ignores any key it does not know in a `.mod`.

Every line the loader and the plugins log goes to `the_grand_loader.log` in the game folder. A second instance started from the same folder while the first runs (a local multiplayer test) writes to `the_grand_loader.<pid>.log` instead, so neither overwrites the other.

## Writing a plugin

See `PLUGINS.md` in the SDK package (`launcher/PLUGINS.md` in the source repository).
