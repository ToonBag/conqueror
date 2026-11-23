#include "GridZoneActor.h"

#include "GridManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Placeholder border, in cm.
	constexpr float BorderWidth = 12.f;
	constexpr float BorderHeight = 2.f;
	constexpr float BorderInset = 4.f;
	constexpr float LabelSize = 90.f;
}

AGridZoneActor::AGridZoneActor()
{
	PrimaryActorTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Border = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Border"));
	Border->SetupAttachment(RootComponent);
	Border->SetStaticMesh(CubeMesh.Object);
	Border->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Border->SetCastShadow(false);
	Border->SetCanEverAffectNavigation(false);
	Border->SetMobility(EComponentMobility::Movable);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(RootComponent);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(LabelSize);
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Label->SetCastShadow(false);
	Label->SetRelativeLocation(FVector(0.f, 0.f, 3.f));
}

void AGridZoneActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SnapToGrid(AGridManager::Find(this));
	BuildVisuals();
}

void AGridZoneActor::SnapToGrid(AGridManager* Grid)
{
	SetActorRotation(FRotator::ZeroRotator);
	if (!Grid)
	{
		return;
	}
	// Centre of the zone = midpoint between its min cell's centre and its max cell's centre.
	const FIntPoint Min = GetMinCell();
	const FVector A = Grid->CellToWorld(Min);
	const FVector B = Grid->CellToWorld(Min + FIntPoint(SizeX - 1, SizeY - 1));
	SetActorLocation((A + B) * 0.5f);
}

FIntPoint AGridZoneActor::GetMinCell() const
{
	const AGridManager* Grid = AGridManager::Find(this);
	if (!Grid)
	{
		return FIntPoint::ZeroValue;
	}
	// Cell (0,0)'s centre gives the grid origin.
	const float Cell = Grid->CellSize;
	const FVector Origin = Grid->CellToWorld(FIntPoint::ZeroValue) - FVector(Cell * 0.5f, Cell * 0.5f, 0.f);
	const FVector Local = (GetActorLocation() - Origin) / Cell;
	return FIntPoint(FMath::RoundToInt(Local.X - SizeX * 0.5f), FMath::RoundToInt(Local.Y - SizeY * 0.5f));
}

bool AGridZoneActor::ContainsCell(FIntPoint Cell) const
{
	const FIntPoint Min = GetMinCell();
	return Cell.X >= Min.X && Cell.X < Min.X + SizeX && Cell.Y >= Min.Y && Cell.Y < Min.Y + SizeY;
}

TArray<FIntPoint> AGridZoneActor::GetCells() const
{
	TArray<FIntPoint> Cells;
	const FIntPoint Min = GetMinCell();
	for (int32 Y = 0; Y < SizeY; ++Y)
	{
		for (int32 X = 0; X < SizeX; ++X)
		{
			Cells.Add(Min + FIntPoint(X, Y));
		}
	}
	return Cells;
}

void AGridZoneActor::BuildVisuals()
{
	const AGridManager* Grid = AGridManager::Find(this);
	const float Cell = Grid ? Grid->CellSize : 100.f;
	const float HalfX = SizeX * Cell * 0.5f - BorderInset - BorderWidth * 0.5f;
	const float HalfY = SizeY * Cell * 0.5f - BorderInset - BorderWidth * 0.5f;

	// Engine cube: 100 units, centred on its pivot.
	auto AddStrip = [this](const FVector& Center, float LengthX, float LengthY)
	{
		const FVector Scale(LengthX / 100.f, LengthY / 100.f, BorderHeight / 100.f);
		Border->AddInstance(FTransform(FRotator::ZeroRotator, Center + FVector(0.f, 0.f, BorderHeight * 0.5f), Scale));
	};

	Border->ClearInstances();
	AddStrip(FVector(0.f, -HalfY, 0.f), HalfX * 2.f + BorderWidth, BorderWidth);
	AddStrip(FVector(0.f, HalfY, 0.f), HalfX * 2.f + BorderWidth, BorderWidth);
	AddStrip(FVector(-HalfX, 0.f, 0.f), BorderWidth, HalfY * 2.f - BorderWidth);
	AddStrip(FVector(HalfX, 0.f, 0.f), BorderWidth, HalfY * 2.f - BorderWidth);

	// Lying flat on the ground, face up, upright from the default camera (which looks north from the Allied side).
	Label->SetRelativeRotation(FQuat(FVector::UpVector, PI) * FRotator(90.f, 90.f, 0.f).Quaternion());
	Label->SetText(GetLabelText());
	RefreshColor();
}

void AGridZoneActor::RefreshColor()
{
	const FLinearColor Color = GetZoneColor();
	if (!BorderMaterial)
	{
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
		{
			BorderMaterial = UMaterialInstanceDynamic::Create(Base, this);
			Border->SetMaterial(0, BorderMaterial);
		}
	}
	if (BorderMaterial)
	{
		BorderMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}
	Label->SetTextRenderColor(Color.ToFColor(true));
}
