# Tempo and stress trials (2026-11-25)

**Question:** fights are over in about 20 s, whoever stops and shoots first wins, and stress almost never comes into
play (2026-11-23 review, findings 2 and 4). Which rule changes slow fights down or make stress matter, in both
combat models?

All runs: `AIMatchBatch.ps1`, Farmstead (symmetric, GDD v0.55), 4 squads a side, bots only, GDD v0.57 rules
(bleed-out, simultaneous shots, equal aim times). 40 matches per row: 20 with Allies south and 20 swapped. Seeds
7001-7020, or 8001-8020 for rows ending `8k`. Table made with `Scripts/Tests/compare_trials.py`. `data/` keeps every
match summary; shot logs only for `base_*`, `r_sm` and `p_stress8`.

## Trial values (new, all off by default)

Set per batch with `AIMatchBatch.ps1 -Tuning "Prop=Value;..."` (the `squad.TuningOverrides` cvar), so the data
asset was not changed:

| Value (`USoldierData`) | What it does |
|---|---|
| `MovingTargetAccuracyMultiplier` | Accuracy against a target that is stepping between cells. Rolls: × the range-band accuracy before cover. Projectiles: divides the spread. |
| `SettleTime`, `SettleAccuracyMultiplier` | After a soldier stops moving, its accuracy starts at the multiplier and rises to full over the settle time. |
| `CasualtyStress`, `CasualtyStressRadius` | Stress added to every living squad-mate within the radius (10 m) when a soldier is injured or killed (not on a bleed-out). |
| `StressPerIncomingShot` (existing, GDD 5) | Stress per round fired at a soldier (rolls) or per round passing within 1.5 m (projectiles). |

Trials: `mov` moving target ×0.5 · `settle` 1.5 s from ×0.4 · `stress` 12 per shot + 20 per casualty ·
`sm` = mov + stress · `combo` = all three. Projectile rows use 12 or 8 per shot.

## Results

| trial | Al-Ax | S-N | t/o | 1st death s | half deaths s | deaths >60s | rds/soldier | hit% | at movers% | reached 33 | reached 66 | time ≥Minor | stressed shots | grenades/match | medkits/match |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **base_rolls** | 21-19 | 23-17 | 0 | 14 | 20 | 10% | 2.3 | 65 | 55 | 1% | 0% | 0.1% | 0% | 3.3 | 2.1 |
| r_mov | 20-19 | 18-22 | 1 | 15 | 21 | 13% | 2.6 | 53 | 46 | 2% | 0% | 0.4% | 1% | 5.2 | 2.0 |
| r_settle | 20-19 | 20-20 | 1 | 15 | 22 | 14% | 2.4 | 61 | 62 | 0% | 0% | 0.1% | 0% | 4.6 | 1.8 |
| r_stress | 20-19 | 21-19 | 1 | 14 | 20 | 9% | 2.4 | 61 | 54 | 51% | 23% | 20.4% | 37% | 3.7 | 1.8 |
| r_sm | 22-18 | 18-22 | 0 | 15 | 21 | 10% | 2.6 | 52 | 39 | 53% | 24% | 24.4% | 37% | 5.9 | 2.3 |
| r_sm8k | 27-13 | 15-25 | 0 | 15 | 21 | 10% | 2.8 | 48 | 39 | 53% | 24% | 24.6% | 41% | 4.7 | 2.3 |
| r_combo | 27-12 | 20-20 | 1 | 15 | 23 | 14% | 3.1 | 46 | 40 | 58% | 24% | 20.8% | 41% | 10.1 | 2.1 |
| r_combo8k | 20-20 | 18-22 | 0 | 15 | 23 | 13% | 3.0 | 46 | 43 | 56% | 24% | 22.4% | 37% | 10.1 | 2.4 |
| **base_proj** | 18-22 | 20-20 | 0 | 13 | 23 | 15% | 2.9 | 47 | 41 | 17% | 4% | 6.5% | 19% | 3.2 | 1.9 |
| p_stress (12) | 17-23 | 21-19 | 0 | 13 | 24 | 17% | 3.5 | 39 | 32 | 59% | 39% | 38.1% | 65% | 3.4 | 2.4 |
| p_stress8 (8) | 15-25 | 21-19 | 0 | 13 | 23 | 14% | 3.0 | 44 | 39 | 55% | 33% | 33.3% | 56% | 4.1 | 2.2 |
| p_sm (mov + 8) | 15-25 | 15-25 | 0 | 13 | 23 | 15% | 3.3 | 39 | 33 | 54% | 28% | 29.3% | 48% | 5.4 | 2.8 |

"at movers%" = share of rifle shots aimed at a soldier who was moving. "stressed shots" = shots fired with a stress
accuracy penalty. "t/o" = 40-minute timeouts.

## Findings

1. **No rule lever tried slows the fights.** In every row the first death comes at 13-15 s and half of all
   deaths by 20-24 s. Bots walk into the open towards the VPs and fire the moment they stop: about 93% of shots are
   at soldiers in no cover. Each side has 16 rifles, so even at a 40% hit rate a meeting in the open is over in
   seconds. Making shots less accurate only adds 1-3 s. The fight length is set by how the bots fight (and by two
   hits killing, §5.1), not by these penalties. Human players taking cover will play at a different pace; the bot
   batches can't measure that.
2. **Stress works once it is fed.** 12 stress per incoming shot plus 20 per nearby casualty takes rolls from 1% to
   about 51% of soldiers reaching Minor and 23% reaching Major, with 37% of shots fired stressed. Win rates don't
   move, so it's a change in how fights play, not who wins. Projectiles already feed stress from every near miss,
   so 8 per round gives them about the same (55% / 33%); 12 is more than rolls get (59% / 39%, 65% of shots
   stressed).
3. **Moving-target penalty (×0.5)** cuts shots at movers from 55% to about 40% and hits from 65% to about 50%, and
   bots throw more grenades (3.3 → 5-6 per match, because they throw when the rifle chance is poor). It doesn't
   change the pace. Pooled over its four roll rows the Allies won 96-63, a possible faction tilt (the Garand's
   higher short-range accuracy keeps more of its value). Not confirmed: the stress-only rows with the same seeds
   are even.
4. **Settling after a move** does nothing the moving-target penalty doesn't already do, and stacked with it
   (`combo`) bots throw 10 grenades a match. Not worth the extra rule.
5. **Factions and sides stay even** in the stress-only rows. Single 40-match rows swing a lot (27-12 then 20-20 with
   the same values), so read only effects bigger than that.

## Options

- **A. Stress only (recommended):** `StressPerIncomingShot` 5 → 12 with dice rolls, and `CasualtyStress` 20 within
  10 m. For projectiles, keep the per-near-miss stress at about 8 (a separate value, since one round can stress
  several soldiers). Makes stress, the Shocked state and suppressing fire part of every fight without changing who
  wins.
- **B. A + moving-target ×0.5:** shooting at a runner gets harder and grenades come out more. Watch the possible
  Allied tilt.
- **C. Leave the pace to human players:** the open-ground pace is mostly the bots. Revisit it with the playtest
  shot logs (§11.2) before changing rifle accuracy or the two-hit rule.

## Decision: option A, stress only (GDD v0.58)

- `StressPerIncomingShot` 5 → **12** (dice rolls, `USoldierData`; `DA_SoldierTuning` didn't store the old value,
  so it takes the new default).
- New `CasualtyStress` **20** within `CasualtyStressRadius` **10 m** (both models).
- New `UBallisticsData::NearMissStress` **8** per round passing within 1.5 m (projectiles only).
- The moving-target and settling trial rules were removed from the code. `squad.TuningOverrides` /
  `AIMatchBatch.ps1 -Tuning` and `compare_trials.py` stay for future trials.

Confirmed with the shipped defaults (no overrides), new seeds 9001-9020:

| trial | Al-Ax | S-N | t/o | 1st death s | half deaths s | deaths >60s | rds/soldier | hit% | reached 33 | reached 66 | time ≥Minor | stressed shots | grenades/match | medkits/match |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| final_rolls | 20-20 | 18-22 | 0 | 14 | 21 | 12% | 2.6 | 61 | 52% | 24% | 23.3% | 37% | 3.0 | 2.0 |
| final_proj | 23-17 | 23-17 | 0 | 13 | 23 | 17% | 3.3 | 42 | 55% | 31% | 29.8% | 55% | 3.5 | 2.8 |

Multiplayer check (Play As Client, dedicated server + 2 clients): injuring one soldier gave its three squad-mates
exactly 20 stress on the server, the same value on both clients half a second later, and the client's squad bar
showed the stress meters.

**Open:** machine guns now suppress about 2.4× faster (12 stress per round, §6.1); not yet checked in play.
