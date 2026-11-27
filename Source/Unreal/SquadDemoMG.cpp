// Demo (non-shipping): machine-gun showcase and tuning measurement (GDD §6.1). Server / standalone only.
//
//   squad.DemoMG [terrain|stats|all = all] [trial seconds = 40]
//
// terrain: the local player's soldier 2 sets the M1919 up on each kind of ground (open ground prone / crouched /
//          standing, behind a hay bale, a low wall, at a window), then fires at a target in front; then the same
//          with the MG 42. Screenshots of each phase go to Saved/Demo/MG, and each case is logged (rest surface,
//          pitch, set-up time).
// stats:   the same shooter fires the M1 Garand, M1919 and MG 42 in turn at one invulnerable target, at short, mid
//          and long range, with the target exposed, behind a hay bale and behind a low wall (crouched, leaning on
//          the cover). Each trial runs for the given time after the order to fire, with real hit rolls and stress.
//          Then a crossing test: a target runs across the gun's front at 15 m. Results: Saved/Demo/MG/MGDemo_stats.csv
//          and LogSquadDemo.
//
// Every soldier is made invulnerable, the bot is paused and the VP win timer is pushed out while it runs; all of it
// (positions, weapons, stances, hold fire, cvars, camera) is put back afterwards.

#include "CoverData.h"
#include "GridManager.h"
#include "Soldier.h"
#include "SquadCameraPawn.h"
#include "SquadPlayerController.h"
#include "WeaponData.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogSquadDemo, Log, All);

#if !UE_BUILD_SHIPPING
namespace
{
	constexpr int32 LaneCells = 14;
	constexpr float TerrainFireTime = 3.f;
	constexpr float CrossingTime = 15.f;
	constexpr int32 CrossingRange = 15;
	constexpr int32 CrossingHalfWidth = 6;

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
		UWeaponData* Weapon = nullptr;
		ESoldierStance Stance = ESoldierStance::Standing;
		bool bHoldFire = false;
		float Yaw = 0.f;
	};

	struct FDemo
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<ASquadPlayerController> PC;
		TWeakObjectPtr<AGridManager> Grid;
		TWeakObjectPtr<ASoldier> Gunner;
		TWeakObjectPtr<ASoldier> Target;
		UWeaponData* Garand = nullptr;
		UWeaponData* M1919 = nullptr;
		UWeaponData* MG42 = nullptr;
		UWeaponData* Thompson = nullptr;
		UWeaponData* MP40 = nullptr;
		/** Weapons the stats and crossing trials compare. */
		TArray<UWeaponData*> StatsWeapons;
		TArray<FSoldierRestore> Restore;
		TArray<FStep> Steps;
		int32 Index = -1;
		float StepTime = 0.f;
		float TrialTime = 40.f;
		FString OutDir;
		FString Csv;
		TArray<FString> Summary;
		int32 ShotIndex = 0;
		// Saved console state.
		int32 PrevBot = 1;
		float PrevVPWinTime = 0.f;
		float PrevMatchTime = 0.f;
		float PrevSlomo = 1.f;
		float OrigArm = 1800.f;
		FRotator OrigCamRot = FRotator::ZeroRotator;
		FVector OrigCamLoc = FVector::ZeroVector;
	};

	FTSTicker::FDelegateHandle ActiveDemo;
	TSharedPtr<FDemo> Demo;

	FString CellStr(FIntPoint C) { return FString::Printf(TEXT("(%d,%d)"), C.X, C.Y); }
	const TCHAR* CoverStr(ECoverType T) { return T == ECoverType::None ? TEXT("Exposed") : UCoverData::GetCoverName(T); }
	FString WeaponStr(const UWeaponData* W) { return W ? W->DisplayName.ToString() : TEXT("?"); }

	IConsoleVariable* FindVar(const TCHAR* Name) { return IConsoleManager::Get().FindConsoleVariable(Name); }

	void Exec(FDemo& D, const TCHAR* Cmd)
	{
		if (GEngine && D.World.IsValid())
		{
			GEngine->Exec(D.World.Get(), Cmd);
		}
	}

	USpringArmComponent* CameraArm(FDemo& D, APawn*& OutPawn)
	{
		OutPawn = D.PC.IsValid() ? D.PC->GetPawn() : nullptr;
		return OutPawn ? OutPawn->FindComponentByClass<USpringArmComponent>() : nullptr;
	}

	/** Point the camera at Focus from the side (Yaw is the camera's heading), Arm cm away. */
	void FrameCamera(FDemo& D, const FVector& Focus, float Yaw, float Arm)
	{
		APawn* Pawn = nullptr;
		if (USpringArmComponent* SpringArm = CameraArm(D, Pawn))
		{
			Pawn->SetActorLocation(FVector(Focus.X, Focus.Y, Pawn->GetActorLocation().Z));
			Pawn->SetActorRotation(FRotator(0.f, Yaw, 0.f));
			SpringArm->TargetArmLength = Arm;
			SpringArm->bEnableCameraLag = false;
		}
	}

	void Screenshot(FDemo& D, const FString& Name)
	{
		const FString Path = FPaths::Combine(D.OutDir, FString::Printf(TEXT("MGDemo_%02d_%s.png"), ++D.ShotIndex, *Name));
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		UE_LOG(LogSquadDemo, Log, TEXT("DemoMG: screenshot %s"), *Path);
	}

	/** Turn at once (the cover reduction depends on which way the target faces, GDD §9.2). */
	void FaceNow(ASoldier* S, FIntPoint Cell, const AGridManager& Grid)
	{
		S->FaceCell(Cell);
		const FVector To = Grid.CellToWorld(Cell) - S->GetActorLocation();
		S->SetActorRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
	}

	void ResetSoldier(ASoldier* S)
	{
		S->OrderHoldFire(true);
		S->DebugHeal();
		S->DebugRestock();
		S->DebugResetCombatStats();
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

	/** No cover on the four cells around C (so a soldier there leans on nothing). */
	bool HasNoCoverAround(const AGridManager& Grid, FIntPoint C)
	{
		static const FIntPoint Dirs[] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
		for (const FIntPoint& D : Dirs)
		{
			if (Grid.GetCoverCell(C + D))
			{
				return false;
			}
		}
		return true;
	}

	/** Cells From+Dir*1 .. From+Dir*Count carry no sight-blocking cover. */
	bool IsSightLaneClear(const AGridManager& Grid, FIntPoint From, FIntPoint Dir, int32 Count, bool bNoCoverAtAll)
	{
		for (int32 K = 1; K <= Count; ++K)
		{
			const FIntPoint C = From + Dir * K;
			if (!Grid.IsInBounds(C))
			{
				return false;
			}
			const FGridCoverCell* Cover = Grid.GetCoverCell(C);
			if (Cover && (bNoCoverAtAll || Cover->bBlocksLineOfSight))
			{
				return false;
			}
		}
		return true;
	}

	static const FIntPoint OrthoDirs[] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

	/** Teleports S to the free cell nearest Cell (within 5 cells). */
	bool TeleportNear(const AGridManager& Grid, ASoldier* S, FIntPoint Cell)
	{
		for (int32 R = 0; R <= 5; ++R)
		{
			for (int32 DX = -R; DX <= R; ++DX)
			{
				for (int32 DY = -R; DY <= R; ++DY)
				{
					if (FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) == R && IsOpen(Grid, Cell + FIntPoint(DX, DY), S) && S->DebugTeleportTo(Cell + FIntPoint(DX, DY)))
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	/**
	 * Spots for the gunner to set up on: Stand is where it stands, Cover the cover cell it faces (or the cell in front,
	 * in the open), Dir the way it faces. Candidates come with a clear lane in front for a target.
	 */
	struct FSpot { FIntPoint Stand; FIntPoint Cover; FIntPoint Dir; };

	TArray<FSpot> FindSetupSpots(const AGridManager& Grid, ECoverType Type, const ASoldier* Gunner)
	{
		TArray<FSpot> Out;
		if (Type == ECoverType::None)
		{
			const FIntPoint Size(Grid.GridSizeX, Grid.GridSizeY);
			for (int32 X = 2; X < Size.X - 2; ++X)
			{
				for (int32 Y = 2; Y < Size.Y - 2; ++Y)
				{
					const FIntPoint C(X, Y);
					if (!IsOpen(Grid, C, Gunner) || !HasNoCoverAround(Grid, C))
					{
						continue;
					}
					for (const FIntPoint& D : OrthoDirs)
					{
						if (IsSightLaneClear(Grid, C, D, LaneCells, true))
						{
							Out.Add({ C, C + D, D });
						}
					}
				}
			}
			return Out;
		}
		for (const TPair<FIntPoint, FGridCoverCell>& Pair : Grid.GetCoverCells())
		{
			if (Pair.Value.Type != Type || Pair.Value.bPassable)
			{
				continue;
			}
			for (const FIntPoint& D : OrthoDirs)
			{
				const FIntPoint Stand = Pair.Key - D;
				if (!CrossesLine(Pair.Value, D) || !IsOpen(Grid, Stand, Gunner) || !IsSightLaneClear(Grid, Pair.Key, D, LaneCells - 1, false))
				{
					continue;
				}
				Out.Add({ Stand, Pair.Key, D });
			}
		}
		return Out;
	}

	/** Puts the target on the lane in front of the spot, as far out as it can (about 12 m), standing. */
	bool PlaceTargetInFront(FDemo& D, const FSpot& Spot)
	{
		ASoldier* Target = D.Target.Get();
		ASoldier* Gunner = D.Gunner.Get();
		const AGridManager* Grid = D.Grid.Get();
		for (int32 K = 12; K >= 6; --K)
		{
			const FIntPoint C = Spot.Stand + Spot.Dir * K;
			if (IsOpen(*Grid, C, Target) && Target->DebugTeleportTo(C))
			{
				Target->OrderStance(ESoldierStance::Standing);
				FaceNow(Target, Spot.Stand, *Grid);
				if (Gunner->GetHitChanceAgainst(Target).CanFire())
				{
					return true;
				}
			}
		}
		return false;
	}

	// ------------------------------------------------------------------------------------------- terrain cases

	void AddTerrainCase(FDemo& D, const FString& Name, ECoverType Cover, ESoldierStance Stance, UWeaponData* Weapon)
	{
		struct FCaseState { FSpot Spot; bool bValid = false; float SetupAt = -1.f; float FireAt = -1.f; bool bShotMid = false; bool bShotSetUp = false; bool bHasTarget = false; int32 RoundsAtFire = 0; };
		TSharedRef<FCaseState> St = MakeShared<FCaseState>();
		FStep Step;
		Step.Name = Name;
		Step.Begin = [&D, St, Name, Cover, Stance, Weapon]()
		{
			ASoldier* Gunner = D.Gunner.Get();
			const AGridManager* Grid = D.Grid.Get();
			if (Gunner->GetWeaponData() != Weapon)
			{
				Gunner->EquipStartingWeapon(Weapon);
			}
			ResetSoldier(Gunner);
			const TArray<FSpot> Spots = FindSetupSpots(*Grid, Cover, Gunner);
			// Prefer the spot nearest the map centre (away from the edges, more to see).
			const FVector2D Centre(Grid->GridSizeX * 0.5f, Grid->GridSizeY * 0.5f);
			TArray<FSpot> Sorted = Spots;
			Sorted.Sort([&Centre](const FSpot& A, const FSpot& B) { return FVector2D::DistSquared(FVector2D(A.Stand), Centre) < FVector2D::DistSquared(FVector2D(B.Stand), Centre); });
			for (const FSpot& Spot : Sorted)
			{
				if (!Gunner->DebugTeleportTo(Spot.Stand))
				{
					continue;
				}
				Gunner->OrderStance(Stance);
				FaceNow(Gunner, Spot.Cover, *Grid);
				if (Cover != ECoverType::None && Gunner->GetLeaningCoverType() != Cover)
				{
					continue; // Not actually leaning on that cover from here.
				}
				St->Spot = Spot;
				St->bValid = true;
				St->bHasTarget = PlaceTargetInFront(D, Spot);
				break;
			}
			if (!St->bValid)
			{
				UE_LOG(LogSquadDemo, Warning, TEXT("DemoMG: %s: no spot on this map, skipped."), *Name);
				return;
			}
			const float FaceYaw = FVector(FVector2D(St->Spot.Dir), 0.f).Rotation().Yaw;
			FrameCamera(D, Gunner->GetActorLocation(), FaceYaw + 90.f, 650.f);
			UE_LOG(LogSquadDemo, Log, TEXT("DemoMG: %s: %s at %s facing %s, target %s."), *Name, *WeaponStr(Weapon), *CellStr(St->Spot.Stand),
				*CellStr(St->Spot.Cover), St->bHasTarget ? TEXT("placed") : TEXT("NONE (no clear lane)"));
		};
		Step.Tick = [&D, St, Name](float T) -> bool
		{
			ASoldier* Gunner = D.Gunner.Get();
			if (!St->bValid || !Gunner)
			{
				return true;
			}
			if (!St->bShotMid && T >= 1.f)
			{
				St->bShotMid = true;
				Screenshot(D, Name.Replace(TEXT(" "), TEXT("")).Replace(TEXT(","), TEXT("_")) + TEXT("_SettingUp"));
			}
			if (St->SetupAt < 0.f && Gunner->IsWeaponSetUp())
			{
				St->SetupAt = T;
			}
			if (St->SetupAt >= 0.f && !St->bShotSetUp && T >= St->SetupAt + 0.6f)
			{
				St->bShotSetUp = true;
				Screenshot(D, Name.Replace(TEXT(" "), TEXT("")).Replace(TEXT(","), TEXT("_")) + TEXT("_SetUp"));
				const FString Line = FString::Printf(TEXT("%s | %s | set up after %.2f s | rests on %s | pitch %.1f deg"), *Name, *WeaponStr(Gunner->GetWeaponData()),
					St->SetupAt, Gunner->GetSetupRestName(), Gunner->GetSetupRestPitch());
				UE_LOG(LogSquadDemo, Log, TEXT("DemoMG: %s"), *Line);
				D.Summary.Add(Line);
				if (St->bHasTarget && D.Target.IsValid())
				{
					St->RoundsAtFire = Gunner->GetRoundsFiredStat();
					Gunner->OrderAttack(D.Target.Get());
					St->FireAt = T;
				}
			}
			if (St->FireAt >= 0.f && T >= St->FireAt + TerrainFireTime)
			{
				Screenshot(D, Name.Replace(TEXT(" "), TEXT("")).Replace(TEXT(","), TEXT("_")) + TEXT("_Firing"));
				const FString Line = FString::Printf(TEXT("%s | fired %d rounds in %.0f s, still set up: %s, rests on %s"), *Name,
					Gunner->GetRoundsFiredStat() - St->RoundsAtFire, TerrainFireTime, Gunner->IsWeaponSetUp() ? TEXT("yes") : TEXT("NO"), Gunner->GetSetupRestName());
				UE_LOG(LogSquadDemo, Log, TEXT("DemoMG: %s"), *Line);
				D.Summary.Add(Line);
				Gunner->OrderHoldFire(true);
				return true;
			}
			if (St->SetupAt >= 0.f && St->FireAt < 0.f && St->bShotSetUp)
			{
				return true; // No target: done after the set-up shot.
			}
			return T > 12.f; // Safety net.
		};
		D.Steps.Add(MoveTemp(Step));
	}

	// ------------------------------------------------------------------------------------------- stats trials

	struct FTrialSpot { FIntPoint Shooter; FIntPoint Target; FIntPoint Cover; bool bValid = false; };

	FTrialSpot FindTrialSpot(FDemo& D, ECoverType Cover, int32 Range)
	{
		ASoldier* Shooter = D.Gunner.Get();
		ASoldier* Target = D.Target.Get();
		const AGridManager* Grid = D.Grid.Get();
		TArray<FTrialSpot> Candidates;
		auto TryAdd = [&](FIntPoint T, FIntPoint Dir, FIntPoint C)
		{
			const FIntPoint S = T - Dir * Range;
			if (!IsOpen(*Grid, S, Shooter) || !HasNoCoverAround(*Grid, S))
			{
				return;
			}
			// Nothing in the way between the shooter and the cover / target.
			const int32 Clear = Cover == ECoverType::None ? Range - 1 : Range - 2;
			if (!IsSightLaneClear(*Grid, S, Dir, Clear, true))
			{
				return;
			}
			Candidates.Add({ S, T, C, true });
		};
		if (Cover == ECoverType::None)
		{
			for (int32 X = 1; X < Grid->GridSizeX - 1; ++X)
			{
				for (int32 Y = 1; Y < Grid->GridSizeY - 1; ++Y)
				{
					const FIntPoint T(X, Y);
					if (!IsOpen(*Grid, T, Target) || !HasNoCoverAround(*Grid, T))
					{
						continue;
					}
					for (const FIntPoint& Dir : OrthoDirs)
					{
						TryAdd(T, Dir, T - Dir);
					}
				}
			}
		}
		else
		{
			for (const TPair<FIntPoint, FGridCoverCell>& Pair : Grid->GetCoverCells())
			{
				if (Pair.Value.Type != Cover || Pair.Value.bPassable)
				{
					continue;
				}
				for (const FIntPoint& Dir : OrthoDirs)
				{
					const FIntPoint T = Pair.Key + Dir; // Target right behind the cover, the shooter in front of it.
					if (CrossesLine(Pair.Value, Dir) && IsOpen(*Grid, T, Target))
					{
						TryAdd(T, Dir, Pair.Key);
					}
				}
			}
		}
		// Validate in the world: the target must be seen, in range and have exactly that cover against the shooter.
		for (const FTrialSpot& Spot : Candidates)
		{
			if (!Shooter->DebugTeleportTo(Spot.Shooter) || !Target->DebugTeleportTo(Spot.Target))
			{
				continue;
			}
			Target->OrderStance(ESoldierStance::Crouched);
			Shooter->OrderStance(ESoldierStance::Standing);
			FaceNow(Target, Spot.Cover, *Grid);
			FaceNow(Shooter, Spot.Target, *Grid);
			const FHitChanceInfo Chance = Shooter->GetHitChanceAgainst(Target);
			if (Chance.CanFire() && Chance.Cover == Cover && Chance.bCoverLeaning)
			{
				return Spot;
			}
		}
		return FTrialSpot();
	}

	void AddCsvHeader(FDemo& D)
	{
		D.Csv = TEXT("test,weapon,cover,range_m,hit_chance_pct,trial_s,setup_s,first_shot_s,rounds,hits,hit_pct,hits_per_s,stress_total,stress_per_s,time_to_shocked_s,first_hit_s,second_hit_s,end_stress,extra\n");
	}

	void AddStatsTrials(FDemo& D, ECoverType Cover, int32 Range)
	{
		struct FGeo { FTrialSpot Spot; };
		TSharedRef<FGeo> Geo = MakeShared<FGeo>();
		// One step finds the geometry; the weapon trials that follow reuse it.
		FStep Find;
		Find.Name = FString::Printf(TEXT("Find %s %d m"), CoverStr(Cover), Range);
		Find.Begin = [&D, Geo, Cover, Range]()
		{
			Geo->Spot = FindTrialSpot(D, Cover, Range);
			if (!Geo->Spot.bValid)
			{
				UE_LOG(LogSquadDemo, Warning, TEXT("DemoMG: no spot for %s at %d m on this map; those trials are skipped."), CoverStr(Cover), Range);
			}
			else
			{
				UE_LOG(LogSquadDemo, Log, TEXT("DemoMG: %s at %d m: shooter %s, target %s behind %s."), CoverStr(Cover), Range,
					*CellStr(Geo->Spot.Shooter), *CellStr(Geo->Spot.Target), *CellStr(Geo->Spot.Cover));
			}
		};
		Find.Tick = [](float) { return true; };
		D.Steps.Add(MoveTemp(Find));

		for (UWeaponData* Weapon : D.StatsWeapons)
		{
			struct FTrial
			{
				float OrderAt = -1.f; float SetupAt = -1.f; float FirstShot = -1.f; float Shocked = -1.f; float Hit1 = -1.f; float Hit2 = -1.f;
				float Chance = 0.f; int32 LastHits = 0;
			};
			TSharedRef<FTrial> Tr = MakeShared<FTrial>();
			FStep Step;
			Step.Name = FString::Printf(TEXT("%s vs %s %d m"), *WeaponStr(Weapon), CoverStr(Cover), Range);
			Step.Begin = [&D, Geo, Tr, Weapon]()
			{
				ASoldier* Shooter = D.Gunner.Get();
				ASoldier* Target = D.Target.Get();
				if (!Geo->Spot.bValid)
				{
					return;
				}
				Shooter->EquipStartingWeapon(Weapon);
				Shooter->DebugTeleportTo(Geo->Spot.Shooter);
				Target->DebugTeleportTo(Geo->Spot.Target);
				ResetSoldier(Shooter);
				ResetSoldier(Target);
				Shooter->OrderStance(ESoldierStance::Standing);
				Target->OrderStance(ESoldierStance::Crouched);
				FaceNow(Target, Geo->Spot.Cover, *D.Grid);
				FaceNow(Shooter, Geo->Spot.Target, *D.Grid);
				FrameCamera(D, (Shooter->GetActorLocation() + Target->GetActorLocation()) * 0.5f, D.PC->GetPawn()->GetActorRotation().Yaw, 2600.f);
			};
			Step.Tick = [&D, Geo, Tr, Weapon, Cover, Range](float T) -> bool
			{
				ASoldier* Shooter = D.Gunner.Get();
				ASoldier* Target = D.Target.Get();
				if (!Geo->Spot.bValid || !Shooter || !Target)
				{
					return true;
				}
				if (Tr->OrderAt < 0.f)
				{
					// A machine gun sets up before the order (it is holding the area), so the trial measures the
					// engagement itself; the set-up time is reported separately.
					if (Weapon->NeedsSetup() && !Shooter->IsWeaponSetUp())
					{
						if (Tr->SetupAt < 0.f && T > 6.f)
						{
							UE_LOG(LogSquadDemo, Warning, TEXT("DemoMG: %s never set up; trial skipped."), *WeaponStr(Weapon));
							return true;
						}
						return false;
					}
					Tr->SetupAt = T;
					Target->SetStress(0.f);
					Target->DebugResetCombatStats();
					Tr->Chance = Shooter->GetHitChanceAgainst(Target).FinalChance;
					Shooter->OrderAttack(Target);
					Tr->OrderAt = T;
					return false;
				}
				const float E = T - Tr->OrderAt;
				if (Tr->FirstShot < 0.f && Shooter->GetRoundsFiredStat() > 0)
				{
					Tr->FirstShot = E;
				}
				const int32 Hits = Shooter->GetHitsLanded();
				if (Hits != Tr->LastHits)
				{
					if (Tr->Hit1 < 0.f) { Tr->Hit1 = E; }
					else if (Tr->Hit2 < 0.f) { Tr->Hit2 = E; }
					if (Hits >= 2 && Tr->Hit2 < 0.f) { Tr->Hit2 = E; }
					Tr->LastHits = Hits;
				}
				if (Tr->Shocked < 0.f && Target->IsShocked())
				{
					Tr->Shocked = E;
				}
				if (E < D.TrialTime)
				{
					return false;
				}
				const int32 Rounds = Shooter->GetRoundsFiredStat();
				const float HitPct = Rounds > 0 ? 100.f * Hits / Rounds : 0.f;
				D.Csv += FString::Printf(TEXT("fixed,%s,%s,%d,%.1f,%.0f,%.2f,%.2f,%d,%d,%.1f,%.3f,%.1f,%.2f,%.2f,%.2f,%.2f,%.1f,\n"),
					*WeaponStr(Weapon), CoverStr(Cover), Range, Tr->Chance, D.TrialTime, Weapon->NeedsSetup() ? Weapon->SetupTime : 0.f, Tr->FirstShot,
					Rounds, Hits, HitPct, Hits / D.TrialTime, Target->GetStressReceivedTotal(), Target->GetStressReceivedTotal() / D.TrialTime,
					Tr->Shocked, Tr->Hit1, Tr->Hit2, Target->GetStress());
				const FString Line = FString::Printf(TEXT("%-15s %-8s %2d m  chance %5.1f%%  rounds %3d  hits %3d (%5.1f%%)  stress %6.1f (%5.2f/s)  shocked at %s  1st hit %s"),
					*WeaponStr(Weapon), CoverStr(Cover), Range, Tr->Chance, Rounds, Hits, HitPct, Target->GetStressReceivedTotal(), Target->GetStressReceivedTotal() / D.TrialTime,
					Tr->Shocked >= 0.f ? *FString::Printf(TEXT("%.1fs"), Tr->Shocked) : TEXT("never"),
					Tr->Hit1 >= 0.f ? *FString::Printf(TEXT("%.1fs"), Tr->Hit1) : TEXT("never"));
				UE_LOG(LogSquadDemo, Log, TEXT("DemoMG: %s"), *Line);
				D.Summary.Add(Line);
				Shooter->OrderHoldFire(true);
				return true;
			};
			D.Steps.Add(MoveTemp(Step));
		}
	}

	/** A target runs across the gun's front at 15 m; how many hits, how much stress, and is it pinned (Shocked)? */
	void AddCrossingTrials(FDemo& D)
	{
		struct FLane { FIntPoint Shooter; FIntPoint Dir; FIntPoint Perp; bool bValid = false; };
		TSharedRef<FLane> Lane = MakeShared<FLane>();
		FStep Find;
		Find.Name = TEXT("Find crossing lane");
		Find.Begin = [&D, Lane]()
		{
			ASoldier* Shooter = D.Gunner.Get();
			ASoldier* Target = D.Target.Get();
			const AGridManager* Grid = D.Grid.Get();
			for (int32 X = 1; X < Grid->GridSizeX - 1 && !Lane->bValid; ++X)
			{
				for (int32 Y = 1; Y < Grid->GridSizeY - 1 && !Lane->bValid; ++Y)
				{
					const FIntPoint S(X, Y);
					if (!IsOpen(*Grid, S, Shooter) || !HasNoCoverAround(*Grid, S))
					{
						continue;
					}
					for (const FIntPoint& Dir : OrthoDirs)
					{
						const FIntPoint Perp(-Dir.Y, Dir.X);
						const FIntPoint Mid = S + Dir * CrossingRange;
						bool bOk = IsSightLaneClear(*Grid, S, Dir, CrossingRange, true);
						for (int32 K = -CrossingHalfWidth; K <= CrossingHalfWidth && bOk; ++K)
						{
							bOk = IsOpen(*Grid, Mid + Perp * K, Target);
						}
						if (!bOk || !Shooter->DebugTeleportTo(S))
						{
							continue;
						}
						// Every lane cell must be in sight from the shooter.
						for (int32 K = -CrossingHalfWidth; K <= CrossingHalfWidth && bOk; K += 2)
						{
							bOk = Target->DebugTeleportTo(Mid + Perp * K) && Shooter->GetHitChanceAgainst(Target).CanFire();
						}
						if (bOk)
						{
							*Lane = { S, Dir, Perp, true };
							break;
						}
					}
				}
			}
			UE_LOG(LogSquadDemo, Log, TEXT("DemoMG: crossing lane %s"), Lane->bValid ? *FString::Printf(TEXT("from %s"), *CellStr(Lane->Shooter)) : TEXT("NOT FOUND, skipped"));
		};
		Find.Tick = [](float) { return true; };
		D.Steps.Add(MoveTemp(Find));

		for (UWeaponData* Weapon : D.StatsWeapons)
		{
			struct FRun { float StartAt = -1.f; float Shocked = -1.f; float Arrived = -1.f; float Hit1 = -1.f; float MaxStress = 0.f; int32 CellsCrossed = 0; };
			TSharedRef<FRun> Run = MakeShared<FRun>();
			FStep Step;
			Step.Name = FString::Printf(TEXT("%s crossing"), *WeaponStr(Weapon));
			Step.Begin = [&D, Lane, Weapon]()
			{
				if (!Lane->bValid)
				{
					return;
				}
				ASoldier* Shooter = D.Gunner.Get();
				ASoldier* Target = D.Target.Get();
				const FIntPoint Mid = Lane->Shooter + Lane->Dir * CrossingRange;
				Shooter->EquipStartingWeapon(Weapon);
				Shooter->DebugTeleportTo(Lane->Shooter);
				Target->DebugTeleportTo(Mid - Lane->Perp * CrossingHalfWidth);
				ResetSoldier(Shooter);
				ResetSoldier(Target);
				Shooter->OrderStance(ESoldierStance::Prone);
				Target->OrderStance(ESoldierStance::Standing);
				FaceNow(Shooter, Mid, *D.Grid);
				// Squad cohesion (GDD §4.3): bring the runner's squad-mates up behind the lane so its move isn't clamped.
				for (const FSoldierRestore& R : D.Restore)
				{
					ASoldier* Mate = R.Soldier.Get();
					if (Mate && Mate != Target && Mate->Team == Target->Team)
					{
						TeleportNear(*D.Grid, Mate, Mid + Lane->Dir * 4 + Lane->Perp * (Mate->GetSquadNumber() - 2));
					}
				}
				FrameCamera(D, D.Grid->CellToWorld(Lane->Shooter + Lane->Dir * (CrossingRange / 2)), D.PC->GetPawn()->GetActorRotation().Yaw, 2600.f);
			};
			Step.Tick = [&D, Lane, Run, Weapon](float T) -> bool
			{
				ASoldier* Shooter = D.Gunner.Get();
				ASoldier* Target = D.Target.Get();
				if (!Lane->bValid || !Shooter || !Target)
				{
					return true;
				}
				const FIntPoint Mid = Lane->Shooter + Lane->Dir * CrossingRange;
				const FIntPoint End = Mid + Lane->Perp * CrossingHalfWidth;
				if (Run->StartAt < 0.f)
				{
					if (Weapon->NeedsSetup() && !Shooter->IsWeaponSetUp() && T < 6.f)
					{
						return false; // Already set up on the lane before the target breaks cover.
					}
					Shooter->DebugResetCombatStats();
					Target->SetStress(0.f);
					Target->DebugResetCombatStats();
					Target->MoveToCell(End);
					Shooter->OrderAttack(Target); // Explicit, so the squad-mates behind the lane are left alone.
					Run->StartAt = T;
					return false;
				}
				const float E = T - Run->StartAt;
				Run->MaxStress = FMath::Max(Run->MaxStress, Target->GetStress());
				if (Run->Hit1 < 0.f && Shooter->GetHitsLanded() > 0) { Run->Hit1 = E; }
				if (Run->Shocked < 0.f && Target->IsShocked()) { Run->Shocked = E; }
				if (Run->Arrived < 0.f && Target->GetCurrentCell() == End && !Target->IsMoving()) { Run->Arrived = E; }
				if (Run->Arrived < 0.f && E < CrossingTime)
				{
					return false;
				}
				const FIntPoint Start = Mid - Lane->Perp * CrossingHalfWidth;
				const FIntPoint Now = Target->GetCurrentCell();
				const int32 Crossed = FMath::Abs((Now - Start).X) + FMath::Abs((Now - Start).Y);
				const int32 Rounds = Shooter->GetRoundsFiredStat();
				const int32 Hits = Shooter->GetHitsLanded();
				const float Window = Run->Arrived >= 0.f ? Run->Arrived : CrossingTime;
				D.Csv += FString::Printf(TEXT("crossing,%s,Exposed,%d,,%.1f,%.2f,,%d,%d,%.1f,%.3f,%.1f,%.2f,%.2f,%.2f,,%.1f,cells_crossed=%d/%d max_stress=%.0f\n"),
					*WeaponStr(Weapon), CrossingRange, Window, Weapon->NeedsSetup() ? Weapon->SetupTime : 0.f, Rounds, Hits, Rounds ? 100.f * Hits / Rounds : 0.f,
					Hits / Window, Target->GetStressReceivedTotal(), Target->GetStressReceivedTotal() / Window, Run->Shocked, Run->Hit1, Target->GetStress(),
					Crossed, CrossingHalfWidth * 2, Run->MaxStress);
				const FString Line = FString::Printf(TEXT("CROSSING %-15s crossed %2d/%d cells in %4.1f s  rounds %3d  hits %2d  max stress %3.0f  pinned (shocked) at %s"),
					*WeaponStr(Weapon), Crossed, CrossingHalfWidth * 2, Window, Rounds, Hits, Run->MaxStress,
					Run->Shocked >= 0.f ? *FString::Printf(TEXT("%.1fs"), Run->Shocked) : TEXT("never"));
				UE_LOG(LogSquadDemo, Log, TEXT("DemoMG: %s"), *Line);
				D.Summary.Add(Line);
				Shooter->OrderHoldFire(true);
				Target->OrderStop();
				return true;
			};
			D.Steps.Add(MoveTemp(Step));
		}
	}

	// ------------------------------------------------------------------------------------------- run / restore

	void Finish(FDemo& D, bool bCompleted)
	{
		// Put everything back. Teleports run twice: one can fail while another soldier still stands on that cell.
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
				if (R.Weapon && S->GetWeaponData() != R.Weapon)
				{
					S->EquipStartingWeapon(R.Weapon);
				}
				S->SetActorRotation(FRotator(0.f, R.Yaw, 0.f));
				S->OrderStance(R.Stance);
				S->DebugHeal();
				S->DebugRestock();
				S->DebugResetCombatStats();
				S->OrderHoldFire(R.bHoldFire);
				S->DebugSetInvulnerable(false);
			}
		}
		if (IConsoleVariable* V = FindVar(TEXT("squad.Bot"))) { V->Set(D.PrevBot); }
		if (IConsoleVariable* V = FindVar(TEXT("squad.VPWinTime"))) { V->Set(D.PrevVPWinTime); }
		if (IConsoleVariable* V = FindVar(TEXT("squad.MatchTime"))) { V->Set(D.PrevMatchTime); }
		Exec(D, *FString::Printf(TEXT("slomo %g"), D.PrevSlomo));
		APawn* Pawn = nullptr;
		if (USpringArmComponent* Arm = CameraArm(D, Pawn))
		{
			Arm->TargetArmLength = D.OrigArm;
			Pawn->SetActorRotation(D.OrigCamRot);
			Pawn->SetActorLocation(D.OrigCamLoc);
		}
		if (!D.Csv.IsEmpty())
		{
			const FString CsvPath = FPaths::Combine(D.OutDir, TEXT("MGDemo_stats.csv"));
			FFileHelper::SaveStringToFile(D.Csv, *CsvPath);
			UE_LOG(LogSquadDemo, Log, TEXT("DemoMG: stats written to %s"), *CsvPath);
		}
		const FString SummaryPath = FPaths::Combine(D.OutDir, TEXT("MGDemo_summary.txt"));
		FFileHelper::SaveStringToFile(FString::Join(D.Summary, TEXT("\n")), *SummaryPath);
		UE_LOG(LogSquadDemo, Log, TEXT("DemoMG: %s. Summary: %s"), bCompleted ? TEXT("done") : TEXT("stopped early"), *SummaryPath);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Green, FString::Printf(TEXT("MG demo %s. Results in %s"), bCompleted ? TEXT("done") : TEXT("stopped"), *D.OutDir));
		}
	}

	bool TickDemo(float DeltaTime)
	{
		FDemo& D = *Demo;
		UWorld* World = D.World.Get();
		if (!World || !D.Gunner.IsValid() || !D.Target.IsValid() || !D.Grid.IsValid() || !D.PC.IsValid())
		{
			UE_LOG(LogSquadDemo, Warning, TEXT("DemoMG: world or soldiers gone; stopped."));
			ActiveDemo.Reset();
			Demo.Reset();
			return false;
		}
		const float GameDelta = World->GetDeltaSeconds();
		if (D.Index < 0 || D.Steps[D.Index].Tick(D.StepTime += GameDelta))
		{
			++D.Index;
			D.StepTime = 0.f;
			if (!D.Steps.IsValidIndex(D.Index))
			{
				Finish(D, true);
				ActiveDemo.Reset();
				Demo.Reset();
				return false;
			}
			UE_LOG(LogSquadDemo, Log, TEXT("DemoMG: [%d/%d] %s"), D.Index + 1, D.Steps.Num(), *D.Steps[D.Index].Name);
			if (D.Steps[D.Index].Begin)
			{
				D.Steps[D.Index].Begin();
			}
		}
		return true;
	}

	void StartDemo(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogSquadDemo, Warning, TEXT("DemoMG: run it on the server (standalone or listen server PIE)."));
			return;
		}
		if (ActiveDemo.IsValid())
		{
			UE_LOG(LogSquadDemo, Warning, TEXT("DemoMG: already running."));
			return;
		}
		AGridManager* Grid = AGridManager::Find(World);
		ASquadPlayerController* PC = Cast<ASquadPlayerController>(World->GetFirstPlayerController());
		if (!Grid || !PC || !PC->GetPawn())
		{
			UE_LOG(LogSquadDemo, Warning, TEXT("DemoMG: no grid or squad player controller in this world."));
			return;
		}
		const FString Mode = Args.Num() > 0 ? Args[0].ToLower() : TEXT("all");
		const bool bTerrain = Mode == TEXT("all") || Mode == TEXT("terrain");
		const bool bStats = Mode == TEXT("all") || Mode == TEXT("stats");
		const bool bSmg = Mode == TEXT("smg"); // GDD §6.2: submachine guns against the rifle at close quarters.

		TSharedPtr<FDemo> New = MakeShared<FDemo>();
		FDemo& D = *New;
		D.World = World;
		D.PC = PC;
		D.Grid = Grid;
		D.TrialTime = Args.Num() > 1 ? FMath::Clamp(FCString::Atof(*Args[1]), 5.f, 300.f) : 40.f;
		D.Garand = LoadObject<UWeaponData>(nullptr, TEXT("/Game/Data/DA_M1Garand.DA_M1Garand"));
		D.M1919 = LoadObject<UWeaponData>(nullptr, TEXT("/Game/Data/DA_M1919.DA_M1919"));
		D.MG42 = LoadObject<UWeaponData>(nullptr, TEXT("/Game/Data/DA_MG42.DA_MG42"));
		D.Thompson = LoadObject<UWeaponData>(nullptr, TEXT("/Game/Data/DA_Thompson.DA_Thompson"));
		D.MP40 = LoadObject<UWeaponData>(nullptr, TEXT("/Game/Data/DA_MP40.DA_MP40"));
		if (!D.Garand || !D.M1919 || !D.MG42 || (bSmg && (!D.Thompson || !D.MP40)))
		{
			UE_LOG(LogSquadDemo, Warning, TEXT("DemoMG: missing weapon data (DA_M1Garand / DA_M1919 / DA_MG42 / DA_Thompson / DA_MP40)."));
			return;
		}
		if (bSmg)
		{
			D.StatsWeapons = { D.Garand, D.Thompson, D.MP40 };
		}
		else
		{
			D.StatsWeapons = { D.Garand, D.M1919, D.MG42 };
		}
		for (TActorIterator<ASoldier> It(World); It; ++It)
		{
			ASoldier* S = *It;
			if (S->IsDead())
			{
				continue;
			}
			FSoldierRestore& R = D.Restore.AddDefaulted_GetRef();
			R.Soldier = S;
			R.Cell = S->GetCurrentCell();
			R.Weapon = const_cast<UWeaponData*>(S->GetWeaponData());
			R.Stance = S->GetStance();
			R.bHoldFire = S->IsHoldingFire();
			R.Yaw = S->GetActorRotation().Yaw;
			S->DebugSetInvulnerable(true);
			S->OrderHoldFire(true);
			S->OrderStop();
			if (S->Team == PC->PlayerTeam && S->GetSquadNumber() == 2) { D.Gunner = S; }
			if (S->Team != PC->PlayerTeam && S->GetSquadNumber() == 1) { D.Target = S; }
		}
		if (!D.Gunner.IsValid() || !D.Target.IsValid())
		{
			UE_LOG(LogSquadDemo, Warning, TEXT("DemoMG: needs your soldier 2 and an enemy soldier 1 alive."));
			for (const FSoldierRestore& R : D.Restore) { if (R.Soldier.IsValid()) { R.Soldier->DebugSetInvulnerable(false); R.Soldier->OrderHoldFire(R.bHoldFire); } }
			return;
		}
		if (IConsoleVariable* V = FindVar(TEXT("squad.Bot"))) { D.PrevBot = V->GetInt(); V->Set(0); }
		if (IConsoleVariable* V = FindVar(TEXT("squad.VPWinTime"))) { D.PrevVPWinTime = V->GetFloat(); V->Set(100000.f); }
		if (IConsoleVariable* V = FindVar(TEXT("squad.MatchTime"))) { D.PrevMatchTime = V->GetFloat(); V->Set(-1.f); }
		if (AWorldSettings* WS = World->GetWorldSettings()) { D.PrevSlomo = WS->TimeDilation; }
		APawn* Pawn = nullptr;
		if (USpringArmComponent* Arm = CameraArm(D, Pawn))
		{
			D.OrigArm = Arm->TargetArmLength;
			D.OrigCamRot = Pawn->GetActorRotation();
			D.OrigCamLoc = Pawn->GetActorLocation();
		}
		D.OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Demo"), TEXT("MG"));
		IFileManager::Get().MakeDirectory(*D.OutDir, true);

		if (bTerrain)
		{
			FStep Speed; Speed.Name = TEXT("Terrain showcase (real time)");
			Speed.Begin = [&D]() { Exec(D, TEXT("slomo 1")); };
			Speed.Tick = [](float) { return true; };
			D.Steps.Add(MoveTemp(Speed));
			AddTerrainCase(D, TEXT("Open ground, prone"), ECoverType::None, ESoldierStance::Prone, D.M1919);
			AddTerrainCase(D, TEXT("Open ground, crouched"), ECoverType::None, ESoldierStance::Crouched, D.M1919);
			AddTerrainCase(D, TEXT("Open ground, standing"), ECoverType::None, ESoldierStance::Standing, D.M1919);
			AddTerrainCase(D, TEXT("Hay bale, crouched"), ECoverType::Soft, ESoldierStance::Crouched, D.M1919);
			AddTerrainCase(D, TEXT("Low wall, crouched"), ECoverType::LowWall, ESoldierStance::Crouched, D.M1919);
			AddTerrainCase(D, TEXT("Low wall, standing"), ECoverType::LowWall, ESoldierStance::Standing, D.M1919);
			AddTerrainCase(D, TEXT("Window, standing"), ECoverType::Window, ESoldierStance::Standing, D.M1919);
			AddTerrainCase(D, TEXT("MG42 open ground, prone"), ECoverType::None, ESoldierStance::Prone, D.MG42);
			AddTerrainCase(D, TEXT("MG42 low wall, crouched"), ECoverType::LowWall, ESoldierStance::Crouched, D.MG42);
		}
		if (bStats)
		{
			AddCsvHeader(D);
			FStep Speed; Speed.Name = TEXT("Statistics (4x speed)");
			Speed.Begin = [&D]() { Exec(D, TEXT("slomo 4")); };
			Speed.Tick = [](float) { return true; };
			D.Steps.Add(MoveTemp(Speed));
			for (ECoverType Cover : { ECoverType::None, ECoverType::Soft, ECoverType::LowWall })
			{
				for (int32 Range : { 6, 15, 30 })
				{
					AddStatsTrials(D, Cover, Range);
				}
			}
			AddCrossingTrials(D);
		}
		if (bSmg)
		{
			AddCsvHeader(D);
			FStep Speed; Speed.Name = TEXT("SMG statistics (4x speed)");
			Speed.Begin = [&D]() { Exec(D, TEXT("slomo 4")); };
			Speed.Tick = [](float) { return true; };
			D.Steps.Add(MoveTemp(Speed));
			for (ECoverType Cover : { ECoverType::None, ECoverType::Soft, ECoverType::LowWall })
			{
				for (int32 Range : { 4, 8, 12, 20 })
				{
					AddStatsTrials(D, Cover, Range);
				}
			}
		}
		Demo = New;
		ActiveDemo = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickDemo));
		UE_LOG(LogSquadDemo, Log, TEXT("DemoMG: started (%s, %d steps, %.0f s trials). Output: %s"), *Mode, D.Steps.Num(), D.TrialTime, *D.OutDir);
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

	FAutoConsoleCommandWithWorldAndArgs CmdDemoMG(
		TEXT("squad.DemoMG"),
		TEXT("squad.DemoMG [terrain|stats|smg|all=all] [trial seconds=40]: machine-gun demo (GDD §6.1). smg (not in all): M1 Garand vs "
			 "Thompson vs MP 40 at 4 / 8 / 12 / 20 m, exposed / hay / low wall (GDD §6.2). terrain: your soldier 2 sets the M1919 "
			 "(then the MG 42) up on open ground, hay, a low wall and a window and fires. stats: M1 Garand vs M1919 vs MG 42 against an "
			 "invulnerable target at 6 / 15 / 30 m, exposed / hay / low wall, plus a crossing run. Results in Saved/Demo/MG."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StartDemo),
		ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs CmdDemoMGStop(
		TEXT("squad.DemoMGStop"),
		TEXT("Stops squad.DemoMG and puts everything back."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StopDemo),
		ECVF_Cheat);
}
#endif
