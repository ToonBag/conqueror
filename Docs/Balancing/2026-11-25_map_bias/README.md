# Farmstead side bias: diagnosis and fix (2026-11-25)

**Problem** (finding 1 of `2026-11-23_farmstead_4v4_ai`): the south start side won 31 of 33 decided 4v4 bot
matches, whichever faction started there. **Fix:** Farmstead's north half is now a mirror image of its south
half (GDD v0.55, §10.5, built by `Scripts/Maps/mirror_farmstead.py`).

All runs: `AIMatchBatch.ps1`, 4 squads a side, bots only, rolls (no `-Ballistics`), bleed-out on (v0.54).
"Swap" = `-SwapSides` (Axis start south). Raw records in `data/<run>/`.

## Diagnosis

Throwaway test levels (not committed), all with the same DZs, VPs and squads as Farmstead:

| Level | Matches | South-North | Allies-Axis | Timeouts |
|---|---|---|---|---|
| Farmstead before (2026-11-23 review, normal + swapped) | 40 | **31-2** | 18-15 | 7 |
| Empty: boundary walls only (`mirrorE`, `mirrorE_swap`) | 40 | 20-20 | **34-6** | 0 |
| North half mirrored south (`mirrorN`, `mirrorN_swap`) | 80 | 38-26 | **48-16** | 16 |
| South half mirrored north (`mirrorS`, `mirrorS_swap`) | 80 | 42-35 | 34-43 | 3 |

- **The side bias was the map.** On every symmetric level it disappears or nearly so; it doesn't follow a
  faction. No north/south asymmetry in code was found (extra-squad placement, bot camera, threat cells are
  symmetric).
- **A faction bias appears on open ground.** On the empty level the Allies won 34-6 from either side. The
  M1 Garand aims faster (0.4 s vs 0.45 s), fires faster (40 vs 38 rounds/min) and is more accurate at short
  range (85% vs 80%, GDD §6), so it wins first-shot duels between soldiers that stop at the same moment.
  The more open the ground (empty, then mirrorN), the stronger the effect.
- Mirroring the south half was fair on both counts and rarely stalled, so it was chosen.

## Result: Farmstead after the fix

| Run | Matches | South-North | Allies-Axis | VP wins | Timeouts | Median length |
|---|---|---|---|---|---|---|
| `fair_normal` (Allies south), seeds 5000+ | 40 | 26-14 | 26-14 | 2 | 0 | 96 s |
| `fair_swap` (Axis south), seeds 5000+ | 40 | 16-22 | 22-16 | 2 | 2 | 100 s |
| **Pooled** | 80 | **42-36** | **48-30** | 4 | 2 | |

- Side: 42-36 of 78 decided is within chance of even (expected 39 ± 4.4).
- Faction: Allies 48-30 (62%). Smaller than on open ground, but probably real. Not changed here: the rifle
  numbers are GDD §6 values, and this is mostly the "whoever stops and shoots first wins" tempo
  (finding 2 of the 2026-11-23 review). Worth deciding together with that and the combat-model choice.

## Level changes

- Cover on cell (X, Y) now equals cover on (X, 63 - Y). Pieces crossing the centre line were merged with their
  mirror copies (outer low walls at columns 0, 16, 63; window walls at columns 29 and 57).
- VP B: a walled yard with window walls and a doorway (32-34) on rows 30 and 33. VP C: a walled room with a
  doorway (53-54) and windows on rows 28 and 35. The farmhouse, the barn's north rooms and the ponds are gone.
- Weakened walls: (36, 30), (36, 33), (56, 28), (56, 35), one beside each doorway (the two old ones were both
  in the north half).
- Floors (roads, building floors) mirrored too. Checked on a Play As Client client: everything draws.
