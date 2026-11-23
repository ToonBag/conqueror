#include "LootPile.h"

#include "GridManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	void SetupLootPart(UStaticMeshComponent* Mesh, UMaterialInterface* Material, const FLinearColor& Color, UObject* Outer)
	{
		// Purely visual: never blocks the cursor, movement or line of sight.
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetCastShadow(false);
		if (Material)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Material, Outer);
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			Mesh->SetMaterial(0, MID);
		}
	}
}

ALootPile::ALootPile()
{
	PrimaryActorTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// Off-centre in the cell so it doesn't sit inside a corpse lying on the same cell.
	Crate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Crate"));
	Crate->SetupAttachment(RootComponent);
	Crate->SetStaticMesh(CubeMesh.Object);
	Crate->SetRelativeScale3D(FVector(0.35f, 0.25f, 0.15f));
	Crate->SetRelativeLocation(FVector(25.f, 25.f, 7.5f));

	Rifle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Rifle"));
	Rifle->SetupAttachment(RootComponent);
	Rifle->SetStaticMesh(CubeMesh.Object);
	Rifle->SetRelativeScale3D(FVector(1.1f, 0.06f, 0.05f));
	Rifle->SetRelativeLocation(FVector(10.f, 25.f, 17.5f));
	Rifle->SetRelativeRotation(FRotator(0.f, 25.f, 0.f));
	Rifle->SetVisibility(false);
}

ALootPile* ALootPile::FindOrSpawnAt(AGridManager& Grid, FIntPoint InCell)
{
	UWorld* World = Grid.GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ALootPile> It(World); It; ++It)
	{
		if (IsValid(*It) && It->Cell == InCell)
		{
			return *It;
		}
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALootPile* Pile = World->SpawnActor<ALootPile>(ALootPile::StaticClass(), FTransform(Grid.CellToWorld(InCell)), Params);
	if (Pile)
	{
		Pile->Cell = InCell;
		UMaterialInterface* ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		SetupLootPart(Pile->Crate, ShapeMaterial, FLinearColor(0.28f, 0.3f, 0.12f), Pile);
		SetupLootPart(Pile->Rifle, ShapeMaterial, FLinearColor(0.25f, 0.14f, 0.06f), Pile);
	}
	return Pile;
}

void ALootPile::OnContentsChanged()
{
	if (Contents.IsEmpty())
	{
		Destroy();
		return;
	}
	Rifle->SetVisibility(Contents.Weapons.Num() > 0);
}
