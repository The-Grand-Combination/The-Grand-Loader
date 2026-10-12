# AI national focus

How the AI scores a province for a national focus.

Sources: `AI/Ai_national_focus.md` in
[maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering).
The function is identified by its strings (`national_focus.cpp`, "Railroad score for", "Promotion score
for"); the formulas are the source's reconstruction and are not re-verified here.

## Summary

`AI_ScoreNationalFocus` (`0x00564770`): `EDI` = province, stack `(focus, out_score, context)`, result
written to `*out_score` (fixed point ×1000; −100000 prints as −100.0). Context is a pair of country
indexes.

```text
if province has no pops and the focus does not allow that:  score = -100000
pop_factor = 1000 - f(total pops)                  smaller provinces score higher
railroad focus (focus+0x54 > 0):
    -100000 if the state is colonial, -100000 more if it has no factory
    else 2000 + 1000 * (factories / 2), or -100000 if no province of the state can use it
for each promotion entry of the focus (focus+0x7C):   each one REPLACES the score
    soldiers      shortfall vs SOLDIER_FRACTION, x SOLDIER_WEIGHT x pop_factor
    bureaucrats   share x country ratio x ADMINISTRATOR_WEIGHT; x10 when small; x10 in the capital
    clergy        (MAX_CLERGY_FOR_LITERACY - clergy%) / MAX x EDUCATOR_WEIGHT x pop_factor
    workers       open job slots in non-closed factories below level 11, x INDUSTRYWORKER_WEIGHT
    capitalists   shortfall vs CAPITALIST_FRACTION; -100000 if the target country is uncivilized,
                  if a plurality threshold fails, or no factory outside the capital
    -100000 more in colonial states
```

Promotion entries assign rather than accumulate, so a focus with several entries would be scored by
its last one. Vanilla foci have one each. With `g_aiLogging` (`0x012586D7`) the scores are written to
the AI log.

## Open questions

1. Exact constants of the population factor (`0x00E456C0`, `0x00E45840`, `0x00E45660`).
2. The bureaucrat country metric (`FUN_00532EA0`).
