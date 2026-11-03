# Coding rules

Rules for the C++ in `launcher/`, `patch/` and `script_extender/`, and for the knowledge base in `docs/re/`.
Each rule says why, cites a source where one exists, and notes where the tree already follows it or still
deviates. Short version for day-to-day work: [../CLAUDE.md](../CLAUDE.md).

Context that shapes everything below: the target is `v2game.exe` 3.04, a 32-bit MSVC 2010 build with
link-time code generation and a relocation table (ASLR). Our code is C++20 under MSVC with `/W4 /permissive-`,
static CRT (`/MT`), and runs inside the game's process as DLLs loaded by a `dinput8.dll` proxy.

## 1. Loader and `DllMain`

The loader's `DllMain` calls each plugin's `TGL_PluginInit` with the loader lock held, before any game code
runs. Patching there is deliberate: no game thread can be inside a site we rewrite. The price is the
`DllMain` rules.

- **Do the minimum in `DllMain` / `TGL_PluginInit`: read the ini, write bytes, install hooks.** Microsoft's
  ideal `DllMain` is "an empty stub"; everything else is postponed. Ours is not empty, so the rest of this
  list is what keeps it safe. ([Dynamic-Link Library Best Practices](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices))
- **Never call, directly or indirectly, `LoadLibrary`, `CoInitialize`, registry, `CreateProcess`, User32 /
  Gdi32 / shell functions, or anything that may take the loader lock.** Deadlock or crash. Kernel32 calls,
  file I/O, `VirtualProtect`, `memcpy`-level patching and creating synchronisation objects are on the safe list.
  (same page) *Status:* `script_extender` links `shell32` but calls nothing from it today; if it ever does,
  only from a hook, never from `TGL_PluginInit`.
- **No `CreateThread` you wait on in init; start threads from a later hook.** A thread created under the
  loader lock cannot run until every `DllMain` returns. ([Raymond Chen](https://devblogs.microsoft.com/oldnewthing/20070904-00/?p=25283))
  *Status:* `worker_pool` starts its threads on the first `parallel::run`, from the game thread. Keep it that way.
- **No CRT or `std` work in static initialisers.** Under `/MT` they run as part of `DllMain` and invert
  the CRT-lock / loader-lock order. Static-storage objects must be trivially constructible and destructible
  (`constexpr` tables, PODs, pointers). ([Barthe](https://blog.barthe.ph/2009/07/30/no-stdlib-in-dllmai/), [PVS V718](https://pvs-studio.com/en/w/v718/), [Google style, static objects](https://google.github.io/styleguide/cppguide.html#Static_and_Global_Variables))
  *Status:* the script extender keeps all such state behind function-local-static accessors (`g_cbs()`);
  the patch still has file-scope `std::vector`s in the performance features (trivially destructible
  contents, no dynamic initialiser). New code uses the accessor form.
- **`DLL_PROCESS_DETACH` stays empty.** Threads are gone at exit; cleanup there is unsafe. (MS, same page)
- **The CRT boundary is a wall.** Static CRT means two heaps and two sets of CRT state in the process.
  Memory the game frees comes from the game's `operator new` (`game::allocate`); memory we free comes from ours.
  No `FILE*`, locale or `errno` crosses. ([Potential errors passing CRT objects across DLL boundaries](https://learn.microsoft.com/en-us/cpp/c-runtime-library/potential-errors-passing-crt-objects-across-dll-boundaries))

## 2. Hooking

- **Every address is `exe_base + RVA`.** Never a listing VA used directly: the image moves per run.
  ([Detours overview](https://github.com/microsoft/Detours/wiki/OverviewInterception), [Halladay](https://kylehalladay.com/blog/2020/11/13/Hooking-By-Example.html))
  *Status:* done through `hooks::exe_addr(rva)` (patch) and `hooks::exe_va(va)` (script extender). Two
  conventions for one thing; whichever plugin you are in, do not mix them in a file.
- **Verify the original bytes before writing. On mismatch: write nothing, log the listing VA, leave the
  feature off.** Catches a different exe version and a site another plugin already took. (community practice,
  no formal standard; [PLUGINS.md](../launcher/PLUGINS.md) makes it the loader's contract)
  *Status:* `expect_bytes`, `jump_hook`, `redirect_call` enforce it; keep it so for every new site.
- **Stolen bytes are whole instructions, at least 5, with no PC-relative operand and no branch target
  inside them.** A thread landing mid-instruction, or a `jcc` into the hole, executes garbage.
  ([Detours](https://github.com/microsoft/Detours/wiki/OverviewInterception), [PolyHook](https://github.com/stevemk14ebr/PolyHook_2_0/blob/master/README.md))
  *Status:* `make_trampoline` copies bytes verbatim and does not relocate `rel8/rel32`. The feature's header
  comment must show the instructions and state that none is relative or a branch target. Check both in the
  listing; the decompiler will not warn you.
- **Write sequence: `VirtualProtect` → write → restore protection → `FlushInstructionCache`.**
  ([FlushInstructionCache](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-flushinstructioncache))
  *Status:* `hooks::write_bytes` does this. Never write code bytes any other way.
- **Only patch while no game thread can be in the site.** That is `TGL_PluginInit` for us. A hook installed
  later from a game thread must target code that thread alone executes, or suspend the others (Detours'
  `DetourUpdateThread`). ([DetourUpdateThread](https://github.com/microsoft/Detours/wiki/DetourUpdateThread))
- **Naked thunks assume a custom calling convention.** LTCG callers keep live values in any register,
  XMM included, and do not follow `__thiscall`/`__cdecl` for internal functions. Save PUSHAD + PUSHFD + XMM0-7
  (`CP_SAVE_ALL`), x87 too when the site is x87 code; read the call site, not the decompiler's signature.
  ([naked functions](https://learn.microsoft.com/en-us/cpp/cpp/naked-function-calls), Cxbx-Reloaded's LTCG
  prologue macros after register-poisoning bugs: [post](https://www.patreon.com/LukeUsher/posts/cxbx-reloaded-47631983))
- **A `CALL` site that already points elsewhere is chained, not overwritten.** Keep the previous target and
  call through it; log that the chain happened. Refuse when the bytes are not a `CALL` at all. (PolyHook
  "follows already hooked functions"; no multi-plugin standard exists, so `chain_call_logged` is ours.)
  *Status:* `chain_call_logged` exists in both plugins; the treaty feature chains onto `demand_province`'s
  site with it.
- **One slot, not one vtable.** Swap a single vtable entry with `replace_pointer` (checked against the
  expected old value). Replace a whole `vptr` only with a cloned table whose every slot is known.
  *Status:* `cb_cull` swaps one `CCasusBelliType` slot; the treaty feature builds a full `CTreatyAction` table
  by copying the peace action's vtable and replacing the slots it owns. Both are the intended shapes.
- **Code caves are a last resort.** `.text` padding `0x00C8911A`–`0x00C891FF` is small and shared; prefer
  `VirtualAlloc`'d trampolines. Register the cave slice in `patch_sites.csv` when you must.

## 3. Mirroring game structs

- **Every mirrored layout carries `static_assert(sizeof)` and `static_assert(offsetof)` for each confirmed
  field.** Padding differences shift fields silently.
  *Status:* `GameString` has both. Offsets elsewhere are `constexpr` `kXxx` with the struct in
  `docs/re/symbols/structs.csv`; that is acceptable as long as the CSV row and the constant agree.
- **Never use our `std::string` / `std::vector` as the game's type.** MSVC 2010 release `std::string` is
  `union { char buf[16]; char* ptr; } + size + capacity` = 0x1C bytes, inline while `capacity == 15`; a debug
  build of the same compiler adds a proxy pointer and is a different layout. Hand-written `GameString` /
  `IntVector` / `Range` with asserts. ([Raymond Chen on MSVC string layout](https://devblogs.microsoft.com/oldnewthing/20230803-00/?p=108532))
- **`reinterpret_cast` lives in `game/*.h` helpers, not in features.** Features read `game::country(i)`,
  `game::read32(p + kOffset)`; the cast is written once and named. No C-style casts, no union punning.
  ([ES.48](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#res-casts), [ES.49](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#res-casts-named), [C.183](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#ru-pun))
  *Status:* `GameString`'s `union` is a deliberate exception (it mirrors the game), documented in the struct.
- **No exception reaches game code.** Every function called from a thunk or installed in a vtable is
  `noexcept` in effect: catch inside, log, return a safe value. The game's frames cannot unwind ours.
  ([E.12](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#re-noexcept))
  *Status:* nothing throws today, but `std::string` / `std::vector` in hook paths can. Mark the entry points
  `noexcept` so a throw terminates with a stack rather than corrupting the game.
- **Ownership never crosses.** Objects the game will destroy are built with the game's allocator and its
  constructors; `GameString` views are read-only for the game. ([I.11](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#ri-raw))

## 4. Lockstep determinism

The multiplayer features and every "same results" optimisation sit on the lockstep path: each machine runs
the same simulation and compares a daily checksum.

- **FP state is pinned, not assumed.** D3D and libraries change the x87 control word and MXCSR under you.
  `pin_fpu` sets both each tick; a hook that changes them restores them before returning. On x86
  `_controlfp_s` covers both. ([Dawson, floating-point determinism](https://randomascii.wordpress.com/2013/07/16/floating-point-determinism/), [`_controlfp_s`](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/controlfp-s))
- **Our sim-side code builds with `/fp:precise`, no FMA, no `rcp`/`rsqrt` estimates, same binary on every
  peer.** ([Gaffer on Games](https://gafferongames.com/post/floating_point_determinism/))
- **RNG draws: same count, same order, on every machine.** Never add a draw that depends on local-only state
  (UI open, player's country, locale). `world_rng` exists because the game's clock-seeded generator broke this.
  ([Riot, fixing divergences](https://technology.riotgames.com/news/determinism-league-legends-fixing-divergences))
- **No hash-ordered iteration on the sim path.** `std::unordered_*` order and pointer values differ per
  process; iterate vectors or ids. Parallel features (`parallel_decisions`, `parallel_events`) only compute
  read-only results that the original serial loop consumes in its own order. ([bugnet](https://bugnet.io/blog/how-to-fix-multiplayer-desync-in-your-game))
- **Hash state to localise, not to guess.** `state_checksum` + `oos_log` name the differing element;
  add a hash before adding a theory.

## 5. The knowledge base (`docs/re/`)

- **Facts, not code.** Symbol tables, layouts, behaviour, numbers. Never the binary, game assets, or
  decompiled listings beyond a few quoted lines. Changing the format does not erase copyright.
  ([retroreversing, clean-room](https://www.retroreversing.com/clean-room-reversing))
  *Status:* `docs/re/decomp/` is git-ignored for this reason; keep it so.
- **Bring your own binary.** The repo builds and checks against the user's own `v2game.exe`
  (`check_patch_sites.sh "<game dir>/v2game.exe"`), as sm64, Shipwright and OpenRCT2 do.
- **Placeholder names until confident.** `FUN_xxxxxxxx` / `DAT_xxxxxxxx` stay until a name reaches `I` or `C`;
  the `note` column says how it was established. ([zeldaret/mm CONTRIBUTING](https://github.com/zeldaret/mm/blob/main/docs/CONTRIBUTING.md) uses `NON_MATCHING` tags the same way)
- **Addresses are bound to a named image.** Every table is for 3.04, SHA-256 in `docs/re/README.md`;
  OpenRCT2's wiki works the same way, by address of one exact exe.
- **A queue for new finds, a table for settled ones.** Ghidra GUI → `V2Symbols export` → move trusted rows
  into the CSVs. OpenRCT2 keeps a "Newly Identified Fields" page for the same reason.
  ([OpenRCT2 wiki](https://github.com/OpenRCT2/OpenRCT2/wiki/Newly-Identified-Fields))
- **Every intentional divergence from vanilla is written next to the code and in the feature page.**
  So the RE still maps onto what ships.
- **Share curated data, not the project file.** CSVs in git, the Ghidra server as a synced copy; the
  `ghidra-data` model (type archives and symbol files, no `.gpr`). ([ghidra-data](https://deepwiki.com/0x6d696368/ghidra-data))

## 6. C++ style

[`.clang-format`](../.clang-format) and [`.editorconfig`](../.editorconfig) settle layout. The rest:

- **Constants over macros.** `constexpr uint32_t kSiteRva`, `constexpr unsigned char kSiteBytes[]`,
  `enum class` for modes. Macros only for inline asm sequences (`CP_SAVE_ALL`) and the version string.
  ([ES.31](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#res-macros2), [ES.45](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#res-magic), [Enum.3](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#renum-class))
  *Status:* ini modes are `int` (`parallel_decisions = 0/1/2`) with meaning in a comment. Acceptable for ini
  mirrors; use `enum class` for anything internal.
- **Globals: few, `g_`-prefixed, file-local in an anonymous namespace, trivially destructible.**
  A thunk needs an absolute slot (`jmp dword ptr [g_resume]`), so some are unavoidable; keep them to the
  addresses and pointers thunks read. ([I.2](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#ri-global), [SF.21](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#rs-unnamed))
- **Headers: `#pragma once`, self-contained, no `using namespace`, no anonymous namespaces.** The header
  of a feature exposes `install_<x>(const Config&)` and nothing else. ([SF.7](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#rs-using-directive), [SF.11](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#rs-contained))
- **Always initialise; no narrowing; `nullptr`; named casts only.** `/W4` already flags most of it.
  ([ES.20](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#res-always), [ES.46](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#res-narrowing), [ES.47](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#res-nullptr))
- **Comments explain the game, not the C++.** The header block of a feature is the listing excerpt and the
  argument why the change is safe; `install_` logs say what is active. A reader with the listing open must be
  able to verify every claim.
- **Measure before and after.** A timing hook costs ~100 ns per `QueryPerformanceCounter` pair; sample 1 in
  N. Numbers go in `docs/timings.md` and `docs/re/log.md`, with the conditions.

## 7. Repository etiquette

- **Commit subjects say what changed, in English.** Symbol updates separately (`RE symbols: ...`), so the CSV
  diff records what was learned.
- **A feature is complete when it has:** symbols, a `systems/` page or section, a `log.md` entry, the
  `Config` key and ini line, the code, the `patch_sites.csv` rows, the `docs/<key>.md` page, the README row,
  and a game log from a real run.
- **Secrets never land in the tree.** Ghidra server password via `GHIDRA_PW` or a prompt;
  `ghidra_sync.local.cmd`, `deploy.local.cmd` are git-ignored and stay machine-local.
- **CLAUDE.md stays under ~200 lines and holds only what cannot be inferred from the code.** Longer files
  lose adherence; procedures go in docs like this one, linked, not pasted. ([Claude Code memory](https://code.claude.com/docs/en/memory), [best practices](https://www.anthropic.com/research/claude-code-best-practices))

## Gaps

No authoritative source was found for: a multi-plugin hook-chaining convention (ours is `chain_call`), a
Microsoft document of the exact VS2010 x86 `std::string` layout (we rely on the listing and the asserts),
a codified confidence schema for RE symbol tables (ours is `C`/`I`/`?`), and anything Paradox-specific on
FP or RNG handling.
