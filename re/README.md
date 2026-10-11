# Reverse engineering of `v2game.exe` 3.04

Everything known about the game binary that the patches rely on, in one place.

## Target

Every address in this folder is a virtual address (VA) of this exact file:

| | |
|---|---|
| file | `v2game.exe` 3.04 (Steam), 12 294 656 bytes |
| SHA-256 | `62D48C204364DD706584777C2E2B3C7AB3C5F1DD0170872554943575D53D6648` |
| image base | `0x00400000` in the listing, **but the exe has a relocation table and is loaded elsewhere at runtime (ASLR; `0x00080000` was seen on 2026-10-11)**. Code must use RVA = VA − `0x00400000` through `hooks::exe_addr`; addresses read at runtime must be converted back (VA = runtime − base + `0x00400000`) before comparing with this folder |

File offsets differ per section. Use them only for byte checks and static patching:

| section | VA range | file offset |
|---|---|---|
| `.text` | `0x00401000`–`0x00C8911A` | VA − `0x00400C00` |
| `.rdata` | `0x00C8A000`–`0x00EF13D9` | VA − `0x00401A00` |
| `.data` | `0x00EF2000`–`0x00F1FA00` (file-backed part; the rest is zero-filled at load) | VA − `0x00402600` |

`.text` padding `0x00C8911A`–`0x00C891FF` is free for code caves. Give each patch its own cave.

## Layout

```
re/
├── README.md        this page
├── symbols/         the source of truth for names and addresses
│   ├── functions.csv     va, name, conf, area, src, note
│   ├── globals.csv       globals, constants, strings, vtables
│   ├── structs.csv       struct, offset, field, type, conf, src, note
│   └── patch_sites.csv   every known byte patch / hook site with its original bytes
├── systems/         one page per subsystem (below)
├── log.md           dated research log: questions, measurements, dead ends, corrections
├── ghidra.md        headless Ghidra setup and the query/sync scripts
├── raw/             unedited material: decompilations, call lists, keyword maps, profiler logs
└── decomp/          full decompilation + index.tsv, generated locally (git-ignored, see ghidra.md)
```

## Systems

| page | covers |
|---|---|
| [tick.md](systems/tick.md) | frame, hourly/daily tick, `Country_DailyUpdate`, calendar, RNG, where the day's ms go |
| [game-speed.md](systems/game-speed.md) | speed index and its two tables |
| [economy.md](systems/economy.md) | world market, prices (fixed-point clamp), stockpiles, loans |
| [threads.md](systems/threads.md) | TBB, the worker-thread class, why the economy is serial |
| [triggers.md](systems/triggers.md) | trigger containers, scopes, values, effects, keywords |
| [casus-belli.md](systems/casus-belli.md) | `cb_types.txt`, `can_use`, the AI's daily CB scans |
| [decisions.md](systems/decisions.md) | where decisions are evaluated and how often |
| [events.md](systems/events.md) | loading, daily MTTH scan, firing, delayed events |
| [ai-diplomacy.md](systems/ai-diplomacy.md) | opinion score, acceptance chance |
| [ai-national-focus.md](systems/ai-national-focus.md) | national focus scoring |
| [technology.md](systems/technology.md) | research availability and the civilization gates |
| [factories.md](systems/factories.md) | factory construction gates |
| [loading.md](systems/loading.md) | what runs once after a game is loaded |
| [checksum.md](systems/checksum.md) | file and runtime checksums, the resign/menu bug |
| [ui-console.md](systems/ui-console.md) | console, fog of war, buttons, version label |
| [performance.md](systems/performance.md) | measured budget, lessons, ranked performance candidates |
| [network.md](systems/network.md) | ENet session, the 35 ms per-frame network wait |

## Conventions

- **Confidence:** `C` confirmed in the listing, `I` inferred from context, `?` guess. Every row of
  the CSVs and every claim in a page carries one.
- **Source** (`src` column): `cp` = found or re-checked in this repo's sessions; `re` = taken from
  [maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering)
  and not yet re-checked beyond what the page says. That repo is someone else's work, with no
  license: we keep facts (addresses, offsets, behaviour), rewritten in our own words, and credit it
  on every page that uses it.
- **Names:** `Class_Method` for methods (`CCasusBelliType_CanUse`), `Subsystem_Verb` otherwise
  (`Country_DailyUpdate`), `g_` for globals, `k` for constants, `str_` for strings. English only.
- **One fact, one place:** an address is defined in `symbols/`; pages refer to it by name and may
  repeat the VA for readability, but never introduce one that is not in the CSVs.

## Adding knowledge

1. Explore in Ghidra (GUI or `V2Dump.java`). Name what you understand.
2. `V2Symbols.java export` lists the new names. Add the ones you trust to the CSVs, with
   confidence and a note.
3. Write or extend the page in `systems/`, using this shape: summary (what a patch author needs),
   entry points, flow, data, measured numbers, patch sites, open questions.
4. If you tried something, measured something, or found that something was wrong, add a dated entry
   to `log.md`. Corrections go there **and** in the page.
5. Run the checks before committing:

```sh
tools/scripts/check_patch_sites.sh "<game dir>/v2game.exe"     # original bytes of every patch site
analyzeHeadless ... -readOnly -postScript V2Symbols.java <repo>/re/symbols <out> check   # names vs Ghidra
```
