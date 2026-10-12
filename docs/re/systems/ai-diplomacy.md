# AI opinion and acceptance

How the AI scores another country and decides whether to accept a diplomatic action.

Sources: `AI/Ai_relations.md` in
[maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering).
Static reading, not re-verified here; confidence as given by the source (mostly **[I]**).

## Summary

- `AI_GetOpinionScore` (`0x0083EC10`, `(self, other, out_score, verbose)`) computes one integer. With
  `verbose` it also builds the `AIREASON_*` lines of the diplomacy tooltip; without it, it returns at
  the first hard cut-off.
- Two wrappers: `AI_GetOpinionScore_ById` (`0x00841640`) returns the raw score;
  `AI_GetPersuasionDifficulty` (`0x008416B0`) returns `50 - score` after early outs (incompatible, the
  other is its overlord, not civilized, `count > 2` -> 100).
- `AI_EvaluateCommandChance` (`0x0083DB20`) is a switch over the action type (`obj->vt[0x20/4]()`)
  returning 0..100. The same switch is inlined in five more functions (tooltip, pending proposals,
  three button validators). `AI_ProcessPendingProposals` (`0x00820DF0`) rolls `% 100` against it and
  marks proposals accepted (3) or rejected (2).

## Opinion score blocks (in order)

| # | condition | effect |
|---|---|---|
| 1 | overlord/vassal relation and it is `other` (`+0xCF4/+0xCF5`, `+0xCFC == other.index`) | `= 1000` (`AIREASON_OVERLORD`) |
| 2 | same, but not `other` | `= -1000` |
| 3 | `self` has another overlord (`+0x142C`) and `other` is a GP | `= -1000` |
| 4 | `self` in `other`'s vassal list (`+0xDA8`) | `= -1000` |
| 5 | rival spheres (`+0x156C`) | `-1000` |
| 6 | colonial rank below `COLONIAL_RANK` | `= -1000` |
| 7–11 | counts of conflicting overlords; GP vassal interference (depends on `world+0xB94`) | `-1000` or `+50` |
| 12 | `AI_GetBaseRelation` (`0x00834150`) | `+= relation` |
| 13 | per-pair pseudo-random term (`FUN_004FB6A0`), doubled if either is uncivilized | about `-25 .. -1` |
| 14 | accumulated vassal conflicts | `-10 × count` |
| 15 | common war/ally flag | `+20` |
| 16–19 | military strength ratios (`+0x7B4`) and an "interest" term | `+5 .. +20`, capped |
| 20 | global special modifiers (`world+0xB3C`) | `+= Σ` |
| 21 | any active reason in `self.relations[other]+0x50` | `-100` |
| 22 | same, reversed | `-10` |

## Action types in the switch

`0x332` -> raw opinion; `0x8CA` -> persuasion difficulty; `0x38D` -> always 100; `0x470` threshold on
the object; `0x897/0x898` -> 0/100 from `FUN_00911760`; `0x81E`, `0x8C6..0x8CB`, `0x8D5/0x8D6`,
`0x9FA/0x9FB`, `0xA02` -> dedicated functions (unmapped); default 0. The names of the action types are
unknown.

## Open questions

1. Names of the action type ids.
2. `AI_GetBaseRelation` and the pseudo-random term `FUN_004FB6A0`.
3. Which screen uses each of the three `Command_CanExecute_*` copies.
