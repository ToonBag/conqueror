# Bleed-out: AI test matches (2026-11-25)

**Change tested:** GDD v0.54. An injured soldier bleeds out 75 s after it was injured unless a medkit
treatment finishes first. The bot sends medics from up to 30 m once 45 s or less are left.
**What ran:** `AIMatchBatch.ps1 -Matches 40 -SeedBase 3000 -Parallel 4`, with and without `-Ballistics`.
4v4 on Farmstead, bots only, same seeds as `2026-11-23_combat_modes`. 4 of the 40 roll matches were lost
because their server processes were closed by mistake mid-batch, so that set has 36.

| | Rolls before | Rolls now | Projectile before | Projectile now |
|---|---|---|---|---|
| Matches | 40 | 36 | 40 | 40 |
| Timeouts (injured-only stalemates) | 6 | **0** | 11 | **0** |
| Allies / Axis wins | 30 / 4 | 33 / 3 | 27 / 2 | 35 / 5 |
| Median length, decided matches | 50 s | 45 s | 77 s | 96 s |
| Longest match | - | 145 s | - | 124 s |
| Medkits used per match | 1.1 | 1.2 | 2.3 | 2.9 |
| Deaths by bleeding out | - | 31 of 838 | - | 81 of 1004 |

- Every match now ends. Bleed-out deaths are about 4% of deaths with rolls and 8% with projectiles. With
  projectiles more injured soldiers drop out of sight behind cover and can't be finished (§16.7).
- Medkit use rose a little. The bots still treat few injuries: most medics are under fire or too far away.
- The south-side bias (finding 1 of `2026-11-23_farmstead_4v4_ai`) is unchanged: Allies start south here.
  That is the next item to fix.

Raw records in `data/` (`bleed_rolls`, `bleed_proj`). Tables:
`python -I Scripts/Tests/analyze_match_stats.py Docs/Balancing/2026-11-25_bleed_out/data/bleed_rolls`.
