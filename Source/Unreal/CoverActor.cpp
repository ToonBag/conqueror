#include "CoverActor.h"

#include "GridManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoverActor, Log, All);

const FName ACoverActor::CoverVisualTag(TEXT("CoverVisual"));

namespace
{
	TSoftObjectPtr<UStaticMesh> EnvMesh(const TCHAR* Folder, const TCHAR* Name)
	{
		return TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(FString::Printf(TEXT("/Game/Environment/%s/%s.%s"), Folder, Name, Name)));
	}

	void MakeSolid(UInstancedStaticMeshComponent* Set)
	{
		// Solid: blocks physics, projectiles and traces. Gameplay blocking (movement, sight, grenades) is on the
		// grid; the mouse (Visibility) and camera still pass through so you can click the ground behind cover.
		Set->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Set->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		Set->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}

	UInstancedStaticMeshComponent* MakeLegacySet(AActor* Owner, const TCHAR* Name, USceneComponent* Parent)
	{
		UInstancedStaticMeshComponent* Set = Owner->CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Set->SetupAttachment(Parent);
		Set->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Set->AlwaysLoadOnServer = true; // A dedicated server skips loading no-collision components otherwise (null on the server).
		Set->SetCanEverAffectNavigation(false);
		Set->SetMobility(EComponentMobility::Movable);
		return Set;
	}
}

ACoverActor::ACoverActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	HayShapes = MakeLegacySet(this, TEXT("HayShapes"), RootComponent);
	StoneShapes = MakeLegacySet(this, TEXT("StoneShapes"), RootComponent);
	WallShapes = MakeLegacySet(this, TEXT("WallShapes"), RootComponent);
	WoodShapes = MakeLegacySet(this, TEXT("WoodShapes"), RootComponent);
	FoliageShapes = MakeLegacySet(this, TEXT("FoliageShapes"), RootComponent);

	// Low-poly set from SourceArt/Environment (imported by import_cover_unreal.py).
	HayMeshes = { EnvMesh(TEXT("LowCover"), TEXT("SM_HayBale_Stack")), EnvMesh(TEXT("LowCover"), TEXT("SM_HayBale_Stack_B")) };
	LowWallMeshes = { EnvMesh(TEXT("LowCover"), TEXT("SM_LowWall_Stone")), EnvMesh(TEXT("LowCover"), TEXT("SM_LowWall_Stone")),
		EnvMesh(TEXT("LowCover"), TEXT("SM_LowWall_Stone")), EnvMesh(TEXT("LowCover"), TEXT("SM_LowWall_Stone_Damaged")) };
	WallMeshes = { EnvMesh(TEXT("Walls"), TEXT("SM_Wall_Plaster")), EnvMesh(TEXT("Walls"), TEXT("SM_Wall_Plaster")),
		EnvMesh(TEXT("Walls"), TEXT("SM_Wall_Plaster_Patched")) };
	WindowMeshes = { EnvMesh(TEXT("Walls"), TEXT("SM_Wall_Window_Plaster")), EnvMesh(TEXT("Walls"), TEXT("SM_Wall_Window_Plaster")),
		EnvMesh(TEXT("Walls"), TEXT("SM_Wall_Window_Plaster_Broken")) };
	TreeMeshes = { EnvMesh(TEXT("Trees"), TEXT("SM_Tree_Oak")), EnvMesh(TEXT("Trees"), TEXT("SM_Tree_Apple")) };
}

void ACoverActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SnapToGrid(AGridManager::Find(this));

	// The editor has already destroyed the sets the last construction made; start afresh.
	MeshSets.Reset();
	for (UInstancedStaticMeshComponent* Set : { HayShapes.Get(), StoneShapes.Get(), WallShapes.Get(), WoodShapes.Get(), FoliageShapes.Get() })
	{
		Set->ClearInstances();
		Set->SetStaticMesh(nullptr);
		Set->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Set->AlwaysLoadOnServer = true; // A dedicated server skips loading no-collision components otherwise (null on the server).
	}

	TArray<FCoverCellSpec> Cells;
	GenerateCells(Cells);
	int32 MinX = TNumericLimits<int32>::Max();
	int32 MaxX = TNumericLimits<int32>::Lowest();
	for (const FCoverCellSpec& Cell : Cells)
	{
		MinX = FMath::Min(MinX, Cell.Offset.X);
		MaxX = FMath::Max(MaxX, Cell.Offset.X);
	}
	for (const FCoverCellSpec& Cell : Cells)
	{
		AddCellVisual(Cell, FVector(Cell.Offset.X * 100.f, Cell.Offset.Y * 100.f, 0.f),
			Cell.Offset.X == MinX ? StartExtension : 0.f, Cell.Offset.X == MaxX ? EndExtension : 0.f);
	}
	AddPieceVisual(Cells);
}

void ACoverActor::SnapToGrid(AGridManager* Grid)
{
	// Centre on a cell and face along a grid axis so the visuals line up with the cells we claim.
	FRotator Rotation = GetActorRotation();
	Rotation.Pitch = 0.f;
	Rotation.Roll = 0.f;
	Rotation.Yaw = FMath::RoundToFloat(Rotation.Yaw / 90.f) * 90.f;

	FVector Location = GetActorLocation();
	FIntPoint Cell;
	if (Grid && Grid->WorldToCell(Location, Cell))
	{
		Location = Grid->CellToWorld(Cell);
	}
	SetActorLocationAndRotation(Location, Rotation);
}

FIntPoint ACoverActor::RotateOffset(FIntPoint Offset) const
{
	// Positive yaw turns +X towards +Y.
	const int32 QuarterTurns = ((FMath::RoundToInt(GetActorRotation().Yaw / 90.f) % 4) + 4) % 4;
	switch (QuarterTurns)
	{
	case 1:  return FIntPoint(-Offset.Y, Offset.X);
	case 2:  return FIntPoint(-Offset.X, -Offset.Y);
	case 3:  return FIntPoint(Offset.Y, -Offset.X);
	default: return Offset;
	}
}

TArray<FIntPoint> ACoverActor::GetOccupiedCells() const
{
	TArray<FIntPoint> Result;
	AGridManager* Grid = AGridManager::Find(this);
	FIntPoint Anchor;
	if (!Grid || !Grid->WorldToCell(GetActorLocation(), Anchor))
	{
		return Result;
	}
	TArray<FCoverCellSpec> Cells;
	GenerateCells(Cells);
	for (const FCoverCellSpec& Cell : Cells)
	{
		Result.Add(Anchor + RotateOffset(Cell.Offset));
	}
	return Result;
}

void ACoverActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Re-assert collision on the cell meshes: actors saved with older settings keep them otherwise.
	TInlineComponentArray<UInstancedStaticMeshComponent*> Sets;
	GetComponents(Sets);
	for (UInstancedStaticMeshComponent* Set : Sets)
	{
		if (Set->ComponentHasTag(CoverVisualTag))
		{
			MakeSolid(Set);
		}
	}

	// Register before any BeginPlay runs, so soldiers snapping to a free cell already see the cover.
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	AGridManager* Grid = AGridManager::Find(this);
	FIntPoint Anchor;
	if (!Grid || !Grid->WorldToCell(GetActorLocation(), Anchor))
	{
		return;
	}
	RegisteredGrid = Grid;

	// The piece's local X (its length) turned into the world: tells the grid which way its slabs run.
	const bool bRunsAlongX = RotateOffset(FIntPoint(1, 0)).X != 0;

	TArray<FCoverCellSpec> Cells;
	GenerateCells(Cells);
	for (const FCoverCellSpec& Cell : Cells)
	{
		Grid->RegisterCoverCell(Anchor + RotateOffset(Cell.Offset), Cell.Type, Cell.bBlocksLineOfSight, this, bRunsAlongX, /*bPassable*/ false, Cell.bFillsCell);
	}
}

void ACoverActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (RegisteredGrid)
	{
		RegisteredGrid->UnregisterCover(this);
		RegisteredGrid = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ACoverActor::GenerateCells(TArray<FCoverCellSpec>& OutCells) const
{
	const int32 First = -(Length - 1) / 2;
	for (int32 i = 0; i < Length; ++i)
	{
		FCoverCellSpec& Cell = OutCells.AddDefaulted_GetRef();
		Cell.Offset = FIntPoint(First + i, 0);
		Cell.Type = CoverType;
		Cell.bBlocksLineOfSight = bBlocksLineOfSight;
	}
}

UStaticMesh* ACoverActor::PickMesh(const TArray<TSoftObjectPtr<UStaticMesh>>& Variants, uint32 Seed)
{
	if (Variants.Num() == 0)
	{
		return nullptr;
	}
	// Start at the seeded variant and fall through to the next loadable one.
	for (int32 i = 0; i < Variants.Num(); ++i)
	{
		if (UStaticMesh* Mesh = Variants[(Seed + i) % Variants.Num()].LoadSynchronous())
		{
			return Mesh;
		}
	}
	return nullptr;
}

uint32 ACoverActor::GetCellSeed(const FVector& LocalLocation, uint32 Salt) const
{
	const FVector World = GetActorTransform().TransformPosition(LocalLocation);
	const int32 X = FMath::RoundToInt(World.X / 100.f);
	const int32 Y = FMath::RoundToInt(World.Y / 100.f);
	uint32 H = HashCombine(HashCombine(GetTypeHash(X), GetTypeHash(Y * 7919)), Salt);
	// Murmur finaliser: neighbouring cells get unrelated values.
	H ^= H >> 16; H *= 0x85EBCA6Bu; H ^= H >> 13; H *= 0xC2B2AE35u; H ^= H >> 16;
	return H;
}

UInstancedStaticMeshComponent* ACoverActor::GetMeshSet(UStaticMesh* Mesh, FName Tag, bool bSolid, bool bConstructionScript)
{
	for (UInstancedStaticMeshComponent* Set : MeshSets)
	{
		if (Set && Set->GetStaticMesh() == Mesh && Set->ComponentHasTag(Tag))
		{
			return Set;
		}
	}
	UInstancedStaticMeshComponent* Set = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transactional);
	if (bConstructionScript)
	{
		// Rebuilt with the actor (like a Blueprint construction script component) and saved with the level.
		Set->CreationMethod = EComponentCreationMethod::UserConstructionScript;
	}
	Set->ComponentTags.Add(Tag);
	Set->SetupAttachment(RootComponent);
	Set->SetStaticMesh(Mesh);
	Set->SetMobility(EComponentMobility::Movable);
	Set->SetCanEverAffectNavigation(false);
	if (bSolid)
	{
		MakeSolid(Set);
	}
	else
	{
		Set->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Set->AlwaysLoadOnServer = true; // A dedicated server skips loading no-collision components otherwise (null on the server).
	}
	Set->RegisterComponent();
	MeshSets.Add(Set);
	return Set;
}

void ACoverActor::AddCellVisual(const FCoverCellSpec& Cell, const FVector& Location, float ExtendBack, float ExtendFront)
{
	// Wall slabs run along local X; ends can be stretched or trimmed so neighbouring pieces meet at corners.
	const float SlabLength = 100.f + ExtendBack + ExtendFront;
	const FVector Slab = Location + FVector((ExtendFront - ExtendBack) * 0.5f, 0.f, 0.f);
	const uint32 Seed = GetCellSeed(Location, static_cast<uint32>(Cell.Type));
	const bool bFlip = bRandomFlip && (Seed & 0x100u) != 0;
	const FRotator Flip(0.f, bFlip ? 180.f : 0.f, 0.f);

	UStaticMesh* Mesh = nullptr;
	FTransform Xf;
	switch (Cell.Type)
	{
	case ECoverType::Soft:
		Mesh = PickMesh(HayMeshes, Seed);
		Xf = FTransform(Flip, Location);
		break;

	case ECoverType::LowWall:
		Mesh = PickMesh(LowWallMeshes, Seed);
		Xf = FTransform(Flip, Slab, FVector(SlabLength / 100.f, 1.f, 1.f));
		break;

	case ECoverType::WallCorner:
		Mesh = PickMesh(WallMeshes, Seed);
		Xf = FTransform(Flip, Slab, FVector(SlabLength / 100.f, 1.f, 1.f));
		break;

	case ECoverType::Window:
		// Not flipped: keeps the shutters on the same face all along a building.
		Mesh = PickMesh(WindowMeshes, Seed);
		Xf = FTransform(FRotator::ZeroRotator, Slab, FVector(SlabLength / 100.f, 1.f, 1.f));
		break;

	case ECoverType::Tree:
	{
		Mesh = PickMesh(TreeMeshes, Seed);
		const float Yaw = static_cast<float>((Seed >> 9) % 360u);
		const float Scale = 1.f + TreeScaleJitter * (static_cast<float>((Seed >> 20) % 1001u) / 500.f - 1.f);
		Xf = FTransform(FRotator(0.f, Yaw, 0.f), Location, FVector(Scale));
		break;
	}

	default:
		return;
	}

	if (!Mesh)
	{
		UE_LOG(LogCoverActor, Warning, TEXT("%s: no loadable mesh for %s cells; check its Cover|Visuals mesh lists."),
			*GetName(), UCoverData::GetCoverName(Cell.Type));
		return;
	}
	GetMeshSet(Mesh, CoverVisualTag, /*bSolid*/ true, /*bConstructionScript*/ true)->AddInstance(Xf);
}

// ------------------------------------------------------------------------------------------------ Pieces

AHayBaleCover::AHayBaleCover()
{
	CoverType = ECoverType::Soft;
	Length = 2;
}

ALowWallCover::ALowWallCover()
{
	CoverType = ECoverType::LowWall;
	Length = 3;
}

AWallCover::AWallCover()
{
	CoverType = ECoverType::WallCorner;
	bBlocksLineOfSight = true;
	Length = 4;
}

AWindowWallCover::AWindowWallCover()
{
	CoverType = ECoverType::Window;
	Length = 3;
}

void AWindowWallCover::GenerateCells(TArray<FCoverCellSpec>& OutCells) const
{
	Super::GenerateCells(OutCells);
	for (int32 i = 0; i < OutCells.Num(); ++i)
	{
		// Solid wall at both ends, windows every WindowSpacing cells in between.
		const bool bWindow = i > 0 && i < OutCells.Num() - 1 && (i % WindowSpacing) == 1 % WindowSpacing;
		OutCells[i].Type = bWindow ? ECoverType::Window : ECoverType::WallCorner;
		OutCells[i].bBlocksLineOfSight = !bWindow;
	}
}

ATreeCover::ATreeCover()
{
	CoverType = ECoverType::Tree;
	Length = 1;
}

// ------------------------------------------------------------------------------------------------ Props (GDD §9.5)

APropCover::APropCover()
{
	CoverType = ECoverType::LowWall;
	Length = 1;
	PropMeshes = { EnvMesh(TEXT("LowCover"), TEXT("SM_CrateStack")) };
}

void APropCover::GenerateCells(TArray<FCoverCellSpec>& OutCells) const
{
	// Centred on the actor like the base row: an even size puts the block's centre on a cell edge.
	const int32 FirstX = -(Length - 1) / 2;
	const int32 FirstY = -(Width - 1) / 2;
	for (int32 X = 0; X < Length; ++X)
	{
		for (int32 Y = 0; Y < Width; ++Y)
		{
			FCoverCellSpec& Cell = OutCells.AddDefaulted_GetRef();
			Cell.Offset = FIntPoint(FirstX + X, FirstY + Y);
			Cell.Type = CoverType;
			Cell.bBlocksLineOfSight = bBlocksLineOfSight;
			Cell.bFillsCell = true;
		}
	}
}

void APropCover::AddPieceVisual(const TArray<FCoverCellSpec>& Cells)
{
	if (Cells.Num() == 0)
	{
		return;
	}
	FVector Centre = FVector::ZeroVector;
	for (const FCoverCellSpec& Cell : Cells)
	{
		Centre += FVector(Cell.Offset.X * 100.f, Cell.Offset.Y * 100.f, 0.f);
	}
	Centre /= Cells.Num();

	const uint32 Seed = GetCellSeed(Centre, 0x9E0Bu);
	float Yaw = 0.f;
	if (bRandomFlip)
	{
		Yaw = (Length == Width) ? 90.f * static_cast<float>((Seed >> 8) % 4u) : ((Seed & 0x100u) ? 180.f : 0.f);
	}
	UStaticMesh* Mesh = PickMesh(PropMeshes, Seed);
	if (!Mesh)
	{
		UE_LOG(LogCoverActor, Warning, TEXT("%s: no loadable prop mesh; check Cover|Visuals > Prop Meshes."), *GetName());
		return;
	}
	GetMeshSet(Mesh, CoverVisualTag, /*bSolid*/ true, /*bConstructionScript*/ true)->AddInstance(FTransform(FRotator(0.f, Yaw, 0.f), Centre));
}

AWellCover::AWellCover()
{
	CoverType = ECoverType::LowWall;
	PropMeshes = { EnvMesh(TEXT("Props"), TEXT("SM_Well")) };
}

AWaterTroughCover::AWaterTroughCover()
{
	CoverType = ECoverType::LowWall;
	Length = 2;
	PropMeshes = { EnvMesh(TEXT("Props"), TEXT("SM_WaterTrough")) };
}

ACrateStackCover::ACrateStackCover()
{
	CoverType = ECoverType::LowWall;
	PropMeshes = { EnvMesh(TEXT("LowCover"), TEXT("SM_CrateStack")) };
}

ABarrelCover::ABarrelCover()
{
	CoverType = ECoverType::LowWall;
	PropMeshes = { EnvMesh(TEXT("LowCover"), TEXT("SM_BarrelGroup")) };
}

AWoodpileCover::AWoodpileCover()
{
	CoverType = ECoverType::LowWall;
	PropMeshes = { EnvMesh(TEXT("LowCover"), TEXT("SM_Woodpile")) };
}

AHayCartCover::AHayCartCover()
{
	CoverType = ECoverType::Soft;
	Length = 3;
	Width = 2;
	PropMeshes = { EnvMesh(TEXT("Vehicles"), TEXT("SM_Cart_Hay")) };
}

ACarCover::ACarCover()
{
	CoverType = ECoverType::LowWall;
	Length = 4;
	Width = 2;
	PropMeshes = { EnvMesh(TEXT("Vehicles"), TEXT("SM_Car_Utility")) };
}

ATruckCover::ATruckCover()
{
	CoverType = ECoverType::WallCorner;
	bBlocksLineOfSight = true;
	Length = 6;
	Width = 2;
	PropMeshes = { EnvMesh(TEXT("Vehicles"), TEXT("SM_Truck_Cargo")) };
}

ATankWreckCover::ATankWreckCover()
{
	CoverType = ECoverType::WallCorner;
	bBlocksLineOfSight = true;
	Length = 6;
	Width = 3;
	PropMeshes = { EnvMesh(TEXT("Vehicles"), TEXT("SM_Tank_Wreck")) };
}

ASandbagCover::ASandbagCover()
{
	LowWallMeshes = { EnvMesh(TEXT("LowCover"), TEXT("SM_Sandbags")) };
}
