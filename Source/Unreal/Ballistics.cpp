#include "Ballistics.h"

#include "BallisticsData.h"
#include "CoverData.h"
#include "Engine/World.h"
#include "GridManager.h"
#include "HAL/IConsoleManager.h"
#include "SquadGameState.h"
#include "WeaponData.h"

namespace
{
	TAutoConsoleVariable<int32> CVarBallistics(
		TEXT("squad.Ballistics"), 1,
		TEXT("Server: 1 (default) = projectile combat (GDD §16): every round is a projectile with spread, cover ")
		TEXT("protects by hiding body parts, soldiers brace on low cover, friendly rounds injure. 0 = the older hit-chance rolls (GDD §5.3), kept for comparison."),
		ECVF_Default);

	/** Closest distance from P to the segment A -> A + Seg. */
	float PointSegmentDistance(const FVector& P, const FVector& A, const FVector& Seg)
	{
		const float LenSq = Seg.SizeSquared();
		const float T = LenSq > KINDA_SMALL_NUMBER ? FMath::Clamp(FVector::DotProduct(P - A, Seg) / LenSq, 0.f, 1.f) : 0.f;
		return FVector::Dist(P, A + Seg * T);
	}

	/** First T in [0, MaxT] where the segment enters the sphere, or -1. */
	float SegmentSphereT(const FVector& A, const FVector& Seg, const FVector& Centre, float Radius, float MaxT)
	{
		const FVector F = A - Centre;
		const float C = F.SizeSquared() - Radius * Radius;
		if (C <= 0.f)
		{
			return 0.f; // Starts inside.
		}
		const float Aq = Seg.SizeSquared();
		const float B = 2.f * FVector::DotProduct(F, Seg);
		const float Disc = B * B - 4.f * Aq * C;
		if (Aq <= KINDA_SMALL_NUMBER || Disc < 0.f)
		{
			return -1.f;
		}
		const float T = (-B - FMath::Sqrt(Disc)) / (2.f * Aq);
		return T >= 0.f && T <= MaxT ? T : -1.f;
	}

	/** T range [OutT0, OutT1] (clipped to [TIn, TOut]) where Lo <= V0 + dV * t <= Hi. False if empty. */
	bool LinearRange(float V0, float DV, float Lo, float Hi, float TIn, float TOut, float& OutT0, float& OutT1)
	{
		if (FMath::Abs(DV) < KINDA_SMALL_NUMBER)
		{
			if (V0 < Lo || V0 > Hi)
			{
				return false;
			}
			OutT0 = TIn;
			OutT1 = TOut;
			return true;
		}
		float A = (Lo - V0) / DV;
		float B = (Hi - V0) / DV;
		if (A > B)
		{
			Swap(A, B);
		}
		OutT0 = FMath::Max(A, TIn);
		OutT1 = FMath::Min(B, TOut);
		return OutT0 <= OutT1;
	}
}

const TCHAR* GetHitPartName(EHitPart Part)
{
	switch (Part)
	{
	case EHitPart::Head:   return TEXT("head");
	case EHitPart::Chest:  return TEXT("chest");
	case EHitPart::Belly:  return TEXT("belly");
	case EHitPart::Hips:   return TEXT("hips");
	case EHitPart::Thighs: return TEXT("thighs");
	default:               return TEXT("shins");
	}
}

UBallisticsSubsystem* UBallisticsSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UBallisticsSubsystem>() : nullptr;
}

bool UBallisticsSubsystem::IsEnabled(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	if (World->GetNetMode() == NM_Client)
	{
		const ASquadGameState* GameState = World->GetGameState<ASquadGameState>();
		return GameState && GameState->bBallistics;
	}
	return CVarBallistics.GetValueOnGameThread() != 0;
}

const UBallisticsData* UBallisticsSubsystem::GetData()
{
	return GetDefault<UBallisticsData>();
}

TStatId UBallisticsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UBallisticsSubsystem, STATGROUP_Tickables);
}

bool UBallisticsSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

AGridManager* UBallisticsSubsystem::GetGrid() const
{
	if (!CachedGrid.IsValid())
	{
		CachedGrid = AGridManager::Find(this);
	}
	return CachedGrid.Get();
}

void UBallisticsSubsystem::FireRound(ASoldier* Shooter, ASoldier* Target, const FVector& Muzzle, const FVector& Direction, const FHitChanceInfo& Chance)
{
	if (!bSeeded)
	{
		Rng.Initialize(FMath::Rand());
		bSeeded = true;
	}
	FBallisticRound& Round = Rounds.AddDefaulted_GetRef();
	Round.Shooter = Shooter;
	Round.Target = Target;
	Round.ShooterTeam = Shooter ? Shooter->Team : ESquadTeam::Allies;
	Round.Muzzle = Muzzle;
	Round.Position = Muzzle;
	Round.Direction = Direction;
	Round.Chance = Chance;
	Round.StressScale = Shooter ? Shooter->GetWeaponData()->StressScale : 1.f;
}

void UBallisticsSubsystem::Tick(float DeltaTime)
{
	// Replicate the switch so clients estimate hit chances (HUD) and pose braced soldiers the same way.
	if (ASquadGameState* GameState = GetWorld()->GetGameState<ASquadGameState>())
	{
		const bool bOn = CVarBallistics.GetValueOnGameThread() != 0;
		if (GameState->bBallistics != bOn)
		{
			GameState->bBallistics = bOn;
			GameState->ForceNetUpdate();
		}
	}
	if (Rounds.Num() == 0)
	{
		return;
	}

	const UBallisticsData* Data = GetData();
	const float Step = Data->MuzzleVelocity * DeltaTime;
	for (int32 i = Rounds.Num() - 1; i >= 0; --i)
	{
		FBallisticRound& Round = Rounds[i];
		const float Move = FMath::Min(Step, Data->MaxRoundTravel - Round.Travelled);
		const FVector End = Round.Position + Round.Direction * Move;
		const FBallisticTrace Hit = Trace(Round.Position, End, Round.Shooter.Get(), &Rng, &Round.HayPassed);
		const FVector Reached = Hit.Kind == EBallisticHit::None ? End : Hit.Point;
		StressNearMisses(Round, Round.Position, Reached);
		Round.Travelled += Move;

		if (Hit.Kind != EBallisticHit::None || Round.Travelled >= Data->MaxRoundTravel - KINDA_SMALL_NUMBER)
		{
			const FBallisticRound Done = MoveTemp(Round);
			Rounds.RemoveAtSwap(i);
			FBallisticTrace Final = Hit;
			if (Final.Kind == EBallisticHit::None)
			{
				Final.Point = End;
			}
			if (ASoldier* Shooter = Done.Shooter.Get())
			{
				Shooter->OnBallisticRoundResolved(Done, Final);
			}
			continue;
		}
		Round.Position = End;
	}
}

void UBallisticsSubsystem::StressNearMisses(FBallisticRound& Round, const FVector& From, const FVector& To)
{
	const AGridManager* Grid = GetGrid();
	if (!Grid)
	{
		return;
	}
	const float Radius = GetData()->NearMissRadius;
	const FVector Seg = To - From;
	for (const TWeakObjectPtr<ASoldier>& Weak : Grid->GetSoldiers())
	{
		ASoldier* S = Weak.Get();
		if (!S || S->IsDead() || S->Team == Round.ShooterTeam || Round.Stressed.Contains(Weak))
		{
			continue;
		}
		if (PointSegmentDistance(S->GetActorLocation() + FVector(0.f, 0.f, 90.f), From, Seg) <= Radius)
		{
			Round.Stressed.Add(Weak);
			S->OnBallisticNearMiss(Round.Shooter.Get(), Round.StressScale);
		}
	}
}

float UBallisticsSubsystem::CoverBlockT(const AGridManager& Grid, FIntPoint Cell, const FVector& A, const FVector& Seg, float TIn, float TOut) const
{
	const FGridCoverCell* Cover = Grid.GetCoverCell(Cell);
	if (!Cover)
	{
		return -1.f;
	}
	const UCoverData* CoverData = Grid.GetCoverData();
	const FVector Centre = Grid.CellToWorld(Cell);
	const float GroundZ = Grid.GetActorLocation().Z;

	// Horizontal extent of the piece inside the cell: whole cell (solid blocks), a trunk (trees) or a slab along
	// the piece (walls, windows, bales), matching the grenade collision (GridManager::IsGrenadeArcClear).
	float T0 = TIn, T1 = TOut;
	if (Cover->Type == ECoverType::Tree && !Cover->bFillsCell)
	{
		const float R = GetData()->TreeTrunkRadius;
		const FVector2D F(A.X - Centre.X, A.Y - Centre.Y);
		const FVector2D D(Seg.X, Seg.Y);
		const float Aq = D.SizeSquared();
		const float B = 2.f * FVector2D::DotProduct(F, D);
		const float C = F.SizeSquared() - R * R;
		const float Disc = B * B - 4.f * Aq * C;
		if (Aq <= KINDA_SMALL_NUMBER || Disc < 0.f)
		{
			return -1.f;
		}
		const float Sq = FMath::Sqrt(Disc);
		T0 = FMath::Max(TIn, (-B - Sq) / (2.f * Aq));
		T1 = FMath::Min(TOut, (-B + Sq) / (2.f * Aq));
		if (T0 > T1)
		{
			return -1.f;
		}
	}
	else if (!Cover->bFillsCell)
	{
		const float Half = UCoverData::GetGrenadeSlabHalfThickness(Cover->Type) * Grid.CellSize / 100.f;
		const float V0 = Cover->bRunsAlongX ? A.Y - Centre.Y : A.X - Centre.X;
		const float DV = Cover->bRunsAlongX ? Seg.Y : Seg.X;
		if (!LinearRange(V0, DV, -Half, Half, TIn, TOut, T0, T1))
		{
			return -1.f;
		}
	}

	// Vertical: the first point in [T0, T1] whose height the piece blocks. Heights where blocking can start are
	// the entry point and the type's height thresholds.
	const float H0 = A.Z - GroundZ;
	const float DH = Seg.Z;
	auto Blocks = [&](float T) { return CoverData->BlocksGrenade(Cover->Type, Cover->bBlocksLineOfSight, H0 + DH * T); };
	if (Blocks(T0))
	{
		return T0;
	}
	float Thresholds[2] = { -1.f, -1.f };
	switch (Cover->Type)
	{
	case ECoverType::Soft:    Thresholds[0] = CoverData->HayBaleHeight; break;
	case ECoverType::LowWall: Thresholds[0] = CoverData->LowWallHeight; break;
	case ECoverType::Window:  Thresholds[0] = CoverData->WindowSillHeight; Thresholds[1] = CoverData->WindowTopHeight; break;
	case ECoverType::Tree:    Thresholds[0] = CoverData->TreeGrenadeBlockHeight; break;
	default: break;
	}
	float Best = -1.f;
	if (FMath::Abs(DH) > KINDA_SMALL_NUMBER)
	{
		for (const float Th : Thresholds)
		{
			if (Th < 0.f)
			{
				continue;
			}
			const float T = (Th - H0) / DH;
			const float Probe = FMath::Min(T + 1e-4f, T1);
			if (T >= T0 && T <= T1 && Blocks(Probe) && (Best < 0.f || T < Best))
			{
				Best = T;
			}
		}
	}
	return Best;
}

FBallisticTrace UBallisticsSubsystem::Trace(const FVector& A, const FVector& B, const ASoldier* Ignore, FRandomStream* RollRng,
	TArray<FIntPoint, TInlineAllocator<2>>* HayPassed) const
{
	FBallisticTrace Result;
	const AGridManager* Grid = GetGrid();
	const FVector Seg = B - A;
	if (!Grid || Seg.IsNearlyZero())
	{
		return Result;
	}

	// Ground (flat).
	float BestT = 1.f;
	const float GroundZ = Grid->GetActorLocation().Z;
	if (B.Z < GroundZ && A.Z >= GroundZ)
	{
		BestT = (A.Z - GroundZ) / (A.Z - B.Z);
		Result.Kind = EBallisticHit::Ground;
	}

	// Soldiers: analytic spheres from each one's current posture.
	FBallisticShapes Shapes;
	for (const TWeakObjectPtr<ASoldier>& Weak : Grid->GetSoldiers())
	{
		ASoldier* S = Weak.Get();
		if (!S || S == Ignore || S->IsDead())
		{
			continue;
		}
		if (PointSegmentDistance(S->GetActorLocation() + FVector(0.f, 0.f, 80.f), A, Seg) > 220.f)
		{
			continue;
		}
		S->GetHitShapes(Shapes);
		for (const FBallisticShape& Shape : Shapes)
		{
			const float T = SegmentSphereT(A, Seg, Shape.Centre, Shape.Radius, BestT);
			if (T >= 0.f && T < BestT)
			{
				BestT = T;
				Result.Kind = EBallisticHit::Soldier;
				Result.Soldier = S;
				Result.Part = Shape.Part;
			}
		}
	}

	// Cover: walk the grid cells the segment crosses (in order) up to the nearest hit so far.
	const UBallisticsData* Data = GetData();
	const float CS = Grid->CellSize;
	const FVector Origin = Grid->CellToWorld(FIntPoint(0, 0)) - FVector(CS * 0.5f, CS * 0.5f, 0.f);
	int32 X = FMath::FloorToInt((A.X - Origin.X) / CS);
	int32 Y = FMath::FloorToInt((A.Y - Origin.Y) / CS);
	const int32 StepX = Seg.X > 0.f ? 1 : -1;
	const int32 StepY = Seg.Y > 0.f ? 1 : -1;
	const float TDeltaX = FMath::Abs(Seg.X) > KINDA_SMALL_NUMBER ? CS / FMath::Abs(Seg.X) : BIG_NUMBER;
	const float TDeltaY = FMath::Abs(Seg.Y) > KINDA_SMALL_NUMBER ? CS / FMath::Abs(Seg.Y) : BIG_NUMBER;
	float TMaxX = FMath::Abs(Seg.X) > KINDA_SMALL_NUMBER ? ((Origin.X + (X + (StepX > 0 ? 1 : 0)) * CS) - A.X) / Seg.X : BIG_NUMBER;
	float TMaxY = FMath::Abs(Seg.Y) > KINDA_SMALL_NUMBER ? ((Origin.Y + (Y + (StepY > 0 ? 1 : 0)) * CS) - A.Y) / Seg.Y : BIG_NUMBER;
	float TIn = 0.f;
	float PassWeight = 1.f;
	for (int32 Guard = 0; Guard < 512 && TIn < BestT; ++Guard)
	{
		const float TOut = FMath::Min3(TMaxX, TMaxY, BestT);
		const FIntPoint Cell(X, Y);
		const float BlockT = CoverBlockT(*Grid, Cell, A, Seg, TIn, TOut);
		if (BlockT >= 0.f && BlockT < BestT)
		{
			const FGridCoverCell* Cover = Grid->GetCoverCell(Cell);
			bool bPass = false;
			if (Cover && Cover->Type == ECoverType::Soft)
			{
				if (RollRng)
				{
					if (HayPassed && HayPassed->Contains(Cell))
					{
						bPass = true;
					}
					else if (RollRng->FRand() < Data->HayPassThroughChance)
					{
						bPass = true;
						if (HayPassed)
						{
							HayPassed->Add(Cell);
						}
					}
				}
				else
				{
					bPass = true;
					PassWeight *= Data->HayPassThroughChance;
				}
			}
			if (!bPass)
			{
				Result.Kind = EBallisticHit::Cover;
				Result.Soldier = nullptr;
				Result.CoverCell = Cell;
				BestT = BlockT;
				break;
			}
		}
		if (TOut >= BestT)
		{
			break;
		}
		if (TMaxX < TMaxY)
		{
			X += StepX;
			TMaxX += TDeltaX;
		}
		else
		{
			Y += StepY;
			TMaxY += TDeltaY;
		}
		TIn = TOut;
	}

	Result.T = BestT;
	Result.Point = A + Seg * BestT;
	Result.PassWeight = PassWeight;
	return Result;
}
