# Research log

Oldest first. Each entry: date, question, where the answer lives, what is still open. Facts that outlive an entry belong in `symbols/` and `systems/`; this log keeps the history (what was tried, what was measured, what was wrong).

## 2026-10-01 — Casus belli evaluation cost ("CBs are checked N×N")

- **Question:** does the engine evaluate `can_use` of every CB type for every attacker × defender pair, and does trigger ordering matter?
- **Answer:** yes, daily, for every AI country × every other country, but only for CB types with `always = yes` or already held in that relation; `CanUse` is an uncached `CAndTrigger::Evaluate` with `THIS` = attacker, `FROM` = defender; AND/OR/NOT short-circuit in file order. → [systems/casus-belli.md](systems/casus-belli.md)
- **Side products:** full `CCasusBelliType` field map, keyword-id → offset table, trigger container semantics, console entry points (`debug cb_use`), the Ghidra headless tooling in `docs/tools/ghidra/` (this repo).
- **Open:** meaning of the AI gates `country+0x37C/0x37D`, `ai+0x19`, `DAT_01318E5C`, `country+0xE4C`; whether `CNullCasusBelliType` instances sit in the manager vector.

## 2026-10-01 — Version label site for the Community Patches loader

- **Question:** where does the corner label "V2 v3.04 (XXXX)" come from, so a loader can append text to it?
- **Answer:** `std::string("V2 v3.04", 8)` at `0x00633826` inside the application constructor; patch the `push imm8` length and the pointer only. Implemented in the Community Patches loader (`launcher/src/version_label.cpp`); patch site `cp_version_label`.

## 2026-10-01 — Engine worker threads (TBB)

- **Question:** what does `task_scheduler_init::initialize(threads)` affect?
- **Answer:** only three `parallel_for` sites (entity/spatial registration near `FUN_0068F390`, graphics near `FUN_0099DA20`); the simulation is otherwise single-threaded. Not yet written up as a document.

## 2026-10-02 — `cull_cb_checks`: attacker-only culling of `can_use`

- **Question:** can the daily N×N CB scan be short-circuited per attacker without changing results?
- **Answer:** yes. Hooked `CCasusBelliType::CanUse` (vtable `0x00E33724+0x20`) and the two AI call sites of `IsAnyCBUsable`; attacker-only top-level conditions are evaluated once per (CB, attacker, day). Classification of all 236 trigger classes by decompiling their `Evaluate`: 174 scalar + 20 country-reference (instance flags `+0x18` THIS / `+0x19` FROM) usable, 40 pair-dependent excluded. → [cb-cull.md](../cull-cb-checks.md)
- **Correction to the previous entry:** in `can_use` the scope is the attacker and `THIS` is the defender (`scope+0x24`); `FROM` is unset. The decomp document was updated.
- **Open:** measure the real gain on a heavy mod (log line every 30 days reports calls/culled/evaluated); whether the scalar parsers' `= THIS` (ignored by their `Evaluate`) is a script-side bug worth documenting.

## 2026-10-02 — `decisions_once_per_day`: `potential` evaluated once per country per day

- **Question:** where are decisions evaluated and how often; can they be gated to once per day like events?
- **Answer:** `CDecision` (`0x00E29B54`, ctor `0x008743C0`) holds `potential` at `+0x24`, `allow` at `+0x40`, `effect` at `+0x5C`, `ai_will_do` at `+0x110`; keyword handler `0x00874CE0` (potential `0x729`, allow `0x72A`). Evaluated daily by the AI (`0x0085D270`) and the country alert (`FUN_005399C0`), and on every refresh by the politics view / decision list / tooltips. Patched by cloning `CAndTrigger::vftable` for the `potential` blocks with a cached `Evaluate`; `allow` left live. (feature removed 2026-10-02, see below)
- **Also found:** the script trigger `casus_belli = X` (`CCasusBelliTrigger::Evaluate` `0x008D28C0`) calls `IsAnyCBUsable`, i.e. runs every `always = yes` CB's `can_use` against that target each time the event/decision is checked. Unmeasured by `cull_cb_checks`, which only covers the two AI scans.
- **Open:** measure `CanUse` from all callers (sampled stack attribution) to see the split between AI scans, `casus_belli` triggers and UI; consider a per-(attacker, defender, day) cache of `IsAnyCBUsable` for the script-trigger path.

## 2026-10-02 — Decisions are already once per day; hook overhead lesson

- **Measured:** 527 k `potential` checks/day = decisions × countries; cache hit rate ≈ 0 %. The engine evaluates each decision once per country per day already; `potential` costs ~25 ms of a ~245 ms day (10 %), 47 ns per block on average.
- **Lesson:** a `QueryPerformanceCounter` pair per call (~100 ns) is more expensive than the work it measures at this granularity; the first `decisions_once_per_day` build slowed the first year from 1:09 to 1:29. Both features now sample timing 1 in 64. `decisions_once_per_day` defaults to `0`.
- **Open:** whether an "evaluate decisions every N days" option is acceptable gameplay-wise; where the remaining ~220 ms/day go (tick profiler).
- **Removed (2026-10-02):** `decisions_once_per_day` and its source were deleted after the measurement above; the findings stay in this log.

## 2026-10-02 — `cull_cb_checks` v2: replace the AI's IsAnyCBUsable loop

- **Problem:** v1 hooked `CanUse`, still called once per defender; hook cost ≈ the ~50 ns evaluation it skipped, so the first year got slightly slower (user report).
- **Change:** the two AI call sites now go to a replacement loop. Per (attacker, day) a pre-pass evaluates the attacker-only children of every CB's `can_use` (classified once per type) and keeps the live types; per defender only those are passed to the original `IsCBUsable` (`0x0092EEC0`, index in EDI via inline asm). `cull_cb_profiling` gates all counters/clock.
- **Open:** first-year timing with cull on / profiling off vs baseline 1:09.6.

## 2026-10-02 — `tick_profiler`: per-block timing of the daily tick

- **Why:** decisions (~10 %) and CB scans (≤5 %) do not explain the ~240 ms/day; the remaining ~85 % had no owner.
- **How:** eight single-caller call sites in the tick redirected to register-preserving thunks that swap the return address (works for the game's custom conventions); inclusive/exclusive ms/day per block every 30 days. → [tick-profiler.md](../profilers/tick-profiler.md)
- **Open:** first report from the user's mod; then split the largest exclusive block.
- **First result:** ~185 ms/day: 43 % outside the simulation step (render/UI/wait), 37 % in `DayTick` outside the province/country/event loops, 10 % `Country_DailyUpdate` (which also holds the decisions' `potential`, now estimated at 10–12 ms/day: the earlier 25 ms included the per-call clock), AI ≤4 %, events 2 %, provinces 1 %. Level 2 (every call inside `DayTick`) added to split the 37 %.
- **Level 2 result (2026-10-03):** `DayTick` exclusive dropped to 2.7 ms once its calls were split. The simulation cost sits in two functions: `Economy_Update` (`0x00489990`, world market, "worldmarket.cpp", 1 call/day) at ~37 ms/day and the per-country pre-pass `FUN_00508590` (683 calls/day) at ~21 ms/day, next to `Country_DailyUpdate` at ~20 ms. `time_throttle` is called 1375×/day and costs 0.2 ms. Level 3 (calls inside those two functions, 90 sites) added.
- **Profiler bugs fixed on the way:** return stub must not clobber `ECX` (whole-program optimisation lets callers assume callee-preserved registers); x87 state saved; frames matched by `ESP` because `time_throttle` renders a frame from inside the tick and re-enters `world_update`; thunk ids must be gap-free (the first macro generated 10..17, 20..27 and mis-dispatched site 18).
- **Level 3 result (2026-10-03):** `Economy_Update` (37 ms) = `WorldMarketUpdate` `0x00484060` 21.6 ms (loops countries → states → factories: `FUN_00482FF0` per factory, `FUN_00485E40` per country) + `ProcessMarketAndGoodsDistribution` `0x004808D0` 8.7 ms + `FUN_00488080` 3.3 ms. `country_prepass` (27 ms) = `FUN_00515490` 8.7 ms (322 calls) + `FUN_0050EA30` 3.3 + `FUN_0051FD10` 3.0 + `FUN_0050B9B0` 2.4 + 6.5 own; `FUN_0068F390` is called 19 047×/day. `Country_DailyUpdate` keeps ~21 ms of its own. At max speed the ~90 ms/day outside `world_update` is the rest of `CInGameIdler::Update` (`0x00654D80`, vtable slot `0x00E0A350`), the per-frame function that also contains the tick via `ProcessGameTimeAdvance`. Profiler gained vtable-slot hooks (`frame_update` at level 1), level 4 (frame internals, 91 sites) and level 5 (market / country internals, 300 sites); 600 thunks.

## 2026-10-03 — Dead decisions: can once-only decisions leave the queue for good?

- **Question (user):** the mod emulates "take once" with `set_country_flag` in the effect and `NOT = { has_country_flag }` in `potential`. If no `clr_country_flag` for that flag exists anywhere, can the decision be dropped from the checked list permanently, like `fire_only_once` events?
- **Static count (TGC, 1653 decisions):** 767 use the once pattern with a flag that is never cleared (412 of them also `tag`-locked); 404 have a `NOT has_global_flag` gate on a global flag that is set somewhere and never cleared (globally dead once set); 666 are `tag`-locked; 1328 have at least one monotonic condition. First condition of `potential`: `tag` 637, `owns` 221, `has_country_flag` 141, `is_greater_power` 130, `primary_culture` 128. → [raw/decisions/](raw/decisions/)
- **Engine facts:** `AI_DiplomacyUpdate` evaluates every decision's `potential` for every AI country every day with no early exit; the player alert (`FUN_005399C0`) stops at the first available one. Country flags are per country, so "remove from the list" is only correct globally for tag-locked decisions or global-flag gates; for the rest it is per (country, decision). `CTagTrigger` (`0x00E30C80`) keeps the literal tag's country index at `+0x20` and compares it with `scope+0x0C`.
- **Expected value:** each dropped decision saves ~320 evaluations/day of ~21 ns each (≈7 µs); tag-mismatched evaluations of the 666 tag-locked decisions are ~212 k/day from day 1, probably the larger share. Both bounded by the ~10–12 ms/day (~6 %) that decisions cost in total.
- **Built to measure instead of guess:** `decision_profiler` — the `potential` CAndTrigger at `CDecision+0x24` has its own vtable pointer, so each decision's block is pointed at a private copy of `CAndTrigger::vftable` whose `Evaluate` slot is the hook; no loop or shared vtable touched. Installed from a `CHourlyTickCommand::Execute` vtable-slot thunk that re-scans the manager lists (handles reloads). Samples 1 in 16; per decision: evals, ms, passes; plus ms spent on tag-locked decisions evaluated for another country and on pairs whose flag gate already fails (the gate is re-evaluated alone on sampled misses).
- **Open:** first report from TGC at max speed; then decide between a tag-lock skip (day 1 gain), a per-country dead bitset (late-game gain), or neither.

## 2026-10-03 — `decision_profiler` results; `cull_decision_checks` + `decision_profiling` built

- **Measured (TGC, 1836, max speed, two 30-day windows):** 529 k `potential` evaluations/day = 23.5 ms of ~200 ms (12 %), 2500 pass/day. 213 k/day (6.5 ms) are tag-locked decisions evaluated for the wrong country; 10 k/day (0.45 ms) are pairs whose flag gate already fails. Flat distribution: heaviest decision 0.29 ms/day (`pay_owed_steamers`), top 25 = 2.5 ms. 665 tag-locked, 1291 with a top-level `NOT has_*_flag` gate. → [raw/profiler/decisions-2026-10-03.log](raw/profiler/decisions-2026-10-03.log)
- **Built:** `decision_profiler` renamed/extended into `cull_decision_checks` (both layers: tag lock, dead-pair bitset invalidated by hooks on `CClrCountryFlagEffect::Execute` `0x00882A40` / `CClrGlobalFlagEffect::Execute` `0x00882BA0`, vtable slot 11, plus a 30-day full reset) and `decision_profiling` (rewrites `v2cp_decisions.log` with running per-day averages for every decision; constant size). → [decision-cull.md](../cull-decision-checks.md)
- **Corrections:** `CHourlyTickCommand::Execute` is vtable slot 6 (`+0x18`), not 7; the gate counts above include flags that other scripts clear, which is why the runtime count (1291) exceeds the static once-only count (767 + 404).
- **Open:** first-year timing with `cull_decision_checks = 1 / 0`; confirm `passed/day` unchanged with cull on; late-game share of dead pairs.
- **Crash fixed (2026-10-03):** `CEffect::Execute` is `__thiscall(scope, arg2)`, two stack arguments (`RET 0x8`); the first hook declared one, cleaned 4 bytes instead of 8 and returned into the stack (crash EIP == ESP+0x10 right after loading, when history effects run). Lesson: check the `RET imm16` of a virtual before hooking its slot; Ghidra's parameter count follows usage, not the convention.
- **Layer 3 (user suggestion, 2026-10-03):** a top-level `ai = yes/no` (`CIsAiTrigger` `0x00E30EE8`, value `+0x1C`, reads the human-player table `world+0xAEC`) is evaluated alone before the block; false ⇒ skip. TGC has 136 `ai = no` and 322 `ai = yes` lines in decisions, so up to ~43 k evaluations/day for AI countries. Reported as `skip_ai` / flag `A`.

## 2026-10-03 — `year_timer` and a shared hourly-tick hook

- **Why:** first-year timings were taken with a stopwatch. The patch now logs `year 1836: 1.09 min wall (1.08 min active, 178 ms/day) over 365 day(s)` on every year change; "active" excludes gaps over 2 s between hourly ticks (pauses, menus, saves). Always on, one line per year.
- **Calendar:** world date is hours at `world+0xB0C`; 365-day years with no leap years, counted from year −5000 (day 2495140 ⇒ 1836). Year = `day / 365 − 5000`.
- **Structure:** `hooks/hourly_tick.{h,cpp}` owns the `CHourlyTickCommand::Execute` vtable slot (`0x00E12414+0x18`) and dispatches to registered callbacks (`on_hourly_tick`), so `decision_cull` and `year_timer` share one hook instead of competing for the slot.
- **Regression found by the user (2026-10-03, "cull_decision_checks parece estar pesando mais"):** the first build re-evaluated the flag gate on every miss of every gated decision (1291 × ~320 pairs ≈ 400 k `has_country_flag` map lookups a day, each costlier than an average `potential`) to decide whether to mark the pair dead. Fixed with a second bitset `known`: the gate is evaluated once per pair, then only after `set_*_flag` (forgets `known` for that country / everyone; `CSetGlobalFlagEffect::Execute` `0x00882B60`, `CSetCountryFlagEffect::Execute` `0x008829E0`, slot 11) or `clr_*_flag` (forgets both). Full reset on a date jump (load) and yearly instead of every 30 days.

## 2026-10-04 — `cull_cb_checks` default off

- **Measured with `year_timer` (TGC, 1836, max speed):** baseline 196 ms/day (1.11 min active), `cull_cb_checks = 1` 203 ms/day (1.14 min). Single runs, so within noise, but consistent with the 2026-10-02 profile: AI CB scans are 11.1 ms/day, of which `can_use` 4.8 ms and only ~58 % cullable (~2.8 ms, 1.4 % of the tick), against a per-attacker pre-pass every day. The N×N theory was right about the shape of the loop and wrong about its weight: 32 `always = yes` CBs with early-exiting `can_use` blocks cost ~2 % of a day.
- **Decision:** default `0` in the shipped ini and README; code and docs kept as the record of the investigation.

## 2026-10-04 — `cull_decision_checks` v2: per-country lists instead of a per-call hook

- **Measured (user, `year_timer`, TGC 1836, both with `cull_cb_checks = 0`):** 196 ms/day without, 203 ms/day with the v1 hook. v1 intercepted all 529 k `potential` calls to return false early for ~256 k of them; the lookup + branches on every call cost about what the ~30 ns evaluations saved. Same lesson as the CB cull and the decision cache: at tens of ns per call, a hook cannot win, the work must leave the loop.
- **v2:** `DecisionManager_Get()` (`0x00875040`) is called only from `AI_DiplomacyUpdate` (`0x0085D34F`, `0x0085D459`), with the AI object in `ESI` (`+0xC` country index). Both calls now go to a stub returning a private manager copy whose two lists are the country's filtered lists (tag lock, `ai`, flag gate evaluated once at build with a scope for that country). Lists are rebuilt on `clr_*_flag` (country / all), on a date jump, every 90 days, and on reload. Zero per-decision work for the AI; the player/UI path is untouched. The `potential` vtable swap now exists only for `decision_profiling`.
- **Open:** `year 1836` with v2 on/off; expected gain ≈ 8 ms/day (4 %) in 1836, more later as pairs die.
- **Correction (2026-10-04, from the first v2 log):** the list requests were 1-6/day, so `AI_DiplomacyUpdate` is not the daily loop. Xrefs of `DAT_01319730` found it: `AI_DecisionUpdate` `0x00821190` (ECX = AI object, `+0x34` country index), called at `0x0081EA9B` from the per-frame AI job (`CInGameIdler::Update` -> `FUN_006874C0` -> `FUN_0081E9E0`), once per AI country per day, reading the global directly; `vt[6] && potential && allow -> ai_will_do`. `FUN_005399C0` runs for the player only (guard at `0x0050B780`). v2 now also wraps that call: the global is pointed at the country's manager copy for the duration and restored in `__finally`. Expect ~320 list requests/day.
- **Result (2026-10-04, `year_timer`, TGC 1836, max speed):** baseline 196 ms/day (1.11 min) -> v1 per-call hook 203 -> v2 per-country lists **189 ms/day (1.08 min)**, -3.6 %. 323 list requests/day confirm `AI_DecisionUpdate` as the daily loop; the AI sees 945 of 1651 decisions per country (663 out by tag, 26 by ai, 11-15 dead); ~11 rebuilds/day (flag clears) cost 0.26 ms/day. → [raw/profiler/decision-cull-v2-2026-10-04.log](raw/profiler/decision-cull-v2-2026-10-04.log)

## 2026-10-04 — `cull_cb_checks` v3

- **Why:** the 2026-10-02 profile put AI CB scans at 11.1 ms/day with only 4.8 in `can_use`. `IsCBUsable` (`0x0092EEC0`) explains the rest: per type it walks the held-CB list `rel+0x50` and, for `always`/held types, builds two `CEventScope`s before `CanUse` (slot 8). ~100 walks per pair and ~350 k scope constructions a day.
- **v3:** per pair, one scope pair (laid out as `IsCBUsable` does: attacker, `+0x24` -> THIS = defender), `CanUse` only for the attacker's live `always` types (v2 pre-pass) and for the held types found in one walk of the list; first true returns. Same boolean OR, no side effects. `IsCBUsable` is no longer called. → [cb-cull.md](../cull-cb-checks.md)
- **Open:** `year 1836` with `cull_cb_checks = 1` (v3) against 189 ms/day (decision cull on, CB cull off). Default stays `0` until measured.
- **Result (2026-10-04, `year_timer`, TGC 1836, `cull_decision_checks = 0`):** `cull_cb_checks` v3 **185 ms/day (1.07 min)** against the 196 baseline: −11 ms/day, −5.6 %, i.e. essentially the whole 11 ms/day the profile attributed to the AI CB scans. The N×N loop was cheap per iteration but the scope construction and the held-list walks per type were not. Default should move to `1` once the combined run confirms.

## 2026-10-11 — Research consolidated into `docs/re/`

- **What:** the knowledge spread over the maxioten/Victoria2-Reverse-Engineering notes, `docs/raw/addresses.md` and the per-feature constants in the code now lives in `docs/re/symbols/*.csv` (161 functions, 82 globals/vtables/constants, struct offsets, 57 patch sites) and one page per subsystem in `docs/re/systems/`. The other repo is not ours; its notes were rewritten, not copied, and each page credits it.
- **Verified:** every function address in `functions.csv` is a function start in the Ghidra project (`V2Symbols.java check`); the original bytes of all patch sites, including the external V2DLL and "Master EXE Mod" ones, match the 3.04 exe (`docs/tools/scripts/check_patch_sites.sh`). Everything in the other repo targets this same exe (the "v3.05" mod only rewrites the label).
- **Corrections found on the way:** the price clamp constants `0x00E45C30` / `0x00E45C28` hold 6554.1 / 163840.5 (0.2 / 5.0 pre-scaled to 2^15 fixed point), so "write 0.1 / 10.0 there" would break prices; `.text` file offsets are VA − 0x400C00, not VA − 0x400000 (the checksum fix is at file offset 0x1F7668, not 0x1F8268); `EconomyManager_UpdateFull` (`0x0068BF00`) is the one-time rebuild after loading (only caller `0x0066321D`), not the daily economy; the date increment at `0x00682FA5` is unconditional, one day per `Update_World_and_Advance_Time` call.
- **Open:** move the per-feature address constants in `patch/src/features/*.cpp` to one header generated from the CSVs.

## 2026-10-11 — Full decompilation and a performance map

- **What:** names from `docs/re/symbols/` imported into the Ghidra project (backup of the unnamed project kept in `%USERPROFILE%\ghidra_projects_backup_2026-10-11`); `V2DecompAll.java` decompiled all 63 770 functions into `docs/re/decomp/` (git-ignored, 53 MB) with `index.tsv` (size, callers, callees, call depth from `DayTick` / `CInGameIdler_Update`). 2 426 functions are reachable from `DayTick` over direct calls. 6 very large functions (18–47 KB) timed out at 60 s; 5 were redone at 120 s into `docs/re/decomp/large/`, `Render_MapFrame` does not decompile.
- **Read:** `WorldMarketUpdate` calls `UpdateTimeThrottle` every 20 countries and does a temporary copy + `vector_resize` per open factory per day; `FUN_00515490` (now `Country_Recalculate`) resets and rebuilds dozens of country fields, frees and reallocates its vectors on each of its 322 daily calls. The most common call targets in the tick are `std::string` / `operator delete` helpers.
- **Result:** [systems/performance.md](systems/performance.md): budget, rules learned, seven ranked candidates. The largest untouched block is the ~90 ms/day frame side at max speed, where every day pays a full rendered frame.
- **Open:** profiler level 4 (frame) and 5 (market / country) logs; allocation count per day; late-game budget.

## 2026-10-11 — `tick_profiler` never reported since the frame hook

- **Symptom (user):** level 4, a whole year 1836 (202 ms/day by `year_timer`), no profiler table in `v2cp.log`.
- **Cause:** the day check ran only when `world_update` was entered at shadow-stack depth 0. Since `frame_update` (`CInGameIdler_Update` slot) became a level-1 hook, `world_update` always runs inside it, so no level ever reported after that change. The level 2/3 logs in `raw/profiler/` predate it.
- **Fix:** check on the outermost `world_update` (`active == 0`). Also: a target's inclusive time is added only by its outermost instance (`frame_update` re-entered through `UpdateTimeThrottle` inside `world_update` counted twice); frames rendered from inside `world_update` are counted and timed on their own report line; the header gives the game year.

## 2026-10-11 — Level 4: the frame side split

- **Measured (TGC 1836, max speed, both culls off):** a game day is 3–4 frames, ~200 ms. Of the ~90 ms outside the simulation: `CommandQueue_Process` (`0x009AF9F0`) ~51 ms own time, `AI_FrameUpdate` (`0x006874C0`) ~24 ms, `Render_MapFrame` ~14.5 ms, the rest ~5 ms. The throttle renders 1 frame/day from inside the tick (~5 ms/day). → [raw/profiler/level4-2026-10-11.log](raw/profiler/level4-2026-10-11.log), [systems/performance.md](systems/performance.md)
- **Corrections:** the daily AI decision loop runs in `AI_FrameUpdate` on the frame side, not inside `Country_DailyUpdate`; rendering is a small part of max-speed cost, so "fewer frames per day" is a weak idea; the mid-tick throttle frames are not worth removing.
- **Profiler bug:** the first window included menu/loading frames (24 frames/day, negative "outside"). Fixed: the counters are reset when the first game day starts.
- **Open:** `CommandQueue_Process` spends its time in virtual calls the call-redirect profiler cannot see; a sampling profiler is next.

## 2026-10-11 — `sample_profiler`

- **Why:** level 4 left ~51 ms/day inside `CommandQueue_Process` in virtual calls, which call redirection cannot see.
- **Built:** a sampler thread started from the first hourly tick (so on the game thread, outside the loader lock): every N ms while the date advances, suspend the game thread, read `EIP`/`ESP`, copy 4 KB of stack, resume; count `EIP` (per module outside the exe) and stack values that follow a `CALL` (inclusive per call site, upper bound). `v2cp_samples.tsv` every 30 days; `docs/tools/scripts/annotate_samples.sh` maps it to functions through `docs/re/decomp/index.tsv`. → [docs/sample-profiler.md](../profilers/sample-profiler.md)
- **Open:** first run (TGC 1836, both culls off, to compare with level 4); zoom into `CommandQueue_Process`.

## 2026-10-11 — First sampling profile; the exe is relocated by ASLR

- **Run:** TGC 1836–1837 (450 days), max speed, both culls off: 46 088 samples, 201 ms/day sampled (`year_timer` 203). → [raw/profiler/samples-v1-2026-10-11.txt](raw/profiler/samples-v1-2026-10-11.txt)
- **ASLR:** 37 % of the samples mapped to no function because the exe ran at base `0x00080000`, not `0x00400000`: it has a relocation table and Windows relocates it. Found by testing which base makes the sampled return addresses follow a `CALL` in the file (392/400 at `0x00080000`). Hooks were never affected (they all go through `exe_addr(rva)`), but `docs/re/README.md` claimed a fixed base. The profiler now writes listing VAs.
- **Findings (exclusive, reliable):** `ntdll.dll` is **25 % of the day (~51 ms)**; the v1 stacks point at `malloc`/`free`, i.e. the heap. Top exe functions: `Country_Recalculate` 8.1, `CountryRelation_DailyPurge` 5.7, `FUN_00522CF0` 5.6, `CAndTrigger_Evaluate` 4.3, `FUN_0084B0F0` 4.1, `Country_PrePass` 3.7, `__alldiv` 3.4 ms/day. Render DLLs (d3d9, d3dx9, driver) total only ~8 ms/day.
- **Profiler v2:** inclusive figures of v1 were inflated (stale return addresses, recursion counted per call site). v2 validates `CALL rel32` frames against the younger frame, records whole stacks, and the script counts each function once per stack and attributes time in other DLLs to the exe function that called out.
- **Open:** a v2 run to confirm who calls into ntdll and how much of it is the heap.

## 2026-10-11 — Sampling v2: one caller for the ntdll time, but a false stack

- **Run:** TGC 1836–1837 (390 days), max speed, culls off, `engine_worker_threads = 0`: 197 ms/day sampled, `ntdll.dll` 50 ms/day (25 %), confirmed.
- **v2 said:** 46 ms/day of it under `FUN_0099FF90`, a generic token serializer (text/binary writer behind save files, 594 callers), itself under a chain through library code (`0x00A61780`, `0x00A8B360`, `0x00A8B400`, near `PHYSFS_write`) and `std_string_dtor` -> `FUN_0070EC60` -> `CommandQueue_Process`. A string destructor calling a serializer is not a real call chain: stale return addresses after indirect calls, left in uninitialised locals, form chains that pass the `CALL rel32` check.
- **v3:** walks the EBP chain first and treats each `[EBP+4]` as a true frame (Windows DLLs and the game's functions with C++ exception frames keep EBP), filling FPO gaps with the validated scan; names the leaf export inside other modules (`ntdll.dll!RtlAllocateHeap` vs `NtDelayExecution` and so on) from their export tables.
- **Open:** v3 run: which ntdll function the 50 ms are and which exe function really calls it.

## 2026-10-11 — The 50 ms/day in ntdll is a 35 ms network wait (ENet `select`)

- **v3 sampling (EBP-anchored, export names):** `ntdll!ZwWaitForSingleObject` 45.8 ms/day (23.6 % of the day). Instruction-level check of the return addresses: `CALL [select]` at `0x00A8B477` in `enet_socket_wait`, called by `enet_host_service` (`0x00A89D00`; ENet's own error strings), called at `0x00A618CC` by the session's service method (`0x00A61780`), reached from `CommandQueue_Process` (`CALL EAX` at `0x009AFBB1`). The serializer and string-destructor frames in v2's chain were stale.
- **Cause:** the client/server constructors (`0x00A635C0`, `0x00A63830`) set the ENet service timeout (`this+0x20`) to 35 ms, also in single player (local loopback). → [systems/network.md](systems/network.md)
- **Built:** `network_wait_ms` (default -1 = vanilla) caps that timeout by redirecting the one call. **Open:** `year_timer` with `0` vs `-1`; CPU use while paused; multiplayer pacing.

## 2026-10-11 — `network_wait_ms = 0`: −20 to −22 %

- **Measured (TGC 1836, max speed, culls off, sampler on):** first year 56 s / 155 ms/day against 1 min 11–14 s / 195–203 ms/day in the same conditions (three runs). `ntdll` time from ~50 to 5 ms/day; the exclusive times of the simulation functions unchanged. → [systems/network.md](systems/network.md), [../docs/timings.md](../timings.md)
- **Explains** the ~51 ms/day "own time" of `CommandQueue_Process` in the level-4 tick profile.
- **Open:** CPU use while paused with `0`; multiplayer; whether a small cap (1–2 ms) keeps the idle behaviour and most of the gain.

## 2026-10-11 — Everything together: 144 ms/day (−26 %)

- **Measured:** `network_wait_ms = 0` + `cull_cb_checks = 1` + `cull_decision_checks = 1`, `engine_worker_threads = 8`, no profiler: first year 52 s, 144 ms/day, against 196 vanilla. One run.
- **Correction:** the session polled with a 15 ms timeout (logged on the first poll), the base constructor's value; 35 ms is what the client/server variants set. The measured wait (~46 ms/day) is with 15 ms.

## 2026-10-11 — `network_wait_ms = 0` becomes the shipped default

- **Checked (user):** with `0`, CPU use still drops sharply when the game is paused (not to zero): the network wait is not the paused-game idle mechanism. The shipped ini now sets `0`; a missing key still means vanilla (`-1`). Multiplayer remains untested.

## 2026-10-11 — `network_wait_ms` renamed `skip_network_wait`

- **Why (user):** every patch option should read `1` = on. `network_wait_ms = 0` meant "on". The option is now a plain switch, `skip_network_wait = 1` (shipped on), which polls with a timeout of 0; the numeric cap is gone. Entries above keep the old name as the history.

## 2026-10-11 — The next three hot spots are one problem: relation objects; `prefetch_relations`

- **Per-instruction samples (v3 leaf addresses):** `FUN_00522CF0` 91 % right after `CMP [rel+0x34],0` (0x00522E23); `CountryRelation_DailyPurge` 73 % at its first read `[rel+0x50]` and 18 % at `[rel+0xA8]`; `Country_Recalculate` 46 % after `CMP [EDI+0x20],0` and 14 % after `CMP byte [EDI+0x70],0`, with `EDI = self+0xBE8[other]`. All three are daily scans over a country's relation with every other country (683 × 683 separately allocated objects): cache-miss bound, ~20 ms/day together.
- **Purge:** its top level reads only `+0x50 +0xA8 +0x2C +0x30 +0x28 +0x20`; everything else is behind a non-zero test, so with all six zero it does nothing. All three callees are stdcall (`RET 4` / `RET 8` / `RET 4`).
- **Built:** `prefetch_relations` (shipped 0): register-preserving thunks on the three call sites (`0x005089DB`, `0x00487483`, `0x0050AC44`) prefetch the needed lines of every relation of the country before the scan; the purge thunk also skips relations whose six fields are zero. Results unchanged by construction. **Open:** `year_timer` against 144 ms/day (all fixes, same settings); a sampling run to see the stalls move.

## 2026-10-11 — Flag atlases: `compress_flag_cache`

- **Question (user):** caches converted by hand to DDS (kept as `.tga`) cut ~1 GB of RAM in TGC; can the game write and read them compressed by itself?
- **Found:** `FlagAtlas_BuildOrLoad` (`0x007E3900`) builds each per-government atlas as A8R8G8B8 MANAGED, saves it with `D3DXSaveTextureToFileInMemory(D3DXIFF_TGA)` and loads the cache with `D3DXCreateTextureFromFileInMemoryEx(Format UNKNOWN, MANAGED)`: the format comes from the file content, which is why the renamed DDS files worked. TGC caches: 11 × 4096×2048×4 = 32 MB each, all resident as managed textures. → [systems/flags.md](systems/flags.md)
- **Built:** `compress_flag_cache` (shipped 0): the save copy is created as DXT5, saved as DDS, and the cache load asks for DXT5 (old caches compressed on load). The individual-flag loads are untouched. Not tested in game yet.

## 2026-10-11 — `prefetch_relations`: −7 ms/day, shipped on

- **Measured (user):** all fixes + `prefetch_relations = 1`, `engine_worker_threads = 4`, no profiler: first year 137 ms/day (0.49 min active) against 144 without it. 196 -> 137 overall (−30 %). One run; the gain is close to the ~5 ms noise but in line with the ~20 ms/day of relation stalls it targets.
- **Shipped default:** `prefetch_relations = 1`.

## 2026-10-11 — `compress_flag_cache` works; switched to DXT1

- **First run (user, TGC):** the patch rebuilt all 11 atlases as DDS/DXT5, 8 388 736 bytes each (was 33 554 463), and the game loaded them. RAM measurement still to report.
- **Alpha check:** every 4×4 block of the DXT5 atlases has alpha 255/255; flags have no alpha (user). Switched to DXT1 (same colour encoding, 4 MB per atlas). The load wrapper skips conversion when the file is already DXT1; DXT5 or uncompressed caches are converted on every start until deleted and rebuilt.

## 2026-10-11 — Why a day takes several frames: the AI job queue; `fast_ai_queue`

- **Read:** `ProcessGameTimeAdvance` issues the next tick only when the AI job queue at `world+0xB84` is empty; `DayTick` fills it (one job per AI country, bucket `country % N`, N = 1 in single player); `AI_FrameUpdate` (`0x006874C0`) drains it at most 20 ms per frame (double 0.02 at `0x00E456C8`). A day = tick frame + AI frames + issue frame, each with its own render/UI. → [systems/tick.md](systems/tick.md)
- **Built:** `fast_ai_queue` (shipped 0): the call at `0x006557C4` goes to a register-preserving thunk that, at max speed only, calls `AI_FrameUpdate` again until the queue is empty (cap 250 ms per frame). Same jobs, same order; integer-only timing (the caller may keep XMM values across the call).
- **Open:** measure frames/day and ms/day with `tick_profiler = 1`, `fast_ai_queue` 0 vs 1.
- **Measured later (2026-10-11):** 133 vs 137 ms/day (one run, see below). If it helps, next step: issue the next tick in the same frame once the queue empties (today the issue waits for the following frame's `ProcessGameTimeAdvance`).

## 2026-10-11 — The market's per-pop loop is pop cache misses; `prefetch_pops`

- **Sampled (v3, 1837, all fixes):** `WorldMarketUpdate` own time is 0.2 ms/day; the 21.6 "exclusive" of the tick profiler was unhooked callees. `Market_UpdateCountryPops` (`0x00485E40`) is 14.8 ms/day inclusive. Hottest instructions are right after the first read of each pop (`0x004853D7`, `0x004869F6`, `0x00486A1C`). The loop walks per-province, per-pop-type linked lists (`province+0x194 + type*16`, next `pop+0x27C`), so each pop is a dependent miss. → [systems/performance.md](systems/performance.md), [systems/economy.md](systems/economy.md)
- **Built:** `prefetch_pops` (shipped 0): thunk on the call at `0x0048468F` walks all the country's lists side by side and prefetches every pop (0x280 bytes) and the `+0xB0`/`+0xE8` lines of `[pop+0x1D4]`. Read-only. **Open:** `year_timer` vs 137 ms/day; sampling run.
- **Not pursued:** the 64-bit helpers `__alldiv`/`__allmul` (~5 ms/day) are spread over dozens of callers, ≤0.5 ms each.

## 2026-10-11 — `fast_ai_queue = 1`: 133 ms/day

- **Measured (user):** all fixes + `prefetch_relations` + `fast_ai_queue = 1`, `engine_worker_threads = 4`, no profiler: first year 0.48 min, **133 ms/day**, against 137 without it. One run; −4 ms is within the ~5 ms noise but in the expected direction (fewer frames per day). Overall 196 -> 133 (−32 %). → [../docs/timings.md](../timings.md)
- **Not in this run:** `prefetch_pops`. The game still loaded the DLL built before it (the `config:` line has no `prefetch_pops` field, no install line). Still to measure.
- **Open:** a second `fast_ai_queue` run (or `tick_profiler = 1` for frames/day) before shipping it on.

## 2026-10-11 — `cull_cb_checks` v3 never installed: wrong prologue check

- **Found in v2cp.log:** `cb_cull: IsAnyCBUsable / CEventScope ctor not recognised, feature off` with `cull_cb_checks = 1`. The install check expected `55 8B EC 6A FF` at `CBManager_IsAnyCBUsable` (`0x0092EDE0`); 3.04 has `55 8B EC 83 E4 F8 64 A1` (aligned-stack SEH frame). The check has been this way since the v3 commit (`ea40f3d`), so the configurations "all" (144), "+ prefetch_relations" (137) and "+ fast_ai_queue" (133/134) ran **without** the CB cull, unless an older DLL was installed at the time.
- **Fixed:** the check now expects `55 8B EC 83 E4 F8` (the function is only called, never patched, so it is a sanity check). The 133 run was also measured a second time: 134 ms/day.
- **Open:** re-measure with the cull really active, and confirm `cb_cull: active` in the log.

## 2026-10-11 — Everything active: 132 ms/day

- **Measured (user):** all patches, now with `cb_cull: active` and `prefetch_pops: active` in the log, `fast_ai_queue = 1`, `engine_worker_threads = 4`, no profiler: first year **132 ms/day** against 133/134 without those two. One run.
- **Reading:** together, the fixed CB cull and `prefetch_pops` gain 1–2 ms/day, below the ~5 ms noise. The CB cull's earlier −11 ms (185 against 196, on an otherwise vanilla game) does not show up here; either that number was noise, or the other patches already removed part of the same time. `prefetch_pops` is unproven: the year timer cannot resolve a few ms.
- **Next:** a `sample_profiler = 1` run with these settings, to compare `Market_UpdateCountryPops` (14.8 ms/day before) and the hot instructions after the first pop reads (`0x004869F6`, `0x004853D7`). That decides `prefetch_pops`, not the year timer.

## 2026-10-11 — The other half of the AI frame job: another relation miss; `prefetch_ai_relations`

- **Sampled (same v3 file):** `AI_CountryFrameJob` 21.9 ms/day = `AI_DecisionUpdate` 10.7 + `FUN_0084B690` 9.7 (named `AI_CountryDailyJob_Update`, vtable slot `0x00E27738`). Inside it, `FUN_0084B0F0` (named `AI_TargetScan`, slot `0x00E2773C`, 5.5 inclusive, 3.9 own, 5th by exclusive time) has **90 % of its samples at one instruction**, `0x0084B29D`, right after `CMP [self+0xBE8[other]+0x34], 0`: the same relation field `FUN_00522CF0` stalls on, read for every country (1..count, with or without provinces) once per AI country per day. `FUN_00826480` (4.1) is spread over its callees, no hot spot.
- **Built:** `prefetch_ai_relations` (shipped 0): the vtable slot points to a thunk that prefetches `rel+0x34` for every country before the scan. Read-only. Separate key so it can be measured; can be folded into `prefetch_relations` once proven. **Open:** measure (a sampling run shows it directly: `AI_TargetScan` own time should drop from ~3.9 ms/day).

## 2026-10-11 — Scan for one-instruction hot spots; two more prefetch sites

- **Method:** for every function with ≥0.8 ms/day exclusive in the v3 file, the share of its samples on its single hottest instruction. A high share right after a load = a cache miss that prefetching can hide. **Caveat:** that file is the 154 ms/day run with both culls and every prefetch off, so trigger and relation costs are overstated against today's settings.
- **Found:** `Country_PrePass` 3.6 ms, 70 % at `0x00508DEE` (after `MOV ECX,[rel+0x2C]`, self's relation with every country); `FUN_004B9C60` 1.3 ms, 89 % after `[pop+0x27C]` (DayTick's per-province pop pass); `FUN_0095EB00` 1.4 ms, 90 % after `[pop+0x11C]` (from ProcessMarketAndGoodsDistribution); `FUN_0095A5F0` 2.1 ms, 46 % on pop `+0x180..+0x21C` writes; `FUN_0052FCD0` 1.9 ms, 48 % after `[other+0x1580][self*8]` (from Country_DailyUpdate); `FUN_009E6C10` 2.2 ms, 67 %: the case-insensitive ternary-trie lookup behind `has_country_flag` (`FUN_008CCB90`, trie at `country+0x1B0`, one node per character; the stall is the node load, not `tolower`), mostly from AI decisions, so largely already cut by `cull_decision_checks`.
- **Built:** `prefetch_relations` now also prefetches `rel+0x2C` (Country_PrePass loop). `prefetch_pops` gets a second site: `CALL FUN_004B9C60` at `0x00685B09` in DayTick (EBX = world, EDI = province index); every 8th province, the pop lists of the next 8 are walked together and prefetched. Conflicts with `tick_profiler` level 2+ on that site (first installer wins, the other logs "skipped").
- **Left for after the next profile:** `FUN_0095EB00`/`FUN_0095A5F0` (market pop loops), `FUN_0052FCD0`, the flag trie. Re-rank with a sampling run at current settings first.

## 2026-10-11 — Feasibility of evaluating decision triggers on worker threads

- **Read:** `AI_DecisionUpdate` sends a chosen decision as a session command (executed later by `CommandQueue_Process`), so the loop does not change the game while it runs. Its roll is CRT `rand()` (`0x00AB047B`, per-thread state), not the MT. → [systems/parallel-triggers.md](systems/parallel-triggers.md)
- **Scanned:** new `docs/tools/ghidra/V2WriteScan.java`, from the 235 `Evaluate` functions + `CDecision` slot 6 (`0x00A31490`), direct calls to depth 8 (688 functions): all 63 global addresses written are lazy singletons, same-value stores, CRT internals or crash paths; the only MT draw is in the pop constructor behind the lazy `CNullPop`. Virtual calls and writes through pointers not covered. → [raw/trigger-writescan-2026-10-11.txt](raw/trigger-writescan-2026-10-11.txt)
- **Prize:** ~5–6 ms/day of `potential`/`allow` left after `cull_decision_checks` in 1836 (~4 ms on 4 threads); late game unknown.
- **Plan:** shadow mode first (parallel precompute + serial compare, mismatches logged), real mode only after zero mismatches. Not built.

## 2026-10-11 — `parallel_decisions`: first multi-threaded feature (untested)

- **Read for the design:** `CDecision` slot 6 is `MOV AL,1 / RET`; the loop's scope is `CEventScope::ctor(scope, 0x2D)` + `{tag, index}` from AI object `+0x30`; the AI job queue (`world+0xB84`) holds nodes `{tag, country, prev, next}` in buckets `{head, tail, count, flag}`, only for countries with provinces; `AI_FrameUpdate` runs a job when `country+0x208` (AI object) is set.
- **Built:** worker pool (`patch/src/parallel/`), batch at the start of the max-speed AI frame (driven by `fast_ai_queue`), filtered lists handed to `AI_DecisionUpdate` through the cull's fake manager; shadow mode (`= 2`) compares instead. `fast_ai_queue`'s thunk now saves FPU/SSE state with `fxsave`. → [systems/parallel-triggers.md](systems/parallel-triggers.md)
- **Open:** shadow run (mismatches, batch ms/day), then `= 1` against 132 ms/day.

## 2026-10-11 — Parallel work: events built, Recalculate and market rejected

- **`parallel_events` built (untested):** the daily event scan's triggers evaluated in chunks on the worker pool in scan order; `Event_Fire` drops the results computed before it; MTTH, the MT draw and firing stay serial. Shadow mode `= 2`. Sites `0x006861E8` (shared with `tick_profiler`), `0x008A7EF4`, `0x008A80D5`. → [systems/parallel-triggers.md](systems/parallel-triggers.md)
- **Rejected after reading:** `Country_Recalculate` creates, unlinks and deletes objects in world lists and writes shared relation objects; the world market draws from the MT per state, shares static scratch buffers across countries and pops, can render a frame mid-loop (`UpdateTimeThrottle`) and clamps its sums. Neither can run in parallel with identical results.

## 2026-10-11 — First run with the parallel features: 121 ms/day

- **Measured (user):** all patches, `parallel_decisions = 1`, `parallel_events = 1`, `prefetch_ai_relations = 1`, `engine_worker_threads = 4`, pool of 7 workers (24 logical processors): first year **121 ms/day** against 132. No crash. One run, straight to `= 1` without a shadow run.
- **Decisions:** 30 batches per 30 days (one per day, as designed with `fast_ai_queue`), ~320 countries and ~302 000 checks per batch, **1.5 ms/day on the game thread** for the whole batch; ~48 decisions pass `potential` + `allow` per day; every AI update was served from the batch.
- **Events:** ~5 000 triggers/day listed, all served, 0 passed through (the sequence always matched); 0.45 ms/day in batches. Many chunks early (397 events fired in the first 30 days, start-of-game events), ~175 per 30 days later.
- **Open:** a shadow run (`= 2` for both) to confirm zero mismatches; the gain (−11 ms) is more than the expected ~7 ms, so part may be noise or the other patches enabled in this run (`prefetch_ai_relations` was not in the 132 run).

## 2026-10-11 — Shadow run: decisions clean, events had a scope bug

- **`parallel_decisions = 2`:** 135 million checks compared over 450 days, **0 mismatches**. Validated.
- **`parallel_events = 2`:** ~0.7 % mismatches, all `serial 1, parallel 0`, mostly the same few triggers. Cause: `CEventScope::ctor` sets `scope+0x24` (THIS scope) to the scope itself; the worker copied the scan's live scope, so `+0x24` still pointed at the scan's stack copy and triggers through THIS read the wrong country. `parallel_decisions` builds its scope with the ctor, so it was not affected.
- **Consequence:** the 121 ms/day run (`parallel_events = 1`) skipped events that should have fired; that number is not valid.
- **Fixed:** fields of the template pointing inside the live scope are relocated into each copy. **Open:** a new shadow run for events.

## 2026-10-11 — Shadow run after the scope fix: both clean

- **`parallel_events = 2`:** ~2.9 million triggers compared over 540 days, **0 mismatches** (was ~0.7 % before the relocation fix). Batches ~0.5 ms/day; 405 events fired in the first 30 days, 16–38 per 30 days later.
- **`parallel_decisions = 2`:** 162 million checks, **0 mismatches**.
- Both validated in 1836–1837. **Open:** `= 1` for both, `year_timer` against 132 ms/day; a late-game shadow run.

## 2026-10-11 — Validated parallel features: 124 ms/day

- **Measured (user):** all patches, `parallel_decisions = 1` and `parallel_events = 1` (both 0 mismatches in shadow), `prefetch_ai_relations = 1`, `engine_worker_threads = 4`, 7 pool workers: first year **124 ms/day** against 132 (−8 ms, −6 %); 196 -> 124 overall (−37 %). One run. Every event trigger was served from the batches (0 passed through); decision batch ~1.5 ms/day, event batches ~0.5 ms/day on the game thread.
- The −8 ms includes `prefetch_ai_relations` (not in the 132 run); separating them needs a run with `parallel_* = 0`.

## 2026-10-11 — Option 2 (skip dead tags) checked: mostly a dead end; allocator swap sized

- **Relation readers ranked** (720 reads of `country+0xBE8` in the exe, joined with sampled exclusive time): the five known scans hold nearly all of it. `Country_Recalculate`, `FUN_00522CF0` (walks `world+0xADC`, not all tags) and the purge loop **already** test the other country's provinces before reading the relation. `Country_PrePass`'s loop (`0x00508DA7`) and `AI_TargetScan` do not, but their bodies are data-dependent and write other countries (PrePass: relation `+0x2C`/`+0x30` objects -> money into `other+0xE78`; target scan: `+0x34`/`+0x3C` -> target lists). Skipping a dead tag cannot be proven a no-op, and testing its relation costs the same miss.
- **Built:** `prefetch_relations` also prefetches `+0x2C` for the tags without provinces that the PrePass loop visits (they were the remaining unprefetched misses there). Read-only.
- **Allocator (option 5) sized:** VS2010 static CRT, one private heap (`__heap_init` `0x00ABABD8`); entry points `_malloc` `0x00AB049C`, `_free` `0x00AAEED7`, `_realloc` `0x00AC4320`, `__calloc_impl` `0x00ABF700`, `__msize` `0x00AB71DB`, `operator new/delete` `0x00AAE9AF`/`0x00AAE91B`. Sampled ntdll time called directly from `_malloc`/`_free`: **~1.7 ms/day** (all ntdll 5.2, part of it waits). Windows enables the Low Fragmentation Heap by default, so same-size objects are already grouped by size class. A mimalloc swap would save ≤ 2–3 ms/day of call overhead with an uncertain locality gain: not worth its risk now.

## 2026-10-11 — The max-speed 20 fps is `kSpeedThrottle[4]`; option 3 closed

- **Found:** at speed 4 the game is inside the tick almost all the time, and `UpdateTimeThrottle` draws a frame at most every `kSpeedThrottle[4]` = 0.06 s (× scale × (1 + time factor)): ~16–20 fps by design. The table is read only there. → [systems/game-speed.md](systems/game-speed.md)
- **Decision (user):** no change in either direction (fewer frames for speed, or more frames for smoothness). Option 3 closed.

## 2026-10-11 — Remaining hot spots of the old profile: two more prefetch sites

- **Reviewed:** `FUN_0095A5F0` and `has_country_flag` are inside loops already covered (`prefetch_pops` market site; decisions via the cull and `parallel_decisions`). `FUN_0068F4C0` (~4 ms/day on the game thread, called once a day from DayTick) is one of the game's own TBB `parallel_for`s (`ProcessPopStatisticsFunctor`, running `Economy_CalculateDistribution` -> `Economy_AccumulateElement`): already parallel; its main-thread share depends on `engine_worker_threads` (4 of 24 logical processors here) — a config test, no code.
- **Built:** `prefetch_relations` 4th site: `CALL FUN_0052FCD0` at `0x0050B6B9` (once per country from `Country_DailyUpdate`), which reads `other+0x1580[self]` for every living country: two-pass prefetch (array pointer lines, then elements). `prefetch_pops` 3rd site: `CALL FUN_0095EB00` at `0x00481860` in `ProcessMarketAndGoodsDistribution`'s inline pop walk (no per-country call to batch on): the next pop (EAX) is prefetched while the current one is processed. Expected ~1–2 ms/day together; not measured.

## 2026-10-11 — Crash in our sampler (late game), fixed

- **Crash:** access violation writing `9BEBBBE8`, on the `sample_profiler` thread (ESP outside the game stack; listing VAs on the stack; ESI/EDI = game ESP and ESP + 16 KB). Cause: the EBP-chain walk bounded `e + 8 <= esp + bytes`; a chain value near `0xFFFFFFFF` (EDX = FFFFFFFF) made `e + 8` wrap, passed the bound, and `anchor[(e + 4 - esp) / 4]` (index ~0x3FAC1CA6, = EAX/ECX in the dump) wrote far outside the array. Seen at day 120 of a 1848 run; never in 1836 runs. The game itself was not at fault.
- **Fixed:** overflow-free bound `e >= esp && e - esp <= bytes - 8`. `crash_log` now names the module and offset of the faulting address (exe addresses as listing VAs) and whether it was the game thread.
- The partial late-game profile (1848, 120 days, ~133 ms/day sampled) was kept for analysis.

## 2026-10-11 — Mid-game profile (1848–1855)

- **Measured (user):** `year_timer` 132–134 ms/day for 1849–1854 with `sample_profiler = 1` (≈ +1–2 %), all patches and both parallel features on; 124 in 1836 without the profiler. No crash in ~2500 days with the sampler fix.
- **Profile** (2490 days, 128.7 ms/day sampled): `DayTick` ~79 (economy ~29, `Country_DailyUpdate` ~20, `Country_PrePass` ~17, `Country_Recalculate` 4.5 own, still the largest exe function), AI frame ~9.5 (was ~22 in 1836 before parallel decisions), `UI_GetState` ~11 under `ProcessGameTimeAdvance` (new), map render ~8, **our DLL 6.5 (5 %)**, `__alldiv` 4.2 + `__allmul` 2.0 own.
- **Next:** own-overhead split (in next-steps.md); a faster `__alldiv` (exact, so identical results); `UI_GetState`.

## 2026-10-11 — `__alldiv` shortcut: exact but useless; `UI_GetState` was a stack artefact

- **`__alldiv`:** 61 % of its samples are right after its two `DIV`s (32-bit divisor path); the slow path is negligible. A shortcut (one `DIV` when `a_hi < b`) was written and verified exact against the CRT routine on ~20 M divisions (edge cases included), but a micro-benchmark on this CPU gave 6.70 vs 6.60 ns per division: modern dividers terminate early, so the first `DIV` of a small dividend is nearly free. The ~4 ms/day are ~630 k divisions a day, not slow ones. Removed; not worth a patch.
- **`UI_GetState` (11 ms/day "inclusive")** is a tiny getter; the samples under it are `AI_FrameUpdate` (9.15) and triggers, reached in reality from `CInGameIdler_Update`. A stale return address of `ProcessGameTimeAdvance`/`UI_GetState` left on the stack formed a false chain. No new cost.

## 2026-10-11 — Own overhead: measurement tooling and a first slimming

- **Tooling:** `sample_profiler` writes leaves inside `community_patch.dll` as `cp+0x<RVA>` (was "nearest export", i.e. one bucket for the whole DLL); the build emits `out/x86-release/bin/community_patch.map` (`/MAP`); `annotate_samples.sh` names those leaves from the map (`CP_MAP=` to use another map, e.g. the one of the DLL that produced the samples).
- **Slimmed:** the relation prefetch loops (Recalculate, market, purge, dead-tag `+0x2C`, AI target scan) tested provinces of all 683 tags on every call, ~1.3 M times a day in mid game. They now iterate per-day lists of country indices (live / dead / all), rebuilt when the date or the tag count changes. Same prefetch targets except for countries that change status during the day (harmless: prefetching changes no result).
- **Open:** a mid-game run with the sampler to see the remaining split by function.

## 2026-10-11 — Shipped defaults updated

- The repo ini now ships on: `cull_cb_checks` (prologue check fixed, active), `prefetch_pops`, `prefetch_ai_relations`, `fast_ai_queue`, `parallel_decisions` and `parallel_events` (0 mismatches in shadow mode), `compress_flag_cache` (atlases rebuilt and loaded as DXT1). Profilers stay off. README defaults match.

## 2026-10-11 — Mid/late-game run (1855–1878): own overhead split

- **Measured (user):** `year_timer` 140–169 ms/day for 1855–1878 (sampler on), rising with the game; no crash in ~8900 days.
- **Our DLL, by function** (new `cp+` leaves + map): 5.8 ms/day, of which `before_purge` 2.6 — 61 % of its samples right after `CMP [rel+0x50],0` and 27 % after `[rel+0xA8]`: the relation cache misses of the daily purge, now taken in our pre-check instead of in `CountryRelation_DailyPurge` (which left the top list; ~5.6 ms in 1836). That is game work moved, not overhead. The rest is ~2.4 ms: `prefetch_list` 0.73 (all relation prefetch loops, after the per-day lists), `walk` 0.55, `purge_thunk` 0.33, `before_pair` 0.28, the pop batches ~0.36, `before_ai_scan` 0.14.
- **`ntdll!NtWaitForAlertByThreadId` 2.2 ms/day** is the display driver waiting inside Direct3D calls (`FUN_009F9D50` device render states, `FUN_0099A200` under `Render_MapFrame`), not our worker pool.
- **Sampler limit:** the stack table filled (98 k distinct stacks at 3/4 of 2^17 slots) and 30 % of the samples were dropped, biased towards the late years, so block totals cannot be compared with the 1848–1855 run. Table raised to 2^19 slots and the pool to 48 MB.

## 2026-10-11 — Late game (1879–1892)

- **Measured (user):** `year_timer` 129–154 ms/day for 1879–1892, faster than 1855–1878 (146–169): ~158 AI countries left in the queue instead of ~320 (decision batch 140 k checks instead of 300 k). Complete profile this time (171 k stacks, 0 dropped).
- **Against 1848–1855:** `DayTick` flat (~78 ms/day). Grew: `AI_CountryDailyJob_Update` 8.3 -> 12.3 (military AI: `FUN_00826480` -> `FUN_00828030` -> `FUN_00828060` -> `FUN_00828550` -> `FUN_0082C1D0` -> `FUN_0082AEA0` -> ..., spread over a deep tree with no function above ~1 ms own; the apparent second call of `FUN_00828060` from `FUN_0082D0F0` is a stale frame) and `Render_MapFrame` 8 -> 12.3. No low-hanging fruit: the growth is game logic scaling with armies and map content.

## 2026-10-11 — Names: The Grand Loader and the Victoria II Performance Patch

- The loader is now **The Grand Loader** (API prefix `TGL_`: `TGL_PluginInit`, `TGL_HostApi`, `include/tgl/plugin_api.h`, CMake `tgl::plugin_api`; log `the_grand_loader.log`; version label suffix " - The Grand Loader"). The plugin is the **Victoria II Performance Patch**: `mod/performance_patch.ini`, `mod/performance_patch/performance_patch.dll`, outputs `performance_patch_samples.tsv` / `performance_patch_decisions.log`, link map `performance_patch.map`. Release zips: `the-grand-loader-<v>.zip`, `the-grand-loader-sdk-<v>.zip`, `performance-patch-<v>.zip`. Entries above keep the old names (`v2cp.log`, `community_patch`) as the history.

## 2026-10-12 — Country_DailyUpdate and Country_PrePass opened (late-game profile)

- **Country_PrePass (15.8 ms/day):** its own time is still 70 % at `0x00508DEE` (relation `+0x2C`) and 12 % at `0x00508EB7` (`+0x30`) despite the relation prefetch: that prefetch runs before `Country_Recalculate`, whose call (`0x005089DB`) is skipped for tags without provinces, while the loop at `0x00508DA7` runs for all 683 tags. In the late game ~525 dead tags scan 683 relations each, unprefetched.
- **FUN_0051FD10 (2.5 ms/day):** called for every tag at the start of Country_PrePass (`0x0050861B`, stdcall 1); resets `country+0x1514` (goods × 0x18 bytes) and the matrix `country+0x14F4` (types × goods × 0x18) to the pair at `DAT_013F3688/C`, ~30 KB per tag per day; samples on the stores.
- **Pop walks:** `FUN_0050B9B0` (Country_PrePass; stall after `[pop+0x27C]` at `0x0050C089`) and Country_DailyUpdate's monthly pop pass (`0x005093C6`, after `[pop+0x27C]`; it reads every pop daily for `id % month == day`).
- **Built:** `country_prepass` thunk on `0x0050861B`: for tags without provinces, prefetch relation `+0x2C/+0x30` with every tag (with `prefetch_relations`); for tags with provinces, prefetch their pops (with `prefetch_pops`); and **`skip_clean_resets`**: skip FUN_0051FD10 when both tables already have the right sizes and hold the reset pair everywhere (then the call writes nothing new). `prefetch_pops` 4th site: `CALL Country_DailyUpdate` at `0x00685ED1`, country pop walk. Same results by construction; not measured yet.

## 2026-10-12 — First run with country_prepass: 125 ms/day in 1836; the tables are always clean

- **Measured (user):** all patches including the new ones, 1836: **125 ms/day** (best so far 124; within noise, as expected: the dead-tag share and the relation-prefetch gap only grow later).
- **`skip_clean_resets`: 100 % of the resets skipped** (245 880 of 245 880 in the first year), alive tags included: at the time FUN_0051FD10 runs, both tables always already hold the reset pair, so the vanilla call rewrote identical values for every tag every day. The check reads exactly the memory the call would write (sizes, row pointers, value pair, entry count, re-checked against the decompilation), so skipping stays exact. Because everything is clean, the check reads the whole ~30 KB per tag (~20 MB/day): it saves the write traffic (line fill + write-back), not the scan.
- The tables are read through row-pointer getters (`FUN_005394D0` and the `0x0053xxxx` accessors, callers `FUN_006E7D70`) and a loop inside Country_PrePass (`0x005085xx`); writes through those pointers cannot be ruled out statically, so the scan cannot be dropped.
- **Open:** late-game measurement (same 1879 save) against 129–154 ms/day.

## 2026-10-12 — Effect parsing pipeline (groundwork for new script effects)

- **Dispatch:** `CPersistent::ParseBlock` (`0x009A2A90`, vtable slot 3 of every script object) reads `key = value` pairs and calls slot 4 (`+0x10`) with `(ctx, keyId)`, where `keyId` is the key token's type at `ctx+0x20` (text at `ctx+0x24`; value token type `ctx+0x228`, text `ctx+0x22C`, each token 0x41 dwords). For effects slot 4 is `CEffect::ParseEffect` (`0x00876B30`); the few effects that override slot 4 (`0x00886DB0`, `0x00888B20`, `0x0088E900`) handle one key themselves and forward everything else to it, so a hook on its entry sees every effect key.
- **Unknown keys:** `ParseEffect`'s default branch tries the key as country tag, province, region, pop type, culture, ... (scope shorthands); if nothing matches and the value is not `{`, it logs an error and adds nothing. A new effect can be added by catching its key text at the entry of `ParseEffect` before that branch.
- **Effect vtable (`CEffect` `0x00DFA6EC`, 15 slots):** 0 dtor, 3 `ParseBlock`, 4 `ParseEffect`, 7 `ParseValue` (token by value, `ret 0x104`), 8 child-list description (`0x0087A6C0`), 9 own description (base `0x0088FC70` returns `""`; `CGovernmentEffect` `0x00880370` builds the localised line; returns `std::string` through a hidden pointer), 11 `Execute(scope, arg2)` (base `0x0087A690` executes the children). Every concrete effect seen so far keeps slots 1, 2, 5, 6, 10, 12 from the base.
- Simple effects are built inline: `operator new(size)` (`0x00AAE9AF`), base fields `{vtable, 0x18D, child list head/tail/count, byte 0, keyId at +0x18, word 0}`, subclass data from `+0x20`; then appended to the parent's child list (`0x10`-byte node `{effect, prev, next, 0}`), then slot 7 (or slot 3 for block effects) reads the value, then `keyId` is stored.

## 2026-10-13 — Multiplayer: OOS detection, built-in RNG realign, join in progress

- **What OOS compares:** `CHourlyTickCommand` carries the host's `world+0xB74` vector = `[g_rngCalls, seed]` (ctor `FUN_0072EC40` at `0x00682CDF`); `Update_World_and_Advance_Time` compares it with the local one (`Vector_Equals` at `0x00683036`). It is a draw counter on the global MT, not a state hash: divergence that keeps the count is invisible. Every global-MT draw is counted, including the inlined loops (`INC [0x00F20C14]` in `DayTick`, `EconomyManager_PrepareUpdate`, `EventManager_DailyScan`).
- **On mismatch:** two `Checksum:` log lines per element, `Rng_Resync` (`0x009B7C80`: reseed and advance `calls` times, correct because all draws are counted), OOS dialog once (`world+0xBA8`), `CPauseGame`, and the game goes on with the state still diverged.
- **Who runs what:** AI objects (`country+0x208`, `world+0xA4`) are created only on the host (`FUN_00687610`, `FUN_00687A70`, `FUN_006881D0`, `FUN_0068B6C0`, is-host = `sessionMgr+0x34`), so frame AI jobs and the AI's event-option choice (`Event_Fire`, event-manager MT seeded by `__time64`) are host-only and reach clients as commands. `AI_DailyUpdate` (`country+0x364`) runs everywhere in lockstep and executes decisions directly. `world+0xD11` is tutorial mode, not "client".
- **Other MTs:** world MT at `world+0xB4` (index `+0xA74`), seeded by the clock in the ctor or by save token `0x7A4` on load, index reset to 0 both times, not counted, read by `FUN_0085CF10` on every machine: the one confirmed desync-capable source.
- **Join in progress:** on connect the server sends message `0x12` = its setup-command list (`sessionMgr+0xD8`, commands whose slot-12 predicate returns 1) through `session->vt[0x6C](peer)` = `FUN_00A60630`; the client goes to state 6, loads it in `FUN_009B07A0` (`FUN_009B1900`), replays the commands received meanwhile, state 4, "REQUESTING SYNCHED". Same primitive can push the host's state to an in-game peer.
- **D3D9 device created without `D3DCREATE_FPU_PRESERVE`** (`0x40`/`0x20` only): main thread at 24-bit x87 precision, workers at 53-bit. To check on our parallel options.
- New page: `systems/multiplayer.md`.

## 2026-10-13 — Multiplayer options in the Performance Patch: state_checksum, oos_log

- Sources in `patch/src/features/multiplayer/`, offsets in `patch/src/game/world.h`, options in `performance_patch.ini` (Multiplayer section). Docs: `docs/state-checksum.md`, `docs/oos-log.md`.
- `state_checksum`: `CALL DayTick` at `0x00683AB1` redirected to a thunk that swaps the return address (DayTick is `RET 4`) and, after it, pushes 5 hashes into `world+0xB74` with the game's `operator new`/`delete` (`0x00AAE9AF`/`0x00AAE91B`): provinces (owner `+0x12C`, controller `+0x134`), countries (money `+0xE78` int64, province count), pops (id `+0xC`, size `+0x58`, lists `province+0x194..+0x198` step 16, next `+0x27C`), world MT index `+0xA74`, date. Chains if the site is already redirected (tick_profiler).
- `oos_log`: `CALL Vector_Equals` at `0x00683036` redirected to a thunk that logs differing elements by name (EAX = local vector, ECX = host vector) before the original call; logs the x87 control word once.
- Loads and patches cleanly (loader log); not yet exercised in a game. Resync: not implemented, see multiplayer.md open questions (the in-game idler's slot `+0x12C` CSelectBookmark is a stub `0x00A46A60`; bookmarks/saves are handled by the lobby idler only).
- **Math audit (same day):** CRT math functions dispatch per call on `DAT_013F3AB8` (SSE2 available), `MXCSR == 0x1F80` and all x87 exceptions masked; FTZ/DAZ patches (V2DLL) move them to the x87 path. Only 22 tick-reachable functions use x87 arithmetic (mostly UI); CRT `rand()` on the daily path only in the text generator `FUN_0046B3A0`; wall clock only for timing and lazy seeds. New option `pin_fpu`/`fpu_precision` (hourly-tick hook: CW precision + round-nearest + masks, MXCSR default). Open: whether the world MT seed (token `0x7A4`) is ever written to the save.
- **Symbols (same day):** `docs/re/symbols/` gained the session's names: 62 functions (session manager, net session, the session commands, file transfer, idler pause/autosave, in-game lobby, AI daily roll, CRT math dispatchers, `rand`, `operator new/delete`), 5 globals (`g_mtSeed`, `crt_sse2_available`, `g_randomLogging`, `g_eventManager`, `g_timer`), 27 struct offsets (World sync vector, world RNG, AI buckets, embedded AI state; CCommand; SessionManager; NetSession) and the 3 patch sites. Corrected: `World+0xD11` is tutorial mode (was "headless"), `CCountry+0x208` is the AI object pointer, `+0x37C/+0x37D` are the embedded AI state's done/skip flags. Re-import into Ghidra with `V2Symbols.java import`.

## 2026-10-13 — First two-instance multiplayer test: what the logs showed, and three fixes

- **Join hang with `skip_network_wait`:** the joining instance stopped responding (Windows AppHang) in the session's connect loop (`FUN_00A639F0`: waits for a state change with `Sleep(30)` for up to 30 s, no message pump). With `skip_network_wait = 0` the join worked. Not proven to be the cause; next: gate the option on session state 4.
- **The world RNG differs from day one:** the client's first mismatch (day 2) was `world_rng` only (local 0x153, host 0x4A). Confirms `world+0xB4` is clock-seeded per machine. Its readers include lockstep code: `AI_DailyRoll` and `FUN_00843B90` (AI accepts a diplomatic action: `chance <= draw % 100`), the latter called from `Command_CanExecute_A/B/C` (every machine) **and** from `AI_GetActionResultTooltip` (local only). Vanilla AI diplomatic answers drift between players from day one; a tooltip hover moves one machine's generator. Fix `sync_world_rng`: the ten `CALL Rng_Next` sites with `EAX = world+0xB4` go to a dispatcher: inside command execution (`sessionMgr+0x234`, covers the hourly tick) the draw is from `g_mtState` and counted in `g_rngCalls`; outside, from a private MT19937.
- **Vanilla's RNG realign is broken:** the mismatch path does `g_rngCalls = host[0]; g_mtSeed = host[1]; g_rngCalls = 0; Rng_Resync()`, so it restarts the generator from the seed. From the day after the first mismatch the client's `rng_calls` is 0 at every tick and countries/pops diverge within a day (log of 13:01). Fix `fix_rng_resync`: the `CALL Rng_Resync` at `0x0068364D` is redirected; `g_rngCalls` is set to the host's `rng_calls_end` (new `state_checksum` element = `g_rngCalls` after `DayTick`, the host's position when a client compares) or, without it, to the host's day-start count.
- **state_checksum no longer feeds the game's compare:** `sync_check.cpp` shortens both vectors to the two game elements around `Vector_Equals` and compares the rest itself (`oos_log` reports "SILENT DIVERGENCE"); letting the game react to state hashes had triggered the broken realign on the day-2 `world_rng` difference. The `world_rng` element is gone (nothing reads that generator any more).
- **Force OOS button** works (host log: flip then `rng_calls` mismatch by 1). On the client it looked inert because the client had been out of sync since day 2 and the dialog shows once.
- **FPU confirmed:** game thread at `CW 007F` (24-bit) on both instances, `MXCSR 1FB3/1FBB` (exception flags set, FTZ/DAZ off) before `pin_fpu` set `1F80`.
- Loader: a second instance in the same folder now logs to `the_grand_loader.<pid>.log` (the first holds the main log with deny-write). Crash logger: logs C++ exceptions (RTTI type, `what()`, throw site, stack) and `abort()`.

## 2026-10-13 — Corrections from an external byte/Ghidra check of a884da1

Five entries checked against the exe bytes (`check_patch_sites.sh` section math) and the local decompilation; all five changed.

- **`kSpeedAdvance4` (`0x00E45BB8`) is 0.0001, not 0.0004:** bytes `17 B7 D1 38`, loaded by `MOVSS` at `0x00682C59`. Fixed in `globals.csv`, `functions.csv` (`ProcessGameTimeAdvance` note) and `systems/game-speed.md`.
- **`0x00AAF0C5` is an `_atol` thunk, not `rand`:** `FUN_00aaf0c5` is `push ebp; mov ebp,esp; pop ebp; jmp _atol`. CRT `rand` is `0x00AB047B` (`ptd+0x14 = state*0x343FD+0x269EC3`, returns `(state>>16)&0x7FFF`), as `parallel-triggers.md` already said; `AI_DecisionUpdate` calls it at `0x008212FA`/`0x00821453`. Consequence: the math-audit statement above ("CRT `rand()` on the daily path only in the text generator") described the `_atol` thunk. The real `rand` is drawn by `AI_DecisionUpdate`, a host-only frame job (`AI_CountryFrameJob`), and the game never calls `srand`, so it is not a multiplayer desync source; its per-thread state matters only for the parallel-triggers work.
- **`cp_mp_hourly_tick_fpu`:** the `original` column held the text `vtable slot`, which made `check_patch_sites.sh` fail; now `F0 EC 72 00` (76 OK, 2 SKIP).
- **`World+0xB3C` is the ongoing-war list** (was `specialModifiers`, no evidence): the loop at `0x00840FD7` in `AI_GetOpinionScore` feeds each payload to `FUN_00832DD0`, which searches two 8-byte-entry participant vectors (`+0x2C/+0x30`, `+0x3C/+0x40`, entry `+4` = country id) and adds ±50 under `AIREASON_CURRENT_WARS` (`0x00E26BCC`). Still [I]: layout of the war object beyond those vectors is unknown.
- **`0x01258A48` is the issue registry, `g_issueManager`** (was `g_unitManager`, "iterated in the per-country pre-pass": no patch source references it). `FUN_008C4A70` (the `CIssueGroupTrigger` parser: "unknown issue in trigger", "mismatching issue ... for group") loads it at `0x008C4AD4` and resolves the name through the string-hash lookup `FUN_00975ED0`; a miss yields the `CNullIssue` singleton `DAT_01258A40`. Its vectors `+0x18..+0x6C` size per-country arrays in `FUN_00503020`.
- Re-import into Ghidra with `V2Symbols.java import` to propagate the renames.

## 2026-10-14 — Code review of the loader and the patch (four parallel reviews, fixes applied)

- **Loader:** version label patched in one write (no half-patched site); `*.ini` discovery rejects 8.3 short-name matches; `exe_path` grows past MAX_PATH; `GetModuleInformation` failure logged and the plugin skipped; "plugins loaded" counts only those whose init returned 0; a second instance logs to `the_grand_loader.<pid>.log` (earlier).
- **Core:** `exe_base` is an inline global set at init (was two out-of-line calls per use); `hooks::chain_call` replaces three copies of "redirect whatever the site calls now"; `hooks/fpu.h` replaces three copies of `fnstcw/stmxcsr`; `game/world.h` gained the session-manager accessor, player-list and day helpers and the `IntVector` view; config parser is one table (keys, log line); crash logger calls `what()` only on objects catchable as `std::exception`, logs nothing heavy on a stack overflow; IAT hook refuses images without a name table; `logf` checks `vsnprintf`; one-core machines get no worker thread.
- **Multiplayer:** DayTick thunk bounds its depth stack (passes the call through beyond 8); `rng_calls_end` element replaces `world_rng`; `state_checksum` skips hashing with fewer than two players and prefetches the pop walk; `sync_check` keeps up to 64 host elements and logs otherwise; host-wait tracks staleness per player instead of one timeout; the Force OOS hotkey needs the game window in the foreground; dead counters removed.
- **Performance features:** `getter_stub` saves ECX/EDX; decision lists are rebuilt when a country's controller entry (`world+0xAEC`) changes; the parallel-mode "decisions seen" statistic no longer double counts; `cb_cull` refreshes its type table by pointer+count and skips scope construction when nothing can match; the dead-tag relation prefetch is one pass over two offsets; `before_purge` null-checks; `events_parallel` resynchronises its sequence within 256 entries after a skipped candidate and decrements the fire depth under the saved flags; flag cache patches in a safe order and checks every write.
- **Profilers:** the tick profiler no longer installs its return stub for a call it could not record (shadow-stack overflow returned the wrong address); unpatchable sites are logged; failure path resets all state; `calls/day` printed as a real; sample profiler clears per-module exports on reset, reads the world under SEH on the sampler thread, pins sampled modules, logs the 100 ms cap; year timer prints `m:ss`.
- Not done, deliberately: `fxsave`-based register save in the tick profiler thunks (needs a measured run), reordering `pops_prefetch`'s walk to consumer order (needs a measurement), and folding the remaining per-file layout constants of ai_queue/pops_prefetch/profilers into `game/world.h`.

## 2026-10-14 — Multiplayer: the pause before the first tick was the host wait

- **Symptom:** several seconds between the lobby's Play and the first tick on the host. The log had `mp_host_wait: a player is 669775 day(s) behind, holding the next tick` 15 s before the first tick, which is exactly the feature's staleness timeout.
- **Cause:** a player entry in the list at `world+0x18` holds its date (`+0x34`) at year 1 (`day 1825365` = `365 · 5001`; base year −5000, `tick.md`) until that machine's first `CClientPingCommand` arrives, which only happens after a tick. 1836 − year 1 = 669775 days, non-zero, so the `date == 0` guard did not cover it; the host held every tick until the entry went stale.
- **Fix:** `max_lag_days` ignores entries whose lag is negative or above 365 days (a real lag that large is impossible: the game pauses at 11). No change to the game's own lag handling.
- **Follow-up (user decision):** `mp_host_wait_days` is now off by default (clients playing days behind the host is a worse trade in a war than the vanilla lag handling), and when on it suspends itself while a human-controlled country takes part in a war: the thunk walks `world+0xB3C` (nodes `{war, ?, next}`) and both participant vectors of each war (`+0x2C/+0x30`, `+0x3C/+0x40`, 8-byte entries, index at `+4`) against `world+0xAEC`. Layout from `AI_GetOpinionScore`/`FUN_00832DD0` (2026-10-13 entry), now also in `game/world.h`.
- **Force OOS "worked once":** the dialog and the `CPauseGame` sit inside the `world+0xBA8 == 0` block of the mismatch path (`0x00682EC0`, decomp lines around `0xBA8` set to 1 after it), and the game never clears the flag, so later mismatches on that machine are silent (game log and `oos_log` only). The button now clears the flag before the flip. The two-instance log also confirmed that a press affects only the pressing machine (the command carries the unflipped copy), and that with `fix_rng_resync` the forced mismatch lasts one day: `rng_calls` differed by exactly 1 and matched again the next day.

## 2026-10-15 — V2DLL's stability items re-checked; three adopted, three rejected, one dropped

Each candidate was re-read in the decompilation and the exe bytes, and implemented from that reading (no code taken from V2DLL). New ini section "Crash prevention" and one performance option.

- **`fix_supply_crash`** (`0x004D15EB`, `FUN_004D1560`, now `Economy_FactoryWalk`): `MOV EBX,[type+0x12C]` then `MOV EAX,[EBX+0x128]` with no null test; the source-dependent part ends at `0x004D1665`, where the stack is back at its level (the three callees clean their arguments) and nothing from the skipped part is read later (`[ESP+0x44]` is dead). Guard: replay or skip.
- **`fix_tech_window_crash`** (`FUN_007A9070` at `0x007A918A`, `FUN_007ADB70` at `0x007ADE98`): the chain `invention+0x430 -> +0x310 -> +0x40`. The comparator's epilogue at `0x007A91A2` takes AL; the guard returns false (equal) through it. The folder builder gets an empty name.
- **Dropped, graph point clamp** (`FUN_009E0EF0` at `0x009E0FD6`): the frame holds the centre, the first point and 152 more 16-byte vertices (`-0x9B0 .. -0x4`), and the slices of a chart add to 100 steps, so an overflow needs corrupt chart data; V2DLL ships it off. Implemented, then removed by decision (user): no evidence it is ever hit. The function stays named `PieChart_Render`.
- **`background_sleep_ms`** (`0x009DF684`, `0x009DF2D5`): V2DLL describes these as "the client's Sleep(100)". They are the device-lost branch: both frame loops sleep 100 ms while `renderDevice+0x6BCA0` is clear, and the writers are: cleared when `Present` (device `vt+0x44`) returns `D3DERR_DEVICELOST` (`FUN_005F8470`, `FUN_006BA220`, `FUN_0099A520`), set at device creation (`FUN_0099BF60`) and when `TestCooperativeLevel` (`vt+0xC`) returns `D3D_OK` again. So a fullscreen game in the background runs at 10 fps, a windowed one is unaffected. The immediate is set to 1 ms by default. `globals.csv` corrected (was "has-focus byte").
- **Rejected, `MP_CLIENT_SLEEP` (`0x00B1DD2C`):** the 40 ms sleep is in `FUN_00B1DC90`, the pump thread of the hidden `WinNetwork` message window created by `MCoreNetwork_Init` (`0x00B1DD40`): the lobby/metaserver socket layer, not the ENet session that carries the game. Latency there does not touch the simulation.
- **Rejected, timer resolution:** `Enet_WinsockInit` (`0x00A8B190`) calls `timeBeginPeriod(1)` after `WSAStartup(1.1)`, with a version test that passes on every Windows (the returned `wVersion` is the requested `0x0101`). V2DLL's claim that the game never raises the resolution is wrong.
- **Rejected, "SFX mixer `select`" (`0x00A8B47D`):** that address is inside `enet_socket_wait` (`0x00A8B400`), the network wait `skip_network_wait` already removes; there is no mixer there.
- New helper `hooks::jump_hook` (JMP rel32 + NOP padding over whole instructions, after a byte check). `check_patch_sites.sh`: all new rows OK.

## 2026-10-15 — Unknown modifier in an effect: where the crash is, and `fix_missing_modifier`

- **Symptom (user):** an `add_country_modifier` with a name that does not exist crashes the game.
- **Found:** the two parse functions (`AddModifierEffect_ParseStatement` `0x00881260`, `RemoveModifierEffect_ParseName` `0x008820D0`) look the name up with `ModifierRegistry_Find` (`0x005661B0`, hash walk over `g_eventModifiers` `0x0125A69C`), log "Unknown modifier-type" to the game's log and store the null. The four execute functions are null-safe (the add ones return, the remove ones search and find nothing). The four tooltip builders (vt `+0x24`: `0x008814E0`, `0x00881B40`, `0x008822C0`, `0x008826B0`) call `modifier->vt[0x18]` (and `+0x1C`) without a test: the crash is when the event or decision window shows the effect, not when it runs. Vtables found by scanning `.rdata` for the function addresses: `0x00E2ABA8` / `0x00E2ABE0` / `0x00E2AC18` / `0x00E2AC50`, 14 slots each, `+0x2C` execute as in `CEffectBlock`, `+0x30` the persist stub.
- **Conventions (Ghidra `disasm`):** tooltip `__thiscall(effect, out, scope, std::string by value at `[ESP+0xC..0x27]`, arg)`, `RET 0x28`, returns `out`; its own empty path at `0x00882320` initialises `out` (`cap 15, size 0, buf[0] = 0`) and frees the by-value string's buffer when `cap >= 16`. Execute `RET 8`. `ModifierRegistry_Find`: registry in EAX, `std::string*` on the stack, `RET 4`; call sites `0x00881337` and `0x0088217A`.
- **Fix:** slot replacements on the eight vtable entries (empty tooltip; execute logged and skipped) and two call redirects that log the unknown name (first 50). `check_patch_sites.sh`: all rows OK. The trigger parsers (`has_country_modifier` `0x008CD650`, `has_province_modifier` `0x008CDE00`) store a null too, but their evaluation only compares pointers: harmless.
- Symbols: 13 functions, 6 globals/vtables, 6 struct rows (effect `+0x20/+0x24`, country modifier lists `+0x4D8/+0x4E8`, province `+0xC8`).

## 2026-10-15 — Crash with uncolonised provinces under armies: `fix_missing_state_crash`

- **Log (user, zombie mod that uncolonises provinces):** `access violation reading 0000000C at 0x0048E126`, game thread, stack `0x0048F086 -> 0x00851C72 -> 0x0084B520 (AI_TargetScan) -> 0x0084B7B9 (AI_CountryDailyJob_Update) -> 0x0081EA4C (AI_CountryFrameJob)`.
- **Chain:** `AI_UnitTargetScan` (`0x00851BD0`) walks the AI country's units; for each, `province[unit+0xDC]+0xE8` (the state) goes to `State_HasHostilePresence` (`0x0048F070`) at `0x00851C6D` with no null test (bytes `8B 80 E8 00 00 00 6A 01 50 ... E8`), which calls `State_GetControllerTag` (`0x0048E120`, `CMP [ECX+0xC],0` first). Other callers (`0x00851D2x`, `0x00828xxx`, `0x00850B20`) test `+0xE8 != 0` before calling; this one relies on units never standing in an ownerless province.
- **Fix:** prologue guards on both helpers (null -> 0 / `---`), first 20 hits logged. Rows added for the two sites, three functions and the province/state fields.
- **Second crash, same session:** after the helper guard answered, `0x00851F51` in the same scan read `[state+0x14]` (the state's unit list) of another unit's province with only `CMP EAX,local_34; JZ` before it. The sibling `FUN_00853B30` (unit scoring, from `FUN_00851AD0`) has the same five bytes at `0x00853BEF`. Both get a cave to the not-found branch. The other 19 `+0xE8` reads in the scan are either tested, compared only, or feed the guarded helper; `FUN_00853C80` compares only.

## 2026-10-16 — Every call of `Rng_Next` classified: eleven world-generator draws were missing

- **Method:** all 40 direct `CALL/JMP Rng_Next` (`0x009B7610`) in `.text`, each classified by the instructions that load EAX (Ghidra `before`), instead of the single byte pattern `A1 E8 88 25 01 05 B4 00 00 00 E8` used on 2026-10-13.
- **World generator (`world+0xB4`): 20 calls**, not 10. The eleven new ones load it as `MOV EAX,[g_world]; <other>; ADD EAX,0xB4` (`0x0083DE11`, `0x0084D856`, `0x00853F74`, `0x00856971`, `0x008595C4`), `MOV ESI,[g_world]; LEA EAX,[ESI+0xB4]` (`0x008387C2`, `0x0085599E`, `0x008569CF`) or `MOV EAX,ESI` / `MOV EAX,[ESP+0x20]` after `ESI = world+0xB4` (`0x0083B4B3`, `0x0083B4C4`, `0x0083B5CD`). All in AI code reached from the daily AI jobs (`AI_TargetScan`, `FUN_0084A5C0`, `FUN_00833930`, `FUN_00833A40`, two virtuals); `FUN_008386E0` and `FUN_0083B3D0` already had one redirected site and kept the others on the world generator. `sync_world_rng` now redirects all 20.
- **The old "tenth site" `0x008687EA`** is `JMP Rng_Next` at the end of `0x008687E0`, the Lua binding `GetAIRand` registered by `FUN_0086A730` (luabind `function_registration<int(*)()>`). The game's Lua is `defines.lua` and `script/autoexec.lua` only, and no mod file calls it: unused, dropped from the list (it was logged as "not recognised" at every start).
- **Registers:** `Rng_Next` uses EAX/ECX/EDX only (pushes ESI/EDI) and no SSE register; with link-time code generation callers rely on it: `0x0084D866` converts the XMM0 computed before the call. The thunk now saves XMM0-7 (and ECX/EDX) around the C dispatcher, whose private-generator refill may use SSE.
- **Second global generator `0x00F0E040`** (index `0x00F0EA00`): seeded with the constant 1234 (`MOV EAX,0x4D2; MOV ECX,0xF0E040; CALL FUN_009B7670`) and index reset at every `EconomyManager_PrepareUpdate`, drawn by `FUN_009599A0` (from the economy update and `Pop_MonthlyUpdate`) in lockstep order. Same on every machine, not an OOS source. `FUN_009B7670` = MT seed (EAX = seed, ECX = state; EAX 0 = copy a cached default state).
- Others: `Event_Fire` `0x008A82EA` (`[arg]+0x40`, host-only AI option choice), `FUN_007810A0` (state in EDI, already listed as AI/unit), `0x009ED2A2/0x009ED40F` (GUI object `+0x160`), `0x00A2D6D3`, `0x00A771D0/0x00A77F29/0x00A780C7` (object `+0x1BC`, next to CRT `rand`): local presentation code. The global `g_mtState` callers are the counted wrappers.
- **Diagnostic:** each site's first draw inside a command is logged ("drew inside a command ... lockstep on this machine"). A site in the host's log and not in a client's would be host-only code drawing in lockstep, the one case the dispatcher's rule gets wrong.

## 2026-10-17 — `parallel_events` off by default after a player report; served results limited to the scan's own calls

- **Report:** "the new TGL performance mod broke simultaneous events. Set parallel_events = 0, and everything works." Read as same-day event chains (one event's effects read by a later event's trigger in the same scan). Not reproduced; no log.
- **Checked by reading:** between two trigger calls the scan does only `Event_ComputeMTTHProbability` (`0x008AE380`, pure: modifier evaluations and arithmetic), one counted draw, the fire-only-once registry lookup and `Event_Fire` through the two hooked sites, so drop-on-fire covers every state change the scan itself makes. Neighbour tables (`country+0xD08`) are rebuilt eagerly by `FUN_00515490` from the ownership-changing effects, inside `Event_Fire`, not lazily from triggers. The scope fix of 2026-10-11 is in release 1.0.0. The release passes mismatched calls through without resynchronising (HEAD looks 256 entries ahead); either way a mismatch runs the original code.
- **Hardened:** results are served only to calls returning to the scan's trigger calls (`0x008A7DEF` country pass, `0x008A7FD0` province pass, both `CALL EDX; TEST AL,AL`, checked at install). Before, any evaluation of an event trigger while the scan flag was set and no fire was running was matched against the list, and could compute a chunk from a foreign scope (an exception unwinding the scan would leave the flag set until the next day).
- **Shadow mode lines** now name the event id, the country tag or province, how many entries before the call the result was computed, and the fires of the day.
- **Default** `parallel_events = 0` in the shipped ini until a shadow run of an affected game is clean. **Open:** that run (`parallel_events = 2` with the reporter's mods).

## 2026-10-17 — Crash (heap corruption) on a `parallel_events` worker: `is_possible_vassal` writes while evaluated

- **Symptom (user, TGC):** the game closed with no line in `the_grand_loader.log`. Windows Error Reporting: `APPCRASH`, exception `0xC0000374` (heap corruption) in ntdll; minidump in `%LOCALAPPDATA%\CrashDumps`. Heap-corruption failures go through `RtlReportFatalFailure`, which bypasses the vectored handler, hence the silent log.
- **Dump (WinDbg `cdb`, x86):** the faulting thread is a `parallel_events` worker: `worker_main -> evaluate_task -> CAndTrigger_Evaluate (0x008B8288) -> IsPossibleVassalTrigger_Evaluate (0x008D8057) -> Country_RebuildPossibleVassals -> vector growth (0x00810876, 0x0042385D) -> _free -> RtlFreeHeap`; the game thread waits in `parallel::run` from `evaluate_hook` (scan call `0x008A7DEF`).
- **Cause:** `FUN_00519A40` empties and refills `country+0xE2C` on every query; two workers evaluating `is_possible_vassal` for the same country resize the same vector. TGC uses it in 45 event triggers (e.g. CANFlavor 44315-44318, DNBFlavor 98656-98681), GFM in 28. Also reachable from `parallel_decisions` (one TGC decision). The race can also make the trigger answer false without crashing, which fits the "simultaneous events" report.
- **Audit:** all 236 trigger classes from RTTI, Evaluate call trees three deep, stores into object fields: only `is_possible_vassal` and `can_create_vassals` write to game objects in a running game (details in `systems/parallel-triggers.md`).
- **Fix:** `parallel/trigger_locks.cpp`: both Evaluate slots (`0x00E30FE0`, `0x00E30FA8`) go to a wrapper holding one critical section, installed when `parallel_decisions` or `parallel_events` is on.

## 2026-10-17 — Join dialog: paste and the last IP

- **Request (user):** the "Enter IP address..." dialog should accept Ctrl+V and remember the last address.
- **Last IP:** the game already has it. `settings.txt` token `lasthost` (0x27F, keyword table at `0x00C3EC2D`) is parsed into `CSettings+0x124` (`Settings_ParseToken` `0x00475750`); `JoinIpDialog_Open` (`0x00766B80`) puts it in the edit box; `JoinIpDialog_OnJoin` (`0x00767140`) stores the typed text back at `0x00767328`. But the file is written only by `Settings_Save` (`0x009D7FA0`, settings in ESI, file name at `+0xC0`), called by four menus and by the `CSettings` destructor, which a normal exit does not reach: the user's file had `lasthost=""`. Fix `save_last_ip`: the store's CALL goes to a thunk that runs it and then `Settings_Save`.
- **Paste:** edit boxes read `WM_CHAR` (0x102) input events (the type checks in the input code, e.g. `0x00657xxx`); Ctrl+V arrives as `WM_CHAR` 0x16 and is dropped. Fix `clipboard_paste`: the exe's `DispatchMessageA` import goes to a wrapper that replaces that message with one `WM_CHAR` per clipboard character (ANSI text, printable only, trimmed, at most 256). Works in every text field.

## 2026-10-17 — Multiplayer port: always UDP 1638; `join_port`

- **Question (user):** hosting through playit (public address `209.25.140.16:6142`, local `192.168.15.244:1638`) did not work.
- **Found:** every connection request is built with port `0x666` = 1638 (`JoinIpDialog_OnJoin` `0x0076727B`, the other lobby join paths `FUN_00767C30`, `FUN_007680B0`, `FUN_007A73B0`, session config `FUN_00A04BB0`); no 1630 anywhere. `ConnectRequest_ctor` (`0x009F8180`, cdecl) stores host +0x40 and port +0x68; `NetClient_ctor` resolves the host with `gethostbyname`, then `inet_addr`; "host:port" fails both and falls back silently. Live check with two instances: the host bound UDP `0.0.0.0:1638`, the client an ephemeral UDP port; no TCP. A tunnel with another public port cannot be reached from the stock dialog.
- **Fix `join_port`:** the CALL at `0x0076729B` goes to a thunk that splits `address:port` (port 1-65535, one colon) into the host and port arguments on the stack and jumps to the original; the typed text, and so `lasthost`, keeps the port. Only the joining side needs it.
- **First test (user):** `join_port` dialled `209.25.140.16` on 61426 and the client reached the game (cb_cull line), but paste did nothing and `TGC/settings.txt` ended with `lasthost="209.25.140.16"`. Settings are per mod (`Documents\...\Victoria II\<mod>\settings.txt`). The game's own `Lobby_SaveSettings` (`0x0076BA60`) copies the session host (`session+0xF4`, already split) into `lasthost` and saves, 13 s after our save; fixed with a second redirect at `0x0076BE7B` that assigns the typed `address:port` instead. Paste: the window procedure (`0x009DFC70`) does pass `WM_CHAR` > 13 to the focused field, but only past an input-manager check; characters dispatched while Ctrl was held were dropped. Now the text is kept on Ctrl+V and posted as `WM_CHAR` once Ctrl is up; pastes are logged.

## 2026-10-17 — Game connects but never starts (playit, two instances); `network_log`

- **Symptom (user):** after Start, the host shows "Waiting for players", the client freezes, shows "Waiting for host", freezes again.
- **Logs:** both instances loaded the map (`cb_cull` 19:48:13 host, 19:48:16 client) and neither ran a single tick (no `pin_fpu: first tick`). No hang or crash record in Windows. The game's own logs in `Documents\...\TGC\logs` are shared by both instances, so their lines interleave; `game.log` shows "Executing History" a second time at the end (the client loading again).
- **Ruled out:** a second connection on the fixed port at game start. The other `ConnectRequest_ctor` callers are lobby paths (local host "localhost", the host box, the server list "NoAdress"), and the three `SessionManager_Start` (`0x009AEB90`) calls in `CInGameIdler_Update` build the local "InitialSession" on the way back to the menu. `skip_network_wait` only touches the server's service call (`0x00A618CC`); the client services ENet from 15 other sites.
- **Added `network_log`:** the entry of `enet_host_service` (`0x00A89D00`, prologue `55 8B EC 83 E4 F8`) jumps to a wrapper (trampoline with the prologue); connect/disconnect events are logged with the peer address (ENet 1.3 layout: host at peer+0x08, address at +0x18; or 1.2: host +0x00, address +0x0C, chosen at the first event), and silences over 2 s on a connection. **Open:** a run with it, through playit and through `127.0.0.1`.

## 2026-10-17 — Lag warning lingers in the chat; out-of-sync notice

- **Request (user):** "X is lagging behind" stays for minutes; make it 15 s. And show a similar message when a player goes out of sync and is recovered.
- **Lag warning:** `CClientPingCommand` execute (`0x0072F1E0`) on the host, at 6-10 days of lag, lowers the speed (idler `vt[0x168]`), builds `LAG_DECREASE_SPEED` with the player's name (`FUN_0042C180`), a chat message (`ChatMessage_ctor` `0x009F3290`, 0x74 bytes) and a chat command (`ChatMessageCommand_ctor` `0x009F4240`, the same command the chat boxes use), and queues it to every player (session `vt[0x38]->vt[0x18]`). On execute it reaches the chat channel and its listener (`Chat_OnMessage` `0x00676E80`) adds a line to `chat_list` (`interface/menubar.gui`). `Chat_ExpireLines` (`0x006784B0`, every frame) removes the oldest line only after 60 s since the last removal (`CMP [EBP-4],0x3C` at `0x006784FD`), one line at a time. Not configurable by script. Fix `chat_line_seconds` (imm8).
- **Out-of-sync notice `oos_notice`:** in `sync_check`'s pre-compare, draws differ and the `state_checksum` hashes match: `world+0xBA8` is set for that comparison (no dialog, no `CPauseGame`) and restored at the next one; a 15 s topmost notice over the game window says "recovered". State differs: the game reacts and the notice names the differing hashes. Local only (only the detecting machine compares). A notice visible to all players would need the chat command path above, sent through the session; not done.
- Shared helper `game::find_main_window` (`game/window.cpp`) now used by the Force OOS overlay too.
- **Revised (user):** the out-of-sync notice must use the lag warning's mechanism and place. It is now a chat line built like the warning: `ChatMessage_ctor` (`0x009F3290`: ECX parts list, EDX sender name, stack message/flag country/key text/text/1, `RET 0x14`; the flag country at +0x54 indexes the country list unchecked, so the local player's `world+0xB60` is used), sender from `UI_GetState` (`0x0068B610`, stdcall), `ChatMessageCommand_ctor` (`0x009F4240`, stdcall 4, `RET 0x10`) with the channel from `ObjectRegistry_FindByTypeAndId` (`0x009C16D0`, cdecl) of the key at `0x00F0D840/44` and the name at session manager+0xBC, queued with session manager+0x38 `vt[0x18]`. Sent to every player. The overlay window is gone.

## 2026-10-17 — Code review of the patch (architecture, duplication, comments)

- **Request (user):** an architecture review for clean, organised C++ with low cyclomatic complexity, then bring `docs/re` up to date.
- **Shared layers:** trigger containers, the `Evaluate` slot, `CEventScope` offsets and the scope ctor were defined three times (cb_cull, decision_cull, events_parallel); they now live in `patch/src/game/triggers.h`. Also new: `hooks/thunk.h` (`CP_SAVE_ALL`/`CP_RESTORE_ALL`), `game/string.h` (`GameString`, `string_at`), `game/window.h`, `util/clock.h`, and `game::current_day()` / `country_or_null()` in `game/world.h`. World, country, province and AI-object offsets that features had copied now come from `game/world.h`.
- **Correction:** the patch's std::string view assumed 0x18 bytes; the MSVC 2010 object is **0x1C** (allocator padded to 4). Rows added to `symbols/structs.csv`.
- **New rows:** `CEventScope+0x10` (province index); the two other AI object classes (`AiDailyObject` +0x0C, `AiCountryJobObject` +0x38) next to `AiObject` +0x34.
- **XMM rule written down** in the README (Compiler and calling conventions): every thunk that calls C++ saves XMM0-7, because callers built with LTCG keep XMM values live across calls.
- **Structure:** `decision_cull.cpp` was split into the cull, `decision_parallel.cpp` and `decision_profiling.cpp`, with a private `decision_cull_internal.h`. `tick_profiler` levels are a table. Long functions were split into smaller ones. `events_parallel` resets its fire depth at each scan, so an unwound fire cannot turn later scans off.
- **Comments:** history, dates (except `log.md` anchors) and measured numbers were removed from code comments; RE facts and why-notes were kept. Every line is now 120 columns or less.
- **Not done, on purpose:** an X-macro for the config table, a global clang-format run (`.clang-format` says 110 columns, the code uses 120, and the asm blocks are a risk), and moving `tick_profiler` into the `cp` namespace (its thunks are `extern "C"`).

## 2026-10-17 — "Unknown" modifier that exists: load order

- **Report (user):** `fix_missing_modifier` logged `corrupt_army` as unknown, but TGC defines it in `common/event_modifiers.txt`.
- **Cause:** the only effect that failed is `remove_country_modifier = corrupt_army` in `common/issues.txt` (the `army_command_principle` reform). `setup.log` shows the issues read (`issues.cpp`, line 188) long before the event modifiers (`modifier.cpp`, ~line 618), and `g_eventModifiers` is filled from that file, so the lookup in the parser returns 0. The same effect in decisions and events, parsed later, resolves. Unpatched, the reform's tooltip would crash and the removal never happens.
- **Fix:** both parsers keep the effect in ESI (`MOV ESI,ECX`; `MOV [ESI+0x24]`/`[ESI+0x20],EAX` after the call at `0x00881337` / `0x0088217A`). The wrapper records (effect, name) on a miss; the tooltip/execute guards look the name up again on first use and store the modifier, then run the original. Gameplay now differs from an unpatched game for such effects (the modifier is removed), so the option is a per-game setting in multiplayer.

## 2026-10-18 — Chat lines: `*>` prefix and per-line lifetime

- **Report (user):** the out-of-sync chat line reads "Host2*> Out of sync ...", and chat lines stay longer than 15 s.
- **Prefix:** the line text comes from the message's `vt[0x20]` (`0x009F3550` -> `0x009F2FF0`): with a sender and the ctor's last argument 1 it is `sender*> text` (format strings `*> ` at `0x00E3CFF4`, `> ` at `0x00E3CFF0`, ` (` / `)` / `*: ` for a key); with an empty sender it is the text alone. `oos_notice` now sends an empty sender. Found through the Ghidra Server copy (first use of it as the reference).
- **Lifetime:** `Chat_ExpireLines` counts from its last removal, not per line, so N lines arriving together leave one per period. `chat_line_seconds` now stamps each line (line count via list `vt[0x28]` before the call at `0x00655477`) and moves `this+0xE0` into the past when the oldest line is due, so the game's own code removes it; the game's period becomes 127 s.

## 2026-10-18 — Alt-tab and the mouse: `borderless_fullscreen`, `fix_edge_scroll`

- **Report (user):** alt-tab is slow, often hangs or shows a black screen; the map scrolls by itself (two monitors); players use external mouse tools.
- **Found (decompilation, call bytes checked in the exe):** exclusive fullscreen loses the device on every alt-tab (1 s sleep, full Reset, retries on failure); the game already has a borderless mode with a windowed device, selected by `fullScreen=no, borderless=yes`; the mouse is Win32 `GetCursorPos` with no capture or clipping; edge scroll never checks that the cursor is inside the window. Details in `systems/ui-console.md`, "Window, focus and mouse".
- **Built:** `borderless_fullscreen` forces the game's borderless mode at the primary monitor's size before the window is created; `fix_edge_scroll` clears the edge-scroll flag of the active-window camera call when the game is not in the foreground or the cursor is outside its client area.
- **Open:** the WM_ACTIVATE wParam check (focus flag after a restore), stuck mouse buttons without capture, the activation click reaching the map, the bordered-window cursor offset; not patched yet.

## 2026-10-23 — `parallel_events` shadow run clean; on by default again

- **Run (user):** `parallel_events = 2`, every other patch on, 1836-1840 (five game years), single player: 10 677 946 trigger results compared, **0 mismatches**, 0 passed through; 5 000-6 200 triggers listed per day, 12-386 events fired per 30 days (same-day chains included). The 2026-10-17 report stays unreproduced.
- **Default** `parallel_events = 1` in the shipped ini.

## 2026-10-23 — Windowed edge scroll: mouse position and `edge_scroll_lock`

- **Report (user):** in windowed mode with `fix_edge_scroll`, the map scrolls only with the cursor placed exactly on the edge, and never upwards.
- **Found:** mouse vt[0x4C] (`0x00A7A970`, slot `0x00E4397C`) is GetCursorPos minus GetWindowRect plus a hard-coded frame (4 px left, SM_CYDLGFRAME + SM_CYCAPTION top), thinner than the Windows 10/11 frame: the top and left 6 px bands of `Camera_Update` lie on the title bar and border, which `fix_edge_scroll` excludes.
- **Built:** `fix_edge_scroll` replaces the slot with a client-area position (ScreenToClient). `edge_scroll_lock` (mouse counts, 0 = off): the cursor is clipped to the client area while the camera update runs with focus; raw mouse input (WM_INPUT, window procedure subclassed) accumulates motion pushed against an edge, and past the value the cursor is released just outside it. Untested.

## 2026-10-23 — Map modes and province buildings (Script Extender research)

- **Question:** for the new Script Extender for TGC: how does the infrastructure map mode work, and how can it show any province building? How are province buildings expanded, and can a bulk upgrade post the same commands as the expand button?
- **Answer:** → [systems/map-modes.md](systems/map-modes.md), [systems/buildings.md](systems/buildings.md). Infrastructure is `MAPMODE` 4 (`mapmode_6`); its handler is loaded by a relocated `MOV EAX,imm32` at `0x0064B151` and its colour function (`0x008014D0`) has one caller; `CInGameIdler_RefreshMapMode` redraws. `Province_CanExpandBuilding` gates the button, Ctrl-click, the AI and the command's Validate; a bulk upgrade is `ConstructBuildingCommand_ctor` + `NetSession_QueueCommand` per province, as vanilla's Ctrl-click (one state) and the AI do.
- **Found on the way:** vanilla never checks that the builder owns the province (only owner == controller); `type =` other than factory / rgo / infrastructure / naval_base becomes 4 (fort), so in TGC `g_buildingDb+0x38` (`build_fort_in_capital`, the AI's fort) is the **bank**, and the AI never builds universities or banks. Script keyword ids: `raw/script-tokens.tsv`.
- **Method:** two research passes over the decompilation and a private copy of the Ghidra project; the hook-site and entry bytes were re-read from the exe.
- **Open:** listed at the end of both pages; the TGC bank-as-fort effect and the one-modifier-per-building limit need an in-game check.

## 2026-10-23 — Script Extender: `building_map_mode`

- **Built:** the infrastructure map mode (`MAPMODE` 4, button `mapmode_6`) shows one province building at a time. A click on the button while the mode is shown selects the next province building of `g_buildingDb+0x1C` (custom types included); the first click selects the railroad. Only the player's provinces are coloured, port buildings only on coastal ones: solid blue = at the highest level the current technology allows (`min(max_level, techMax − terrain reduction)`, as in `Building_CanBuildInProvince`), blue with khaki stripes = can be expanded, red with khaki stripes = anything else. A level under construction counts as built (construction list `CProvince+0xD8`, entry vt slot 12). The stripes are made thicker by swapping the stripes entry of the texture manager while button 6 is shown (pre-hook of `MenuBar_SetMapModeButtons`). The menubar tooltip title becomes "<MAPMODE_6> - <building>", with the texts read from the localisation CSVs. Hook sites: rows `se_*` in `symbols/patch_sites.csv`; → [systems/map-modes.md](systems/map-modes.md).
- **Crash and fix:** the first version read the building's localised name from the game's table, taking `LocalisationVariant+0x10` as a `std::string`. It is a parsed rich-text object (vectors of polymorphic segments), so the game crashed with an access violation in a `memcpy` with a garbage size. **Correction:** the game keeps no plain text of a localisation entry → [systems/ui-console.md](systems/ui-console.md#localisation-2026-10-23). The plugin now reads the CSVs itself (`-mod=` .mod file → `path` and `replace_path`, mod folders first, same-named files replace vanilla ones, first key wins, column 2) and passes the composed title to `Localize` as a "key", which a missing key shows verbatim.
- **Found on the way:** the layout of `CButtonObserverGlue<CInGameIdler>` (nine handler pairs, slot 2 = the click); `MapModeButton_Infrastructure` skips the click sound when the mode is already 4; the 0.3 dim of `MapColor_Infrastructure` depends on `g_mapDimFlagA/B` and `[prov+0x5C]+0x360 < 1`; `CCountryTechState+0x08` is a pointer to the max-level array; `CProvince+0x158` is read as a 32-bit int.
- **Untested in game after the fix:** the tooltip title from the CSVs (including the TGC `replace_path` case and a building without a localisation key), the thick-stripes swap across a map reload and a device reset, and the colours of every TGC province building (port buildings, `one_per_state`, constructions in progress). The CSV reader is the plugin's model of the game's loader, not checked against the loader code.

## 2026-10-24 — Events: body keywords, the event window, and what a `background` command needs

- **Question:** for the Script Extender: how is an event body parsed and its window built, and what would a new body command need to change the window background, not only the picture?
- **Answer:** → [systems/events.md](systems/events.md). `CEvent` vt slot 3 is the generic `Persistent_ReadBlock`; slot 4, `Event_OnToken` (`0x008A6280`), is a `switch` over 19 keywords (title `+0xDC`, desc `+0x114`, picture `+0x184`, the news strings, `is_triggered_only` / `allow_multiple_instances` / `major` / `fire_only_once` / `election` at `+0x1A0..+0x1A4`, news `+0x1A5`, issue_group `+0x1A8`). Any other key is logged as "Unexpected <key>" and its value skipped; the event is kept. The window type (election, major, country or province) comes from two flags and the scope; its `Background` child and sprite are fixed in the `.gui`/`.gfx`, so no event can choose its background today. The picture is a file: `<gfx>/pictures/events/<picture>.tga` loaded into a `TexturedSprite` with the `GFX_event_no_image` template and set with the icon's `vt[0xE0]` (SetSprite).
- **Design found:** `background` is already a keyword (token `0x99`), so a game without the plugin only logs one error per event. Plugin: replace `CEvent` vt slot 4 (`0x00E2C844`) to keep the value, wrap the two `EventWindow_ctor` calls (`0x008A8476` in `Event_Fire`, `0x0041F54E` in the console `event` command), find `Background` with `window->vt[0x44]` and give it a sprite made from a `GFX_` name (`SpriteTypes_Find` → `type->vt[0x48]()`) with `Button_SetSprite` (`0x009EF5B0`, `CEU3Button` slot `0xE0`). Display only: no multiplayer or save impact.
- **Found on the way:** the GUI type factory (`0x009A41C0`, EU3 layer `0x00646220`), the runtime SetSprite of buttons and icons (old sprite deleted, element size kept), `SpriteTypes_Find`'s hash, the parser fields (`+0x20` key token, `+0x24` key text, `+0x228` value type, `+0x22C` value text). The old notes called slot 3 the event body parser and left `+0x1A0`, `+0x1A2` and `+0x1A4` unnamed; corrected.
- **Method:** decompilation plus capstone disassembly of the exe for string arguments and call conventions; the original bytes of the three proposed hook sites were read from the exe.
- **Open:** whether `vt[0x44]("Background")` returns the background element, whether it is a `CEU3Button`, and whether `vt[0x48]` works for a `spriteType` used as `quadTextureSprite`: all to check in game. Rows in `symbols/*.csv`; the hook sites are `se_event_*` (proposal).

## 2026-10-24 — Script Extender: `event_background`

- **Built:** `background = "GFX_..."` in an event's body. `CEvent` vt slot 4 (`0x00E2C844`) is replaced: token `0x99` stores the value per event, other keys go to `Event_OnToken`. The two `EventWindow_ctor` calls (`0x008A8476`, `0x0041F54E`) go through a naked thunk (the by-value scope points to itself and must not be copied); after the constructor, `window->vt[0x44]("Background")`, `SpriteTypes_Find(g_renderDevice+0xA0, name)`, `type->vt[0x48]()`, `child->vt[0xE0](sprite)`. Rows `se_event_*` (experimental); symbols imported into the local Ghidra project (388 functions, 0 not at a function start). Not yet on the Ghidra Server (password needed).
- **Untested in game:** the three checks at the end of the command section of [systems/events.md](systems/events.md); the log line `event_background: <sprite> (Background element vtable ...)` should show `00E08BEC` (CEU3Button) or `00E3CE50`.

- **Changed the same day (user):** the value is a file, as for `picture`, not a sprite name: `background = "war"` → `gfx/pictures/events/backgrounds/war.dds` or `.tga`, mod first, nothing to declare. Found for it: `g_searchPaths` folder arrays (`+0x84` game, `+0x244` mod, `+0x4C` mode), `PHYSFS_exists` `0x00B21820`, `TextureManager_LoadFile` `0x009BC490` (PhysFS, `.dds` tried before `.tga`). The log line now names the file: `event_background: <path> (Background element vtable ...)`.

## 2026-10-24 — Vassal colours on the political map

- **Question:** where does the political map get a country's colour, and how can a substate or puppet be drawn as a shade of its overlord?
- **Answer:** `Country_GetMapColor` (`0x004FB5C0`, ESI = country, EAX = out `CColor`) returns the per-government colour or the base colour. `MapColor_Political` and `MapColor_PoliticalPlain` call it for each province's owner and controller. The same function also feeds a file rewrite (`0x0051B870`) and UI charts, so only the four map call sites are redirected. The overlord fields were read from the `is_substate` / `is_vassal` / `substate_of` / `vassal_of` triggers. → [systems/map-modes.md](systems/map-modes.md#political-colours-2026-10-24)
- **Correction:** `structs.csv` had `+0xCF4`/`+0xCF5` as `hasOverlord`/`isOverlord` and `+0x142C` as `overlord`. They are `isSubstate`, `isVassal` (not a substate) and the sphere leader's index; the overlord is `+0xCF8/+0xCFC`. `ai-diplomacy.md` row 3 was fixed accordingly.
- **Built:** Script Extender `vassal_map_color` (ini `vassal_map_color`, `vassal_map_color_own` = percent of the vassal's own colour, default 30). The four `CALL` sites (`se_vassal_color_*`) go to a thunk that runs the original, then blends the vassal's colour with its overlord's. XMM1 (the dim factor of `MapColor_Political`) and every other register are kept. Builds clean; not yet tested in game.
- **Open:** whether the political mode recolours when a country becomes or stops being a vassal, or only on the next recolour; which screens `MapColor_PoliticalPlain`'s other callers (`0x00756170`, `0x0076B930`, `0x0076D340`, `0x00777470`) are.

## 2026-10-24 — Peace options and `po_demand_province`

- **Question:** how does `po_demand_state` work end to end, and can a CB demand a single province (a new keyword `po_demand_province = yes`, for mods)?
- **Answer:** peace options are a list of `{token, prev, next, u8}` nodes at `CCasusBelliType+0x120`, which every user walks by token id.
  - `WarGoal_Execute` (`0x0094E660`) hands the state's province ids to `TransferProvinces` (`0x004B5A90`), or annexes the target when the state is its last one with the capital.
  - `WarGoal_PeaceCost` (`0x00951920`) sums a per-province value over the state. Infamy (`0x009500E0`) is a define times `badboy_factor`.
  - New keywords are possible because the lexer looks each word up lazily, and `Token_AddDynamic` (`0x009902D0`) adds names to its table. → [systems/casus-belli.md](systems/casus-belli.md) ("Peace options", "Lexer tokens")
- **Built:** Script Extender `demand_province`. It adds the keyword as a variant of `po_demand_state` that transfers, prices and annexes by the war goal's province (5 patch sites `se_demand_province_*`). Not tested in game.
- **Also found:** `country+0xE4C` is the state count (the list at `+0xE44`); it was labelled `aiScore`. `po_create_country` and `po_concede_defeat` are not parsed by `OnKeyword`.
- **Open:** which province the war-goal dialog and the AI put in `CWarGoal+0x28`; the ticking war score and the AI value still use the whole state.

## 2026-10-24 — Diplomatic actions, peace offers and the diplomacy UI (treaty research)

- **Question:** what would a peacetime "treaty" need? Two countries trade CB terms (provinces) and money; the AI evaluates the deal like a peace offer.
- **Answer:** the diplomatic-action framework, the peace offer and its AI rule, the peace options outside a war, the command factory, the province-transfer helpers and the diplomacy dialogs, with ~240 new function rows, ~50 globals and ~125 struct fields. → [systems/diplomacy.md](systems/diplomacy.md); the plan → [systems/treaties.md](systems/treaties.md)
- **Corrections:**
  - `0x006480E0` is `GuiOwner_AddDialog`, not `Window_Show`.
  - `0x00947210` / `0x00942540` are the per-frame updates of `CPeaceDialog` / `CWarGoalDialog`, not `Command_CanExecute_*`.
  - `CCountry+0xDA8` reads as "at war with", not "vassals".
  - Relation `+0x20..+0x30` are agreement pointers (`+0x2C` reparations), not lists.
  - `CWarGoal+0x18` is the `actor` (beneficiary); the save key `receiver` is `+0x20`, the target.
  - `g_economyDefines` is the defines root.
- **Answered:** casus-belli.md question 4 for the dialog. The war goal state list stores the state's first province.
- **Side note for `event_background`:** window `vt[0x44]` finds icons only, and the event window's `Background` is a `guiButtonType` (and the `backGround` button sits apart at `window+0x248`). The element lookup may return 0, so check its log line in game.
- **Open:** listed in diplomacy.md and treaties.md (RTTI on actions, `WarGoal_IsPossible` in peacetime, late `.gui` parsing, `CConquerProvinceCommand` round trip).

## 2026-10-24 — A province list for `po_demand_province`

- **Question:** the war-goal dialog lists states and stores the state's first province, so the player cannot choose which province a `po_demand_province` CB takes. Can it list provinces instead?
- **Answer:** the state rows (`CStateSelectionItem`, ctor `0x0093F2B0`) read only the state's name (vt `0x20`, `CState_GetName` `0x004CECE0`) and its `+0x48/+0x4C` province ids (tooltip `0x0093F5E0`; click `CWarGoalDialog_SelectState` `0x00944B10` stores the first id in the action clone's `+0x50` = war goal `+0x28`). `FillStateList` takes the list head with `MOV EAX,[EAX+0xE44]` at `0x00945429` (ESI = dialog; the CB is action clone `+0x34`). → [systems/casus-belli.md](systems/casus-belli.md) ("`po_demand_province`")
- **Built:** `se_demand_province_states`: for a marked CB that load returns a list of stand-in states, one per province, each named after its province. `allowed_states` and `WarGoal_IsPossible` now run per province for these CBs. Not tested in game.
- **Open:** the AI still picks a state's first province for these CBs.

## 2026-10-24 — Treaties built

- **Question:** build the peacetime treaty planned in treaties.md: CBs with `is_treaty_cb = yes` as terms, plus money, proposed from the "Justify war" button, answered like a peace offer, with an AI rule.
- **Built:** Script Extender `treaty` (9 patch sites `se_treaty_*`). `CTreatyAction` is a `CPeaceAction` object (0xA0 bytes, money at `+0x98`) with a copied vtable; the entry hook on `DiplomaticAction_CreateByType` covers commands, saves and the UI; the AI answers through a hook on the type switch of `AI_ProcessPendingProposals`; a "Diplomatic Play" chooser on DefaultDialog and an editor on the plugin's `se_treaty_dialog`; 11 treaty CBs in the plugin's `common/se_treaty_cbs.txt`; answer message types and texts added at run time. Testing in progress. → [systems/treaties.md](systems/treaties.md)
- **Answered:**
  - action slot conventions (all thiscall; 7 / 9 / 16 `(out)` RET 4, 36 `(bool)` RET 4, 32 / 34 `(ctx)` RET 4, 37 `(out, int)` RET 8) and base slots that need no war → [systems/diplomacy.md](systems/diplomacy.md);
  - answer message keys (slot 10 or 9 + `_THEY_ACCEPT` ...), message types are data (`MessageTypes_Read` `0x006A4FA0` can read a second file), and texts can be injected like `Localize`'s missing-key path → diplomacy.md ("Messages");
  - `PeaceOffer_GetTermsText` and `CPeaceAction_CopyCtor` read no war (treaties.md risk 5);
  - `WarGoal_Execute` semantics of `po_add_to_sphere`, `po_release_puppet`, `po_make_puppet`, `po_install_communist_gov_type` with and without a third country (`CWarGoal+0x10`, casus-belli.md question 5) → diplomacy.md;
  - the user asked whether `common/se_cb_types.generated.txt` is necessary: no in principle (an in-memory token reader exists, `0x00990DF0`, and `FileBuffer_ctor` `0x009D5450`), not done yet.
- **Corrections:**
  - **CB scopes:** `can_use` and `allowed_countries` run on the CB's *target* scope with THIS = the actor (`CWarGoalDialog_FillCbList` `0x00940205`..); the attacker / defender names of casus-belli.md's `IsCBUsable` pseudo-code may be swapped (note extended, new "Scopes" section).
  - **`CEventScope` FROM** is `+0x2C` tag / `+0x30` index (structs.csv said `fromRef` u64 at `+0x30/+0x34`). War-goal province scopes use `+0x18`.
  - **The FROM quirk:** `WarGoal_IsValid` keeps FROM only in the THIS scope, and nested scopes (`EventScope_Copy`, `any_owned_province`) read FROM from their own copy, so `free_peoples` / `liberate_country` never pass it; `WarGoal_GetTargetStates` puts FROM in the evaluated scope. The treaty validates that way.
  - `cb+0x0C` is `sprite_index` (the icon frame), `months` is `+0x134` (already fixed in structs.csv; diplomacy.md question 6 closed).
  - `CBManager_Load` clears the CB vector and restarts the index at 1, and deletes its block: it cannot be called twice, so the plugin merges its CBs into one file.
  - `g_searchPaths` points at the game folder when `cb_types.txt` is loaded, even with `-mod=`.
- **Crashes found and fixed:**
  1. Drawing the editor crashed in `Sprite_Draw` (`0x00A28A6B`): the plugin's `.gfx` was parsed at `Gui_Finalize`, after the game's `.gfx` loop, so the sprite's texture (`+0xD0`) was null. Now parsed at the end of the loop (`0x00635D89`).
  2. TGC saves crashed at the first peace in `War_Archive` (`0x00467F4E`, reading `goal->cb`): the merge used `g_searchPaths` and so vanilla `cb_types.txt`, so TGC's CB list was replaced. Now the source is found from the `-mod=` folders (`game::game_file`).
- **Open:** listed in treaties.md (in-memory CB block, terms in the answer messages, untested `install_government` and the release of dead countries, saves with a pending treaty without the plugin).

## 2026-10-25 — Wars and crises: how a war starts, who joins it, how a crisis is made

- **Question:** where are the gates that let a country into a war, and how is a crisis created and filled? Asked for a feature that would keep neutral countries out of wars.
- **Answer:** one test decides a declaration, `War_CanDeclare` `0x0093AA90` (vassal/substate target, sphere, alliance, crisis, military access, truce, existing war, usable CB); the `war` effect and the console skip it and call `CDeclareWarAction_Execute` `0x00913510` directly. Every way into a war ends in `War_AddAttacker` `0x00934280` / `War_AddDefender` `0x00935390` (18 direct calls, listed). A crisis is created in `CrisisManager_DayTick` `0x0042A130` (flashpoint, else colonial) and countries enter it through `Crisis_CanJoinInterest` `0x00425B40` and `Crisis_CanBackSide` `0x00425E20`; `CCrisis` layout (0xB4 bytes) in structs.csv. Puppets are protected by the vassal rules, not by a flag: a mod can keep a country neutral by making it the puppet of a dummy tag. → [systems/wars.md](systems/wars.md)
- **Open:** `world+0xD11`; `country+0xBB8` in `Crisis_CanBackSide`; the callers `FUN_004E7290` / `FUN_00933560`; a crisis loaded from a save; who writes the tension `state+0x280`.

## 2026-10-25 — Neutrality feature: built to compile, then left out

- **Question:** keep countries with `has_country_modifier = neutrality` out of wars by hooking the engine.
- **Tried:** the hook set in wars.md ("Hook points for a neutrality feature"): both calls of `War_CanDeclare` and of the reason, `CDeclareWarAction_Execute` and its two direct callers, all 18 `War_AddAttacker` / `War_AddDefender` calls, the CanPerform / reason slots of alliance, justify war, call ally and the crisis actions, the crisis creation sites `0x0042A245` / `0x0042ABEE`, and entry detours on the two crisis tests. Built and compiled on 2026-10-25, not shipped.
- **Result:** left out. The vassal rule gives a mod the same without code (a puppet of a dummy tag cannot be declared on), and the hook set is wide (about 30 sites) for a result the script already has. No row in patch_sites.csv.
- **Open:** none; the hook points stay documented in wars.md in case a mod needs more than the vassal rule.

## 2026-10-25 — Script effects: the factory, the effect object, and the `dismantle` effect

- **Question:** how does the engine build and run an effect block, so that a plugin can add a keyword of its own (a forced treaty after a peace)?
- **Answer:** one factory, `EffectBlock_AddChild` `0x00876B30` (slot 4 of every compound effect, plus four direct calls), allocates one object per keyword, appends it to the block's list (`+0x08/+0x0C/+0x10`) and passes the parser value to the child's slot 7. The vtable has 14 slots: 7 parse, 9 tooltip, 11 Execute `(scope, ?)`. `CReleaseVassalEffect` (`0x00E2B4AC`, 0x2C bytes) is the template that takes `TAG` / `THIS` / `FROM`; `CEventScope` fields for THIS and FROM. Effects run on every machine and are never saved. → [systems/effects.md](systems/effects.md)
- **Built:** Script Extender effects `dismantle = TAG`, `dismantled_by = TAG`, `dismantle_gw`, `dismantled_by_gw` (treaty.cpp "Dismantling"): an entry hook on the factory (`se_treaty_effect_factory`) builds a `release_vassal` template for the new tokens and swaps in a copied vtable with the plugin's tooltip and Execute; a turn whose countries are at war waits until `CPeaceAction` slot 32 (`se_treaty_peace_execute`) returns, then becomes a forced `CTreatyAction` with a budget by the loser's rank (`treaty_dismantle_*` keys), imposed by the human dismantler in a forced editor or picked by the AI dismantler. → [systems/treaties.md](systems/treaties.md) ("The `dismantle` / `dismantle_gw` effects"); the AI dismantler and partitions continue in the 2026-10-30/31 entry below.
- **Open:** the role of effect slot 8 (`0x0087A6C0`); the second argument of Execute; turns waiting for a peace are not saved.

## 2026-10-26 — Crash at `0x008CDC09`: `has_country_modifier` triggers in pop types

- **Question:** TGC crashed on the game thread ~10 s into a game: access violation reading 0 at `0x008CDC09`, ECX = 0.
- **Answer:** `0x008CDC09` is in the tooltip (vt `0x28`) of `CHasCountryModifierTrigger` (`0x008CD930`), which calls `modifier->vt[0x18]` on the null modifier at `+0x1C`. The parser leaves it null when the lookup fails, and here the failure is load order: setup.log shows "PopDatabase Phase Initialized" (line 406) before the modifiers (line 408 on), so every `has_country_modifier` in `poptypes/*.txt` (TGC: `totalitarian_system`, `central_bank_established`, ...) and `issues.txt` is null. All the names exist in `event_modifiers.txt`. Evaluate never matches them (no crash), so those pop weights have never worked. The game's "Unknown modifier" line for them is not in this run's logs. → [systems/crashes.md](systems/crashes.md)
- **Built:** `fix_missing_modifier` now covers the two triggers. The parsers' misses are recorded (`0x008CD7A7`, `0x008CDEAA`) and looked up again at `0x0063C356`, right after "Static Modifiers Initialized". The tooltips (`0x00E30880`, `0x00E308B8`) give an empty line for a name that really does not exist. Not tested in game.
- **Open:** which window showed the tooltip (the stack scan points into a military window, not reliable).

## 2026-10-30/31 — Partitions and the AI dismantler

- **Question:** can a dismantle break a loser into countries that have no cores on its land (Brazil's states), and can the AI dismantle?
- **Built (partitions):** treaty CB `treaty_partition` (`po_partition = yes`) and a mod's `common/se_partitions.txt`, generated by `script_extender/tools/partitions.pl` from the mod's cores, its `add_core` and its limited `secede_province`. At execution the listed provinces become cores of the third country (`Province_AddCore` `0x004B61A0`). → [systems/treaties.md](systems/treaties.md) ("Partitions")
- **Built (AI):** the engine already reaches a dismantle by itself (`AI_ManagePeace` → `AI_PickWarGoal` `0x00845AD0`, value from `AI_WarGoalValue` `0x00846C40`, threshold 20000, cost ≤ `ONE_SIDE_MAX_WARSCORE`); with a mod's dismantle CB that is mostly Great Wars. The plugin's AI dismantler now weighs terms by a CB keyword `ai_dismantle_weight` and caps homeland states (`treaty_dismantle_ai_max_states`, 35 %) and new countries (`treaty_dismantle_ai_max_new`, 3). A human loser gets a read-only notice (budget −1). Built, not deployed, not tested in game.
- **Open:** in-game test of the AI dismantler and the notice; whether a long-pending notice expires; the notice in multiplayer (local only).

## 2026-10-31 — How the AI chooses province buildings

- **Question:** how does the AI decide what to build, and why does a new type-4 building listed before the fort replace the fort for the AI?
- **Answer:** one job per AI country, `AI_ConstructionUpdate` (slot 6 of `0x00E284EC`), runs every builder. It caches the first province building of type 2, 4 and 3 once, so the fort builder builds whatever type-4 building comes first, with the fort's border scoring. Type-4 buildings after it are never built by the AI. One world-MT roll against the five `CHANCE_*` AI defines picks the builders; nothing is built at war. → [systems/buildings.md](systems/buildings.md) ("The AI builder")
- **Correction:** party rules are `country+0xAC8 + 8·i` (not `+0xAC0`), *i* in `CRuleDefinition` order. Read from the definition list; every earlier [I] mapping fits it.
- **Open:** the job's scheduling (`+0x30` countdown, 10–26), the fort scoring's inputs (`0x0050E450`, AI tables `+0x3B8/+0x3F0`); a native builder for other buildings (hook point and constraints in buildings.md).

## 2026-10-31 — `buildings_ai`: the AI builds any province building a mod lists

- **Question:** can the AI build province buildings beyond the three vanilla ones, driven by script?
- **Answer:** yes. A decision's `ai_will_do` is a `CMeanTimeToHappen` (`0x00E2CEB0`). Its slot 4 (`0x008AE8A0`) reads `factor`/`base`/`days`/`months`/`years` and `modifier`; the modifiers are `CAndTrigger`s with a factor at `+0x20`. `Persistent_ReadBlock` calls only slots 4 and 5 of the object it reads into, so an object with a vtable of our own can walk any file with the game's parser. The Lua the game embeds (5.1 + luabind) serves only `defines.lua` and the tutorial. → [systems/buildings.md](systems/buildings.md) ("Built: buildings_ai")
- **Built:** Script Extender `buildings_ai`: wrapper on the construction job's slot (`0x00E28504`); `common/buildings_ai.txt` per building (`chance`, `money`, `weight`); vanilla fort / naval base / railroad found by name. Shared helpers `game/script.h` (parser block, script blocks) and `game/buildings.h` (expand check, cost, construction command), now also used by `country_upgrade` and `treaty_ui`.
- **Open:** in-game test (the file is read on the first AI run: its log lines, then the constructions); whether the parser block is safe to build mid-game (it is during loading).

## 2026-10-31 — Console commands of our own

- **Question:** how does the console find a command, and can a plugin add one (a `dismantle` test command)?
- **Answer:** a static table of 60 `CConsoleCommand` entries (0x4C bytes) collected into a vector at console
  creation; the dispatcher only searches that vector. Appending an entry pointer is enough. → [systems/ui-console.md](systems/ui-console.md)
- **Correction:** `Console_Execute` (`0x00420EB0`) was named as the central dispatcher; it is the handler of
  `debug`. The dispatcher is `Console_Dispatch` (`0x00423B50`).
- **Built:** `script_extender/src/game/console.cpp` (site `se_console_commands`), first user the treaty
  feature's `dismantle <TAG> [<TAG>] [gw]`.
- **Open:** whether the console is reachable in multiplayer at all; the `hidden` byte (`+1`) is inferred
  from the single entry that sets it.

## 2026-10-31 — rich/middle/poor_income_modifier do nothing

- **Question:** why the three income modifiers barely work, and where to fix them.
- **Answer:** their only reader, `Pop_AddWorkingCash`, is on the artisan path and scales the artisan's
  input budget, not income, from the province modifier sum, which no country modifier reaches. Pop pay
  goes through `Pop_AddMoney` with no modifier. → [systems/economy.md](systems/economy.md)
- **Method:** modifier index from the order of `ModifierDefinitions_Init` (87-89); byte scan for reads of
  `+0x2B8/+0x2C0/+0x2C8` after a load of the country (`+0xA60`) and province (`+0x9C`) sums: one reader;
  income calls told apart by the income tax call before them.
- **Built:** Script Extender `income_modifiers`.
- **Open:** the categories 5 and 7 of the pop income array; whether `+0xA90` is a second country sum.

## 2026-10-31 — set/clr/has_province_flag "do not work"

- **Question:** whether the three province flag keywords work.
- **Answer:** the two effects work and the flags are saved; `has_province_flag` has no trigger class and
  no case in `TriggerBlock_AddChild`, so it is logged as unknown and dropped: always true, inside `NOT`
  too. The vanilla localisation has `HAVE_PROVINCE_FLAG`, the exe never uses it. → [systems/triggers.md](systems/triggers.md)
- **Method:** RTTI list (`CSetProvinceFlagEffect`, `CClrProvinceFlagEffect`, no `CHasProvinceFlagTrigger`);
  the token switch of both factories; province save and load (`flags` = token 0x40D at `+0x34C`).
- **Correction:** `0x0068F390` is `vector<T*>::push_back` (`Vector_PushBackPtr`), not entity registration.
- **Built:** Script Extender `province_flags`.
- **Open:** the two unnamed arguments of the trigger tooltip slot 10; who reads `g_mapView+0x3A28` after
  a province flag changes (the map draws sprites for some province flags, `0x00643xxx`).
