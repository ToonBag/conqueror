# SQUAD COMMAND

**World War II Real-Time Strategy — Game Design Document**
Version 0.48 | Unreal Engine 5.8.4 | Steam Multiplayer

> **Changelog (v0.48):** Several squads per team (§4.1, §4.3, §11.1, §11.2; implementation of existing rules, no new numbers). A team can now hold more than one squad, one per player: a joining player takes the first squad of their team that no other player commands, and a player leaving hands only their own squad back to a bot. The squad cohesion limit (§4.3) holds each squad together, not the whole team. Every squad no player commands gets its own bot; a team's bots see each other's VP assignments and spread over the VPs, and won't grenade any friendly soldier. For testing, `squad.SquadsPerTeam` copies the level's squad into each deployment zone (Farmstead was built for 1 squad a side, §10.1). New AI test matches (§11.2) record every shot, hit, grenade blast, medkit, capture and stress level to files for balancing.

> **Changelog (v0.47):** Networked play (§11.1, §14.1; implementation, no rule changes). Soldiers, grenades and loot piles are now simulated only on the dedicated server and replicated to every client; every order a player gives (move, stop, face, attack, stance, hold fire, grenade, drag, medkit, loot) is sent to the server, which checks the soldier is that player's before running it. Until matchmaking assigns teams (§12, §14.2), each player who joins goes to the team with fewer players (Allies on a tie). A dedicated server waits for its first player before the match starts and bots take empty teams (`squad.MinPlayers`, default 1); a player joining a bot-run team takes that squad over, and a team every player has left goes back to a bot. Development and tests now run as a dedicated server with networked clients. The debug menu's Restart (§13) restarts the match on the server, so every client restarts with it.

> **Changelog (v0.46):** Cover props (§9.5, _provisional_). Wells, water troughs, crate stacks, barrels, woodpiles, sandbags, hay carts and abandoned vehicles can be placed as cover. Each uses an existing cover type: a truck or tank wreck is full cover that blocks sight like a wall, a car and the small props are low walls, a hay cart is hay. Multi-cell props are solid blocks, so grenades can't drop between their cells.

> **Changelog (v0.45):** Low-poly cover art (§2.1, §9.4, cosmetic only). Cover pieces now use the low-poly set from `SourceArt/Environment` instead of placeholder boxes: plaster walls and windows, stone low walls, hay bale stacks and oak / apple trees (apple trees in the orchard around VP A). Weakened walls use their own cracked, notched wall mesh. A breach leaves a broken-stub-and-heap mesh on each cell and throws debris-chunk meshes. Gameplay sizes are unchanged: the meshes match the `UCoverData` heights and the old placeholder footprints.

> **Changelog (v0.44):** MG 42 faction parity (§6.1, provisional). The MG 42 now fires **5-round bursts with a 1.3 s pause** (was 7 rounds, 1.2 s). It had put out about 45% more stress per second than the M1919, because stress comes from every round fired. Now it sustains 3.38 rounds/s against the M1919's 3.25 and pins a soldier behind a low wall in about 12.6 s (M1919: 11.9 s). Its lower accuracy keeps its hit rate slightly below the M1919's. Its bursts stay short and fast (5 rounds in 0.2 s).

> **Changelog (v0.43):** MG set-up pose, a fire-timing fix and the MG demo (§6.1). **Set-up pose (cosmetic):** as the 2 s set-up runs, the gun swings from the gunner's hands onto what is in front of it. Prone, the bipod rests on the ground; behind hay, a low wall or a window sill the legs fold and the barrel lies on top. Set up, the gun kicks back slightly per round instead of playing the rifle's fire clip. Standing or crouched in the open there is nothing to rest on, so the gun stays in the hands (an open question). **Fire timing:** the time left over after a shot now carries into the next for every weapon. Before, a rifle lost the leftover part of each frame, so on a slow tick it fired measurably slower (on a 30 Hz server about 2%). A machine gun fires every round of a burst that falls due within one frame. **Demo:** `squad.DemoMG [terrain|stats|all] [trial seconds]` shows the set-up on each kind of ground and measures the M1 Garand against both MGs (6 / 15 / 30 m; exposed, hay, low wall; plus a crossing run). Results go to `Saved/Demo/MG`. Tuning is unchanged; the measured findings are under §6.1.

> **Changelog (v0.42):** Machine guns (§6.1, provisional). An Allied **M1919 Browning** and an Axis **MG 42**. Both are belt-fed, fire **bursts** where every round gets its own hit roll and adds stress like a rifle shot, and must be **set up** first: **2 s** standing still, facing where they will fire, before the first burst. Set up, the gun fires across a **90°** arc and prefers automatic targets inside it. Moving, a change of stance, anything that takes the gunner's hands, or a target or facing order outside the arc tears it down. Each MG has its own ammo (250-round belt, 500 rounds carried). The squad bar shows a belt as a fill bar instead of pips, with **SETTING UP** / **MG SET UP** tags. No squad type carries an MG yet. **For play testing**, soldier 2 of every player-commanded squad starts with its faction's MG instead of a rifle (`TestMachineGunSoldier` on the game mode, 0 turns it off). Bot squads keep rifles. All numbers are provisional.

> **Changelog (v0.41):** Master volume and mute (§13). A small audio widget sits at the top left of the screen: a speaker button that mutes and unmutes all sound (the icon turns red with a cross, and the label reads MUTED), a master volume slider (click or drag; moving it also unmutes) and the volume in percent. The speaker shows one to three sound waves by volume. Clicks on the widget never reach the map. It is hidden while the debug menu or a debug click tool uses that corner. The settings are local to the player (never sent to the server) and are saved in `GameUserSettings.ini`, so they carry over between matches and sessions. Default: 100%, not muted.

> **Changelog (v0.40):** Combat sounds (§2.1, cosmetic). Every rifle shot plays a gunshot at the muzzle, a reload plays the clip-ejection sound at the soldier, and grenades and C4 play an explosion when they go off. One M1 Garand recording is used for both rifles for now. Sounds fall off with distance from the camera. Fire and reload sounds are set per rifle on `UWeaponData` (`FireSound`, `ReloadSound`); the explosion and the shared distance falloff are on `USoldierVisualData` (`ExplosionSound`, `CombatSoundAttenuation`). Not played on a dedicated server.

> **Changelog (v0.39):** Cover stops grenade blasts, and a move path line (§7.1, §13). **Blast cover:** cover between where a grenade goes off and a soldier now shields that soldier from both the kill roll and the stun. Judged when the grenade goes off along the line between the two cell centres, the same way sight lines are traced (§9.3). Full walls, wall corners and trees always shield. Hay bales, low walls (and rubble) and windows shield too, unless the soldier is right up against that cover cell (diagonals included) with its head above the top or the window sill. A standing or crouched soldier peering over low cover or through a window is caught, but one that **drops prone before the grenade goes off** is shielded. A soldier leaning out past a wall's edge is also exposed from the open cell beside the wall's end. C4 is unchanged: no cover shields its blast. The play-test bot drops prone behind low cover or a window instead of running when that would shield it, and ignores a grenade that cover already blocks. **Move path line:** each of your moving soldiers shows a dashed red line on the ground along the route it is taking to its destination cell, flowing toward it and ending in an arrowhead. It is brighter for selected soldiers and shrinks as the soldier walks. Own soldiers only.

> **Changelog (v0.38):** Grenade step back only when moving (§7). A soldier steps back onto its previous cell to throw only if it was moving when the throw was ordered. A soldier standing still throws where it stands: the **0.5 s** wind-up starts at once and the pin is pulled during it, as before v0.37.

> **Changelog (v0.37):** Grenade rework (§7, §7.1, §7.2, §5). **Range:** grenades can be thrown at most **10 m** (was a provisional 30 m), measured from the cell the soldier throws from. **Blast:** the stun area is now **5 m across** (stun radius **2.5 m**, was 4 m); the 1 m kill zone (10% kill, else 7 s stun) is unchanged. C4 keeps its old blast (1 m / 4 m), now set by its own `C4KillRadius` / `C4StunRadius` instead of the grenade radii times `C4BlastRadiusScale`. **Step back:** to throw, a soldier finishes the step it is on, then **steps back one cell** onto the cell it came from, walking at its stance's speed and still facing the throw, while taking the grenade out and pulling the pin. Only then does the **0.5 s** wind-up start, and the grenade is thrown from that cell. If the previous cell can't be stepped to (taken, blocked, it is the target, or the soldier has none yet), it steps to the free cell directly away from the target, or the one either side of that; with none free, or if it is Shocked, it throws where it stands. Firing stays paused for the whole sequence, and a move order, stun or death before the release cancels it without using the grenade. This makes a throw take longer and moves the thrower, so a grenadier can be caught and shot mid-throw. New animations: a step back with pin pull, and a throw starting with the pin already out, for each stance.

> **Changelog (v0.36):** Developer debug menu (§13, development builds only, not part of the game). **F6** opens a panel top left with live info (net mode, fps, game speed, the grid cell and soldier under the cursor), world toggles (reveal all, bot AI, bot debug view, god mode for player squads, game speed 0.25×–4×, pause, VP win time override, breach all walls, restart), actions on the selected soldiers (set stress, heal, injure, stun 5 s, kill, restock to the starting loadout), actions on every enemy (injure, kill, stress 0 / 100) and click tools (teleport the selection, or heal / injure / stun / kill the soldier clicked). Every action runs on the server. It is compiled out of shipping builds, and a dedicated server outside the editor refuses it unless `squad.AllowDebugMenu 1`.

> **Changelog (v0.35):** Destructible walls and buildables (§4.2, §7.2, §9.4, §10.5, §13.2, §13.3). Some maps have **weakened full walls**, which look cracked and act as normal full walls until breached. Farmstead has two: the barn partition (54–56, 35) and the farmhouse partition (31–33, 37). A **Build** slice on the command wheel opens a buildables sub-wheel for every player. For the CO, XO and planners it is the 10th slice. Each soldier carries **1 C4** and **1 Wall Reinforcement**, and placing either takes **7 s**, channelled from an open cell against the wall's face. Any order other than a face order cancels the placement without spending the item, and so does being stunned, injured, killed or dragged. C4 detonates after a **5 s** fuse (_provisional_). It breaches the wall into walkable rubble that gives Low Wall cover (50%) and scatters debris. Its blast hits soldiers with the frag grenade's effects: friendly fire applies, and the wall doesn't shield them. A reinforced wall can't be breached, but a charge on it still blasts soldiers. Finishing a reinforcement while a charge on that wall is still on its fuse makes the charge a dud.

> **Changelog (v0.34):** Leaning out round building corners (§9.2). A soldier at the end of a full wall that carries on away from it (a building corner, or the end of a wall running the way it faces) now leans out sideways past the edge when it leans on that wall, and looks down the wall's outer face. It sees, shoots and is seen from the open cell beside the wall's end, covering everything beyond that cell's row from that open column outwards. Before this, only a soldier behind a wall's face could lean out, so at a corner it saw nothing down the side of the wall. Whether a wall carries on is judged from the neighbouring cells, because corners are single-cell pieces. If both sides of a wall's end are open, the soldier leans out on the side it is turned towards. Cosmetic: while leaning out, the model slides **15 cm** sideways towards the open side (`PeekSideShift` in the soldier visual data). That is just enough for the rifle to clear the wall and point down its face, and the soldier stays tucked in at the corner. The vision cone of a soldier leaning out (§9.2 overlays) now fans out from the wall's corner over the open side only: from straight along the wall to 60° off it. It no longer spills over the wall or into the building, and every cone's rays now stop at full walls.

> **Changelog (v0.33):** Free facing, grenade priority and movement / cover overlays (§7, §9.2, §13). **Facing:** a face order (Shift + Left Click on ground) works at any time. A walking soldier given one keeps looking that way and walks on (the animation doesn't show it yet), and it no longer cancels a medkit treatment. It still drops an attack in progress, and a soldier with no explicit target still turns to engage a visible enemy once it stands still (§5.3). A move order without a later face order faces the way of travel as before. **Grenades:** a soldier that is firing now pauses its fire and turns to face the throw (previously it kept facing its target); after the release it finishes the throwing motion for **0.4 s** (_provisional_, `GrenadeFollowThroughTime`) before it turns back and resumes firing. **Overlays** (own soldiers only, cosmetic): a faint circle on the ground marks how far the selected soldier(s) can be sent before the squad cohesion limit (§4.3) cuts the move short; hovering a free cell next to cover with a soldier selected shows the arc of fire each adjacent piece of cover would protect against there, the cover it will lean on when it arrives bright green with its value (e.g. "-50 LOW WALL") and the rest dim amber with the reduced back-turned value; and when one of your soldiers has turned to lean on cover a cone of what it can see (±60° of its facing, up to the 40 m sight range, cut off by cover as §9.3) flashes up for about 2 s.

> **Changelog (v0.32):** Hold fire added (§5.3, §13). **H** toggles hold fire for the selected soldiers (a mixed selection all switch to holding; H again releases them). A soldier holding fire never picks targets on its own, so it doesn't waste ammo on enemies in hard cover it has little chance of hitting; turning it on also stops any fire in progress. An explicit attack order still fires, and once that target is gone the soldier goes back to holding. Grenades, moving and everything else are unaffected, and it stays on until released (stuns and injuries don't clear it). Shown to its own team only: the soldier's number turns red with a red **HOLD** tag above it (kept short so tags of soldiers 1 m apart don't run together), and on its squad-bar card a red stripe along the top and a solid red **HOLD FIRE** tag that always comes first. The squad-bar card's idle tag now reads **Ready** (was "Holding", to avoid confusion).

> **Changelog (v0.31):** Soldier models and animation (§13, §2.1). Soldiers are now low-poly uniformed models: Allied US infantry (olive drab, M1 helmet, khaki webbing and leggings) and Axis German infantry (field grey, stone-grey trousers, black leather, dark grey helmet), on one shared skeleton. Each holds the model of the rifle it actually carries (M1 Garand / Gewehr 43; a looted rifle changes the model), and the team's grenade (Mk 2 / Stielhandgranate 24) appears in the hand during the throw and is what flies. Animations: idle, aim and recoil, run (standing), crouched run and prone crawl (paced to the real movement speed), reload (timed to the reload), stance changes, the grenade throw (its release lands exactly at the end of the 0.5 s wind-up, the rifle moves to the left hand), leaning on cover (low cover: rifle rested on top; full walls and trees: shoulder to the cover; at a wall's end: leaning out past the edge), stunned, injured (lying and crawling), dragging, and death (standing deaths fall backwards and lie face-up, prone deaths stay face-down; corpses are greyed and drop their rifle). Purely cosmetic: no gameplay value changed.

> **Changelog (v0.30):** Medkits implemented and clarified (§5.2), with a right-click context menu (§13). **Right click no longer moves soldiers**: Left Click moves and selects. Right clicking an injured ally (on the battlefield or on its squad-bar card) opens a menu at the cursor with **Use medkit**. The nearest selected soldier able to treat does it (otherwise the nearest one in the squad); if it isn't next to the patient, it walks over first. "Adjacent" means the medic's own cell or any of the 8 around it (diagonals count, like looting). The 7-second treatment starts once both are still; the medic can't shoot during it. Any new order to the medic, the medic being stunned, injured, killed or Shocked, or the patient dying, being dragged or crawling out of reach stops it, and the medkit is kept: it is only used up when the treatment finishes. The treated soldier is Active again and stays prone. Only Active soldiers can treat, never themselves, and one medic per patient at a time. The play-test bot uses medkits too (§11.1).

> **Changelog (v0.29):** Command wheel and pings added (§13.2). Holding the **middle mouse button** opens a radial command wheel at the cursor, on the battlefield, minimap or tactical map. Every player gets **Spot** (enemy spotted here; snaps onto a visible enemy within 1.5 m) and **Ping** (look / go here); a quick tap of the middle mouse is a Ping. Players with planning rights also get Push, Hold line, Route, Defend, Attack, Rally and Erase, so plans can be drawn live in the play area. Push / Hold / Route start where the wheel opened and follow the cursor until a left click places them. Players who aren't CO or XO also get Request orders. Plan marks are now also drawn on the battlefield itself, and pings show on the battlefield (with an arrow at the screen edge when off screen), the minimap and the tactical map. Pings are team-only. Provisional: Spot lasts 10 s, Ping 6 s, at most one ping per player every 0.5 s and 3 up at once.

> **Changelog (v0.28):** Tactical map and match command roles added (§13.1). **M** (or the expand button in the minimap's corner) opens the map maximised in the middle of the screen. Each team's highest-ELO player is its **Commanding Officer (CO)** and the second-highest its **Executive Officer (XO)** (ties: whoever joined first); these match roles are separate from regiment roles (§12.3). The CO and XO, and anyone they give planning rights to, draw the team's plan on the map: Push arrows, Hold-line lines, freehand Routes, Defend shields, Attack targets, Rally flags and Enemy markers. The plan is shared with the team only (never sent to the enemy) and also shows on the minimap. Any other player can **Request orders**, which marks their squad's position and alerts the CO and XO until one of them acknowledges it. Provisional limits: 40 marks per team (the oldest goes), one open request per player, 15 s between requests, unanswered requests expire after 60 s.

> **Changelog (v0.27):** Squad cohesion limit added (§4.3): a squad's two furthest-apart living soldiers may never be more than **30 m** apart, measured in a straight line between cell centres and judged on where each soldier is heading. A move order that would break it is shortened to the nearest reachable cell within the limit and the soldier shows **SQUAD LIMIT**; group moves keep their formation and stop where the soldiers left behind allow (§13). Stunned, injured and dragged soldiers count; the dead don't. The play-test bot only holds Victory Points that fit inside the limit together and takes the others in turn (§11.1).

> **Changelog (v0.26):** Minimap added (§13): bottom left of the screen, showing the whole grid with cover, Victory Points, Deployment Zones and the camera's view, your soldiers with facing cones, allied soldiers, and only the enemies your team can currently see. It is turned so your own Deployment Zone is at the bottom. Left click or drag on it to move the camera there; clicks on it never reach the map. Double-tapping a soldier's number key (1–4) moves the camera over that soldier.

> **Changelog (v0.25):** Weapon range bands shortened to Short ≤8 m, Mid ≤20 m, Long ≤40 m (were 10 / 30 / 60 m); accuracies unchanged (§5.3). Maximum sight range added: **40 m**, matching the rifles' Long range, so nothing is spotted that can't be shot at (§9.3). Play-test bot now plays like a person (§11.1): it sees through a simulated camera with the player's angle, field of view and zoom limits, notices enemies only on its screen after a reaction delay, issues orders one at a time, and usually lets soldiers finish their moves instead of stopping them the moment an enemy appears.

> **Changelog (v0.24):** Automatic target acquisition implemented for every soldier (§5.3). A soldier with no explicit attack order engages the enemy it can see with the highest hit chance, but only while standing still: it doesn't break off a move to shoot. It swaps to another automatic target only for a shot at least 5 points better (provisional), drops an automatic target silently when it goes out of sight, and doesn't auto-acquire with no ammo for its rifle or while dragging. An explicit attack order overrides it until that target dies or is lost.

> **Changelog (v0.23):** Victory conditions implemented (§11). Clarified: a contested VP keeps its owner (§10.3), so it still counts as held for the VP win timer; any moment the same team doesn't own every VP resets the timer. If both squads are wiped out at the same moment the match is a draw. The match ends with a Victory / Defeat banner and the game pauses; while one team holds every VP the time left is shown at the top of the screen. Play-test AI bot added (§11.1): a squad no player commands is driven by a server-side bot. Console: `squad.Bot 0/1`, `squad.VPWinTime <seconds>`.

> **Changelog (v0.22):** Victory Point capture rules and Deployment Zones added (§10.3, §10.4). Each team has an 8x8-cell Deployment Zone at its own edge of the map where its squad starts. A Victory Point belongs to the last team that had soldiers in it while the other team had none: it starts neutral, both teams inside means contested (no change), and an empty point keeps its owner. Capture is immediate (no timer). Provisional: only soldiers able to act (not stunned, injured or dead) capture or contest. Farmstead layout defined (§10.5): Allied DZ on the south edge, Axis DZ on the north edge, three 6x6 VPs on the map's centre line (A Orchard, B Farmhouse, C Barn), each the same distance from both DZs. Farmstead is now the default map.

> **Changelog (v0.21):** Looking over low cover (§9.3). A standing or crouched soldier right up against a hay bale or low wall looks over it and sees a prone soldier lying on the other side (and is seen by it). Distant viewers still can't see a prone soldier hidden behind low cover.

> **Changelog (v0.20):** Kill feed added (§13): top right of the screen, one line per kill showing the killer's rank and regiment tag above their name, a rifle or grenade icon with the chance of the roll that killed (the killing shot's hit chance after cover, or the 10% grenade kill chance), the rifle's name, and the victim's rank and name. Ranks are shown as short tags (PVT, PFC, CPL, SGT, SSG, PSG, MSG, 1SG, SGM, WO1, WO2, WO3, CWO) from the rank ladder (§12.1). Kills are reported by the server and sent to every player.

> **Changelog (v0.19):** Aim lines added (§13): a line from each attacking soldier's muzzle to its target, dashed amber while aiming, solid red while firing, grey while reloading, with target brackets and the hit chance.

> **Changelog (v0.18):** Squad bar HUD added (§13): a translucent card per soldier along the bottom of the screen showing kit (rifle, loaded/spare rounds, grenades, medkits), reload progress, stress, health and status (under fire, in cover, spotted, stunned, …). Selected soldiers' cards are highlighted; clicking a card selects that soldier (Shift + click adds or removes it). The loot & trade menu now opens above the bar.

> **Changelog (v0.17):** Standing base speed set to 2.4 squares per second (20% slower than the 3 m/s the prototype ran at; the old 1 square/s figure was never what the game used), so crouched is ≈1.58 and prone ≈0.79 squares per second (§10.2). Grenade stun radius lowered from 5 m to 4 m (§7.1). Throwing a grenade now has a 0.5 s wind-up (pulling the pin, throwing action) before the grenade leaves the hand: the soldier finishes its current step and stops, doesn't fire during the wind-up, and a move order, stun or death during it cancels the throw without using a grenade (§7).

> **Changelog (v0.16):** Stress system implemented (§8). Tier penalties given provisional numbers (fire interval, reload time, aim time, accuracy). Every round fired at a soldier adds 5 stress, hit or miss, reduced by the cover that applies to the shot (§9.2). Cross-fire defined: a squad-mate firing past a soldier within 1.5 m of the line of fire adds 2.5 stress (provisional). Decay starts 1 s after the last stress gain (provisional). Shocked soldiers stop moving and let go of a drag but can still fire, throw and change stance. Values live in `USoldierData`.

> **Changelog (v0.15):** Leaning on cover (§9.2). A soldier is fully in cover only while facing the cover (leaning on it); with its back to the cover it gets half the reduction. Soldiers turn to lean on adjacent cover when they finish a move. Cover doesn't count against an attacker who is up against the same cover from the other side. A soldier leaning on the end of a full wall looks out past its edge and can see, shoot and be shot across the far side of the wall (§9.2, §9.3). Building Window reduction lowered from 70% to 40% (§9.1).

> **Changelog (v0.14):** Looting & trading implemented as an area menu (§5.5). Tab opens one loot / trade menu listing every body (either team) and dropped-item pile on the soldier's own cell or any of the 8 cells around it (diagonals included). From it the soldier swaps rifles (loaded rounds go with the rifle), takes ammo / grenades / medkits, or drops its own rounds, grenades and medkits on its cell for a squad-mate. Ammo is specific to the rifle that fires it. Only Active soldiers that aren't dragging can loot; looting is instant and there are no carry limits (provisional). Tab no longer cycles the selected soldier (§13).

> **Changelog (v0.13):** Grenades and cover (§7). Full walls stop grenades at any height (no throwing through or over them). Half walls and hay bales can be thrown over. Windows can be thrown through between sill and top. Trees stop grenades below 4.2 m. Cover pieces are slabs of their real thickness. The thrower picks an arc that clears cover (up to a 10 m lob); if none exists, the throw is refused with "BLOCKED" and no grenade is spent. Cover is also physically solid, so missed rounds stop on it (§9.2).

> **Changelog (v0.12):** Line of sight and team vision (§9.3). Sight lines run cell centre to cell centre between the two soldiers' sight heights (standing 160 cm, crouched 110 cm, prone 40 cm). Cover is solid: hay bales and low walls block sight lines that pass below their top, so a prone soldier behind half-height cover is hidden (and cannot see or shoot over it) unless viewed from an angle that gets round its end, while standing and crouched soldiers see over it. Full walls and trees always block; windows only between sill and top. A player sees an enemy soldier only while one of their own soldiers has line of sight to it, and a soldier can only shoot what it can see. Trees now block line of sight (previously they did not).

> **Changelog (v0.11):** Cover system implemented (§9). Reductions are flat percentage points off the hit chance, and only the best cover applies. Added a provisional Tree value of 40%. New §9.2 Cover Rules: cover objects block movement; cover comes from orthogonally adjacent cells within a 60° arc (outside it the soldier is flanked); full walls block line of sight, and soldiers at a wall's edge get Wall Corner / Edge cover; windows can be shot through.

> **Changelog (v0.10):** Stunned soldiers are knocked prone for the duration of the stun and return to their previous stance afterwards (§5.1, §13).

> **Changelog (v0.9):** Dragging implemented and clarified (§5.4): "within 1 m" is measured centre to centre, so the dragger must be on the same cell or an orthogonally adjacent one (a diagonal neighbour is 1.41 m away); the dragged soldier follows into the cell the dragger has just left; a dragged soldier cannot take orders; the drag ends automatically if the dragger is stunned, injured or killed, or if the dragged soldier recovers from a stun.

> **Changelog (v0.8):** Group moves are anchored on the lowest-numbered living soldier in the selection (normally soldier 1): that soldier moves to the clicked cell and the others keep their positions relative to it (§13).

> **Changelog (v0.7):** Orders given to several selected soldiers are carried out by all of them: every selected soldier throws a grenade at the target cell, and all attack the targeted enemy. Group moves keep the squad's current formation, centred on the clicked cell (§13).

> **Changelog (v0.6):** Weapon range bands defined: Short ≤10 m, Mid ≤30 m, Long ≤60 m, no fire beyond 60 m (§5.3). A rifle hit from 5 m or closer kills outright (§5.1). Provisional grenade values added: 30 m max throw range, 1 s between throws by one soldier, grenades affect both teams (§7). Stance is shown on placeholder bodies by posture (§13). The developer console moves from backtick to F10 because backtick selects all (§13).

> **Changelog (v0.5):** Soldiers are numbered 1–4; the number is shown above each soldier and matches its selection hotkey. Clicking a soldier also selects it. A move order makes the destination cell flash red with the moving soldier's number until it arrives (§13).

> **Changelog (v0.4):** Movement is now 8-directional (diagonal steps allowed, cost √2, no corner-cutting) (§10.2).

> **Changelog (v0.3):** Engine changed from Unity 6.3 LTS to Unreal Engine 5.8.4. Networking, server and implementation notes updated to Unreal equivalents (§2, §14). Added Unreal implementation notes (§2.1). Duplicate v0.1/v0.2 text removed. Gameplay rules unchanged.

---

## 1. Game Overview

Squad Command is a low-poly, WW2-themed multiplayer Real-Time Strategy game developed in Unreal Engine 5.8.4 and distributed through Steam. Players command small squads of soldiers across tactical grid-based maps, competing in team vs team matches. The game features a ranked ELO matchmaking system, a full lobby with social features, and a suite of tactical mechanics centred on cover, suppression, and stress.

### 1.1 Core Pillars

- **Tactical Depth:** Every action carries consequence. Cover, suppression, stress and grenade timing all shape the outcome of engagements.
- **Low-Poly WW2 Aesthetic:** Clean, readable visuals inspired by WW2 theatre, emphasising clarity over photorealism.
- **Social Multiplayer:** Integrated friends system, party lobbying, and ELO-ranked matchmaking.
- **Grid-Based Precision:** Movement and ability execution are snapped to a grid, enabling deterministic, fair gameplay.

## 2. Platform & Technical Overview

| Technical Stack     |                                                                                          |
| ------------------- | ---------------------------------------------------------------------------------------- |
| **Engine**          | Unreal Engine 5.8.4 (source build, required for dedicated server targets)                |
| **Platform**        | PC (Steam)                                                                               |
| **Multiplayer**     | Unreal built-in replication (server-authoritative), Online Subsystem Steam + Steam Sockets |
| **Server Backend**  | Google Cloud Platform (GKE + Agones, Open Match, Cloud Spanner)                          |
| **Art Style**       | Low-poly, WW2 themed                                                                     |
| **Map Type**        | Square, grid-based (scaling dimensions)                                                  |
| **Target Players**  | 2+ (team vs team) up to 32v32                                                            |

The game uses Unreal Engine 5.8.4 for its mature, server-authoritative networking model (actor replication, RPCs, dedicated server builds). Steam handles distribution, friends lists, and lobby infrastructure via the Steamworks SDK (exposed through Online Subsystem Steam), while a dedicated Google Cloud backend handles authoritative match simulation and database storage. All game maps are square grids. All positional logic — including grenade throws, movement snapping, and cover attachment — operates on grid coordinates.

### 2.1 Unreal Implementation Notes

These are recommended mappings of design systems onto Unreal's framework; they guide implementation and may be revised.

| Design Area                     | Unreal Approach                                                                                                   |
| ------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| **Units / Scale**               | Unreal uses centimetres. **1 grid cell = 1 m = 100 Unreal units (uu).**                                           |
| **Match rules**                 | `AGameModeBase` subclass (server-only): win conditions, VP timers, squad limits, friendly fire.                   |
| **Replicated match state**      | `AGameStateBase` subclass: VP ownership, VP hold timer, match clock, team scores.                                 |
| **Player data**                 | `APlayerState` subclass: team, ELO, rank, selected squad, Steam identity.                                         |
| **Player input / orders**       | `APlayerController` + Enhanced Input (Input Actions + Mapping Contexts). Orders sent to server via Server RPCs.    |
| **Camera**                      | RTS camera pawn (`APawn` + `USpringArmComponent` + `UCameraComponent`), not a possessed soldier.                  |
| **Soldiers**                    | `ACharacter` (or lightweight `APawn`) driven by an `AAIController`; state, stress, inventory replicated.          |
| **Grid & pathfinding**          | Custom grid subsystem (`UWorldSubsystem`) owning cell data, occupancy, cover and A* pathfinding on cells.         |
| **Weapon / squad stats**        | `UPrimaryDataAsset` or DataTables so balancing does not require code changes.                                     |
| **Hit chance / RNG**            | Computed only on the server using a seeded `FRandomStream`; results replicated to clients.                        |
| **Lobby / HUD UI**              | UMG + Common UI.                                                                                                  |
| **Grenades**                    | Server-simulated ballistic arc on grid; clients receive replicated projectile for visuals.                        |
| **Soldier models / animation**  | `USkeletalMeshComponent` on the soldier pawn with a native `USoldierAnimInstance` that picks and cross-fades clips from replicated state (cosmetic only, not ticked on a dedicated server). Models, props and clips live in `USoldierVisualData` (`DA_SoldierVisuals`); rifle models on `UWeaponData`. Clips are hand-keyed by `SourceArt/Characters/Tools/build_soldier_anims.py`. |
| **Cover / environment art**     | Low-poly static meshes built by `SourceArt/Environment/build_ww2_cover.py` (Blender) and imported to `/Game/Environment` by `import_cover_unreal.py`. Pivot at ground level in the centre of the footprint; one cell = 1 m; `UCX_` boxes for collision. `ACoverActor` draws each cell with an instanced mesh picked per cell from the per-type variant lists (`Cover|Visuals`). Cosmetic only. |
| **Combat audio**                | Cosmetic `PlaySoundAtLocation` where the shot, reload or blast is shown, with a shared `USoundAttenuation`. Rifle sounds on `UWeaponData`; explosion on `USoldierVisualData`. Source files in `SourceArt/Audio`, imported to `/Game/Audio`. |

## 3. Lobby & Social System

### 3.1 Lobby Overview

The lobby is the primary pre-game social hub. Players arrive here after launching the game and remain here between matches. The lobby is persistent and Steam-native.

### 3.2 Friends System

- Search for Steam users by name or Steam ID.
- Send and receive friend requests.
- View online/offline status and current activity (e.g. 'In Lobby', 'In Match').
- Remove friends.

### 3.3 Messaging

- Direct messaging between friends via the in-lobby chat panel.
- Party-wide group chat visible to all party members.
- System notifications for invites, match found, and squad selection.

### 3.4 Party System

- A player may form a party of up to 5 players for Ranked, or up to 32 for Opens.
- The party leader may invite friends from the friends list.
- Invited players receive a lobby invite notification and may accept or decline.
- The party enters matchmaking together and is placed on the same team.
- The party leader may kick members.
- Parties are disbanded when all members leave or the leader disconnects.

### 3.5 Game Settings (Host / Lobby Leader)

| Setting              | Description / Options                                                                          |
| -------------------- | ---------------------------------------------------------------------------------------------- |
| **Map**              | Selected from available map pool                                                               |
| **Team Size**        | Fixed at 5v5 for Ranked. 2–32 per side for Opens. Fixed at 5v5 or 10v10 for Regiment Battles.  |
| **VP Win Timer**     | Duration a team must hold all VPs to win (default: 5 minutes)                                  |
| **Match Time Limit** | Optional total match duration before score-based tiebreaker                                    |
| **Friendly Fire**    | Toggle friendly fire on/off                                                                    |
| **Squad Limits**     | Number of each squad type available across both teams                                          |

### 3.6 Squad Selection (Pre-Match)

Before a match begins, each player selects their squad. Squad types have limited availability shared across both teams to prevent mirror stacking. Once a squad type is fully claimed, it is locked for the remaining players. Selection is first-come, first-served within the lobby.

### 3.7 Unit Customisation (Future)

A unit customisation panel is reserved in the lobby UI for future development. This will allow players to apply cosmetic modifications to soldiers (uniform skins, helmet variants, etc.). No gameplay stats will be affected.

## 4. Squads

### 4.1 Squad System Overview

Each player controls exactly one squad per match. Squads are the core tactical unit of the game. A squad is composed of individual soldiers, each with independent stats, stress levels, and inventory. Players issue orders to individual soldiers or to the whole squad simultaneously.

In a match with several players per team (e.g. 4v4), each team has one squad per player. A player commands only their own squad; other friendly squads are teammates (shown as friendly, never ordered). A joining player gets the first squad of their team that no other player commands; a squad with no player is run by a bot (§11.1).

### 4.2 Standard Squad

| Standard Squad Composition |                                        |
| -------------------------- | -------------------------------------- |
| **Squad Size**             | 4 Soldiers                             |
| **Faction**                | Allied (M1 Garand) or Axis (Gewehr 43) |
| **Weapon**                 | 1x Semi-Automatic Rifle per soldier    |
| **Grenades**               | 5x Fragmentation Grenades per soldier  |
| **Rifle Ammo**             | 70 rounds per soldier                  |
| **Medkit**                 | 1x Medkit per soldier                  |
| **C4**                     | 1x C4 breaching charge per soldier (§7.2) |
| **Wall Reinforcement**     | 1x Wall Reinforcement per soldier (§13.3) |
| **Role**                   | Versatile all-round infantry squad     |

The Standard Squad is the only available squad type in the initial release. Future squad types (e.g. Machine Gun team, Sniper pair, Mortar crew) are planned. Soldiers cannot change class mid-match, but may trade weapons and equipment with fallen soldiers.

### 4.3 Squad Cohesion

A squad must stay together: its two furthest-apart living soldiers may never be more than **30 m** apart.

- **Measured** in a straight line between cell centres (30 cells = 3000 uu), not along the walking route.
- **Judged on destinations.** Each soldier counts at the cell it is heading for (its current cell when standing still), so a move is checked when it is ordered. A path that detours round a wall may briefly take a soldier further away mid-route; that is allowed. _Provisional._
- **Who counts:** every living soldier in the squad (only that squad: other friendly squads don't count), including Stunned and Injured soldiers (who can't walk far, so they anchor the squad until dragged) and dragged soldiers, who count wherever their dragger is heading. Dead soldiers don't count. A dragger is never held back by the soldier it is dragging.
- **Too far:** a move order whose destination would break the limit is not refused outright. The soldier goes to the reachable free cell nearest the clicked cell that keeps it within 30 m of every squad-mate, and shows **SQUAD LIMIT**. If it is already as close as it can get, the order is refused with the same message. _Provisional (could instead refuse the whole order)._
- **Already beyond the limit** (e.g. after a fallback cell in a group move): a soldier may still move as long as the move doesn't take it further from the squad-mate it is too far from, so it can always close up again.
- **Group moves** (§13) keep their formation. If the formation would end up too far from selected-but-unable or unselected soldiers left behind, the whole group stops at the nearest point the limit allows (the anchor soldier shows **SQUAD LIMIT**).
- Checked on the server like every order. The limit is `SquadCohesionLimit` in the soldier data asset (`USoldierData`, `DA_SoldierTuning`); 0 turns it off.

## 5. Soldiers

### 5.1 Soldier States

| State        | Description |
| ------------ | ----------- |
| **Active**   | Fully operational — can move, shoot, throw grenades, take cover, trade. |
| **Stressed** | Active but with degraded performance. |
| **Stunned**  | Incapacitated; cannot act. Knocked prone for the duration of the stun, then returns to its previous stance. May be dragged by an allied soldier (see §5.4). Recovery time is dynamically based on grenade proximity: **7 seconds** if within 1 m, and **5 seconds** if within 2.5 m (4 m for C4, §7.2). |
| **Injured**  | Shot from more than 5 metres away (a hit from 5 metres or closer kills outright). Can only crawl (reduced movement speed). Cannot sprint or use cover normally. If hit by another shot while Injured, the soldier dies immediately. May be dragged by an allied soldier (see §5.4). |
| **Dead**     | Removed from active play. Leaves a corpse that can be looted by nearby soldiers. The corpse may be dragged by an allied soldier (e.g. to clear the position or move to cover; see §5.4). |

### 5.2 Soldier Stats

- **Health State:** One of four states: Alive, Stunned, Injured, or Dead.
- **Stress Level:** A continuous value (0–100) affecting combat performance.
- **Ammo Count:** Current magazine capacity plus reserve rounds. Each soldier carries 70 rounds of rifle ammunition by default.
- **Grenade Count:** Each soldier starts with 5 fragmentation grenades.
- **Medkit:** Each soldier carries exactly 1 medkit. A medkit can be used on an adjacent Injured allied soldier to restore them to the Active state. Applying a medkit requires a **7-second channel time** during which the user is vulnerable. Once used, the medkit is consumed.
  - **Adjacent:** the patient is on the medic's own cell or one of the 8 cells around it (diagonals count, the same reach as looting, §5.5).
  - **Ordering it:** right click the injured ally and choose **Use medkit** (§13). The nearest selected soldier able to treat does it, or failing that the nearest soldier in the squad; the menu names who. If that soldier isn't adjacent, it walks to the nearest free adjacent cell first (if the patient crawls off meanwhile it follows a few times, then gives up with "CAN'T REACH").
  - **Who can treat:** only an Active soldier (not Stunned, Injured, Shocked, dragging or being dragged) carrying a medkit, on an Injured ally that isn't being dragged. Not on itself. One medic per patient at a time.
  - **The channel:** starts once medic and patient are both standing still within reach. The medic turns to the patient, stays put and can't shoot. It is stopped by any new order to the medic, the medic being stunned, injured, killed or Shocked, or the patient dying, being dragged or crawling out of reach. A stopped treatment keeps the medkit: it is only used up when the 7 s finish.
  - **Result:** the patient becomes Active again (a later hit from beyond 5 m injures rather than kills) and stays prone until given a stance.
  - Tuning: `MedkitChannelTime` and `MedkitReachCells` in the soldier data asset.

### 5.3 Hit Chance Calculation

Every shot fired resolves through a hit chance calculation that combines all relevant attacker and defender factors into a single percentage value. This value is then tested against a random roll to determine whether the shot connects.

- **Range Bands:** The weapon's Short / Mid / Long accuracy (§6) applies to targets up to **8 m / 20 m / 40 m** away. Soldiers cannot fire at targets beyond 40 m. The bands are per rifle (`ShortRangeMax`, `MidRangeMax`, `LongRangeMax` in its weapon data asset); both rifles use the same bands.
- **Target Acquisition:** When a soldier has no explicit target order, they automatically select the enemy in their line of sight with the highest calculated hit chance. This happens only while the soldier stands still (a move is never interrupted); after a move it opens fire once it stops. An automatically chosen target is swapped only for one at least **5** percentage points easier to hit (_provisional_), and is dropped without a message when it goes out of sight. A face order doesn't stop it: a soldier facing away turns to engage a visible enemy.
- **Hold Fire:** A soldier ordered to hold fire (H, §13) does no automatic target acquisition at all, to save ammo against enemies it has little chance of hitting (e.g. behind hard cover). Ordering it stops any fire in progress, automatic or ordered. An explicit attack order is still obeyed; when that target dies or is lost the soldier goes back to holding rather than picking a new one. Hold fire doesn't affect grenades, movement or anything else, and stays on (through stuns and injuries) until released. Released soldiers resume automatic targeting at once. Only the soldier's own team can see that it is holding fire.
- **Shot Resolution:** A random integer is generated in the range 0 to 100,000 (inclusive), giving a resolution of 3 decimal places (0.001%) per step. If the random roll is less than or equal to the hit chance threshold, the shot hits. This roll is performed on the dedicated server only.

### 5.4 Dragging Soldiers (Stunned, Injured, or Dead)

An active soldier may drag an allied soldier who is Stunned, Injured, or Dead. **Start drag:** when the active soldier is within **1 m** of the target, hold **Control** and **left-click** the target soldier. The dragging soldier then moves at **crouch speed** (as if crouching), with the dragged soldier following behind; they cannot shoot or throw grenades while dragging. **Stop drag:** hold **Control** and **left-click** the dragged soldier again to release them.

- **Range:** "within 1 m" is measured centre to centre on the grid, so the dragger must stand on the same cell as the target (e.g. a corpse) or on an orthogonally adjacent cell; a diagonal neighbour (1.41 m) is too far. The dragger must be standing still to take hold.
- **Who drags:** with several soldiers selected, the closest selected soldier able to drag takes hold. Only Active soldiers can drag (not Stunned, Injured or Dead), each soldier drags at most one ally, and an ally can only be dragged by one soldier.
- **Movement:** the dragger moves crouched at crouch speed. On each step, the dragged soldier is pulled into the cell the dragger has just left, so it always trails one cell behind.
- **Dragged soldier:** cannot take orders while being dragged.
- **Automatic release:** the drag ends if the dragger is Stunned, Injured or killed, or if the dragged soldier recovers (its stun wears off).

### 5.5 Looting & Trading

Soldiers may interact with dead soldiers or dropped items on adjacent or occupied grid squares to swap weapons, collect ammo/grenades, or collect unused medkits. Dropped items persist on the map until collected or the match ends.

- **Area menu:** pressing **Tab** opens one loot / trade menu for the selected soldier (the lowest-numbered one if several are selected). It lists the soldier's own kit and, in a single list, everything within reach: the bodies of dead soldiers of either team and piles of dropped items on the soldier's own cell or any of the **8 cells around it** (diagonal neighbours count as adjacent). The menu stays open while the player keeps commanding and updates as soldiers move; pressing a number key switches which soldier is looting. Tab closes it.
- **Who can loot:** only Active soldiers (§5.1), and not while dragging or being dragged.
- **Bodies:** a dead soldier keeps its full kit (rifle with its loaded rounds, loose ammo, grenades, medkit). Dragging a body (§5.4) moves its kit with it.
- **Swap rifle:** a soldier always carries exactly one rifle. Taking a rifle from a body or pile swaps it for the soldier's own, which is left in its place. Each rifle keeps the rounds loaded in it. Swapping cancels any reload or attack in progress.
- **Ammo is per rifle:** rounds fit only the rifle they were made for (M1 Garand or Gewehr 43). A soldier can carry and trade rounds for the other rifle but can only load rounds for the one it holds.
- **Take:** ammo, grenades and medkits can be taken a whole stack at a time (click) or one at a time (Shift + click; one clip for ammo). **Take all** collects every round, grenade and medkit in reach but leaves rifles alone.
- **Drop / trade:** a soldier can drop its own ammo, grenades and medkits on its cell, where they form a dropped-item pile any soldier can loot. This is how squad-mates trade. Rifles can't be dropped, only swapped.
- **Piles:** one pile per cell, shared by everything dropped there and removed once empty.
- **Provisional (not yet specified):** looting is instant, with no carry limits on ammo, grenades or medkits.
- **Server authority:** loot requests are checked and applied by the server (state, reach and that the items still exist); clients only send requests.

## 6. Weapons

_The M1 Garand and Gewehr 43 are designed to be broadly similar in performance, with minor asymmetric trade-offs to reflect the factions without creating imbalanced team compositions._

| Stat                  | M1 Garand (Allied)      | Gewehr 43 (Axis)           |
| --------------------- | ----------------------- | -------------------------- |
| **Fire Rate**         | ~40 rounds/min          | ~38 rounds/min             |
| **Ammo Capacity**     | 8 rounds (en-bloc clip) | 10 rounds (detachable mag) |
| **Short Range Acc.**  | 85%                     | 80%                        |
| **Mid Range Acc.**    | 65%                     | 68%                        |
| **Long Range Acc.**   | 35%                     | 42%                        |
| **Short Range Supp.** | 60                      | 55                         |
| **Aim Time**          | 0.4 seconds             | 0.45 seconds               |
| **Reload Time**       | 2.5 seconds (base)      | 2.2 seconds (base)         |

### 6.1 Machine Guns

_Provisional: numbers and rules are first play-test values. No squad type carries a machine gun yet (§4.2, §15). For testing, soldier 2 of every player-commanded squad starts with its faction's MG instead of its rifle and rifle ammo (`TestMachineGunSoldier` on the game mode; 0 turns it off). Bot squads keep their rifles._

| Stat                  | M1919 Browning (Allied) | MG 42 (Axis)              |
| --------------------- | ----------------------- | ------------------------- |
| **Cyclic Rate**       | 450 rounds/min          | 1,200 rounds/min          |
| **Burst**             | 5 rounds, 1.0 s pause   | 5 rounds, 1.3 s pause     |
| **Belt**              | 250 rounds              | 250 rounds                |
| **Rounds Carried**    | 500 (belt + 250 spare)  | 500 (belt + 250 spare)    |
| **Short Range Acc.**  | 30% per round           | 25% per round             |
| **Mid Range Acc.**    | 20% per round           | 17% per round             |
| **Long Range Acc.**   | 12% per round           | 10% per round             |
| **Short Range Supp.** | 80 (unused, §8.2)       | 85 (unused, §8.2)         |
| **Aim Time**          | 0.6 seconds             | 0.6 seconds               |
| **Reload Time**       | 8 seconds               | 7 seconds                 |
| **Set-up Time**       | 2 seconds               | 2 seconds                 |
| **Firing Arc**        | 90°                     | 90°                       |

- **Bursts.** Each trigger pull fires a burst at the cyclic rate, then waits out the burst pause. Every round is a separate shot: it gets its own hit roll (§5.3), adds stress to the target (§8.2, reduced by cover as usual) and causes cross-fire stress. Stress stretches the pause by the "time between shots" multiplier (§8.1); the cyclic rate is mechanical and doesn't change. Range bands and the 40 m limit are as for rifles.
- **Set-up.** An MG can't fire until it is set up. The gunner sets it up whenever it stands still, facing the way it means to fire (its target, otherwise its facing order, otherwise the way it stands). Set-up takes **2 s** (scaled by the aim-time stress multiplier) and starts once the gunner faces that way. Aim time (§6) then runs as for a rifle. The status line shows **SETTING UP Ns**.
- **Firing arc.** Set up, the MG can fire at anything within **45°** either side of the way it faced when the set-up finished. Automatic targeting (§5.3) prefers targets inside the arc and turns to one outside it only when nothing inside can be shot.
- **Tear-down.** The MG is torn down instantly, and must be set up again, when the gunner moves, changes stance, is stunned, dragged or drags, starts a medkit treatment or buildable, or its target or facing order lies outside the arc. Reloading and throwing a grenade within the arc don't tear it down.
- **Ammo.** Each MG has its own ammo, separate from the rifles' (§5.5). MGs are looted and swapped like rifles; a soldier who swaps to an MG must set it up before firing.
- **Squad bar.** A belt shows as a fill bar instead of one pip per round. Tags: **SETTING UP** with seconds left, then **MG SET UP**.
- **Set-up pose (cosmetic):** during the set-up the gun swings from the gunner's hands down onto whatever is in front of it, pivoting on the grip until something touches. Prone, the bipod feet rest on the ground. Behind hay, a low wall or a window sill the legs fold and the barrel lies on top (bipod feet only count on open ground). Set up, the gun traverses with the gunner and kicks back about 1.5 cm per round in place of the rifle fire clip. Standing or crouched in the open nothing is in reach, so the gun stays in the gunner's hands. _Open question:_ require prone or cover to set up, or add a hip/shoulder MG hold. Tuning: `BipodFoot`, `BarrelUndersideHeight`, `SetupRecoil` on the weapon data asset.
- **Measured tuning (v0.43, `squad.DemoMG`):** M1919 3.25 rounds/s and MG 42 4.73 rounds/s, against the M1's 0.575 shots/s over 40 s. Exposed target: hits per second at short / mid / long range are about 2× the M1's, and a kill (2 hits) comes about 25% faster at mid range and about 35% faster at long range. Covered targets: 0% (the best MG accuracy, 30%, never beats the weakest cover). Stress: an exposed target is Shocked after 5.7 s, one behind a low wall after 11.9 s. Four M1s take 18.3 s, one M1 takes 109 s. With its original 7-round bursts and 1.2 s pause the MG 42 was about 45% stronger at suppression (8.3 s low-wall pin), so it was changed in v0.44 to 5-round bursts with a 1.3 s pause. It now fires 3.38 rounds/s and pins a low-wall target in 12.6 s (expected from the model; not re-measured in game). It lands slightly fewer hits than the M1919 (0.57 vs 0.65 per second at mid range). _Open point:_ is immunity behind any cover intended?
- **Placeholders:** the MGs use the M1 Garand's fire and reload sounds, and the gunner holds the MG with the rifle animations while it isn't set up.
- Tuning: `RoundsPerBurst`, `BurstPause`, `SetupTime`, `SetupArcDegrees` and `StartingRounds` on each weapon's data asset (`DA_M1919`, `DA_MG42`), alongside the rifle stats.

## 7. Grenades

Each soldier carries 5 fragmentation grenades. A grenade is thrown from the centre of the grid square the soldier throws from (where it stands, or the square it steps back onto if it was moving; see **Throwing** below), to ensure deterministic targeting. Maximum throw range is **10 m**, measured from that square to the target square.

### 7.1 Grenade Effects

The blast is **5 m across** (2.5 m radius), measured on the ground from where the grenade comes to rest.

- **Landing within 1 metre:** 10% chance to KILL the soldier outright. If no kill, the soldier is stunned for **7 seconds**.
- **Landing within 2.5 metres:** STUN — Soldier is incapacitated for **5 seconds**.
- **Outside 2.5 metres:** No effect.

Grenades follow a ballistic arc, cannot pass through solid walls, but can be thrown through windows. On landing, the grenade bounces once before coming to rest. It detonates 0.5 seconds after the bounce. Grenade effects apply to every soldier in range, friend or foe, unless cover shields them.

**Cover shields the blast.** When the grenade goes off, the line from the centre of its cell to the centre of each soldier's cell is traced through the grid as for sight lines (§9.3). Any cover cell on that line shields the soldier from the kill roll and the stun:
- **Full walls, wall corners and trees** always shield.
- **Hay bales, low walls (and rubble, §9.4) and windows** shield, unless the soldier is right up against that cover cell (the cover cell touches the soldier's cell, diagonals included) with its head above the top (hay 75 cm, low wall 90 cm) or the window sill (90 cm). Standing (160 cm) and crouched (110 cm) soldiers there are peering over or through it and are caught. Prone soldiers (40 cm) are shielded, so a soldier that sees the grenade coming and **goes prone before it goes off** is safe. Stunned and injured soldiers are prone.
- A soldier leaning out past a wall's edge (§9.2) is also exposed from the open cell beside the wall's end. It is shielded only if cover shields both cells.
- A line passing exactly through a grid corner is shielded only if both cells beside the corner shield.
- Distance is still measured on the ground. Cover never extends the blast, it only stops it.

**Throwing:** a throw is a short sequence that exposes the thrower, so it can be punished:
1. The soldier finishes the step it is on and stops.
2. **Step back (only if it was moving when ordered):** it steps back one cell onto the cell it just came from (its previous cell), walking at its stance's movement speed (about 0.4 s standing, 0.6 s crouched, 1.3 s prone) while still facing the throw. During the step it takes the grenade out and pulls the pin (animated). If it can't step to its previous cell (another soldier is on it or heading there, cover blocks it or a corner is cut, or it is the target cell), it steps to the free cell directly away from the target, then one of the two cells either side of that. If none is free, or the soldier is Shocked (it can't move, §8.1), it throws from where it stands. A soldier standing still when ordered skips this step and throws from where it stands; it pulls the pin during the wind-up instead.
3. **Wind-up:** **0.5 seconds** of throwing action, then the grenade leaves the hand.
4. **Follow-through:** **0.4 s** (_provisional_) finishing the throwing motion.

The throw takes priority over firing: from the order until the end of the follow-through the soldier doesn't shoot and faces the throw (even with a target, and over any face order), then turns back to its target and resumes fire. A move order, a stun or death before the grenade leaves the hand cancels the throw and no grenade is used (a step back already under way still finishes). Range and cover are checked when the order is given (from the cell it will step back to) and again on release (from where it actually stands).

**Grenades and cover:**
- **Full walls** stop grenades at any height: a grenade can't be thrown through a full wall or over it.
- **Half walls** (low walls) and **hay bales** can be thrown over.
- **Windows** can be thrown through, between the sill (90 cm) and the top of the opening (200 cm). The wall above and below the opening stops grenades like a full wall.
- **Trees** stop grenades below 4.2 m (trunk and canopy).
- Each piece of cover is treated as a slab of its real thickness running along the piece, so a grenade can drop into the cell right behind a wall.
- The thrower picks the arc automatically. It tries the natural lob first, then flatter or higher arcs, up to a 10 m lob; a flatter arc is how a grenade goes through a window.
- If no arc reaches the target cell, the throw is refused and the soldier shows **BLOCKED**. No grenade is used.
- The grenade bounces just short of the centre of the target cell, so the bounce never carries it into cover.

_Provisional (to be confirmed):_ a soldier must wait **1 second** between throws.

### 7.2 C4 Breaching Charge

Each soldier carries **1 C4** (§4.2). It is placed on a weakened wall (§9.4) through the Build sub-wheel (§13.3), which also covers who places it, the **7 s** placement and what interrupts it.

- **Stuck to the face.** A finished C4 sticks to the face of the wall cell the soldier was kneeling at, on the soldier's side, as a small charge with a blinking red light that blinks faster as the fuse runs down.
- **Fuse:** it detonates **5 s** after it is placed (_provisional_). It can't be disarmed or moved.
- **Detonation:**
  - **Breach:** the wall is breached (§9.4), unless it was already reinforced when the C4 finished being placed.
  - **Blast:** every soldier round the charge, friend or foe, gets the frag grenade's effects (§7.1) but with C4's own radii, centred on the charge: within **1 m**, 10% chance to be killed, otherwise stunned for 7 s; within **4 m**, stunned for 5 s (_provisional_; larger than a grenade's 2.5 m). The wall doesn't shield anyone, since it is the thing being blown apart, so soldiers on the far side are caught too. Distance is measured on the ground and, unlike a grenade (§7.1), no other cover shields anyone from it either. The soldier who placed it is credited with any kills (the kill feed shows the explosive icon and "C4"). It is kneeling about half a metre from the charge, so it must move away during the fuse.
- **On a reinforced wall:** a C4 that finished being placed on a wall that was already reinforced still explodes (blast as above) but doesn't breach. (C4 can't be *started* on a reinforced wall, but the wall may be reinforced while the C4 is still being placed.)
- **Dud:** if a reinforcement on the same wall **finishes while the C4's fuse is burning**, the C4 is a dud: it fizzles out with a grey puff, its light goes out and it shows **DUD** for a few seconds, then disappears. No breach and no blast. With a 5 s fuse, this means the reinforcement must already have been under way before the C4 finished being placed.
- **Who sees it:** the placing team always sees its charges. The enemy sees one only while one of its living soldiers has a sight line (§9.3) to the open cell in front of the charge, at the charge's height (60 cm), within the 40 m sight range. The explosion is seen by everyone.
- Server-only like grenades: the fuse, breach, blast rolls and dud check all run on the server; the charge (state, team, fuse, enemy visibility) replicates. Tuning: `C4FuseTime`, `C4KillRadius` and `C4StunRadius` in the soldier data asset (`USoldierData`, `DA_SoldierTuning`); the blast reuses the grenade's kill chance and stun durations.

## 8. Stress System

Stress is a continuous value (0 to 100) for each soldier. High stress impairs combat performance, modelling the psychological impact of sustained fire.

### 8.1 Stress Tiers & Effects

Stress is broken into four scaling tiers:

- **None (<33%):** Normal operation.
- **Minor (33%–66%):** Minor reductions to fire rate, fumbled reloads, and aim stability.
- **Major (66%–99%):** Severe reductions to fire rate, reloads, and accuracy.
- **Shocked (100%):** The soldier cannot move, and suffers maximum performance penalties.

Tier penalties (_provisional, to be confirmed_). Multipliers apply to the weapon's stats (§6); the accuracy multiplier scales the range-band accuracy before cover is subtracted (§9.1).

| Tier    | Time between shots | Reload time | Aim time | Accuracy |
| ------- | ------------------ | ----------- | -------- | -------- |
| None    | ×1                 | ×1          | ×1       | ×1       |
| Minor   | ×1.15              | ×1.25       | ×1.25    | ×0.95    |
| Major   | ×1.5               | ×1.5        | ×1.5     | ×0.75    |
| Shocked | ×2                 | ×2          | ×2       | ×0.5     |

For example, a mid-range M1 Garand shot (65%) from a Major-stressed soldier at a target behind a low wall becomes 65 × 0.75 − 50 = **0%** (48.75 − 50, clamped at 0), and at an exposed target **48.75%**.

- **Shocked:** a soldier finishes the step it is on and then can't move; move orders show **SHOCKED**. It lets go of any soldier it is dragging and can't start a drag. _Provisional:_ it can still fire (with the penalties above), throw grenades and change stance. It stays Shocked while stress is at 100, so with no further fire it recovers after the decay delay plus about a second.
- **Stressed** (§5.1) means the Minor tier or above. The status label is tinted yellow (Minor) or orange (Major) and shows **SHOCKED** at 100; a selected soldier's label shows its stress as **S##**. The hit-chance preview adds **(STRESSED)** when the shooter's accuracy is reduced.
- Stress is still gained and still decays while Stunned or Injured. It resets to 0 on death.

### 8.2 Stress Gain & Decay

- **Gain Triggers:** Each bullet shot at the soldier adds **5% stress**, whether it hits or misses. Friendly soldiers firing from behind the soldier at a target in front also causes cross-fire stress.
- **Cover:** the stress from a shot is reduced by the same percentage as the hit chance, using the cover that applies to that shot (§9), including the halved value when the soldier has its back to the cover. Behind a low wall (50%) a shot adds 2.5 stress; with its back to the wall (25%), 3.75. _Provisional._
- **Cross-fire** (_provisional values_): when a soldier fires, every living squad-mate within **1.5 m** of the line of fire, between the shooter and the target and at least **1 m** in front of the shooter, gains **2.5 stress**. Cover does not reduce cross-fire stress.
- **Decay Rate:** Stress decreases naturally at a rate of **1% per second** when no stress-gain triggers are active. "Active" means stress was gained in the last **1 second** (_provisional_); decay resumes after that.
- Stress, the tiers and their penalties are computed on the server only (§2). Tuning values are kept in the soldier data asset (`USoldierData`, `DA_SoldierTuning`). The console command `squad.SetStress <0-100>` sets the selected soldiers' stress for testing.
- _Not yet used:_ the weapons' **Short Range Supp.** stat (§6). Its meaning isn't defined yet; it may later scale the stress a shot causes.

## 9. Cover System

Soldiers can occupy cover positions on the map. Cover reduces incoming hit probability from the covered direction and reduces stress gain from incoming fire. A soldier is exposed if attacked from a flank or rear.

### 9.1 Cover Protection Values

Incoming fire hit chances are reduced by the following flat percentages:

- **Soft Cover (Hay Bales):** 30% reduction.
- **Low Wall / Half Wall:** 50% reduction.
- **Wall Corner / Edge:** 60% reduction.
- **Building Window:** 40% reduction. _(Was 70%; lowered in v0.15 because shots at window positions almost never hit.)_
- **Tree:** 40% reduction. _Provisional (to be confirmed)._

"Flat" means percentage points are subtracted from the weapon's range-band accuracy, never going below 0%. For example, a mid-range M1 Garand shot (65%) at a soldier behind a low wall becomes 65 − 50 = **15%**. Only the single best cover applies; reductions do not stack.

### 9.2 Cover Rules

- **Cover objects occupy grid cells** and cannot be walked through. Hay bales, low walls, full walls, window walls, trees and cover props (§9.5) each take one or more cells. Cover is solid: it blocks physical objects and stray rounds (a missed shot that runs into cover stops there). Grenades are stopped as described in §7.
- **Taking cover:** a soldier is in cover when a cover object stands on one of the four cells orthogonally adjacent to theirs. Diagonal neighbours do not give cover.
- **Direction and flanking:** that cover protects against fire arriving within **60°** of the direction from the soldier to the cover cell. Fire from further round (flank or rear) ignores it.
- **Leaning:** a soldier leans on (is fully in) cover while facing within **75°** of it; facing the corner of a wall counts. With its back to the cover it is not leaning and the cover gives only **half** its reduction (e.g. a low wall gives 25 instead of 50). Soldiers turn to lean on adjacent cover when they finish a move, unless they were given a face order during the move (they keep looking that way); a facing order can turn them away again. The hit-chance preview shows "BACK TURNED" when this applies.
- **Same cover, opposite sides:** cover does not count against an attacker who is itself up against that cover cell from the other side (the attacker's cell touches the cover cell, diagonals included). Two soldiers either side of the same hay bale shoot each other as if exposed.
- **Leaning out past a wall's edge:** a soldier standing behind the end cell of a full wall and leaning on it looks out past the edge, as if from the open cell beside the wall's end. From there it can see and shoot (and be seen and shot from) everything beyond the wall's row, from that open column back along the wall. At a building corner, or at the end of a wall that carries on away from the soldier (the way it faces), it leans out sideways instead and looks down the wall's outer face: it sees everything beyond the open cell's row, from that open column outwards. If both sides of a wall's end are open, it leans out on the side it is turned towards. It keeps Wall Corner / Edge cover (60%) against that fire. It needs open ground (no cover) beside the wall's end, and it can't lean out while moving, dragging or being dragged. Turning its back to the wall ends the lean.
- **Full walls block line of sight.** A shot or sight line (cell centre to cell centre) that passes through a full-wall cell is blocked. A soldier beside a full wall can only be seen or shot from angles that get past its edge, and then has Wall Corner / Edge cover (60%). Hay bales, low walls, windows and trees also affect line of sight; see §9.3.
- Cover values, the 60° arc, the 75° leaning angle, the not-leaning multiplier and the reduction mode are kept in a data asset (`UCoverData`).
- Cover also reduces stress from incoming fire (§8.2).
- _Not yet implemented:_ how Injured soldiers' "cannot use cover normally" (§5.1) affects cover.

### 9.3 Line of Sight & Vision

- **Team vision.** A player sees an enemy soldier only while at least one of their own living soldiers has line of sight to it. Enemies nobody can see are not shown and cannot be hovered or targeted. The server decides who is spotted; clients only render it.
- **Shooting needs sight.** A soldier can only fire at an enemy it can see itself (a squad mate spotting the enemy is not enough). An attack order ends with "NO LINE OF SIGHT" if the target goes out of sight.
- **Sight lines** run from the centre of the viewer's cell to the centre of the target's cell (the cell the soldier's body is in, switching halfway through a step), rising or falling in a straight line from the viewer's sight height to the target's. Sight heights by posture: **Standing 160 cm**, **Crouched 110 cm**, **Prone 40 cm**. Stunned and Injured soldiers are prone; corpses count as prone. Sight is symmetrical: if A can see B, B can see A.
- **Cover is not see-through.** Each cover cell the line passes through blocks it as follows:
  - **Hay bales** (top 75 cm) and **low walls** (top 90 cm): block where the line runs below the top. Standing and crouched soldiers see over them; a prone soldier right behind one is hidden from the front and sees nothing over it. Exception: a cover cell touching the viewer's own cell (diagonals included) doesn't block if the viewer's sight height is above its top. A standing or crouched soldier up against the cover looks down over it and sees a prone soldier on the other side, and sight stays symmetrical, so that prone soldier sees them too.
  - **Full walls:** always block.
  - **Windows:** visible through only between the sill (90 cm) and the top (200 cm), so a prone soldier can't see or be seen through a window.
  - **Trees:** always block.
- **Seeing round corners.** Because the line runs between cell centres, a soldier tucked behind cover is seen once the viewer's angle takes the line past the cover's end. For a soldier lying right beside the last cell of a low wall or full wall, that is roughly 45° or more off the wall's face. A line passing exactly through a grid corner is blocked only if both cells beside that corner block it. A soldier leaning out past a full wall's edge (§9.2) also sees from the open cell beside the wall's end; sight stays symmetrical.
- Sight heights, cover heights and whether trees block are kept in `UCoverData`. The console command `squad.RevealAll 1` shows every soldier for testing.
- **Maximum sight range:** **40 m** (cell centre to cell centre), the same as the rifles' Long range, so a soldier never spots an enemy it couldn't shoot at. Symmetrical like all sight. Kept in `UCoverData` (`MaxSightRange`).
- _Not yet implemented:_ last-known positions of enemies that drop out of sight, and network relevancy so the server never sends unseen enemies' positions to clients (needed against wallhacks once replication exists).

### 9.4 Destructible Walls

Some maps contain **weakened full walls** that can be blown open (breached) with a demolition charge (C4).

- **Before the breach** a weakened wall is an ordinary full wall in every respect: it can't be walked through, blocks line of sight and grenades, and gives Wall Corner / Edge cover (60%) at its ends, including leaning out past its edge (§9.2).
- **Spotting them:** a weakened wall is a darker, weathered shade than a normal wall, with dark cracks running up both faces, chipped plaster patches and a notch knocked out of its top, so players can pick them out.
- **Breach:** the server destroys the wall. Every cell of the wall becomes **rubble**:
  - Rubble can be **walked over** (it no longer blocks movement or pathfinding), and soldiers can stand on it.
  - Rubble gives **Low Wall cover (50%)** to soldiers on the orthogonally adjacent cells, with all the normal §9.2 rules (60° arc, leaning, same-cover-opposite-sides).
  - For **line of sight** it is a low wall (top 90 cm): standing and crouched soldiers see over it, a prone soldier right behind it is hidden from the far side, and the "up against low cover" exception of §9.3 applies.
  - For **grenades** it follows the low-wall rule (§7): grenades can be thrown over it, and onto or from a rubble cell.
  - Neighbouring full walls that now end beside the rubble count as wall ends (corner cover, leaning out), since rubble is open ground for that purpose.
  - The minimap and tactical map draw rubble in its own warm rubble colour; cover overlays, move range and the bot pick up the change immediately.
- **Reinforcement:** a wall can be fitted with a reinforcement (§13.3). A **reinforced wall can't be breached**: a charge set off against it does nothing to the wall, though its blast still hits nearby soldiers (§7.2). Reinforced walls show bolted steel plates over both faces, each propped by a raking brace.
- **Visuals (cosmetic, never affect play):** on the breach a broken stub of the wall and a heap of masonry stay on the wall's cells (kept below the 90 cm low-wall height), debris chunks are thrown away from the charge and settle on the nearby cells (up to ~3.5 m, _provisional_), and a cloud of dust billows out for about 1.6 s (_provisional_). Debris placement is deterministic (seeded per cell), so every player sees the same rubble; players joining after the breach just see the settled rubble. The intact weakened look is modelled into its wall mesh. The stub-and-heap meshes, chunk meshes, chunk counts, scatter radius, plate / dust colours and timings are properties of `ABreachableWall`.
- The breach state replicates to every client, including late joiners.
- Debug console commands: `squad.ListBreachableWalls`, `squad.BreachWallAt X Y [FromX FromY]`, `squad.BreachAllWalls`, `squad.ReinforceWallAt X Y`; read-only probes `squad.CellInfo X Y`, `squad.PathTest`, `squad.LOSTest`, `squad.CoverTest`, `squad.GrenadeArcTest`.

### 9.5 Cover Props

_Provisional._ Props placed as cover give one of the existing cover types (§9.1) on every cell they cover, and follow all of §9.2–§9.3 for that type. Unlike walls and bales, which are slabs across their cells, a prop is a **solid block**: a grenade below the type's height is stopped anywhere inside its cells (§7).

| Prop | Cells (long x wide) | Cover type | Blocks line of sight |
| ---- | ------------------- | ---------- | -------------------- |
| Well | 1 x 1 | Low wall (50%) | Low wall rule (90 cm) |
| Water trough | 2 x 1 | Low wall (50%) | Low wall rule |
| Crate stack | 1 x 1 | Low wall (50%) | Low wall rule |
| Barrels | 1 x 1 | Low wall (50%) | Low wall rule |
| Woodpile | 1 x 1 | Low wall (50%) | Low wall rule |
| Sandbags | 1 per cell, in a row | Low wall (50%) | Low wall rule |
| Hay cart | 3 x 2 | Soft / hay (30%) | Hay rule (75 cm) |
| Abandoned car | 4 x 2 | Low wall (50%) | Low wall rule |
| Cargo truck | 6 x 2 | Wall corner / edge (60%) | Always, like a full wall |
| Tank wreck | 6 x 3 | Wall corner / edge (60%) | Always, like a full wall |

The car (about 1.4 m tall) and the hay cart (about 1.5 m) are taller than the heights their cover type uses for sight, so standing and crouched soldiers see over them. The meshes are cosmetic only; play uses the cover type. Implemented as `APropCover` and its presets (`AWellCover`, `ATruckCover`, …) and `ASandbagCover`.

## 10. Map & Grid System

Maps are square grids of varying dimensions. Each map cell (grid square) is a fixed size in world units: **1 square = 1 metre (100 Unreal units)**. Pathfinding, line of sight, and grenade mechanics all operate on grid coordinates.

### 10.1 Grid Scaling

Dimensions mathematically scale based on the player count:

- 1v1: 32x32 cells
- 2v2: 64x64 cells
- 3v3: 128x128 cells
- Scales up to 10v10 Regiment Battles.

### 10.2 Movement Speeds

Soldiers move cell-by-cell along calculated paths. Movement is **8-directional**: soldiers may step orthogonally or diagonally. A diagonal step covers √2 squares (≈1.41 m) and so takes ≈1.41× as long at the same speed; pathfinding (A*) uses these costs. Diagonal steps may not cut corners — both orthogonally adjacent squares must be passable.

- **Standing (Base Speed):** 2.4 squares per second (240 uu/s).
- **Crouched:** ≈1.58 squares per second (66%).
- **Prone:** ≈0.79 squares per second (33%).
- **Dragging:** Dragging soldier moves at crouch speed; see §5.4 for Control+click to drag/undrag.

### 10.3 Victory Points

Victory Points occupy a defined cluster of cells forming the capture zone. There is no minimum or maximum limit to the number of VPs per map; the count and layout are determined entirely by the map creator.

- **Ownership.** A VP belongs to the last team that had soldiers inside it while the other team had none. Every VP starts the match neutral.
- **Capturing** is immediate: the moment a team has soldiers in the zone and the enemy has none, the VP is theirs (no capture timer).
- **Contested.** While both teams have soldiers inside, the VP is contested and keeps its current owner (or stays neutral).
- **Empty.** When everyone leaves, the VP keeps its owner.
- **Who counts** (provisional): only soldiers able to act. Stunned, injured and dead soldiers neither capture nor contest. A soldier counts on the cell it is standing on.
- The server decides ownership; clients see the owner and contested state. On the map a VP shows its letter and a border on the ground, and a flag in the middle. The border and flag take the owner's colour (Allies blue, Axis red, neutral off-white); the border turns amber while contested.

### 10.4 Deployment Zones

Each team has one Deployment Zone (DZ): an **8x8-cell** block at its own edge of the map where its squad starts the match. Cover objects inside a DZ stay as they are. DZs are marked on the ground with a border and label in the team's colour.

### 10.5 Map: Farmstead

64x64 cells (2v2 size, §10.1); the default map. Cells are numbered from the south-west corner (0,0), X running east, Y running north.

| Zone | Cells (X, Y) | Size | Notes |
| ---- | ------------ | ---- | ----- |
| Allied DZ | 34–41, 1–8 | 8x8 | South edge, just inside the boundary wall. |
| Axis DZ | 34–41, 55–62 | 8x8 | North edge, mirroring the Allied DZ (clear of the ponds at the north centre). |
| VP A — Orchard | 9–14, 29–34 | 6x6 | Among the orchard trees, west. |
| VP B — Farmhouse | 30–35, 29–34 | 6x6 | Across the farmhouse's south doorway, centre. |
| VP C — Barn | 50–55, 29–34 | 6x6 | Inside the east building's walled room, east. |

All three VPs lie on the map's centre line, so each is the same distance from both DZs (in a straight line, centre to centre: A ≈ 37.5 m, B ≈ 27.5 m, C ≈ 31 m).

**Weakened walls (§9.4)** — _provisional placement:_

| Wall | Cells (X, Y) | Breach opens |
| ---- | ------------ | ------------ |
| Barn partition | 54–56, 35 | A second way from the barn's north room straight into VP C's walled room (otherwise only through the doorway at 52–53). |
| Farmhouse partition | 31–33, 37 | The farmhouse's interior wall just north of VP B, letting attackers in the north rooms flank VP B. |

## 11. Victory Conditions

- **Annihilation Victory:** A team wins immediately if all soldiers on the opposing team are killed.
- **Victory Point Victory:** A team wins if they hold all VPs simultaneously for a continuous 5-minute duration.
- **Holding** means owning (§10.3): a contested VP keeps its owner and still counts. The timer restarts whenever the team stops owning every VP. The duration is the VP Win Timer (§3.5, `VPWinTime` on the game mode).
- **Draw:** if both squads are wiped out at the same moment (e.g. by one grenade), the match is a draw.
- When the match ends the server shows the result (Victory / Defeat and how) to every player and the game pauses. Reporting the result for ELO (§14.4) is not yet implemented.

### 11.1 Play-Test Bot

For testing without a second player, a squad no player commands is given to a server-side AI bot (`ASquadBot`). It gives its soldiers only orders a player could give (move, stop, face, grenade), so it follows every rule (own line of sight to shoot, grenade arcs, stress, reloads, wind-ups). Its soldiers fire on their own (§5.3) exactly like the player's.

In a networked game (§14.1) the bots start once the first player has joined. A player who joins a team a bot is running takes the squad over at once, and a team every player has left goes back to a bot.

**Plays like a person, not a script:**
- **Simulated camera.** The bot sees through a camera with the same angle, field of view and zoom limits as the player's (it plays at a fairly wide zoom). It looks at one area at a time, pans at the player's pan speed, and moves its attention between its soldiers every few seconds (fighting soldiers first). A soldier coming under fire or getting hit off screen draws the camera there after a short delay, as a flashing squad card would.
- **Noticing enemies.** An enemy is noticed only when the bot's team spots it (§9.3), it is on the bot's screen, and no wall or tree hides it from the camera. Noticing takes a reaction delay (about 0.5–1 s) plus extra time for a prone enemy, an enemy in cover, or one near the edge of the screen. The bot remembers noticed enemies at their last seen cell for about 10 s after losing them.
- **One order at a time.** Orders go out with a short gap (about 0.3–0.8 s) between them, like clicks.
- **Finishing moves.** A soldier on the move usually completes its move into position. Only a good shot (45%+) at a noticed enemy with at least 5 steps still to go may make the bot stop it to fight, and that is decided once per contact (40% of the time).
- **Grenades** are noticed after a short moment when they land on screen; the bot then moves soldiers out of the blast if it has time.

**Plan:** one soldier per VP (unowned VPs first), spare soldiers reinforce the most threatened VP. Because of the squad cohesion limit (§4.3) it only spreads over VPs that fit inside 30 m together (and near any injured squad-mates); since an empty VP keeps its owner, it takes the rest in turn. On Farmstead, A and C are too far apart, so it holds A+B or B+C. Soldiers pick cells in their VP with cover facing where the enemy is believed to be, spaced apart. They throw grenades at recently seen enemies when shots are poor or enemies bunch up (never near a squad-mate), and fall back to nearby cover when pinned in the open with poor shots. When a squad-mate is injured, the bot notices after 1–2.5 s and sends the closest soldier with a medkit within 12 m that isn't under fire to treat it (§5.2), stops the injured soldier crawling so the medic can reach it, and leaves the medic alone until it finishes (a grenade landing nearby still makes it dodge).

All bot tuning (zoom, reaction times, order gap, contact rules) is on the `ASquadBot` actor. Console: `squad.Bot 0` stops the bot, `squad.BotDebug 1` draws its screen (yellow) and the enemies it is noticing (orange) or knows about (red), `squad.VPWinTime <seconds>` shortens the VP timer for testing.

**Several squads per team:** each squad no player commands has its own bot. A team's bots act like teammates who can see each other: each VP looks `FriendlySquadSpreadPenalty` (6 m) further away for every soldier another friendly squad has already sent there, so the squads spread over the VPs; a bot never throws a grenade near any friendly soldier and doesn't pick a cell another friendly squad holds or is heading for.

### 11.2 AI Test Matches (balancing)

Bot-only matches on a dedicated server with no clients, recorded for balancing. `Scripts/AIMatchBatch.ps1` runs a batch (one server process per match, on a fixed 30 Hz step as fast as the CPU allows, about a minute for ten 4v4 matches); `Scripts/Tests/analyze_match_stats.py` turns the records into tables. Results and reviews go in `Docs/Balancing/`.

- `squad.SquadsPerTeam <n>` — squads per team; extra squads are copies of the level's squad placed in the team's deployment zone. Set before the map loads.
- `squad.MinPlayers 0` — a dedicated server starts at once with bots only.
- `squad.MatchSeed <n>` — seeds the bots and every soldier's hit rolls (0: random). Runs with the same seed are close but not identical.
- `squad.MatchStats 1` — the server writes `Saved/MatchStats/<time>_seed<n>.json` (result, per-soldier and per-team totals, events) and `_shots.csv` (every shot: positions, range, stance, cover, hit chance, roll, result).
- `squad.MatchTimeLimit <s>` ends the record as a timeout; `squad.QuitAfterMatch 1` closes the server after writing it.
- `squad.SwapSides 1` — each faction starts in the other's deployment zone with its own rifles (tells a map-side bias from a faction bias).

The bots decide these matches, so the numbers show how the rules play under bot tactics, not under skilled players.

## 12. Game Modes, Ranking & Matchmaking

### 12.1 K-Factor Rules & ELO Tiers

All new players begin at an ELO of 100. K-Factors determine rating volatility:

- **New Players (<200 ELO):** K-Factor 50 (Fast calibration).
- **Standard Matches (200–2000 ELO):** K-Factor 30.
- **Veteran Matches (2001+ ELO):** K-Factor 15 (Protects high ranks).
- **Opens Mode:** Uses a 0.5x multiplier on the player's K-Factor.
- **Regiment Matches:** K-Factor 40.

**Player Rank Ladder:**

| ELO        | Rank                    |
| ---------- | ----------------------- |
| 0–100      | Private                 |
| 101–200    | Private First Class     |
| 201–300    | Corporal                |
| 301–400    | Sergeant                |
| 401–500    | Staff Sergeant          |
| 501–600    | Platoon Sergeant        |
| 601–700    | Master Sergeant         |
| 701–800    | First Sergeant          |
| 801–1000   | Sergeant Major          |
| 1001–1500  | Warrant Officer (W-1)   |
| 1501–2000  | Warrant Officer (W-2)   |
| 2001–2500  | Warrant Officer (W-3)   |
| 2501–3000  | Chief Warrant Officer   |

### 12.2 Game Modes

- **Ranked Mode:** Fixed 5v5. Full ELO weight. Parties up to 5 may queue.
- **Opens Mode:** 2v2 to 32v32. Reduced ELO impact. Open lobbies or loose matchmaking.

### 12.3 Regiments

A Regiment is a persistent group of players (Max Size: 50) that competes collectively under a shared name and tag, accumulates its own ELO rating, and queues for Regiment battles.

- **Structure:** Commanding Officer (1), Executive Officer (1–3), Company Commander (1–8).
- **Matchmaking:** Regiments group in a party and queue via a dedicated Regiment Matchmaking pool (functionally identical to Ranked queue flow, rather than manual challenges).
- **Regiment ELO & Ranks:** Starting ELO is 100.

| ELO       | Regiment Rank                                   |
| --------- | ----------------------------------------------- |
| 0–100     | None                                            |
| 101–200   | Second Lieutenant                               |
| 201–300   | First Lieutenant                                |
| 301–400   | Captain                                         |
| 401–500   | Major                                           |
| 501–600   | Lieutenant Colonel                              |
| 601–700   | Colonel                                         |
| 701–800   | Brigadier General                               |
| 801–900   | Major General                                   |
| 901–1000  | General                                         |
| 1000+     | General of the Army (Reserved for the #1 Regiment) |

## 13. Controls

All controls are designed for keyboard and mouse and are implemented with Enhanced Input (Input Actions + Input Mapping Contexts).

- **Camera:** Pan with W/A/S/D or Screen Edge. Rotate with Q/E. Zoom with Scroll Wheel.
- **Selection:** Each soldier is numbered 1–4, shown above the soldier. Pressing that number (1/2/3/4) or Left Clicking the soldier selects it. Double-tapping the number (two presses within 0.3 s) also moves the camera over that soldier. Backtick (\`) selects all. With several soldiers selected, every order applies to all of them: a move sends the lowest-numbered living soldier in the selection (normally soldier 1) to the clicked cell and the others keep their current formation around it (same spacing and orientation), with any soldier whose spot is blocked taking the nearest free cell, and the group stopping short if it would leave soldiers behind further than the squad cohesion limit (§4.3); an attack order makes every selected soldier fire at the target; and a grenade order makes every selected soldier throw one grenade at the target cell. (The developer console uses F10, not backtick.)
- **Commands:** Left Click to Move. The destination cell flashes red, labelled with the moving soldier's number, until the soldier arrives (or the order is cancelled). A dashed red line runs on the ground from the soldier along the route it is taking (its path, cell by cell) to that cell, flowing toward it and ending in an arrowhead. It is brighter and thicker for selected soldiers, shrinks as the soldier walks and shows only for your own soldiers. Shift + Left Click on ground to Face: works at any time, including while walking (the soldier keeps looking that way as it walks) and while treating a casualty; it stops an attack in progress. Shift + Left Click on enemy to explicit Attack. Alt + Left Click to Throw Grenade. Control + Left Click on a downed ally to Drag / Release (§5.4). Left Click is also how every menu and panel is used.
- **Movement & cover overlays:** with soldiers selected, a faint smooth circle on the ground (where several squad-mates limit the move, the overlap of their 30 m circles) marks the furthest they can be sent before the squad cohesion limit shortens the move (§4.3; for a group, where its lead soldier can go with the formation kept; hidden when the whole living squad is selected). These overlays count only cover pieces: hay bales, low walls, windows, trees, and full walls at a corner or the end of a wall (open ground beside the wall cell along its face). Standing against the middle of a full wall's face shows neither the cover preview nor the vision cone (the wall still gives its cover value as above). Hovering a free cell next to cover shows, round that cell, the 60° arc each adjacent piece of cover protects against (§9.2): the cover the soldier will lean on when it arrives (it faces the way of its last step and leans on the cover nearest that) is bright green and labelled with its full value, the rest dim amber with the back-turned value. Not shown while Shift or Alt is held. When one of your soldiers settles into cover (once it has finished turning to lean on the cover after arriving, so the cone shows where it actually looks, not the way it ran in; not again when it turns away and back), a faint cone of its vision (±60° of its facing, out to the 40 m sight range and cut off where cover blocks sight of a standing soldier, §9.3) flashes up for about 2 s. All of these are drawn for your own soldiers only.
- **Context Menu:** Right Click on an injured ally, on the battlefield or on its squad-bar card, opens a small menu at the cursor titled with the soldier's number. For now its one entry is **Use medkit** (§5.2), showing which soldier will treat and whether it walks over first, or, greyed out, why nobody can (e.g. no medkits left in the squad). Left Click an entry to choose it. A click anywhere else, a Right Click elsewhere, Esc, the command wheel or opening the tactical map closes it; the click that closes it does nothing else. It also closes by itself when the soldier is treated or dies. Right Click never moves soldiers.
- **Squad Bar:** A translucent bar along the bottom of the screen shows one card per soldier (1–4): rifle, rounds loaded / spare rounds for that rifle, one pip per round in the magazine (replaced by a progress bar while reloading), grenades, medkits, stance, a stress meter with the tier thresholds marked (§8.1), and health state (Active / Injured / Stunned with seconds left / KIA). A soldier holding fire (§5.3) has a red stripe along the top of its card and a solid red **Hold Fire** tag ahead of every other tag. Status tags show, most urgent first: Shocked, Under Fire (a round was fired at the soldier or it was stunned in the last 2 s), Dragged / Dragging, Medkit (treating, with seconds left) / To Medic (walking over) / Treated / Medic Coming, Grenade Out (stepping back or winding up), Firing with the current hit chance, Cover (and its type) while leaning on cover, Spotted / Exposed (an enemy can see it, in or out of cover) and Moving; with none of these it shows Ready (or Down). A card flashes red when its soldier is hit or stunned. Selected soldiers' cards have a gold border and sit slightly raised. Left Click a card to select that soldier; Shift + Left Click a card adds it to or removes it from the selection. Clicks on the bar don't reach the map. The loot & trade menu opens just above the bar.
- **Aim Lines:** While one of your soldiers is attacking, a line runs from its muzzle to its target, with corner brackets round the target and the soldier's number and current hit chance beside it. The line is amber and dashed (flowing toward the target) while the soldier is still finishing a step, turning or aiming; solid red once it is firing; dim grey and dashed while reloading. Only your own soldiers' aim lines are shown. Placeholder soldiers have no rifle model yet, so the line starts at chest height just in front of the body; it will start at the rifle's muzzle once soldiers carry weapon meshes.
- **Kill Feed:** The top right of the screen lists recent kills, newest on top (up to 6, each fading out after 8 s). Each line reads killer → weapon → victim. The killer and victim each show their player's rank as a short tag (PVT, PFC, CPL, SGT, SSG, PSG, MSG, 1SG, SGM, WO1, WO2, WO3, CWO, from §12.1) followed by their regiment tag in brackets if they have one (e.g. "1SG [LER]"), with the player name beneath; names are blue for your team and red for the enemy. Between them is a rifle or grenade icon, the chance of the roll that killed (for a rifle, the killing shot's final hit chance after cover, §5.3/§9; for a grenade, the 10% kill chance, §7.1) and the rifle's name (or "Grenade"). Kills by grenade are credited to the thrower, including kills of their own team. Soldiers no player commands (test enemies) show as "PVT Axis Squad" / "PVT Allied Squad". Kills are decided and reported only by the server and sent to every player.
- **Minimap:** The bottom left of the screen shows the whole map, turned so your own Deployment Zone is at the bottom and the enemy's at the top. It is a dark plan view: full walls are white lines, windows pale blue, low walls grey, hay bales tan and trees green dots; Victory Points are outlined in their owner's colour (neutral white, flashing amber while contested) with their letter, and both Deployment Zones are faintly outlined. A white outline shows the ground the camera currently sees. Your soldiers are dots in their squad-bar colours with a translucent cone showing which way each is facing (selected soldiers have a gold ring and a stronger cone; dead soldiers are a grey ×). Allied soldiers from other squads on your team are light-blue dots. Enemies are red dots, shown only while your team can see them (§9.3), using the same server-decided visibility as the battlefield. The minimap shrinks to stay clear of the squad bar on narrow screens. Left Click on the minimap centres the camera on that point; holding the button and dragging keeps the camera following the cursor (clamped to the map edge if the cursor leaves the minimap) until released. Clicks on the minimap never reach the map, so they can't issue orders. The team's plan marks and order requests (§13.1) are drawn on the minimap too. A small expand button in its top-right corner opens the tactical map; for the CO and XO, a pulsing "N ORDER REQUESTS" tag sits just above the minimap while requests are waiting.
- **Tactical Map:** M (or the minimap's expand button) opens and closes the tactical map (§13.1); Esc also closes it.
- **Command Wheel:** Hold the middle mouse button for the command wheel; tap it to Ping (§13.2). Its Build slice opens the Build sub-wheel for C4 and wall reinforcements (§13.3).
- **Loot / Trade:** Tab opens and closes the loot & trade menu for the selected soldier (§5.5). Clicks on the menu don't reach the map. Inside it, click moves a whole stack and Shift + click moves one (one clip for ammo).
- **Hold Fire:** Pressing H toggles hold fire (§5.3) for the selected soldiers: if any of them is firing freely, all of them hold fire; if all already hold, all are released. A soldier holding fire has its number shown in red with a red **HOLD** tag between the number and its status line (own team only).
- **Stance:** Pressing C cycles Standing → Crouched → Prone. Soldiers show stance by their animated posture (standing at low ready / kneeling on one knee / lying face-down with the rifle forward), playing a short transition when the stance changes while standing still, plus a text label. Without a model asset they fall back to placeholder beans (upright / squashed / lying face-down). Injured soldiers lie prone (crawling) and cannot change stance; stunned soldiers lie prone until the stun ends, then return to their previous stance; dead soldiers lie face-up and turn grey.
- **Audio:** The top left of the screen has a speaker button that mutes / unmutes all sound and a master volume slider (0–100%, click or drag; moving it unmutes) with the volume shown beside it. Saved per player between sessions. Clicks on it never reach the map.
- **Debug Menu (development builds only):** F6 opens and closes the debug menu (see the v0.36 changelog). A picked click tool stays active until right click or Esc, and a hint stays at the top left while it is active, even with the menu closed. The squad bar still selects soldiers while a tool is active.

### 13.1 Tactical Map & Command

**Opening it.** Pressing **M**, or clicking the expand button in the minimap's corner, opens the tactical map: the same plan view as the minimap (same orientation, your Deployment Zone at the bottom, the enemy's side at the top), enlarged to fill most of the screen and centred, over a darkened battlefield. Soldier dots are larger and your soldiers show their numbers. A side panel to its right holds the plan tools, the team list and order requests. The squad bar, minimap and loot menu are hidden while it is open. Clicks never reach the battlefield while it is open; keyboard commands (selection, stance, camera) still work. M or Esc closes it (Esc first cancels a stroke being drawn).

**Command roles.** At all times the server ranks each team's players by individual ELO (§12.1): the highest is the **Commanding Officer (CO)**, the second-highest the **Executive Officer (XO)**. Ties go to whoever joined the match first. A team with one player has a CO and no XO. Roles are re-checked continuously, so if the CO leaves, the XO becomes CO. These match roles are separate from the Regiment roles in §12.3. When you become CO or XO a notice tells you to press M.

**Planning rights.** The CO and XO may draw on the map. Either of them can **Delegate** planning rights to any other team-mate from the team list (shown with a **PLN** tag), and **Revoke** them again. Planners may draw and erase their own marks. The CO and XO can erase anyone's marks and **Clear** the whole plan. Players without planning rights see the map and plan read-only; for them a click on the map just moves the camera.

**Plan marks.** Each tool makes one kind of mark:

| Tool    | How to draw          | Mark                                                       |
| ------- | -------------------- | ---------------------------------------------------------- |
| View    | Click / drag         | (no mark) moves the camera to that point                   |
| Push    | Drag start → end     | Blue arrow: advance / push this way                        |
| Hold    | Drag start → end     | Green line with ticks on the enemy's side: hold this line  |
| Route   | Drag freehand        | Pale-yellow path with an arrowhead: route to take          |
| Defend  | Click                | Green shield: defend this position                         |
| Attack  | Click                | Orange target: attack / assault this position              |
| Rally   | Click                | Gold flag: rally / regroup point                           |
| Enemy   | Click                | Red diamond with ×: enemy reported here                    |
| Erase   | Click a mark         | Removes it (right click does the same with any tool)       |

Point marks and the ends of arrows and lines snap to cell centres (§10). Strokes shorter than one cell are discarded. Hovering a mark shows what it is and who drew it (role and name). When someone else adds marks, every team-mate gets a notice ("New orders from CO …"). A team's plan holds up to **40 marks** (provisional); drawing another removes the oldest. Freehand routes are thinned to **64 points** (provisional).

**Requesting orders.** Any player who is not CO or XO can press **Request orders** on the side panel. The request is marked at the centre of their squad's living soldiers as a pulsing amber ring labelled with their rank and name, on the tactical map and minimap. The CO and XO get a notice and a waiting-requests tag above their minimap. In the side panel's request list they can **Go to** a request (moves the camera there) or **Acknowledge** it. The requester is told who acknowledged; the ring turns green and disappears after 8 s. A player has at most one open request (asking again renews it) and must wait **15 s** between requests; unanswered requests expire after **60 s** (all provisional).

**Networking.** Each team has one server-owned plan actor, replicated **only to that team's players**, so the enemy never receives it. Every change is a Server RPC from the player controller; the server checks the player's team and role and fills in the author. Roles and planning rights live on the replicated player state. Tuning values (mark and route limits, request cooldown and lifetimes) are properties on the plan actor (`ATacticalPlan`).

### 13.2 Command Wheel & Pings

**The wheel.** Holding the **middle mouse button** opens a radial command wheel centred on the cursor. It works over the battlefield (the point on the ground under the cursor), the minimap and the tactical map (the map point under the cursor), but not over the squad bar or loot menu. Move the cursor towards a slice and let go of the button to choose it; letting go in the centre does nothing. A **quick tap** (released within 0.25 s without choosing) is a **Ping** at the cursor. Right click or Esc closes the wheel without choosing. The point the wheel opened on is where the chosen order goes.

**Slices.** Spot is always at the top; the rest run clockwise.

| Slice          | Who                            | What it does                                                                                                 |
| -------------- | ------------------------------ | ------------------------------------------------------------------------------------------------------------ |
| Spot           | Everyone                       | Red warning ping: enemy spotted here. Snaps onto the nearest enemy your team can see within 1.5 m of the point. |
| Ping           | Everyone                       | Cyan ping: look here / go here.                                                                                |
| Push           | CO, XO, planners (§13.1)       | Arrow from the wheel's point; it follows the cursor until a left click places it.                             |
| Hold line      | CO, XO, planners               | Hold line from the wheel's point; it follows the cursor until a left click places it.                         |
| Route          | CO, XO, planners               | Route from the wheel's point, traced by moving the cursor; left click finishes it.                            |
| Defend         | CO, XO, planners               | Defend shield on the wheel's point (placed at once).                                                           |
| Attack         | CO, XO, planners               | Attack target on the wheel's point (placed at once).                                                           |
| Rally          | CO, XO, planners               | Rally flag on the wheel's point (placed at once).                                                              |
| Erase          | CO, XO, planners               | Erase mode: left click marks to remove them (planners only their own); right click / Esc ends it.             |
| Build          | Everyone                       | Opens the **Build sub-wheel** in place (§13.3): C4 and Wall Reinforcement on the wall at the wheel's point.    |
| Request orders | Everyone except the CO and XO  | Same as the tactical map's Request orders button (§13.1).                                                      |

So a player without planning rights has a four-slice wheel (Spot, Ping, Build, Request orders) and the CO / XO / planners a ten- or nine-slice one. Build is **always** a sub-wheel, for every player, so it looks and works the same whichever wheel it comes from.

While a line is being placed or erase mode is on, a hint at the bottom of the screen says what to do; clicks don't issue soldier orders, the camera still moves (keys and screen edge), and right click, Esc or the middle mouse button cancels. Marks placed from the wheel are the same plan marks as the tactical map's and follow the same rules (snapping, limits, permissions).

**On the battlefield.** To keep the play area readable, everything here is drawn faint and see-through (plan marks at about 40% opacity, pings about 70%, request rings about 55%); a line being placed and a mark about to be erased are drawn stronger. The minimap and tactical map draw them at full strength. The team's plan marks are drawn over the play area where they lie on the ground: arrows, lines and routes along the ground, point marks as icons standing just above their cell. Open order requests show to the CO and XO as amber rings with "… needs orders". Pings show as a pulsing ring on the ground with their icon above it and the pinging player's rank and name ("ENEMY - …" for a Spot). A ping off screen shows as an arrow at the screen edge pointing towards it. Everything is also drawn on the minimap and the tactical map (with the player's name on the tactical map).

**Ping rules (provisional).** Spots last **10 s**, Pings **6 s**, fading out over the last second. A player can ping at most once every **0.5 s** and have at most **3** pings up at once (a new one replaces their oldest). Pings belong to the team's plan actor, so they are server-checked and **only the team** receives them.

### 13.3 Buildables

Buildables are things a soldier places on a weakened wall (§9.4): **C4** (§7.2) and **Wall Reinforcement**. Each soldier carries one of each (§4.2).

**The Build sub-wheel.** Choosing **Build** on the command wheel (§13.2) reopens the wheel in the same place as the Build sub-wheel, with the same look. It has one slice per buildable, in loadout order (C4 at the top, then Reinforce clockwise). Each slice shows the buildable's name and how many are left across the squad's living soldiers (e.g. "C4 x3"). Under that it shows which soldier will place it and how many that soldier has left (e.g. "Soldier 2 (1 left)"), or, greyed out, why it can't be placed here: NO BREACHABLE WALL HERE, WALL BREACHED, WALL REINFORCED / ALREADY REINFORCED, SELECT A SOLDIER, NO C4 LEFT / NO REINFORCEMENT LEFT, CAN'T REACH WALL, or the chosen soldier's state (STUNNED, INJURED, SHOCKED, DRAGGING, …). The sub-wheel stays open once the middle mouse is let go. **Left click** a slice to choose it (pressing and releasing the middle mouse over it also works). A click in the centre (marked X), **right click** or **Esc** closes it without choosing. Choosing a greyed-out slice, or an order the server refuses, shows the reason in orange at the wheel's point for a moment, like a grenade's BLOCKED.

**Target and who places it.** The point the wheel was opened on picks the wall: the weakened-wall cell under it, or the one right next to it if the point is on the open cell in front of the wall's face (within 75 cm of that wall cell's centre). Of the selected soldiers, the **nearest one** (straight line to the wall cell) that carries the buildable and can reach the wall places it; with several selected, only that one goes.

**Placing.**
- The soldier walks to the open cell orthogonally in front of that wall cell's face that is quickest to walk to, which is the side it is approaching from. If neither face cell of that wall cell is free and reachable within the squad cohesion limit (§4.3), it uses the nearest face cell of another cell of the same wall. If it has none, the order is refused with CAN'T REACH WALL. A buildable has to be placed from the side of the wall to stick.
- Once it is standing still on that cell it kneels facing the wall and channels for **7 s** (both buildables). It can't shoot meanwhile. The soldier's status line shows the buildable and seconds left (e.g. "C4 5s", or "TO WALL (C4)" while walking over). A progress bar with the time left floats over the soldier, for its own team only. To the enemy it is just a soldier (seen by the normal rules).
- **Interrupted** (the item is kept, and only used up when the channel finishes) by any new order to the soldier (move, stop, attack, grenade, stance change, drag, medkit, another build), by the soldier being stunned, injured, killed or dragged, or by the wall being breached meanwhile. A face order doesn't interrupt it: the soldier keeps facing the wall. Becoming Shocked part-way doesn't interrupt the channel (_provisional_), but a Shocked soldier can't start one, and one still walking over stops (it can't move, §8.1).
- **Who can place:** an Active soldier that isn't Shocked, dragging or being dragged, and carries the buildable. C4 can't be started on a reinforced wall, and a reinforcement can't be started on one that is already reinforced (a reinforcement still being placed when another one finishes stops, keeping the item). A soldier already placing C4 when the wall becomes reinforced carries on, and that C4 explodes without breaching (§7.2).

**Wall Reinforcement.** Can be placed from **either side** of the wall. When the 7 s finish, the wall is reinforced (§9.4) and can't be breached any more. Any C4 already on that wall whose fuse is still burning becomes a **dud** (§7.2).

**Not yet:** buildables can't be looted or traded (they aren't in the loot menu and stay with a dead soldier's body) (_provisional_). The play-test bot (§11.1) doesn't use buildables.

**Networking.** The order is a Server RPC from the player controller (`ServerOrderBuild`: buildable, the wheel's point, the selected soldiers). The server re-checks everything (only the player's own living soldiers, the wall, who carries it and can reach it) and picks the soldier itself. The channel runs on the server; the charge, the wall's reinforced / breached state and the counts belong to the server. Tuning lives in the soldier data asset (`USoldierData`, `DA_SoldierTuning`): `Buildables` is the list of buildables in the loadout, each with its kind, name on the wheel, count per soldier (`StartingCount`, 1) and placement time (`PlaceTime`, 7 s). New buildables are added to that list (plus their effect in code). C4's `C4FuseTime` and `C4BlastRadiusScale` sit next to it.

## 14. Server-Side Architecture (Google Cloud Platform)

Because the game relies on deterministic grid movement, true RNG shot calculations, and a strict ELO ladder, peer-to-peer (P2P) or listen-server networking is insufficient. The game utilises a secure, authoritative server backend hosted on Google Cloud Platform.

### 14.1 Dedicated Game Servers (GKE & Agones)

- The game runs strictly on headless **Unreal Engine dedicated servers** (a `Server` build target, Linux, built from the engine source). The server is the ultimate authority for line of sight, stress, and hit-chance RNG; clients only send orders (Server RPCs) and render replicated state.
- **Google Kubernetes Engine (GKE):** Servers are containerised and deployed globally.
- **Replication (implemented):** the server simulates every soldier and replicates what clients need to show it (position, stance, health, stress, ammo, timers, orders in progress); clients only animate and render. Orders and loot actions go to the server as RPCs and run only after it checks the soldier belongs to the sender. Grenades are launched on the server; clients fly a copy of the same arc, and only the server's blast does damage. Line of sight decides what each client draws (§9.3).
- **Local testing:** development runs the same way on one PC: Play-in-Editor as a dedicated server with networked clients, or `Scripts/LocalMultiplayer.ps1` for separate server and client processes (see `CLAUDE.md`).
- **Agones:** This open-source game server orchestrator (via its Unreal Engine SDK plugin) manages game server lifecycles, spinning up dedicated 5v5 or 10v10 instances exactly when matchmaking completes, and shutting them down when the match concludes.

### 14.2 Matchmaking (Open Match)

- Instead of native Steam matchmaking, the game uses **Open Match** to handle the custom ticket logic required for routing players based on their ELO, K-Factors and Regiment standings. Matched players receive the allocated Agones server address and connect via Unreal's client travel.

### 14.3 Persistent Backend (Cloud Spanner)

- Player profiles, individual ELOs, Regiment rosters, and hierarchical roles are stored in **Cloud Spanner**, offering horizontal scalability and relational consistency.

### 14.4 Post-Match ELO Calculation (Cloud Functions)

- To prevent client-side manipulation, clients never calculate their own ELO. Once a match ends, the authoritative dedicated server (from the server-only `AGameMode`) pushes the raw match result to a secure **Cloud Function / Cloud Run** endpoint. This serverless function executes the K-Factor math and updates the database in Spanner.

## 15. Future Scope

- Additional Squad Types (Machine Gun team, Sniper pair, Mortar crew).
- Unit Customisation.
- Additional Maps & further Cover Destruction (weakened walls that can be breached are in, §9.4; destroying other cover such as hay bales, low walls or trees is not).
- Spectator Mode & Replay System (Unreal's built-in Replay/DemoNetDriver is a candidate).
