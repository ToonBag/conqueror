// Demo (non-shipping): combat-model comparison, hit-chance rolls (GDD §5.3, §9.1) against projectile combat (§16).
// Server / standalone only; runs headless on a dedicated server (Scripts/CombatModeDemo.ps1) or in a PIE server world.
//
//   squad.DemoCombat [hitrate|engage|all = all] [rounds per trial = 40] [engagement reps = 12] [quit]
//
// For every target set-up (in the open standing / crouched / prone; braced or prone behind a hay bale, a low wall,
// a window; crouched at a tree) and range (4-40 m) it finds one firing lane on the current map, then runs the same
// trial twice on that exact geometry: once with squad.Ballistics 0 and once with 1, for a standing and a prone
// shooter (Allied soldier 1, M1 Garand) against Axis soldier 1, who holds fire.
//
// hitrate: the target is invulnerable and keeps its stance; the shooter fires N rounds. Hit rate, mean shown chance,
//          exposure, body parts hit, rounds stopped by cover, stray hits.
// engage:  the target is shot for real (§5.1): the first hit injures it (it drops prone, wherever it is), the next
//          kills it; a hit from 5 m or closer kills outright. Rounds and seconds to injure and to kill, and how
//          often an injured target can't be finished within the time limit (out of sight or too little showing).
//          Deaths are simulated so the same soldier can be reused: the target stays invulnerable to the game and the
//          demo applies the injury itself, then heals it for the next run.
//
// Stress is held at 0 on both soldiers so the trials measure the combat model alone. Results go to
// Saved/Demo/Combat/DemoCombat_<what>_<time>_{trials,engagements,shots}.csv; Scripts/Tests/compare_combat_modes.py
// turns them (and AI match records) into the comparison report. Everything is put back afterwards (or the process
// closes with "quit").

#include "CoverData.h"
#include "GridManager.h"
#include "Soldier.h"
#include "SoldierData.h"
#include "WeaponData.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogSquadDemoCombat, Log, All);

#if !UE_BUILD_SHIPPING
namespace
{
	constexpr float SettleTime = 1.0f;       // after placing the soldiers, before the order to fire
	constexpr float EngageTimeLimit = 45.f;  // game seconds per engagement before it counts as not finished
	constexpr float NoLosGiveUp = 4.f;       // seconds without line of sight before a trial is given up

	struct FTargetCfg
	{
		FString Name;
		ECoverType Cover = ECoverType::None;
		ESoldierStance Stance = ESoldierStance::Standing;
	};

	struct FStep
	{
		FString Name;
		TFunction<void()> Begin;
		/** Game seconds since Begin; returns true when the step is finished. */
		TFunction<bool(float)> Tick;
	};

	struct FSoldierRestore
	{
		TWeakObjectPtr<ASoldier> Soldier;
		FIntPoint Cell = FIntPoint::ZeroValue;
		ESoldierStance Stance = ESoldierStance::Standing;
		bool bHoldFire = false;
		float Yaw = 0.f;
	};

	struct FLane
	{
		FIntPoint Shooter = FIntPoint::ZeroValue;
		FIntPoint Target = FIntPoint::ZeroValue;
		FIntPoint Cover = FIntPoint::ZeroValue; // the cell the target faces (cover cell, or the next open cell)
		bool bValid = false;
	};

	/** What the shot callback adds up for the trial in progress. */
	struct FTrialCounters
	{
		bool bActive = false;
		bool bEngage = false;
		FString Key;            // "mode,shooter_stance,target,cover,range_m,rep" for shot rows
		int32 Resolved = 0;
		int32 Hits = 0;
		int32 Stray = 0;
		int32 Friendly = 0;
		int32 CoverStops = 0;
		int32 Parts[6] = { 0, 0, 0, 0, 0, 0 };
		double ChanceSum = 0.0;
		double ExposureSum = 0.0;
		double SpreadSum = 0.0;
		// Engagement: 0 unhurt, 1 injured, 2 dead.
		int32 Phase = 0;
		bool bOutright = false;
		int32 RoundsToInjure = -1;
		int32 RoundsToKill = -1;
		float TimeToInjure = -1.f;
		float TimeToKill = -1.f;
		float StartTime = 0.f;
	};

	struct FDemo
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AGridManager> Grid;
		TWeakObjectPtr<ASoldier> Shooter;
		TWeakObjectPtr<ASoldier> Target;
		TArray<FSoldierRestore> Restore;
		TArray<FStep> Steps;
		int32 Index = -1;
		float StepTime = 0.f;
		float Clock = 0.f; // game seconds since the demo began
		int32 RoundsPerTrial = 40;
		int32 EngageReps = 12;
		bool bQuit = false;
		FString What;
		FString OutBase;
		TArray<FString> TrialRows;
		TArray<FString> EngageRows;
		TArray<FString> ShotRows;
		FTrialCounters Ctr;
		FDelegateHandle ShotHandle;
		// Saved console state.
		int32 PrevBot = 1;
		int32 PrevBallistics = 0;
		float PrevVPWinTime = 0.f;
		float PrevMatchTime = 0.f;
	};

	FTSTicker::FDelegateHandle ActiveDemo;
	TSharedPtr<FDemo> Demo;

	IConsoleVariable* FindVar(const TCHAR* Name) { return IConsoleManager::Get().FindConsoleVariable(Name); }
	FString CellStr(FIntPoint C) { return FString::Printf(TEXT("(%d,%d)"), C.X, C.Y); }
	const TCHAR* CoverStr(ECoverType T) { return T == ECoverType::None ? TEXT("None") : UCoverData::GetCoverName(T); }
	const TCHAR* StanceStr(ESoldierStance S)
	{
		return S == ESoldierStance::Prone ? TEXT("prone") : S == ESoldierStance::Crouched ? TEXT("crouched") : TEXT("standing");
	}
	const TCHAR* ModeStr(bool bBallistics) { return bBallistics ? TEXT("projectile") : TEXT("rolls"); }

	void SetBallistics(bool bOn)
	{
		if (IConsoleVariable* V = FindVar(TEXT("squad.Ballistics")))
		{
			V->Set(bOn ? 1 : 0, ECVF_SetByConsole);
		}
	}

	/** Turn at once (cover counts fully only when the target faces it, GDD §9.2). */
	void FaceNow(ASoldier* S, FIntPoint Cell, const AGridManager& Grid)
	{
		S->FaceCell(Cell);
		const FVector To = Grid.CellToWorld(Cell) - S->GetActorLocation();
		S->SetActorRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
	}

	bool IsWallLike(ECoverType T) { return T == ECoverType::LowWall || T == ECoverType::Window || T == ECoverType::WallCorner; }

	/** A wall slab crosses the line along Dir (the soldier faces the wall's face, not its end). */
	bool CrossesLine(const FGridCoverCell& Cell, FIntPoint Dir)
	{
		return !IsWallLike(Cell.Type) || (Dir.X != 0 ? !Cell.bRunsAlongX : Cell.bRunsAlongX);
	}

	bool IsOpen(const AGridManager& Grid, FIntPoint C, const ASoldier* Ignore)
	{
		return Grid.IsInBounds(C) && Grid.IsCellFreeAsDestination(C, Ignore) && !Grid.GetCoverCell(C);
	}

	bool HasNoCoverAround(const AGridManager& Grid, FIntPoint C)
	{
		static const FIntPoint Dirs[] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };
		for (const FIntPoint& D : Dirs)
		{
			if (Grid.GetCoverCell(C + D))
			{
				return false;
			}
		}
		return true;
	}

	/** Cells From+Dir*1 .. From+Dir*Count carry no cover at all. */
	bool IsLaneClear(const AGridManager& Grid, FIntPoint From, FIntPoint Dir, int32 Count)
	{
		for (int32 K = 1; K <= Count; ++K)
		{
			const FIntPoint C = From + Dir * K;
			if (!Grid.IsInBounds(C) || Grid.GetCoverCell(C))
			{
				return false;
			}
		}
		return true;
	}

	static const FIntPoint OrthoDirs[] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

	/**
	 * A lane on this map: the target right behind cover of the given type (or in the open with nothing around),
	 * the shooter Range cells straight in front with nothing in between. Checked in the world: the target, crouched
	 * and facing the cover, must be in sight and get exactly that cover, leaning on it.
	 */
	FLane FindLane(FDemo& D, ECoverType Cover, int32 Range)
	{
		ASoldier* Shooter = D.Shooter.Get();
		ASoldier* Target = D.Target.Get();
		const AGridManager* Grid = D.Grid.Get();
		TArray<FLane> Candidates;
		auto TryAdd = [&](FIntPoint T, FIntPoint Dir, FIntPoint C)
		{
			const FIntPoint S = T - Dir * Range;
			if (!IsOpen(*Grid, S, Shooter) || !HasNoCoverAround(*Grid, S))
			{
				return;
			}
			const int32 Clear = Cover == ECoverType::None ? Range - 1 : Range - 2;
			if (!IsLaneClear(*Grid, S, Dir, Clear))
			{
				return;
			}
			FLane L; L.Shooter = S; L.Target = T; L.Cover = C; L.bValid = true;
			Candidates.Add(L);
		};
		if (Cover == ECoverType::None)
		{
			for (int32 X = 1; X < Grid->GridSizeX - 1; ++X)
			{
				for (int32 Y = 1; Y < Grid->GridSizeY - 1; ++Y)
				{
					const FIntPoint T(X, Y);
					if (IsOpen(*Grid, T, Target) && HasNoCoverAround(*Grid, T))
					{
						for (const FIntPoint& Dir : OrthoDirs)
						{
							TryAdd(T, Dir, T - Dir);
						}
					}
				}
			}
		}
		else
		{
			for (const TPair<FIntPoint, FGridCoverCell>& Pair : Grid->GetCoverCells())
			{
				if (Pair.Value.Type != Cover || (Pair.Value.bPassable && Cover != ECoverType::Soft && Cover != ECoverType::Tree))
				{
					continue;
				}
				for (const FIntPoint& Dir : OrthoDirs)
				{
					const FIntPoint T = Pair.Key + Dir; // target just behind the cover, the shooter in front of it
					if (CrossesLine(Pair.Value, Dir) && IsOpen(*Grid, T, Target))
					{
						TryAdd(T, Dir, Pair.Key);
					}
				}
			}
		}
		// Spread the lanes over the map a little: sort so the same spot isn't always the first one in memory order.
		Candidates.Sort([](const FLane& A, const FLane& B) { return A.Target.X != B.Target.X ? A.Target.X < B.Target.X : A.Target.Y < B.Target.Y; });
		for (const FLane& L : Candidates)
		{
			if (!Shooter->DebugTeleportTo(L.Shooter) || !Target->DebugTeleportTo(L.Target))
			{
				continue;
			}
			Target->OrderStance(ESoldierStance::Crouched);
			Shooter->OrderStance(ESoldierStance::Standing);
			FaceNow(Target, L.Cover, *Grid);
			FaceNow(Shooter, L.Target, *Grid);
			const FHitChanceInfo Chance = Shooter->GetHitChanceAgainst(Target);
			if (Chance.CanFire() && Chance.Cover == Cover && (Cover == ECoverType::None || Chance.bCoverLeaning))
			{
				return L;
			}
		}
		return FLane();
	}

	void ResetCounters(FDemo& D, const FString& Key, bool bEngage)
	{
		D.Ctr = FTrialCounters();
		D.Ctr.Key = Key;
		D.Ctr.bEngage = bEngage;
		D.Ctr.StartTime = D.Clock;
	}

	/** ASoldier::OnShotResolved: counts the rounds of the trial in progress and plays out the engagement. */
	void OnShot(const FSoldierShotReport& R)
	{
		if (!Demo.IsValid())
		{
			return;
		}
		FDemo& D = *Demo;
		FTrialCounters& C = D.Ctr;
		ASoldier* Target = D.Target.Get();
		if (!C.bActive || R.Shooter != D.Shooter.Get() || R.Target != Target || !Target)
		{
			return;
		}
		const bool bWasInjured = C.bEngage && C.Phase == 1;
		++C.Resolved;
		C.ChanceSum += R.Chance.FinalChance;
		C.ExposureSum += R.Chance.bBallistic ? R.Chance.ExposurePercent : 0.f;
		C.SpreadSum += R.Chance.SpreadMrad;
		if (R.bHitTarget)
		{
			++C.Hits;
			C.Parts[static_cast<int32>(R.Part)]++;
		}
		else if (R.Outcome == TEXT("miss_cover"))
		{
			++C.CoverStops;
		}
		else if (R.Outcome == TEXT("friendly"))
		{
			++C.Friendly;
		}
		else if (R.Outcome == TEXT("stray_enemy"))
		{
			++C.Stray;
		}
		D.ShotRows.Add(FString::Printf(TEXT("%s,%d,%d,%.2f,%.1f,%.2f,%d,%d,%s,%s"), *C.Key, C.Resolved, bWasInjured ? 1 : 0,
			R.Chance.FinalChance, R.Chance.bBallistic ? R.Chance.ExposurePercent : -1.f, R.Chance.SpreadMrad, R.Chance.bBraced ? 1 : 0,
			R.bHitTarget ? 1 : 0, R.bHitTarget && R.Chance.bBallistic ? GetHitPartName(R.Part) : TEXT(""), *R.Outcome));

		if (C.bEngage && R.bHitTarget && C.Phase < 2)
		{
			const float Elapsed = D.Clock - C.StartTime;
			const float KillDist = Target->GetSoldierData() ? Target->GetSoldierData()->CloseRangeKillDistance : 5.f;
			if (C.Phase == 0 && R.Chance.DistanceMetres > KillDist)
			{
				C.Phase = 1;
				C.RoundsToInjure = C.Resolved;
				C.TimeToInjure = Elapsed;
				Target->DebugInjure(); // §5.1: injured, drops prone where it is.
			}
			else
			{
				C.bOutright = C.Phase == 0;
				C.Phase = 2;
				C.RoundsToKill = C.Resolved;
				C.TimeToKill = Elapsed;
				if (C.bOutright)
				{
					C.RoundsToInjure = C.Resolved;
					C.TimeToInjure = Elapsed;
				}
			}
		}
	}

	/** Both soldiers back to a clean state on the lane, in the given stances. */
	void PlaceForTrial(FDemo& D, const FLane& L, const FTargetCfg& Cfg, ESoldierStance ShooterStance)
	{
		ASoldier* Shooter = D.Shooter.Get();
		ASoldier* Target = D.Target.Get();
		for (ASoldier* S : { Shooter, Target })
		{
			S->OrderHoldFire(true);
			S->DebugHeal();
			S->DebugRestock();
			S->DebugResetCombatStats();
			S->SetStress(0.f);
		}
		Shooter->DebugTeleportTo(L.Shooter);
		Target->DebugTeleportTo(L.Target);
		Shooter->OrderStance(ShooterStance);
		Target->OrderStance(Cfg.Stance);
		FaceNow(Target, L.Cover, *D.Grid);
		FaceNow(Shooter, L.Target, *D.Grid);
	}

	/** Keep the explicit attack going (it drops when the target goes out of sight); false while it can't fire. */
	bool KeepAttacking(ASoldier* Shooter, ASoldier* Target)
	{
		if (Shooter->GetAttackTarget() == Target)
		{
			return true;
		}
		return Shooter->OrderAttack(Target);
	}

	FString PartsStr(const FTrialCounters& C)
	{
		return FString::Printf(TEXT("%d,%d,%d,%d,%d,%d"), C.Parts[0], C.Parts[1], C.Parts[2], C.Parts[3], C.Parts[4], C.Parts[5]);
	}

	void AddHitRateTrial(FDemo& D, TSharedRef<FLane> Lane, const FTargetCfg& Cfg, int32 Range, ESoldierStance ShooterStance, bool bBallistics)
	{
		struct FState { float OrderAt = -1.f; float NoLos = 0.f; bool bLos = false; float FirstChance = -1.f; float FirstExposure = -1.f; };
		TSharedRef<FState> St = MakeShared<FState>();
		const FString Key = FString::Printf(TEXT("hitrate,%s,%s,%s,%s,%d,0"), ModeStr(bBallistics), StanceStr(ShooterStance), *Cfg.Name, CoverStr(Cfg.Cover), Range);
		FStep Step;
		Step.Name = Key;
		Step.Begin = [&D, Lane, Cfg, ShooterStance, bBallistics, Key]()
		{
			if (!Lane->bValid)
			{
				return;
			}
			SetBallistics(bBallistics);
			D.Target->DebugSetInvulnerable(true);
			PlaceForTrial(D, *Lane, Cfg, ShooterStance);
			ResetCounters(D, Key, false);
		};
		Step.Tick = [&D, Lane, St, Cfg, Range, ShooterStance, bBallistics](float T) -> bool
		{
			ASoldier* Shooter = D.Shooter.Get();
			ASoldier* Target = D.Target.Get();
			if (!Lane->bValid)
			{
				return true;
			}
			Shooter->SetStress(0.f);
			Target->SetStress(0.f);
			if (T < SettleTime)
			{
				return false;
			}
			if (St->OrderAt < 0.f)
			{
				const FHitChanceInfo Chance = Shooter->GetHitChanceAgainst(Target);
				St->FirstChance = Chance.CanFire() ? Chance.FinalChance : 0.f;
				St->FirstExposure = Chance.bBallistic ? Chance.ExposurePercent : -1.f;
				St->bLos = Chance.CanFire();
				St->OrderAt = T;
				D.Ctr.bActive = true;
				D.Ctr.StartTime = D.Clock;
			}
			const bool bFiring = KeepAttacking(Shooter, Target);
			St->NoLos = bFiring ? 0.f : St->NoLos + D.World->GetDeltaSeconds();
			const bool bDone = D.Ctr.Resolved >= D.RoundsPerTrial || St->NoLos > NoLosGiveUp || T - St->OrderAt > D.RoundsPerTrial * 5.f;
			if (!bDone)
			{
				return false;
			}
			Shooter->OrderHoldFire(true);
			D.Ctr.bActive = false;
			const FTrialCounters& C = D.Ctr;
			const float Seconds = D.Clock - C.StartTime;
			const int32 N = C.Resolved;
			D.TrialRows.Add(FString::Printf(TEXT("%s,%s,%s,%s,%s,%d,%d,%.2f,%.1f,%d,%d,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%s,%.1f,%s,%s"),
				TEXT("hitrate"), ModeStr(bBallistics), StanceStr(ShooterStance), *Cfg.Name, CoverStr(Cfg.Cover), Range, St->bLos ? 1 : 0,
				St->FirstChance, St->FirstExposure, N, C.Hits, N ? 100.0 * C.Hits / N : 0.0, N ? C.ChanceSum / N : 0.0,
				N && bBallistics ? C.ExposureSum / N : -1.0, N && bBallistics ? C.SpreadSum / N : -1.0, C.CoverStops, C.Stray, C.Friendly,
				*PartsStr(C), Seconds, *CellStr(Lane->Shooter), *CellStr(Lane->Target)));
			UE_LOG(LogSquadDemoCombat, Log, TEXT("DemoCombat: %-10s %-8s shooter %-8s %-16s %2d m  shown %5.1f%%  hits %3d/%3d (%5.1f%%)%s"),
				ModeStr(bBallistics), *Cfg.Name, StanceStr(ShooterStance), CoverStr(Cfg.Cover), Range, St->FirstChance, C.Hits, N,
				N ? 100.0 * C.Hits / N : 0.0, St->bLos ? TEXT("") : TEXT("  NO LINE OF SIGHT"));
			return true;
		};
		D.Steps.Add(MoveTemp(Step));
	}

	void AddEngagement(FDemo& D, TSharedRef<FLane> Lane, const FTargetCfg& Cfg, int32 Range, ESoldierStance ShooterStance, bool bBallistics, int32 Rep)
	{
		struct FState { float OrderAt = -1.f; bool bLos = false; float NoLosAfterInjury = 0.f; float LosLostAt = -1.f; };
		TSharedRef<FState> St = MakeShared<FState>();
		const FString Key = FString::Printf(TEXT("engage,%s,%s,%s,%s,%d,%d"), ModeStr(bBallistics), StanceStr(ShooterStance), *Cfg.Name, CoverStr(Cfg.Cover), Range, Rep);
		FStep Step;
		Step.Name = Key;
		Step.Begin = [&D, Lane, Cfg, ShooterStance, bBallistics, Key]()
		{
			if (!Lane->bValid)
			{
				return;
			}
			SetBallistics(bBallistics);
			D.Target->DebugSetInvulnerable(true); // the demo applies injuries itself (see OnShot)
			PlaceForTrial(D, *Lane, Cfg, ShooterStance);
			ResetCounters(D, Key, true);
		};
		Step.Tick = [&D, Lane, St, Cfg, Range, ShooterStance, bBallistics, Rep](float T) -> bool
		{
			ASoldier* Shooter = D.Shooter.Get();
			ASoldier* Target = D.Target.Get();
			if (!Lane->bValid)
			{
				return true;
			}
			Shooter->SetStress(0.f);
			Target->SetStress(0.f);
			if (T < SettleTime)
			{
				return false;
			}
			FTrialCounters& C = D.Ctr;
			if (St->OrderAt < 0.f)
			{
				St->bLos = Shooter->GetHitChanceAgainst(Target).CanFire();
				St->OrderAt = T;
				C.bActive = true;
				C.StartTime = D.Clock;
			}
			const float E = T - St->OrderAt;
			bool bFiring = false;
			if (C.Phase < 2)
			{
				bFiring = KeepAttacking(Shooter, Target);
				if (!bFiring && St->LosLostAt < 0.f && C.Phase == 1)
				{
					St->LosLostAt = E;
				}
				St->NoLosAfterInjury = bFiring ? 0.f : St->NoLosAfterInjury + D.World->GetDeltaSeconds();
			}
			const bool bNeverSeen = C.Phase == 0 && !bFiring && E > NoLosGiveUp;
			// An injured target out of sight for good: wait a few seconds in case it shows again, then call it.
			const bool bHidden = C.Phase == 1 && St->NoLosAfterInjury > NoLosGiveUp * 2.f;
			if (C.Phase < 2 && E < EngageTimeLimit && !bNeverSeen && !bHidden)
			{
				return false;
			}
			if (C.Phase == 2 && E < C.TimeToKill + 0.3f)
			{
				return false; // let rounds already in the air land (they no longer count)
			}
			Shooter->OrderHoldFire(true);
			C.bActive = false;
			const TCHAR* Result = C.Phase == 2 ? (C.bOutright ? TEXT("killed_outright") : TEXT("killed"))
				: C.Phase == 1 ? (bHidden ? TEXT("injured_hidden") : TEXT("injured_timeout"))
				: (!St->bLos || bNeverSeen) && C.Resolved == 0 ? TEXT("no_los") : TEXT("untouched");
			D.EngageRows.Add(FString::Printf(TEXT("engage,%s,%s,%s,%s,%d,%d,%s,%d,%.2f,%d,%.2f,%d,%d,%.2f,%.2f,%.2f,%s,%s"),
				ModeStr(bBallistics), StanceStr(ShooterStance), *Cfg.Name, CoverStr(Cfg.Cover), Range, Rep, Result,
				C.RoundsToInjure, C.TimeToInjure, C.RoundsToKill, C.TimeToKill, C.Resolved, C.Hits, E, St->LosLostAt,
				C.Resolved ? C.ChanceSum / C.Resolved : 0.0, *CellStr(Lane->Shooter), *CellStr(Lane->Target)));
			UE_LOG(LogSquadDemoCombat, Log, TEXT("DemoCombat: ENGAGE %-10s %-8s shooter %-8s %2d m rep %2d: %-15s injure %2d rds %5.1fs  kill %2d rds %5.1fs"),
				ModeStr(bBallistics), *Cfg.Name, StanceStr(ShooterStance), Range, Rep, Result, C.RoundsToInjure, C.TimeToInjure, C.RoundsToKill, C.TimeToKill);
			Target->DebugHeal();
			return true;
		};
		D.Steps.Add(MoveTemp(Step));
	}

	/** One step that finds the lane; the trials after it reuse it. */
	TSharedRef<FLane> AddFindLane(FDemo& D, ECoverType Cover, int32 Range)
	{
		TSharedRef<FLane> Lane = MakeShared<FLane>();
		FStep Find;
		Find.Name = FString::Printf(TEXT("Find lane: %s at %d m"), CoverStr(Cover), Range);
		Find.Begin = [&D, Lane, Cover, Range]()
		{
			SetBallistics(false);
			*Lane = FindLane(D, Cover, Range);
			if (Lane->bValid)
			{
				UE_LOG(LogSquadDemoCombat, Log, TEXT("DemoCombat: lane %s %d m: shooter %s, target %s, facing %s."), CoverStr(Cover), Range,
					*CellStr(Lane->Shooter), *CellStr(Lane->Target), *CellStr(Lane->Cover));
			}
			else
			{
				UE_LOG(LogSquadDemoCombat, Warning, TEXT("DemoCombat: no lane for %s at %d m on this map; skipped."), CoverStr(Cover), Range);
				D.TrialRows.Add(FString::Printf(TEXT("nolane,,,,%s,%d,0,,,,,,,,,,,,,,,,,,"), CoverStr(Cover), Range));
			}
		};
		Find.Tick = [](float) { return true; };
		D.Steps.Add(MoveTemp(Find));
		return Lane;
	}

	// ------------------------------------------------------------------------------------------- run / restore

	void WriteFiles(FDemo& D)
	{
		auto Save = [&D](const TCHAR* Suffix, const TCHAR* Header, const TArray<FString>& Rows)
		{
			if (Rows.Num() == 0)
			{
				return;
			}
			TArray<FString> Lines;
			Lines.Add(Header);
			Lines.Append(Rows);
			const FString Path = D.OutBase + Suffix;
			FFileHelper::SaveStringArrayToFile(Lines, *Path);
			UE_LOG(LogSquadDemoCombat, Log, TEXT("DemoCombat: wrote %s (%d rows)"), *Path, Rows.Num());
		};
		Save(TEXT("_trials.csv"), TEXT("test,mode,shooter_stance,target,cover,range_m,los,shown_chance,shown_exposure,rounds,hits,hit_pct,mean_chance,mean_exposure,mean_spread_mrad,cover_stops,stray,friendly,head,chest,belly,hips,thighs,shins,seconds,shooter_cell,target_cell"), D.TrialRows);
		Save(TEXT("_engagements.csv"), TEXT("test,mode,shooter_stance,target,cover,range_m,rep,result,rounds_to_injure,time_to_injure,rounds_to_kill,time_to_kill,rounds,hits,seconds,los_lost_after_injury_s,mean_chance,shooter_cell,target_cell"), D.EngageRows);
		Save(TEXT("_shots.csv"), TEXT("test,mode,shooter_stance,target,cover,range_m,rep,round,target_injured,chance,exposure,spread_mrad,braced,hit,part,outcome"), D.ShotRows);
	}

	void Finish(FDemo& D, bool bCompleted)
	{
		ASoldier::OnShotResolved.Remove(D.ShotHandle);
		WriteFiles(D);
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			for (const FSoldierRestore& R : D.Restore)
			{
				ASoldier* S = R.Soldier.Get();
				if (S && S->GetCurrentCell() != R.Cell)
				{
					S->DebugTeleportTo(R.Cell);
				}
			}
		}
		for (const FSoldierRestore& R : D.Restore)
		{
			if (ASoldier* S = R.Soldier.Get())
			{
				S->SetActorRotation(FRotator(0.f, R.Yaw, 0.f));
				S->DebugHeal();
				S->OrderStance(R.Stance);
				S->DebugRestock();
				S->DebugResetCombatStats();
				S->OrderHoldFire(R.bHoldFire);
				S->DebugSetInvulnerable(false);
			}
		}
		if (IConsoleVariable* V = FindVar(TEXT("squad.Bot"))) { V->Set(D.PrevBot, ECVF_SetByConsole); }
		if (IConsoleVariable* V = FindVar(TEXT("squad.VPWinTime"))) { V->Set(D.PrevVPWinTime, ECVF_SetByConsole); }
		if (IConsoleVariable* V = FindVar(TEXT("squad.MatchTime"))) { V->Set(D.PrevMatchTime, ECVF_SetByConsole); }
		if (IConsoleVariable* V = FindVar(TEXT("squad.Ballistics"))) { V->Set(D.PrevBallistics, ECVF_SetByConsole); }
		UE_LOG(LogSquadDemoCombat, Log, TEXT("DemoCombat: %s. Output: %s_*.csv"), bCompleted ? TEXT("done") : TEXT("stopped early"), *D.OutBase);
		if (D.bQuit)
		{
			FPlatformMisc::RequestExit(false, TEXT("squad.DemoCombat quit"));
		}
	}

	bool TickDemo(float)
	{
		FDemo& D = *Demo;
		UWorld* World = D.World.Get();
		if (!World || !D.Shooter.IsValid() || !D.Target.IsValid() || !D.Grid.IsValid())
		{
			UE_LOG(LogSquadDemoCombat, Warning, TEXT("DemoCombat: world or soldiers gone; stopped."));
			ASoldier::OnShotResolved.Remove(D.ShotHandle);
			ActiveDemo.Reset();
			Demo.Reset();
			return false;
		}
		const float GameDelta = World->GetDeltaSeconds();
		D.Clock += GameDelta;
		if (D.Index < 0 || D.Steps[D.Index].Tick(D.StepTime += GameDelta))
		{
			++D.Index;
			D.StepTime = 0.f;
			if (!D.Steps.IsValidIndex(D.Index))
			{
				TSharedPtr<FDemo> Keep = Demo;
				ActiveDemo.Reset();
				Finish(D, true);
				Demo.Reset();
				return false;
			}
			if (D.Index % 25 == 0)
			{
				UE_LOG(LogSquadDemoCombat, Log, TEXT("DemoCombat: [%d/%d] %s"), D.Index + 1, D.Steps.Num(), *D.Steps[D.Index].Name);
			}
			if (D.Steps[D.Index].Begin)
			{
				D.Steps[D.Index].Begin();
			}
		}
		return true;
	}

	UWorld* FindServerWorld(UWorld* Hint)
	{
		if (Hint && Hint->GetNetMode() != NM_Client && (Hint->WorldType == EWorldType::Game || Hint->WorldType == EWorldType::PIE))
		{
			return Hint;
		}
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				UWorld* W = Ctx.World();
				if (W && W->GetNetMode() != NM_Client && (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE))
				{
					return W;
				}
			}
		}
		return nullptr;
	}

	void StartDemo(const TArray<FString>& Args, UWorld* WorldHint);

	/** Started from -ExecCmds the soldiers may not be spawned yet: try again for up to 30 s. */
	void RetryLater(const TArray<FString>& Args, int32 Attempt)
	{
		if (Attempt > 30)
		{
			UE_LOG(LogSquadDemoCombat, Warning, TEXT("DemoCombat: no soldiers after 30 s; giving up."));
			return;
		}
		TArray<FString> Copy = Args;
		Copy.Add(FString::Printf(TEXT("__attempt=%d"), Attempt + 1));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Copy](float) { StartDemo(Copy, nullptr); return false; }), 1.f);
	}

	void StartDemo(const TArray<FString>& InArgs, UWorld* WorldHint)
	{
		if (ActiveDemo.IsValid())
		{
			UE_LOG(LogSquadDemoCombat, Warning, TEXT("DemoCombat: already running."));
			return;
		}
		TArray<FString> Args;
		int32 Attempt = 0;
		bool bQuit = false;
		for (const FString& A : InArgs)
		{
			if (A.StartsWith(TEXT("__attempt=")))
			{
				Attempt = FCString::Atoi(*A.RightChop(10));
			}
			else if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
			{
				bQuit = true;
			}
			else
			{
				Args.Add(A);
			}
		}
		TArray<FString> Passed = Args;
		if (bQuit) { Passed.Add(TEXT("quit")); }

		UWorld* World = FindServerWorld(WorldHint);
		AGridManager* Grid = World ? AGridManager::Find(World) : nullptr;
		ASoldier* Shooter = nullptr;
		ASoldier* Target = nullptr;
		if (World)
		{
			for (TActorIterator<ASoldier> It(World); It; ++It)
			{
				if (It->IsDead()) { continue; }
				if (It->Team == ESquadTeam::Allies && It->GetSquadNumber() == 1 && !Shooter) { Shooter = *It; }
				if (It->Team == ESquadTeam::Axis && It->GetSquadNumber() == 1 && !Target) { Target = *It; }
			}
		}
		if (!World || !Grid || !Shooter || !Target)
		{
			UE_LOG(LogSquadDemoCombat, Log, TEXT("DemoCombat: waiting for the map and soldiers (attempt %d)."), Attempt);
			RetryLater(Passed, Attempt);
			return;
		}

		const FString What = Args.Num() > 0 ? Args[0].ToLower() : TEXT("all");
		const bool bHitRate = What == TEXT("all") || What == TEXT("hitrate");
		const bool bEngage = What == TEXT("all") || What == TEXT("engage");
		TSharedPtr<FDemo> New = MakeShared<FDemo>();
		FDemo& D = *New;
		D.World = World;
		D.Grid = Grid;
		D.Shooter = Shooter;
		D.Target = Target;
		D.What = What;
		D.bQuit = bQuit;
		D.RoundsPerTrial = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 5, 1000) : 40;
		D.EngageReps = Args.Num() > 2 ? FMath::Clamp(FCString::Atoi(*Args[2]), 1, 200) : 12;

		for (TActorIterator<ASoldier> It(World); It; ++It)
		{
			ASoldier* S = *It;
			if (S->IsDead()) { continue; }
			FSoldierRestore& R = D.Restore.AddDefaulted_GetRef();
			R.Soldier = S;
			R.Cell = S->GetCurrentCell();
			R.Stance = S->GetStance();
			R.bHoldFire = S->IsHoldingFire();
			R.Yaw = S->GetActorRotation().Yaw;
			S->DebugSetInvulnerable(true);
			S->OrderHoldFire(true);
			S->OrderStop();
		}
		if (IConsoleVariable* V = FindVar(TEXT("squad.Bot"))) { D.PrevBot = V->GetInt(); V->Set(0, ECVF_SetByConsole); }
		if (IConsoleVariable* V = FindVar(TEXT("squad.VPWinTime"))) { D.PrevVPWinTime = V->GetFloat(); V->Set(1000000.f, ECVF_SetByConsole); }
		if (IConsoleVariable* V = FindVar(TEXT("squad.MatchTime"))) { D.PrevMatchTime = V->GetFloat(); V->Set(-1.f, ECVF_SetByConsole); }
		if (IConsoleVariable* V = FindVar(TEXT("squad.Ballistics"))) { D.PrevBallistics = V->GetInt(); }

		const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Demo"), TEXT("Combat"));
		IFileManager::Get().MakeDirectory(*Dir, true);
		D.OutBase = FPaths::Combine(Dir, FString::Printf(TEXT("DemoCombat_%s_%s"), *What, *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))));

		// Target set-ups by cover. Crouched behind low cover is braced (§16.2); prone shows what an injured soldier gets.
		struct FCoverSet { ECoverType Cover; TArray<FTargetCfg> HitRate; TArray<FTargetCfg> Engage; TArray<int32> Ranges; };
		const TArray<int32> AllRanges = { 4, 8, 12, 16, 20, 25, 30, 40 };
		const TArray<int32> EngageRanges = { 4, 8, 15, 25, 35 };
		TArray<FCoverSet> Sets;
		Sets.Add({ ECoverType::None,
			{ { TEXT("open_standing"), ECoverType::None, ESoldierStance::Standing }, { TEXT("open_crouched"), ECoverType::None, ESoldierStance::Crouched },
			  { TEXT("open_prone"), ECoverType::None, ESoldierStance::Prone } },
			{ { TEXT("open_standing"), ECoverType::None, ESoldierStance::Standing }, { TEXT("open_prone"), ECoverType::None, ESoldierStance::Prone } } });
		Sets.Add({ ECoverType::Soft,
			{ { TEXT("hay_braced"), ECoverType::Soft, ESoldierStance::Crouched }, { TEXT("hay_prone"), ECoverType::Soft, ESoldierStance::Prone } },
			{ { TEXT("hay_braced"), ECoverType::Soft, ESoldierStance::Crouched } } });
		Sets.Add({ ECoverType::LowWall,
			{ { TEXT("lowwall_braced"), ECoverType::LowWall, ESoldierStance::Crouched }, { TEXT("lowwall_prone"), ECoverType::LowWall, ESoldierStance::Prone } },
			{ { TEXT("lowwall_braced"), ECoverType::LowWall, ESoldierStance::Crouched } } });
		Sets.Add({ ECoverType::Window,
			{ { TEXT("window_braced"), ECoverType::Window, ESoldierStance::Crouched }, { TEXT("window_standing"), ECoverType::Window, ESoldierStance::Standing } },
			{ { TEXT("window_braced"), ECoverType::Window, ESoldierStance::Crouched } } });
		Sets.Add({ ECoverType::Tree,
			{ { TEXT("tree_crouched"), ECoverType::Tree, ESoldierStance::Crouched } },
			{ { TEXT("tree_crouched"), ECoverType::Tree, ESoldierStance::Crouched } } });

		const ESoldierStance ShooterStances[] = { ESoldierStance::Standing, ESoldierStance::Prone };
		for (const FCoverSet& Set : Sets)
		{
			TArray<int32> Ranges = AllRanges;
			if (bEngage && !bHitRate) { Ranges = EngageRanges; }
			else if (bEngage) { for (int32 R : EngageRanges) { Ranges.AddUnique(R); } Ranges.Sort(); }
			for (int32 Range : Ranges)
			{
				TSharedRef<FLane> Lane = AddFindLane(D, Set.Cover, Range);
				for (const ESoldierStance ShooterStance : ShooterStances)
				{
					for (const bool bBallistics : { false, true })
					{
						if (bHitRate && AllRanges.Contains(Range))
						{
							for (const FTargetCfg& Cfg : Set.HitRate)
							{
								AddHitRateTrial(D, Lane, Cfg, Range, ShooterStance, bBallistics);
							}
						}
						if (bEngage && EngageRanges.Contains(Range) && ShooterStance == ESoldierStance::Standing)
						{
							for (const FTargetCfg& Cfg : Set.Engage)
							{
								for (int32 Rep = 1; Rep <= D.EngageReps; ++Rep)
								{
									AddEngagement(D, Lane, Cfg, Range, ShooterStance, bBallistics, Rep);
								}
							}
						}
					}
				}
			}
		}

		D.ShotHandle = ASoldier::OnShotResolved.AddStatic(&OnShot);
		Demo = New;
		ActiveDemo = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickDemo));
		UE_LOG(LogSquadDemoCombat, Log, TEXT("DemoCombat: started (%s, %d steps, %d rounds per trial, %d engagement reps). Shooter %s, target %s."),
			*What, D.Steps.Num(), D.RoundsPerTrial, D.EngageReps, *Shooter->GetName(), *Target->GetName());
	}

	void StopDemo(const TArray<FString>&, UWorld*)
	{
		if (ActiveDemo.IsValid() && Demo.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(ActiveDemo);
			ActiveDemo.Reset();
			Finish(*Demo, false);
			Demo.Reset();
		}
	}

	FAutoConsoleCommandWithWorldAndArgs CmdDemoCombat(
		TEXT("squad.DemoCombat"),
		TEXT("squad.DemoCombat [hitrate|engage|all=all] [rounds per trial=40] [engagement reps=12] [quit]: firing-range comparison of "
			 "hit-chance rolls and projectile combat (GDD §16) on identical lanes: open / hay / low wall / window / tree, 4-40 m. "
			 "Results in Saved/Demo/Combat; summarise with Scripts/Tests/compare_combat_modes.py."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StartDemo),
		ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs CmdDemoCombatStop(
		TEXT("squad.DemoCombatStop"),
		TEXT("Stops squad.DemoCombat, writes what it has and puts everything back."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StopDemo),
		ECVF_Cheat);
}
#endif
