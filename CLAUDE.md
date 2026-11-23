# CLAUDE.md

## Project

**Squad Command** — a low-poly WW2 multiplayer real-time strategy game (squad-based, grid maps, ELO-ranked) for PC/Steam.

- **Engine:** Unreal Engine 5.8.4 (source build — `EngineAssociation` in `Unreal.uproject` is a GUID).
- **Project file:** `Unreal.uproject`, single runtime C++ module `Unreal` in `Source/Unreal/`.
- **Targets:** `Source/Unreal.Target.cs` (game), `Source/UnrealEditor.Target.cs` (editor). A `Server` target will be needed for dedicated servers.

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
- `Config/` — `DefaultEngine.ini`, `DefaultGame.ini`, `DefaultInput.ini`, `DefaultEditor.ini`.
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
