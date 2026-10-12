# The Grand Loader (`dinput8.dll`)

A plugin loader for Victoria II 3.04 (`v2game.exe`). On its own it changes nothing in the game: it
loads the plugins installed next to it.

## Install

Copy `dinput8.dll` into the game folder, next to `v2game.exe`. To uninstall, delete it.

The game imports DirectInput 8 statically, so Windows loads this `dinput8.dll` first. It forwards every
DirectInput call to the system library, so input works as before.

## What it loads

For every `mod/<name>.ini` in the game folder that has a `mod/<name>/<name>.dll` next to it, the loader
loads that DLL and calls its `TGL_PluginInit` export. Plugins are loaded in file-system order (usually
alphabetical). An `.ini` without a matching DLL is ignored, so content mods are not affected; the game
itself only reads `.mod` descriptors, so plugins never appear in its mod list.

Every line the loader and the plugins log goes to `the_grand_loader.log` in the game folder.

## Writing a plugin

See `PLUGINS.md` in the SDK package (`launcher/PLUGINS.md` in the source repository).
