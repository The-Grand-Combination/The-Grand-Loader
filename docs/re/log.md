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
