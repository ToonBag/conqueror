# Combat models compared: hit-chance rolls vs projectiles

## 1. Firing range (controlled)

Same lane, same soldiers, both rule sets back to back: Allied soldier 1 (M1 Garand) fires at Axis soldier 1, who holds fire. Stress held at 0 on both. Lanes are straight on: the shooter is square in front of the cover. 1024 hit-rate trials (46560 rounds) and 1840 engagements.

### Hit rate by range, shooter standing

Each cell: **rolls / projectile** hit rate (240 rounds each). `nl` = no line of sight (can't fire), `-` = no lane on the map.

| Target | 4 m | 8 m | 12 m | 16 m | 20 m | 25 m | 30 m | 40 m |
|---|---|---|---|---|---|---|---|---|
| Open, standing | 78 / 100 | 86 / 80 | 68 / 57 | 60 / 51 | 65 / 36 | 31 / 22 | 36 / 16 | 34 / 11 |
| Open, crouched | 82 / 100 | 83 / 85 | 67 / 60 | 62 / 47 | 66 / 33 | 36 / 22 | 42 / 22 | 34 / 8 |
| Open, prone | 83 / 100 | 88 / 75 | 61 / 38 | 64 / 26 | 60 / 16 | 35 / 11 | 37 / 4 | 33 / 2 |
| Hay bale, braced | 58 / 73 | 55 / 52 | 35 / 39 | 35 / 35 | 35 / 29 | 8 / 15 | 3 / 13 | - / - |
| Hay bale, prone | nl / nl | nl / nl | nl / nl | nl / nl | nl / nl | nl / nl | nl / nl | - / - |
| Low wall, braced | 35 / 86 | 35 / 45 | 16 / 26 | 16 / 18 | 15 / 15 | 0 / 7 | 0 / 5 | 0 / 5 |
| Low wall, prone | nl / nl | nl / nl | nl / nl | nl / nl | nl / nl | nl / nl | nl / nl | nl / nl |
| Window, braced | 46 / 86 | 40 / 49 | 28 / 34 | 22 / 18 | 23 / 12 | - / - | - / - | - / - |
| Window, standing | 40 / 90 | 46 / 55 | 28 / 31 | 31 / 15 | 23 / 10 | - / - | - / - | - / - |

### Hit rate by range, shooter prone

Each cell: **rolls / projectile** hit rate (240 rounds each). `nl` = no line of sight (can't fire), `-` = no lane on the map.

| Target | 4 m | 8 m | 12 m | 16 m | 20 m | 25 m | 30 m | 40 m |
|---|---|---|---|---|---|---|---|---|
| Open, standing | 88 / 100 | 86 / 96 | 64 / 78 | 68 / 66 | 68 / 60 | 37 / 42 | 35 / 35 | 35 / 25 |
| Open, crouched | 85 / 100 | 82 / 98 | 65 / 83 | 71 / 67 | 62 / 47 | 30 / 44 | 38 / 32 | 35 / 27 |
| Open, prone | 84 / 100 | 87 / 92 | 69 / 68 | 65 / 40 | 65 / 31 | 37 / 20 | 37 / 17 | 34 / 9 |
| Hay bale, braced | 48 / 68 | 53 / 66 | 37 / 50 | 35 / 46 | 31 / 32 | 3 / 28 | 6 / 23 | - / - |
| Hay bale, prone | nl / nl | nl / nl | nl / nl | nl / nl | nl / nl | nl / nl | nl / nl | - / - |
| Low wall, braced | 33 / 0 | 40 / 57 | 18 / 40 | 15 / 29 | 13 / 21 | 0 / 10 | 0 / 6 | 0 / 2 |
| Low wall, prone | nl / nl | nl / nl | nl / nl | nl / nl | nl / nl | nl / nl | nl / nl | nl / nl |
| Window, braced | nl / nl | 39 / 66 | 23 / 38 | 25 / 24 | 22 / 20 | - / - | - / - | - / - |
| Window, standing | 41 / 0 | 42 / 67 | 29 / 37 | 26 / 27 | 21 / 20 | - / - | - / - | - / - |

### Out of cover vs in cover (pooled, shooter standing)

Out of cover = open standing / crouched / prone pooled; in cover = braced behind hay, low wall or window pooled. Only ranges where every pooled set-up has a lane.

| Range | Out of cover: rolls | Out: projectile | In cover: rolls | In: projectile | Cover protection rolls | Cover protection projectile |
|---|---|---|---|---|---|---|
| 4 m | 81.1% | 100% | 46% | 81.8% | 43% | 18% |
| 8 m | 85.6% | 80% | 43.3% | 48.8% | 49% | 39% |
| 12 m | 65.3% | 51.4% | 26% | 32.9% | 60% | 36% |
| 16 m | 61.9% | 41.5% | 24.6% | 24% | 60% | 42% |
| 20 m | 63.5% | 28.5% | 24% | 18.3% | 62% | 36% |
| 25 m | 34% | 18.2% | - | - | - | - |
| 30 m | 38.2% | 14% | - | - | - | - |
| 40 m | 33.6% | 7.1% | - | - | - | - |

_Cover protection = how much of the open-ground hit rate the cover takes away._

### Does the shown hit chance match? (shooter standing)

| Target | Mode | Mean shown | Actual | Gap (actual - shown) |
|---|---|---|---|---|
| Open, standing | Rolls | 58.8% | 57.1% | -1.6 |
| Open, standing | Projectile | 45.7% | 46.6% | +0.9 |
| Open, crouched | Rolls | 58.8% | 59.0% | +0.2 |
| Open, crouched | Projectile | 47.7% | 47.2% | -0.5 |
| Open, prone | Rolls | 58.8% | 57.6% | -1.1 |
| Open, prone | Projectile | 34.8% | 34.0% | -0.7 |
| Hay bale, braced | Rolls | 32.1% | 32.6% | +0.4 |
| Hay bale, braced | Projectile | 35.2% | 36.5% | +1.3 |
| Low wall, braced | Rolls | 14.4% | 14.5% | +0.2 |
| Low wall, braced | Projectile | 26.2% | 25.8% | -0.3 |
| Window, braced | Rolls | 33.0% | 31.8% | -1.2 |
| Window, braced | Projectile | 40.0% | 39.8% | -0.2 |
| Window, standing | Rolls | 33.0% | 33.6% | +0.6 |
| Window, standing | Projectile | 40.0% | 40.1% | +0.1 |

### Projectile model: where the rounds go (all ranges, shooter standing)

| Target | Hit | Stopped by cover | Missed past | Hits on head | chest | belly | hips/legs |
|---|---|---|---|---|---|---|---|
| Open, standing | 47.0% | 11.0% | 42.0% | 3.0% | 20.0% | 55.0% | 22.0% |
| Open, crouched | 47.0% | 17.0% | 36.0% | 3.0% | 30.0% | 47.0% | 20.0% |
| Open, prone | 34.0% | 17.0% | 49.0% | 56.0% | 41.0% | 1.0% | 2.0% |
| Hay bale, braced | 37.0% | 33.0% | 31.0% | 3.0% | 14.0% | 48.0% | 35.0% |
| Low wall, braced | 26.0% | 44.0% | 30.0% | 41.0% | 59.0% | 0.0% | 0.0% |
| Window, braced | 40.0% | 56.0% | 4.0% | 45.0% | 55.0% | 0.0% | 0.0% |
| Window, standing | 40.0% | 57.0% | 3.0% | 44.0% | 56.0% | 0.0% | 0.0% |

### Engagements: injure, then finish (shooter standing)

The target is shot for real (§5.1): the first hit injures it and it drops prone where it is; the next hit kills. A hit from 5 m or closer kills outright. Up to 45 s per engagement; "finished" = killed in that time. "Injured, then hidden" = once prone it couldn't be seen any more.

| Target | Range | Mode | Finished | Injured, then hidden | Injured, time ran out | Never hit | Median rounds to injure | Median s to injure | Median rounds to kill | Median s to kill |
|---|---|---|---|---|---|---|---|---|---|---|
| Open, standing | 4 m | Rolls | 100.0% | 0.0% | 0.0% | 0.0% | 1 | 0.4 s | 1 | 0.4 s |
| Open, standing | 4 m | Projectile | 100.0% | 0.0% | 0.0% | 0.0% | 1 | 0.4 s | 1 | 0.4 s |
| Open, standing | 8 m | Rolls | 100.0% | 0.0% | 0.0% | 0.0% | 1 | 0.4 s | 2 | 1.9 s |
| Open, standing | 8 m | Projectile | 100.0% | 0.0% | 0.0% | 0.0% | 1 | 0.4 s | 2.5 | 2.7 s |
| Open, standing | 15 m | Rolls | 100.0% | 0.0% | 0.0% | 0.0% | 1 | 0.4 s | 3 | 3.4 s |
| Open, standing | 15 m | Projectile | 100.0% | 0.0% | 0.0% | 0.0% | 2 | 1.9 s | 4.5 | 5.7 s |
| Open, standing | 25 m | Rolls | 100.0% | 0.0% | 0.0% | 0.0% | 2 | 1.9 s | 6 | 8 s |
| Open, standing | 25 m | Projectile | 88.0% | 0.0% | 12.0% | 0.0% | 3.5 | 4.2 s | 11 | 18.5 s |
| Open, standing | 35 m | Rolls | 100.0% | 0.0% | 0.0% | 0.0% | 2 | 1.9 s | 4.5 | 5.7 s |
| Open, standing | 35 m | Projectile | 58.0% | 0.0% | 40.0% | 2.0% | 5 | 6.4 s | 11 | 18.4 s |
| Open, prone | 4 m | Rolls | 100.0% | 0.0% | 0.0% | 0.0% | 1 | 0.4 s | 1 | 0.4 s |
| Open, prone | 4 m | Projectile | 100.0% | 0.0% | 0.0% | 0.0% | 1 | 0.4 s | 1 | 0.4 s |
| Open, prone | 8 m | Rolls | 100.0% | 0.0% | 0.0% | 0.0% | 1 | 0.4 s | 2 | 1.9 s |
| Open, prone | 8 m | Projectile | 100.0% | 0.0% | 0.0% | 0.0% | 1 | 0.4 s | 2.5 | 2.7 s |
| Open, prone | 15 m | Rolls | 100.0% | 0.0% | 0.0% | 0.0% | 1 | 0.4 s | 3 | 3.4 s |
| Open, prone | 15 m | Projectile | 100.0% | 0.0% | 0.0% | 0.0% | 3 | 3.4 s | 6 | 7.9 s |
| Open, prone | 25 m | Rolls | 100.0% | 0.0% | 0.0% | 0.0% | 2 | 1.9 s | 5 | 6.5 s |
| Open, prone | 25 m | Projectile | 80.0% | 0.0% | 18.0% | 2.0% | 7 | 9.4 s | 15 | 24.3 s |
| Open, prone | 35 m | Rolls | 100.0% | 0.0% | 0.0% | 0.0% | 2 | 1.9 s | 6 | 7.9 s |
| Open, prone | 35 m | Projectile | 58.0% | 0.0% | 25.0% | 18.0% | 9 | 15.4 s | 16 | 25.9 s |
| Hay bale, braced | 4 m | Rolls | 100.0% | 0.0% | 0.0% | 0.0% | 2 | 1.9 s | 2 | 1.9 s |
| Hay bale, braced | 4 m | Projectile | 100.0% | 0.0% | 0.0% | 0.0% | 1 | 0.4 s | 1 | 0.4 s |
| Hay bale, braced | 8 m | Rolls | 0.0% | 100.0% | 0.0% | 0.0% | 1 | 0.4 s | - | - |
| Hay bale, braced | 8 m | Projectile | 0.0% | 100.0% | 0.0% | 0.0% | 1 | 0.4 s | - | - |
| Hay bale, braced | 15 m | Rolls | 0.0% | 100.0% | 0.0% | 0.0% | 2 | 1.9 s | - | - |
| Hay bale, braced | 15 m | Projectile | 0.0% | 100.0% | 0.0% | 0.0% | 3 | 3.4 s | - | - |
| Hay bale, braced | 25 m | Rolls | 0.0% | 72.0% | 2.0% | 25.0% | 8 | 10.9 s | - | - |
| Hay bale, braced | 25 m | Projectile | 0.0% | 92.0% | 2.0% | 5.0% | 5 | 6.4 s | - | - |
| Hay bale, braced | 35 m | Rolls | 0.0% | 68.0% | 5.0% | 28.0% | 8 | 10.9 s | - | - |
| Hay bale, braced | 35 m | Projectile | 0.0% | 90.0% | 0.0% | 10.0% | 8.5 | 13.2 s | - | - |
| Low wall, braced | 4 m | Rolls | 100.0% | 0.0% | 0.0% | 0.0% | 2 | 1.9 s | 2 | 1.9 s |
| Low wall, braced | 4 m | Projectile | 100.0% | 0.0% | 0.0% | 0.0% | 1 | 0.4 s | 1 | 0.4 s |
| Low wall, braced | 8 m | Rolls | 0.0% | 100.0% | 0.0% | 0.0% | 2 | 1.9 s | - | - |
| Low wall, braced | 8 m | Projectile | 0.0% | 100.0% | 0.0% | 0.0% | 1 | 0.4 s | - | - |
| Low wall, braced | 15 m | Rolls | 0.0% | 98.0% | 0.0% | 2.0% | 4 | 4.9 s | - | - |
| Low wall, braced | 15 m | Projectile | 0.0% | 100.0% | 0.0% | 0.0% | 2 | 1.9 s | - | - |
| Low wall, braced | 25 m | Rolls | 0.0% | 0.0% | 0.0% | 100.0% | - | - | - | - |
| Low wall, braced | 25 m | Projectile | 0.0% | 75.0% | 5.0% | 20.0% | 8.5 | 13.1 s | - | - |
| Low wall, braced | 35 m | Rolls | 0.0% | 0.0% | 0.0% | 100.0% | - | - | - | - |
| Low wall, braced | 35 m | Projectile | 0.0% | 58.0% | 5.0% | 38.0% | 14 | 22.9 s | - | - |
| Window, braced | 4 m | Rolls | 100.0% | 0.0% | 0.0% | 0.0% | 1 | 0.4 s | 1 | 0.4 s |
| Window, braced | 4 m | Projectile | 100.0% | 0.0% | 0.0% | 0.0% | 1 | 0.4 s | 1 | 0.4 s |
| Window, braced | 8 m | Rolls | 0.0% | 100.0% | 0.0% | 0.0% | 2 | 1.9 s | - | - |
| Window, braced | 8 m | Projectile | 0.0% | 100.0% | 0.0% | 0.0% | 1 | 0.4 s | - | - |
| Window, braced | 15 m | Rolls | 0.0% | 100.0% | 0.0% | 0.0% | 3 | 3.4 s | - | - |
| Window, braced | 15 m | Projectile | 0.0% | 100.0% | 0.0% | 0.0% | 4 | 4.9 s | - | - |

## 2. AI test matches (4v4 Farmstead, bots only)

40 matches per rule set on the same seeds. Bots decide these fights, so this shows how each model plays under bot tactics.

| | Rolls | Projectile |
|---|---|---|
| Result (Allies / Axis / none) | 30 / 4 / 6 | 27 / 2 / 11 |
| Timeouts (stalemates) | 6 | 11 |
| Median match length | 53.9 s | 321 s |
| Median length, decided matches | 49.6 s | 76.9 s |
| Shots per match | 67.8 | 89.9 |
| Hit rate, all shots | 67.9 | 51.7 |
| Mean shown hit chance | 67.4 | 52.1 |
| Shots per rifle kill | 2.9 | 3.9 |
| Deaths per match (of 32) | 23.3 | 23.4 |
| Injuries per match | 22.9 | 24.6 |
| Still lying injured at the end, per match | 2.7 | 3.8 |
| Median injury -> death | 1.5 s | 1.6 s |
| Medkits used per match | 1.1 | 2.3 |
| Grenades thrown per match | 3.3 | 3.3 |
| Grenade kills per match | 0.1 | 0.1 |
| Rounds fired per soldier | 2.1 | 2.8 |
| Mean peak stress | 7.3 | 12.6 |
| Death causes | {'rifle': 928, 'grenade': 4} | {'rifle': 932, 'grenade': 3} |
| Friendly-fire hits per match | n/a | 0.5 |
| Rounds stopped by cover | n/a | 23.3% |

### Hit rate in and out of cover, by range

Cover = the grid cover the target had against the shooter (any type). Cells: hit rate (shots).

| Range | Open: rolls | Open: projectile | In cover: rolls | In cover: projectile |
|---|---|---|---|---|
| 0-5 m | 80.7% (306) | 97.1% (274) | 50.0% (4) | 100.0% (9) |
| 5-8 m | 82.7% (585) | 88.8% (498) | 60.0% (10) | 75.0% (24) |
| 8-12 m | 65.8% (360) | 67.4% (619) | 16.7% (18) | 66.7% (30) |
| 12-20 m | 68.4% (1099) | 39.4% (1496) | 37.2% (43) | 34.9% (86) |
| 20-30 m | 35.4% (158) | 25.7% (202) | 0.0% (12) | 20.0% (20) |
| 30-40 m | 33.0% (115) | 3.9% (309) | 50.0% (2) | 3.6% (28) |

### By target stance

| Target | Rolls | Projectile |
|---|---|---|
| prone | 65.6% (456) | 34.1% (1095) |
| prone moving | 68.3% (763) | 62.4% (710) |
| standing | 65.7% (391) | 53.1% (687) |
| standing moving | 69.4% (1102) | 61.6% (1103) |

### What a hit does: injured vs killed, in and out of cover

Killed outright = hit from 5 m or closer; finished = second hit on an already injured soldier.

| Where | Mode | Hits | Injured | Killed outright | Finished off |
|---|---|---|---|---|---|
| Out of cover | Rolls | 1814 | 49.0% | 9.0% | 41.0% |
| Out of cover | Projectile | 1778 | 50.0% | 11.0% | 39.0% |
| In cover | Rolls | 28 | 71.0% | 4.0% | 25.0% |
| In cover | Projectile | 82 | 60.0% | 7.0% | 33.0% |
| All | Rolls | 1842 | 50.0% | 9.0% | 41.0% |
| All | Projectile | 1860 | 51.0% | 10.0% | 39.0% |

### What happened to injured soldiers

| Fate | Rolls | Projectile |
|---|---|---|
| injured at end | 108 (12.0%) | 152 (15.0%) |
| killed grenade | 2 (0.0%) | 1 (0.0%) |
| killed rifle | 756 (83.0%) | 737 (75.0%) |
| other | 2 (0.0%) | 1 (0.0%) |
| treated | 46 (5.0%) | 93 (9.0%) |

### Shown chance vs actual, by range

| Range | Rolls shown | Rolls actual | Projectile shown | Projectile actual |
|---|---|---|---|---|
| 0-5 m | 82.4% | 80.3% | 99% | 97.2% |
| 5-8 m | 82.2% | 82.4% | 90.4% | 88.1% |
| 8-12 m | 63.8% | 63.5% | 66.9% | 67.3% |
| 12-20 m | 64.5% | 67.3% | 38.3% | 39.1% |
| 20-30 m | 35.5% | 32.9% | 22.9% | 25.2% |
| 30-40 m | 38.7% | 33.3% | 8.3% | 3.9% |

### Stress (share of time alive per tier: calm / minor / major / shocked)

- Rolls: [100.0, 0.0, 0.0, 0.0]  
- Projectile: [99.4, 0.4, 0.2, 0.0]

Projectile hits by body part: {'head': 12.0, 'chest': 34.0, 'belly': 39.0, 'hips': 13.0, 'thighs': 1.0, 'shins': 0.0}

