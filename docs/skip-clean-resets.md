# `skip_clean_resets`

## Source

`patch/src/features/performance/country_prepass.cpp`. Research: [re/systems/performance.md](re/systems/performance.md).

## Why

`Country_PrePass` runs for all 683 tags every day, alive or not. Early on it calls `FUN_0051FD10(country)`
(`0x0050861B`), which resets two per-country tables to a constant pair (`DAT_013F3688/C`):
`country+0x1514` (goods × 24 bytes) and the matrix `country+0x14F4` (types × goods × 24 bytes), about
30 KB per tag, ~20 MB of memory writes a day. In practice the tables already hold that value: in the
first measured year every reset found them clean, alive tags included.

## How

A thunk on the call checks that both tables have the sizes the function expects and that every entry
already holds the reset pair; then the call is skipped, since it would change nothing. Otherwise the
original runs. The check reads the same memory the function would write, which saves the write traffic
but not the scan.

The same thunk serves two other options at that point of `Country_PrePass`: with `prefetch_relations`, it
prefetches relation `+0x2C/+0x30` for tags without provinces (the loop at `0x00508DA7` reads them for every
tag, but the relation prefetch before `Country_Recalculate` only runs for tags with provinces); with
`prefetch_pops`, it prefetches the pops of tags with provinces for `FUN_0050B9B0`'s pop walk.
