# Factory construction

Sources: `Economy/factory-construction.md` and the V2DLL table in `Patch/patch.md`, in
[maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering).
Every patch site below has its original bytes verified against 3.04 in this repo.

## Summary

Whether a country can build a factory is decided by several independent gates, each with its own
civilization check. That is why removing one check is never enough.

```text
Factory_CanBuildNow (0x0052C9B0)                          the real gate
├─ Factory_CanBuild (0x0052E960)                          also drives build_factory_button
│    civilized (country+0x12D0)       JZ  at 0x0052E96E
│    not colonial (state+0x84 <= 0)   JG  at 0x0052E977
│    factories < 8 (state+0x68)
│    Factory_CheckProductionCountry (0x0052FB40)
│    country+0xB20 / +0xB60 permissions
├─ Factory_CanBuildTypeInState (0x0052CA30)
│    civilized                        JNZ at 0x0052CA3E
│    state buildings (+0x60 -> +0x18, +0x20, +0x224), country+0xBCC +0x2FC, state+0x130
│    ProductionType_CanUse (0x004D04B0)
└─ Factory_CheckCost (0x004DA720)
```

`Factory_CheckProductionCountry` checks a **second** country tied to the production type
(`[EAX+0x31] == 0`, `[EAX+0x12D0] != 0`, `[EAX+0xB60] != 0`, record `+0x34 == 0`); its civilization
test is not the builder's and should not be removed with the others. **[C]** layout, **[?]** meaning.

## Known patches (from V2DLL, all verified on 3.04)

| patch site | effect |
|---|---|
| `v2dll_build_factory_ignore_uncivilized_button` | skip the civilization test of `Factory_CanBuild` |
| `v2dll_build_factory_button_ignore_colonial` | skip the colonial test of `Factory_CanBuild` |
| `v2dll_build_factory_ignore_uncivilized_can_build` | `JNZ` -> `JMP` in `Factory_CanBuildTypeInState` |
| `v2dll_build_factory_checklist_uncivilized_own` / `_other` | the build checklist |
| `v2dll_build_factory_ignore_colonial_1` / `_2`, `v2dll_local_supply_factory_ignore_colonial` | colonial checks elsewhere |

V2DLL pairs the uncivilized patches with a code cave, `CivilizeNullCheck` (hook at RVA `0x14248B`,
VA `0x0054248B`, 7 bytes). Without it the game can crash when a country with factories becomes
civilized. Its bytes are not documented.

A miscalculated colonial-supply patch once started at the `JLE` instead of the `CMP` and broke other
branches sharing the same `return false`. Always patch whole instructions and check what else jumps
to the target.

## Open questions

1. What `CivilizeNullCheck` guards (the null pointer and where it comes from).
2. Meaning of the second country in `Factory_CheckProductionCountry`.
