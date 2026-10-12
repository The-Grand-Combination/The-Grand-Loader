# `skip_clean_resets`

**Default 1.** Skips a daily per-country table reset when it would write what is already there. Same
results by construction. Source: `patch/src/features/performance/country_prepass.cpp`.

## Why

`Country_PrePass` runs for all 683 tags every day, alive or not. Early on it calls `FUN_0051FD10(country)`
(`0x0050861B`), which resets two per-country tables to a constant pair (`DAT_013F3688/C`):
`country+0x1514` (goods × 24 bytes) and the matrix `country+0x14F4` (types × goods × 24 bytes), about
30 KB per tag, ~20 MB of memory writes a day. In practice the tables already hold that value: in the
first measured year **every** reset (245 880 of 245 880) found them clean, alive tags included.

## How

A thunk on the call checks, before calling, that both tables have the sizes the function expects and
that every entry already holds the reset pair; then the call is skipped (it would change nothing).
Otherwise the original runs. The check is a sequential read of the same memory the function would
write, which saves the write traffic but not the scan itself.

The same thunk also helps the rest of `Country_PrePass` (these parts follow their own options):

- with `prefetch_relations`: for tags **without** provinces, prefetch relation `+0x2C/+0x30` with every
  tag. The loop at `0x00508DA7` reads them for every tag, but the existing relation prefetch only ran for
  tags with provinces, so ~70 % of `Country_PrePass`'s own time was still these misses (late game: ~525
  dead tags × 683 relations a day);
- with `prefetch_pops`: for tags with provinces, prefetch their pops for `FUN_0050B9B0`'s pop walk.

## Result

First year (1836): 125 ms/day against the best previous 124 (noise); the gain is expected later in the
game, when most tags are dead. Late-game measurement pending.
