// ASquadPlayerController: the Build sub-wheel and build orders (GDD §13.3).

#include "SquadPlayerController.h"

#include "BreachableWall.h"
#include "GridManager.h"
#include "Soldier.h"
#include "SoldierData.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

namespace
{
	/** A point this close (cm) to a neighbouring wall cell's centre still picks that wall (clicks on the face's edge). */
	constexpr float WallPickSlack = 75.f;

	ABreachableWall* WallOccupying(const UObject* WorldContext, FIntPoint Cell)
	{
		if (ABreachableWall* Wall = ABreachableWall::FindAtCell(WorldContext, Cell))
		{
			return Wall;
		}
		// Fallback while FindAtCell isn't wired up: ask each breachable wall which cells it claims.
		UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<ABreachableWall> It(World); It; ++It)
		{
			if (It->GetOccupiedCells().Contains(Cell))
			{
				return *It;
			}
		}
		return nullptr;
	}
}

void ASquadPlayerController::ShowBuildFeedback(const FString& Text, const FVector2D& World)
{
	BuildFeedback.Text = Text;
	BuildFeedback.World = World;
	BuildFeedback.Time = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
}

ABreachableWall* ASquadPlayerController::FindBreachableWallAt(const FVector2D& WorldPoint, FIntPoint& OutWallCell) const
{
	if (!Grid)
	{
		return nullptr;
	}
	FIntPoint Cell;
	if (!Grid->WorldToCell(FVector(WorldPoint, Grid->GetActorLocation().Z), Cell))
	{
		return nullptr;
	}
	if (ABreachableWall* Wall = WallOccupying(this, Cell))
	{
		OutWallCell = Cell;
		return Wall;
	}
	// The open cell right in front of a wall: take the wall cell whose centre is nearest the point, if close enough.
	ABreachableWall* Best = nullptr;
	float BestDistance = WallPickSlack;
	for (const FIntPoint Side : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
	{
		const FIntPoint Near = Cell + Side;
		ABreachableWall* Wall = WallOccupying(this, Near);
		const float Distance = Wall ? FVector2D::Distance(FVector2D(Grid->CellToWorld(Near)), WorldPoint) : BIG_NUMBER;
		if (Distance < BestDistance)
		{
			Best = Wall;
			BestDistance = Distance;
			OutWallCell = Near;
		}
	}
	return Best;
}

ASoldier* ASquadPlayerController::FindBuilderFor(EBuildableKind Kind, const ABreachableWall* Wall, FIntPoint WallCell, const TArray<ASoldier*>& Candidates, FString* OutReason) const
{
	auto Fail = [OutReason](const FString& Reason) -> ASoldier*
	{
		if (OutReason)
		{
			*OutReason = Reason;
		}
		return nullptr;
	};
	if (!Wall)
	{
		return Fail(TEXT("NO BREACHABLE WALL HERE"));
	}
	if (!Wall->IsIntact())
	{
		return Fail(TEXT("WALL BREACHED"));
	}
	if (Wall->IsReinforced())
	{
		return Fail(Kind == EBuildableKind::C4 ? TEXT("WALL REINFORCED") : TEXT("ALREADY REINFORCED"));
	}
	if (Candidates.Num() == 0)
	{
		return Fail(TEXT("SELECT A SOLDIER"));
	}
	// Nearest first; the first one able to place it and reach the face does it.
	TArray<ASoldier*> Sorted = Candidates;
	const FVector2D Target = Grid ? FVector2D(Grid->CellToWorld(WallCell)) : FVector2D::ZeroVector;
	Sorted.Sort([&Target](const ASoldier& A, const ASoldier& B)
	{
		return FVector2D::DistSquared(FVector2D(A.GetActorLocation()), Target) < FVector2D::DistSquared(FVector2D(B.GetActorLocation()), Target);
	});
	FString FirstReason;
	bool bAnyCarries = false;
	for (ASoldier* Soldier : Sorted)
	{
		if (!Soldier)
		{
			continue;
		}
		bAnyCarries |= Soldier->GetBuildableCount(Kind) > 0;
		FString Reason = Soldier->GetBuildBlockReason(Kind, Wall);
		FIntPoint Stand, Faced;
		if (Reason.IsEmpty() && !Soldier->FindBuildStandCell(Wall, WallCell, Stand, Faced))
		{
			Reason = TEXT("CAN'T REACH WALL");
		}
		if (Reason.IsEmpty())
		{
			return Soldier;
		}
		if (FirstReason.IsEmpty() && Soldier->GetBuildableCount(Kind) > 0)
		{
			FirstReason = Reason; // The nearest soldier carrying one: its reason is the useful one.
		}
	}
	if (!bAnyCarries)
	{
		return Fail(Kind == EBuildableKind::C4 ? TEXT("NO C4 LEFT") : TEXT("NO REINFORCEMENT LEFT"));
	}
	return Fail(FirstReason);
}

TArray<ASquadPlayerController::FBuildWheelItem> ASquadPlayerController::GetBuildWheelItems(const FVector2D& WorldPoint) const
{
	TArray<FBuildWheelItem> Items;
	FIntPoint WallCell = FIntPoint::ZeroValue;
	const ABreachableWall* Wall = FindBreachableWallAt(WorldPoint, WallCell);
	const TArray<ASoldier*> Candidates = GetSelectedSoldiers();

	// One entry per buildable in the loadout (GDD §4.2), in data order.
	const USoldierData* Data = nullptr;
	for (const ASoldier* Soldier : Squad)
	{
		if (Soldier)
		{
			Data = Soldier->GetSoldierData();
			break;
		}
	}
	if (!Data)
	{
		Data = GetDefault<USoldierData>();
	}
	for (const FBuildableSpec& Spec : Data->Buildables)
	{
		FBuildWheelItem& Item = Items.AddDefaulted_GetRef();
		Item.Kind = Spec.Kind;
		Item.Label = Spec.DisplayName;
		for (const ASoldier* Soldier : Squad)
		{
			if (Soldier && !Soldier->IsDead())
			{
				Item.SquadCount += Soldier->GetBuildableCount(Spec.Kind);
			}
		}
		FString Reason;
		ASoldier* Builder = FindBuilderFor(Spec.Kind, Wall, WallCell, Candidates, &Reason);
		Item.Builder = Builder;
		Item.bEnabled = Builder != nullptr;
		Item.Detail = Builder
			? FString::Printf(TEXT("Soldier %d (%d left)"), Builder->GetSquadNumber(), Builder->GetBuildableCount(Spec.Kind))
			: Reason;
	}
	return Items;
}

void ASquadPlayerController::OrderBuildSelected(EBuildableKind Kind, FVector2D WorldPoint)
{
	ServerOrderBuild(Kind, WorldPoint, GetSelectedSoldiers());
}

void ASquadPlayerController::ServerOrderBuild_Implementation(EBuildableKind Kind, FVector2D WorldPoint, const TArray<ASoldier*>& Candidates)
{
	// The server re-checks everything: only this player's own living soldiers, the wall and who can reach it.
	TArray<ASoldier*> Mine;
	for (ASoldier* Soldier : Candidates)
	{
		if (Soldier && Squad.Contains(Soldier) && Soldier->Team == PlayerTeam && !Soldier->IsDead())
		{
			Mine.AddUnique(Soldier);
		}
	}
	FIntPoint WallCell = FIntPoint::ZeroValue;
	ABreachableWall* Wall = FindBreachableWallAt(WorldPoint, WallCell);
	FString Reason;
	ASoldier* Builder = FindBuilderFor(Kind, Wall, WallCell, Mine, &Reason);
	if (!Builder || !Builder->OrderBuild(Kind, Wall, WallCell))
	{
		// Feedback at the wheel's point. Once soldiers replicate and players are remote this becomes a client RPC.
		ShowBuildFeedback(Reason.IsEmpty() ? FString(TEXT("BLOCKED")) : Reason, WorldPoint);
	}
}

void ASquadPlayerController::OpenCommandWheelAt(FVector2D ScreenPosition, FVector2D WorldPoint, bool bBuildMenu)
{
	CancelCommand();
	Radial.ScreenCenter = ScreenPosition;
	Radial.WorldOrigin = WorldPoint;
	Radial.Items = GetRadialActions();
	Radial.Hover = INDEX_NONE;
	Radial.OpenTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	Radial.bBuildMenu = bBuildMenu;
	if (bBuildMenu)
	{
		Radial.BuildItems = GetBuildWheelItems(WorldPoint);
	}
	CommandMode = ECommandMode::Radial;
}

TArray<FString> ASquadPlayerController::DescribeCommandWheel(FVector2D WorldPoint) const
{
	TArray<FString> Lines;
	FString Main;
	for (const ERadialAction Action : GetRadialActions())
	{
		Main += FString(Main.IsEmpty() ? TEXT("") : TEXT(", ")) + TacticalPlan::RadialName(Action);
	}
	Lines.Add(FString::Printf(TEXT("Wheel: %s"), *Main));
	for (const FBuildWheelItem& Item : GetBuildWheelItems(WorldPoint))
	{
		Lines.Add(FString::Printf(TEXT("  Build > %s x%d [%s]%s"), *Item.Label, Item.SquadCount, *Item.Detail, Item.bEnabled ? TEXT("") : TEXT(" (greyed)")));
	}
	return Lines;
}

namespace
{
	/** squad.OpenWheel X Y [build]: open the command wheel (or the Build sub-wheel) on world point X Y and log its slices. */
	FAutoConsoleCommandWithWorldAndArgs CmdOpenWheel(
		TEXT("squad.OpenWheel"),
		TEXT("squad.OpenWheel <WorldX> <WorldY> [build] - open the command wheel (or Build sub-wheel) on that ground point and log its slices (tests / screenshots)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ASquadPlayerController* PC = World ? Cast<ASquadPlayerController>(World->GetFirstPlayerController()) : nullptr;
			if (!PC || Args.Num() < 2)
			{
				return;
			}
			const FVector2D Point(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]));
			const bool bBuild = Args.Num() > 2 && Args[2].Equals(TEXT("build"), ESearchCase::IgnoreCase);
			FVector2D Screen;
			const float GroundZ = 0.f;
			if (!PC->ProjectWorldLocationToScreen(FVector(Point, GroundZ), Screen, false))
			{
				int32 SX = 0, SY = 0;
				PC->GetViewportSize(SX, SY);
				Screen = FVector2D(SX * 0.5f, SY * 0.5f);
			}
			PC->OpenCommandWheelAt(Screen, Point, bBuild);
			for (const FString& Line : PC->DescribeCommandWheel(Point))
			{
				UE_LOG(LogTemp, Log, TEXT("squad.OpenWheel: %s"), *Line);
			}
		}));
}
