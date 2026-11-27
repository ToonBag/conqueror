# Bot tactics (2026-11-27, GDD v0.66)

**Question:** watching a 4v4 bot match, the bots don't play like people. Can they fight more realistically, using
cover, without changing any rules?

**What was wrong (baseline):** bots walked each soldier straight to its VP slot, standing, and fought wherever it
stopped. 94.5% of shots were at targets in no cover, 60% of the shots at able soldiers hit them while they were
walking, 22% of shots were fired from 5 m or closer, each soldier fired about 2 rounds, and half of all deaths came
by 23 s. Soldiers treated with a medkit stayed prone and crawled for the rest of the match (7.4% of active time
prone).

All runs: `AIMatchBatch.ps1`, Farmstead, 4 squads a side, bots only, hit-chance rolls unless marked, seeds
5001-5020 (6001-6020 for `bot_v5_ball2`). 20 matches per row. The summaries in this folder come from
`Scripts/Tests/analyze_match_stats.py`; it now has an **Exposure** section (share of position samples of active
soldiers: seen by the enemy, seen in the open, seen while moving, under fire). `data/` keeps every match summary,
plus shot logs for `bot_base` and `bot_v5`.

## What changed (all in `ASquadBot`, GDD §11.1)

Added one at a time and measured after each step:

1. **v1:** bounds from cover to cover, pausing in cover between them; overwatch (at most half the squad moving
   while enemies are known); breaking for cover when caught in the open on a long move; moving to cover when under
   fire in the open (with or without a target of its own); heads down (prone) behind hay or a low wall at Major
   stress; flanking an enemy in cover; VP slots that can see the enemy score higher; stand a prone soldier up
   before moving it.
2. **v2:** no moving across ground that a free (not pinned) enemy can see. Little change on its own, because most
   soldiers were shot by enemies their bot hadn't noticed yet.
3. **v3:** caution near VPs the team doesn't hold (within 25 m): short bounds, long pauses and overwatch even
   before any enemy is seen.
4. **v4:** cover to cover also on the last stretch into the VP when cautious, and medics wait until the casualty
   isn't in a free enemy's sights. A temporary log showed these two caused most hits on moving soldiers in v3:
   211 hits on the final walk into a slot, 90 on medics walking out to casualties.
5. **v5 (final):** the bot notices an enemy the team spots off screen after 1.5-3 s, because the minimap shows it
   (GDD §13); before, only enemies on its simulated screen counted.

## Results

| Run | Allies-Axis | Half the deaths by | Rounds/soldier | Shots at targets in no cover | Shots within 5 m | Shots at 30-50 m | Seen in the open | Seen moving | Rushes/match | Medkit treatments |
|---|---|---|---|---|---|---|---|---|---|---|
| baseline | 11-9 | 23 s | 2.1 | 94.5% | 22.4% | 0% | 36.3% | 31.4% | 13.6 | 9.9% |
| v1 | 13-7 | 33 s | 2.8 | 84.9% | 11.2% | 14.6% | 18.4% | 16.4% | 5.3 | 8.8% |
| v3 | 9-11 | 45 s | 3.7 | 83.1% | | | 14.5% | 11.5% | | 18.4% |
| v4 | 11-9 | 43 s | 3.8 | 79.8% | | | 14.7% | 11.7% | | 13.7% |
| **v5** | **11-9** | **43 s** | **4.0** | **77.8%** | **6.4%** | **35.2%** | **11.7%** | **8.8%** | **1.4** | **12.7%** |

"Seen in the open" and "Seen moving" are shares of all samples of active soldiers. The v2-v4 runs weren't kept
(blanks: not recorded).

Checks on the final version:

- **Swapped sides** (`bot_v5_swap`): Allies 7-13, so the south start won 24 of 40 over both runs. That's within
  noise for 40 matches (the 2026-11-25 map-bias review had 42-36 over 80).
- **1 squad a side** (`bot_v5_1v1`, the usual player-against-one-bot setting): 8-12, no timeouts, half the deaths
  by 48 s. One match took 15.5 min to finish (a long stand-off), well inside the time limit.
- **Projectile combat** (`bot_v5_ball`, `bot_v5_ball2`, `-Ballistics`): 6-14 and 13-7 (19-21 together), half the
  deaths by about 62 s, 31% of injuries treated.
- **In a client:** `WatchAIMatch.ps1` (dedicated server plus one client) played a full match with no errors in the
  server log. On screen, squads held the farmhouse rooms and walls and a medic treated a soldier.

## Findings

1. **The fight moved from the open to cover and range.** Time seen in the open fell from 36% to 12%, shots within
   5 m from 22% to 6%, and rushes from 13.6 to 1.4 a match. Fights last about twice as long (half the deaths by
   43 s instead of 23 s), and soldiers fire twice as many rounds.
2. **Most shots still hit targets in no cover (78%).** About 40% of the shots at targets in the open are at
   injured soldiers lying where they fell (injured soldiers have no cover rule yet, GDD §9.2 "not yet
   implemented"). Most of the rest come at first contact, when a soldier walks into the sights of an enemy its bot
   hasn't noticed yet. A human player gets surprised the same way. All three Farmstead VPs lie on the centre line,
   so both sides head for the same few cells.
3. **Stress matters a little more.** Time at Major stress or higher went from 5.7% to 6.6%. Heads-down and pinned
   enemies now affect who moves, so stress changes how the bots play, not only how they shoot.
4. **The old bot left treated soldiers crawling.** A medkit leaves the patient prone (GDD §5.2) and the bot never
   stood it up. Fixed, so active prone time fell from 7.4% to 2.3%. Prone time that's left is mostly heads-down.
5. **Rush success fell from 46% to 39%,** and only 1.4 rushes a match remain. Charging doesn't pay under bot
   tactics.

## Open

- Earlier reviews' numbers (fight length, stress, rushes) were measured with the old bot. Compare later balancing
  trials with `bot_v5`, not with earlier baselines.
- Dragging casualties into cover, retreating when outnumbered and coordinating squads (one pins, another
  flanks) aren't done yet.
- The bots now play more slowly and more cautiously. Whether that makes them a better sparring partner needs a
  human against a bot squad in a real playtest.
