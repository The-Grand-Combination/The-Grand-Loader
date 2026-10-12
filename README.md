# The Grand Loader & Victoria II Performance Patch

Runtime patches for Victoria II 3.04. The game files are not modified.

- **The Grand Loader**: a plugin loader (`dinput8.dll`).
- **Victoria II Performance Patch**: a plugin that makes the game faster without changing its results.

## Install

1. Download the release zip.
2. Extract them into the game folder, next to `v2game.exe`. The zip already have the right layout
   (`dinput8.dll` and `mod/`).
3. Play as usual, with any mods.

To uninstall, delete `dinput8.dll` and `mod/performance_patch*`.

Settings are in `mod/performance_patch.ini`, with one line explaining each option. Each launch writes
`the_grand_loader.log` next to `v2game.exe`.

## Development

```
build.cmd     build into build/ (the game layout)
deploy.cmd    build and copy into the game folder (set V2_GAME_DIR in deploy.local.cmd)
package.cmd   release zips in out/release/
```

Requires Visual Studio Build Tools (x86 C++), CMake ≥ 3.25 and Ninja.

- Writing another plugin: [launcher/PLUGINS.md](launcher/PLUGINS.md)
- Feature notes and timings: [docs/](docs/)
- Reverse engineering of `v2game.exe`: [docs/re/README.md](docs/re/README.md)
