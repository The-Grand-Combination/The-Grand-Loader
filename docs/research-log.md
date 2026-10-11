# Research log

Newest first. Each entry: date, question, where the answer lives, what is still open.

## 2026-10-01 — Casus belli evaluation cost ("CBs are checked N×N")

- **Question:** does the engine evaluate `can_use` of every CB type for every attacker × defender pair, and does trigger ordering matter?
- **Answer:** yes, daily, for every AI country × every other country, but only for CB types with `always = yes` or already held in that relation; `CanUse` is an uncached `CAndTrigger::Evaluate` with `THIS` = attacker, `FROM` = defender; AND/OR/NOT short-circuit in file order. → `Engine/Casus_Belli_Evaluation.md` in the Victoria2-Reverse-Engineering repo
- **Side products:** full `CCasusBelliType` field map, keyword-id → offset table, trigger container semantics, console entry points (`debug cb_use`), the Ghidra headless tooling in `tools/ghidra/` (this repo).
- **Open:** meaning of the AI gates `country+0x37C/0x37D`, `ai+0x19`, `DAT_01318E5C`, `country+0xE4C`; whether `CNullCasusBelliType` instances sit in the manager vector.

## 2026-10-01 — Version label site for the Community Patches loader

- **Question:** where does the corner label "V2 v3.04 (XXXX)" come from, so a loader can append text to it?
- **Answer:** `std::string("V2 v3.04", 8)` at `0x00633826` inside the application constructor; patch the `push imm8` length and the pointer only. Implemented in the Community Patches loader (`launcher/src/version_label.cpp`); not yet registered in `Patch/patch.md` (Victoria2-Reverse-Engineering).

## 2026-10-01 — Engine worker threads (TBB)

- **Question:** what does `task_scheduler_init::initialize(threads)` affect?
- **Answer:** only three `parallel_for` sites (entity/spatial registration near `FUN_0068F390`, graphics near `FUN_0099DA20`); the simulation is otherwise single-threaded. Not yet written up as a document.

## 2026-10-02 — `cull_cb_checks`: attacker-only culling of `can_use`

- **Question:** can the daily N×N CB scan be short-circuited per attacker without changing results?
- **Answer:** yes. Hooked `CCasusBelliType::CanUse` (vtable `0x00E33724+0x20`) and the two AI call sites of `IsAnyCBUsable`; attacker-only top-level conditions are evaluated once per (CB, attacker, day). Classification of all 236 trigger classes by decompiling their `Evaluate`: 174 scalar + 20 country-reference (instance flags `+0x18` THIS / `+0x19` FROM) usable, 40 pair-dependent excluded. → [cb-cull.md](cb-cull.md)
- **Correction to the previous entry:** in `can_use` the scope is the attacker and `THIS` is the defender (`scope+0x24`); `FROM` is unset. The decomp document was updated.
- **Open:** measure the real gain on a heavy mod (log line every 30 days reports calls/culled/evaluated); whether the scalar parsers' `= THIS` (ignored by their `Evaluate`) is a script-side bug worth documenting.

## 2026-10-02 — `decisions_once_per_day`: `potential` evaluated once per country per day

- **Question:** where are decisions evaluated and how often; can they be gated to once per day like events?
- **Answer:** `CDecision` (`0x00E29B54`, ctor `0x008743C0`) holds `potential` at `+0x24`, `allow` at `+0x40`, `effect` at `+0x5C`, `ai_will_do` at `+0x110`; keyword handler `0x00874CE0` (potential `0x729`, allow `0x72A`). Evaluated daily by the AI (`0x0085D270`) and the country alert (`FUN_005399C0`), and on every refresh by the politics view / decision list / tooltips. Patched by cloning `CAndTrigger::vftable` for the `potential` blocks with a cached `Evaluate`; `allow` left live. → [decision-cache.md](decision-cache.md)
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
- **How:** eight single-caller call sites in the tick redirected to register-preserving thunks that swap the return address (works for the game's custom conventions); inclusive/exclusive ms/day per block every 30 days. → [tick-profiler.md](tick-profiler.md)
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
- **Built:** `decision_profiler` renamed/extended into `cull_decision_checks` (both layers: tag lock, dead-pair bitset invalidated by hooks on `CClrCountryFlagEffect::Execute` `0x00882A40` / `CClrGlobalFlagEffect::Execute` `0x00882BA0`, vtable slot 11, plus a 30-day full reset) and `decision_profiling` (rewrites `v2cp_decisions.log` with running per-day averages for every decision; constant size). → [decision-cull.md](decision-cull.md)
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
- **v3:** per pair, one scope pair (laid out as `IsCBUsable` does: attacker, `+0x24` -> THIS = defender), `CanUse` only for the attacker's live `always` types (v2 pre-pass) and for the held types found in one walk of the list; first true returns. Same boolean OR, no side effects. `IsCBUsable` is no longer called. → [cb-cull.md](cb-cull.md)
- **Open:** `year 1836` with `cull_cb_checks = 1` (v3) against 189 ms/day (decision cull on, CB cull off). Default stays `0` until measured.
- **Result (2026-10-04, `year_timer`, TGC 1836, `cull_decision_checks = 0`):** `cull_cb_checks` v3 **185 ms/day (1.07 min)** against the 196 baseline: −11 ms/day, −5.6 %, i.e. essentially the whole 11 ms/day the profile attributed to the AI CB scans. The N×N loop was cheap per iteration but the scope construction and the held-list walks per type were not. Default should move to `1` once the combined run confirms.
