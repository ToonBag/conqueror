# AI test matches: 4v4 on Farmstead (2026-11-23)

**What ran:** 4 squads per team (16 soldiers a side, 32 total), every squad run by a bot (GDD §11.1), on a dedicated server with no clients. GDD v0.48 rules and default tuning: 300 s VP timer, standard squads, rifles only (bots don't get the test MG). 30-minute timeout. Recorded with `squad.MatchStats` (GDD §11.2).

| Run | Matches | Setup | Allies | Axis | No result |
|---|---|---|---|---|---|
| `run1_4v4` | 10 | Allies start south (normal) | 8 | 0 | 2 |
| `run2_4v4` | 10 | Same seeds as run1 (the sim isn't bit-exact, so these are fresh matches) | 8 | 0 | 2 |
| `run4_4v4_swapped` | 20 | `squad.SwapSides 1`: **Axis start south**, each side keeps its rifles | 2 | 15 | 3 |
| `run3_1v1` | 10 | 1 squad a side (the map's normal size), normal sides | 7 | 2 | 1 |

Raw records are in `data/`: one `.json` per match (result, per-soldier and per-team totals, events) and one `_shots.csv` (every shot). `tables_all_4v4.md` has the pooled tables for the 40 4v4 matches. To rebuild them:

```
"C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" -I Scripts/Tests/analyze_match_stats.py Docs/Balancing/2026-11-23_farmstead_4v4_ai/data/run1_4v4 Docs/Balancing/2026-11-23_farmstead_4v4_ai/data/run2_4v4 Docs/Balancing/2026-11-23_farmstead_4v4_ai/data/run4_4v4_swapped
```

## Caveats (read first)

- **The bots decide these numbers.** They run straight to VP cells and only fire once they stop, so most fights are meeting engagements in the open. Skilled players would use cover and stances far more. Treat this as "how the rules behave under simple tactics", and as a baseline to re-run after each change.
- **The map is too small for 4v4.** Farmstead is 64x64, the 2v2 size (§10.1). GDD scaling puts 3v3 at 128x128, so 32 soldiers here is very crowded. Its deployment zones are 8x8 for one squad, and four squads fill half of each.
- 40 matches is enough for the large effects below (31-2 is not chance), but not for fine differences of a few percent.

## Findings

### 1. The south start side wins almost every match (map bias). Severity: high

| | South side | North side | No result |
|---|---|---|---|
| 4v4, 40 matches | **31** | 2 | 7 |
| 1 squad a side, 10 matches | 7 | 2 | 1 |

Swapping the factions' start zones moves the wins with the side, so this is the **side, not the faction**. Pooled over both orientations the factions are even: Allies 18, Axis 15. The south side out-fights the north at all three VPs, most at B and A:

| Area | Shots south / north | Casualties caused south / north |
|---|---|---|
| A (orchard, west) | 402 / 265 | 259 / 158 |
| B (farmhouse, centre) | 533 / 317 | 374 / 204 |
| C (barn, east) | 352 / 269 | 241 / 209 |

The south side fires first in nearly every match (first shot about 12.7 s, north about 13.6 s) and fires about 1.6x as many shots. Both sides meet about 4 m from the VP centre line, so distance isn't the cause. Soldiers only auto-fire while standing still (§5.3), so the side whose soldiers reach a firing cell first wins the exchange. **Likely cause:** the VPs' surroundings aren't mirrored. VP B is "across the farmhouse's south doorway" (§10.5), and the cover cells the bot picks (cover facing the enemy) are probably nearer the south edge of A and B. Not yet ruled out: a north/south asymmetry in code (bot camera, pathfinding tie-breaks). **Next test:** a mirrored copy of Farmstead. If the bias goes away, it's the map.

### 2. Fights are over in seconds; whoever stops first wins. Severity: high

- Half of all deaths happen by **21 s** and **97% within the first 60 s**. First deaths at 14 s, right after first contact.
- **95% of shots** are at targets in no cover, with a mean hit chance of 69%. It takes **3.0 shots per kill** and two hits kill (§5.1).
- Hit chance by range: 0-5 m 81%, 5-15 m 73%, 15-30 m 59%, 30-50 m 37%. Half of all shots are at 5-15 m.
- Each soldier fires **2.2 rounds on average** (max 19 of 70). Survivors end with about 66 rounds. **Ammo is irrelevant** at this tempo.
- Low walls work: shots at targets behind one hit 15% of the time. Fights just rarely happen there.

**Balancing levers to try:** lower open-ground accuracy, or a penalty for firing at a moving target or right after stopping (the "stops first" effect). The test numbers don't say which; try each through the batch. This is also partly a bot issue: bots walk into the open in front of the enemy.

### 3. Matches can't end when only injured soldiers are left. Severity: medium (rules gap)

- **7 of 40** 4v4 matches (17.5%) hit the 30-minute timeout, with no shots after the first 2 minutes:
  - **5 of 7:** every soldier left on both sides was injured. Nobody can capture or contest (§10.3), and nobody is dead, so neither win condition (§11) can ever trigger.
  - **2 of 7** (seeds 1004, 1006): one side still had 2-3 able soldiers but never took the last VP. The bot only spreads over VPs within 30 m of its injured squad-mates (§11.1, cohesion §4.3), so the injured anchored them away from it. That's a bot problem, not a rules problem.
- 129 soldiers were still lying injured when their match ended. Bots treated only **6.5%** of injuries with a medkit (64 of 984). The bot sends a medic only within 12 m and not under fire, which rarely happens after these fights.
- **Design question:** should injured soldiers bleed out after some time, or should a match end when neither side has an able soldier left (or when one side has none)? §11 only covers annihilation (all **dead**). This needs a design decision; I haven't changed the rules.

### 4. Stress doesn't come into play. Severity: medium

- **4 of 1,280** soldiers ever reached 33 stress (Minor). Mean peak stress is 7.7. Only 5 of 2,818 shots had a stress accuracy penalty.
- 5 stress per incoming round (§8.2) means 7 rounds are needed to reach Minor. A soldier takes only about 2 before it's hit or the fight ends.
- If stress is meant to matter in short exchanges, `StressPerIncomingShot` probably needs to be about 3-4x higher, or near-misses and nearby casualties should add stress too. Re-test after fixing finding 2: slower fights give stress more time to build.

### 5. Grenades barely matter. Severity: low-medium

- **123 thrown of 6,400 carried** (1.9%), 3 per match. Mean throw 7.8 m, max 10 m.
- Per grenade: **0.01 kills, 0.99 stuns**. The 10% kill chance only applies within 1 m (§7.1). 7 of 123 grenades (5.7%) hit friendly soldiers.
- The bots throw only when rifle shots are poor (under 25%), and in these open fights they almost never are. Low use is mostly the bot. The low lethality is the rules (§7.1 numbers).

### 6. Factions and rifles are balanced

- Both rifles average **66.7%** hit chance per shot taken. M1 Garand: 1,459 shots, 68.0% hit. Gewehr 43: 1,359 shots, 67.7% hit.
- Kills follow side, not faction (finding 1). Nothing to change here at these ranges.
- The hit roll matches the odds: mean hit chance 66.7%, actual hit rate 67.8% over 2,818 shots.

## Suggested next steps

1. Mirrored Farmstead copy and re-run, to confirm finding 1 (map vs code).
2. A design decision on the injured stalemate (finding 3), and a bot fix so able soldiers go for the last VP even when injured squad-mates hold them back.
3. A 128x128 map for 3v3+/4v4 (§10.1) before trusting 4v4-specific numbers.
4. Try the levers from findings 2 and 4 one at a time, re-running `Scripts/AIMatchBatch.ps1 -Matches 20` (and `-SwapSides`) after each, and compare with these tables.
