# Address book (v2game.exe 3.04, image base 0x00400000)

Raw list of everything located during the October 2026 sessions. VA = virtual address; RVA = VA - 0x400000.
Confidence: C = confirmed in the listing, I = inferred from context, ? = guess.

## Globals

| VA | Name | Notes | |
|---|---|---|---|
| 0x012588E8 | world | `+0xB0C` date in hours (day = /24), `+0xB5C/+0xB60` player tag/index, `+0xB28` speed 0..4, `+0xBD0/+0xBD4` country list used by the world market, `+0xACC` province array, `+0xAEC/+0xAF0`, `+0xAE0/+0xADC` country vector of the day tick | C |
| 0x012587E4 | country manager | `+0` count (all defined tags, 683 in the user's mod), `+4` array of `CCountry*` | C |
| 0x0131ACAC | CB manager | `+0xC/+0x10` `vector<CCasusBelliType*>`; getter 0x0090CC40 | C |
| 0x01319730 | decision manager | lists `+0xC/+0x10` and `+0x28/+0x2C`; getter 0x00875040 | C |
| 0x01258A74 | render device singleton | `+0x6BCA0` "has focus" byte read by the message pump | I |
| 0x01258A48 | army/unit manager? | iterated in the per-country pre-pass | ? |
| 0x012586DC | economy/defines holder | `+0x50` market totals | ? |

## Country object (`CCountry`, pointer from the manager array)

| Offset | Meaning | |
|---|---|---|
| +0x1C | tag (3 chars) | C |
| +0x20 | index | C |
| +0x50 | (relation object) list of held CBs, entries `+0x24` = CB type index | C/I |
| +0x9D8/+0x9DC | owned provinces vector (empty = country does not exist) | C |
| +0xBE8 | array of relation objects indexed by other country | C |
| +0xCF4/+0xCF5 | has overlord / is overlord flags | I |
| +0xD08 | neighbour table (byte per country) | C |
| +0xE44 | state list (world market iterates states, then `+0x18` factories) | I |
| +0xE4C | score/rank-like value used as AI gate | ? |
| +0x1404 | rank | I |
| +0x1430 | infamy x1000 | C |
| +0x12D0 | civilized flag | C |
| +0x37C/+0x37D | gates for AI_DailyUpdate (human? disabled?) | ? |

## Functions

| VA | Name | Convention / notes | |
|---|---|---|---|
| 0x00682EC0 | Update_World_and_Advance_Time | called by CHourlyTickCommand::Execute 0x0072ECF0 (vtable 0x00E12414 slot 6, +0x18) | C |
| 0x006859C0 | DayTick | provinces loop, countries loop, events; calls `time_throttle` 11x | C |
| 0x004B1A10 | Province_DailyUpdate | | C |
| 0x005091A0 | Country_DailyUpdate | 683 calls/day; `+0x0DF`/`+0x37D` gates before AI | C |
| 0x00508590 | per-country pre-pass | units/relations/truces; 21 ms/day total | I |
| 0x0085CCB0 | AI_DailyUpdate | `this` in EAX | C |
| 0x0085CF10 | FUN_0085CF10 | calls AI_DailyUpdate at 0x0085CFFD | C |
| 0x0085D270 | AI_DiplomacyUpdate | decisions for AI, then war/diplomacy target scans | C |
| 0x0085DB40 | AI_ScoreWarTargets | per other country; IsAnyCBUsable at 0x0085DCDA | C |
| 0x0085E0E0 | AI_ScoreDiplomaticTargets | per other country; IsAnyCBUsable at 0x0085E5FB | C |
| 0x008A7CD0 | EventManager_DailyScan | | C |
| 0x00685620 | UpdateTimeThrottle | `this`(world) in EDI; renders a frame via 0x009DF2B0 when the time threshold is reached | C |
| 0x009DF2B0 | App::ProcessMessagePumpAndUpdate | PeekMessage loop, then state->vt[1](); `this` in ESI | C |
| 0x00654D80 | CInGameIdler::Update | per-frame in-game function; vtable slot at 0x00E0A350; contains ProcessGameTimeAdvance 0x00682BD0 | C |
| 0x00489990 | Economy_Update | world market daily update ("worldmarket.cpp"); 37 ms/day | C |
| 0x00484060 | WorldMarketUpdate | countries -> states -> factories (0x00482FF0 per factory, 0x00485E40 per country); 21.6 ms/day | C/I |
| 0x004808D0 | ProcessMarketAndGoodsDistribution | 8.7 ms/day | C |
| 0x00488080 | market post-processing | 3.3 ms/day | I |
| 0x00515490 | per existing country, 8.7 ms/day total | unknown purpose | ? |
| 0x0068F390 | entity registration (parallel_for inside) | 19 047 calls/day | C/I |
| 0x0092EDE0 | CBManager_IsAnyCBUsable(rel, defRef, attRef) | 3 stack args, RET 0xC | C |
| 0x0092EEC0 | CBManager_IsCBUsable(rel, defRef, attRef) | CB index in EDI; walks rel+0x50 (held CBs: node +0 CCasusBelli, +8 next; CB +0x24 type index) for the type; false if not held and not `always` (+0x132); else two CEventScopes (ebp-0x5C attacker, ebp-0xA4 THIS = defender, main+0x24 -> THIS) and type->vt[8] CanUse(scope) | C |
| 0x0090A7F0 | CCasusBelliType::CanUse(scope) | = can_use.Evaluate | C |
| 0x0090A1E0 | CCasusBelliType::OnKeyword | | C |
| 0x00909D60 | CCasusBelliType::ctor | | C |
| 0x0090CDC0 | CBManager_Load | | C |
| 0x0090DA40 | CBManager_FindByName | | C |
| 0x008B8260 / 0x008B88A0 / 0x008B9240 | CAnd/COr/CNotTrigger::Evaluate | short-circuit | C |
| 0x008B82B0 | CAndTrigger::Describe | tooltip | C |
| 0x008A8650 | CEventScope::ctor(this, parent) | | C |
| 0x00882A40 | CClrCountryFlagEffect::Execute(scope, arg2) | vtable 0x00E2ACC0 slot 11 (+0x2C); two stack args, `RET 0x8`; flag name std::string at effect+0x20; country = scope+0x0C; only reached via vtable | C |
| 0x00882BA0 | CClrGlobalFlagEffect::Execute(scope, arg2) | vtable 0x00E2ADA0 slot 11; `RET 0x8` | C |
| 0x008829E0 | CSetCountryFlagEffect::Execute(scope, arg2) | vtable 0x00E2AC88 slot 11 | C |
| 0x00882B60 | CSetGlobalFlagEffect::Execute(scope, arg2) | vtable 0x00E2AD68 slot 11 | C |
| 0x008D4000 | CTagTrigger::Evaluate | `scope+0x0C == this+0x20` (country index of the literal tag; +0x1C tag chars); THIS/FROM flags at +0x18/+0x19 | C |
| 0x008D7450 | CIsAiTrigger::Evaluate | value bool at +0x1C (bool parser 0x008B93B0: `yes` = token 0x29F); AI = `world+0xAEC[country] == 0` (human-player table, count at +0xAF0) | C |
| 0x008D28C0 | CCasusBelliTrigger::Evaluate | calls IsAnyCBUsable | C |
| 0x008D3F50 | country-reference value parser | THIS=0x405, FROM=0x3DE | C |
| 0x00874CE0 | CDecision::OnKeyword | | C |
| 0x008743C0 | CDecision::ctor | | C |
| 0x00874EA0 | decision ai_will_do evaluation | | C |
| 0x005399C0 | player decision alert (potential && allow, stops at the first available) | from Country_DailyUpdate 0x0050B792 (player only: guarded by `country == world+0xB60`), CInGameIdler::Update 0x00654E48, tooltip 0x0053A701 | C |
| 0x00821190 | AI_DecisionUpdate(ai) | ECX = AI object (+0x30 tag, +0x34 country index); reads DAT_01319730 directly; vt[6] && potential && allow -> ai_will_do -> execute; once per AI country per day = the 529 k potential evaluations/day | C |
| 0x0081E9E0 | AI per-country frame job | calls 0x00820760, 0x0081E5B0, 0x0081E280, then AI_DecisionUpdate at 0x0081EA9B; callers FUN_0064EF50, FUN_006874C0 (from CInGameIdler::Update), FUN_00662010 | C |
| 0x00505010 | one-time decision list setup | caller FUN_00662010 (CInGameIdler vtable slot 0x00E0A358); reads/writes DAT_01319730 | I |
| 0x00875040 | DecisionManager_Get() | creates DAT_01319730 on first use; only callers: AI_DiplomacyUpdate at 0x0085D34F (list +0x28) and 0x0085D459 (list +0x0C); ESI = AI object (+0x08 tag, +0x0C country index) at both | C |
| 0x00633800 | application ctor | version string "V2 v3.04" at 0x00633826 (push 8 / push 0x00E0764C) | C |
| 0x00C381C0 | keyword/token registration | `MOV EDX,<string>; ...; MOV ECX,<id>` | C |

## Vtables (RTTI names)

| VA | Class |
|---|---|
| 0x00E33724 | CCasusBelliType |
| 0x00E3374C | CNullCasusBelliType |
| 0x00E34C08 | CCasusBelli |
| 0x00DF9994 | CAndTrigger |
| 0x00E2FB70 | COrTrigger |
| 0x00E2FBA8 | CNotTrigger |
| 0x00DFA6B4 | CTrigger (base) |
| 0x00E2FBE0 | CContextTrigger (scope change) |
| 0x00E30B30 | CCasusBelliTrigger |
| 0x00E29B54 | CDecision |
| 0x00E12414 | CHourlyTickCommand |
| 0x00E2ACC0 | CClrCountryFlagEffect |
| 0x00E2ADA0 | CClrGlobalFlagEffect |
| 0x00E2AC88 | CSetCountryFlagEffect |
| 0x00E2AD68 | CSetGlobalFlagEffect |
| 0x00E30C80 | CTagTrigger |
| 0x00E307E8 | CHasCountryFlagTrigger |
| 0x00E30820 | CHasGlobalFlagTrigger |
| 0x00E30EE8 | CIsAiTrigger |
| 0x00E0A34C | CInGameIdler |
| 0x00E0AE60 | CGameState |
| 0x00E0E458 | CPoliticsView |

Full list: `rtti-vftables.txt` (3111 symbols). Trigger classes with their Evaluate/parser slots: `trigger-vtables.txt`.

## Call sites patched by community_patch

| Site | Original call | Feature |
|---|---|---|
| 0x0085DCDA, 0x0085E5FB | IsAnyCBUsable | cull_cb_checks (loop replacement) |
| 0x0072ECFA, 0x00683AB1, 0x00685B49, 0x00685ED1, 0x0050B0EB, 0x0050B0F0, 0x0050B0F6, 0x006861E8 | level-1 tick blocks | tick_profiler |
| 0x00E0A350 (vtable slot) | CInGameIdler::Update | tick_profiler |
| 0x0085D34F, 0x0085D459 | DecisionManager_Get | cull_decision_checks (per-country lists) |
| 0x0081EA9B | AI_DecisionUpdate | cull_decision_checks (global swapped for the call) |
| 0x00E2ACC0+0x2C, 0x00E2ADA0+0x2C (vtable slots) | Clr*FlagEffect::Execute | cull_decision_checks (list invalidation) |
| CDecision+0x24 vtable pointer of every decision | potential.Evaluate | decision_profiling |
| 0x00E12414+0x18 (vtable slot) | CHourlyTickCommand::Execute | hooks/hourly_tick (decision_cull, year_timer) |
| see `patch/src/features/tick_profiler_sites.h` | levels 2-5 | tick_profiler |
| 0x00633826 (+1 imm8, +8 pointer) | version string | launcher label |
| IAT `tbb.dll!?initialize@task_scheduler_init@tbb@@QAEXHI@Z` | | engine_worker_threads |
