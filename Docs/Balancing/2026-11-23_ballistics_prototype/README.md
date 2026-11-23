# Projectile combat prototype vs hit-chance rolls: 4v4 on Farmstead (2026-11-23)

**What ran:** the `ballistics-prototype` branch (GDD v0.51 §16), 4 squads a side, bots only, dedicated server with no clients, 20 matches per run on the **same seeds (2001-2020)**. Seeds don't reproduce matches exactly, so treat each run as a fresh sample.

| Run | Rules | Notes |
|---|---|---|
| `rolls_baseline_4v4` | `squad.Ballistics 0`: GDD §5.3 rolls, §9.1 flat cover | Same build, switch off |
| `ballistics_v1_4v4` | `squad.Ballistics 1`, first values | Rifle spread 10 mrad, braced head 28 cm over cover, aim at the most visible part |
| `ballistics_v2_4v4` | v1 + spread 16 mrad, smaller lean-out | Not discussed below |
| `ballistics_v3_4v4` | v2 + braced head 18 cm over cover, aim where most visible body is within the spread | **Current defaults** |

Raw records are in `data/`. Re-run with `./Scripts/AIMatchBatch.ps1 -Matches 20 -Parallel 4 -SeedBase 2000 [-Ballistics]`.

## Results

| | Rolls | Projectiles v1 | Projectiles v3 |
|---|---|---|---|
| Result (Allies / Axis / none) | 17 / 1 / 2 | 13 / 1 / 6 | 14 / 0 / 6 |
| Timeouts (injured-only stalemates) | 2 | 6 | 6 |
| Median match length | 40 s | 322 s | 342 s |
| Shots per match | 70 | 62 | 93 |
| Hit rate, all shots | 64% | 74% | 52% |
| Hit rate 0-8 m / 8-20 m / 20-40 m | 81 / 62 / 21% | 96 / 71 / 37% | 92 / 49 / 18% |
| Target behind a low wall (grid cover) | 11% (72 shots) | 47% (34) | 26% (68) |
| Target at a wall corner | 12% (17) | 79% (28) | 40% (78) |
| Friendly-fire hits (all injure-only) | n/a | 3 | 8 (0.4 per match) |
| Rounds stopped by cover | n/a | 141 | 429 |
| Mean peak stress | 8 | 8 | 13 |

The estimate shown to players matches what actually happens: in v3, rounds estimated at 48% at 8-20 m hit 49% of the time, and 17% vs 18% at long range.

## Controlled check (PIE, dedicated server + 2 clients)

This is the setup from the original sketch. Green is braced at the end of a low wall at (38,52). Server and both clients computed identical numbers.

| Shooter | Exposure of green | Hit chance |
|---|---|---|
| Red, straight across the wall, 12 m | 14% (head over the wall) | 28% |
| Blue, at a moderate angle past the end, 11.7 m | 39% (a strip down one side) | 19% |
| Blue, further round the end, 10 m | 100% | 66% |

In live fire, red's first two rounds hit the wall and the third clipped green's shoulder above the wall top, injuring it. Green was then prone and out of sight on every machine.

## Takeaways

1. **The angle matters as intended.** A shooter straight in front mostly hits the wall; one round the end sees and hits the whole body.
2. **Cover is still weaker than the flat reductions on average** (26% vs 11% behind low walls). The "low wall" column counts every target the grid rule calls covered, including ones physically exposed at an angle. Under fire from straight in front, the braced head shows and is hit about 25-30% of the time at 12 m.
3. **Matches take longer and stall more often.** Injured soldiers are prone and hide well behind cover, so injured-only stalemates rose from 2 to 6 of 20.
4. **Friendly fire is rare** with these bots (8 in 20 matches). They don't fire through their own lines often, but players will.
5. Starting-side bias (the earlier review) is unchanged: Allies still win almost every decided match.
