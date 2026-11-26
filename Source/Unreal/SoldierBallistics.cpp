// Experimental projectile combat (GDD §16, squad.Ballistics 1): hit shapes, bracing on cover, spread, firing a
// round and applying where it lands. The round itself flies in UBallisticsSubsystem.

#include "Soldier.h"

#include "Ballistics.h"
#include "BallisticsData.h"
#include "Engine/Engine.h"
#include "GridManager.h"
#include "HAL/IConsoleManager.h"
#include "SoldierData.h"
#include "SquadMatchStats.h"
#include "WeaponData.h"

namespace
{
	constexpr float CmPerMetre = 100.f;

	// Debug: log every soldier's projectile-model estimate against every enemy it can see, on this world.
	FAutoConsoleCommandWithWorldAndArgs CmdBallisticsProbe(
		TEXT("squad.BallisticsProbe"),
		TEXT("squad.BallisticsProbe: log each soldier's shot estimate (chance, exposure, braced) against every visible enemy (GDD §16, debug)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			const AGridManager* Grid = AGridManager::Find(World);
			if (!Grid)
			{
				return;
			}
			const TCHAR* Tag = World->GetNetMode() == NM_DedicatedServer ? TEXT("SERVER") : World->GetNetMode() == NM_Client ? TEXT("CLIENT") : TEXT("LOCAL");
			for (const TWeakObjectPtr<ASoldier>& WeakA : Grid->GetSoldiers())
			{
				const ASoldier* A = WeakA.Get();
				if (!A || A->IsDead())
				{
					continue;
				}
				for (const TWeakObjectPtr<ASoldier>& WeakB : Grid->GetSoldiers())
				{
					const ASoldier* B = WeakB.Get();
					if (!B || B->IsDead() || B->Team == A->Team)
					{
						continue;
					}
					const FHitChanceInfo Info = A->GetHitChanceAgainst(B);
					if (!Info.CanFire())
					{
						continue;
					}
					UE_LOG(LogTemp, Log, TEXT("BallisticsProbe %s %s%d (%d,%d)%s -> %s%d (%d,%d)%s %.1fm: %s aim=%s spread=%.1fmrad"), Tag,
						A->Team == ESquadTeam::Allies ? TEXT("Al") : TEXT("Ax"), A->GetSquadNumber(), A->GetCurrentCell().X, A->GetCurrentCell().Y,
						A->IsBraced() ? TEXT(" braced") : TEXT(""),
						B->Team == ESquadTeam::Allies ? TEXT("Al") : TEXT("Ax"), B->GetSquadNumber(), B->GetCurrentCell().X, B->GetCurrentCell().Y,
						B->IsBraced() ? TEXT(" braced") : TEXT(""), Info.DistanceMetres, *Info.Describe(), GetHitPartName(Info.AimPart), Info.SpreadMrad);
				}
			}
		}));

	/** One hit sphere in the soldier's frame: Forward (cm), Up (cm above the ground), radius. */
	struct FPartPose
	{
		EHitPart Part;
		float Forward;
		float Up;
		float Radius;
	};

	// Sizes roughly match the low-poly soldier (1.8 m tall). Spheres overlap a little so no gap lets a round through.
	constexpr FPartPose StandingPose[] = {
		{ EHitPart::Head,   0.f, 160.f, 12.f },
		{ EHitPart::Chest,  0.f, 132.f, 18.f },
		{ EHitPart::Belly,  0.f, 108.f, 17.f },
		{ EHitPart::Hips,   0.f,  86.f, 17.f },
		{ EHitPart::Thighs, 0.f,  55.f, 15.f },
		{ EHitPart::Shins,  0.f,  22.f, 13.f },
	};
	constexpr FPartPose CrouchedPose[] = {
		{ EHitPart::Head,  10.f, 112.f, 12.f },
		{ EHitPart::Chest,  8.f,  88.f, 18.f },
		{ EHitPart::Belly,  4.f,  68.f, 17.f },
		{ EHitPart::Hips,  -6.f,  48.f, 18.f },
		{ EHitPart::Thighs, 6.f,  30.f, 16.f },
		{ EHitPart::Shins, -4.f,  12.f, 13.f },
	};
	constexpr FPartPose PronePose[] = {
		{ EHitPart::Head,   75.f, 18.f, 12.f },
		{ EHitPart::Chest,  45.f, 16.f, 18.f },
		{ EHitPart::Belly,  15.f, 15.f, 17.f },
		{ EHitPart::Hips,  -15.f, 15.f, 17.f },
		{ EHitPart::Thighs,-50.f, 12.f, 14.f },
		{ EHitPart::Shins, -85.f, 10.f, 12.f },
	};

	/** How far each part moves with a lean out past a wall's edge (head most, legs not at all). */
	float PeekWeight(EHitPart Part)
	{
		switch (Part)
		{
		case EHitPart::Head:  return 1.f;
		case EHitPart::Chest: return 0.6f;
		case EHitPart::Belly: return 0.3f;
		case EHitPart::Hips:  return 0.f;
		default:              return 0.f;
		}
	}

	/** An orthonormal frame around Forward for spreading rounds. */
	void SpreadBasis(const FVector& Forward, FVector& OutRight, FVector& OutUp)
	{
		OutRight = FVector::CrossProduct(Forward, FVector::UpVector);
		if (!OutRight.Normalize())
		{
			OutRight = FVector::RightVector;
		}
		OutUp = FVector::CrossProduct(OutRight, Forward).GetSafeNormal();
	}
}

bool ASoldier::IsBraced(float* OutCoverTop, FVector* OutTowardCover) const
{
	if (!Grid || bDead || bStepping || IsDragging() || IsBeingDragged() || GetEffectiveStance() == ESoldierStance::Prone
		|| !UBallisticsSubsystem::IsEnabled(this))
	{
		return false;
	}
	const float Yaw = GetActorRotation().Yaw;
	FIntPoint Side;
	if (!Grid->FindCoverToLeanOn(CurrentCell, Yaw, Side) || !Grid->IsLeaningToward(Side, Yaw))
	{
		return false;
	}
	const FGridCoverCell* Cover = Grid->GetCoverCell(CurrentCell + Side);
	if (!Cover || Cover->bBlocksLineOfSight)
	{
		return false;
	}
	const UCoverData* CoverData = Grid->GetCoverData();
	float Top;
	switch (Cover->Type)
	{
	case ECoverType::Soft:    Top = CoverData->HayBaleHeight; break;
	case ECoverType::LowWall: Top = CoverData->LowWallHeight; break;
	case ECoverType::Window:  Top = CoverData->WindowSillHeight; break;
	default:                  return false; // Full walls, corners and trees: lean out instead (GetHitShapes).
	}
	if (OutCoverTop)
	{
		*OutCoverTop = Top;
	}
	if (OutTowardCover)
	{
		*OutTowardCover = FVector(Side.X, Side.Y, 0.f).GetSafeNormal();
	}
	return true;
}

ESoldierStance ASoldier::GetPostureStance() const
{
	return IsBraced() ? ESoldierStance::Crouched : GetEffectiveStance();
}

void ASoldier::GetHitShapes(FBallisticShapes& OutShapes) const
{
	OutShapes.Reset();
	if (bDead)
	{
		return;
	}
	const UBallisticsData* Data = UBallisticsSubsystem::GetData();
	const FVector Base = GetActorLocation();
	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();

	float CoverTop = 0.f;
	FVector Toward = Forward;
	if (IsBraced(&CoverTop, &Toward))
	{
		// Crouched behind low cover with the rifle on top: head and shoulders above it, the rest below. Reloading,
		// the soldier ducks right down behind it.
		const float Drop = bReloading ? Data->DuckDrop : 0.f;
		const float Lean = Data->BracedLeanForward;
		const float HeadZ = CoverTop + Data->BracedHeadAboveCover;
		const FPartPose Braced[] = {
			{ EHitPart::Head,   Lean,         HeadZ - Drop,                         12.f },
			{ EHitPart::Chest,  Lean - 5.f,   HeadZ - 26.f - Drop,                  18.f },
			{ EHitPart::Belly,  Lean - 10.f,  FMath::Max(HeadZ - 50.f - Drop, 30.f), 17.f },
			{ EHitPart::Hips,   Lean - 18.f,  FMath::Max(HeadZ - 72.f, 28.f),        18.f },
			{ EHitPart::Thighs, 2.f,          26.f,                                 16.f },
			{ EHitPart::Shins,  -6.f,         12.f,                                 13.f },
		};
		for (const FPartPose& P : Braced)
		{
			OutShapes.Add({ Base + Toward * P.Forward + FVector(0.f, 0.f, P.Up), P.Radius, P.Part });
		}
		return;
	}

	const FPartPose* Pose = StandingPose;
	switch (GetEffectiveStance())
	{
	case ESoldierStance::Crouched: Pose = CrouchedPose; break;
	case ESoldierStance::Prone:    Pose = PronePose; break;
	default: break;
	}

	// Leaning out past a wall's edge (GDD §9.2): the upper body moves out past the wall's end.
	FVector PeekShift = FVector::ZeroVector;
	FVector PeekForward = FVector::ZeroVector;
	FWallPeek Peek;
	if (GetEffectiveStance() != ESoldierStance::Prone && GetActivePeek(Peek))
	{
		PeekShift = FVector(Peek.Edge.X, Peek.Edge.Y, 0.f).GetSafeNormal() * Data->PeekSideways;
		PeekForward = FVector(Peek.Toward.X, Peek.Toward.Y, 0.f).GetSafeNormal() * (Peek.bAlongWall ? 10.f : 20.f);
	}
	for (int32 i = 0; i < 6; ++i)
	{
		const FPartPose& P = Pose[i];
		const float W = PeekWeight(P.Part);
		OutShapes.Add({ Base + Forward * P.Forward + (PeekShift + PeekForward) * W + FVector(0.f, 0.f, P.Up), P.Radius, P.Part });
	}
}

FVector ASoldier::GetBallisticMuzzle(const FVector& Toward) const
{
	const UBallisticsData* Data = UBallisticsSubsystem::GetData();
	const FVector Base = GetActorLocation();
	FVector Dir = (Toward - Base).GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		Dir = GetActorForwardVector().GetSafeNormal2D();
	}

	float CoverTop = 0.f;
	FVector TowardCover;
	if (IsBraced(&CoverTop, &TowardCover))
	{
		// Resting on the cover: the muzzle sits just over its top, in front of the body.
		return Base + TowardCover * (Data->BracedLeanForward + 20.f) + FVector(0.f, 0.f, CoverTop + Data->BracedMuzzleAboveCover);
	}

	FBallisticShapes Shapes;
	GetHitShapes(Shapes);
	if (Shapes.Num() > 0 && GetEffectiveStance() != ESoldierStance::Prone)
	{
		// Standing / crouched (leaning out or not): shoulder height, a little in front of the head.
		const FVector Head = Shapes[0].Centre;
		return FVector(Head.X, Head.Y, Head.Z - 18.f) + Dir * 40.f;
	}
	return Base + Dir * 95.f + FVector(0.f, 0.f, 22.f); // Prone: rifle out in front of the head.
}

float ASoldier::GetBallisticSpreadMrad() const
{
	const UBallisticsData* Data = UBallisticsSubsystem::GetData();
	float Spread = GetWeaponData()->SpreadMrad;
	if (IsBraced())
	{
		Spread *= Data->BracedSpreadMultiplier;
	}
	else if (GetEffectiveStance() == ESoldierStance::Crouched)
	{
		Spread *= Data->CrouchedSpreadMultiplier;
	}
	else if (GetEffectiveStance() == ESoldierStance::Prone)
	{
		Spread *= Data->ProneSpreadMultiplier;
	}
	if (bInjured)
	{
		Spread *= Data->InjuredSpreadMultiplier;
	}
	const float Accuracy = FMath::Max(GetStressEffects().AccuracyMultiplier, 0.05f);
	Spread *= FMath::Pow(1.f / Accuracy, Data->StressSpreadExponent);
	return Spread + RecoilMrad;
}

void ASoldier::FillBallisticChance(const ASoldier& Target, FHitChanceInfo& Info) const
{
	const UBallisticsSubsystem* Sim = UBallisticsSubsystem::Get(this);
	if (!Sim)
	{
		return;
	}
	const UBallisticsData* Data = UBallisticsSubsystem::GetData();
	Info.bBallistic = true;
	Info.bBraced = IsBraced();
	Info.SpreadMrad = GetBallisticSpreadMrad();

	FBallisticShapes Shapes;
	Target.GetHitShapes(Shapes);
	if (Shapes.Num() == 0)
	{
		Info.FinalChance = 0.f;
		return;
	}
	const FVector Muzzle = GetBallisticMuzzle(Target.GetActorLocation());

	// Exposure: five points on each part (centre and four round its edge, square to the line of fire), weighted by
	// the part's area. Visible points are kept to choose the aim point below.
	struct FSeenPoint { FVector P; float W; };
	TArray<FSeenPoint, TInlineAllocator<30>> SeenPoints;
	TArray<FVector, TInlineAllocator<6>> Candidates;
	float VisibleArea = 0.f;
	float TotalArea = 0.f;
	float BestPartArea = 0.f;
	int32 BestPart = 1; // Chest when nothing shows.
	for (int32 i = 0; i < Shapes.Num(); ++i)
	{
		const FBallisticShape& Shape = Shapes[i];
		const FVector Line = (Shape.Centre - Muzzle).GetSafeNormal();
		FVector Right, Up;
		SpreadBasis(Line, Right, Up);
		const float Off = Shape.Radius * 0.7f;
		const FVector Points[5] = { Shape.Centre, Shape.Centre + Right * Off, Shape.Centre - Right * Off, Shape.Centre + Up * Off, Shape.Centre - Up * Off };
		const float Area = Shape.Radius * Shape.Radius;
		float Seen = 0.f;
		FVector PartSum = FVector::ZeroVector;
		for (const FVector& P : Points)
		{
			const FBallisticTrace Hit = Sim->Trace(Muzzle, P + Line * 2.f, this);
			if (Hit.Kind == EBallisticHit::Soldier && Hit.Soldier == &Target)
			{
				Seen += Hit.PassWeight;
				PartSum += P * Hit.PassWeight;
				SeenPoints.Add({ P, Area / 5.f * Hit.PassWeight });
			}
		}
		if (Seen > 0.f)
		{
			Candidates.Add(PartSum / Seen); // Centre of this part's visible area.
		}
		const float PartArea = Area * Seen / 5.f;
		VisibleArea += PartArea;
		TotalArea += Area;
		if (PartArea > BestPartArea + KINDA_SMALL_NUMBER)
		{
			BestPartArea = PartArea;
			BestPart = i;
		}
	}
	Info.ExposurePercent = TotalArea > 0.f ? 100.f * VisibleArea / TotalArea : 0.f;
	// Aim where the most visible area lies within the shooter's spread at that range (a Gaussian-weighted sum over
	// the visible points), so it aims at a head over a wall, or between head and shoulders when both show, but
	// not at the empty gap between separate visible pieces.
	Info.AimPoint = Shapes[BestPart].Centre;
	Info.AimPart = Shapes[BestPart].Part; // The most exposed part (readout only).
	{
		const float S = FMath::Max(Info.SpreadMrad / 1000.f * FVector::Dist(Muzzle, Target.GetActorLocation()), 5.f);
		float BestScore = -1.f;
		for (const FVector& C : Candidates)
		{
			float Score = 0.f;
			for (const FSeenPoint& Pt : SeenPoints)
			{
				Score += Pt.W * FMath::Exp(-FVector::DistSquared(Pt.P, C) / (2.f * S * S));
			}
			if (Score > BestScore)
			{
				BestScore = Score;
				Info.AimPoint = C;
			}
		}
	}

	// Chance: fire a fixed pattern of sample rounds spread like real ones (2D normal, sunflower layout) and count
	// the share that reach the target. Deterministic, so the HUD number doesn't flicker.
	const FVector Forward = (Info.AimPoint - Muzzle).GetSafeNormal();
	FVector Right, Up;
	SpreadBasis(Forward, Right, Up);
	const float Sigma = Info.SpreadMrad / 1000.f;
	const float Reach = FVector::Dist(Muzzle, Info.AimPoint) + 300.f;
	const int32 N = Data->EstimateSamples;
	float Hits = 0.f;
	for (int32 i = 0; i < N; ++i)
	{
		const float U = (i + 0.5f) / N;
		const float R = Sigma * FMath::Sqrt(-2.f * FMath::Loge(1.f - U));
		const float Theta = i * 2.39996323f; // Golden angle.
		const FVector Dir = (Forward + Right * (R * FMath::Cos(Theta)) + Up * (R * FMath::Sin(Theta))).GetSafeNormal();
		const FBallisticTrace Hit = Sim->Trace(Muzzle, Muzzle + Dir * Reach, this);
		if (Hit.Kind == EBallisticHit::Soldier && Hit.Soldier == &Target)
		{
			Hits += Hit.PassWeight;
		}
	}
	Info.FinalChance = 100.f * Hits / N;
	Info.BaseChance = Info.FinalChance;
}

void ASoldier::FireBallisticRound(ASoldier* Target, const FHitChanceInfo& Chance)
{
	UBallisticsSubsystem* Sim = UBallisticsSubsystem::Get(this);
	if (!Sim || !Target)
	{
		return;
	}
	const UBallisticsData* Data = UBallisticsSubsystem::GetData();
	const FVector Aim = Chance.bBallistic ? Chance.AimPoint : Target->GetActorLocation() + FVector(0.f, 0.f, 110.f);
	const FVector Muzzle = GetBallisticMuzzle(Aim);
	const FVector Forward = (Aim - Muzzle).GetSafeNormal();
	FVector Right, Up;
	SpreadBasis(Forward, Right, Up);

	// GDD §16: a random angular error, normal in both axes (server RNG only). Spread is read per round so recoil
	// from the previous round of a burst counts.
	const float Sigma = GetBallisticSpreadMrad() / 1000.f;
	const float R = Sigma * FMath::Sqrt(-2.f * FMath::Loge(FMath::Max(Rng.FRand(), 1e-6f)));
	const float Theta = Rng.FRand() * 2.f * PI;
	const FVector Dir = (Forward + Right * (R * FMath::Cos(Theta)) + Up * (R * FMath::Sin(Theta))).GetSafeNormal();
	RecoilMrad = FMath::Min(RecoilMrad + Data->RecoilPerShotMrad, Data->MaxRecoilMrad);

	Sim->FireRound(this, Target, Muzzle, Dir, Chance);
	ApplyCrossfireStress(Target); // GDD §8.2: firing at an enemy still stresses its nearby squadmates.
}

void ASoldier::OnBallisticRoundResolved(const FBallisticRound& Round, const FBallisticTrace& Hit)
{
	ASoldier* Target = Round.Target.Get();
	ASoldier* Victim = Hit.Kind == EBallisticHit::Soldier ? Hit.Soldier : nullptr;
	FShotTargetSnapshot TargetBefore; // for the match record, which is written after the hit is applied
	if (Target)
	{
		TargetBefore.Stance = Target->GetEffectiveStance();
		TargetBefore.bMoving = Target->IsMoving();
		TargetBefore.bInjured = Target->IsInjured();
	}
	MulticastShot(Round.Muzzle, Hit.Point, Victim != nullptr); // Tracer and gunshot on every machine.

	USquadMatchStats* Stats = USquadMatchStats::Get(this);
	FString Outcome;
	bool bHitTarget = false;
	if (Victim)
	{
		const float DistanceMetres = FVector::Dist2D(GetActorLocation(), Victim->GetActorLocation()) / CmPerMetre;
		FKillCause Cause;
		Cause.Killer = this;
		Cause.Method = EKillMethod::Rifle;
		Cause.Weapon = GetWeaponData();
		Cause.ChancePercent = Round.Chance.FinalChance;
		const bool bWasDead = Victim->IsDead();
		const bool bWasInjured = Victim->IsInjured();
		if (Victim->Team == Team)
		{
			// GDD §16: friendly rounds injure, never kill.
			if (UBallisticsSubsystem::GetData()->bFriendlyFireInjures && !bWasInjured && !bWasDead)
			{
				Victim->ReceiveHit(BIG_NUMBER, Cause); // Beyond the close-range kill distance: injures.
			}
			const FString Effect = (Victim->IsInjured() && !bWasInjured) ? TEXT("injured") : TEXT("none");
			if (Stats)
			{
				Stats->RecordStrayHit(*this, *Victim, true, DistanceMetres, Effect);
			}
			Outcome = TEXT("friendly");
		}
		else
		{
			++HitsLanded;
			Victim->ReceiveHit(DistanceMetres, Cause);
			const FString Effect = bWasDead ? TEXT("none") : Victim->IsDead() ? TEXT("killed")
				: (Victim->IsInjured() && !bWasInjured) ? TEXT("injured") : TEXT("none");
			if (Victim == Target)
			{
				bHitTarget = true;
				Outcome = Effect;
			}
			else
			{
				if (Stats)
				{
					Stats->RecordStrayHit(*this, *Victim, false, DistanceMetres, Effect);
				}
				Outcome = TEXT("stray_enemy");
			}
		}
	}
	else
	{
		Outcome = Hit.Kind == EBallisticHit::Cover ? TEXT("miss_cover") : Hit.Kind == EBallisticHit::Ground ? TEXT("miss_ground") : TEXT("miss_lost");
	}

	if (Target)
	{
		if (Stats)
		{
			Stats->RecordBallisticShot(*this, *Target, Round.Chance, bHitTarget, Hit.Part, Outcome, TargetBefore);
		}
		const FString ShotLog = FString::Printf(TEXT("%s %d -> %s %d: %s, round -> %s%s%s"),
			Team == ESquadTeam::Allies ? TEXT("Allied") : TEXT("Axis"), GetSquadNumber(),
			Target->Team == ESquadTeam::Allies ? TEXT("Allied") : TEXT("Axis"), Target->GetSquadNumber(),
			*Round.Chance.Describe(), *Outcome, Victim ? TEXT(" ") : TEXT(""), Victim ? GetHitPartName(Hit.Part) : TEXT(""));
		UE_LOG(LogTemp, Log, TEXT("%s"), *ShotLog);
#if !UE_BUILD_SHIPPING
		if (GEngine && GetNetMode() != NM_DedicatedServer)
		{
			GEngine->AddOnScreenDebugMessage(-1, 6.f, Victim ? FColor(255, 120, 80) : FColor(200, 200, 200), ShotLog);
		}
#endif
	}
	if (OnShotResolved.IsBound())
	{
		FSoldierShotReport Report;
		Report.Shooter = this;
		Report.Target = Target;
		Report.Victim = Victim;
		Report.bHitTarget = bHitTarget;
		Report.Chance = Round.Chance;
		Report.Part = Hit.Part;
		Report.Outcome = Outcome;
		OnShotResolved.Broadcast(Report);
	}
}

void ASoldier::OnBallisticNearMiss(const ASoldier* Shooter)
{
	if (bDead)
	{
		return;
	}
	// GDD §16.5: a round passing close (or striking the soldier's cover) stresses the soldier (NearMissStress, in
	// place of §8.2's per-shot stress), reduced by the cover the soldier has against the shooter.
	const USoldierData* D = GetSoldierData();
	float CoverFactor = 1.f;
	if (Grid && Shooter)
	{
		const FCoverResult Cover = Grid->GetCoverAgainst(CurrentCell, Shooter->GetCurrentCell(), TOptional<float>(GetActorRotation().Yaw));
		CoverFactor = FMath::Clamp(1.f - Cover.Reduction / 100.f * D->CoverStressReductionScale, 0.f, 1.f);
	}
	AddStress(UBallisticsSubsystem::GetData()->NearMissStress * CoverFactor);
	UnderFireRemaining = UnderFireTime;
}
