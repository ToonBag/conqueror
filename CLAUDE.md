# CLAUDE.md

## Project

**Squad Command** — a low-poly WW2 multiplayer real-time strategy game (squad-based, grid maps, ELO-ranked) for PC/Steam.

- **Engine:** Unreal Engine 5.8.4, an *installed* build at `C:\Program Files\Epic Games\UE_5.8` (it has `Engine/Build/InstalledBuild.txt`; `EngineAssociation` in `Unreal.uproject` is a GUID). It cannot build a packaged `Server` target; that needs a source-built engine.
- **Project file:** `Unreal.uproject`, single runtime C++ module `Unreal` in `Source/Unreal/` (unity builds off: files keep file-local helpers).
- **Targets:** `Source/Unreal.Target.cs` (game), `Source/UnrealEditor.Target.cs` (editor). A `Server` target will be needed for dedicated servers.

## Multiplayer-only: develop and test against a dedicated server

The game is multiplayer only. Never treat Standalone play as proof that something works.

- **Play-in-Editor** defaults (`Config/DefaultEditorPerProjectUserSettings.ini`) to *Play As Client*: an in-editor dedicated server plus 2 clients, one process, mild lag/loss emulation. Editor Python tests should run this way and check results on the **client** worlds (replicated state), not only on the server.
- **Separate processes:** `Scripts/LocalMultiplayer.ps1` starts a dedicated server (`-server`) and N clients (`-game 127.0.0.1`), with logs in `Saved/Logs/LocalMP_*.log`; `-Lag` adds packet lag/loss, `-Stop` closes them. These use the DLLs on disk, so build with UBT first (Live Coding only patches the open editor). A client that fails to connect times out and **falls back to a local single-player map**, so check the server log for `Join succeeded`.
- **How soldiers replicate:** `ASoldier` simulates only on the server (`Tick` → `HasAuthority`) and copies its live fields into one replicated `FSoldierNetState` each tick (`WriteNetState`); clients copy it back (`ReadNetState`) and only animate (`TickClient`). A new field that the HUD, animation or labels read must be added to `FSoldierNetState` and to both functions. One-off effects (tracer, gunshot, reload sound) are `NetMulticast` RPCs.
- **Orders:** every public order method on `ASoldier` (`MoveToCell`, `OrderAttack`, `ThrowGrenadeAt`, …) starts with `if (!HasAuthority())` and forwards through `ForwardOrder` → `ASquadPlayerController::QueueSoldierOrder` → `ServerSoldierOrder` → `ASoldier::ExecuteOrder`. A new order needs an `ESoldierOrderType`, the forward, and a case in `ExecuteOrder`. Server RPCs validate ownership and never `check()` on client input.
- **Teams, squads and bots:** a team can hold several squads (`ASoldier::SquadId`; `squad.SquadsPerTeam` copies the level's squad, one per player). `ASquadGameMode::PostLogin` puts each player on the team with fewer players, gives them the first squad no other player commands (`ASquadPlayerController::PlayerSquadId`) and takes over that squad's bot (one `ASquadBot` per uncommanded squad); a dedicated server starts the match once `squad.MinPlayers` (default 1) have joined.
- **AI test matches (balancing):** `Scripts/AIMatchBatch.ps1` runs bot-only matches on server-only processes (fixed 30 Hz step, ~1 min for 10 4v4 matches) with `squad.MatchStats 1` (`USquadMatchStats`, server only) writing `Saved/MatchStats/*.json` + `_shots.csv`; `Scripts/Tests/analyze_match_stats.py` (run with the engine's bundled Python, none on PATH) makes the tables. Reviews go in `Docs/Balancing/`. See GDD §11.2.
- **Testing from editor Python:** UE runs RPCs called from editor scripts locally, so every client→server call goes through `ASquadPlayerController::CallServer` (sent on the controller's next tick): use it for any new Server RPC too. Call order methods on a **client** world's soldier or controller and read results on every world (`Scripts/Tests/net_probe.py` prints all of them). Set up a scenario on the **server** world with the soldier debug hooks (`DebugTeleportTo`, `DebugInjure`, `DebugKill`, `DebugStun`). The Aura Python tool can run a script twice, so make setup steps idempotent (a second `DebugInjure` kills). `ASoldier::GetStatusMessage()` shows why the server refused an order, build refusals log `Build refused: …`, and `LogSquadGrenade` logs each grenade's flight per machine.
- **Feedback for the player** from a Server RPC must go back through a Client RPC (e.g. `ClientBuildFeedback`); state set on the server's copy of a controller never reaches the player.
- **Dedicated-server pitfalls:** a level-placed component with collision off is not loaded on a dedicated server unless `AlwaysLoadOnServer = true`. Text render components have no font there, so keep them unloaded and null-check them (`ASoldier::HasLabels`, `AGridZoneActor::HasVisuals`, `AC4Charge::HasLabel`). Gate cosmetic work with `IsRunningDedicatedServer()` or `GetNetMode() == NM_DedicatedServer`.

## Game Design Document

The authoritative design spec is **[Docs/GDD.md](Docs/GDD.md)**. Read it before implementing or changing any gameplay system, and follow its numbers (weapon stats, stress values, grenade radii, movement speeds, ELO/K-factors) exactly unless told otherwise.

- If an implementation decision changes or clarifies the design, update `Docs/GDD.md` (and bump its version/changelog) in the same change.
- If the GDD is ambiguous or contradicts itself, ask rather than guess.

## Key design constraints (summary — GDD is the source of truth)

- **Server-authoritative multiplayer.** Dedicated servers own all gameplay state, line of sight, stress and hit-chance RNG. Clients send orders via Server RPCs and render replicated state. Never compute hit rolls, damage or ELO on the client.
- **Grid-based.** 1 grid cell = 1 m = **100 Unreal units**. Movement, pathfinding, cover, LOS and grenade targeting work on grid coordinates.
- **One squad per player** (Standard Squad: 4 soldiers, M1 Garand / Gewehr 43, 70 rounds, 5 grenades, 1 medkit each).
- **Data-driven balancing.** Keep weapon/squad/stress tuning values in Data Assets or DataTables, not hard-coded.
- **Input** uses Enhanced Input (controls listed in GDD §13).
- **Backend:** GCP — GKE + Agones (game servers), Open Match (matchmaking), Cloud Spanner (persistence), Cloud Functions/Run (post-match ELO).

## Folders

- `Source/` — C++ code.
- `Content/` — Unreal assets.
- `Config/` — `DefaultEngine.ini`, `DefaultGame.ini`, `DefaultInput.ini`, `DefaultEditor.ini`, `DefaultEditorPerProjectUserSettings.ini` (PIE net mode).
- `Scripts/` — editor Python map builders and `LocalMultiplayer.ps1`.
- `SourceArt/` — raw art sources (e.g. `SourceArt/Characters`) before import.
- `Docs/` — design documentation (GDD).
- Do not edit `Binaries/`, `Intermediate/`, `DerivedDataCache/` or `Saved/` — they are generated.

## Standing permissions

Claude may do the following without asking first:

- **Run Python** in the Unreal Editor (editor scripting, Play-mode tests).
- **Compile and load C++**: Live Coding, and UnrealBuildTool builds of the project's targets.
- **Restart the full Unreal Editor**: save, close, rebuild and reopen it on the same level. Always the full editor, never headless.
- **Run shell commands** (PowerShell).
- **Run Bash commands.**
- **Edit existing Blueprint graphs.**
