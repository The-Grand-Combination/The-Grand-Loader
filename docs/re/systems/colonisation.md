# Colonisation: colonial range

How the game decides that a country reaches a province with its colonies. Found for the Script
Extender's AI dismantler (2026-11-01), which lets the AI take a colonial state only within the game's
colonial range. Names and addresses in `symbols/` (`Country_HasColonialRangeTo`, `Country_CanColonizeProvince`,
`Province_Distance`, `ProvinceSource_PairDistance`, `g_navalBaseIndex`, struct rows `CCountry+0xA38`,
`CCountry+0x10C`, `CProvince+0x58`, `+0xDC`, `+0xE0`, `+0x15C`, `+0x164`, `ProvinceSource+0x2AA4..+0x2AB8`).

## Summary

- There is no stored "colonial range" per country. It is computed on demand from the naval bases:
  a province is in range when one of the country's naval bases is within
  `100 + effective level x colonial_range` of it, `colonial_range` being the naval base building's
  field of `buildings.txt` (`CBuilding+0xDC`, x1000), the level `CProvinceBuilding+0x24` (x1000). **[C]**
- Distances come from a precomputed table of the province source, in map units x1000. **[I]**
- The colonial map mode (`MapColor_Colonial`) and `Country_CanColonizeProvince` use the same check, the
  latter only for coastal targets and after the adjacency and colonisation-state tests. **[C]**

## `Country_HasColonialRangeTo` (`0x004BC020`) **[C]**

`EAX = CountryRef* {tag, index}`, stack `(CProvince* target)`, `RET 4`, returns AL. Callers:
`Country_CanColonizeProvince` (`0x004BBF42`) and `MapColor_Colonial` (`0x00801F25`).

For each node of the country's province list at `country+0xA38` (nodes `{province, ?, next}`):

1. skip a blockaded province (`+0x170`) and one the country does not control (`+0x134 != +0x12C`);
2. `Province_Distance(country+0x10C, &d, province+0x58)` with `EAX = target+0x58` (`+0x58` is the
   province id): `d` in map units x1000; skip when `d <= 0`;
3. the naval base: `[[province+0x118] + g_navalBaseIndex*4]` (`CProvinceBuilding*`); its type's
   `colonial_range` (`CBuilding+0xDC`, x1000); when `<= 0` the range is 0;
   else `range = effectiveLevel(x1000) x colonial_range(x1000) / 1000 + 100000` (`__alldiv`), that is
   `100 + level x colonial_range` in map units x1000;
4. true when `d <= range`.

False when no province qualifies. Base range 100 even at level 0 of a province that has the building
entry, since every province has an entry per building (the vector is indexed by `CBuilding+0x134`).

## `Province_Distance` (`0x00456700`) **[I]**

`EAX = to id`, stack `(cache, int* out, from id)`, `RET 0xC`. First `ProvinceSource_PairDistance(from,
&out, to, &known)` (`EDI = g_provinceSource`, thiscall-like with the ids in ECX / stack); when the pair is
known and `cache` (the country's object at `+0x10C`, a map keyed by province id, `std::map` lookup
`0x0093D080`) holds an entry for `to` whose `+0x14` is not `to`, the entry's `+0x10` replaces the distance.
Else the table's value. The cache's meaning is open (a per-country distance override, canals?).

## `ProvinceSource_PairDistance` (`0x0049B7F0`) **[I]**

`EDI = g_provinceSource`; provinces at `+0x2AA4` (array of `CProvince*` by id). Each province has a
distance-node index at `+0xE0` (−1: none, then the node of the province at `+0xDC` is used). With both
nodes, the pair index into a triangular table: `(n − 1) x lo − lo(lo − 1)/2 + hi` with `n` at `+0x2AB0`;
the distance is `u16` at `+0x2AB4` x 1000, the "known" flag a byte at `+0x2AB8`. Without a node on either
side the result is a large constant (unreachable).

## `Country_CanColonizeProvince` (`0x004BBD60`) **[I]**

`EAX = CountryRef*`, stack `(CProvince*)`, `RET 4`. In order: the province is land (`geo+0x2A`); the
colonisation state (`province+0x15C` → `CColonizationData`, `+0x164`) allows it; the country's colonial
points (`country+0xBCC` → `+0x54 + +0x294` against `FUN_004CB480` x 1000) suffice; the province has no
owner; the country is a great or secondary power (`Country_GetRankClass` 1 or 0); the crisis and the
colonisation data's own tests; then true when the province borders the country's land (`FUN_004B7F50`
over the region's provinces, `FUN_004B7FD0`), else for a coastal province (`+0x158`) when
`Country_HasColonialRangeTo`.

## Use by the Script Extender

`treaty.cpp` `in_colonial_range` calls `Country_HasColonialRangeTo` with a `CountryRef` built from the
country's tag (`country+0x1C`) and index; `colonial_range_reaches` asks it for every coastal province of
the states a term moves. An AI dismantler takes a colonial state only when that is true (land of
interest apart); a colony that borders its land passes the border rule before this one, as in the game.
Reads only: deterministic on every machine.

## Open

- What the country's object at `+0x10C` caches (`Province_Distance`).
- Whether the distance table covers all provinces or only ports and their neighbours (`+0xE0` = −1 case).
