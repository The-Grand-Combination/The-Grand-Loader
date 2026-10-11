# Economy and world market

Sources: profiler levels 2–3 in this repo ([log.md](../log.md), 2026-10-03); `Economy/*.md` and
`Engine/conclusions.md` in
[maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering).
The price constants were re-read from the exe here, which corrected their meaning (see below).

## Summary

- `Economy_Update` (`0x00489990`, "worldmarket.cpp") runs once per day from `DayTick` and is the most
  expensive simulation block: ~37 ms/day in 1836 (TGC). It is `WorldMarketUpdate` ~21.6 ms +
  `ProcessMarketAndGoodsDistribution` ~8.7 ms + `Market_PostProcess` ~3.3 ms.
- Prices follow supply/demand through a multiplier and are clamped to `[0.2, 5.0] × base price`.
  The two factors are stored **pre-scaled to 2^15 fixed point** (`kPriceMinFactor` = 6554.1,
  `kPriceMaxFactor` = 163840.5), not as 0.2 / 5.0.
- Quantities are 64-bit fixed point (`value / 2^15`); stockpiles are vectors of 8-byte entries.
- The economy writes into shared accumulators everywhere; parallelising it needs private
  accumulators per thread and a merge (see [threads.md](threads.md)).

## Daily path

```text
DayTick
  Economy_Update (0x00489990)                         1 call/day, ~37 ms
    WorldMarketUpdate (0x00484060)                    ~21.6 ms
      for country: for state: for factory: Factory_MarketUpdate (0x00482FF0)
      per country: Market_UpdateCountryPops (0x00485E40)      pop money, needs, savings [I]
      0x00484309: CALL UpdateTimeThrottle             (removed by the external world_market_throttle patch)
    ProcessMarketAndGoodsDistribution (0x004808D0)    ~8.7 ms
    Market_PostProcess (0x00488080)                   ~3.3 ms
```

The other repo described `EconomyManager_UpdateFull` (`0x0068BF00`) as the economy update. Its only
caller is `Game_FinalizeLoadAndEnterGameplay` (`0x0066321D`) **[C]**: it is the full rebuild done once
after loading, which is also where its five `Economy_Update(..., 0..4)` passes come from. The daily
path is the one above: `Economy_Update` has two callers, `DayTick` (`0x00685F7E`) and
`EconomyManager_UpdateFull` itself (`0x0068BF71`).

```text
EconomyManager_UpdateFull (0x0068BF00)        once, after loading
  Economy_CalculateDistribution (0x00520150)   nested loops, shared 64-bit accumulators
  EconomyManager_RebuildLists (0x0068D250)
  Market_UpdateSupply (0x00482930) -> price update (0x00482B..)
  ProcessMarketAndGoodsDistribution (0x004808D0)
  EconomyManager_PrepareUpdate (0x0068D950)
  Economy_Update(..., 0..4)
  EconomyManager_ProcessEntries (0x0068DC70)
```

`Economy_CalculateDistribution` has 24 callers across the game (load, UI, diplomacy, events), so it is
a general recalculation and not part of the daily cost unless one of those callers runs daily.

## Prices

```text
supply, demand -> 64-bit compare -> SignedInt64Divide -> Market_CalcSupplyMultiplier (0x00482610)
               -> price = base * multiplier -> clamp [base * min, base * max]
```

The clamp loads `kPriceMinFactor` with `FLD qword [0x00E45C30]` at `0x00482C46` **[C]**.

| constant | VA | stored value | meaning |
|---|---|---|---|
| `kPriceMinFactor` | `0x00E45C30` | 6554.1 | 0.2 × 32768 + 0.5 |
| `kPriceMaxFactor` | `0x00E45C28` | 163840.5 | 5.0 × 32768 + 0.5 |

So a patch that wants 0.1 / 10.0 has to write 3277.3 / 327680.5 (or patch the load to its own
constant). Writing 0.1 / 10.0 directly, as earlier notes suggested, would make the bounds 32768 times
too small. Replacing the supply/demand logic itself was tried in the other repo and produced prices
pinned at ~0.01 or ~1000; changing only the bounds keeps vanilla behaviour inside them.

`Market_AddRealDemand` (`0x00487410`) is confirmed demand-side (`LEA EAX,[EDX+EAX*8]` into the
real-demand vector at `0x004877B1`). `Buffer_AccumulateIndexedValue` (`0x0047DC20`) adds a stockpile
entry into a market accumulator. `ScaledDotProduct` (`0x0047E3E0`, ~58 callers) and
`Buffer_ScaleValuesInRange` (`0x0047DE60`, ~53 callers) are generic, not market-specific.

## Stockpiles

`Stockpile_SubtractArtisanFactory` (`0x0047DCA0`) shows the layout **[C]**: a byte per good at
`holder+0x08+good` says whether the good is present; the amounts are a vector of int64 at
`holder+0x48/+0x4C`; the good count is `g_goodsCount` (`0x012587F4`). Missing entries are created with
the negated amount.

## Loans

`Loan_CalculateInterest` (`0x00523400`) applies `LOAN_BASE_INTEREST` and a minimum interest; the
external "no minimum interest" patch NOPs two blocks at `0x005237D2` / `0x005237DA`.

## Periodic report

`DayTick` keeps a two-slot history of 0x70-byte economic records (`g_econReportHistory` `0x012624E8`,
active slot `g_econReportSlot` `0x00F20BFC`) filled from several 64-bit accumulators. The categories
are not identified. **[I]**

## Open questions

1. Which of the 24 callers of `Economy_CalculateDistribution` run during play, and how often.
2. `FUN_00482FF0` (per factory) and `FUN_00485E40` (per country): split their ~21 ms.
3. Field map of the 0x70-byte report.
