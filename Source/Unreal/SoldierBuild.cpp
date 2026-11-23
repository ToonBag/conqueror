// ASoldier buildables (GDD §13.3): placing C4 and wall reinforcements on breachable walls.
// Kept out of Soldier.cpp; the interruption hooks (CancelBuild on new orders, stun, injury, death, drag) live there.

#include "Soldier.h"

#include "BreachableWall.h"
#include "C4Charge.h"
#include "GridManager.h"
#include "SoldierData.h"

namespace
{
	const FIntPoint OrthogonalSides[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
}

void ASoldier::InitBuildables()
{
	BuildableCounts.Reset();
	for (const FBuildableSpec& Spec : GetSoldierData()->Buildables)
	{
		BuildableCounts.FindOrAdd(Spec.Kind) += Spec.StartingCount;
	}
}

int32 ASoldier::GetBuildableCount(EBuildableKind Kind) const
{
	const int32* Count = BuildableCounts.Find(Kind);
	return Count ? *Count : 0;
}

FString ASoldier::GetBuildBlockReason(EBuildableKind Kind, const ABreachableWall* Wall) const
{
	if (bDead)
	{
		return TEXT("KIA");
	}
	if (StunRemaining > 0.f)
	{
		return TEXT("STUNNED");
	}
	if (IsBeingDragged())
	{
		return TEXT("BEING DRAGGED");
	}
	if (bInjured)
	{
		return TEXT("INJURED");
	}
	if (IsShocked())
	{
		return TEXT("SHOCKED"); // Can't walk to the wall (GDD §8.1).
	}
	if (IsDragging())
	{
		return TEXT("DRAGGING");
	}
	if (GetBuildableCount(Kind) <= 0)
	{
		return Kind == EBuildableKind::C4 ? TEXT("NO C4") : TEXT("NO REINFORCEMENT");
	}
	if (!Wall)
	{
		return TEXT("NO WALL");
	}
	if (!Wall->IsIntact())
	{
		return TEXT("BREACHED");
	}
	if (Wall->IsReinforced())
	{
		return Kind == EBuildableKind::C4 ? TEXT("REINFORCED") : TEXT("ALREADY REINFORCED");
	}
	return FString();
}

bool ASoldier::FindBuildStandCell(const ABreachableWall* Wall, FIntPoint WallCell, FIntPoint& OutStandCell, FIntPoint& OutWallCell) const
{
	if (!Grid || !Wall)
	{
		return false;
	}
	const FIntPoint From = GetStandCell();

	// Face cells of the chosen wall cell first; if none is usable, those of the rest of the wall, nearest first.
	auto Gather = [&](FIntPoint Cell, TArray<TPair<FIntPoint, FIntPoint>>& Out)
	{
		for (const FIntPoint& Side : OrthogonalSides)
		{
			const FIntPoint Stand = Cell + Side;
			FIntPoint Faced;
			if (Wall->GetFaceCellFor(Stand, Faced) && Faced == Cell && Grid->IsInBounds(Stand)
				&& (Stand == From || Grid->IsCellFreeAsDestination(Stand, this)))
			{
				Out.Emplace(Stand, Faced);
			}
		}
	};
	TArray<TPair<FIntPoint, FIntPoint>> Candidates;
	Gather(WallCell, Candidates);
	if (Candidates.Num() == 0)
	{
		TArray<FIntPoint> Cells = Wall->GetOccupiedCells();
		Cells.Sort([WallCell](const FIntPoint& A, const FIntPoint& B)
		{
			return FVector2D::DistSquared(FVector2D(A), FVector2D(WallCell)) < FVector2D::DistSquared(FVector2D(B), FVector2D(WallCell));
		});
		for (const FIntPoint& Cell : Cells)
		{
			if (Cell != WallCell)
			{
				Gather(Cell, Candidates);
				if (Candidates.Num() > 0)
				{
					break;
				}
			}
		}
	}

	// The one quickest to walk to: the side the soldier is approaching from.
	float BestCost = TNumericLimits<float>::Max();
	bool bFound = false;
	for (const TPair<FIntPoint, FIntPoint>& Candidate : Candidates)
	{
		float Cost = 0.f;
		if (Candidate.Key != From)
		{
			TArray<FIntPoint> Route;
			if (!IsCohesiveDestination(Candidate.Key) || !Grid->FindPath(From, Candidate.Key, this, Route))
			{
				continue;
			}
			FIntPoint Prev = From;
			for (const FIntPoint& Step : Route)
			{
				Cost += FVector2D::Distance(FVector2D(Prev), FVector2D(Step));
				Prev = Step;
			}
		}
		if (Cost < BestCost)
		{
			BestCost = Cost;
			OutStandCell = Candidate.Key;
			OutWallCell = Candidate.Value;
			bFound = true;
		}
	}
	return bFound;
}

bool ASoldier::OrderBuild(EBuildableKind Kind, ABreachableWall* Wall, FIntPoint WallCell)
{
	const FString Reason = GetBuildBlockReason(Kind, Wall);
	if (!Reason.IsEmpty())
	{
		if (!bDead)
		{
			ShowMessage(Reason);
		}
		return false;
	}
	if (HasBuildOrder() && BuildWall.Get() == Wall && BuildKind == Kind && BuildWallCell == WallCell)
	{
		return true; // Already on it.
	}
	// A new order like any other: it drops a throw winding up, a treatment, an attack and a previous placement.
	CancelBuild();
	CancelMedkit();
	CancelGrenadeThrow();
	ClearAttack();
	BuildApproachTries = 0;
	if (!ApproachBuild(Kind, Wall, WallCell))
	{
		ShowMessage(TEXT("CAN'T REACH WALL"));
		return false;
	}
	return true;
}

bool ASoldier::ApproachBuild(EBuildableKind Kind, ABreachableWall* Wall, FIntPoint WallCell)
{
	FIntPoint Stand, Faced;
	if (!FindBuildStandCell(Wall, WallCell, Stand, Faced))
	{
		return false;
	}
	if (Stand == GetStandCell())
	{
		StopAfterCurrentStep(); // Already there: finish the step in progress and start once still.
	}
	else if (!MoveToCell(Stand)) // MoveToCell clears any build link (it is an order like any other), so link afterwards.
	{
		return false;
	}
	BuildKind = Kind;
	BuildWall = Wall;
	BuildWallCell = Faced;
	BuildStandCell = Stand;
	bBuildChanneling = false;
	BuildRemaining = 0.f;
	BuildDuration = 0.f;
	return true;
}

void ASoldier::CancelBuild()
{
	BuildWall = nullptr;
	bBuildChanneling = false;
	BuildRemaining = 0.f;
	BuildDuration = 0.f;
}

void ASoldier::UpdateBuild(float DeltaSeconds)
{
	ABreachableWall* Wall = BuildWall.Get();
	if (!Wall)
	{
		if (!BuildWall.IsExplicitlyNull())
		{
			CancelBuild(); // The wall went away.
		}
		return;
	}

	// The soldier's own state and the item are re-checked all the way through. The wall: breached by someone else
	// stops it; a C4 still goes on a wall reinforced meanwhile (it will blast but not breach, GDD §7.2), while a
	// second reinforcement is pointless and stops (the item is kept).
	FString Reason = GetBuildBlockReason(BuildKind, Wall);
	if (Reason == TEXT("SHOCKED") && bBuildChanneling)
	{
		Reason.Reset(); // GDD §13.3: only a new order, a stun, injury, death or being dragged stop the channel.
	}
	if (Reason == TEXT("REINFORCED") && BuildKind == EBuildableKind::C4)
	{
		Reason.Reset();
	}
	if (!Reason.IsEmpty())
	{
		CancelBuild();
		ShowMessage(Reason);
		return;
	}

	if (!bBuildChanneling)
	{
		if (IsMoving())
		{
			return;
		}
		if (CurrentCell == BuildStandCell)
		{
			// GDD §13.3: kneel at the wall, face it and channel. Hands are busy: no firing meanwhile.
			const FBuildableSpec* Spec = GetSoldierData()->FindBuildable(BuildKind);
			ClearAttack();
			bBuildChanneling = true;
			BuildDuration = FMath::Max(Spec ? Spec->PlaceTime : 7.f, KINDA_SMALL_NUMBER);
			BuildRemaining = BuildDuration;
		}
		else if (++BuildApproachTries > MaxBuildApproachTries || !ApproachBuild(BuildKind, Wall, BuildWallCell))
		{
			// Cut short on the way (squad limit, blocked cell): try a few times, then give up.
			CancelBuild();
			ShowMessage(TEXT("CAN'T REACH WALL"));
		}
		return;
	}

	if (CurrentCell != BuildStandCell || IsMoving())
	{
		CancelBuild();
		return;
	}
	if (Grid)
	{
		DesiredYaw = (Grid->CellToWorld(BuildWallCell) - GetActorLocation()).Rotation().Yaw;
		bHasFacingOrder = true;
	}
	BuildRemaining -= DeltaSeconds;
	if (BuildRemaining <= 0.f)
	{
		FinishBuild();
	}
}

void ASoldier::FinishBuild()
{
	ABreachableWall* Wall = BuildWall.Get();
	const EBuildableKind Kind = BuildKind;
	const FIntPoint WallCell = BuildWallCell;
	const FIntPoint StandCell = BuildStandCell;
	CancelBuild();
	if (!Wall)
	{
		return;
	}
	int32& Count = BuildableCounts.FindOrAdd(Kind);
	Count = FMath::Max(0, Count - 1); // Used up only now that it is in place.

	switch (Kind)
	{
	case EBuildableKind::C4:
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FTransform Where(AC4Charge::GetFaceLocation(this, WallCell, StandCell));
		if (AC4Charge* Charge = GetWorld()->SpawnActor<AC4Charge>(AC4Charge::StaticClass(), Where, Params))
		{
			Charge->Arm(Wall, WallCell, StandCell, this, *GetSoldierData());
		}
		ShowMessage(TEXT("C4 PLANTED"), 2.f);
		break;
	}
	case EBuildableKind::WallReinforcement:
		// Any C4 still on its fuse on this wall turns into a dud (AC4Charge watches for this).
		Wall->SetReinforced();
		ShowMessage(TEXT("WALL REINFORCED"), 2.f);
		break;
	}
}
