#include "BreachableWall.h"

#include "GridManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogBreachableWall, Log, All);

namespace
{
	// Match the full wall meshes (SM_Wall_*, 2.6 m x 0.3 m) (cm).
	constexpr float BreachWallHeight = 260.f;

	const FName RubbleTag(TEXT("BreachRubble"));

	TSoftObjectPtr<UStaticMesh> EnvMesh(const TCHAR* Folder, const TCHAR* Name)
	{
		return TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(FString::Printf(TEXT("/Game/Environment/%s/%s.%s"), Folder, Name, Name)));
	}
	constexpr float BreachWallThickness = 30.f;

	UInstancedStaticMeshComponent* MakeCosmeticSet(AActor* Owner, const TCHAR* Name, UStaticMesh* Mesh, USceneComponent* Parent)
	{
		UInstancedStaticMeshComponent* Set = Owner->CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Set->SetupAttachment(Parent);
		Set->SetStaticMesh(Mesh);
		// Cosmetic only: never blocks movement, shots, the cursor or the camera (GDD §9.4: rubble never affects play).
		Set->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Set->AlwaysLoadOnServer = true; // A dedicated server skips loading no-collision components otherwise (null on the server).
		Set->SetCanEverAffectNavigation(false);
		Set->SetMobility(EComponentMobility::Movable);
		return Set;
	}

	void TintSet(UInstancedStaticMeshComponent* Set, UObject* Outer, const FLinearColor& Color)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!Set || !Base)
		{
			return;
		}
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Set->GetMaterial(0));
		if (!MID || MID->Parent != Base)
		{
			MID = UMaterialInstanceDynamic::Create(Base, Outer);
			Set->SetMaterial(0, MID);
		}
		MID->SetVectorParameterValue(TEXT("Color"), Color);
	}

	/** World transform for an engine cube (100 cm, centred) of Size cm centred at Centre, X along Dir and Z towards Up. */
	FTransform BoxTransform(const FVector& Centre, const FVector& Dir, const FVector& Up, const FVector& Size)
	{
		return FTransform(FRotationMatrix::MakeFromXZ(Dir, Up).ToQuat(), Centre, Size / 100.f);
	}

	uint32 CellSeed(FIntPoint Cell, uint32 Salt)
	{
		return HashCombine(HashCombine(GetTypeHash(Cell.X), GetTypeHash(Cell.Y * 7919)), Salt);
	}
}

ABreachableWall::ABreachableWall()
{
	PrimaryActorTick.bCanEverTick = true; // Only while the rubble flies.
	PrimaryActorTick.bStartWithTickEnabled = false;

	bReplicates = true;
	bAlwaysRelevant = true; // Map state: every client needs it, whatever its camera.
	SetNetUpdateFrequency(2.f); // Changes push a ForceNetUpdate.

	CoverType = ECoverType::WallCorner;
	bBlocksLineOfSight = true;
	Length = 3;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	PlateShapes = MakeCosmeticSet(this, TEXT("PlateShapes"), CubeMesh.Object, RootComponent);
	DustShapes = MakeCosmeticSet(this, TEXT("DustShapes"), SphereMesh.Object, RootComponent);
	DustShapes->SetCastShadow(false);
	DamageShapes = MakeCosmeticSet(this, TEXT("DamageShapes"), nullptr, RootComponent);
	RubbleShapes = MakeCosmeticSet(this, TEXT("RubbleShapes"), nullptr, RootComponent);
	RubbleDarkShapes = MakeCosmeticSet(this, TEXT("RubbleDarkShapes"), nullptr, RootComponent);

	// Darker, cracked plaster with a notch knocked out of the top (GDD §9.4 "spotting them").
	WallMeshes = { EnvMesh(TEXT("Walls"), TEXT("SM_Wall_Plaster_Weakened")), EnvMesh(TEXT("Walls"), TEXT("SM_Wall_Plaster_Weakened_B")) };
	StubMeshes = { EnvMesh(TEXT("Rubble"), TEXT("SM_Rubble_WallStub_A")), EnvMesh(TEXT("Rubble"), TEXT("SM_Rubble_WallStub_B")),
		EnvMesh(TEXT("Rubble"), TEXT("SM_Rubble_WallStub_C")) };
	ChunkMeshes = { EnvMesh(TEXT("Rubble"), TEXT("SM_Rubble_Chunk_A")), EnvMesh(TEXT("Rubble"), TEXT("SM_Rubble_Chunk_B")),
		EnvMesh(TEXT("Rubble"), TEXT("SM_Rubble_Chunk_C")), EnvMesh(TEXT("Rubble"), TEXT("SM_Rubble_Chunk_D")) };
}

void ABreachableWall::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABreachableWall, bBreached);
	DOREPLIFETIME(ABreachableWall, bReinforced);
	DOREPLIFETIME(ABreachableWall, BreachOrigin);
	DOREPLIFETIME(ABreachableWall, BreachServerTime);
}

// ------------------------------------------------------------------------------------------------ Geometry

bool ABreachableWall::RunsAlongX() const
{
	const int32 QuarterTurns = ((FMath::RoundToInt(GetActorRotation().Yaw / 90.f) % 4) + 4) % 4;
	return QuarterTurns % 2 == 0;
}

FIntPoint ABreachableWall::FaceNormal() const
{
	return RunsAlongX() ? FIntPoint(0, 1) : FIntPoint(1, 0);
}

bool ABreachableWall::GetFaceCellFor(FIntPoint StandCell, FIntPoint& OutWallCell) const
{
	OutWallCell = FIntPoint::ZeroValue;
	if (bBreached)
	{
		return false;
	}
	const AGridManager* Grid = AGridManager::Find(this);
	if (!Grid || !Grid->IsInBounds(StandCell) || Grid->IsCellBlockedByCover(StandCell))
	{
		return false;
	}
	const FIntPoint N = FaceNormal();
	for (const FIntPoint& Cell : GetOccupiedCells())
	{
		if (StandCell == Cell + N || StandCell == Cell - N)
		{
			OutWallCell = Cell;
			return true;
		}
	}
	return false;
}

ABreachableWall* ABreachableWall::FindAtCell(const UObject* WorldContext, FIntPoint Cell)
{
	if (const AGridManager* Grid = AGridManager::Find(WorldContext))
	{
		// Breached walls keep owning their (rubble) cells.
		if (const FGridCoverCell* Cover = Grid->GetCoverCell(Cell))
		{
			if (ABreachableWall* Wall = Cast<ABreachableWall>(Cover->Owner.Get()))
			{
				return Wall;
			}
		}
		if (Grid->GetWorld() && Grid->GetWorld()->IsGameWorld())
		{
			return nullptr; // In play the grid is authoritative.
		}
	}
	// Editor (no registered cover): check the walls themselves.
	TArray<ABreachableWall*> Walls;
	GetAll(WorldContext, Walls);
	for (ABreachableWall* Wall : Walls)
	{
		if (Wall->GetOccupiedCells().Contains(Cell))
		{
			return Wall;
		}
	}
	return nullptr;
}

void ABreachableWall::GetAll(const UObject* WorldContext, TArray<ABreachableWall*>& OutWalls)
{
	OutWalls.Reset();
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	for (TActorIterator<ABreachableWall> It(World); It; ++It)
	{
		OutWalls.Add(*It);
	}
}

// ------------------------------------------------------------------------------------------------ State

void ABreachableWall::SetReinforced()
{
	if (!HasAuthority() || bReinforced || bBreached)
	{
		return;
	}
	bReinforced = true;
	ForceNetUpdate();
	ApplyReinforcedVisual();
}

bool ABreachableWall::Breach(const FVector& BlastOrigin)
{
	if (!HasAuthority() || bBreached || bReinforced)
	{
		return false;
	}
	// Whole cm, as FVector_NetQuantize sends it, so every machine scatters the rubble identically.
	BreachOrigin = FVector(FMath::RoundToFloat(BlastOrigin.X), FMath::RoundToFloat(BlastOrigin.Y), FMath::RoundToFloat(BlastOrigin.Z));
	BreachServerTime = GetWorld()->GetTimeSeconds();
	bBreached = true;
	ForceNetUpdate();
	ApplyBreached(true);
	OnBreached.Broadcast(this);
	UE_LOG(LogBreachableWall, Log, TEXT("%s breached (%d cells)."), *GetName(), GetOccupiedCells().Num());
	return true;
}

void ABreachableWall::OnRep_Breached()
{
	if (!bBreached || bBreachApplied)
	{
		return;
	}
	// Seeing it happen: animate. Joining (or catching up) long after: just lay the rubble down.
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const float Now = GS ? GS->GetServerWorldTimeSeconds() : BreachServerTime;
	ApplyBreached(Now - BreachServerTime <= LateJoinSkipAnimationAfter);
	OnBreached.Broadcast(this);
}

void ABreachableWall::OnRep_Reinforced()
{
	if (bReinforced)
	{
		ApplyReinforcedVisual();
	}
}

void ABreachableWall::BeginPlay()
{
	Super::BeginPlay();
	if (bBreached && !bBreachApplied)
	{
		ApplyBreached(false);
	}
	else if (bReinforced)
	{
		ApplyReinforcedVisual();
	}
}

void ABreachableWall::ApplyBreached(bool bAnimate)
{
	if (bBreachApplied)
	{
		return;
	}
	bBreachApplied = true;

	// ---- Gameplay (every machine, so client-side previews match the server): the wall cells become rubble that
	// can be walked over and gives low-wall cover / sight / grenade rules (GDD §9.4).
	if (AGridManager* Grid = AGridManager::Find(this))
	{
		const bool bAlongX = RunsAlongX();
		Grid->UnregisterCover(this);
		for (const FIntPoint& Cell : GetOccupiedCells())
		{
			Grid->RegisterCoverCell(Cell, ECoverType::LowWall, /*bBlocksLineOfSight*/ false, this, bAlongX, /*bPassable*/ true);
		}
		Grid->NotifyCoverChanged();
	}

	// ---- Visuals: the wall goes, a broken stub and rubble heap stay, chunks fly.
	TInlineComponentArray<UInstancedStaticMeshComponent*> Sets;
	GetComponents(Sets);
	for (UInstancedStaticMeshComponent* Set : Sets)
	{
		if (Set == PlateShapes || Set->ComponentHasTag(CoverVisualTag))
		{
			Set->ClearInstances();
			Set->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		return; // Nothing to render.
	}
	BuildRubble(bAnimate);
}

void ABreachableWall::BuildRubble(bool bAnimate)
{
	const AGridManager* Grid = AGridManager::Find(this);
	if (!Grid)
	{
		return;
	}
	TintSet(DustShapes, this, DustColor);
	DustShapes->ClearInstances();
	FlyingChunks.Reset();
	DustPuffs.Reset();

	// Cosmetic sets (no collision), made at runtime: one per stub / chunk mesh.
	TArray<UInstancedStaticMeshComponent*> StubSets;
	TArray<UInstancedStaticMeshComponent*> ChunkSets;
	for (const TArray<TSoftObjectPtr<UStaticMesh>>* List : { &StubMeshes, &ChunkMeshes })
	{
		for (const TSoftObjectPtr<UStaticMesh>& Ref : *List)
		{
			if (UStaticMesh* Mesh = Ref.LoadSynchronous())
			{
				UInstancedStaticMeshComponent* Set = GetMeshSet(Mesh, RubbleTag, /*bSolid*/ false, /*bConstructionScript*/ false);
				Set->ClearInstances();
				(List == &StubMeshes ? StubSets : ChunkSets).Add(Set);
			}
		}
	}
	if (ChunkSets.Num() == 0)
	{
		UE_LOG(LogBreachableWall, Warning, TEXT("%s: no loadable rubble chunk meshes; breach shows no debris."), *GetName());
	}
	auto PickChunkSet = [&ChunkSets](FRandomStream& R) -> UInstancedStaticMeshComponent*
	{
		return ChunkSets.Num() > 0 ? ChunkSets[R.RandRange(0, ChunkSets.Num() - 1)] : nullptr;
	};

	const FVector Along = RunsAlongX() ? FVector::XAxisVector : FVector::YAxisVector;
	const FVector Normal = RunsAlongX() ? FVector::YAxisVector : FVector::XAxisVector;
	const float Cell = Grid->CellSize;
	const float Scale = Cell / 100.f;
	const float MinSize = FMath::Min(ChunkSize.X, ChunkSize.Y) * Scale;
	const float MaxSize = FMath::Max(ChunkSize.X, ChunkSize.Y) * Scale;
	const float Mound = MoundHeight * Scale;

	auto RandomRot = [](FRandomStream& R, float Tilt)
	{
		return FRotator(R.FRandRange(-Tilt, Tilt), R.FRandRange(0.f, 360.f), R.FRandRange(-Tilt, Tilt)).Quaternion();
	};
	auto RandomSize = [&](FRandomStream& R)
	{
		return FVector(R.FRandRange(MinSize, MaxSize), R.FRandRange(MinSize, MaxSize), R.FRandRange(MinSize, MaxSize) * 0.7f);
	};

	for (const FIntPoint& WallCell : GetOccupiedCells())
	{
		FRandomStream R(CellSeed(WallCell, 0xB4EAC4u));
		const FVector Centre = Grid->CellToWorld(WallCell);

		// Away from the charge; a charge right on the wall (or no origin) throws both ways along the face normal.
		FVector Away = FVector(Centre - FVector(BreachOrigin)).GetSafeNormal2D();
		const bool bBothWays = Away.IsNearlyZero() || FVector::Dist2D(Centre, BreachOrigin) < Cell * 0.3f;
		if (bBothWays)
		{
			Away = Normal;
		}

		// Broken stub of the wall along its old line, with its masonry heap (mesh stays under low-wall height).
		if (StubSets.Num() > 0)
		{
			UInstancedStaticMeshComponent* Stub = StubSets[R.RandRange(0, StubSets.Num() - 1)];
			const FVector StubAlong = R.FRand() < 0.5f ? Along : -Along;
			Stub->AddInstance(FTransform(FRotationMatrix::MakeFromXZ(StubAlong, FVector::UpVector).ToQuat(), Centre, FVector(Scale)), true);
		}

		// Chunks dropping onto the cell from where the wall stood.
		for (int32 i = 0; i < MoundChunksPerCell; ++i)
		{
			UInstancedStaticMeshComponent* Set = PickChunkSet(R);
			const FVector Size = RandomSize(R);
			if (!Set)
			{
				continue;
			}
			const FVector Offset = Along * R.FRandRange(-0.45f, 0.45f) * Cell + Normal * R.FRandRange(-0.42f, 0.42f) * Cell;
			// Higher towards the old wall line, lower at the cell's edges.
			const float Ridge = 1.f - FMath::Abs(FVector::DotProduct(Offset, Normal)) / (0.5f * Cell);
			const float Z = FMath::Clamp(R.FRandRange(0.f, Mound * Ridge) - Size.Z * 0.5f, 0.f, FMath::Max(0.f, Mound - Size.Z)) + Size.Z * 0.5f;
			const FTransform To(RandomRot(R, 25.f), Centre + Offset + FVector(0.f, 0.f, Z), Size / 100.f);
			const int32 Index = Set->AddInstance(To, true);
			if (bAnimate)
			{
				// Drops from where the wall stood.
				FFlyingChunk& Chunk = FlyingChunks.AddDefaulted_GetRef();
				Chunk.Set = Set;
				Chunk.Instance = Index;
				Chunk.To = To;
				Chunk.From = FTransform(RandomRot(R, 180.f), Centre + Along * R.FRandRange(-0.45f, 0.45f) * Cell + FVector(0.f, 0.f, R.FRandRange(0.3f, 0.9f) * BreachWallHeight * Scale), Size / 100.f);
				Chunk.Spin = FRotator(R.FRandRange(-360.f, 360.f), R.FRandRange(-360.f, 360.f), 0.f);
				Chunk.Delay = R.FRandRange(0.f, 0.12f);
			}
		}

		// Thrown chunks, settling on the nearby cells.
		for (int32 i = 0; i < ScatterChunksPerCell; ++i)
		{
			UInstancedStaticMeshComponent* Set = PickChunkSet(R);
			const FVector Size = RandomSize(R) * R.FRandRange(0.6f, 1.f);
			if (!Set)
			{
				continue;
			}
			FVector Dir = Away;
			if (bBothWays && R.FRand() < 0.5f)
			{
				Dir = -Dir;
			}
			Dir = Dir.RotateAngleAxis(R.FRandRange(-ScatterSpreadAngle, ScatterSpreadAngle), FVector::UpVector);
			// A few fall back on the blast side, close to the wall.
			const bool bBack = !bBothWays && R.FRand() < 0.15f;
			const float Dist = bBack ? R.FRandRange(0.3f, 0.9f) * Cell : FMath::Lerp(0.6f * Cell, ScatterRadius * Scale, FMath::Sqrt(R.FRand()));
			const FVector Start = Centre + Along * R.FRandRange(-0.45f, 0.45f) * Cell;
			const FVector Land = Start + (bBack ? -Dir : Dir) * Dist;
			const FTransform To(RandomRot(R, 30.f), FVector(Land.X, Land.Y, Centre.Z + Size.Z * 0.5f), Size / 100.f);
			const int32 Index = Set->AddInstance(To, true);
			if (bAnimate)
			{
				FFlyingChunk& Chunk = FlyingChunks.AddDefaulted_GetRef();
				Chunk.Set = Set;
				Chunk.Instance = Index;
				Chunk.To = To;
				Chunk.From = FTransform(RandomRot(R, 180.f), Start + FVector(0.f, 0.f, R.FRandRange(0.15f, 0.85f) * BreachWallHeight * Scale), Size / 100.f);
				Chunk.Spin = FRotator(R.FRandRange(-720.f, 720.f), R.FRandRange(-720.f, 720.f), R.FRandRange(-360.f, 360.f));
				Chunk.Delay = R.FRandRange(0.f, 0.06f);
			}
		}

		// Dust.
		if (bAnimate)
		{
			for (int32 i = 0; i < DustPuffsPerCell; ++i)
			{
				FDustPuff& Puff = DustPuffs.AddDefaulted_GetRef();
				Puff.Location = Centre + Along * R.FRandRange(-0.5f, 0.5f) * Cell + FVector(0.f, 0.f, R.FRandRange(0.2f, 0.8f) * BreachWallHeight * Scale);
				FVector DriftDir = (bBothWays && R.FRand() < 0.5f) ? -Away : Away;
				Puff.Drift = DriftDir.RotateAngleAxis(R.FRandRange(-60.f, 60.f), FVector::UpVector) * R.FRandRange(0.5f, 1.4f) * Cell + FVector(0.f, 0.f, R.FRandRange(0.3f, 1.0f) * Cell);
				Puff.Size = R.FRandRange(1.2f, 2.2f) * Cell;
				Puff.Delay = R.FRandRange(0.f, 0.15f);
				Puff.Instance = DustShapes->AddInstance(FTransform(FQuat::Identity, Puff.Location, FVector(0.01f)), true);
			}
		}
	}

	if (bAnimate && (FlyingChunks.Num() > 0 || DustPuffs.Num() > 0))
	{
		AnimTime = 0.f;
		SetActorTickEnabled(true);
		Tick(0.f); // Put the chunks at their start right away.
	}
}

void ABreachableWall::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AnimTime += DeltaSeconds;

	bool bBusy = false;
	for (const FFlyingChunk& Chunk : FlyingChunks)
	{
		const float Alpha = FMath::Clamp((AnimTime - Chunk.Delay) / ScatterDuration, 0.f, 1.f);
		bBusy |= Alpha < 1.f;
		const float Dist = FVector::Dist2D(Chunk.From.GetLocation(), Chunk.To.GetLocation());
		const float Arc = ScatterArcHeight * FMath::Clamp(Dist / FMath::Max(1.f, ScatterRadius), 0.f, 1.f) * 4.f * Alpha * (1.f - Alpha);
		// Ease-in fall (gravity-ish) on Z, linear across the ground.
		FVector Pos = FMath::Lerp(Chunk.From.GetLocation(), Chunk.To.GetLocation(), Alpha);
		Pos.Z = FMath::Lerp(Chunk.From.GetLocation().Z, Chunk.To.GetLocation().Z, Alpha * Alpha) + Arc;
		const FQuat Rot = Chunk.To.GetRotation() * (Chunk.Spin * (1.f - Alpha)).Quaternion();
		Chunk.Set->UpdateInstanceTransform(Chunk.Instance, FTransform(Rot, Pos, Chunk.To.GetScale3D()), true, false, true);
	}
	for (const FDustPuff& Puff : DustPuffs)
	{
		const float Alpha = FMath::Clamp((AnimTime - Puff.Delay) / DustDuration, 0.f, 1.f);
		bBusy |= Alpha < 1.f;
		// Billows out quickly, then thins away.
		const float Grow = FMath::Clamp(Alpha / 0.25f, 0.f, 1.f);
		const float Fade = 1.f - FMath::Clamp((Alpha - 0.35f) / 0.65f, 0.f, 1.f);
		const float Size = FMath::Max(0.01f, Puff.Size * FMath::Sqrt(Grow) * Fade);
		const FVector Pos = Puff.Location + Puff.Drift * (1.f - FMath::Square(1.f - Alpha));
		DustShapes->UpdateInstanceTransform(Puff.Instance, FTransform(FQuat::Identity, Pos, FVector(Size / 100.f)), true, false, true);
	}
	TSet<UInstancedStaticMeshComponent*, DefaultKeyFuncs<UInstancedStaticMeshComponent*>, TInlineSetAllocator<8>> Dirty;
	Dirty.Add(DustShapes);
	for (const FFlyingChunk& Chunk : FlyingChunks)
	{
		Dirty.Add(Chunk.Set);
	}
	for (UInstancedStaticMeshComponent* Set : Dirty)
	{
		Set->MarkRenderStateDirty();
	}

	if (!bBusy && AnimTime > 0.f)
	{
		FlyingChunks.Reset();
		DustPuffs.Reset();
		DustShapes->ClearInstances();
		SetActorTickEnabled(false);
	}
}

// ------------------------------------------------------------------------------------------------ Intact visuals

void ABreachableWall::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform); // Builds the full wall from WallMeshes (the weakened wall mesh).

	// Editor / spawn state is always intact; runtime state (breach, reinforcement) is applied on top in play.
	TintSet(PlateShapes, this, PlateColor);
	for (UInstancedStaticMeshComponent* Set : { DamageShapes.Get(), PlateShapes.Get(), RubbleShapes.Get(), RubbleDarkShapes.Get(), DustShapes.Get() })
	{
		Set->ClearInstances();
	}
}

void ABreachableWall::ApplyReinforcedVisual()
{
	if (bReinforcedVisualApplied || bBreached)
	{
		return;
	}
	bReinforcedVisualApplied = true;
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const AGridManager* Grid = AGridManager::Find(this);
	if (!Grid)
	{
		return;
	}
	TintSet(PlateShapes, this, PlateColor);
	const float Cell = Grid->CellSize;
	const float Scale = Cell / 100.f;
	const FVector Along = RunsAlongX() ? FVector::XAxisVector : FVector::YAxisVector;
	const FVector Normal = RunsAlongX() ? FVector::YAxisVector : FVector::XAxisVector;
	const float HalfThick = BreachWallThickness * 0.5f * Scale;

	// Hide the cracks behind steel: a plate on each face of every cell, with a beam raking up to it.
	for (const FIntPoint& WallCell : GetOccupiedCells())
	{
		const FVector Centre = Grid->CellToWorld(WallCell);
		for (const float Side : { 1.f, -1.f })
		{
			const FVector Face = Normal * Side;
			const float PlateDepth = 5.f * Scale;
			PlateShapes->AddInstance(BoxTransform(Centre + Face * (HalfThick + PlateDepth * 0.5f) + FVector(0.f, 0.f, 120.f * Scale),
				Along, Face, FVector(Cell * 0.9f, 170.f * Scale, PlateDepth)), true);
			// Bolt heads.
			for (const float BX : { -0.35f, 0.35f })
			{
				for (const float BZ : { 50.f, 190.f })
				{
					PlateShapes->AddInstance(BoxTransform(Centre + Face * (HalfThick + PlateDepth + 2.f * Scale) + Along * BX * Cell + FVector(0.f, 0.f, BZ * Scale),
						Along, Face, FVector(6.f, 6.f, 4.f) * Scale), true);
				}
			}
			// Raking brace from the ground ~60 cm out up to 2 m on the face.
			const FVector Foot = Centre + Face * (HalfThick + 60.f * Scale);
			const FVector Head = Centre + Face * (HalfThick + PlateDepth + 6.f * Scale) + FVector(0.f, 0.f, 200.f * Scale);
			const FVector Dir = (Head - Foot).GetSafeNormal();
			const FVector Up = FVector::CrossProduct(Dir, Along).GetSafeNormal();
			PlateShapes->AddInstance(BoxTransform((Foot + Head) * 0.5f, Dir, Up.IsNearlyZero() ? Face : Up, FVector(FVector::Dist(Foot, Head), 12.f * Scale, 12.f * Scale)), true);
		}
	}
}

// ------------------------------------------------------------------------------------------------ Debug commands

namespace
{
	bool ParseCell(const TArray<FString>& Args, int32 First, FIntPoint& OutCell)
	{
		if (Args.Num() < First + 2)
		{
			return false;
		}
		OutCell = FIntPoint(FCString::Atoi(*Args[First]), FCString::Atoi(*Args[First + 1]));
		return true;
	}

	bool CheckServer(UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogBreachableWall, Warning, TEXT("Breachable wall commands only work on the server (or in a standalone / listen-server PIE world)."));
			return false;
		}
		return true;
	}

	/** Default blast origin: 1.5 cells off the wall's face, on the -normal side. */
	FVector DefaultBlastOrigin(const ABreachableWall* Wall, FIntPoint Cell)
	{
		const AGridManager* Grid = AGridManager::Find(Wall);
		const FVector Centre = Grid ? Grid->CellToWorld(Cell) : Wall->GetActorLocation();
		const FVector Normal = Wall->RunsAlongX() ? FVector::YAxisVector : FVector::XAxisVector;
		return Centre - Normal * 150.f + FVector(0.f, 0.f, 50.f);
	}

	FAutoConsoleCommandWithWorldAndArgs CmdBreachWallAt(
		TEXT("squad.BreachWallAt"),
		TEXT("squad.BreachWallAt <X> <Y> [<FromX> <FromY>]: breach the weakened wall on cell (X,Y), as if a charge went off on cell (FromX,FromY) (default: beside the wall's -X/-Y face). GDD §9.4, debug."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			FIntPoint Cell;
			if (!CheckServer(World) || !ParseCell(Args, 0, Cell))
			{
				return;
			}
			ABreachableWall* Wall = ABreachableWall::FindAtCell(World, Cell);
			if (!Wall)
			{
				UE_LOG(LogBreachableWall, Warning, TEXT("squad.BreachWallAt: no breachable wall on cell (%d,%d)."), Cell.X, Cell.Y);
				return;
			}
			FVector Origin = DefaultBlastOrigin(Wall, Cell);
			FIntPoint FromCell;
			if (ParseCell(Args, 2, FromCell))
			{
				if (const AGridManager* Grid = AGridManager::Find(World))
				{
					Origin = Grid->CellToWorld(FromCell) + FVector(0.f, 0.f, 50.f);
				}
			}
			const bool bOk = Wall->Breach(Origin);
			UE_LOG(LogBreachableWall, Log, TEXT("squad.BreachWallAt (%d,%d): %s"), Cell.X, Cell.Y,
				bOk ? TEXT("breached") : (Wall->IsReinforced() ? TEXT("refused (reinforced)") : TEXT("refused (already breached)")));
		}),
		ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs CmdBreachAllWalls(
		TEXT("squad.BreachAllWalls"),
		TEXT("squad.BreachAllWalls: breach every weakened wall that isn't reinforced (GDD §9.4, debug)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!CheckServer(World))
			{
				return;
			}
			TArray<ABreachableWall*> Walls;
			ABreachableWall::GetAll(World, Walls);
			int32 Count = 0;
			for (ABreachableWall* Wall : Walls)
			{
				const TArray<FIntPoint> Cells = Wall->GetOccupiedCells();
				Count += Cells.Num() > 0 && Wall->Breach(DefaultBlastOrigin(Wall, Cells[Cells.Num() / 2])) ? 1 : 0;
			}
			UE_LOG(LogBreachableWall, Log, TEXT("squad.BreachAllWalls: breached %d of %d."), Count, Walls.Num());
		}),
		ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs CmdReinforceWallAt(
		TEXT("squad.ReinforceWallAt"),
		TEXT("squad.ReinforceWallAt <X> <Y>: fit a reinforcement to the weakened wall on cell (X,Y); it can then no longer be breached (GDD §9.4, debug)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			FIntPoint Cell;
			if (!CheckServer(World) || !ParseCell(Args, 0, Cell))
			{
				return;
			}
			if (ABreachableWall* Wall = ABreachableWall::FindAtCell(World, Cell))
			{
				Wall->SetReinforced();
				UE_LOG(LogBreachableWall, Log, TEXT("squad.ReinforceWallAt (%d,%d): reinforced=%d intact=%d"), Cell.X, Cell.Y, Wall->IsReinforced(), Wall->IsIntact());
			}
			else
			{
				UE_LOG(LogBreachableWall, Warning, TEXT("squad.ReinforceWallAt: no breachable wall on cell (%d,%d)."), Cell.X, Cell.Y);
			}
		}),
		ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs CmdListBreachableWalls(
		TEXT("squad.ListBreachableWalls"),
		TEXT("squad.ListBreachableWalls: log every weakened wall, its cells and state (GDD §9.4, debug)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			TArray<ABreachableWall*> Walls;
			ABreachableWall::GetAll(World, Walls);
			for (const ABreachableWall* Wall : Walls)
			{
				FString CellText;
				for (const FIntPoint& Cell : Wall->GetOccupiedCells())
				{
					CellText += FString::Printf(TEXT("(%d,%d) "), Cell.X, Cell.Y);
				}
				UE_LOG(LogBreachableWall, Log, TEXT("%s: cells %s intact=%d reinforced=%d"), *Wall->GetName(), *CellText, Wall->IsIntact(), Wall->IsReinforced());
			}
			UE_LOG(LogBreachableWall, Log, TEXT("%d breachable wall(s)."), Walls.Num());
		}),
		ECVF_Cheat);
}

// ------------------------------------------------------------------------------------------------ Grid probes (debug)
// Read-only queries for testing cover / paths / sight around breaches without a soldier in place.

namespace
{
	float ParseHeight(const TArray<FString>& Args, int32 Index, float Default)
	{
		if (!Args.IsValidIndex(Index))
		{
			return Default;
		}
		const FString& S = Args[Index];
		if (S.StartsWith(TEXT("s"), ESearchCase::IgnoreCase)) return 160.f;
		if (S.StartsWith(TEXT("c"), ESearchCase::IgnoreCase)) return 110.f;
		if (S.StartsWith(TEXT("p"), ESearchCase::IgnoreCase)) return 40.f;
		return FCString::Atof(*S);
	}

	FAutoConsoleCommandWithWorldAndArgs CmdCellInfo(
		TEXT("squad.CellInfo"),
		TEXT("squad.CellInfo <X> <Y>: log the cover on a grid cell and whether it can be walked on (debug)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const AGridManager* Grid = AGridManager::Find(World);
			FIntPoint Cell;
			if (!Grid || !ParseCell(Args, 0, Cell))
			{
				return;
			}
			const FGridCoverCell* Cover = Grid->GetCoverCell(Cell);
			UE_LOG(LogBreachableWall, Log, TEXT("CellInfo (%d,%d): cover=%s blocksLOS=%d passable=%d blockedByCover=%d owner=%s"),
				Cell.X, Cell.Y, Cover ? UCoverData::GetCoverName(Cover->Type) : TEXT("none"), Cover ? Cover->bBlocksLineOfSight : 0,
				Grid->IsCellPassable(Cell), Grid->IsCellBlockedByCover(Cell), Cover && Cover->Owner.IsValid() ? *Cover->Owner->GetName() : TEXT("-"));
		}),
		ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs CmdPathTest(
		TEXT("squad.PathTest"),
		TEXT("squad.PathTest <X1> <Y1> <X2> <Y2>: log the grid path between two cells (debug)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const AGridManager* Grid = AGridManager::Find(World);
			FIntPoint A, B;
			if (!Grid || !ParseCell(Args, 0, A) || !ParseCell(Args, 2, B))
			{
				return;
			}
			TArray<FIntPoint> Path;
			const bool bFound = Grid->FindPath(A, B, nullptr, Path);
			FString Text;
			for (const FIntPoint& P : Path)
			{
				Text += FString::Printf(TEXT("(%d,%d) "), P.X, P.Y);
			}
			UE_LOG(LogBreachableWall, Log, TEXT("PathTest (%d,%d)->(%d,%d): found=%d steps=%d %s"), A.X, A.Y, B.X, B.Y, bFound, Path.Num(), *Text);
		}),
		ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs CmdLOSTest(
		TEXT("squad.LOSTest"),
		TEXT("squad.LOSTest <X1> <Y1> <X2> <Y2> [stand|crouch|prone|cm] [stand|crouch|prone|cm]: log line of sight between two cells (debug)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const AGridManager* Grid = AGridManager::Find(World);
			FIntPoint A, B;
			if (!Grid || !ParseCell(Args, 0, A) || !ParseCell(Args, 2, B))
			{
				return;
			}
			const float HA = ParseHeight(Args, 4, 160.f);
			const float HB = ParseHeight(Args, 5, HA);
			UE_LOG(LogBreachableWall, Log, TEXT("LOSTest (%d,%d)@%.0f -> (%d,%d)@%.0f: %s"), A.X, A.Y, HA, B.X, B.Y, HB,
				Grid->HasLineOfSight(A, B, HA, HB) ? TEXT("VISIBLE") : TEXT("BLOCKED"));
		}),
		ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs CmdCoverTest(
		TEXT("squad.CoverTest"),
		TEXT("squad.CoverTest <TargetX> <TargetY> <AttackerX> <AttackerY>: log the cover a soldier on Target has against fire from Attacker (debug)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const AGridManager* Grid = AGridManager::Find(World);
			FIntPoint T, A;
			if (!Grid || !ParseCell(Args, 0, T) || !ParseCell(Args, 2, A))
			{
				return;
			}
			const FCoverResult R = Grid->GetCoverAgainst(T, A);
			UE_LOG(LogBreachableWall, Log, TEXT("CoverTest target (%d,%d) vs (%d,%d): %s reduction=%.0f from (%d,%d)"), T.X, T.Y, A.X, A.Y,
				UCoverData::GetCoverName(R.Type), R.Reduction, R.CoverCell.X, R.CoverCell.Y);
		}),
		ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs CmdGrenadeTest(
		TEXT("squad.GrenadeArcTest"),
		TEXT("squad.GrenadeArcTest <X1> <Y1> <X2> <Y2> [ArcHeight cm]: log whether a grenade lob between two cells is clear of cover (debug)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const AGridManager* Grid = AGridManager::Find(World);
			FIntPoint A, B;
			if (!Grid || !ParseCell(Args, 0, A) || !ParseCell(Args, 2, B))
			{
				return;
			}
			const float Arc = Args.IsValidIndex(4) ? FCString::Atof(*Args[4]) : 150.f;
			const bool bClear = Grid->IsGrenadeArcClear(Grid->CellToWorld(A) + FVector(0.f, 0.f, 150.f), Grid->CellToWorld(B), Arc);
			UE_LOG(LogBreachableWall, Log, TEXT("GrenadeArcTest (%d,%d)->(%d,%d) arc %.0f: %s"), A.X, A.Y, B.X, B.Y, Arc, bClear ? TEXT("CLEAR") : TEXT("BLOCKED"));
		}),
		ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs CmdBlastTest(
		TEXT("squad.BlastTest"),
		TEXT("squad.BlastTest <BlastX> <BlastY> <SoldierX> <SoldierY> [stand|crouch|prone|cm]: log whether cover shields a soldier from a grenade going off (debug)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const AGridManager* Grid = AGridManager::Find(World);
			FIntPoint B, S;
			if (!Grid || !ParseCell(Args, 0, B) || !ParseCell(Args, 2, S))
			{
				return;
			}
			const float H = ParseHeight(Args, 4, 160.f);
			UE_LOG(LogBreachableWall, Log, TEXT("BlastTest (%d,%d) -> (%d,%d)@%.0f: %s"), B.X, B.Y, S.X, S.Y, H,
				Grid->IsBlastShielded(B, S, H) ? TEXT("SHIELDED") : TEXT("EXPOSED"));
		}),
		ECVF_Cheat);
}
