#include "GridManager.h"

#include "Soldier.h"
#include "Algo/Reverse.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Step costs in metres (1 cell = 1 m); a diagonal step covers sqrt(2) m.
	constexpr float OrthogonalCost = 1.f;
	constexpr float DiagonalCost = UE_SQRT_2;

	// Admissible octile-distance heuristic for 8-way grids.
	float OctileDistance(FIntPoint A, FIntPoint B)
	{
		const int32 DX = FMath::Abs(A.X - B.X);
		const int32 DY = FMath::Abs(A.Y - B.Y);
		return OrthogonalCost * FMath::Max(DX, DY) + (DiagonalCost - OrthogonalCost) * FMath::Min(DX, DY);
	}

	struct FPathNode
	{
		FIntPoint Cell;
		float F;
		bool operator<(const FPathNode& Other) const { return F < Other.F; }
	};
}

AGridManager::AGridManager()
{
	PrimaryActorTick.bCanEverTick = true; // Team vision (GDD §9.3), server only.

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UCoverData> DefaultCoverData(TEXT("/Game/Data/DA_CoverTuning.DA_CoverTuning"));
	if (DefaultCoverData.Succeeded())
	{
		CoverData = DefaultCoverData.Object;
	}

	// Flat ground: a thin box whose top face sits at the actor's Z.
	Ground = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ground"));
	Ground->SetupAttachment(RootComponent);
	Ground->SetStaticMesh(CubeMesh.Object);
	Ground->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Ground->SetCanEverAffectNavigation(false);

	HoverHighlight = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HoverHighlight"));
	HoverHighlight->SetupAttachment(RootComponent);
	HoverHighlight->SetStaticMesh(PlaneMesh.Object);
	HoverHighlight->SetMaterial(0, ShapeMaterial.Object);
	HoverHighlight->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HoverHighlight->SetCastShadow(false);
	HoverHighlight->SetHiddenInGame(true);
	HoverHighlight->SetVisibility(false);
}

void AGridManager::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	constexpr float GroundThickness = 20.f;
	const float SizeX = GridSizeX * CellSize;
	const float SizeY = GridSizeY * CellSize;

	// Engine cube is 100 units on each side and centred on its pivot.
	Ground->SetRelativeScale3D(FVector(SizeX / 100.f, SizeY / 100.f, GroundThickness / 100.f));
	Ground->SetRelativeLocation(FVector(0.f, 0.f, -GroundThickness * 0.5f));

	if (GroundMaterial)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(GroundMaterial, this);
		const FVector Origin = GetGridOrigin();
		MID->SetVectorParameterValue(TEXT("GridOrigin"), FLinearColor(Origin.X, Origin.Y, Origin.Z, 0.f));
		MID->SetScalarParameterValue(TEXT("CellSize"), CellSize);
		Ground->SetMaterial(0, MID);
	}

	const float HighlightScale = CellSize * 0.92f / 100.f;
	HoverHighlight->SetRelativeScale3D(FVector(HighlightScale, HighlightScale, 1.f));
	if (UMaterialInterface* Base = HoverHighlight->GetMaterial(0))
	{
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Base);
		if (!MID)
		{
			MID = UMaterialInstanceDynamic::Create(Base, this);
			HoverHighlight->SetMaterial(0, MID);
		}
		MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.85f, 0.2f));
	}
}

FVector AGridManager::GetGridOrigin() const
{
	const FVector Center = GetActorLocation();
	return FVector(Center.X - GridSizeX * CellSize * 0.5f, Center.Y - GridSizeY * CellSize * 0.5f, Center.Z);
}

FVector AGridManager::CellToWorld(FIntPoint Cell) const
{
	const FVector Origin = GetGridOrigin();
	return FVector(Origin.X + (Cell.X + 0.5f) * CellSize, Origin.Y + (Cell.Y + 0.5f) * CellSize, Origin.Z);
}

bool AGridManager::WorldToCell(const FVector& WorldLocation, FIntPoint& OutCell) const
{
	const FVector Local = WorldLocation - GetGridOrigin();
	OutCell = FIntPoint(FMath::FloorToInt(Local.X / CellSize), FMath::FloorToInt(Local.Y / CellSize));
	return IsInBounds(OutCell);
}

bool AGridManager::IsInBounds(FIntPoint Cell) const
{
	return Cell.X >= 0 && Cell.Y >= 0 && Cell.X < GridSizeX && Cell.Y < GridSizeY;
}

namespace
{
	/** Ignore the asking soldier itself, and whoever it is dragging (that soldier trails behind it). */
	bool IsIgnored(const ASoldier* Soldier, const ASoldier* Ignore)
	{
		return Soldier == Ignore || (Ignore && Ignore->GetDraggedSoldier() == Soldier);
	}
}

bool AGridManager::IsCellOccupied(FIntPoint Cell, const ASoldier* Ignore) const
{
	for (const TWeakObjectPtr<ASoldier>& Weak : Soldiers)
	{
		const ASoldier* Soldier = Weak.Get();
		if (Soldier && !IsIgnored(Soldier, Ignore) && Soldier->OccupiesCell(Cell))
		{
			return true;
		}
	}
	return false;
}

bool AGridManager::IsCellPassable(FIntPoint Cell, const ASoldier* Ignore) const
{
	return IsInBounds(Cell) && !IsCellBlockedByCover(Cell) && !IsCellOccupied(Cell, Ignore);
}

bool AGridManager::IsCellFreeAsDestination(FIntPoint Cell, const ASoldier* Ignore) const
{
	if (!IsCellPassable(Cell, Ignore))
	{
		return false;
	}
	for (const TWeakObjectPtr<ASoldier>& Weak : Soldiers)
	{
		const ASoldier* Soldier = Weak.Get();
		if (Soldier && !IsIgnored(Soldier, Ignore) && Soldier->BlocksCells() && Soldier->GetDestinationCell() == Cell)
		{
			return false;
		}
	}
	return true;
}

bool AGridManager::FindPath(FIntPoint Start, FIntPoint Goal, const ASoldier* Mover, TArray<FIntPoint>& OutPath) const
{
	OutPath.Reset();
	if (Start == Goal)
	{
		return true;
	}
	if (!IsCellPassable(Goal, Mover))
	{
		return false;
	}

	// A* over 8-connected cells. Diagonal steps cost sqrt(2) and may not cut
	// corners: both orthogonally adjacent cells must be passable.
	TMap<FIntPoint, FIntPoint> CameFrom;
	TMap<FIntPoint, float> CostSoFar;
	TArray<FPathNode> Open;
	CameFrom.Add(Start, Start);
	CostSoFar.Add(Start, 0.f);
	Open.HeapPush({ Start, OctileDistance(Start, Goal) });

	// 8-way movement: 4 orthogonal then 4 diagonal neighbours. Kept local (not a
	// file-scope global) so Live Coding can't keep a stale copy of the table.
	const FIntPoint GridDirections[8] = {
		{1, 0}, {-1, 0}, {0, 1}, {0, -1},
		{1, 1}, {1, -1}, {-1, 1}, {-1, -1}
	};

	auto IsPassable = [this, Mover](FIntPoint Cell)
	{
		return IsCellPassable(Cell, Mover);
	};

	while (Open.Num() > 0)
	{
		FPathNode Node;
		Open.HeapPop(Node, EAllowShrinking::No);
		const FIntPoint Current = Node.Cell;
		const float CurrentCost = CostSoFar[Current];
		if (Node.F > CurrentCost + OctileDistance(Current, Goal) + KINDA_SMALL_NUMBER)
		{
			continue; // Stale heap entry.
		}

		if (Current == Goal)
		{
			for (FIntPoint Step = Goal; Step != Start; Step = CameFrom[Step])
			{
				OutPath.Add(Step);
			}
			Algo::Reverse(OutPath);
			return true;
		}

		for (const FIntPoint& Dir : GridDirections)
		{
			const FIntPoint Next = Current + Dir;
			if (!IsPassable(Next))
			{
				continue;
			}
			const bool bDiagonal = Dir.X != 0 && Dir.Y != 0;
			if (bDiagonal && (!IsPassable(FIntPoint(Current.X + Dir.X, Current.Y)) || !IsPassable(FIntPoint(Current.X, Current.Y + Dir.Y))))
			{
				continue; // No squeezing diagonally past a blocked corner.
			}

			const float NewCost = CurrentCost + (bDiagonal ? DiagonalCost : OrthogonalCost);
			const float* Existing = CostSoFar.Find(Next);
			if (!Existing || NewCost < *Existing - KINDA_SMALL_NUMBER)
			{
				CostSoFar.Add(Next, NewCost);
				CameFrom.Add(Next, Current);
				Open.HeapPush({ Next, NewCost + OctileDistance(Next, Goal) });
			}
		}
	}
	return false;
}

bool AGridManager::FindNearestFreeCell(FIntPoint Desired, const ASoldier* Ignore, FIntPoint& OutCell) const
{
	Desired.X = FMath::Clamp(Desired.X, 0, GridSizeX - 1);
	Desired.Y = FMath::Clamp(Desired.Y, 0, GridSizeY - 1);
	const int32 MaxRadius = FMath::Max(GridSizeX, GridSizeY);
	for (int32 Radius = 0; Radius <= MaxRadius; ++Radius)
	{
		for (int32 DX = -Radius; DX <= Radius; ++DX)
		{
			for (int32 DY = -Radius; DY <= Radius; ++DY)
			{
				if (FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) != Radius)
				{
					continue;
				}
				const FIntPoint Candidate = Desired + FIntPoint(DX, DY);
				if (IsCellFreeAsDestination(Candidate, Ignore))
				{
					OutCell = Candidate;
					return true;
				}
			}
		}
	}
	return false;
}

// ------------------------------------------------------------------------------------------------ Cover

void AGridManager::RegisterCoverCell(FIntPoint Cell, ECoverType Type, bool bBlocksLineOfSight, AActor* CoverOwner, bool bRunsAlongX, bool bPassable, bool bFillsCell)
{
	if (!IsInBounds(Cell))
	{
		return;
	}
	FGridCoverCell& Entry = CoverCells.FindOrAdd(Cell);
	Entry.Type = Type;
	Entry.bBlocksLineOfSight = bBlocksLineOfSight;
	Entry.bRunsAlongX = bRunsAlongX;
	Entry.bPassable = bPassable;
	Entry.bFillsCell = bFillsCell;
	Entry.Owner = CoverOwner;
}

void AGridManager::NotifyCoverChanged()
{
	++CoverRevision;
	OnCoverChanged.Broadcast();
}

void AGridManager::UnregisterCover(const AActor* CoverOwner)
{
	for (auto It = CoverCells.CreateIterator(); It; ++It)
	{
		if (It.Value().Owner.Get() == CoverOwner || !It.Value().Owner.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

const UCoverData* AGridManager::GetCoverData() const
{
	return CoverData ? CoverData.Get() : GetDefault<UCoverData>();
}

FCoverResult AGridManager::GetCoverAgainst(FIntPoint TargetCell, FIntPoint AttackerCell, TOptional<float> TargetYaw) const
{
	FCoverResult Best;
	const FVector2D ToAttacker(AttackerCell.X - TargetCell.X, AttackerCell.Y - TargetCell.Y);
	if (ToAttacker.IsNearlyZero())
	{
		return Best;
	}
	const FVector2D AttackDir = ToAttacker.GetSafeNormal();
	const UCoverData* Data = GetCoverData();
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(Data->CoverArcHalfAngle)) - KINDA_SMALL_NUMBER;

	const FIntPoint Sides[4] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
	for (const FIntPoint& Side : Sides)
	{
		const FIntPoint Cell = TargetCell + Side;
		const FGridCoverCell* Cover = CoverCells.Find(Cell);
		if (!Cover || Cover->Type == ECoverType::None)
		{
			continue;
		}
		if (FVector2D::DotProduct(FVector2D(Side.X, Side.Y), AttackDir) < MinDot)
		{
			continue; // Flanked: fire comes from outside this side's arc.
		}
		const FIntPoint AttackerToCover = AttackerCell - Cell;
		if (FMath::Max(FMath::Abs(AttackerToCover.X), FMath::Abs(AttackerToCover.Y)) <= 1)
		{
			continue; // The attacker is up against the same cover on the other side: it shoots straight over it.
		}
		const bool bLeaning = !TargetYaw.IsSet() || IsLeaningToward(Side, TargetYaw.GetValue());
		const float Reduction = Data->GetReduction(Cover->Type) * (bLeaning ? 1.f : Data->NotLeaningCoverMultiplier);
		if (Reduction > Best.Reduction)
		{
			Best.Type = Cover->Type;
			Best.CoverCell = Cell;
			Best.Reduction = Reduction;
			Best.bLeaning = bLeaning;
		}
	}
	return Best;
}

bool AGridManager::IsLeaningToward(FIntPoint Side, float Yaw) const
{
	const float SideYaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Side.Y), static_cast<float>(Side.X)));
	return FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, SideYaw)) <= GetCoverData()->LeanFacingHalfAngle + KINDA_SMALL_NUMBER;
}

bool AGridManager::FindCoverToLeanOn(FIntPoint Cell, float Yaw, FIntPoint& OutSide) const
{
	// Lean on the adjacent cover nearest the way the soldier already faces.
	const FIntPoint Sides[4] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
	float BestDelta = TNumericLimits<float>::Max();
	for (const FIntPoint& Side : Sides)
	{
		const FGridCoverCell* Cover = CoverCells.Find(Cell + Side);
		if (!Cover || Cover->Type == ECoverType::None)
		{
			continue;
		}
		const float SideYaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Side.Y), static_cast<float>(Side.X)));
		const float Delta = FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, SideYaw));
		if (Delta < BestDelta)
		{
			BestDelta = Delta;
			OutSide = Side;
		}
	}
	return BestDelta < TNumericLimits<float>::Max();
}

bool AGridManager::IsCoverPiece(FIntPoint Cell, FIntPoint Side) const
{
	const FGridCoverCell* Cover = CoverCells.Find(Cell + Side);
	if (!Cover || Cover->Type == ECoverType::None)
	{
		return false;
	}
	if (Cover->Type != ECoverType::WallCorner)
	{
		return true;
	}
	// A full wall counts only at a corner or the end of a wall: where the face the soldier is up against stops, with
	// open ground (no cover) beside the wall cell along that face. Building corners count from either side.
	const FIntPoint WallCell = Cell + Side;
	const FIntPoint Along(-Side.Y, Side.X);
	for (const FIntPoint Beside : { WallCell + Along, WallCell - Along })
	{
		if (IsInBounds(Beside) && !IsCellBlockedByCover(Beside)) // Rubble (GDD §9.4) counts as open ground.
		{
			return true;
		}
	}
	return false;
}

void AGridManager::GetWallPeeks(FIntPoint Cell, float Yaw, TArray<FWallPeek>& OutPeeks) const
{
	OutPeeks.Reset();
	const FIntPoint Sides[4] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
	for (const FIntPoint& Toward : Sides)
	{
		const FIntPoint WallCell = Cell + Toward;
		const FGridCoverCell* Wall = CoverCells.Find(WallCell);
		if (!Wall || !Wall->bBlocksLineOfSight || !IsLeaningToward(Toward, Yaw))
		{
			continue;
		}
		// Judged from the neighbouring cells, not the piece's own run direction: building corners are single-cell
		// pieces. If the wall carries on beyond WallCell (a corner, or the end of a wall running the way the soldier
		// faces) the soldier leans out sideways and looks down that wall's outer face.
		const FGridCoverCell* Beyond = CoverCells.Find(WallCell + Toward);
		const bool bAlongWall = Beyond && Beyond->bBlocksLineOfSight;
		const FIntPoint Across(-Toward.Y, Toward.X);
		for (const FIntPoint Edge : { Across, FIntPoint(-Across.X, -Across.Y) })
		{
			// The wall must end here, with open ground beside its end to look out from.
			const FIntPoint PeekCell = WallCell + Edge;
			if (!IsInBounds(PeekCell) || IsCellBlockedByCover(PeekCell)) // Rubble (GDD §9.4) counts as open ground.
			{
				continue;
			}
			FWallPeek& Peek = OutPeeks.AddDefaulted_GetRef();
			Peek.WallCell = WallCell;
			Peek.PeekCell = PeekCell;
			Peek.Toward = Toward;
			Peek.Edge = Edge;
			Peek.bAlongWall = bAlongWall;
		}
	}
}

bool AGridManager::IsGrenadeArcClear(const FVector& Start, const FVector& End, float ArcHeight) const
{
	const UCoverData* Data = GetCoverData();
	const float GroundZ = GetActorLocation().Z;

	// Sample the flight every ~5 cm of horizontal travel; cover cells are 100 cm across, so nothing is skipped.
	const int32 Samples = FMath::Max(2, FMath::CeilToInt(FVector::Dist2D(Start, End) / 5.f));
	FIntPoint StartCell = FIntPoint(MIN_int32, MIN_int32), EndCell = StartCell;
	WorldToCell(Start, StartCell);
	WorldToCell(End, EndCell);
	for (int32 i = 1; i < Samples; ++i)
	{
		const float Alpha = static_cast<float>(i) / Samples;
		FVector P = FMath::Lerp(Start, End, Alpha);
		P.Z += ArcHeight * 4.f * Alpha * (1.f - Alpha);

		FIntPoint Cell;
		if (!WorldToCell(P, Cell))
		{
			continue;
		}
		const FGridCoverCell* Cover = CoverCells.Find(Cell);
		if (!Cover || !Data->BlocksGrenade(Cover->Type, Cover->bBlocksLineOfSight, P.Z - GroundZ))
		{
			continue;
		}
		if (Cover->bPassable && (Cell == StartCell || Cell == EndCell))
		{
			continue; // Rubble (GDD §9.4) a soldier stands on: throwing from it or landing on it is fine.
		}
		// Solid blocks (vehicles, wells, crate stacks) fill the cell.
		if (Cover->bFillsCell)
		{
			return false;
		}
		// Walls, windows and bales are slabs running the length of the cell along the piece, thinner across
		// it: only the slab stops grenades, so one can drop in right behind a wall or pass a window opening.
		const FVector Offset = P - CellToWorld(Cell);
		const float Across = Cover->bRunsAlongX ? Offset.Y : Offset.X;
		const float HalfThickness = UCoverData::GetGrenadeSlabHalfThickness(Cover->Type) * CellSize / 100.f;
		if (FMath::Abs(Across) <= HalfThickness)
		{
			return false;
		}
	}
	return true;
}

bool AGridManager::HasLineOfSight(FIntPoint From, FIntPoint To, float FromHeight, float ToHeight) const
{
	if (From == To)
	{
		return true;
	}
	const UCoverData* Data = GetCoverData();

	// Track the sight line's height where it enters and leaves each cell it crosses.
	auto HeightAt = [&](double T) { return static_cast<float>(FromHeight + (ToHeight - FromHeight) * T); };
	return IsGridLineClear(From, To, [&](FIntPoint Cell, double TIn, double TOut)
	{
		const FGridCoverCell* Cover = CoverCells.Find(Cell);
		if (!Cover)
		{
			return false;
		}
		// A soldier right up against low cover (hay, low wall) looks down over it, so it sees a prone soldier
		// lying on the other side - and is seen by it. Only if its own sight height clears the cover's top.
		const float SeeOverHeight = Cover->bBlocksLineOfSight ? -1.f : Data->GetSeeOverHeight(Cover->Type);
		if (SeeOverHeight >= 0.f)
		{
			auto Touches = [&](FIntPoint Other) { return FMath::Abs(Other.X - Cell.X) <= 1 && FMath::Abs(Other.Y - Cell.Y) <= 1; };
			if ((Touches(From) && FromHeight > SeeOverHeight) || (Touches(To) && ToHeight > SeeOverHeight))
			{
				return false;
			}
		}
		return Data->BlocksSight(Cover->Type, Cover->bBlocksLineOfSight, HeightAt(TIn), HeightAt(TOut));
	});
}

bool AGridManager::IsBlastShielded(FIntPoint BlastCell, FIntPoint TargetCell, float TargetHeight) const
{
	if (BlastCell == TargetCell)
	{
		return false;
	}
	const UCoverData* Data = GetCoverData();
	auto Touches = [&](FIntPoint A, FIntPoint B) { return FMath::Abs(A.X - B.X) <= 1 && FMath::Abs(A.Y - B.Y) <= 1; };

	// GDD §7.1: any cover piece on the line between the blast and the soldier shields it, except low cover and
	// windows the soldier is right up against with its head above the top / sill (peering over or through).
	const bool bClear = IsGridLineClear(BlastCell, TargetCell, [&](FIntPoint Cell, double, double)
	{
		const FGridCoverCell* Cover = CoverCells.Find(Cell);
		if (!Cover || Cover->Type == ECoverType::None)
		{
			return false;
		}
		if (Cover->bBlocksLineOfSight)
		{
			return true; // Full walls (and the wall round a window flagged full height).
		}
		float ExposedAbove = -1.f;
		switch (Cover->Type)
		{
		case ECoverType::Soft:    ExposedAbove = Data->HayBaleHeight; break;
		case ECoverType::LowWall: ExposedAbove = Data->LowWallHeight; break;
		case ECoverType::Window:  ExposedAbove = Data->WindowSillHeight; break;
		default:                  return true; // Wall corners, trees.
		}
		return !(Touches(Cell, TargetCell) && TargetHeight > ExposedAbove);
	});
	return !bClear;
}

bool AGridManager::IsGridLineClear(FIntPoint From, FIntPoint To, TFunctionRef<bool(FIntPoint Cell, double TIn, double TOut)> Blocks) const
{
	if (From == To)
	{
		return true;
	}
	// Walk every cell the segment between the two cell centres passes through (grid traversal). The two end
	// cells never block.
	const int64 AbsDX = FMath::Abs(To.X - From.X);
	const int64 AbsDY = FMath::Abs(To.Y - From.Y);
	const int32 StepX = To.X > From.X ? 1 : (To.X < From.X ? -1 : 0);
	const int32 StepY = To.Y > From.Y ? 1 : (To.Y < From.Y ? -1 : 0);
	auto CellBlocks = [&](FIntPoint Cell, double TIn, double TOut)
	{
		return Cell != From && Cell != To && Blocks(Cell, TIn, TOut);
	};

	// The line crosses its (KX+1)th vertical grid line at T = (KX + 0.5) / AbsDX, likewise for Y.
	// Comparing (2KX+1)*AbsDY with (2KY+1)*AbsDX orders the crossings exactly, so lines through a
	// grid corner are detected without floating-point error.
	int64 KX = 0;
	int64 KY = 0;
	FIntPoint Cell = From;
	double TEnter = 0.0;
	const int64 MaxSteps = AbsDX + AbsDY + 1;
	for (int64 Step = 0; Step < MaxSteps && Cell != To; ++Step)
	{
		const int64 CrossX = StepX ? (2 * KX + 1) * AbsDY : MAX_int64;
		const int64 CrossY = StepY ? (2 * KY + 1) * AbsDX : MAX_int64;
		const double TExit = CrossX <= CrossY
			? (KX + 0.5) / static_cast<double>(AbsDX)
			: (KY + 0.5) / static_cast<double>(AbsDY);

		if (CellBlocks(Cell, TEnter, TExit))
		{
			return false;
		}

		if (CrossX == CrossY)
		{
			// Exactly through a grid corner: the line squeezes past unless both cells beside the corner block.
			if (CellBlocks(Cell + FIntPoint(StepX, 0), TExit, TExit) && CellBlocks(Cell + FIntPoint(0, StepY), TExit, TExit))
			{
				return false;
			}
			Cell += FIntPoint(StepX, StepY);
			++KX;
			++KY;
		}
		else if (CrossX < CrossY)
		{
			Cell.X += StepX;
			++KX;
		}
		else
		{
			Cell.Y += StepY;
			++KY;
		}
		TEnter = TExit;
	}
	return true;
}

void AGridManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		UpdateTeamVision();
	}
}

void AGridManager::UpdateTeamVision()
{
	// GDD §9.3: a soldier is visible to the other team while any living enemy soldier can see it.
	for (const TWeakObjectPtr<ASoldier>& TargetPtr : Soldiers)
	{
		ASoldier* Target = TargetPtr.Get();
		if (!Target)
		{
			continue;
		}
		bool bSpotted = false;
		for (const TWeakObjectPtr<ASoldier>& ViewerPtr : Soldiers)
		{
			const ASoldier* Viewer = ViewerPtr.Get();
			if (Viewer && Viewer->Team != Target->Team && !Viewer->IsDead() && Viewer->CanSee(Target))
			{
				bSpotted = true;
				break;
			}
		}
		Target->SetSpottedByEnemy(bSpotted);
	}
}

void AGridManager::RegisterSoldier(ASoldier* Soldier)
{
	Soldiers.AddUnique(Soldier);
}

void AGridManager::UnregisterSoldier(ASoldier* Soldier)
{
	Soldiers.Remove(Soldier);
}

void AGridManager::SetHoveredCell(bool bVisible, FIntPoint Cell)
{
	bHoverVisible = bVisible && IsInBounds(Cell);
	HoveredCell = Cell;
	if (bHoverVisible)
	{
		HoverHighlight->SetWorldLocation(CellToWorld(Cell) + FVector(0.f, 0.f, 1.f));
		HoverHighlight->SetHiddenInGame(false);
		HoverHighlight->SetVisibility(true);
	}
	else
	{
		HoverHighlight->SetVisibility(false);
	}
}

AGridManager* AGridManager::Find(const UObject* WorldContextObject)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGridManager> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}
