# Allied edge: update order and rifles (2026-11-25)

**Question:** after the Farmstead side fix (`2026-11-25_map_bias`) the Allies won 48-30 of 78 decided bot
matches. Why?

All runs: `AIMatchBatch.ps1`, 4 squads a side, bots only, dice rolls, bleed-out on, 40 matches per row
(seeds 3000+ on the empty map, 5000+ on Farmstead). "Swap" = Axis start south. The empty map is Farmstead
with only its boundary walls, a throwaway test level (not committed). `data/` keeps every match summary;
per-shot logs only for `simF*`.

## 1. Update order (fixed in code, GDD v0.56)

The server updated soldiers one at a time each tick, Allies first (placed in the level first). A rifle hit
took effect at once, so when two soldiers fired at each other in the same tick, the Allied shot could kill
before the Axis soldier fired. Test: give the Gewehr 43 the Garand's exact numbers (identical rifles).

| Identical rifles, empty map | Normal (Allies south) | Swap (Axis south) | Pooled Allies-Axis |
|---|---|---|---|
| Hits at once (old) `sameE*` | 33-7 | Allies 18-22 | **51-29** |
| Hits at end of tick (new) `simE*` | 23-17 | Allies 19-21 | **42-38** |

Fix: rifle hits now take effect at the end of the server tick (`squad.SimultaneousShots`, default 1). Two
soldiers that fire on the same tick both land their shots.

## 2. The rifles (not changed: GDD numbers)

With the fix, real rifles (`simF*`, `simRealE*`):

| Map | Allies-Axis | South-North |
|---|---|---|
| Farmstead (fair layout) | 44-34 (2 timeouts) | 41-37 |
| Empty map | **67-13** | 40-40 |

On Farmstead the gap (56%) is within chance of even (expected 39 ± 4.4). On open ground it's large. Which
stat? The Gewehr 43 given one Garand value at a time, empty map (`x_*`, 80 matches each):

| Gewehr 43 change | Allies-Axis |
|---|---|
| none | 67-13 |
| aim time 0.45 → **0.40 s** | **48-32** |
| short-range accuracy 80 → 85% | 60-20 |
| fire rate 38 → 40 rounds/min | 63-17 |

- **Aim time is the main lever.** Bots stop and open fire at about the same moment, so 0.05 s more aim
  decides who lands the first shot, and hits at 5 m or less kill outright. The Garand's 85% short-range
  accuracy adds to it. The Gewehr 43's advantages (mid and long range accuracy, 10-round magazine, faster
  reload) rarely come into play in these short open fights.
- The edge depends on the "whoever stops and shoots first wins" tempo (2026-11-23 review, finding 2). Bots
  fight in the open far more than players would.

## Decision: equal aim times (GDD v0.57)

The Gewehr 43's aim time is now 0.40 s, like the Garand's (option 1 below, matching the faster rifle so fights
don't slow down). Farmstead, 40 + 40 matches, seeds 5000+ (`aimF*`):

| | Allies-Axis | South-North | Timeouts |
|---|---|---|---|
| Before (`simF*`) | 44-34 | 41-37 | 2 |
| Equal aim times (`aimF*`) | 43-36 | 38-41 | 1 |

On Farmstead the change is within noise (it was already close); on open ground it cuts the edge from 67-13 to
48-32 (`x_aim*`). The rest of the open-ground edge comes from the Garand's short-range accuracy and fire rate.

## Options considered

1. **Same aim time for both rifles** (e.g. both 0.4 s or both 0.425 s). Biggest effect; keeps the other trade-offs.
2. **Narrow short-range accuracy** (e.g. Garand 83 / G43 82).
3. **Leave the rifles** until the tempo fix (a penalty for firing right after stopping or at a mover) and
   the combat-model choice, since both change how much first-shot timing matters. Re-run this test then.
