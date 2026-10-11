# Decisions

Where and how often `potential` / `allow` are evaluated. Feature built on it:
[docs/decision-cull.md](../../docs/decision-cull.md).

Sources: this repo's sessions (2026-10-02..04). Raw material:
[raw/decision-keyword-map.txt](../raw/decision-keyword-map.txt), [raw/decisions/](../raw/decisions/),
[raw/profiler/](../raw/profiler/).

## Summary

- `CDecision` (vtable `0x00E29B54`, ctor `0x008743C0`, keyword handler `0x00874CE0`): name `+0x08`,
  `potential` `+0x24`, `allow` `+0x40`, `effect` `+0x5C`, `ai_will_do` `+0x110` (an MTTH-style block).
- The manager `g_decisionManager` (`0x01319730`, 0x38 bytes) has two `vector<CDecision*>` at
  `+0x0C` and `+0x28`.
- The daily AI loop is `AI_DecisionUpdate` (`0x00821190`): once per AI country per day, driven by the
  per-frame AI job. It tests `vt[6]() && potential && allow`, then `ai_will_do`, then executes.
  In TGC this is 1651 decisions × ~320 countries ≈ 529 k `potential` evaluations a day, ~23.5 ms of a
  ~200 ms day.
- Decisions are already evaluated once per country per day, so caching cannot help. Skipping them
  can: 213 k/day are tag-locked decisions evaluated for the wrong country.

## Evaluation sites **[C]**

| site | when | what |
|---|---|---|
| `AI_DecisionUpdate` `0x00821190`, called at `0x0081EA9B` from `AI_CountryFrameJob` | once per AI country per day | both lists; reads `g_decisionManager` directly; `ECX` = AI object (`+0x30` tag, `+0x34` country index) |
| `AI_DiplomacyUpdate` `0x0085D270` | a few countries a day (1–6 measured) | both lists through `DecisionManager_Get` (`0x00875040`, calls at `0x0085D34F` / `0x0085D459`, `ESI` = AI object, `+0x0C` index) |
| `Player_DecisionAlert` `0x005399C0` | player only (`0x0050B792`, guarded at `0x0050B780`), also per frame from `CInGameIdler_Update` (`0x00654E48`) | `potential && allow`, stops at the first available one |
| `CPoliticsView` (`0x0046B3A0`, `0x0047D0F0`), `0x006DB2E0`, `0x006D2DD0`, tooltip `0x0053A6D0` | on UI refresh | player |

The first write-up named `AI_DiplomacyUpdate` as the daily loop; the per-country list requests
(1–6/day) disproved it and the xrefs of `g_decisionManager` led to `AI_DecisionUpdate`.

## Script patterns (static count, TGC)

1653 decisions: 767 use "take once" (`set_country_flag` in the effect, `NOT has_country_flag` in
`potential`) with a flag nothing ever clears; 404 have a `NOT has_global_flag` gate on a global flag
that is set and never cleared; 666 are tag-locked. First condition of `potential`: `tag` 637, `owns`
221, `has_country_flag` 141, `is_greater_power` 130, `primary_culture` 128. Scripts:
`tools/scripts/decision_*.pl`; output in [raw/decisions/](../raw/decisions/).

## Measured (TGC, 1836, max speed)

529 k evaluations/day = 23.5 ms (12 %), 2500 passes/day; flat distribution (heaviest
`pay_owed_steamers` 0.29 ms/day, top 25 = 2.5 ms). With `cull_decision_checks` v2 (per-country lists
without tag-locked, `ai = yes/no` and dead flag-gated decisions) the AI sees 945 of 1651 decisions per
country and the day drops from 196 to 189 ms.

## Open questions

1. Meaning of `CDecision` slot 6 (returns 1 in the base class).
2. Late-game share of dead (country, decision) pairs.
