# Hit-chance rolls vs projectile combat: firing range + 4v4 AI matches (2026-11-23)

**Branch:** `ballistics-prototype`, GDD v0.52. Projectile model at its v3 defaults (16 mrad spread, braced head 18 cm over cover).
Full tables: [tables.md](tables.md). Raw data: `data/` (`summary.json` holds every number below).

## What ran

| Part | How | Size |
|---|---|---|
| Firing range, hit rate | `squad.DemoCombat hitrate`, 4 server processes pooled. Each lane tested with rolls and then projectiles, shooter standing and prone. Target invulnerable, holding its stance, stress held at 0. | 1,024 trials, 46,560 rounds (240 per cell) |
| Firing range, engagements | `squad.DemoCombat engage`: first hit injures (the target drops prone where it is), the next kills; ≤5 m kills outright. 45 s limit. Shooter standing. | 1,840 engagements (40 per cell) |
| AI matches | `AIMatchBatch.ps1 -Matches 40 -SeedBase 3000 [-Ballistics]`, 4v4 Farmstead, bots only | 40 + 40 matches |

Re-run: `./Scripts/CombatModeDemo.ps1 -Rounds 60 -Reps 40 -HitRateProcesses 4` (the comparison script pools the processes), the two AI batches, then
`python -I Scripts/Tests/compare_combat_modes.py --demo <folder> --rolls <folder> --projectile <folder>`.

Lanes are straight on (shooter square in front of the cover) and come from the Farmstead map: no tree lanes, no window lanes past 20 m, no hay lane at 40 m.

## Headline numbers

### Hit rate, shooter standing (firing range, rolls / projectile)

| Target | 4 m | 8 m | 12 m | 16 m | 20 m | 25 m | 30 m | 40 m |
|---|---|---|---|---|---|---|---|---|
| Open, standing | 78 / 100 | 86 / 80 | 68 / 57 | 60 / 51 | 65 / 36 | 31 / 22 | 36 / 16 | 34 / 11 |
| Open, prone | 83 / 100 | 88 / 75 | 61 / 38 | 64 / 26 | 60 / 16 | 35 / 11 | 37 / 4 | 33 / 2 |
| Hay bale, braced | 58 / 73 | 55 / 52 | 35 / 39 | 35 / 35 | 35 / 29 | 8 / 15 | 3 / 13 | - |
| Low wall, braced | 35 / 86 | 35 / 45 | 16 / 26 | 16 / 18 | 15 / 15 | 0 / 7 | 0 / 5 | 0 / 5 |
| Window, braced | 46 / 86 | 40 / 49 | 28 / 34 | 22 / 18 | 23 / 12 | - | - | - |

Prone behind hay or a low wall: **no line of sight from the front at any range, in both models.**

### Out of cover vs in cover (pooled, shooter standing)

| Range | Open: rolls | Open: projectile | Braced in cover: rolls | Braced in cover: projectile | Protection rolls | Protection projectile |
|---|---|---|---|---|---|---|
| 4 m | 81% | 100% | 46% | 82% | 43% | 18% |
| 8 m | 86% | 80% | 43% | 49% | 49% | 39% |
| 12 m | 65% | 51% | 26% | 33% | 60% | 36% |
| 16 m | 62% | 42% | 25% | 24% | 60% | 42% |
| 20 m | 64% | 29% | 24% | 18% | 62% | 36% |

Protection = share of the open-ground hit rate the cover takes away.

### Injuries vs kills

- **Out of cover the two models match.** Of all hits in the AI matches: injured 49% / 50%, killed outright (≤5 m) 9% / 11%, finished off 41% / 39% (rolls / projectile).
- **In cover, more first hits only injure** (rolls 71%, projectile 60%; small samples: 28 and 82 hits). Bots rarely fire at covered targets.
- **A braced soldier that gets injured behind cover can't be finished from the front.** In none of the 800 engagements from 8 m or more against hay, low wall or window targets was the injured target killed, in either model: once it drops prone it is out of sight. Only ≤5 m (outright kills) finished them.
- Open-ground targets: rolls finished every engagement at every range. Projectiles finished 88% (standing) and 80% (prone) at 25 m, and 58% at 35 m within 45 s. Median time to finish a prone target at 25 m: 6.5 s with rolls, 24.3 s with projectiles.
- Fate of injured soldiers in AI matches: finished by rifle 83% / 75%, treated with a medkit 5% / 9%, still lying injured at the end 12% / 15%.

### AI matches (40 each)

| | Rolls | Projectile |
|---|---|---|
| Allies / Axis / no result | 30 / 4 / 6 | 27 / 2 / 11 |
| Timeouts (injured-only stalemates) | 6 | 11 |
| Median length, decided matches | 50 s | 77 s |
| Hit rate, all shots | 68% | 52% |
| Shots per rifle kill | 2.9 | 3.9 |
| Hit rate 12-20 m, out of cover | 68% | 39% |
| Hit rate 30-40 m, out of cover | 33% | 4% |
| Medkits used per match | 1.1 | 2.3 |
| Friendly-fire hits per match | n/a | 0.5 |
| Rounds stopped by cover | n/a | 23% |
| Mean peak stress | 7 | 13 |

## Takeaways

1. **The shown chance is honest in both models.** Firing-range gaps between shown and actual are within ±1.6 points for every set-up. In AI matches the projectile estimate is slightly high only at 30-40 m (8% shown, 4% actual).
2. **Range matters far more with projectiles.** Rolls are flat inside each band (about 65% from 8 to 20 m) and then drop sharply at 20 m. Projectiles fall smoothly with range: 100% at 4 m, 51% at 16 m, 11% at 40 m against a standing target.
3. **Stance and posture matter with projectiles, not with rolls.** A prone target is about half as easy to hit at 16-20 m (26% vs 51%). A prone shooter hits more (open standing target at 20 m: 36% standing, 60% prone). With rolls neither stance changes anything.
4. **Cover protects less, in relative terms, with projectiles** (about 36-42% of the open-ground rate removed, against 60% for rolls at 12-20 m). This is partly because projectile fire in the open is already weaker. In absolute terms, braced soldiers at 12-20 m get hit about as often in both models.
5. **Up close, projectiles make cover nearly useless** (braced behind a low wall at 4 m: 86% vs 35%), because the head and shoulders are a big target at that range. The exception is a **prone shooter 4 m from a low wall or window: 0% hits.** Nothing of the target is exposed to the muzzle and the estimate correctly shows 0%, so automatic targeting skips it. An explicit attack order still fires every round into the wall, though, because the soldier has line of sight. Players will read that as a bug.
6. **Low walls beyond 20 m:** rolls make a braced soldier completely safe (0%), while projectiles still allow 5-7% head hits.
7. **Injured-only stalemates are a rule problem, not a model problem.** Both models fail to finish injured soldiers behind cover. Projectiles add more of them because long-range kills in the open take longer too (timeouts 6 → 11 of 40).
8. **Stress still does nothing** in AI matches with either model (more than 99% of time alive is Calm), so stress-driven spread hasn't been tested in play yet.

## Fixed during this review

Projectile match records wrote the target's stance and "moving" flag **after** the hit was applied. Every injuring hit was logged as "prone, still", so hits on standing or moving targets looked like 0%. Stance, movement and "already injured" are now taken before the hit (`FShotTargetSnapshot`). The earlier review (`2026-11-23_ballistics_prototype`) used no stance tables, so its numbers stand.
