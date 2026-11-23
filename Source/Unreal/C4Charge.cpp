#include "C4Charge.h"

#include "BreachableWall.h"
#include "CoverData.h"
#include "Grenade.h"
#include "GridManager.h"
#include "KillFeedTypes.h"
#include "Soldier.h"
#include "SoldierData.h"
#include "SoldierVisualData.h"
#include "SquadPlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float UUPerMetre = 100.f;
	/** Half the thickness of a full wall's slab (CoverActor.cpp WallThickness 30 cm) plus a little: the charge sits on the face. */
	constexpr float FaceOffset = 15.f + 3.f;
	/** Height of the charge on the wall (cm above the ground), also the height enemies must see to spot it. */
	constexpr float ChargeHeight = 60.f;
	/** Cosmetic: how much further out a charge sits on a reinforced wall (ABreachableWall's plates stand proud of the face). */
	constexpr float ReinforcedFaceExtra = 10.f;

	UStaticMeshComponent* MakePart(AActor* Owner, FName Name, UStaticMesh* Mesh, UMaterialInterface* Material, USceneComponent* Parent)
	{
		UStaticMeshComponent* Part = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetStaticMesh(Mesh);
		Part->SetMaterial(0, Material);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->AlwaysLoadOnServer = true; // A dedicated server skips loading no-collision components otherwise (null on the server).
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(false);
		Part->SetupAttachment(Parent);
		return Part;
	}

	void SetPartColor(UStaticMeshComponent* Mesh, UObject* Outer, const FLinearColor& Color)
	{
		if (UMaterialInterface* Base = Mesh ? Mesh->GetMaterial(0) : nullptr)
		{
			UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Base);
			if (!MID)
			{
				MID = UMaterialInstanceDynamic::Create(Base, Outer);
				Mesh->SetMaterial(0, MID);
			}
			MID->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}
}

AC4Charge::AC4Charge()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(false); // Never moves once stuck to the wall.

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// Placeholder charge: a flat olive block (local X points out of the wall) with a red light on its face.
	Body = MakePart(this, TEXT("Body"), CubeMesh.Object, ShapeMaterial.Object, RootComponent);
	Body->SetRelativeScale3D(FVector(0.06f, 0.26f, 0.18f));

	Light = MakePart(this, TEXT("Light"), SphereMesh.Object, ShapeMaterial.Object, RootComponent);
	Light->SetRelativeLocation(FVector(4.f, 8.f, 5.f));
	Light->SetRelativeScale3D(FVector(0.045f));

	Blast = MakePart(this, TEXT("Blast"), SphereMesh.Object, ShapeMaterial.Object, RootComponent);
	Blast->SetVisibility(false);

	StunRing = MakePart(this, TEXT("StunRing"), CylinderMesh.Object, ShapeMaterial.Object, RootComponent);
	StunRing->SetVisibility(false);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(RootComponent);
	Label->SetRelativeLocation(FVector(10.f, 0.f, 40.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(22.f);
	Label->SetTextRenderColor(FColor(255, 90, 60));
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AC4Charge::BeginPlay()
{
	Super::BeginPlay();
	SetPartColor(Body, this, FLinearColor(0.28f, 0.3f, 0.16f));
	SetPartColor(Light, this, FLinearColor(1.f, 0.05f, 0.02f));
	SetPartColor(Blast, this, FLinearColor(1.f, 0.45f, 0.05f));
	SetPartColor(StunRing, this, FLinearColor(0.6f, 0.25f, 0.05f));
	UpdateLocalVisibility();
}

void AC4Charge::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AC4Charge, State);
	DOREPLIFETIME(AC4Charge, Team);
	DOREPLIFETIME(AC4Charge, bSpottedByEnemy);
	DOREPLIFETIME(AC4Charge, Wall);
	DOREPLIFETIME(AC4Charge, FuseRemaining);
}

FVector AC4Charge::GetFaceLocation(const UObject* WorldContext, FIntPoint InWallCell, FIntPoint InStandCell)
{
	const AGridManager* Grid = AGridManager::Find(WorldContext);
	if (!Grid)
	{
		return FVector::ZeroVector;
	}
	const FVector WallCentre = Grid->CellToWorld(InWallCell);
	const FVector Out = (Grid->CellToWorld(InStandCell) - WallCentre).GetSafeNormal2D();
	return WallCentre + Out * FaceOffset + FVector(0.f, 0.f, ChargeHeight);
}

void AC4Charge::Arm(ABreachableWall* InWall, FIntPoint InWallCell, FIntPoint InStandCell, ASoldier* InPlacer, const USoldierData& Data)
{
	Wall = InWall;
	WallCell = InWallCell;
	StandCell = InStandCell;
	Placer = InPlacer;
	Team = InPlacer ? InPlacer->Team : ESquadTeam::Allies;
	bWallReinforcedAtPlant = InWall && InWall->IsReinforced();
	FuseRemaining = Data.C4FuseTime;
	KillRadius = Data.C4KillRadius;
	StunRadius = Data.C4StunRadius;
	KillChance = Data.GrenadeKillChance;
	CloseStunDuration = Data.GrenadeCloseStunDuration;
	StunDuration = Data.GrenadeStunDuration;

	if (const AGridManager* Grid = AGridManager::Find(this))
	{
		const FVector Out = (Grid->CellToWorld(InStandCell) - Grid->CellToWorld(InWallCell)).GetSafeNormal2D();
		const float Extra = bWallReinforcedAtPlant ? ReinforcedFaceExtra : 0.f; // Over the reinforcement's plates.
		SetActorLocationAndRotation(GetFaceLocation(this, InWallCell, InStandCell) + Out * Extra, Out.Rotation());
	}
	State = EC4State::Armed;
	StateTime = 0.f;
	bArmed = true;
	UpdateSpotted();
	ApplyStateVisuals();
}

void AC4Charge::GetChargesOnWall(const UObject* WorldContext, const ABreachableWall* InWall, TArray<AC4Charge*>& OutCharges)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || !InWall)
	{
		return;
	}
	for (TActorIterator<AC4Charge> It(World); It; ++It)
	{
		if (It->Wall == InWall && It->IsArmed())
		{
			OutCharges.Add(*It);
		}
	}
}

void AC4Charge::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	StateTime += DeltaSeconds;

	switch (State)
	{
	case EC4State::Armed:
	{
		FuseRemaining = FMath::Max(0.f, FuseRemaining - DeltaSeconds);
		// Blinks faster as the fuse runs down: 2 Hz at 5 s left, ~8 Hz at the end.
		const float Rate = FMath::Lerp(8.f, 2.f, FMath::Clamp(FuseRemaining / 5.f, 0.f, 1.f));
		Light->SetVisibility(FMath::Frac(StateTime * Rate) < 0.5f && !IsHidden());
		if (HasLabel()) Label->SetText(FText::FromString(FString::Printf(TEXT("C4 %.1f"), FuseRemaining)));

		if (HasAuthority() && bArmed)
		{
			// GDD §13.3: a reinforcement finished while the fuse burns makes the charge a dud.
			if (!bWallReinforcedAtPlant && Wall && Wall->IsReinforced())
			{
				Fizzle();
				break;
			}
			if (FuseRemaining <= 0.f)
			{
				Detonate();
				break;
			}
			SpotCheckRemaining -= DeltaSeconds;
			if (SpotCheckRemaining <= 0.f)
			{
				SpotCheckRemaining = SpotCheckInterval;
				UpdateSpotted();
			}
		}
		break;
	}
	case EC4State::Detonated:
	{
		const float Alpha = FMath::Min(StateTime / ExplosionDuration, 1.f);
		const float FireballDiameter = FMath::Max(KillRadius, 1.f) * 2.5f * FMath::Sin(Alpha * PI);
		Blast->SetRelativeScale3D(FVector(FMath::Max(FireballDiameter, 0.01f)));
		if (Alpha >= 1.f && HasAuthority())
		{
			Destroy();
		}
		break;
	}
	case EC4State::Dud:
		// A little grey puff, then it just sits there dead until it is cleared away.
		Blast->SetRelativeScale3D(FVector(FMath::Max(0.5f * FMath::Sin(FMath::Min(StateTime / 1.5f, 1.f) * PI), 0.01f)));
		if (StateTime >= DudLingerTime && HasAuthority())
		{
			Destroy();
		}
		break;
	}
	UpdateLocalVisibility();
}

void AC4Charge::Detonate()
{
	State = EC4State::Detonated;
	StateTime = 0.f;
	ApplyStateVisuals();
	PlayExplosionSound(); // Clients play it from OnRep_State.

	const FVector Origin = GetActorLocation();
	// GDD §7.2: breach the wall unless it was reinforced before the charge went on (then only the blast happens).
	if (ABreachableWall* Target = Wall.Get(); Target && Target->IsIntact() && !Target->IsReinforced())
	{
		Target->Breach(Origin);
	}
	// Grenade blast effects on soldiers round the charge, friend or foe. Pure distance: the wall being blown
	// apart doesn't shield anyone behind it.
	AGrenade::ApplyBlast(GetWorld(), Origin, KillRadius, KillChance, CloseStunDuration, StunRadius, StunDuration, Placer.Get(), EKillMethod::C4);
}

void AC4Charge::Fizzle()
{
	// Cosmetic: the reinforcement's plates now cover the face, so sit the dead charge on top of them.
	SetActorLocation(GetActorLocation() + GetActorForwardVector() * ReinforcedFaceExtra);
	State = EC4State::Dud;
	StateTime = 0.f;
	ApplyStateVisuals();
}

void AC4Charge::OnRep_State()
{
	StateTime = 0.f;
	ApplyStateVisuals();
	if (State == EC4State::Detonated)
	{
		PlayExplosionSound();
	}
}

void AC4Charge::PlayExplosionSound() const
{
	if (const USoldierVisualData* Visuals = USoldierVisualData::GetDefaultVisuals())
	{
		Visuals->PlayCombatSound(this, Visuals->ExplosionSound, GetActorLocation());
	}
}

void AC4Charge::ApplyStateVisuals()
{
	switch (State)
	{
	case EC4State::Armed:
		Blast->SetVisibility(false);
		StunRing->SetVisibility(false);
		if (HasLabel()) Label->SetTextRenderColor(FColor(255, 90, 60));
		break;
	case EC4State::Detonated:
	{
		Body->SetVisibility(false);
		Light->SetVisibility(false);
		if (HasLabel()) Label->SetVisibility(false);
		Blast->SetVisibility(true);
		SetPartColor(Blast, this, FLinearColor(1.f, 0.45f, 0.05f));
		// Stun radius on the ground (cylinder is 100 cm across: scale = diameter in metres).
		StunRing->SetVisibility(true);
		StunRing->SetAbsolute(false, true, false);
		StunRing->SetWorldRotation(FRotator::ZeroRotator);
		StunRing->SetRelativeLocation(FVector(0.f, 0.f, -ChargeHeight + 1.f));
		StunRing->SetRelativeScale3D(FVector(StunRadius * 2.f, StunRadius * 2.f, 0.01f));
		break;
	}
	case EC4State::Dud:
		Light->SetVisibility(false);
		SetPartColor(Body, this, FLinearColor(0.15f, 0.15f, 0.14f));
		Blast->SetVisibility(true);
		SetPartColor(Blast, this, FLinearColor(0.45f, 0.45f, 0.45f));
		if (HasLabel()) Label->SetText(FText::FromString(TEXT("DUD")));
		if (HasLabel()) Label->SetTextRenderColor(FColor(190, 190, 190));
		break;
	}
}

void AC4Charge::UpdateSpotted()
{
	// GDD §9.3 team vision: the other team sees the charge while one of its living soldiers has a sight line to the
	// face it is on (from the open cell in front of it, at the charge's height), within the maximum sight range.
	const AGridManager* Grid = AGridManager::Find(this);
	if (!Grid)
	{
		bSpottedByEnemy = true;
		return;
	}
	const float MaxSight = Grid->GetCoverData() ? Grid->GetCoverData()->MaxSightRange : 0.f;
	bool bSpotted = false;
	for (const TWeakObjectPtr<ASoldier>& Weak : Grid->GetSoldiers())
	{
		const ASoldier* Viewer = Weak.Get();
		if (!Viewer || Viewer->Team == Team || Viewer->IsDead())
		{
			continue;
		}
		const FIntPoint From = Viewer->GetSightCell();
		if (MaxSight > 0.f && FVector2D(From - StandCell).Size() > MaxSight)
		{
			continue;
		}
		if (From == StandCell || Grid->HasLineOfSight(From, StandCell, Viewer->GetSightHeight(), ChargeHeight))
		{
			bSpotted = true;
			break;
		}
	}
	bSpottedByEnemy = bSpotted;
}

void AC4Charge::UpdateLocalVisibility()
{
	const UWorld* World = GetWorld();
	const ASquadPlayerController* PC = World ? Cast<ASquadPlayerController>(World->GetFirstPlayerController()) : nullptr;
	if (!PC || !PC->IsLocalController())
	{
		return; // Dedicated server: nothing to render.
	}
	static const IConsoleVariable* RevealAll = IConsoleManager::Get().FindConsoleVariable(TEXT("squad.RevealAll"));
	const bool bReveal = RevealAll && RevealAll->GetInt() != 0;
	// The blast itself is seen (and heard) by everyone.
	const bool bHide = State != EC4State::Detonated && !bReveal && !IsVisibleToTeam(PC->PlayerTeam);
	if (bHide != IsHidden())
	{
		SetActorHiddenInGame(bHide);
	}
}
