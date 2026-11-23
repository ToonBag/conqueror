#include "VictoryPoint.h"

#include "GridManager.h"
#include "MapZoneColors.h"
#include "Soldier.h"
#include "SquadMatchStats.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogVictoryPoint, Log, All);

namespace
{
	// Placeholder flag, in cm.
	constexpr float PoleHeight = 320.f;
	constexpr float PoleDiameter = 6.f;
	constexpr float FlagLength = 90.f;
	constexpr float FlagHeight = 55.f;
	constexpr float FlagThickness = 3.f;
}

AVictoryPoint::AVictoryPoint()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.f);

	SizeX = 5;
	SizeY = 5;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	auto MakePart = [this](const TCHAR* Name, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(RootComponent);
		Part->SetStaticMesh(Mesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->AlwaysLoadOnServer = true; // A dedicated server skips loading no-collision components otherwise (null on the server).
		Part->SetCanEverAffectNavigation(false);
		Part->SetMobility(EComponentMobility::Movable);
		return Part;
	};

	// Engine cylinder and cube: 100 units, centred on their pivots.
	FlagPole = MakePart(TEXT("FlagPole"), CylinderMesh.Object);
	FlagPole->SetMaterial(0, ShapeMaterial.Object);
	FlagPole->SetRelativeScale3D(FVector(PoleDiameter / 100.f, PoleDiameter / 100.f, PoleHeight / 100.f));
	FlagPole->SetRelativeLocation(FVector(0.f, 0.f, PoleHeight * 0.5f));

	Flag = MakePart(TEXT("Flag"), CubeMesh.Object);
	Flag->SetRelativeScale3D(FVector(FlagLength / 100.f, FlagThickness / 100.f, FlagHeight / 100.f));
	Flag->SetRelativeLocation(FVector(FlagLength * 0.5f + PoleDiameter * 0.5f, 0.f, PoleHeight - FlagHeight * 0.5f - 5.f));
}

void AVictoryPoint::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AVictoryPoint, bOwned);
	DOREPLIFETIME(AVictoryPoint, OwningTeam);
	DOREPLIFETIME(AVictoryPoint, bContested);
}

void AVictoryPoint::BeginPlay()
{
	Super::BeginPlay();
	RefreshColor();
	RefreshFlag();
}

void AVictoryPoint::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}
	TimeToNextCheck -= DeltaSeconds;
	if (TimeToNextCheck <= 0.f)
	{
		TimeToNextCheck = CheckInterval;
		UpdateOwnership();
	}
}

void AVictoryPoint::UpdateOwnership()
{
	const AGridManager* Grid = AGridManager::Find(this);
	if (!Grid)
	{
		return;
	}

	int32 Allies = 0;
	int32 Axis = 0;
	for (const TWeakObjectPtr<ASoldier>& Weak : Grid->GetSoldiers())
	{
		const ASoldier* Soldier = Weak.Get();
		if (!Soldier || Soldier->IsDead() || (!bDownedSoldiersCount && Soldier->IsDowned()))
		{
			continue;
		}
		if (ContainsCell(Soldier->GetCurrentCell()))
		{
			(Soldier->Team == ESquadTeam::Allies ? Allies : Axis)++;
		}
	}

	const bool bNewContested = Allies > 0 && Axis > 0;
	bool bNewOwned = bOwned;
	ESquadTeam NewTeam = OwningTeam;
	if (!bNewContested && (Allies > 0 || Axis > 0))
	{
		bNewOwned = true;
		NewTeam = Allies > 0 ? ESquadTeam::Allies : ESquadTeam::Axis;
	}

	if (bNewOwned != bOwned || NewTeam != OwningTeam || bNewContested != bContested)
	{
		if (bNewOwned && (!bOwned || NewTeam != OwningTeam))
		{
			UE_LOG(LogVictoryPoint, Log, TEXT("Victory point %s captured by %s"), *PointName.ToString(),
				NewTeam == ESquadTeam::Allies ? TEXT("Allies") : TEXT("Axis"));
		}
		if (bNewOwned && (!bOwned || NewTeam != OwningTeam))
		{
			if (USquadMatchStats* Stats = USquadMatchStats::Get(this))
			{
				Stats->RecordVPCapture(*this, NewTeam);
			}
		}
		bOwned = bNewOwned;
		OwningTeam = NewTeam;
		bContested = bNewContested;
		OnRep_State();
		ForceNetUpdate();
	}
}

void AVictoryPoint::OnRep_State()
{
	RefreshColor();
	RefreshFlag();
}

FLinearColor AVictoryPoint::GetZoneColor() const
{
	if (bContested)
	{
		return MapZoneColors::Contested;
	}
	return bOwned ? MapZoneColors::ForTeam(OwningTeam) : MapZoneColors::Neutral;
}

void AVictoryPoint::BuildVisuals()
{
	Super::BuildVisuals();
	if (!HasVisuals())
	{
		return;
	}
	// The letter sits in front of the pole (towards -Y) so it isn't hidden under it.
	Label->SetRelativeLocation(FVector(0.f, -110.f, 3.f));
	Label->SetWorldSize(140.f);
	RefreshFlag();
}

void AVictoryPoint::RefreshFlag()
{
	if (!HasVisuals() || !Flag)
	{
		return;
	}
	if (!FlagMaterial)
	{
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
		{
			FlagMaterial = UMaterialInstanceDynamic::Create(Base, this);
			Flag->SetMaterial(0, FlagMaterial);
		}
	}
	if (FlagMaterial)
	{
		FlagMaterial->SetVectorParameterValue(TEXT("Color"), bOwned ? MapZoneColors::ForTeam(OwningTeam) : MapZoneColors::Neutral);
	}
}
