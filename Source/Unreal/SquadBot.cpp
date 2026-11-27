#include "SquadBot.h"

#include "DeploymentZone.h"
#include "Grenade.h"
#include "GridManager.h"
#include "Soldier.h"
#include "SoldierData.h"
#include "SquadCameraPawn.h"
#include "SquadGameState.h"
#include "VictoryPoint.h"
#include "WeaponData.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSquadBot, Log, All);

namespace
{
	TAutoConsoleVariable<int32> CVarSquadBot(
		TEXT("squad.Bot"), 1,
		TEXT("1: AI bots command squads no player controls (default). 0: bots stand idle."),
		ECVF_Cheat);

	TAutoConsoleVariable<int32> CVarSquadBotDebug(
		TEXT("squad.BotDebug"), 0,
		TEXT("1: draw what the AI bot sees: its simulated screen (yellow), enemies it is noticing (orange) and enemies it knows about (red)."),
		ECVF_Cheat);

	constexpr float UUPerMetre = 100.f;
	/** How old a remembered position may be for a grenade to be thrown at it. */
	constexpr float MaxGrenadeIntelAge = 3.f;

	float CellDistance(FIntPoint A, FIntPoint B)
	{
		return FVector2D(A - B).Size();
	}

	float RandRange(const FVector2D& Range)
	{
		return FMath::FRandRange(FMath::Min(Range.X, Range.Y), FMath::Max(Range.X, Range.Y));
	}

	ESquadTeam OtherTeam(ESquadTeam Team)
	{
		return Team == ESquadTeam::Allies ? ESquadTeam::Axis : ESquadTeam::Allies;
	}

	/** Soldiers that hold ground for VP purposes (GDD §10.3 provisional: able to act). */
	bool CanHoldGround(const ASoldier* S)
	{
		return S && !S->IsDead() && !S->IsInjured();
	}

	FVector2D To2D(const FVector& V)
	{
		return FVector2D(V.X, V.Y);
	}
}

// ------------------------------------------------------------------------------------------------ View

bool ASquadBot::FView::Contains(const FVector2D& Point) const
{
	// Convex quad: the point is inside if it is on the same side of every edge.
	float Sign = 0.f;
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector2D& A = Corners[i];
		const FVector2D& B = Corners[(i + 1) % 4];
		const float Cross = FVector2D::CrossProduct(B - A, Point - A);
		if (FMath::IsNearlyZero(Cross))
		{
			continue;
		}
		if (Sign == 0.f)
		{
			Sign = FMath::Sign(Cross);
		}
		else if (FMath::Sign(Cross) != Sign)
		{
			return false;
		}
	}
	return true;
}

float ASquadBot::FView::EdgeFactor(const FVector2D& Point) const
{
	return FMath::Clamp(FVector2D::Distance(Point, Centre) / FMath::Max(Radius, 1.f), 0.f, 1.f);
}

// ------------------------------------------------------------------------------------------------ Setup

ASquadBot::ASquadBot()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false; // Server-only brain; it acts through the soldiers' own (server-side) orders.
}

ASquadBot* ASquadBot::FindForSquad(const UObject* WorldContextObject, ESquadTeam InTeam, int32 InSquadId)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ASquadBot> It(World); It; ++It)
	{
		if (It->Team == InTeam && It->SquadId == InSquadId)
		{
			return *It;
		}
	}
	return nullptr;
}

void ASquadBot::BeginPlay()
{
	Super::BeginPlay();
	Grid = AGridManager::Find(this);
	if (!Grid)
	{
		UE_LOG(LogSquadBot, Warning, TEXT("%s: no GridManager in level; bot disabled."), *GetName());
		return;
	}

	const ADeploymentZone* EnemyDZ = ADeploymentZone::FindForTeam(this, OtherTeam(Team));
	const ADeploymentZone* OwnDZ = ADeploymentZone::FindForTeam(this, Team);
	EnemyHomeCell = EnemyDZ
		? EnemyDZ->GetMinCell() + FIntPoint(EnemyDZ->SizeX / 2, EnemyDZ->SizeY / 2)
		: FIntPoint(Grid->GridSizeX / 2, Team == ESquadTeam::Axis ? 0 : Grid->GridSizeY - 1); // No DZ: the far end.

	GatherSquad();

	// The camera starts over the squad, looking towards the enemy's side like a player would set it up.
	FVector2D Centroid = FVector2D::ZeroVector;
	int32 Count = 0;
	for (const FSoldierBrain& Brain : Brains)
	{
		if (Brain.Soldier.IsValid())
		{
			Centroid += To2D(Brain.Soldier->GetActorLocation());
			++Count;
		}
	}
	CameraFocus = CameraGoal = Count > 0 ? Centroid / Count : To2D(Grid->CellToWorld(EnemyHomeCell));
	const FVector2D From = OwnDZ ? To2D(OwnDZ->GetActorLocation()) : CameraFocus;
	const FVector2D Towards = To2D(Grid->CellToWorld(EnemyHomeCell)) - From;
	CameraYaw = Towards.IsNearlyZero() ? 0.f : FMath::RadiansToDegrees(FMath::Atan2(Towards.Y, Towards.X));
	CameraDwellUntil = Now() + RandRange(CameraDwellTime);
	ComputeView();
}

float ASquadBot::Now() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

void ASquadBot::GatherSquad()
{
	TArray<FSoldierBrain> OldBrains = MoveTemp(Brains);
	TArray<FEnemyMemory> OldMemory = MoveTemp(Memory);
	Brains.Reset();
	Memory.Reset();
	VictoryPoints.Reset();

	for (TActorIterator<ASoldier> It(GetWorld()); It; ++It)
	{
		ASoldier* S = *It;
		if (S->Team == Team && S->SquadId != SquadId)
		{
			continue; // Another friendly squad: its own bot or player commands it.
		}
		if (S->Team != Team)
		{
			const FEnemyMemory* Existing = OldMemory.FindByPredicate([S](const FEnemyMemory& M) { return M.Soldier.Get() == S; });
			FEnemyMemory& Mem = Memory.Add_GetRef(Existing ? *Existing : FEnemyMemory());
			Mem.Soldier = S;
			continue;
		}
		const FSoldierBrain* Existing = OldBrains.FindByPredicate([S](const FSoldierBrain& B) { return B.Soldier.Get() == S; });
		FSoldierBrain& Brain = Brains.Add_GetRef(Existing ? *Existing : FSoldierBrain());
		if (!Existing)
		{
			Brain.Soldier = S;
			// Stagger first decisions so the squad doesn't act in lock-step.
			Brain.NextDecision = Now() + DecisionInterval * 0.25f * S->SquadIndex;
		}
	}
	Brains.Sort([](const FSoldierBrain& A, const FSoldierBrain& B)
	{
		return A.Soldier.IsValid() && B.Soldier.IsValid() && A.Soldier->SquadIndex < B.Soldier->SquadIndex;
	});

	for (TActorIterator<AVictoryPoint> It(GetWorld()); It; ++It)
	{
		VictoryPoints.Add(*It);
	}
}

void ASquadBot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority() || !Grid || CVarSquadBot.GetValueOnGameThread() == 0)
	{
		return;
	}
	if (const ASquadGameState* GS = GetWorld()->GetGameState<ASquadGameState>())
	{
		if (GS->IsMatchOver())
		{
			return;
		}
	}

	const float T = Now();
	if (T >= NextAssign)
	{
		NextAssign = T + AssignInterval;
		GatherSquad();
		AssignVictoryPoints();
	}

	UpdateCamera(DeltaSeconds);
	UpdatePerception(DeltaSeconds);

	for (FSoldierBrain& Brain : Brains)
	{
		// GDD §5.2: a squad-mate going down is noticed after a moment (its card turns red), then a medic may go.
		const ASoldier* Soldier = Brain.Soldier.Get();
		if (Soldier && Soldier->IsInjured())
		{
			if (Brain.InjuryNoticedAt < 0.f)
			{
				Brain.InjuryNoticedAt = T + RandRange(MedicReactionTime);
			}
		}
		else
		{
			Brain.InjuryNoticedAt = -1.f;
		}

		if (T >= Brain.NextDecision)
		{
			Brain.NextDecision = T + DecisionInterval;
			Think(Brain);
		}
	}

	if (CVarSquadBotDebug.GetValueOnGameThread() != 0)
	{
		const UWorld* World = GetWorld();
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector2D& A = View.Corners[i];
			const FVector2D& B = View.Corners[(i + 1) % 4];
			DrawDebugLine(World, FVector(A, 20.f), FVector(B, 20.f), FColor::Yellow, false, 0.f, 0, 8.f);
		}
		for (const FEnemyMemory& Mem : Memory)
		{
			if (Mem.bKnown)
			{
				DrawDebugSphere(World, Grid->CellToWorld(Mem.LastKnownCell) + FVector(0.f, 0.f, 220.f), 30.f, 8, FColor::Red, false, 0.f, 0, 3.f);
			}
			else if (Mem.NoticeProgress > 0.f && Mem.Soldier.IsValid())
			{
				DrawDebugSphere(World, Mem.Soldier->GetActorLocation() + FVector(0.f, 0.f, 220.f), 20.f, 8, FColor::Orange, false, 0.f, 0, 2.f);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------ Simulated player

void ASquadBot::UpdateCamera(float DeltaSeconds)
{
	const float T = Now();

	// A squad card flashing (under fire, hit, stunned) off screen pulls the camera over, after a moment.
	for (FSoldierBrain& Brain : Brains)
	{
		ASoldier* S = Brain.Soldier.Get();
		if (!S || S->IsDead())
		{
			Brain.bWasAlerting = false;
			continue;
		}
		const bool bAlerting = S->IsUnderFire() || S->GetDamageFlash() > 0.f;
		if (bAlerting && !Brain.bWasAlerting && !PendingAlert.IsValid() && !View.Contains(To2D(S->GetActorLocation())))
		{
			PendingAlert = S;
			PendingAlertTime = T + RandRange(AlertReactionTime);
		}
		Brain.bWasAlerting = bAlerting;
	}

	if (ASoldier* Alert = PendingAlert.Get(); Alert && T >= PendingAlertTime)
	{
		CameraGoal = To2D(Alert->GetActorLocation());
		CameraDwellUntil = T + RandRange(CameraDwellTime);
		PendingAlert = nullptr;
	}
	else if (T >= CameraDwellUntil && Brains.Num() > 0)
	{
		// Check on the next soldier that isn't comfortably on screen; fighting soldiers first.
		const ASoldier* Pick = nullptr;
		for (const FSoldierBrain& Brain : Brains)
		{
			const ASoldier* S = Brain.Soldier.Get();
			if (S && !S->IsDead() && S->GetAttackTarget() && View.EdgeFactor(To2D(S->GetActorLocation())) > 0.6f)
			{
				Pick = S;
				break;
			}
		}
		for (int32 Tries = 0; !Pick && Tries < Brains.Num(); ++Tries)
		{
			NextSoldierToCheck = (NextSoldierToCheck + 1) % Brains.Num();
			const ASoldier* S = Brains[NextSoldierToCheck].Soldier.Get();
			if (S && !S->IsDead() && View.EdgeFactor(To2D(S->GetActorLocation())) > 0.6f)
			{
				Pick = S;
			}
		}
		if (Pick)
		{
			CameraGoal = To2D(Pick->GetActorLocation());
		}
		CameraDwellUntil = T + RandRange(CameraDwellTime);
	}

	// Pan towards the goal at the player camera's pan speed for this zoom.
	const ASquadCameraPawn* CameraDefaults = GetDefault<ASquadCameraPawn>();
	const float Zoom = FMath::Clamp(CameraZoom, CameraDefaults->MinZoom, CameraDefaults->MaxZoom);
	const float PanSpeed = CameraDefaults->PanSpeed * Zoom / ASquadCameraPawn::ReferenceArmLength;
	const FVector2D ToGoal = CameraGoal - CameraFocus;
	const float Step = PanSpeed * DeltaSeconds;
	CameraFocus = ToGoal.Size() <= Step ? CameraGoal : CameraFocus + ToGoal.GetSafeNormal() * Step;
	ComputeView();
}

void ASquadBot::ComputeView()
{
	// The same rig as ASquadCameraPawn: a spring arm pitched down, looking at a point on the ground.
	const ASquadCameraPawn* CameraDefaults = GetDefault<ASquadCameraPawn>();
	const float Zoom = FMath::Clamp(CameraZoom, CameraDefaults->MinZoom, CameraDefaults->MaxZoom);
	const float GroundZ = Grid ? Grid->GetActorLocation().Z : 0.f;
	const FRotator Rotation(ASquadCameraPawn::CameraPitch, CameraYaw, 0.f);
	const FVector Forward = Rotation.Vector();
	const FVector Right = FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y);
	const FVector Up = FRotationMatrix(Rotation).GetUnitAxis(EAxis::Z);
	const FVector Focus(CameraFocus, GroundZ);
	View.CameraLocation = Focus - Forward * Zoom;

	// Corners of a 16:9 screen, projected onto the ground.
	const float TanH = FMath::Tan(FMath::DegreesToRadians(ASquadCameraPawn::FieldOfView * 0.5f));
	const float TanV = TanH * 9.f / 16.f;
	const FVector2D ScreenCorners[4] = { {-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f} };
	View.Radius = 1.f;
	for (int32 i = 0; i < 4; ++i)
	{
		FVector Dir = Forward + Right * (ScreenCorners[i].X * TanH) + Up * (ScreenCorners[i].Y * TanV);
		Dir.Z = FMath::Min(Dir.Z, -0.05f); // Never above the horizon with this rig, but stay safe.
		const float Dist = (GroundZ - View.CameraLocation.Z) / Dir.Z;
		View.Corners[i] = To2D(View.CameraLocation + Dir * Dist);
	}
	View.Centre = CameraFocus;
	for (const FVector2D& Corner : View.Corners)
	{
		View.Radius = FMath::Max(View.Radius, FVector2D::Distance(Corner, View.Centre));
	}
}

bool ASquadBot::IsOnScreen(const FVector& WorldLocation) const
{
	return View.Contains(To2D(WorldLocation));
}

bool ASquadBot::IsHiddenFromCamera(const ASoldier& Enemy) const
{
	// Walls and tree canopies between the camera and the soldier hide it on screen, even if the team spots it.
	FCollisionQueryParams Query(SCENE_QUERY_STAT(SquadBotCamera), false, this);
	Query.AddIgnoredActor(&Enemy);
	const FVector Body = Enemy.GetAimPoint();
	const FVector Points[2] = { Body, Body + FVector(0.f, 0.f, 40.f) };
	for (const FVector& Point : Points)
	{
		if (!GetWorld()->LineTraceTestByChannel(View.CameraLocation, Point, ECC_WorldStatic, Query))
		{
			return false;
		}
	}
	return true;
}

void ASquadBot::UpdatePerception(float DeltaSeconds)
{
	const float T = Now();
	for (FEnemyMemory& Mem : Memory)
	{
		const ASoldier* E = Mem.Soldier.Get();
		if (!E || E->IsDead())
		{
			Mem.bKnown = false; // The kill feed and the body tell the player it's gone.
			Mem.NoticeProgress = 0.f;
			continue;
		}
		const bool bSpotted = E->IsVisibleToTeam(Team);
		const bool bObservable = bSpotted && IsOnScreen(E->GetActorLocation()) && !IsHiddenFromCamera(*E);
		// GDD §13: the minimap shows every enemy the team spots. A glance at it notices one off screen, more slowly.
		if (bSpotted && !bObservable)
		{
			if (Mem.MapRequired < 0.f)
			{
				Mem.MapRequired = RandRange(MinimapNoticeTime);
			}
			Mem.MapProgress += DeltaSeconds;
			if (Mem.MapProgress >= Mem.MapRequired)
			{
				Mem.bKnown = true;
			}
		}
		else if (!bSpotted)
		{
			Mem.MapProgress = 0.f;
			Mem.MapRequired = -1.f;
		}
		if (bSpotted && !bObservable && Mem.bKnown)
		{
			Mem.LastKnownCell = E->GetCurrentCell(); // Followed as a red dot.
			Mem.LastSeenTime = T;
		}
		if (bObservable)
		{
			if (!Mem.bKnown)
			{
				if (Mem.NoticeRequired < 0.f)
				{
					// How long this one takes to spot: harder lying down, in cover or at the edge of the screen.
					Mem.NoticeRequired = RandRange(NoticeTime)
						+ (E->GetEffectiveStance() == ESoldierStance::Prone ? NoticeProneDelay : 0.f)
						+ (E->GetLeaningCoverType() != ECoverType::None ? NoticeCoverDelay : 0.f)
						+ View.EdgeFactor(To2D(E->GetActorLocation())) * NoticeEdgeDelay;
				}
				Mem.NoticeProgress += DeltaSeconds;
				if (Mem.NoticeProgress >= Mem.NoticeRequired)
				{
					Mem.bKnown = true;
				}
			}
			if (Mem.bKnown)
			{
				Mem.LastKnownCell = E->GetCurrentCell();
				Mem.LastSeenTime = T;
			}
		}
		else
		{
			Mem.NoticeProgress = FMath::Max(0.f, Mem.NoticeProgress - DeltaSeconds * 2.f);
			if (Mem.NoticeProgress <= 0.f)
			{
				Mem.NoticeRequired = -1.f;
			}
			if (Mem.bKnown && T - Mem.LastSeenTime > MemoryTime)
			{
				Mem.bKnown = false;
			}
		}
	}

	// Grenades on screen are noticed after a short moment.
	for (auto It = SeenGrenades.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Key()->IsLive())
		{
			It.RemoveCurrent();
		}
	}
	for (TActorIterator<AGrenade> It(GetWorld()); It; ++It)
	{
		if (It->IsLive() && !SeenGrenades.Contains(*It) && (IsOnScreen(It->GetActorLocation()) || IsOnScreen(It->GetLandingLocation())))
		{
			SeenGrenades.Add(*It, T);
		}
	}
}

bool ASquadBot::CanIssueOrder() const
{
	return Now() >= NextOrderTime;
}

void ASquadBot::OnOrderIssued()
{
	NextOrderTime = Now() + RandRange(OrderInterval);
}

void ASquadBot::GetKnownEnemies(TArray<TPair<ASoldier*, FIntPoint>>& Out) const
{
	for (const FEnemyMemory& Mem : Memory)
	{
		if (Mem.bKnown && Mem.Soldier.IsValid() && !Mem.Soldier->IsDead())
		{
			Out.Emplace(Mem.Soldier.Get(), Mem.LastKnownCell);
		}
	}
}

// ------------------------------------------------------------------------------------------------ Squad plan

void ASquadBot::AssignVictoryPoints()
{
	// Soldiers that can take and hold ground.
	TArray<FSoldierBrain*> Able;
	FVector2D Centroid = FVector2D::ZeroVector;
	for (FSoldierBrain& Brain : Brains)
	{
		if (CanHoldGround(Brain.Soldier.Get()))
		{
			Able.Add(&Brain);
			Centroid += FVector2D(Brain.Soldier->GetCurrentCell());
		}
		else
		{
			Brain.AssignedVP = nullptr;
			Brain.bHasSlot = false;
		}
	}
	if (Able.Num() == 0)
	{
		return;
	}
	Centroid /= Able.Num();

	struct FVPInfo
	{
		AVictoryPoint* VP = nullptr;
		FIntPoint Centre;
		float Distance = 0.f;
		float Threat = 0.f;
		bool bOwned = false;
		int32 Demand = 0;
	};
	TArray<FVPInfo> VPs;
	for (const TWeakObjectPtr<AVictoryPoint>& Weak : VictoryPoints)
	{
		AVictoryPoint* VP = Weak.Get();
		if (!VP)
		{
			continue;
		}
		FVPInfo& Info = VPs.AddDefaulted_GetRef();
		Info.VP = VP;
		Info.Centre = VP->GetMinCell() + FIntPoint(VP->SizeX / 2, VP->SizeY / 2);
		Info.Distance = FVector2D::Distance(Centroid, FVector2D(Info.Centre))
			+ FriendlySquadSpreadPenalty * CountOtherSquadsAssigned(VP); // Spread the team's squads out.
		Info.bOwned = VP->IsOwnedBy(Team);
		// VP ownership and contest state are shown on every player's screen.
		if (VP->IsOwned() && !Info.bOwned)
		{
			Info.Threat += 2.f;
		}
		if (VP->IsContested())
		{
			Info.Threat += 2.f;
		}
		Info.Threat += CountKnownEnemiesNear(Grid->CellToWorld(Info.Centre), ThreatRadius);
	}
	if (VPs.Num() == 0)
	{
		return;
	}

	// Priority: points we don't own first, then nearest.
	VPs.Sort([](const FVPInfo& A, const FVPInfo& B)
	{
		if (A.bOwned != B.bOwned)
		{
			return !A.bOwned;
		}
		return A.Distance < B.Distance;
	});

	// GDD §4.3: the squad can't spread further than the cohesion limit, so only hold points that fit together
	// (and near any downed squad-mates, who still count but can't walk). An empty point keeps its owner, so
	// the others are taken in turn once these are owned.
	const float Limit = Able[0]->Soldier->GetSoldierData()->SquadCohesionLimit;
	if (Limit > 0.f)
	{
		auto HalfDiagonal = [](const AVictoryPoint* VP) { return 0.5f * FVector2D(VP->SizeX - 1, VP->SizeY - 1).Size(); };
		TArray<FVPInfo> Fitting;
		for (const FVPInfo& Info : VPs)
		{
			bool bFits = true;
			for (const FVPInfo& Taken : Fitting)
			{
				bFits &= CellDistance(Info.Centre, Taken.Centre) + HalfDiagonal(Info.VP) + HalfDiagonal(Taken.VP) <= Limit;
			}
			for (const FSoldierBrain& Brain : Brains)
			{
				const ASoldier* Mate = Brain.Soldier.Get();
				if (Mate && !Mate->IsDead() && !CanHoldGround(Mate))
				{
					bFits &= CellDistance(Info.Centre, Mate->GetCohesionCell()) + HalfDiagonal(Info.VP) <= Limit;
				}
			}
			if (bFits)
			{
				Fitting.Add(Info);
			}
		}
		if (Fitting.Num() == 0)
		{
			Fitting.Add(VPs[0]); // Nothing fits round the downed: head for the top point as far as the limit allows.
		}
		VPs = MoveTemp(Fitting);
	}

	// One soldier per VP in priority order; spares reinforce the most threatened (or the top-priority) VP.
	int32 Remaining = Able.Num();
	for (FVPInfo& Info : VPs)
	{
		if (Remaining == 0)
		{
			break;
		}
		Info.Demand = 1;
		--Remaining;
	}
	while (Remaining > 0)
	{
		FVPInfo* Best = &VPs[0];
		for (FVPInfo& Info : VPs)
		{
			if (Info.Threat > Best->Threat)
			{
				Best = &Info;
			}
		}
		++Best->Demand;
		--Remaining;
	}

	// Keep current assignments where the VP still wants them (stops soldiers swapping back and forth).
	TArray<FSoldierBrain*> Unassigned;
	for (FSoldierBrain* Brain : Able)
	{
		FVPInfo* Info = VPs.FindByPredicate([Brain](const FVPInfo& I) { return I.VP == Brain->AssignedVP.Get(); });
		if (Info && Info->Demand > 0)
		{
			--Info->Demand;
		}
		else
		{
			Unassigned.Add(Brain);
		}
	}

	// Fill what's left greedily by distance, highest-priority VP first.
	for (FVPInfo& Info : VPs)
	{
		while (Info.Demand > 0 && Unassigned.Num() > 0)
		{
			int32 BestIndex = 0;
			float BestDistance = TNumericLimits<float>::Max();
			for (int32 i = 0; i < Unassigned.Num(); ++i)
			{
				const float D = CellDistance(Unassigned[i]->Soldier->GetCurrentCell(), Info.Centre);
				if (D < BestDistance)
				{
					BestDistance = D;
					BestIndex = i;
				}
			}
			FSoldierBrain* Brain = Unassigned[BestIndex];
			Unassigned.RemoveAtSwap(BestIndex);
			Brain->AssignedVP = Info.VP;
			Brain->bHasSlot = false;
			Brain->bFacedAfterArriving = false;
			--Info.Demand;
		}
	}
}

// ------------------------------------------------------------------------------------------------ Soldier

void ASquadBot::Think(FSoldierBrain& Brain)
{
	ASoldier* S = Brain.Soldier.Get();
	if (!S || S->IsDead() || !S->CanAct())
	{
		return;
	}
	if (TryDodgeGrenade(*S))
	{
		return;
	}
	if (S->IsThrowingGrenade())
	{
		return;
	}
	if (S->GetMedkitPatient())
	{
		return; // Treating (or on the way): any other order would stop it. A grenade landing nearby still does.
	}

	// Best shot this soldier has at an enemy the bot has noticed (the player's hover preview shows this).
	TArray<TPair<ASoldier*, FIntPoint>> Known;
	GetKnownEnemies(Known);
	float BestChance = -1.f;
	for (const TPair<ASoldier*, FIntPoint>& Pair : Known)
	{
		const FHitChanceInfo Info = S->GetHitChanceAgainst(Pair.Key);
		if (Info.CanFire())
		{
			BestChance = FMath::Max(BestChance, Info.FinalChance);
		}
	}
	if (BestChance < 0.f)
	{
		Brain.bContactDecided = false; // Contact over: the next one gets a fresh decision.
	}

	if (CanIssueOrder() && TryThrowGrenade(Brain))
	{
		return;
	}
	if (CanIssueOrder() && TryTreatSquadMate(Brain))
	{
		return;
	}
	if (S->IsInjured() && S->GetTreatedBy() && S->IsMoving())
	{
		// A medic is on the way: stop crawling so it can catch up (the treatment only starts once both are still).
		if (CanIssueOrder() && S->OrderStop())
		{
			OnOrderIssued();
		}
		return;
	}

	if (Brain.bHeadDown && S->GetStance() != ESoldierStance::Prone)
	{
		Brain.bHeadDown = false; // Stood up by something else (an order, recovering from a stun).
	}

	if (S->IsMoving())
	{
		// Usually finish the move into position. Only a good shot with a long way to go may make the
		// player stop the soldier to fight, and that's decided once per contact.
		if (BestChance >= HaltForShotChance && !Brain.bContactDecided && S->GetRemainingSteps() >= HaltMinRemainingSteps && CanIssueOrder())
		{
			Brain.bContactDecided = true;
			if (FMath::FRand() < HaltProbability && S->OrderStop())
			{
				OnOrderIssued();
				return;
			}
		}
		// Caught in the open with a long way still to go to a spot that has no cover either: a player sees the
		// tracers and redirects the soldier into the nearest cover rather than let it walk on under fire.
		if (!S->IsInjured() && S->IsUnderFire() && S->GetRemainingSteps() >= BreakForCoverMinSteps
			&& Now() >= Brain.NextReposition && CanIssueOrder())
		{
			const FIntPoint ThreatCell = GetThreatCell(S->GetCurrentCell());
			if (Grid->GetCoverAgainst(S->GetDestinationCell(), ThreatCell).Reduction <= 0.f)
			{
				TrySeekCover(Brain, ThreatCell);
			}
		}
		return;
	}

	if (CanIssueOrder() && UpdateHeadDown(Brain, BestChance))
	{
		return;
	}
	if (Brain.bHeadDown)
	{
		return; // Keeping its head down behind cover until stress falls.
	}

	// Under fire in the open: get into cover unless it is winning the exchange from here. Without a target of
	// its own (it can't see the shooter) there's nothing to gain by standing there.
	if (!S->IsInjured() && S->IsUnderFire() && Now() >= Brain.NextReposition && CanIssueOrder()
		&& (!S->GetAttackTarget() || S->GetCurrentHitChance() < SeekCoverHitChance))
	{
		const FIntPoint ThreatCell = GetThreatCell(S->GetCurrentCell());
		const FCoverResult Cover = Grid->GetCoverAgainst(S->GetCurrentCell(), ThreatCell, S->GetActorRotation().Yaw);
		if (Cover.Reduction <= 0.f && TrySeekCover(Brain, ThreatCell))
		{
			return;
		}
	}

	if (S->GetAttackTarget())
	{
		// Fighting (soldiers fire on their own). Can't hit the enemy where it is: work round its cover.
		if (CanIssueOrder() && TryFlank(Brain, BestChance))
		{
			return;
		}
		// Fire and movement: a soldier stopped on its way to the VP, trading poor shots, carries on to its next
		// bound when it may (squad-mates covering it), unless it is too shaken to get up.
		const bool bOnTheWay = Brain.bHasSlot && S->GetCurrentCell() != Brain.Slot;
		if (bOnTheWay && !S->IsInjured() && S->GetCurrentHitChance() < AdvanceUnderFireShot
			&& S->GetStressTier() < EStressTier::Major && CanIssueOrder())
		{
			HoldObjective(Brain);
		}
		return;
	}

	if (S->IsInjured() || !CanIssueOrder())
	{
		return; // Injured: crawling only and can't capture, so hold and shoot what comes into view.
	}
	// Holding its slot with an enemy dug in nearby that it can't hit: work round it.
	if (Brain.bHasSlot && S->GetCurrentCell() == Brain.Slot && TryFlank(Brain, BestChance))
	{
		return;
	}
	HoldObjective(Brain);
}

bool ASquadBot::StandUpToMove(ASoldier& Soldier)
{
	if (Soldier.GetStance() == ESoldierStance::Prone && !Soldier.IsInjured() && Soldier.OrderStance(ESoldierStance::Standing))
	{
		OnOrderIssued();
		return true;
	}
	return false;
}

bool ASquadBot::UpdateHeadDown(FSoldierBrain& Brain, float BestChance)
{
	ASoldier* S = Brain.Soldier.Get();
	if (!S || S->IsInjured())
	{
		Brain.bHeadDown = false;
		return false;
	}
	const FIntPoint Cell = S->GetCurrentCell();
	const FCoverResult Cover = Grid->GetCoverAgainst(Cell, GetThreatCell(Cell));
	// GDD §9.3: lying right behind hay or a low wall hides a soldier from the front (and it sees nothing over it).
	const bool bLowCover = Cover.Type == ECoverType::Soft || Cover.Type == ECoverType::LowWall;

	if (!Brain.bHeadDown)
	{
		if (bLowCover && S->GetStress() >= HeadDownStress && BestChance < HeadDownMaxShot && S->GetStance() != ESoldierStance::Prone
			&& S->OrderStance(ESoldierStance::Prone))
		{
			Brain.bHeadDown = true;
			OnOrderIssued();
			return true;
		}
		return false;
	}
	// Recovered, or the cover no longer faces the threat (flanked): get up.
	if ((S->GetStress() <= HeadUpStress || !bLowCover) && S->OrderStance(ESoldierStance::Standing))
	{
		Brain.bHeadDown = false;
		OnOrderIssued();
		return true;
	}
	return false;
}

bool ASquadBot::IsCautious(const ASoldier& Soldier) const
{
	TArray<TPair<ASoldier*, FIntPoint>> Known;
	GetKnownEnemies(Known);
	if (Known.Num() > 0 || Soldier.IsUnderFire())
	{
		return true;
	}
	// The enemy is heading for the same VPs: near one this team doesn't hold, expect them.
	const FVector Here = Soldier.GetActorLocation();
	for (const TWeakObjectPtr<AVictoryPoint>& Weak : VictoryPoints)
	{
		const AVictoryPoint* VP = Weak.Get();
		if (VP && (!VP->IsOwnedBy(Team) || VP->IsContested())
			&& FVector::Dist2D(Here, Grid->CellToWorld(VP->GetMinCell() + FIntPoint(VP->SizeX / 2, VP->SizeY / 2))) <= CautionRadius * UUPerMetre)
		{
			return true;
		}
	}
	return false;
}

bool ASquadBot::ShouldHoldForOverwatch(const ASoldier& Soldier) const
{
	if (!IsCautious(Soldier))
	{
		return false; // No contact expected: everyone moves.
	}
	int32 Able = 0;
	int32 Moving = 0;
	for (const FSoldierBrain& Brain : Brains)
	{
		const ASoldier* Mate = Brain.Soldier.Get();
		if (!CanHoldGround(Mate))
		{
			continue;
		}
		++Able;
		if (Mate != &Soldier && Mate->IsMoving())
		{
			++Moving;
		}
	}
	const int32 MaxMoving = FMath::Max(1, FMath::FloorToInt(Able * MaxMovingFraction));
	return Moving >= MaxMoving;
}

bool ASquadBot::TryFlank(FSoldierBrain& Brain, float BestChance)
{
	ASoldier* S = Brain.Soldier.Get();
	const float T = Now();
	if (!S || S->IsInjured() || S->IsShocked() || T < Brain.NextFlank || T < Brain.FlankUntil || BestChance >= FlankShotChance
		|| S->GetStressTier() >= EStressTier::Major)
	{
		return false;
	}
	// One flanker per squad at a time; the others hold the VP.
	for (const FSoldierBrain& Other : Brains)
	{
		if (&Other != &Brain && T < Other.FlankUntil && CanHoldGround(Other.Soldier.Get()))
		{
			return false;
		}
	}

	// The nearest recently seen enemy that its cover protects from here.
	const UWeaponData* Weapon = S->GetWeaponData();
	const float MaxRange = Weapon ? Weapon->LongRangeMax : 40.f;
	const FIntPoint From = S->GetCurrentCell();
	const ASoldier* Target = nullptr;
	FIntPoint TargetCell = FIntPoint::ZeroValue;
	float TargetDistance = TNumericLimits<float>::Max();
	for (const FEnemyMemory& Mem : Memory)
	{
		const ASoldier* E = Mem.Soldier.Get();
		if (!Mem.bKnown || !E || E->IsDead() || T - Mem.LastSeenTime > MaxGrenadeIntelAge)
		{
			continue;
		}
		const float D = CellDistance(From, Mem.LastKnownCell);
		if (D > MaxRange || D >= TargetDistance || Grid->GetCoverAgainst(Mem.LastKnownCell, From).Reduction <= 0.f)
		{
			continue;
		}
		Target = E;
		TargetCell = Mem.LastKnownCell;
		TargetDistance = D;
	}
	if (!Target || !Weapon)
	{
		return false;
	}

	const UCoverData* CoverData = Grid->GetCoverData();
	const float EyeHeight = CoverData->StandingSightHeight;
	const float TargetHeight = CoverData->GetSightHeight(Target->GetEffectiveStance());
	const float StunRadius = S->GetSoldierData()->GrenadeStunRadius;
	FIntPoint BestCell = FIntPoint::ZeroValue;
	float BestScore = BestChance + 20.f; // Only worth the walk for a clearly better shot.
	bool bFound = false;
	for (int32 DY = -FlankSearchRadius; DY <= FlankSearchRadius; ++DY)
	{
		for (int32 DX = -FlankSearchRadius; DX <= FlankSearchRadius; ++DX)
		{
			const FIntPoint Cell = From + FIntPoint(DX, DY);
			const float Walk = CellDistance(Cell, From);
			if (Walk < 2.f || Walk > FlankSearchRadius)
			{
				continue;
			}
			const float Range = CellDistance(Cell, TargetCell);
			if (Range > MaxRange || Range < StunRadius + 3.f)
			{
				continue; // Out of range, or so close that it's a charge.
			}
			if (Grid->GetCoverAgainst(TargetCell, Cell).Reduction > 0.f || !Grid->IsCellFreeAsDestination(Cell, S) || IsSlotTaken(Cell, S)
				|| !Grid->HasLineOfSight(Cell, TargetCell, EyeHeight, TargetHeight) || !S->IsCohesiveDestination(Cell))
			{
				continue;
			}
			// A clean shot, ideally from cover of its own, without a long walk across the enemy's front.
			const float Score = Weapon->GetAccuracyAtRange(Range) + Grid->GetCoverAgainst(Cell, TargetCell).Reduction * 0.5f - Walk * 3.f;
			if (Score > BestScore)
			{
				BestScore = Score;
				BestCell = Cell;
				bFound = true;
			}
		}
	}
	Brain.NextFlank = T + 4.f; // Nothing found: look again in a moment.
	if (!bFound || IsRouteWatched(*S, BestCell))
	{
		return false; // Nowhere better, or getting there means walking across another enemy's sights.
	}
	if (StandUpToMove(*S))
	{
		Brain.NextFlank = T; // Up first (prone soldiers crawl); go on the next decision.
		return true;
	}
	if (!S->MoveToCell(BestCell))
	{
		return false;
	}
	OnOrderIssued();
	Brain.Slot = BestCell;
	Brain.bHasSlot = true;
	Brain.bBounding = false;
	Brain.bFacedAfterArriving = false;
	Brain.FlankUntil = T + FlankHoldTime;
	Brain.NextFlank = T + FlankCooldown;
	return true;
}

bool ASquadBot::TryTreatSquadMate(FSoldierBrain& Brain)
{
	ASoldier* S = Brain.Soldier.Get();
	if (!S || S->GetMedkits() <= 0 || S->IsUnderFire())
	{
		return false; // A player wouldn't send a soldier that is being shot at to kneel over someone for 7 s.
	}
	const float T = Now();
	const float MaxDistance = MaxMedicDistance * 100.f;
	ASoldier* BestPatient = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (const FSoldierBrain& Other : Brains)
	{
		ASoldier* Patient = Other.Soldier.Get();
		if (!Patient || Patient == S || Other.InjuryNoticedAt < 0.f || T < Other.InjuryNoticedAt || Patient->GetTreatedBy()
			|| !S->GetMedkitBlockReason(Patient).IsEmpty())
		{
			continue;
		}
		const float Distance = FVector::Dist2D(S->GetActorLocation(), Patient->GetActorLocation());
		// GDD §5.1: a squad-mate close to bleeding out is worth a longer walk, if the medic can still make it.
		const float BleedLeft = Patient->GetBleedOutRemaining();
		const bool bUrgent = BleedLeft > 0.f && BleedLeft <= UrgentBleedOutTime;
		const float Reach = bUrgent ? FMath::Max(MaxDistance, UrgentMedicDistance * 100.f) : MaxDistance;
		if (Distance > Reach || Distance >= BestDistance)
		{
			continue;
		}
		if (BleedLeft > 0.f)
		{
			const USoldierData* Data = S->GetSoldierData();
			const float WalkSeconds = Distance / FMath::Max(1.f, S->GetCurrentMoveSpeed());
			if (WalkSeconds + Data->MedkitChannelTime > BleedLeft)
			{
				continue; // Can't arrive and finish before it bleeds out.
			}
		}
		if (IsRouteWatched(*S, Patient->GetCurrentCell()))
		{
			continue; // Lying in an enemy's sights: wait until that enemy is pinned down or gone.
		}
		// Leave it to a closer squad-mate that could go (it gets its own turn to decide).
		bool bCloserMedic = false;
		for (const FSoldierBrain& Mate : Brains)
		{
			const ASoldier* M = Mate.Soldier.Get();
			if (M && M != S && M != Patient && M->GetMedkits() > 0 && !M->IsUnderFire() && !M->GetMedkitPatient()
				&& M->GetMedkitBlockReason(Patient).IsEmpty()
				&& FVector::Dist2D(M->GetActorLocation(), Patient->GetActorLocation()) < Distance)
			{
				bCloserMedic = true;
				break;
			}
		}
		if (!bCloserMedic)
		{
			BestPatient = Patient;
			BestDistance = Distance;
		}
	}
	if (!BestPatient || !S->OrderUseMedkit(BestPatient))
	{
		return false;
	}
	OnOrderIssued();
	return true;
}

bool ASquadBot::TryDodgeGrenade(ASoldier& Soldier)
{
	const float T = Now();
	const FVector Here = Soldier.GetActorLocation();
	for (const TPair<TWeakObjectPtr<AGrenade>, float>& Seen : SeenGrenades)
	{
		const AGrenade* G = Seen.Key.Get();
		if (!G || !G->IsLive() || T - Seen.Value < GrenadeNoticeTime)
		{
			continue; // Not noticed yet.
		}
		const FVector Landing = G->GetLandingLocation();
		const float DangerRadius = (G->GetStunRadius() + 0.5f) * UUPerMetre;
		if (FVector::Dist2D(Here, Landing) > DangerRadius)
		{
			continue;
		}
		if (Soldier.IsMoving() && FVector::Dist2D(Grid->CellToWorld(Soldier.GetDestinationCell()), Landing) > DangerRadius)
		{
			return true; // Already running clear.
		}
		// GDD §7.1: cover between the grenade and the soldier stops the blast. Behind low cover or a window, dropping
		// prone gets the head below the top in time; already shielded, there's nothing to do.
		FIntPoint BlastCell = FIntPoint::ZeroValue;
		const bool bBlastCell = Grid->WorldToCell(Landing, BlastCell);
		if (bBlastCell && Grid->IsBlastShielded(BlastCell, Soldier.GetSightCell(), Soldier.GetSightHeight()))
		{
			continue;
		}
		if (!CanIssueOrder())
		{
			return true; // Busy clicking another order; try again next decision.
		}
		if (bBlastCell && !Soldier.IsMoving() && Soldier.GetStance() != ESoldierStance::Prone
			&& Grid->IsBlastShielded(BlastCell, Soldier.GetCurrentCell(), Grid->GetCoverData()->ProneSightHeight)
			&& Soldier.OrderStance(ESoldierStance::Prone))
		{
			OnOrderIssued();
			return true;
		}

		// Nearest free cell outside the blast.
		const FIntPoint From = Soldier.GetCurrentCell();
		const int32 SearchRadius = FMath::CeilToInt(G->GetStunRadius()) + 4;
		FIntPoint BestCell = FIntPoint::ZeroValue;
		float BestScore = TNumericLimits<float>::Max();
		bool bFound = false;
		for (int32 DY = -SearchRadius; DY <= SearchRadius; ++DY)
		{
			for (int32 DX = -SearchRadius; DX <= SearchRadius; ++DX)
			{
				const FIntPoint Cell = From + FIntPoint(DX, DY);
				if (FVector::Dist2D(Grid->CellToWorld(Cell), Landing) <= DangerRadius || !Grid->IsCellFreeAsDestination(Cell, &Soldier))
				{
					continue;
				}
				const float Score = CellDistance(Cell, From);
				if (Score < BestScore)
				{
					BestScore = Score;
					BestCell = Cell;
					bFound = true;
				}
			}
		}
		if (bFound && Soldier.MoveToCell(BestCell))
		{
			OnOrderIssued();
			return true;
		}
	}
	return false;
}

bool ASquadBot::TryThrowGrenade(FSoldierBrain& Brain)
{
	ASoldier* S = Brain.Soldier.Get();
	if (!S || S->GetGrenades() <= 0 || Now() < Brain.NextGrenade || S->IsDragging())
	{
		return false;
	}
	const USoldierData* Data = S->GetSoldierData();
	const float StunRadius = Data->GrenadeStunRadius;
	// GDD §7: a thrower on the move steps back up to a diagonal cell before throwing, so leave room for that.
	const float MaxRange = Data->GrenadeMaxThrowRange - (S->IsMoving() ? UE_SQRT_2 : 0.f);

	// The soldier's best shot at anything it knows about: grenades are for when shooting isn't working.
	float BestShot = 0.f;
	for (const FEnemyMemory& Mem : Memory)
	{
		if (Mem.bKnown && Mem.Soldier.IsValid() && !Mem.Soldier->IsDead())
		{
			const FHitChanceInfo Info = S->GetHitChanceAgainst(Mem.Soldier.Get());
			if (Info.CanFire())
			{
				BestShot = FMath::Max(BestShot, Info.FinalChance);
			}
		}
	}

	struct FCandidate
	{
		FIntPoint Cell;
		float Score;
	};
	TArray<FCandidate> Candidates;
	const float T = Now();
	for (const FEnemyMemory& Mem : Memory)
	{
		const ASoldier* E = Mem.Soldier.Get();
		if (!Mem.bKnown || !E || E->IsDead() || E->IsDowned() || T - Mem.LastSeenTime > MaxGrenadeIntelAge)
		{
			continue;
		}
		const FIntPoint Cell = Mem.LastKnownCell;
		const float Distance = CellDistance(Cell, S->GetCurrentCell());
		if (Distance > MaxRange || Distance < StunRadius + 1.5f)
		{
			continue; // Out of reach, or close enough to stun the thrower.
		}
		const FVector Landing = Grid->CellToWorld(Cell);
		if (IsFriendlyNear(Landing, StunRadius + FriendlySafetyMargin))
		{
			continue;
		}
		// Don't double up on a spot another grenade is already heading for.
		bool bAlreadyTargeted = false;
		for (TActorIterator<AGrenade> It(GetWorld()); It; ++It)
		{
			if (It->IsLive() && FVector::Dist2D(It->GetLandingLocation(), Landing) < StunRadius * UUPerMetre)
			{
				bAlreadyTargeted = true;
				break;
			}
		}
		if (bAlreadyTargeted)
		{
			continue;
		}

		const int32 Cluster = CountKnownEnemiesNear(Landing, StunRadius);
		const FHitChanceInfo Shot = S->GetHitChanceAgainst(E);
		const float ShotChance = Shot.CanFire() ? Shot.FinalChance : 0.f;
		const bool bWorthIt = Cluster >= GrenadeClusterCount || (ShotChance < GrenadeHitChanceThreshold && BestShot < GrenadeHitChanceThreshold);
		if (bWorthIt)
		{
			Candidates.Add({ Cell, Cluster * 10.f - ShotChance - Distance * 0.1f });
		}
	}

	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score > B.Score; });
	for (const FCandidate& C : Candidates)
	{
		if (S->ThrowGrenadeAt(C.Cell))
		{
			Brain.NextGrenade = Now() + GrenadeCooldown;
			OnOrderIssued();
			return true;
		}
	}
	if (Candidates.Num() > 0)
	{
		Brain.NextGrenade = Now() + 2.f; // Every arc blocked: don't retry every decision.
	}
	return false;
}

bool ASquadBot::TrySeekCover(FSoldierBrain& Brain, FIntPoint ThreatCell)
{
	ASoldier* S = Brain.Soldier.Get();
	if (!S || S->IsShocked())
	{
		return false;
	}
	if (StandUpToMove(*S))
	{
		return true; // Up first (prone soldiers crawl); it runs for cover on the next decision.
	}
	Brain.NextReposition = Now() + 4.f;
	const FIntPoint From = S->GetCurrentCell();

	FIntPoint BestCell = FIntPoint::ZeroValue;
	float BestScore = 0.f;
	bool bFound = false;
	for (int32 DY = -CoverSearchRadius; DY <= CoverSearchRadius; ++DY)
	{
		for (int32 DX = -CoverSearchRadius; DX <= CoverSearchRadius; ++DX)
		{
			const FIntPoint Cell = From + FIntPoint(DX, DY);
			if (Cell == From || !Grid->IsCellFreeAsDestination(Cell, S) || IsSlotTaken(Cell, S))
			{
				continue;
			}
			const float Reduction = Grid->GetCoverAgainst(Cell, ThreatCell).Reduction;
			if (Reduction <= 0.f)
			{
				continue;
			}
			// Prefer good cover close by; stay inside the assigned VP if possible.
			float Score = Reduction - CellDistance(Cell, From) * 4.f;
			if (Brain.AssignedVP.IsValid() && Brain.AssignedVP->ContainsCell(Cell))
			{
				Score += 15.f;
			}
			if (Score > BestScore)
			{
				BestScore = Score;
				BestCell = Cell;
				bFound = true;
			}
		}
	}
	if (bFound && S->MoveToCell(BestCell))
	{
		OnOrderIssued();
		if (Brain.AssignedVP.IsValid() && Brain.AssignedVP->ContainsCell(BestCell))
		{
			Brain.Slot = BestCell;
			Brain.bHasSlot = true;
			Brain.bBounding = false;
		}
		else
		{
			// Out of the VP: treat the cover as a bound stop, so it waits there before moving on.
			Brain.bBounding = true;
			Brain.BoundCell = BestCell;
		}
		Brain.bFacedAfterArriving = false;
		return true;
	}
	return false;
}

void ASquadBot::HoldObjective(FSoldierBrain& Brain)
{
	ASoldier* S = Brain.Soldier.Get();
	AVictoryPoint* VP = Brain.AssignedVP.Get();
	if (!S || !VP)
	{
		return;
	}

	const float T = Now();
	const bool bFlanking = T < Brain.FlankUntil;
	if (!Brain.bHasSlot || (!bFlanking && !VP->ContainsCell(Brain.Slot)) || IsSlotTaken(Brain.Slot, S))
	{
		Brain.FlankUntil = 0.f;
		if (!PickSlot(Brain))
		{
			return;
		}
	}

	// Arrived at the end of a bound: wait in that cover a moment (longer in contact) before the next one.
	if (Brain.bBounding && S->GetCurrentCell() == Brain.BoundCell)
	{
		Brain.bBounding = false;
		Brain.NextBound = T + RandRange(IsCautious(*S) ? BoundPauseContact : BoundPauseQuiet);
		return;
	}
	Brain.bBounding = false;

	if (S->GetCurrentCell() == Brain.Slot)
	{
		if (!Brain.bFacedAfterArriving)
		{
			Brain.bFacedAfterArriving = true;
			// Soldiers lean on adjacent cover by themselves after a move; only face the enemy's side in the open.
			FIntPoint Side;
			if (!Grid->FindCoverToLeanOn(Brain.Slot, S->GetActorRotation().Yaw, Side) && S->FaceCell(GetThreatCell(Brain.Slot)))
			{
				OnOrderIssued();
			}
		}
		return;
	}

	if (T < Brain.NextHoldRetry || T < Brain.NextBound)
	{
		return; // Held short by the squad cohesion limit, or pausing in cover between bounds.
	}
	if (ShouldHoldForOverwatch(*S))
	{
		return; // Enough squad-mates on the move: stay put and cover them.
	}
	// Long way to go: move from cover to cover rather than straight across the open.
	FIntPoint Goal = Brain.Slot;
	FIntPoint BoundCell = FIntPoint::ZeroValue;
	const bool bBound = !bFlanking && PickBoundCell(Brain, Brain.Slot, BoundCell);
	if (bBound)
	{
		Goal = BoundCell;
	}
	if (IsRouteWatched(*S, Goal))
	{
		Brain.NextBound = T + RandRange(BoundPauseContact) * 0.5f; // Wait for squad-mates to pin that enemy down.
		return;
	}
	if (StandUpToMove(*S))
	{
		return; // Prone soldiers crawl: get up first, move on the next decision.
	}
	Brain.bFacedAfterArriving = false;
	if (S->MoveToCell(Goal))
	{
		OnOrderIssued();
		if (bBound)
		{
			Brain.bBounding = true;
			Brain.BoundCell = S->GetDestinationCell();
		}
		else if (S->GetDestinationCell() != Brain.Slot)
		{
			Brain.NextHoldRetry = T + 3.f; // Stopped at the cohesion limit (GDD §4.3).
		}
	}
	else if (!bBound)
	{
		Brain.bHasSlot = false; // Unreachable or just taken: pick another next time.
		Brain.NextHoldRetry = T + 1.f;
	}
	else
	{
		Brain.NextBound = T + 1.f; // That bound didn't work out: try again (or go straight) shortly.
	}
}

bool ASquadBot::IsRouteWatched(const ASoldier& Soldier, FIntPoint Goal) const
{
	const float T = Now();
	const FIntPoint From = Soldier.GetCurrentCell();
	const FIntPoint Mid = (From + Goal) / 2;
	const float Eye = Grid->GetCoverData()->StandingSightHeight;
	for (const FEnemyMemory& Mem : Memory)
	{
		const ASoldier* E = Mem.Soldier.Get();
		// Only enemies it is watching right now, and only ones free to shoot: one being fired at (the player sees
		// the tracers going in) or shaken is pinned, which is when the squad moves.
		if (!Mem.bKnown || !E || E->IsDowned() || T - Mem.LastSeenTime > 1.f || E->IsUnderFire()
			|| E->GetStressTier() >= EStressTier::Major)
		{
			continue;
		}
		const UWeaponData* Weapon = E->GetWeaponData();
		const float Range = Weapon ? Weapon->LongRangeMax : 40.f;
		const FIntPoint EnemyCell = Mem.LastKnownCell;
		const float EnemyEye = Grid->GetCoverData()->GetSightHeight(E->GetEffectiveStance());
		for (const FIntPoint& Cell : { Mid, Goal })
		{
			if (CellDistance(Cell, EnemyCell) <= Range && Grid->GetCoverAgainst(Cell, EnemyCell).Reduction <= 0.f
				&& Grid->HasLineOfSight(EnemyCell, Cell, EnemyEye, Eye))
			{
				return true;
			}
		}
	}
	return false;
}

bool ASquadBot::PickBoundCell(const FSoldierBrain& Brain, FIntPoint Goal, FIntPoint& OutCell) const
{
	const ASoldier* S = Brain.Soldier.Get();
	if (!S)
	{
		return false;
	}
	const bool bCautious = IsCautious(*S);
	const float MaxLeg = bCautious ? BoundLength.Y : BoundLength.X;
	const FIntPoint From = S->GetCurrentCell();
	const float ToGoal = CellDistance(From, Goal);
	// Nearly there: go straight in. Expecting the enemy, even the last stretch goes from cover to cover.
	if (ToGoal <= (bCautious ? 3.f : MaxLeg + 2.f))
	{
		return false;
	}
	const FIntPoint ThreatCell = GetThreatCell(From);

	struct FLeg
	{
		FIntPoint Cell;
		float Score;
		float Distance;
	};
	TArray<FLeg> Legs;
	const int32 R = FMath::CeilToInt(MaxLeg);
	for (int32 DY = -R; DY <= R; ++DY)
	{
		for (int32 DX = -R; DX <= R; ++DX)
		{
			const FIntPoint Cell = From + FIntPoint(DX, DY);
			const float D = CellDistance(Cell, From);
			if (D < 2.f || D > MaxLeg)
			{
				continue;
			}
			const float Progress = ToGoal - CellDistance(Cell, Goal);
			if (Progress < D * 0.5f)
			{
				continue; // Mostly sideways or backwards.
			}
			const float Reduction = Grid->GetCoverAgainst(Cell, ThreatCell).Reduction;
			if (Reduction <= 0.f || !Grid->IsCellFreeAsDestination(Cell, S) || IsSlotTaken(Cell, S) || !S->IsCohesiveDestination(Cell))
			{
				continue;
			}
			float Score = Reduction + Progress * 4.f;
			// Don't bunch up behind the same wall (one grenade).
			for (const FSoldierBrain& Other : Brains)
			{
				const ASoldier* Mate = Other.Soldier.Get();
				if (Mate && Mate != S && !Mate->IsDead() && CellDistance(Cell, Mate->GetDestinationCell()) < 2.5f)
				{
					Score -= 30.f;
				}
			}
			Legs.Add({ Cell, Score, D });
		}
	}
	Legs.Sort([](const FLeg& A, const FLeg& B) { return A.Score > B.Score; });

	// The best few, checking the walk there isn't a long detour round a building.
	TArray<FIntPoint> Path;
	for (int32 i = 0; i < FMath::Min(Legs.Num(), 4); ++i)
	{
		Path.Reset();
		if (Grid->FindPath(From, Legs[i].Cell, S, Path) && Path.Num() <= FMath::CeilToInt(Legs[i].Distance * 1.5f) + 2)
		{
			OutCell = Legs[i].Cell;
			return true;
		}
	}
	return false;
}

bool ASquadBot::PickSlot(FSoldierBrain& Brain)
{
	ASoldier* S = Brain.Soldier.Get();
	AVictoryPoint* VP = Brain.AssignedVP.Get();
	if (!S || !VP)
	{
		return false;
	}
	const FIntPoint Centre = VP->GetMinCell() + FIntPoint(VP->SizeX / 2, VP->SizeY / 2);
	const FIntPoint ThreatCell = GetThreatCell(Centre);

	// GDD §4.3: judge squad-mates at the slots they're heading for, so the squad ends up within the cohesion limit.
	TMap<const ASoldier*, FIntPoint> Plan;
	for (const FSoldierBrain& Other : Brains)
	{
		if (Other.bHasSlot && Other.Soldier.IsValid() && Other.Soldier.Get() != S)
		{
			Plan.Add(Other.Soldier.Get(), Other.Slot);
		}
	}

	float BestScore = -TNumericLimits<float>::Max();
	bool bFound = false;
	for (const FIntPoint& Cell : VP->GetCells())
	{
		if (!Grid->IsCellFreeAsDestination(Cell, S) || IsSlotTaken(Cell, S))
		{
			continue;
		}
		float Score = ScoreHoldCell(Cell, *S, ThreatCell) - CellDistance(Cell, S->GetCurrentCell()) * 0.5f;
		if (!S->IsCohesiveDestination(Cell, &Plan))
		{
			Score -= 1000.f; // Only if nothing in the VP fits (MoveToCell then stops the soldier at the limit).
		}
		if (Score > BestScore)
		{
			BestScore = Score;
			Brain.Slot = Cell;
			bFound = true;
		}
	}
	Brain.bHasSlot = bFound;
	Brain.bFacedAfterArriving = false;
	return bFound;
}

float ASquadBot::ScoreHoldCell(FIntPoint Cell, const ASoldier& Soldier, FIntPoint ThreatCell) const
{
	float Score = Grid->GetCoverAgainst(Cell, ThreatCell).Reduction;

	// A firing position, not just a hiding place: prefer cells that see where the enemy is (within rifle range).
	const UWeaponData* Weapon = Soldier.GetWeaponData();
	if (Weapon && CellDistance(Cell, ThreatCell) <= Weapon->LongRangeMax)
	{
		const float Eye = Grid->GetCoverData()->StandingSightHeight;
		if (Grid->HasLineOfSight(Cell, ThreatCell, Eye, Eye))
		{
			Score += FieldOfFireBonus;
		}
	}

	// Spread out so one grenade can't catch two soldiers.
	for (const FSoldierBrain& Other : Brains)
	{
		const ASoldier* Mate = Other.Soldier.Get();
		if (!Mate || Mate == &Soldier || Mate->IsDead())
		{
			continue;
		}
		const FIntPoint MateCell = Other.bHasSlot ? Other.Slot : Mate->GetCurrentCell();
		const float D = CellDistance(Cell, MateCell);
		if (D < 2.5f)
		{
			Score -= 25.f;
		}
		else if (D < 4.5f)
		{
			Score -= 10.f;
		}
	}
	return Score;
}

FIntPoint ASquadBot::GetThreatCell(FIntPoint From) const
{
	// Only what the bot has noticed: otherwise it assumes the enemy comes from their deployment zone.
	FIntPoint Best = EnemyHomeCell;
	float BestDistance = TNumericLimits<float>::Max();
	for (const FEnemyMemory& Mem : Memory)
	{
		if (!Mem.bKnown || !Mem.Soldier.IsValid() || Mem.Soldier->IsDead())
		{
			continue;
		}
		const float D = CellDistance(Mem.LastKnownCell, From);
		if (D < BestDistance)
		{
			BestDistance = D;
			Best = Mem.LastKnownCell;
		}
	}
	return Best;
}

bool ASquadBot::IsSlotTaken(FIntPoint Cell, const ASoldier* Except) const
{
	for (const FSoldierBrain& Brain : Brains)
	{
		if (Brain.Soldier.Get() != Except && Brain.bHasSlot && Brain.Slot == Cell && Brain.Soldier.IsValid() && !Brain.Soldier->IsDead())
		{
			return true;
		}
	}
	// Other friendly squads: a cell one of their soldiers stands on, is heading for or has been given.
	for (TActorIterator<ASquadBot> It(GetWorld()); It; ++It)
	{
		if (*It == this || It->Team != Team)
		{
			continue;
		}
		for (const FSoldierBrain& Brain : It->Brains)
		{
			if (Brain.bHasSlot && Brain.Slot == Cell && Brain.Soldier.IsValid() && !Brain.Soldier->IsDead())
			{
				return true;
			}
		}
	}
	for (const TWeakObjectPtr<ASoldier>& Weak : Grid->GetSoldiers())
	{
		const ASoldier* S = Weak.Get();
		if (S && S != Except && S->Team == Team && S->SquadId != SquadId && !S->IsDead()
			&& (S->GetCurrentCell() == Cell || S->GetDestinationCell() == Cell))
		{
			return true;
		}
	}
	return false;
}

int32 ASquadBot::CountOtherSquadsAssigned(const AVictoryPoint* VP) const
{
	int32 Count = 0;
	for (TActorIterator<ASquadBot> It(GetWorld()); It; ++It)
	{
		if (*It == this || It->Team != Team)
		{
			continue;
		}
		for (const FSoldierBrain& Brain : It->Brains)
		{
			if (Brain.AssignedVP.Get() == VP && Brain.Soldier.IsValid() && !Brain.Soldier->IsDead())
			{
				++Count;
			}
		}
	}
	return Count;
}

int32 ASquadBot::CountKnownEnemiesNear(FVector Location, float RadiusMetres) const
{
	int32 Count = 0;
	for (const FEnemyMemory& Mem : Memory)
	{
		const ASoldier* E = Mem.Soldier.Get();
		if (Mem.bKnown && E && !E->IsDead() && !E->IsDowned()
			&& FVector::Dist2D(Grid->CellToWorld(Mem.LastKnownCell), Location) <= RadiusMetres * UUPerMetre)
		{
			++Count;
		}
	}
	return Count;
}

bool ASquadBot::IsFriendlyNear(FVector Location, float RadiusMetres) const
{
	// Every friendly squad, not just this one: a grenade doesn't care whose squad it lands among.
	for (const TWeakObjectPtr<ASoldier>& Weak : Grid->GetSoldiers())
	{
		const ASoldier* Mate = Weak.Get();
		if (!Mate || Mate->Team != Team || Mate->IsDead())
		{
			continue;
		}
		// Where it is and where it's going: it may walk into the blast.
		if (FVector::Dist2D(Mate->GetActorLocation(), Location) <= RadiusMetres * UUPerMetre
			|| FVector::Dist2D(Grid->CellToWorld(Mate->GetDestinationCell()), Location) <= RadiusMetres * UUPerMetre)
		{
			return true;
		}
	}
	return false;
}
