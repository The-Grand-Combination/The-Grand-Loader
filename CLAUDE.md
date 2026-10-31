# CLAUDE.md

Runtime patches for Victoria II 3.04 (`v2game.exe`, 32-bit MSVC 2010, LTCG, ASLR). Three release units
built from one CMake tree:

| dir | target | what |
|---|---|---|
| `launcher/` | `dinput8.dll` | The Grand Loader: proxy DLL that loads plugins and owns the plugin ABI (`include/tgl/plugin_api.h`) |
| `patch/` | `mod/performance_patch/performance_patch.dll` | Performance Patch: speed, multiplayer sync, crash guards, interface. Namespace `cp` |
| `script_extender/` | `mod/script_extender/script_extender.dll` | Script Extender for TGC: new script keywords, treaties, map modes. Namespace `se` |
| `docs/re/` | | **The reverse-engineering knowledge base. Source of truth for every address, offset and name.** |
| `docs/*.md` | | One page per ini option: Source / Why / How |
| `docs/tools/` | | Ghidra scripts (`V2Dump`, `V2Symbols`, `V2DecompAll`, `V2WriteScan`) and check scripts |

Coding rules with rationale and sources: [docs/CODING.md](docs/CODING.md). Plugin contract:
[launcher/PLUGINS.md](launcher/PLUGINS.md). RE conventions: [docs/re/README.md](docs/re/README.md).

## Build and check

```
build.cmd [x86-release|x86-debug]   configure + build + install into build/ (the drop-in game layout)
deploy.cmd                          build.cmd then xcopy build/ into V2_GAME_DIR (set in deploy.local.cmd, git-ignored)
package.cmd                         release zips into out/release/ (cpack, one zip per component)
docs/tools/scripts/check_patch_sites.sh "<game dir>/v2game.exe"    original bytes of every patch_sites.csv row
docs/tools/scripts/ghidra_sync.cmd check|export|import             CSV names vs the shared Ghidra server
```

- x86 only: `build.cmd` finds VS Build Tools via vswhere and runs `vcvarsall x86`. Needs CMake >= 3.25 and Ninja.
  Compile database: `out/x86-release/compile_commands.json`.
- There is no test suite. Verification is: it builds with `/W4` clean, `check_patch_sites.sh` passes, and the
  user runs the game and shares `the_grand_loader.log`. **Never launch `v2game.exe` yourself.**
- Game folder on this machine: `F:\SteamLibrary\steamapps\common\Victoria 2` (read-only for us except via `deploy.cmd`).
- Formatting: `.clang-format` (LLVM base, 4 spaces, 110 columns, includes regrouped: `<windows.h>`, C headers,
  C++ headers, `"tgl/..."`, project). `.editorconfig`: LF everywhere except `.ini` (CRLF, the game reads them).

## Code layout and conventions

Each plugin has the same shape:

```
src/main.cpp                 TGL_PluginInit: validate host, set_exe_base, load_config, install_* in order
src/config.{h,cpp}           struct Config, one field per ini key, defaults = feature off; load_config()
src/log.h                    logf(): one line in the_grand_loader.log, tagged with the plugin name
src/hooks/memory.{h,cpp}     expect_bytes, write_bytes, replace_pointer, redirect_call, chain_call, jump_hook, make_trampoline
src/hooks/thunk.h            CP_SAVE_ALL / CP_RESTORE_ALL for naked thunks
src/game/*.h                 offsets, globals and tiny readers for game objects (world.h, string.h, objects.h, ...)
src/features/<area>/<x>.{h,cpp}   one feature = one ini key = one install_<x>(const Config&)
```

- **A feature file starts with a comment block** naming the function (`FUN_xxxxxxxx` / RE name), the VA of each
  instruction it touches, what the registers hold there, and why the change is result-preserving. See
  `patch/src/features/stability/supply_guard.cpp` for the canonical shape.
- **Every patch site is guarded**: `constexpr uint32_t kSiteRva`, `constexpr unsigned char kSiteBytes[]`,
  `expect_bytes` / `jump_hook` / `redirect_call` refuse and `logf("<key>: site %08X not recognised, off", ...)`
  when the bytes differ. A refused site must leave the game unpatched and playable.
- **Every feature logs one line at install**: `"<ini key>: disabled"`, `"<ini key>: active, <what>"`, or why it
  is off. Logs print **listing VAs** (`hooks::listing_va`), so they can be looked up in `docs/re/`.
- **Addresses in code**: the Performance Patch stores RVAs (`kXxxRva = VA - 0x00400000`, resolved with
  `hooks::exe_addr`); the Script Extender stores listing VAs resolved with `hooks::exe_va`. Keep each plugin
  consistent with itself. Never hard-code a runtime address: the exe is relocated.
- **Naked thunks** save everything (`CP_SAVE_ALL`: PUSHAD, PUSHFD, XMM0-7) because LTCG callers keep live values
  in any register across calls. Read the call site in Ghidra before writing a thunk; Ghidra's signature is a
  starting point, not the truth. Stack arguments of the hooked call sit at `[esp + CP_SAVED_ALL_BYTES + 4]`.
- **Install order in `main.cpp` matters** where two features touch the same site (they `chain_call` onto what
  is already there). Moving a line means checking both features' sites; the comment above each block says why.
- **Memory the game frees** comes from the game's heap (`game::allocate` / `game::release`, the exe's
  `operator new/delete`), never from our CRT (static `/MT`, separate heap). `GameString` views are built for the
  game to read, never for it to destroy.
- `TGL_PluginInit` runs inside the loader's `DllMain` under the loader lock: patch, hook, read files; no
  thread creation you wait on, no waiting on synchronisation objects. Start threads from a later hook.
- Namespaces: `cp` / `se` root, `::hooks`, `::game`, `::features`, `::parallel`. File-local state in an
  anonymous namespace; globals `g_`, constants `k`, enum-like ini values as `int` with the meaning in the
  `Config` comment. `/W4 /permissive-`, no warnings tolerated. No exceptions reach game code.
- Multiplayer: anything on the lockstep path must be bit-identical on every machine (no `std::unordered_*`
  iteration order, no `float` fast paths, every RNG draw goes through the counted generator). A feature that
  changes results says so in its doc page and in the ini comment.

## Reverse-engineering rules (docs/re/)

- `docs/re/symbols/*.csv` define names and addresses; `systems/*.md` explain; `log.md` records dated
  attempts, measurements and corrections. **One fact, one place**: a page may repeat a VA for readability but
  never introduces one that is not in the CSVs. Every row and claim carries a confidence (`C` confirmed in the
  listing, `I` inferred, `?` guess) and a source (`cp` ours, `re` maxioten's repo, not yet re-checked).
- Addresses are listing VAs (image base `0x00400000`). Runtime = `exe_base + (VA - 0x00400000)`.
- Names: `Class_Method` (`CCasusBelliType_CanUse`), `Subsystem_Verb` (`Country_DailyUpdate`), `g_` globals,
  `k` constants, `str_` strings. English only.
- A new or changed patch site gets a row in `patch_sites.csv` with its original bytes; then run
  `check_patch_sites.sh`.
- The full decompilation (`docs/re/decomp/`) is generated locally and git-ignored: it is derived from the
  game binary and must not be published. Quote short excerpts in `systems/` pages, never whole functions.
- `docs/re/` covers the engine and these patches only. **No mod content** (TGC tags, events, decisions)
  in the knowledge base; mod-specific findings go in `docs/re/raw/decisions/` style analyses at most.
- [maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering) is a
  third-party, unlicensed source: take facts, rewrite in our words, mark `src=re`, credit on the page.
- Ghidra: headless workflow and the shared server repo `VictoriaII` in [docs/re/ghidra.md](docs/re/ghidra.md).
  Sync with `ghidra_sync.cmd`; the password comes from `GHIDRA_PW` or a prompt. **Never store it** in the repo,
  in `ghidra_sync.local.cmd`, or in notes.

## Adding a feature (checklist)

1. RE first: name the functions in Ghidra, add the rows to `symbols/*.csv`, write or extend the `systems/` page
   (summary, entry points, flow, data, numbers, patch sites, open questions). Add a dated `log.md` entry.
2. `Config` field + ini key with a one-line comment in both `config.h` and `mod/<plugin>.ini`. The `Config`
   default is off (a missing key disables the feature); the shipped ini turns on what is result-preserving or
   a pure fix and ships profilers and diagnostics at 0.
3. `features/<area>/<x>.{h,cpp}`: header comment, guarded sites, install log line. Add the `.cpp` to the
   plugin's `CMakeLists.txt` and the `install_` call to `main.cpp` at the right place in the order.
4. Row(s) in `patch_sites.csv` (`status`: proposal, experimental, built, confirmed, shipped, documented,
   diagnostic, unsafe), run `check_patch_sites.sh`.
5. Doc page `docs/<ini-key>.md` (Source / Why / How) and a row in the table of `docs/README.md`
   (or `script_extender/README.md` for the Script Extender). Localisation keys go in
   `mod/script_extender/localisation/00_script_extender.csv` (game CSV format, `;` separated).
6. Build with `build.cmd`, ask the user to run the game and send `the_grand_loader.log`.

## Gotchas

- `std::string` in the game is the MSVC 2010 layout, 0x1C bytes (`game::GameString`, `static_assert`ed).
  `std::vector` is `{begin, end, capacity}` of raw pointers. Do not use our STL types to read game memory.
- Many internal functions take arguments in `ECX`/`EDX`/`ESI`/`EDI` plus stack and clean their own stack
  (`RET n`). Check the call site, not the decompiler's guess.
- `.text` padding `0x00C8911A`–`0x00C891FF` is the only free code cave; each patch gets its own slice.
  Prefer `make_trampoline` (allocated executable memory) over caves.
- Date = `world + 0xB0C` in hours since year -5000, 365-day years. Day = hours / 24.
- Timing hooks cost: a `QueryPerformanceCounter` pair (~100 ns) can exceed the work it measures; sample 1 in N.
- The ini files are CRLF and read by the game's own parser; keep the one-line-per-key comment style.
- Commit messages: short subject in English saying what changed (`RE symbols: treaty, CB and diplomacy UI
  names`, `missing state crash`), not `a`, `updates` or `ajusts`. Pure symbol updates as their own commit, so the
  CSV diff is the record of what was learned.
