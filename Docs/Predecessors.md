# Predecessors: Chain of Command → Call of Combat → Divided We Fall

Squad Command adapts a line of squad-based WW2 multiplayer games. This note records what is known about them and what we can learn from them. It is a reference, not design. The GDD (`Docs/GDD.md`) stays the source of truth. An idea from here goes into the GDD only once it is chosen.

Researched 2026-11-26. Sources are listed at the end. Points marked *(uncertain)* had thin or conflicting sources.

## Lineage

| Game | Developer | Years | Notes |
| ---- | --------- | ----- | ----- |
| **Chain of Command** | 2AM Creations | early 2000s *(uncertain: the review found is dated 2008, but Call of Combat, 2002, was built from its code)* | Web-based, 2D battlefield, teams of four infantry. A reviewer compared it to the Close Combat series. Players got orders from commanders, could pull men back when it got risky, earned promotion points, and joined regiments whose standing reflected team play. |
| **Call of Combat** | KAVA (Antoine Meurillon, Florian Käding) | 2002 to about 2012 | Free, cross-platform, Java with OpenGL. Each player commanded a four-soldier squad. Ran for about 10 years, then closed to make a new edition. One review calls the 2002 game "Call of Command" *(uncertain)*. |
| **Divided We Fall** | KAVA Game Studio (Dutch) | Early Access 7 Sep 2016, release 2 Oct 2017 | Unity; Windows, macOS, Linux. Free to play, with a paid "Officer Edition" DLC (about £7). The free version advertises "access to the good old Call of Combat". Mixed reviews: about 55% positive on Steam, 5.5/10 from GameSpace. |

## Divided We Fall mechanics

- **Squads:** four soldiers per player, each with its own inventory, and group orders. Steam claims "up to 120 soldiers commanded by up to 15 players per team", which does not match four soldiers per player *(uncertain)*.
- **Victory:** holding victory points.
- **Respawn:** none in the standard mode. A **Reinforcements** mode respawns dead soldiers after a few seconds. A **co-op vs AI** mode (added when it left Early Access) respawns both sides.
- **Command:** the most experienced player draws the plan on a map before the battle and hands out weapons from a map board. Other players can request orders but may act on openings themselves.
- **Cover and line of sight:** cover lowers the chance to be hit. Soldiers only shoot accurately at targets they can see clearly. Each soldier shows a hit-chance percentage. Grenades are the exception to the line-of-sight rule.
- **Stress:** a soldier pinned down long enough shows a heavy "pulse" indicator, and its accuracy appears to drop.
- **Weapons and ammo:** found and picked up on the battlefield.
- **Community:** map editor, custom maps, modding, Steam Workshop, achievements, leaderboards, clan and regiment rankings and competitions. No campaign.

## Already in our GDD

The core loop matches:
- four-man squads with per-soldier inventory and group orders (§4);
- victory points (§10.3) and no respawn (§11);
- a hit chance shown per soldier, which cover lowers (§5.3, §9);
- line of sight needed to shoot, with grenades exempt (§7, §9.3);
- stress from being pinned, which hurts accuracy (§8);
- looting and trading (§5.5);
- the highest-rated player commands, draws the plan and answers order requests (§13.1);
- regiments, ladders and military rank names (§12).

## What they had that we don't

1. **Reinforcements mode:** soldiers respawn after a delay. It worked as the casual mode.
2. **Co-op vs AI:** `ASquadBot` (§11.1) makes this cheap.
3. **Map editor, mods, Steam Workshop:** our square grid maps suit a simple editor.
4. **Commander loadouts:** the commander hands out weapons before the battle. Ours is first-come squad selection (§3.6).
5. **Pulling soldiers back** (Chain of Command): saving soldiers mattered, and promotion points rewarded team play as well as wins.
6. **Business model:** a free base game with a paid supporter edition.

## What reviewers criticised, and what it means for us

| Criticism | Our answer |
| --------- | ---------- |
| **Strategy turned into charging the enemy**, which hurt replay value. | The biggest risk to watch. Stress per shot, casualty stress and near-miss stress (GDD v0.58) are aimed at it. Add a "rush success" measure to `Scripts/Tests/analyze_match_stats.py` (e.g. how often soldiers that close inside 10 m across open ground win the fight) so balancing reviews track it. |
| **Needed a lot of players**: low player counts made matches hard to find. | Bots already take over empty squads (§11.1). Co-op vs AI and bot backfill in Opens extend that. Keep Ranked at 5v5 so queues stay short. |
| **Incomplete tutorial** that didn't state the objective. | A scripted bot scenario on a small map that teaches cover, stress, medkits and VPs. |
| **Picking up ammo and weapons felt inconsistent.** | Keep the loot menu (§5.5) predictable, and make every refusal say why (as build refusals do). |
| **Few maps at launch.** | A grid map editor, and Workshop later (§15). |

**Praised:** logical cover and line-of-sight rules, a historical feel balanced with gameplay, and a responsive developer and community.

No source found explains why the player base shrank. GameRevolution's Chain of Command review could not be read.

## Identity

We take mechanics and lessons, not names, maps, art or text. Squad Command keeps its own name, maps and look.

## Candidate GDD changes (not adopted)

- Reinforcements mode (§12.2)
- Co-op vs AI (§12.2)
- Map editor and Workshop (§15)
- Commander-assigned loadouts (§3.6)
- ~~Rush-success measure in AI match reviews (§11.2)~~ adopted in GDD v0.62
- Tutorial scenario

## Sources

- [Divided We Fall (Wikipedia)](https://en.wikipedia.org/wiki/Divided_We_Fall_(video_game))
- [Divided We Fall (Steam)](https://store.steampowered.com/app/495580/Divided_We_Fall/)
- [GameSpace review, Oct 2017](https://www.gamespace.com/featured/divided-we-fall-review/)
- [Chain of Command (g4g.it)](https://www.g4g.it/?p=1070)
- [Call of Combat (Retro Replay)](https://retro-replay.com/db/linux/call-of-combat)
