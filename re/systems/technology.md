# Technology research

Sources: `Technology/technology.md` and `Patch/patch.md` in
[maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering)
(Spanish Ghidra names translated in [functions.csv](../symbols/functions.csv)). Patch bytes verified
on 3.04 in this repo.

## Summary

- `Tech_IsAvailable` (`0x00569920`, result in `AL`) checks the normal rules: date (`tech+0x340`),
  state (`country+0xBCC -> +0x2B8 [tech+0x2E4]`) and prerequisites (`tech+0x2EC`). It has **no**
  civilization check and five callers (list, entry update, UI state, action, AI), so patching it
  changes all of them.
- Civilization (`country+0x12D0`) is checked separately in three places:

| where | site | patch site | |
|---|---|---|---|
| player UI state, `TechView_UpdateState` `0x007AA580` | `0x007AA757` `JNZ` | `tech_player_civ_gate`: `75 5A` -> `EB 5A` | confirmed |
| player "start" action, `TechView_OnAction` `0x007AA890` | `0x007AAA83` `CMP; JNZ 0x007AAB5E` | (none yet) | C |
| AI, `AI_ProcessResearch` `0x00859710` | `0x00859755` `JZ` | `tech_ai_civ_gate`: six NOPs | confirmed |

Turning the player's `JNZ` into `JZ` inverted the rule (uncivilized could research, civilized could
not), which is how the branch was identified; `JMP` makes both take the same path. Forcing the list
state in `TechView_BuildList` (`PUSH 3`/`PUSH 4` -> `PUSH 1`) only paints the list blue; "start"
stays disabled.

`Country_GetRankClass` (`0x005379E0`) returns 0 GP, 1 secondary, 2 civilized, 3 uncivilized from
`+0x12D0`, `+0x31`, `+0x1404`; it is not a research gate.

## Open questions

1. Whether the `0x007AAA83` gate needs its own patch once the UI-state patch is applied.
2. The AI research choice inside `AI_ProcessResearch`.
