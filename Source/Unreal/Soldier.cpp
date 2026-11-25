#include "Soldier.h"
#include "Ballistics.h"
#include "BallisticsData.h"

#include "BreachableWall.h"
#include "Grenade.h"
#include "GridManager.h"
#include "LootSystem.h"
#include "SoldierAnimInstance.h"
#include "SoldierData.h"
#include "SoldierVisualData.h"
#include "SquadGameState.h"
#include "SquadMatchStats.h"
#include "SquadPlayerController.h"
#include "SquadPlayerState.h"
#include "WeaponData.h"
#include "HAL/IConsoleManager.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** GDD §5.3 hold-fire tag over the soldier: short enough not to run into a neighbour 1 m away. */
	const TCHAR* const HoldFireLabelText = TEXT("HOLD");
	constexpr float HoldFireLabelSize = 30.f;
}

namespace
{
	TAutoConsoleVariable<int32> CVarRevealAllSoldiers(
		TEXT("squad.RevealAll"),
		0,
		TEXT("1 = show every soldier regardless of line of sight (debug). 0 = enemies only while spotted (GDD §9.3)."),
		ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs CmdSetStress(
		TEXT("squad.SetStress"),
		TEXT("squad.SetStress <0-100>: set the stress of the selected soldiers (GDD §8, debug)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || Args.Num() < 1)
			{
				return;
			}
			const float Value = FCString::Atof(*Args[0]);
			const AGridManager* Grid = AGridManager::Find(World);
			if (!Grid)
			{
				return;
			}
			for (const TWeakObjectPtr<ASoldier>& Weak : Grid->GetSoldiers())
			{
				if (ASoldier* Soldier = Weak.Get(); Soldier && Soldier->IsSelected() && Soldier->HasAuthority())
				{
					Soldier->SetStress(Value);
				}
			}
		}),
		ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs CmdInjure(
		TEXT("squad.Injure"),
		TEXT("squad.Injure [squad number]: injure the selected soldiers, or the soldier(s) with that number (GDD §5.1, debug)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const AGridManager* Grid = World ? AGridManager::Find(World) : nullptr;
			if (!Grid)
			{
				return;
			}
			const int32 Number = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
			for (const TWeakObjectPtr<ASoldier>& Weak : Grid->GetSoldiers())
			{
				ASoldier* Soldier = Weak.Get();
				const bool bPicked = Number > 0 ? (Soldier && Soldier->GetSquadNumber() == Number) : (Soldier && Soldier->IsSelected());
				if (bPicked && Soldier->HasAuthority() && !Soldier->IsDead() && !Soldier->IsInjured())
				{
					Soldier->ReceiveHit(30.f, FKillCause()); // Beyond the close-range kill distance: injures.
				}
			}
		}),
		ECVF_Cheat);

	constexpr float UUPerMetre = 100.f;

	// Bean: 60 cm wide, 160 cm tall (cylinder with a sphere at each end).
	constexpr float BeanRadius = 30.f;
	constexpr float BeanBodyHeight = 100.f;
	constexpr float BeanHeight = BeanRadius * 2.f + BeanBodyHeight;
	constexpr float CrouchHeightScale = 0.6f;

	const FLinearColor CorpseColor(0.18f, 0.18f, 0.18f);

	void SetupClickablePart(UStaticMeshComponent* Mesh)
	{
		// Only respond to mouse traces (Visibility); soldiers never physically collide.
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Mesh->SetCanEverAffectNavigation(false);
	}

	void SetupDecorPart(UStaticMeshComponent* Mesh)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->AlwaysLoadOnServer = true; // A dedicated server skips loading no-collision components otherwise (null on the server).
		Mesh->SetCanEverAffectNavigation(false);
	}

	void SetupLabel(UTextRenderComponent* Label, float WorldSize)
	{
		Label->SetHorizontalAlignment(EHTA_Center);
		Label->SetVerticalAlignment(EVRTA_TextCenter);
		Label->SetWorldSize(WorldSize);
		Label->SetTextRenderColor(FColor::White);
		Label->SetCastShadow(false);
		Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		// Not AlwaysLoadOnServer: text has no font on a dedicated server, so labels stay null there (see HasLabels).
	}

	void SetPartColor(UStaticMeshComponent* Mesh, UObject* Outer, const FLinearColor& Color)
	{
		// Always tint an instance of the basic shape material (it has the "Color" parameter). Don't trust
		// whatever is on the mesh: a stale instance of another material (e.g. after Live Coding re-creates
		// the actor) would otherwise leave the soldier grey for good.
		static const TCHAR* ShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, ShapeMaterialPath);
		if (!Base)
		{
			return;
		}
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
		if (!MID || MID->Parent != Base)
		{
			MID = UMaterialInstanceDynamic::Create(Base, Outer);
			Mesh->SetMaterial(0, MID);
		}
		MID->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

ASoldier::ASoldier()
{
	PrimaryActorTick.bCanEverTick = true;

	// Server-simulated; every client needs every soldier (hidden enemies are hidden locally from bSpottedByEnemy).
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false); // FSoldierNetState carries the position; TickClient glides to it.
	SetNetUpdateFrequency(20.f);
	AutoPossessAI = EAutoPossessAI::Disabled;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UWeaponData> DefaultWeapon(TEXT("/Game/Data/DA_M1Garand.DA_M1Garand"));
	static ConstructorHelpers::FObjectFinder<USoldierData> DefaultSoldierData(TEXT("/Game/Data/DA_SoldierTuning.DA_SoldierTuning"));
	static ConstructorHelpers::FObjectFinder<USoldierVisualData> DefaultVisualData(TEXT("/Game/Data/DA_SoldierVisuals.DA_SoldierVisuals"));
	if (DefaultVisualData.Succeeded())
	{
		VisualData = DefaultVisualData.Object;
	}
	if (DefaultWeapon.Succeeded())
	{
		Weapon = DefaultWeapon.Object;
	}
	if (DefaultSoldierData.Succeeded())
	{
		SoldierData = DefaultSoldierData.Object;
	}

	// Root sits on the ground at the centre of the soldier's cell.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	BodyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyRoot"));
	BodyRoot->SetupAttachment(RootComponent);

	const float Diameter = BeanRadius * 2.f / 100.f;

	BeanBottom = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeanBottom"));
	BeanBottom->SetupAttachment(BodyRoot);
	BeanBottom->SetStaticMesh(SphereMesh.Object);
	BeanBottom->SetMaterial(0, ShapeMaterial.Object);
	BeanBottom->SetRelativeScale3D(FVector(Diameter));
	BeanBottom->SetRelativeLocation(FVector(0.f, 0.f, BeanRadius));
	SetupClickablePart(BeanBottom);

	BeanBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeanBody"));
	BeanBody->SetupAttachment(BodyRoot);
	BeanBody->SetStaticMesh(CylinderMesh.Object);
	BeanBody->SetMaterial(0, ShapeMaterial.Object);
	BeanBody->SetRelativeScale3D(FVector(Diameter, Diameter, BeanBodyHeight / 100.f));
	BeanBody->SetRelativeLocation(FVector(0.f, 0.f, BeanRadius + BeanBodyHeight * 0.5f));
	SetupClickablePart(BeanBody);

	BeanTop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeanTop"));
	BeanTop->SetupAttachment(BodyRoot);
	BeanTop->SetStaticMesh(SphereMesh.Object);
	BeanTop->SetMaterial(0, ShapeMaterial.Object);
	BeanTop->SetRelativeScale3D(FVector(Diameter));
	BeanTop->SetRelativeLocation(FVector(0.f, 0.f, BeanRadius + BeanBodyHeight));
	SetupClickablePart(BeanTop);

	Visor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visor"));
	Visor->SetupAttachment(BodyRoot);
	Visor->SetStaticMesh(CubeMesh.Object);
	Visor->SetMaterial(0, ShapeMaterial.Object);
	Visor->SetRelativeScale3D(FVector(0.12f, 0.4f, 0.1f));
	Visor->SetRelativeLocation(FVector(BeanRadius - 2.f, 0.f, BeanRadius + BeanBodyHeight - 5.f));
	SetupDecorPart(Visor);

	SelectionRing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SelectionRing"));
	SelectionRing->SetupAttachment(RootComponent);
	SelectionRing->SetStaticMesh(CylinderMesh.Object);
	SelectionRing->SetMaterial(0, ShapeMaterial.Object);
	SelectionRing->SetRelativeScale3D(FVector(0.85f, 0.85f, 0.02f));
	SelectionRing->SetRelativeLocation(FVector(0.f, 0.f, 2.f));
	SelectionRing->SetCastShadow(false);
	SelectionRing->SetVisibility(false);
	SetupDecorPart(SelectionRing);

	NumberLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("NumberLabel"));
	NumberLabel->SetupAttachment(RootComponent);
	NumberLabel->SetUsingAbsoluteRotation(true);
	SetupLabel(NumberLabel, 60.f);

	StatusLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusLabel"));
	StatusLabel->SetupAttachment(RootComponent);
	StatusLabel->SetUsingAbsoluteRotation(true);
	SetupLabel(StatusLabel, 22.f);
	StatusLabel->SetVerticalAlignment(EVRTA_TextBottom);

	HoldFireLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("HoldFireLabel"));
	HoldFireLabel->SetupAttachment(RootComponent);
	HoldFireLabel->SetUsingAbsoluteRotation(true);
	SetupLabel(HoldFireLabel, HoldFireLabelSize);
	HoldFireLabel->SetVerticalAlignment(EVRTA_TextBottom);
	HoldFireLabel->SetText(FText::FromString(HoldFireLabelText)); // Short: soldiers stand 1 m apart.
	HoldFireLabel->SetTextRenderColor(FColor(235, 10, 0)); // Deep red: lighter reds wash out to pink on the pale ground.
	HoldFireLabel->SetVisibility(false);

	// Destination marker lives in world space so it stays on the target cell while the soldier walks.
	MoveMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoveMarker"));
	MoveMarker->SetupAttachment(RootComponent);
	MoveMarker->SetUsingAbsoluteLocation(true);
	MoveMarker->SetUsingAbsoluteRotation(true);
	MoveMarker->SetUsingAbsoluteScale(true);
	MoveMarker->SetStaticMesh(PlaneMesh.Object);
	MoveMarker->SetMaterial(0, ShapeMaterial.Object);
	MoveMarker->SetCastShadow(false);
	MoveMarker->SetVisibility(false);
	SetupDecorPart(MoveMarker);

	MoveMarkerLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("MoveMarkerLabel"));
	MoveMarkerLabel->SetupAttachment(RootComponent);
	MoveMarkerLabel->SetUsingAbsoluteLocation(true);
	MoveMarkerLabel->SetUsingAbsoluteRotation(true);
	MoveMarkerLabel->SetUsingAbsoluteScale(true);
	SetupLabel(MoveMarkerLabel, 70.f);
	MoveMarkerLabel->SetVisibility(false);

	Tracer = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Tracer"));
	Tracer->SetupAttachment(RootComponent);
	Tracer->SetUsingAbsoluteLocation(true);
	Tracer->SetUsingAbsoluteRotation(true);
	Tracer->SetUsingAbsoluteScale(true);
	Tracer->SetStaticMesh(CubeMesh.Object);
	Tracer->SetMaterial(0, ShapeMaterial.Object);
	Tracer->SetCastShadow(false);
	Tracer->SetVisibility(false);
	SetupDecorPart(Tracer);

	// Uniformed model. Faces +Y as imported, so turn it to the actor's +X. Purely visual: clicks still hit
	// the (hidden) bean parts, and the pose only updates where it is rendered (never on a dedicated server).
	Model = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Model"));
	Model->SetupAttachment(RootComponent);
	Model->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Model->AlwaysLoadOnServer = true; // A dedicated server skips loading no-collision components otherwise (null on the server).
	Model->SetCanEverAffectNavigation(false);
	Model->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Model->SetAnimInstanceClass(USoldierAnimInstance::StaticClass());
	Model->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

	WeaponModel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponModel"));
	WeaponModel->SetupAttachment(Model); // Bone/socket set in UpdateModel, once the skeletal mesh is assigned.
	SetupDecorPart(WeaponModel);

	GrenadeModel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GrenadeModel"));
	GrenadeModel->SetupAttachment(Model);
	GrenadeModel->SetVisibility(false);
	SetupDecorPart(GrenadeModel);
}

const UWeaponData* ASoldier::GetWeaponData() const
{
	if (Inventory.Weapons.Num() > 0 && Inventory.Weapons[0].Weapon)
	{
		return Inventory.Weapons[0].Weapon; // The rifle actually held, which may have been swapped by looting.
	}
	return Weapon ? Weapon.Get() : GetDefault<UWeaponData>();
}

void ASoldier::EquipStartingWeapon(UWeaponData* NewWeapon)
{
	if (!NewWeapon || bDead)
	{
		return;
	}
	// The weapon in hand and its starting rounds (loaded, then spare) replace the old weapon and all loose
	// rounds. Grenades, medkits and buildables are kept.
	const int32 Rounds = NewWeapon->GetStartingRounds(GetSoldierData()->StartingAmmo);
	Inventory.Weapons.Reset();
	Inventory.Ammo.Reset();
	FLootWeapon& Held = Inventory.Weapons.AddDefaulted_GetRef();
	Held.Weapon = NewWeapon;
	Held.LoadedRounds = FMath::Min(NewWeapon->MagazineCapacity, Rounds);
	Inventory.AddAmmo(NewWeapon, Rounds - Held.LoadedRounds);
	if (HasActorBegunPlay())
	{
		NotifyWeaponChanged();
	}
}

const USoldierData* ASoldier::GetSoldierData() const
{
	return SoldierData ? SoldierData.Get() : GetDefault<USoldierData>();
}

void ASoldier::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SetBodyColor(bDead ? CorpseColor : SoldierColor);
	SetPartColor(Visor, this, FLinearColor(0.02f, 0.02f, 0.02f));
	SetPartColor(SelectionRing, this, FLinearColor(0.1f, 1.f, 0.2f));
	SetPartColor(MoveMarker, this, MoveMarkerColor);
	SetPartColor(Tracer, this, FLinearColor(1.f, 0.85f, 0.3f));

	if (HasLabels())
	{
		const FText Number = FText::AsNumber(GetSquadNumber());
		NumberLabel->SetText(Number);
		MoveMarkerLabel->SetText(Number);
		NumberLabel->SetTextRenderColor(Team == ESquadTeam::Allies ? FColor::White : FColor(255, 170, 150));
	}

	UpdateModel();
	ApplyPostureVisuals();
}

void ASoldier::BeginPlay()
{
	Super::BeginPlay();

	// A soldier the server spawned during play (squad.SquadsPerTeam) was constructed here before its team,
	// number, colour and data assets arrived; they have now, so build its look again.
	if (GetNetMode() == NM_Client && !IsNetStartupActor())
	{
		OnConstruction(GetActorTransform());
	}

	// GDD §4.2 loadout: one rifle (loaded from the 70 rounds), grenades and a medkit.
	const USoldierData* D = GetSoldierData();
	Inventory = FLootInventory();
	EquipStartingWeapon(Weapon ? Weapon.Get() : GetMutableDefault<UWeaponData>());
	Inventory.Grenades = D->StartingGrenades;
	Inventory.Medkits = D->StartingMedkits;
	InitBuildables(); // GDD §4.2: C4 and wall reinforcement (SoldierBuild.cpp).
	Rng.Initialize(FMath::Rand());
	DesiredYaw = GetActorRotation().Yaw;
	// Clients animate every soldier, even one hidden behind a tree, so a throw or death that starts
	// out of view is already playing when it comes into view. A dedicated server never renders: skip it.
	Model->VisibilityBasedAnimTickOption = GetNetMode() == NM_DedicatedServer
		? EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered
		: EVisibilityBasedAnimTickOption::AlwaysTickPose;
	UpdateModel();
	ApplyPostureVisuals();

	Grid = AGridManager::Find(this);
	if (!Grid)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: no GridManager in level; soldier cannot move."), *GetName());
		return;
	}

	// Snap to the nearest free cell.
	FIntPoint Desired;
	Grid->WorldToCell(GetActorLocation(), Desired);
	FIntPoint Cell = Desired;
	Grid->FindNearestFreeCell(Desired, this, Cell);
	CurrentCell = Cell;
	DestinationCell = Cell;
	Grid->RegisterSoldier(this);
	SetActorLocation(Grid->CellToWorld(Cell));
}

void ASoldier::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Grid)
	{
		Grid->UnregisterSoldier(this);
	}
	Super::EndPlay(EndPlayReason);
}

ESoldierHealth ASoldier::GetHealthState() const
{
	if (bDead)
	{
		return ESoldierHealth::Dead;
	}
	if (StunRemaining > 0.f)
	{
		return ESoldierHealth::Stunned;
	}
	return bInjured ? ESoldierHealth::Injured : ESoldierHealth::Active;
}

// ------------------------------------------------------------------------------------------------ Orders

bool ASoldier::MoveToCell(FIntPoint Goal)
{
	return MoveToCellInGroup(Goal, TMap<const ASoldier*, FIntPoint>());
}

bool ASoldier::MoveToCellInGroup(FIntPoint Goal, const TMap<const ASoldier*, FIntPoint>& GroupPlan)
{
	if (!HasAuthority())
	{
		// The server plans the path. Send the squad-mates' slots too, so it applies the same cohesion check.
		ASquadPlayerController* PC = GetWorld() ? Cast<ASquadPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr;
		if (!PC || !PC->IsLocalController() || !CanAct())
		{
			return false;
		}
		FSoldierOrder Order;
		Order.Soldier = this;
		Order.Type = GroupPlan.Num() > 0 ? ESoldierOrderType::MoveInGroup : ESoldierOrderType::Move;
		Order.Cell = Goal;
		for (const TPair<const ASoldier*, FIntPoint>& Pair : GroupPlan)
		{
			Order.GroupSoldiers.Add(const_cast<ASoldier*>(Pair.Key));
			Order.GroupCells.Add(Pair.Value);
		}
		PC->QueueSoldierOrder(Order);
		ShowMoveMarker(Goal); // Shown at once; the server may still clamp the goal (GDD §4.3).
		return true;
	}
	if (!CanAct() || !Grid || !Grid->IsCellFreeAsDestination(Goal, this))
	{
		return false;
	}
	if (IsShocked())
	{
		ShowMessage(TEXT("SHOCKED")); // GDD §8.1: a shocked soldier cannot move.
		return false;
	}

	// Plan from where the soldier will be once its current step finishes.
	const FIntPoint From = bStepping ? StepCell : CurrentCell;
	TArray<FIntPoint> NewPath;
	if (!IsCohesiveDestination(Goal, &GroupPlan))
	{
		// GDD §4.3: too far from a squad-mate. Go as near the clicked cell as the limit allows instead.
		FIntPoint Clamped;
		if (!FindCohesiveDestination(Goal, &GroupPlan, Clamped, NewPath) || (Clamped == From && !IsMoving()))
		{
			ShowMessage(TEXT("SQUAD LIMIT"));
			return false;
		}
		ShowMessage(TEXT("SQUAD LIMIT"));
		Goal = Clamped;
	}
	else if (!Grid->FindPath(From, Goal, this, NewPath))
	{
		return false;
	}

	CancelGrenadeThrow(); // A new move order cancels a throw not yet released (the grenade is kept).
	GrenadeFollowThroughRemaining = 0.f;
	CancelMedkit(); // ...and a treatment in progress (the medkit is kept).
	CancelBuild(); // ...and a placement on a wall (the item is kept, GDD §13.3).
	ClearAttack();
	bHasFacingOrder = false;
	BlockedWaitTime = 0.f;
	Path = MoveTemp(NewPath);
	DestinationCell = Goal;
	ShowMoveMarker(Goal);
	return true;
}

// ------------------------------------------------------------------------------------------------ Squad cohesion

namespace
{
	float CellMetres(FIntPoint A, FIntPoint B)
	{
		return FVector2D::Distance(FVector2D(A), FVector2D(B)); // 1 cell = 1 m, centre to centre.
	}
}

FIntPoint ASoldier::GetCohesionCell(const TMap<const ASoldier*, FIntPoint>* GroupPlan) const
{
	// A dragged soldier goes wherever its dragger goes (it trails one cell behind).
	const ASoldier* Mover = DraggedBy.IsValid() ? DraggedBy.Get() : this;
	if (const FIntPoint* Planned = GroupPlan ? GroupPlan->Find(Mover) : nullptr)
	{
		return *Planned;
	}
	return Mover->DestinationCell;
}

bool ASoldier::IsCohesiveDestination(FIntPoint Goal, const TMap<const ASoldier*, FIntPoint>* GroupPlan) const
{
	const float Limit = GetSoldierData()->SquadCohesionLimit;
	if (Limit <= 0.f || !Grid)
	{
		return true;
	}
	// GDD §4.3: every living squad-mate counts (stunned, injured and dragged ones too); the dead don't.
	const FIntPoint Here = GetCohesionCell();
	for (const TWeakObjectPtr<ASoldier>& Weak : Grid->GetSoldiers())
	{
		const ASoldier* Mate = Weak.Get();
		if (!Mate || Mate->IsDead() || !IsSquadMate(Mate) || Mate->GetDraggedBy() == this)
		{
			continue; // The soldier this one drags comes along with it.
		}
		const float NewDistance = CellMetres(Goal, Mate->GetCohesionCell(GroupPlan));
		if (NewDistance <= Limit + KINDA_SMALL_NUMBER)
		{
			continue;
		}
		// Already beyond the limit (e.g. placed that way): allowed only if it doesn't spread the pair further.
		const float OldDistance = CellMetres(Here, Mate->GetCohesionCell());
		if (NewDistance > OldDistance + KINDA_SMALL_NUMBER)
		{
			return false;
		}
	}
	return true;
}

bool ASoldier::FindCohesiveDestination(FIntPoint Goal, const TMap<const ASoldier*, FIntPoint>* GroupPlan, FIntPoint& OutCell, TArray<FIntPoint>& OutPath) const
{
	if (!Grid)
	{
		return false;
	}
	// The soldier's own destination always passes (it never spreads the squad further), so nothing beyond it
	// from Goal needs looking at.
	const FIntPoint From = bStepping ? StepCell : CurrentCell;
	const int32 MaxRadius = FMath::CeilToInt(CellMetres(Goal, GetCohesionCell())) + 1;

	TArray<TPair<float, FIntPoint>> Candidates;
	float Nearest = TNumericLimits<float>::Max();
	for (int32 Radius = 0; Radius <= MaxRadius && Radius <= Nearest + 1.f; ++Radius)
	{
		for (int32 DY = -Radius; DY <= Radius; ++DY)
		{
			for (int32 DX = -Radius; DX <= Radius; ++DX)
			{
				if (FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) != Radius)
				{
					continue;
				}
				const FIntPoint Cell = Goal + FIntPoint(DX, DY);
				if (!Grid->IsCellFreeAsDestination(Cell, this) || !IsCohesiveDestination(Cell, GroupPlan))
				{
					continue;
				}
				const float Distance = CellMetres(Cell, Goal);
				Candidates.Emplace(Distance, Cell);
				Nearest = FMath::Min(Nearest, Distance);
			}
		}
	}
	// A wall can cut off the nearest cells: try a few in order of distance and take the first reachable one.
	Candidates.Sort([](const TPair<float, FIntPoint>& A, const TPair<float, FIntPoint>& B) { return A.Key < B.Key; });
	constexpr int32 MaxPathTries = 12;
	for (int32 i = 0; i < Candidates.Num() && i < MaxPathTries; ++i)
	{
		if (Grid->FindPath(From, Candidates[i].Value, this, OutPath))
		{
			OutCell = Candidates[i].Value;
			return true;
		}
	}
	// Fall back to the soldier's own destination: stay on course rather than break the limit.
	const FIntPoint Own = GetCohesionCell();
	if (IsCohesiveDestination(Own, GroupPlan) && Grid->IsCellFreeAsDestination(Own, this) && Grid->FindPath(From, Own, this, OutPath))
	{
		OutCell = Own;
		return true;
	}
	return false;
}

bool ASoldier::OrderStop()
{
	if (!HasAuthority())
	{
		return CanAct() && IsMoving() && ForwardOrder(ESoldierOrderType::Stop);
	}
	if (!CanAct() || !IsMoving())
	{
		return false;
	}
	StopAfterCurrentStep();
	CancelMedkit();
	CancelBuild();
	return true;
}

bool ASoldier::FaceCell(FIntPoint Cell)
{
	if (!HasAuthority())
	{
		return CanAct() && ForwardOrder(ESoldierOrderType::Face, Cell);
	}
	if (!CanAct() || !Grid)
	{
		return false;
	}
	const FVector ToCell = Grid->CellToWorld(Cell) - GetActorLocation();
	if (ToCell.SizeSquared2D() < 1.f)
	{
		return false;
	}
	// Soldiers can look any way at any time: while walking (they keep looking that way and walk on), while
	// treating a casualty, or mid-throw (the throw keeps its own facing until the grenade is gone). Only an
	// attack in progress is dropped, since the soldier turns its rifle away from the target.
	ClearAttack();
	DesiredYaw = ToCell.Rotation().Yaw;
	bHasFacingOrder = true;
	return true;
}

bool ASoldier::OrderAttack(ASoldier* Target)
{
	if (!HasAuthority())
	{
		return CanAct() && Target && Target->Team != Team && !Target->IsDead() && ForwardOrder(ESoldierOrderType::Attack, FIntPoint::ZeroValue, Target);
	}
	if (!CanAct() || !Target || Target == this || Target->IsDead() || Target->Team == Team)
	{
		return false;
	}
	if (IsDragging())
	{
		ShowMessage(TEXT("DRAGGING")); // GDD §5.4: can't shoot while dragging.
		return false;
	}
	const FHitChanceInfo Chance = GetHitChanceAgainst(Target);
	if (!Chance.CanFire())
	{
		ShowMessage(Chance.bInRange ? TEXT("NO LINE OF SIGHT") : TEXT("OUT OF RANGE"));
		return false;
	}

	StopAfterCurrentStep();
	CancelMedkit();
	CancelBuild();
	bHasFacingOrder = false;
	if (AttackTarget.Get() != Target)
	{
		AttackTarget = Target;
		AimRemaining = GetWeaponData()->AimTime * GetStressEffects().AimTimeMultiplier;
	}
	bAutoTarget = false; // An explicit order: stick with this target (GDD §5.3).
	return true;
}

bool ASoldier::OrderHoldFire(bool bHold)
{
	if (!HasAuthority())
	{
		return ForwardOrder(ESoldierOrderType::HoldFire, FIntPoint::ZeroValue, nullptr, bHold ? 1 : 0);
	}
	if (bDead)
	{
		return false;
	}
	bHoldFire = bHold;
	if (bHold && AttackTarget.IsValid())
	{
		ClearAttack(); // Cease fire now, whether the target was picked automatically or ordered.
	}
	AutoTargetScanRemaining = 0.f; // On release, engage straight away if an enemy is in sight.
	return true;
}

bool ASoldier::ThrowGrenadeAt(FIntPoint Cell)
{
	if (!HasAuthority())
	{
		return CanAct() && GrenadeCooldown <= 0.f && !IsThrowingGrenade() && ForwardOrder(ESoldierOrderType::Grenade, Cell);
	}
	if (!CanAct() || !Grid || !Grid->IsInBounds(Cell) || Cell == CurrentCell || GrenadeCooldown > 0.f || IsThrowingGrenade())
	{
		return false;
	}
	// GDD §7: a soldier ordered to throw while moving finishes the step it is on, then steps back onto the cell it
	// came from, pulling the pin, and throws from there. One standing still throws where it stands. Range and
	// cover are checked now from the cell it will throw from, and again on release.
	const FIntPoint Stand = GetStandCell();
	if (Cell == Stand)
	{
		return false;
	}
	const bool bStepBack = IsMoving() && !IsShocked(); // GDD §8.1: a shocked soldier can't move.
	FIntPoint ThrowFrom = Stand;
	if (bStepBack)
	{
		FIntPoint Back;
		if (FindGrenadeStepBackCell(Stand, bStepping || bHasPreviousCell, bStepping ? CurrentCell : PreviousCell, Cell, Back))
		{
			ThrowFrom = Back;
		}
	}
	FGrenadeThrow Throw;
	if (!PlanGrenadeThrow(ThrowFrom, Cell, Throw))
	{
		return false;
	}

	StopAfterCurrentStep();
	CancelMedkit();
	CancelBuild();
	PendingGrenadeCell = Cell;
	GrenadeWindupRemaining = 0.f;
	GrenadeFollowThroughRemaining = 0.f;
	// UpdateGrenadeThrow takes the step back once the current step is done. Standing still there is no step back:
	// marking it already decided sends UpdateGrenadeThrow straight to the wind-up.
	bGrenadeStepBack = true;
	bGrenadeStepBackStarted = !bStepBack;
	GrenadeStepBackFrom = Stand;
	bThrowAfterStepBack = false;
	++ThrowCount;
	// Firing pauses for the throw (Tick skips UpdateAttack) and the soldier turns to the throw even while it has
	// a target: UpdateFacing gives the throw priority, then turns back to the target once the grenade is gone.
	// It keeps facing the throw while it steps back. With no target it keeps looking the way it threw.
	if (!AttackTarget.IsValid())
	{
		DesiredYaw = (Grid->CellToWorld(Cell) - Grid->CellToWorld(Stand)).Rotation().Yaw;
		bHasFacingOrder = true;
	}
	return true;
}

void ASoldier::CancelGrenadeThrow()
{
	bGrenadeStepBack = false;
	bGrenadeStepBackStarted = false;
	bThrowAfterStepBack = false;
	GrenadeWindupRemaining = 0.f;
}

bool ASoldier::FindGrenadeStepBackCell(FIntPoint From, bool bHasPrevious, FIntPoint Previous, FIntPoint Target, FIntPoint& OutCell) const
{
	if (!Grid)
	{
		return false;
	}
	auto CanStepTo = [this, From, Target](FIntPoint C)
	{
		const FIntPoint D = C - From;
		if (C == From || C == Target || FMath::Abs(D.X) > 1 || FMath::Abs(D.Y) > 1 || !Grid->IsCellFreeAsDestination(C, this))
		{
			return false;
		}
		// Same rule as walking: no squeezing diagonally past a blocked corner.
		return D.X == 0 || D.Y == 0
			|| (Grid->IsCellPassable(FIntPoint(C.X, From.Y), this) && Grid->IsCellPassable(FIntPoint(From.X, C.Y), this));
	};

	if (bHasPrevious && CanStepTo(Previous))
	{
		OutCell = Previous;
		return true;
	}

	// No usable previous cell: the cell directly away from the target (nearest of the 8 directions), then the
	// two either side of it.
	const FVector2D Away = FVector2D(From - Target);
	if (Away.IsNearlyZero())
	{
		return false;
	}
	static const FIntPoint Directions[8] = { {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1} };
	const int32 Octant = FMath::RoundToInt(FMath::Atan2(Away.Y, Away.X) / (UE_PI / 4.f));
	for (const int32 Offset : { 0, 1, -1 })
	{
		const FIntPoint C = From + Directions[((Octant + Offset) % 8 + 8) % 8];
		if (CanStepTo(C))
		{
			OutCell = C;
			return true;
		}
	}
	return false;
}

bool ASoldier::PlanGrenadeThrow(FIntPoint FromCell, FIntPoint Cell, FGrenadeThrow& OutThrow)
{
	if (IsDragging())
	{
		ShowMessage(TEXT("DRAGGING")); // GDD §5.4: can't throw while dragging.
		return false;
	}
	if (Inventory.Grenades <= 0)
	{
		ShowMessage(TEXT("NO GRENADES"));
		return false;
	}

	// GDD §7: thrown from a cell centre (the cell stepped back to), so the throw is deterministic.
	const USoldierData* D = GetSoldierData();
	const FVector Origin = Grid->CellToWorld(FromCell);
	const FVector Landing = Grid->CellToWorld(Cell);
	if (FVector::Dist2D(Origin, Landing) / UUPerMetre > D->GrenadeMaxThrowRange)
	{
		ShowMessage(TEXT("TOO FAR"));
		return false;
	}

	// GDD §7: grenades go over half-height cover and through windows, never through or over full walls.
	// Released at the soldier's current stance height above FromCell.
	const FVector Location = GetActorLocation();
	const FVector Release = FVector(Origin.X, Origin.Y, Location.Z) + (GetAimPoint() - Location) + FVector(0.f, 0.f, 30.f);
	if (!AGrenade::PlanThrow(*Grid, Release, Landing, *D, OutThrow))
	{
		ShowMessage(TEXT("BLOCKED"));
		return false;
	}
	return true;
}

void ASoldier::UpdateGrenadeThrow(float DeltaSeconds)
{
	if (GrenadeFollowThroughRemaining > 0.f)
	{
		GrenadeFollowThroughRemaining = CanAct() && !IsMoving() ? FMath::Max(0.f, GrenadeFollowThroughRemaining - DeltaSeconds) : 0.f;
	}
	if (!IsThrowingGrenade())
	{
		return;
	}
	if (!CanAct())
	{
		CancelGrenadeThrow(); // Stunned, killed or grabbed mid-throw: the grenade stays in the pouch.
		return;
	}

	// GDD §7: step back onto the previous cell (pulling the pin), then wind up.
	if (bGrenadeStepBack)
	{
		if (bStepping)
		{
			return; // Finishing the step it was on when ordered, or the step back itself.
		}
		if (!bGrenadeStepBackStarted)
		{
			bGrenadeStepBackStarted = true;
			GrenadeStepBackFrom = CurrentCell;
			FIntPoint Back;
			if (!IsShocked() && FindGrenadeStepBackCell(CurrentCell, bHasPreviousCell, PreviousCell, PendingGrenadeCell, Back))
			{
				// One step, walked at the stance's speed, still facing the throw (UpdateFacing).
				Path.Reset();
				BlockedWaitTime = 0.f;
				StepCell = Back;
				DestinationCell = Back;
				bStepping = true;
				return;
			}
		}
		// Arrived, or there was nowhere to step to (it throws where it stands).
		bThrowAfterStepBack = CurrentCell != GrenadeStepBackFrom;
		bGrenadeStepBack = false;
		bGrenadeStepBackStarted = false;
		GrenadeWindupRemaining = FMath::Max(GetSoldierData()->GrenadeThrowDelay, KINDA_SMALL_NUMBER);
		GrenadeWindupDuration = GrenadeWindupRemaining;
		return;
	}

	GrenadeWindupRemaining -= DeltaSeconds;
	if (GrenadeWindupRemaining > 0.f)
	{
		return;
	}
	GrenadeWindupRemaining = 0.f;

	bThrowAfterStepBack = false;

	// Re-plan from where the soldier is now (its stance may have changed during the wind-up).
	FGrenadeThrow Throw;
	if (!Grid || PendingGrenadeCell == CurrentCell || !PlanGrenadeThrow(CurrentCell, PendingGrenadeCell, Throw))
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGrenade* Grenade = GetWorld()->SpawnActor<AGrenade>(AGrenade::StaticClass(), FTransform(Throw.Start), Params);
	if (!Grenade)
	{
		return;
	}
	const USoldierData* D = GetSoldierData();
	Grenade->Launch(Throw, *D);
	if (VisualData)
	{
		Grenade->SetBodyMesh(VisualData->GetGrenadeMesh(Team));
	}
	++GrenadesReleased;
	if (USquadMatchStats* Stats = USquadMatchStats::Get(this))
	{
		Stats->RecordGrenadeThrown(*this, CurrentCell, PendingGrenadeCell);
	}

	--Inventory.Grenades;
	GrenadeCooldown = D->GrenadeThrowCooldown;
	GrenadeFollowThroughRemaining = D->GrenadeFollowThroughTime;
}

bool ASoldier::OrderStance(ESoldierStance NewStance)
{
	if (!HasAuthority())
	{
		return CanAct() && !bInjured && !IsDragging() && ForwardOrder(ESoldierOrderType::Stance, FIntPoint::ZeroValue, nullptr, static_cast<int32>(NewStance));
	}
	if (!CanAct() || bInjured || IsDragging())
	{
		return false;
	}
	CancelMedkit();
	CancelBuild();
	Stance = NewStance;
	ApplyPostureVisuals();
	return true;
}

ESoldierStance ASoldier::GetNextStance() const
{
	switch (Stance)
	{
	case ESoldierStance::Standing: return ESoldierStance::Crouched;
	case ESoldierStance::Crouched: return ESoldierStance::Prone;
	default:                       return ESoldierStance::Standing;
	}
}

// ------------------------------------------------------------------------------------------------ Damage

FOnSoldierShotResolved ASoldier::OnShotResolved;

void ASoldier::ReceiveHit(float DistanceMetres, const FKillCause& Cause)
{
	if (bDead || IsDebugInvulnerable())
	{
		return;
	}
	CancelMedkit(); // GDD §5.2: a medic that is hit stops treating (the medkit is kept).
	CancelBuild(); // GDD §13.3: so does a soldier placing C4 or a reinforcement.
	// GDD §5.1: hit from beyond 5 m injures; a second hit, or any hit from 5 m or closer, kills.
	if (bInjured || DistanceMetres <= GetSoldierData()->CloseRangeKillDistance)
	{
		Die(Cause);
		return;
	}
	bInjured = true;
	// GDD §5.1: an untreated injury kills after BleedOutTime; the death is credited to this hit.
	BleedOutRemaining = GetSoldierData()->BleedOutTime;
	InjuredBy = Cause.Killer;
	InjuryCause = Cause;
	InjuryCause.Killer = nullptr; // Held weakly in InjuredBy instead.
	DamageFlashRemaining = DamageFlashTime;
	ApplyPostureVisuals();
}

bool ASoldier::IsBleedOutPaused() const
{
	const ASoldier* Medic = MedkitTreatedBy.Get();
	return Medic && Medic->IsApplyingMedkit() && Medic->MedkitPatient.Get() == this;
}

void ASoldier::UpdateBleedOut(float DeltaSeconds)
{
	if (!bInjured || bDead || BleedOutRemaining <= 0.f || IsBleedOutPaused())
	{
		return;
	}
	BleedOutRemaining = FMath::Max(0.f, BleedOutRemaining - DeltaSeconds);
	if (BleedOutRemaining <= 0.f && !IsDebugInvulnerable())
	{
		FKillCause Cause = InjuryCause;
		Cause.Killer = InjuredBy.Get();
		Cause.bBledOut = true;
		Die(Cause);
	}
}

void ASoldier::ApplyStun(float Seconds)
{
	if (bDead || Seconds <= 0.f || IsDebugInvulnerable())
	{
		return;
	}
	StunRemaining = FMath::Max(StunRemaining, Seconds);
	DamageFlashRemaining = DamageFlashTime;
	UnderFireRemaining = UnderFireTime;
	CancelMedkit();
	CancelBuild();
	StopAfterCurrentStep();
	ClearAttack();
	bHasFacingOrder = false;
	ApplyPostureVisuals(); // Knocked prone for the duration.
}

void ASoldier::Die(const FKillCause& Cause)
{
	if (bDead)
	{
		return;
	}
	if (HasAuthority())
	{
		if (ASquadGameState* GameState = GetWorld()->GetGameState<ASquadGameState>())
		{
			FKillFeedEntry Entry;
			Entry.Killer = Cause.Killer ? Cause.Killer->GetKillFeedIdentity() : GetKillFeedIdentity();
			Entry.Victim = GetKillFeedIdentity();
			Entry.Method = Cause.Method;
			Entry.WeaponName = Cause.Method == EKillMethod::Rifle ? SquadLoot::WeaponName(Cause.Weapon) : FString();
			Entry.ChancePercent = Cause.ChancePercent;
			Entry.bBledOut = Cause.bBledOut;
			GameState->AddKill(Entry);
		}
		if (USquadMatchStats* Stats = USquadMatchStats::Get(this))
		{
			Stats->RecordDeath(*this, Cause);
		}
	}
	StopDragging(); // A dead dragger lets go; a dead soldier being dragged keeps being dragged.
	CancelMedkit(); // A dead medic stops; a dead patient is noticed by its medic next tick.
	CancelBuild();
	bDead = true;
	StunRemaining = 0.f;
	BleedOutRemaining = 0.f;
	CancelGrenadeThrow();
	Stress = 0.f;
	StressDecayHold = 0.f;
	Path.Reset();
	if (!IsBeingDragged())
	{
		bStepping = false;
	}
	DestinationCell = CurrentCell;
	ClearAttack();
	bReloading = false;
	ApplyDeathVisuals();
}

void ASoldier::ApplyDeathVisuals()
{
	SetSelected(false);
	MoveMarker->SetVisibility(false);
	Tracer->SetVisibility(false);
	if (HasLabels())
	{
		MoveMarkerLabel->SetVisibility(false);
		NumberLabel->SetVisibility(false);
		StatusLabel->SetVisibility(false);
		HoldFireLabel->SetVisibility(false);
	}
	SetBodyColor(CorpseColor);
	ApplyPostureVisuals();
}

void ASoldier::SetOwningPlayer(APlayerState* Player)
{
	OwningPlayer = Player;
}

APlayerState* ASoldier::GetOwningPlayer() const
{
	return OwningPlayer.Get();
}

bool ASoldier::IsOwnedByLocalPlayer() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC && PC->PlayerState && OwningPlayer.Get() == PC->PlayerState;
}

FKillFeedPlayer ASoldier::GetKillFeedIdentity() const
{
	FKillFeedPlayer Identity;
	Identity.Team = Team;
	Identity.Rank = ASquadPlayerState::RankAbbreviationForElo(100); // New-player rank unless the owner says otherwise.
	if (const ASquadPlayerState* Player = Cast<ASquadPlayerState>(OwningPlayer.Get()))
	{
		Identity.Rank = Player->GetRankAbbreviation();
		Identity.Regiment = Player->RegimentTag;
		Identity.Name = Player->GetPlayerName();
	}
	else if (const APlayerState* Plain = OwningPlayer.Get())
	{
		Identity.Name = Plain->GetPlayerName();
	}
	else
	{
		// No player commands this soldier (test enemies placed in the level).
		Identity.Name = Team == ESquadTeam::Allies ? TEXT("Allied Squad") : TEXT("Axis Squad");
	}
	return Identity;
}

// ------------------------------------------------------------------------------------------------ Stress

EStressTier ASoldier::GetStressTier() const
{
	return GetSoldierData()->GetStressTier(Stress);
}

const FStressTierEffects& ASoldier::GetStressEffects() const
{
	return GetSoldierData()->GetStressEffects(GetStressTier());
}

void ASoldier::AddStress(float Amount)
{
	if (bDead || Amount <= 0.f)
	{
		return;
	}
	const bool bWasShocked = IsShocked();
	StressReceivedTotal += Amount;
	Stress = FMath::Clamp(Stress + Amount, 0.f, GetSoldierData()->MaxStress);
	// GDD §8.2: stress only decays while no stress-gain triggers are active.
	StressDecayHold = GetSoldierData()->StressDecayDelay;
	if (!bWasShocked && IsShocked())
	{
		OnShocked();
	}
}

void ASoldier::SetStress(float NewStress)
{
	if (bDead)
	{
		return;
	}
	const bool bWasShocked = IsShocked();
	Stress = FMath::Clamp(NewStress, 0.f, GetSoldierData()->MaxStress);
	if (!bWasShocked && IsShocked())
	{
		OnShocked();
	}
}

void ASoldier::OnShocked()
{
	// GDD §8.1: a shocked soldier cannot move. It finishes the step it is on so it stays on a cell, and lets go
	// of anyone it is dragging (dragging means moving). It can still fire, with maximum penalties.
	StopAfterCurrentStep();
	StopDragging();
	CancelMedkit(); // GDD §5.2: a Shocked medic stops treating (the medkit is kept).
}

void ASoldier::UpdateStress(float DeltaSeconds)
{
	if (StressDecayHold > 0.f)
	{
		StressDecayHold = FMath::Max(0.f, StressDecayHold - DeltaSeconds);
		return;
	}
	Stress = FMath::Max(0.f, Stress - GetSoldierData()->StressDecayPerSecond * DeltaSeconds);
}

void ASoldier::ApplyCrossfireStress(const ASoldier* Target)
{
	// GDD §8.2: a squad-mate firing from behind a soldier at a target in front of it stresses that soldier.
	// "In front" = within CrossfireRadius of the line of fire, between the shooter and the target.
	const USoldierData* D = GetSoldierData();
	if (!Grid || !Target || D->CrossfireStressPerShot <= 0.f)
	{
		return;
	}
	const FVector2D From(GetActorLocation());
	const FVector2D To(Target->GetActorLocation());
	const FVector2D Line = To - From;
	const float Length = Line.Size();
	if (Length < KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector2D Dir = Line / Length;
	const float Radius = D->CrossfireRadius * UUPerMetre;
	const float MinAlong = D->CrossfireMinDistanceFromShooter * UUPerMetre;

	for (const TWeakObjectPtr<ASoldier>& Weak : Grid->GetSoldiers())
	{
		ASoldier* Mate = Weak.Get();
		if (!Mate || Mate == this || Mate == Target || Mate->Team != Team || Mate->IsDead())
		{
			continue;
		}
		const FVector2D ToMate = FVector2D(Mate->GetActorLocation()) - From;
		const float Along = FVector2D::DotProduct(ToMate, Dir);
		if (Along < MinAlong || Along >= Length)
		{
			continue;
		}
		const float Lateral = FMath::Abs(FVector2D::CrossProduct(Dir, ToMate));
		if (Lateral <= Radius)
		{
			Mate->AddStress(D->CrossfireStressPerShot);
		}
	}
}

// ------------------------------------------------------------------------------------------------ Tick

void ASoldier::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		TickClient(DeltaSeconds);
		return;
	}

	TickCosmetics(DeltaSeconds); // A listen server / standalone game renders too; a dedicated server skips most of it.

	if (StunRemaining > 0.f)
	{
		StunRemaining = FMath::Max(0.f, StunRemaining - DeltaSeconds);
		if (StunRemaining <= 0.f && !bDead)
		{
			ApplyPostureVisuals(); // Stun over: back to the soldier's own stance.
		}
	}
	GrenadeCooldown = FMath::Max(0.f, GrenadeCooldown - DeltaSeconds);
	UnderFireRemaining = FMath::Max(0.f, UnderFireRemaining - DeltaSeconds);
	RecoilMrad = FMath::Max(0.f, RecoilMrad - UBallisticsSubsystem::GetData()->RecoilRecoveryMradPerSecond * DeltaSeconds);
	DamageFlashRemaining = FMath::Max(0.f, DamageFlashRemaining - DeltaSeconds);
	MessageRemaining = FMath::Max(0.f, MessageRemaining - DeltaSeconds);

	if (bDead)
	{
		// A corpse only moves when someone drags it.
		if (bStepping)
		{
			UpdateMovement(DeltaSeconds);
		}
		WriteNetState();
		return;
	}

	UpdateBleedOut(DeltaSeconds);
	if (bDead)
	{
		WriteNetState(); // Bled out this tick.
		return;
	}
	UpdateDrag();
	UpdateStress(DeltaSeconds);
	UpdateGrenadeThrow(DeltaSeconds);
	UpdateMedkit(DeltaSeconds);
	UpdateBuild(DeltaSeconds);
	// Hands are busy with the grenade during the wind-up, and with the medkit during a treatment (GDD §5.2).
	if (!IsBusyThrowing() && !IsApplyingMedkit() && !IsBuilding())
	{
		UpdateAutoTarget(DeltaSeconds);
		UpdateAttack(DeltaSeconds);
	}
	UpdateMovement(DeltaSeconds);
	UpdateFacing(DeltaSeconds);
	UpdateWeaponSetup(DeltaSeconds);
	WriteNetState();
}

void ASoldier::TickCosmetics(float DeltaSeconds)
{
	UpdateLocalVisibility();

	HitPreviewRemaining = FMath::Max(0.f, HitPreviewRemaining - DeltaSeconds);
	if (TracerRemaining > 0.f)
	{
		TracerRemaining -= DeltaSeconds;
		if (TracerRemaining <= 0.f)
		{
			Tracer->SetVisibility(false);
		}
	}

	if (HasModel())
	{
		// Cosmetic: the rifle in hand follows the inventory (looting swaps it, a looted corpse loses it), and
		// the grenade shows in the hand for the part of the throw clip between the pouch and the release.
		RefreshWeaponModel();
		UpdateSetupWeaponVisual(DeltaSeconds);
		const USoldierAnimInstance* Anim = Cast<USoldierAnimInstance>(Model->GetAnimInstance());
		const bool bGrenadeInHand = Anim && Anim->IsGrenadeInHand();
		if (GrenadeModel->IsVisible() != bGrenadeInHand)
		{
			GrenadeModel->SetVisibility(bGrenadeInHand);
		}

		// Leaning out past a wall's edge (the peek clips): slide the model towards the open side so the rifle
		// clears the wall's end and points down its face. Mirrors when the anim instance picks a peek clip.
		const bool bPeekPose = !IsMoving() && !IsDowned() && !IsDragging() && GetEffectiveStance() != ESoldierStance::Prone
			&& GetLeaningCoverType() == ECoverType::WallCorner;
		const float TargetShift = bPeekPose ? GetPeekSide() * VisualData->PeekSideShift : 0.f;
		const FVector ModelLocation = Model->GetRelativeLocation();
		if (!FMath::IsNearlyEqual(ModelLocation.Y, TargetShift, 0.1f))
		{
			Model->SetRelativeLocation(FVector(ModelLocation.X, FMath::FInterpTo(ModelLocation.Y, TargetShift, DeltaSeconds, VisualData->PeekShiftSpeed), ModelLocation.Z));
		}
	}

	if (!bDead)
	{
		FaceCamera(NumberLabel);
		FaceCamera(StatusLabel);
		UpdateMoveMarker(DeltaSeconds);
		UpdateStatusLabel();
		UpdateHoldFireLabel();
	}
}

void ASoldier::TickClient(float DeltaSeconds)
{
	// Count timers down between server updates so progress bars and clip times run smoothly; each update resets
	// them to the server's values. Nothing here decides anything: the server does.
	// A timer the server has paused (a medkit waiting for both soldiers to stop) changes nothing, so no update comes:
	// stop extrapolating shortly after the last one instead of running ahead of the server.
	const bool bExtrapolate = GetWorld() && GetWorld()->GetTimeSeconds() - LastNetStateTime < 0.25f;
	auto CountDown = [DeltaSeconds, bExtrapolate](float& Value) { if (bExtrapolate) { Value = FMath::Max(0.f, Value - DeltaSeconds); } };
	CountDown(GrenadeCooldown);
	CountDown(UnderFireRemaining);
	CountDown(DamageFlashRemaining);
	CountDown(MessageRemaining);
	CountDown(AimRemaining);
	CountDown(GrenadeWindupRemaining);
	CountDown(GrenadeFollowThroughRemaining);
	if (bReloading) { CountDown(ReloadRemaining); }
	if (bSettingUpWeapon) { CountDown(SetupRemaining); }
	if (bApplyingMedkit) { CountDown(MedkitRemaining); }
	if (bBuildChanneling) { CountDown(BuildRemaining); }
	if (StunRemaining > 0.f)
	{
		CountDown(StunRemaining);
		if (StunRemaining <= 0.f && !bDead)
		{
			ApplyPostureVisuals();
		}
	}

	// Glide to the server's position at a little over walking pace (faster when far behind), and snap if a
	// teleport or a long hitch left it far away.
	if (bHasNetLocation)
	{
		const FVector Location = GetActorLocation();
		const FVector Target = NetState.Location;
		const float Distance = FVector::Dist(Location, Target);
		if (Distance > 300.f)
		{
			SetActorLocation(Target);
		}
		else if (Distance > 0.1f)
		{
			const float Speed = FMath::Max(GetCurrentMoveSpeed() * 1.25f, Distance * 8.f);
			SetActorLocation(FMath::VInterpConstantTo(Location, Target, DeltaSeconds, Speed));
		}
		const FRotator Desired(0.f, NetState.Yaw, 0.f);
		if (!GetActorRotation().Equals(Desired, 0.5f))
		{
			SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), Desired, DeltaSeconds, TurnSpeed * 1.5f));
		}
	}

	TickCosmetics(DeltaSeconds);
}

void ASoldier::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASoldier, NetState);
	// Identity: set once (in the level, or by the game mode before a spawned soldier begins play).
	DOREPLIFETIME_CONDITION(ASoldier, Team, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ASoldier, SquadId, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ASoldier, SquadIndex, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ASoldier, SoldierColor, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ASoldier, Weapon, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ASoldier, SoldierData, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ASoldier, VisualData, COND_InitialOnly);
}

void ASoldier::WriteNetState()
{
	FSoldierNetState& S = NetState;
	S.Location = GetActorLocation();
	S.Yaw = GetActorRotation().Yaw;
	S.CurrentCell = CurrentCell;
	S.DestinationCell = DestinationCell;
	S.StepCell = StepCell;
	S.bStepping = bStepping;
	S.Path = Path;

	S.Stance = Stance;
	S.bInjured = bInjured;
	S.bDead = bDead;
	S.StunRemaining = StunRemaining;
	S.BleedOutRemaining = BleedOutRemaining;
	S.Stress = Stress;
	S.bSpottedByEnemy = bSpottedByEnemy;
	S.OwningPlayer = OwningPlayer.Get();

	S.AttackTarget = AttackTarget.Get();
	S.bAutoTarget = bAutoTarget;
	S.bHoldFire = bHoldFire;
	S.AimRemaining = AimRemaining;
	S.LastHitChance = LastHitChance;
	S.Inventory = Inventory;
	S.bWeaponSetUp = bWeaponSetUp;
	S.bSettingUpWeapon = bSettingUpWeapon;
	S.SetupRemaining = SetupRemaining;
	S.SetupDuration = SetupDuration;
	S.SetupYaw = SetupYaw;
	S.bReloading = bReloading;
	S.ReloadRemaining = ReloadRemaining;
	S.ReloadDuration = ReloadDuration;
	S.UnderFireRemaining = UnderFireRemaining;
	S.DamageFlashRemaining = DamageFlashRemaining;
	S.ShotCount = ShotCount;
	S.HitsLanded = HitsLanded;
	S.RoundsFiredStat = RoundsFiredStat;
	S.StressReceivedTotal = StressReceivedTotal;

	S.GrenadeCooldown = GrenadeCooldown;
	S.GrenadeWindupRemaining = GrenadeWindupRemaining;
	S.GrenadeWindupDuration = GrenadeWindupDuration;
	S.GrenadeFollowThroughRemaining = GrenadeFollowThroughRemaining;
	S.PendingGrenadeCell = PendingGrenadeCell;
	S.bGrenadeStepBack = bGrenadeStepBack;
	S.bGrenadeStepBackStarted = bGrenadeStepBackStarted;
	S.bThrowAfterStepBack = bThrowAfterStepBack;
	S.GrenadeStepBackFrom = GrenadeStepBackFrom;
	S.ThrowCount = ThrowCount;
	S.GrenadesReleased = GrenadesReleased;

	S.DraggedSoldier = DraggedSoldier.Get();
	S.DraggedBy = DraggedBy.Get();
	S.MedkitPatient = MedkitPatient.Get();
	S.MedkitTreatedBy = MedkitTreatedBy.Get();
	S.bApplyingMedkit = bApplyingMedkit;
	S.MedkitRemaining = MedkitRemaining;
	S.BuildableCounts.SetNumZeroed(StaticEnum<EBuildableKind>()->NumEnums() - 1); // Every kind (NumEnums counts _MAX).
	for (const TPair<EBuildableKind, int32>& Pair : BuildableCounts)
	{
		if (S.BuildableCounts.IsValidIndex(static_cast<int32>(Pair.Key)))
		{
			S.BuildableCounts[static_cast<int32>(Pair.Key)] = Pair.Value;
		}
	}
	S.BuildWall = BuildWall.Get();
	S.BuildKind = BuildKind;
	S.bBuildChanneling = bBuildChanneling;
	S.BuildRemaining = BuildRemaining;
	S.BuildDuration = BuildDuration;

	S.Message = Message;
	S.MessageRemaining = MessageRemaining;
}

void ASoldier::OnRep_NetState()
{
	LastNetStateTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	ReadNetState();
}

void ASoldier::ReadNetState()
{
	const FSoldierNetState& S = NetState;
	const bool bWasDead = bDead;
	const bool bPostureChanged = Stance != S.Stance || bInjured != S.bInjured || bDead != S.bDead
		|| (StunRemaining > 0.f) != (S.StunRemaining > 0.f) || DraggedBy.Get() != S.DraggedBy || DraggedSoldier.Get() != S.DraggedSoldier;

	if (!bHasNetLocation)
	{
		// First update: start where the server has the soldier.
		SetActorLocationAndRotation(S.Location, FRotator(0.f, S.Yaw, 0.f));
		bHasNetLocation = true;
	}
	CurrentCell = S.CurrentCell;
	DestinationCell = S.DestinationCell;
	StepCell = S.StepCell;
	bStepping = S.bStepping;
	Path = S.Path;

	Stance = S.Stance;
	bInjured = S.bInjured;
	bDead = S.bDead;
	StunRemaining = S.StunRemaining;
	BleedOutRemaining = S.BleedOutRemaining;
	Stress = S.Stress;
	bSpottedByEnemy = S.bSpottedByEnemy;
	OwningPlayer = S.OwningPlayer;

	AttackTarget = S.AttackTarget;
	bAutoTarget = S.bAutoTarget;
	bHoldFire = S.bHoldFire;
	AimRemaining = S.AimRemaining;
	LastHitChance = S.LastHitChance;
	Inventory = S.Inventory;
	bWeaponSetUp = S.bWeaponSetUp;
	bSettingUpWeapon = S.bSettingUpWeapon;
	SetupRemaining = S.SetupRemaining;
	SetupDuration = S.SetupDuration;
	SetupYaw = S.SetupYaw;
	bReloading = S.bReloading;
	ReloadRemaining = S.ReloadRemaining;
	ReloadDuration = S.ReloadDuration;
	UnderFireRemaining = S.UnderFireRemaining;
	DamageFlashRemaining = S.DamageFlashRemaining;
	ShotCount = S.ShotCount;
	HitsLanded = S.HitsLanded;
	RoundsFiredStat = S.RoundsFiredStat;
	StressReceivedTotal = S.StressReceivedTotal;

	GrenadeCooldown = S.GrenadeCooldown;
	GrenadeWindupRemaining = S.GrenadeWindupRemaining;
	GrenadeWindupDuration = S.GrenadeWindupDuration;
	GrenadeFollowThroughRemaining = S.GrenadeFollowThroughRemaining;
	PendingGrenadeCell = S.PendingGrenadeCell;
	bGrenadeStepBack = S.bGrenadeStepBack;
	bGrenadeStepBackStarted = S.bGrenadeStepBackStarted;
	bThrowAfterStepBack = S.bThrowAfterStepBack;
	GrenadeStepBackFrom = S.GrenadeStepBackFrom;
	ThrowCount = S.ThrowCount;
	GrenadesReleased = S.GrenadesReleased;

	DraggedSoldier = S.DraggedSoldier;
	DraggedBy = S.DraggedBy;
	MedkitPatient = S.MedkitPatient;
	MedkitTreatedBy = S.MedkitTreatedBy;
	bApplyingMedkit = S.bApplyingMedkit;
	MedkitRemaining = S.MedkitRemaining;
	BuildableCounts.Reset();
	for (int32 Kind = 0; Kind < S.BuildableCounts.Num(); ++Kind)
	{
		BuildableCounts.Add(static_cast<EBuildableKind>(Kind), S.BuildableCounts[Kind]);
	}
	BuildWall = S.BuildWall;
	BuildKind = S.BuildKind;
	bBuildChanneling = S.bBuildChanneling;
	BuildRemaining = S.BuildRemaining;
	BuildDuration = S.BuildDuration;

	// A refusal this client showed itself ("TOO FAR TO DRAG") stays up until the server has one of its own.
	if (S.MessageRemaining > 0.f || MessageRemaining <= 0.f)
	{
		Message = S.Message;
		MessageRemaining = S.MessageRemaining;
	}

	if (bDead && !bWasDead)
	{
		ApplyDeathVisuals();
	}
	else if (bPostureChanged)
	{
		ApplyPostureVisuals();
	}
}

bool ASoldier::ForwardOrder(ESoldierOrderType Type, FIntPoint Cell, ASoldier* Other, int32 Param)
{
	ASquadPlayerController* PC = GetWorld() ? Cast<ASquadPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr;
	if (!PC || !PC->IsLocalController() || bDead)
	{
		return false;
	}
	FSoldierOrder Order;
	Order.Soldier = this;
	Order.Type = Type;
	Order.Cell = Cell;
	Order.Other = Other;
	Order.Param = Param;
	PC->QueueSoldierOrder(Order);
	return true; // Sent. The server may still refuse it; its reason comes back on the status line.
}

void ASoldier::ExecuteOrder(const FSoldierOrder& Order)
{
	if (!HasAuthority())
	{
		return; // Orders only ever run on the server.
	}
	ASoldier* Other = Order.Other.Get();
	switch (Order.Type)
	{
	case ESoldierOrderType::Move:         MoveToCell(Order.Cell); break;
	case ESoldierOrderType::MoveInGroup:
	{
		TMap<const ASoldier*, FIntPoint> Plan;
		for (int32 I = 0; I < Order.GroupSoldiers.Num() && I < Order.GroupCells.Num(); ++I)
		{
			if (const ASoldier* Mate = Order.GroupSoldiers[I].Get())
			{
				Plan.Add(Mate, Order.GroupCells[I]);
			}
		}
		MoveToCellInGroup(Order.Cell, Plan);
		break;
	}
	case ESoldierOrderType::Stop:         OrderStop(); break;
	case ESoldierOrderType::Face:         FaceCell(Order.Cell); break;
	case ESoldierOrderType::Attack:       OrderAttack(Other); break;
	case ESoldierOrderType::Stance:       OrderStance(static_cast<ESoldierStance>(FMath::Clamp(Order.Param, 0, 2))); break;
	case ESoldierOrderType::HoldFire:     OrderHoldFire(Order.Param != 0); break;
	case ESoldierOrderType::Grenade:      ThrowGrenadeAt(Order.Cell); break;
	case ESoldierOrderType::StartDrag:    StartDragging(Other); break;
	case ESoldierOrderType::StopDrag:     StopDragging(); break;
	case ESoldierOrderType::Medkit:       OrderUseMedkit(Other); break;
	case ESoldierOrderType::CancelMedkit: CancelMedkit(); break;
	case ESoldierOrderType::CancelBuild:  CancelBuild(); break;
	}
	ForceNetUpdate(); // Let the player see the result without waiting for the next regular update.
}

void ASoldier::MulticastShot_Implementation(FVector_NetQuantize From, FVector_NetQuantize To, bool bHit)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	ShowTracer(From, To, bHit);
	if (VisualData)
	{
		VisualData->PlayCombatSound(this, GetWeaponData()->FireSound, From);
	}
}

void ASoldier::MulticastReloadSound_Implementation()
{
	if (VisualData && GetNetMode() != NM_DedicatedServer)
	{
		VisualData->PlayCombatSound(this, GetWeaponData()->ReloadSound, GetMuzzleLocation());
	}
}


float ASoldier::GetCurrentMoveSpeed() const
{
	const USoldierData* D = GetSoldierData();
	switch (GetEffectiveStance())
	{
	case ESoldierStance::Crouched: return MoveSpeed * D->CrouchedSpeedMultiplier;
	case ESoldierStance::Prone:    return MoveSpeed * D->ProneSpeedMultiplier;
	default:                       return MoveSpeed;
	}
}

void ASoldier::UpdateMovement(float DeltaSeconds)
{
	if (!Grid || (!bStepping && !TryBeginNextStep()))
	{
		return;
	}

	const FVector Target = Grid->CellToWorld(StepCell);
	FVector Location = GetActorLocation();
	FVector Delta = Target - Location;
	Delta.Z = 0.f;
	const float Distance = Delta.Size();

	// A dragged soldier is pulled along without turning; it moves at its dragger's pace.
	const ASoldier* Dragger = DraggedBy.Get();
	// A soldier with a facing order (or mid-throw) keeps looking that way as it walks; UpdateFacing turns it.
	if (Distance > KINDA_SMALL_NUMBER && !Dragger && !bDead && !bHasFacingOrder && !IsThrowingGrenade())
	{
		const FRotator Desired = Delta.Rotation();
		SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), Desired, DeltaSeconds, TurnSpeed));
	}

	const float StepDistance = (Dragger ? Dragger->GetCurrentMoveSpeed() : GetCurrentMoveSpeed()) * DeltaSeconds;
	if (StepDistance >= Distance)
	{
		SetActorLocation(FVector(Target.X, Target.Y, Location.Z));
		PreviousCell = CurrentCell; // GDD §7: a throw steps back onto this cell.
		bHasPreviousCell = true;
		CurrentCell = StepCell;
		bStepping = false;
		if (Path.Num() == 0)
		{
			DestinationCell = CurrentCell;
			// GDD §9.2: arriving beside cover, the soldier turns to lean on it (a facing order can turn it away).
			FIntPoint CoverSide;
			if (!Dragger && !bDead && !AttackTarget.IsValid() && !bHasFacingOrder
				&& Grid->FindCoverToLeanOn(CurrentCell, GetActorRotation().Yaw, CoverSide))
			{
				DesiredYaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(CoverSide.Y), static_cast<float>(CoverSide.X)));
				bHasFacingOrder = true;
			}
		}
	}
	else
	{
		SetActorLocation(Location + Delta / Distance * StepDistance);
	}
}

void ASoldier::UpdateFacing(float DeltaSeconds)
{
	if (IsBeingDragged())
	{
		return; // Pulled along without turning.
	}

	// Priority: the grenade throw in progress, then the attack target (once standing still), then the facing
	// order. With none of them a walking soldier faces the way it walks (UpdateMovement).
	float TargetYaw;
	const ASoldier* Target = AttackTarget.Get();
	if (IsBusyThrowing() && Grid && PendingGrenadeCell != CurrentCell)
	{
		TargetYaw = (Grid->CellToWorld(PendingGrenadeCell) - GetActorLocation()).Rotation().Yaw;
	}
	else if (Target && !bStepping)
	{
		TargetYaw = (Target->GetActorLocation() - GetActorLocation()).Rotation().Yaw;
	}
	else if (bHasFacingOrder)
	{
		TargetYaw = DesiredYaw;
	}
	else
	{
		return;
	}
	SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), FRotator(0.f, TargetYaw, 0.f), DeltaSeconds, TurnSpeed));
}

bool ASoldier::IsFacingYaw(float Yaw, float ToleranceDegrees) const
{
	return FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, Yaw)) <= ToleranceDegrees;
}

float ASoldier::GetIntendedYaw() const
{
	// Same priorities as UpdateFacing.
	const ASoldier* Target = AttackTarget.Get();
	if (IsBusyThrowing() && Grid && PendingGrenadeCell != CurrentCell)
	{
		return (Grid->CellToWorld(PendingGrenadeCell) - GetActorLocation()).Rotation().Yaw;
	}
	if (Target && !bStepping)
	{
		return (Target->GetActorLocation() - GetActorLocation()).Rotation().Yaw;
	}
	if (bHasFacingOrder)
	{
		return DesiredYaw;
	}
	return GetActorRotation().Yaw;
}

// ------------------------------------------------------------------------------------------------ Machine gun set-up

bool ASoldier::HasSetupWeapon() const
{
	return GetWeaponData()->NeedsSetup();
}

bool ASoldier::IsYawInSetupArc(float Yaw) const
{
	const float HalfArc = GetWeaponData()->SetupArcDegrees * 0.5f;
	return FMath::Abs(FMath::FindDeltaAngleDegrees(SetupYaw, Yaw)) <= HalfArc + 0.5f;
}

float ASoldier::GetSetupProgress() const
{
	if (bWeaponSetUp)
	{
		return 1.f;
	}
	if (bSettingUpWeapon && SetupDuration > 0.f)
	{
		return FMath::Clamp(1.f - SetupRemaining / SetupDuration, 0.f, 1.f);
	}
	return 0.f;
}

const TCHAR* ASoldier::GetSetupRestName() const
{
	if (!bSetupRestFound)
	{
		return TEXT("none (held)");
	}
	return bSetupRestOnCover ? TEXT("cover") : TEXT("ground");
}

bool ASoldier::FindSetupRestPitch(const FVector& GripWorld, float Yaw, float& OutPitch, bool& bOutOnCover) const
{
	// The gun pivots on the gunner's grip. Lowering it from above, the first part to touch something (the bipod
	// feet, or the barrel's underside on a wall top or window sill) decides its pitch. A point (X, Z) in the
	// mesh's space sits at height X sin(p) + Z cos(p) above the grip at pitch p.
	const UWeaponData* W = GetWeaponData();
	const FVector Foot = W->BipodFoot;
	if (Foot.X <= 0.f || !GetWorld())
	{
		return false;
	}
	constexpr float MinPitch = -30.f;
	constexpr float MaxPitch = 20.f;
	const FVector Fwd = FRotator(0.f, Yaw, 0.f).Vector();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(SetupRest), false, this);
	float Best = -BIG_NUMBER;
	bool bBestOnCover = false;
	auto Consider = [&](float X, float Z, bool bGroundOnly)
	{
		const FVector P = GripWorld + Fwd * X;
		FIntPoint PCell;
		const bool bOverCover = Grid && Grid->WorldToCell(P, PCell) && Grid->GetCoverCell(PCell) != nullptr;
		if (bGroundOnly && bOverCover)
		{
			return; // The bipod's legs fold away on cover: the gun lies on it by its barrel instead.
		}
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, FVector(P.X, P.Y, GripWorld.Z + 30.f), FVector(P.X, P.Y, GripWorld.Z - 200.f), ECC_WorldStatic, Query))
		{
			return;
		}
		const float Drop = Hit.ImpactPoint.Z - GripWorld.Z;
		const float R = FMath::Sqrt(X * X + Z * Z);
		if (R < 1.f || FMath::Abs(Drop / R) > 1.f)
		{
			return; // Out of reach at any pitch.
		}
		const float Pitch = FMath::RadiansToDegrees(FMath::Asin(Drop / R) - FMath::Atan2(Z, X));
		if (Pitch > Best)
		{
			Best = Pitch;
			bBestOnCover = bOverCover;
		}
	};
	Consider(Foot.X, Foot.Z, true);
	for (int32 I = 3; I <= 12; ++I)
	{
		Consider(Foot.X * I / 10.f, W->BarrelUndersideHeight, false);
	}
	if (Best < MinPitch)
	{
		return false; // Nothing within reach below the gun (e.g. standing in the open): it stays in the hands.
	}
	OutPitch = FMath::Min(Best, MaxPitch);
	bOutOnCover = bBestOnCover;
	return true;
}

void ASoldier::UpdateSetupWeaponVisual(float DeltaSeconds)
{
	// GDD §6.1 (cosmetic): as the set-up runs, the gun swings from the gunner's hands down onto its bipod: on the
	// ground when prone, on top of the hay, low wall or window sill in front otherwise. Set up, it traverses with
	// the gunner and kicks back a little per round instead of playing the rifle's fire clip.
	if (!HasModel() || !WeaponModel->GetStaticMesh())
	{
		return;
	}
	const UWeaponData* W = GetWeaponData();
	const bool bSetupWeapon = W->NeedsSetup() && !bDead;
	const float Target = bSetupWeapon ? GetSetupProgress() : 0.f;
	SetupVisualAlpha = Target >= SetupVisualAlpha ? Target : FMath::FInterpConstantTo(SetupVisualAlpha, Target, DeltaSeconds, 5.f);
	if (ShotCount != SetupVisualShotCount)
	{
		SetupVisualShotCount = ShotCount;
		if (bSetupWeapon)
		{
			SetupRecoilOffset = W->SetupRecoil;
		}
	}
	SetupRecoilOffset = FMath::FInterpTo(SetupRecoilOffset, 0.f, DeltaSeconds, 25.f);

	if (SetupVisualAlpha <= 0.f && SetupRecoilOffset < 0.05f)
	{
		bSetupRestFound = false;
		if (WeaponModel->IsUsingAbsoluteLocation())
		{
			// Back in the hands: ride the weapon bone again.
			WeaponModel->SetUsingAbsoluteLocation(false);
			WeaponModel->SetUsingAbsoluteRotation(false);
			WeaponModel->SetRelativeTransform(VisualData->WeaponOffset);
		}
		return;
	}

	const FTransform Held = VisualData->WeaponOffset * Model->GetSocketTransform(VisualData->WeaponBone);
	const FVector Grip = Held.GetLocation();
	const float Yaw = GetActorRotation().Yaw;
	float Pitch = 0.f;
	bSetupRestFound = SetupVisualAlpha > 0.f && FindSetupRestPitch(Grip, Yaw, Pitch, bSetupRestOnCover);
	SetupRestPitch = bSetupRestFound ? Pitch : 0.f;
	const FQuat Rest = bSetupRestFound ? FRotator(Pitch, Yaw, 0.f).Quaternion() : Held.GetRotation();
	const FQuat Rot = FQuat::Slerp(Held.GetRotation(), Rest, SetupVisualAlpha);
	const FVector Location = Grip - Rot.GetForwardVector() * SetupRecoilOffset;
	if (!WeaponModel->IsUsingAbsoluteLocation())
	{
		WeaponModel->SetUsingAbsoluteLocation(true);
		WeaponModel->SetUsingAbsoluteRotation(true);
	}
	WeaponModel->SetWorldLocationAndRotation(Location, Rot);
}

void ASoldier::TearDownWeapon()
{
	bWeaponSetUp = false;
	bSettingUpWeapon = false;
	SetupRemaining = 0.f;
	BurstRoundsFired = 0;
}

void ASoldier::UpdateWeaponSetup(float DeltaSeconds)
{
	// GDD §6.1: a machine gun fires only once set up. The soldier sets it up whenever it stands still, facing
	// the way it means to fire (its target, else its facing order, else the way it stands). Once set up it can
	// fire across an arc centred on that direction. Moving, a target or facing order outside the arc, a change
	// of stance, or anything that takes its hands (stun, drag, medkit, buildable) tears it down.
	const UWeaponData* W = GetWeaponData();
	if (!W->NeedsSetup())
	{
		TearDownWeapon(); // E.g. swapped for a rifle by looting.
		return;
	}
	const bool bCanSetUp = CanAct() && !IsMoving() && !IsDragging() && !IsApplyingMedkit() && !IsBuilding();
	if (!bCanSetUp)
	{
		TearDownWeapon();
		return;
	}
	const float Intended = GetIntendedYaw();
	const ESoldierStance Posture = GetEffectiveStance();
	if (bWeaponSetUp)
	{
		if (Posture == SetupStance && IsYawInSetupArc(Intended))
		{
			return;
		}
		TearDownWeapon(); // Then starts setting up again below, once it faces the new way.
	}
	if (bSettingUpWeapon && Posture != SetupStance)
	{
		TearDownWeapon();
	}
	if (!IsFacingYaw(Intended))
	{
		bSettingUpWeapon = false; // Still turning: the set-up starts once it faces the way it will fire.
		SetupRemaining = 0.f;
		return;
	}
	if (!bSettingUpWeapon)
	{
		bSettingUpWeapon = true;
		SetupStance = Posture;
		// Stress slows the set-up like the aim (GDD §8.1).
		SetupRemaining = W->SetupTime * GetStressEffects().AimTimeMultiplier;
		SetupDuration = SetupRemaining;
	}
	SetupRemaining -= DeltaSeconds;
	if (SetupRemaining <= 0.f)
	{
		bSettingUpWeapon = false;
		SetupRemaining = 0.f;
		bWeaponSetUp = true;
		SetupYaw = Intended;
	}
}

bool ASoldier::TryBeginNextStep()
{
	if (Path.Num() == 0 || !CanAct())
	{
		return false;
	}
	if (IsShocked())
	{
		StopMoving(); // GDD §8.1: shocked soldiers cannot move.
		return false;
	}

	const FIntPoint Next = Path[0];
	const bool bDiagonal = Next.X != CurrentCell.X && Next.Y != CurrentCell.Y;
	const bool bBlocked = Grid->IsCellOccupied(Next, this)
		|| (bDiagonal && (Grid->IsCellOccupied(FIntPoint(Next.X, CurrentCell.Y), this)
			|| Grid->IsCellOccupied(FIntPoint(CurrentCell.X, Next.Y), this)));
	if (bBlocked)
	{
		// Someone stepped into our route: re-plan. If there's no way through (often a squad-mate
		// crossing our destination), wait a moment for them to pass before giving up.
		TArray<FIntPoint> NewPath;
		if (!Grid->FindPath(CurrentCell, DestinationCell, this, NewPath) || NewPath.Num() == 0)
		{
			BlockedWaitTime += GetWorld()->GetDeltaSeconds();
			if (BlockedWaitTime >= MaxBlockedWait)
			{
				StopMoving();
			}
			return false;
		}
		Path = MoveTemp(NewPath);
	}

	BlockedWaitTime = 0.f;
	StepCell = Path[0];
	Path.RemoveAt(0);
	bStepping = true;

	// The dragged soldier follows behind, into the cell we're leaving.
	if (ASoldier* Dragged = DraggedSoldier.Get())
	{
		Dragged->FollowDragger(CurrentCell);
	}
	return true;
}

// ------------------------------------------------------------------------------------------------ Dragging

bool ASoldier::CanStartDragging(const ASoldier* Target) const
{
	// GDD §5.4: an Active soldier may drag an allied soldier who is Stunned, Injured or Dead.
	if (!Target || Target == this || Target->Team != Team || !Target->IsDowned() || Target->IsBeingDragged())
	{
		return false;
	}
	if (!CanAct() || bInjured || IsDragging() || bStepping || IsShocked())
	{
		return false;
	}
	const float Metres = FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()) / UUPerMetre;
	return Metres <= GetSoldierData()->DragStartRange + KINDA_SMALL_NUMBER;
}

bool ASoldier::StartDragging(ASoldier* Target)
{
	if (!CanStartDragging(Target))
	{
		if (Target && Target->IsDowned() && CanAct() && !bInjured && !IsDragging())
		{
			ShowMessage(IsShocked() ? TEXT("SHOCKED") : bStepping ? TEXT("STOP TO DRAG") : TEXT("TOO FAR TO DRAG"));
		}
		return false;
	}
	if (!HasAuthority())
	{
		return ForwardOrder(ESoldierOrderType::StartDrag, FIntPoint::ZeroValue, Target); // Checked here for the message; done on the server.
	}

	StopAfterCurrentStep();
	ClearAttack();
	CancelMedkit();
	CancelBuild();
	bHasFacingOrder = false;
	DraggedSoldier = Target;
	Target->DraggedBy = this;
	Target->StopAfterCurrentStep();
	Target->ClearAttack();
	Target->DestinationCell = Target->bStepping ? Target->StepCell : Target->CurrentCell;
	ApplyPostureVisuals();
	return true;
}

void ASoldier::StopDragging()
{
	if (!HasAuthority())
	{
		if (IsDragging())
		{
			ForwardOrder(ESoldierOrderType::StopDrag);
		}
		return;
	}
	const bool bWasDragging = !DraggedSoldier.IsExplicitlyNull();
	if (ASoldier* Dragged = DraggedSoldier.Get())
	{
		Dragged->DraggedBy = nullptr;
	}
	DraggedSoldier = nullptr;
	if (bWasDragging)
	{
		ApplyPostureVisuals(); // Back to the soldier's own stance.
	}
}

void ASoldier::UpdateDrag()
{
	ASoldier* Dragged = DraggedSoldier.Get();
	if (!Dragged)
	{
		if (!DraggedSoldier.IsExplicitlyNull())
		{
			StopDragging(); // Dragged soldier was destroyed.
		}
		return;
	}
	// Let go if we can no longer drag, or the dragged soldier recovered (stun wore off).
	if (!CanAct() || bInjured || IsShocked() || !Dragged->IsDowned())
	{
		StopDragging();
	}
}

void ASoldier::FollowDragger(FIntPoint Cell)
{
	if (!Grid)
	{
		return;
	}
	if (bStepping)
	{
		// Still finishing the previous pull (e.g. a diagonal step): snap onto it first.
		const FVector Snap = Grid->CellToWorld(StepCell);
		SetActorLocation(FVector(Snap.X, Snap.Y, GetActorLocation().Z));
		CurrentCell = StepCell;
		bStepping = false;
	}
	DestinationCell = Cell;
	if (Cell != CurrentCell)
	{
		StepCell = Cell;
		bStepping = true;
	}
}

// ------------------------------------------------------------------------------------------------ Medkit

namespace
{
	/** Cells apart with diagonals counting as one (0 = same cell, 1 = the 8 around it). */
	int32 ChebyshevCells(FIntPoint A, FIntPoint B)
	{
		return FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y));
	}
}

FString ASoldier::GetMedkitBlockReason(const ASoldier* Patient) const
{
	// GDD §5.2: an Active soldier uses its own medkit on an Injured ally (not on itself).
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
		return TEXT("SHOCKED");
	}
	if (IsDragging())
	{
		return TEXT("DRAGGING");
	}
	if (Inventory.Medkits <= 0)
	{
		return TEXT("NO MEDKIT");
	}
	if (!Patient || Patient == this || Patient->Team != Team)
	{
		return TEXT("CAN'T TREAT");
	}
	if (Patient->IsDead())
	{
		return TEXT("PATIENT KIA");
	}
	if (!Patient->IsInjured())
	{
		return TEXT("NOT INJURED");
	}
	if (Patient->IsBeingDragged())
	{
		return TEXT("PATIENT DRAGGED");
	}
	if (const ASoldier* Other = Patient->MedkitTreatedBy.Get(); Other && Other != this)
	{
		return TEXT("ALREADY TREATED");
	}
	return FString();
}

bool ASoldier::IsInMedkitReach(const ASoldier* Patient) const
{
	return Patient && !bStepping && !Patient->bStepping
		&& ChebyshevCells(CurrentCell, Patient->CurrentCell) <= GetSoldierData()->MedkitReachCells;
}

bool ASoldier::OrderUseMedkit(ASoldier* Patient)
{
	const FString Reason = GetMedkitBlockReason(Patient);
	if (!Reason.IsEmpty())
	{
		if (!bDead)
		{
			ShowMessage(Reason);
		}
		return false;
	}
	if (MedkitPatient.Get() == Patient)
	{
		return true; // Already on it.
	}
	if (!HasAuthority())
	{
		return ForwardOrder(ESoldierOrderType::Medkit, FIntPoint::ZeroValue, Patient); // Checked here for the message; done on the server.
	}
	CancelMedkit(); // A previous patient.
	CancelBuild();
	CancelGrenadeThrow(); // A new order cancels a throw not yet released (the grenade is kept).
	ClearAttack();
	MedkitApproachTries = 0;
	if (!ApproachPatient(Patient))
	{
		ShowMessage(TEXT("CAN'T REACH"));
		return false;
	}
	return true;
}

bool ASoldier::ApproachPatient(ASoldier* Patient)
{
	if (!Grid || !Patient)
	{
		return false;
	}
	const int32 Reach = GetSoldierData()->MedkitReachCells;
	const FIntPoint From = GetStandCell();
	const FIntPoint PatientCell = Patient->GetStandCell();
	if (ChebyshevCells(From, PatientCell) <= Reach)
	{
		// Close enough already: finish the step in progress and start once both are still.
		StopAfterCurrentStep();
		LinkMedkitPatient(Patient);
		return true;
	}

	// Otherwise walk to the free cell within reach of the patient that is nearest to the medic.
	TArray<TPair<float, FIntPoint>> Candidates;
	for (int32 DY = -Reach; DY <= Reach; ++DY)
	{
		for (int32 DX = -Reach; DX <= Reach; ++DX)
		{
			const FIntPoint Cell = PatientCell + FIntPoint(DX, DY);
			if (Grid->IsInBounds(Cell) && Grid->IsCellFreeAsDestination(Cell, this))
			{
				Candidates.Emplace(FVector2D::Distance(FVector2D(Cell), FVector2D(From)), Cell);
			}
		}
	}
	Candidates.Sort([](const TPair<float, FIntPoint>& A, const TPair<float, FIntPoint>& B) { return A.Key < B.Key; });
	for (const TPair<float, FIntPoint>& Candidate : Candidates)
	{
		// MoveToCell clears any treatment link (it is an order like any other), so link afterwards.
		if (MoveToCell(Candidate.Value))
		{
			LinkMedkitPatient(Patient);
			return true;
		}
	}
	return false;
}

void ASoldier::LinkMedkitPatient(ASoldier* Patient)
{
	MedkitPatient = Patient;
	Patient->MedkitTreatedBy = this;
	bApplyingMedkit = false;
	MedkitRemaining = 0.f;
}

void ASoldier::BeginMedkitTreatment(ASoldier* Patient)
{
	// GDD §5.2: a 7-second channel during which the medic stays put and can't shoot.
	ClearAttack();
	bApplyingMedkit = true;
	MedkitRemaining = FMath::Max(GetSoldierData()->MedkitChannelTime, KINDA_SMALL_NUMBER);
	const FVector ToPatient = Patient->GetActorLocation() - GetActorLocation();
	if (ToPatient.SizeSquared2D() > 1.f)
	{
		DesiredYaw = ToPatient.Rotation().Yaw;
		bHasFacingOrder = true;
	}
}

void ASoldier::CancelMedkit()
{
	if (!HasAuthority())
	{
		if (MedkitPatient.IsValid())
		{
			ForwardOrder(ESoldierOrderType::CancelMedkit);
		}
		return;
	}
	if (ASoldier* Patient = MedkitPatient.Get(); Patient && Patient->MedkitTreatedBy.Get() == this)
	{
		Patient->MedkitTreatedBy = nullptr;
	}
	MedkitPatient = nullptr;
	bApplyingMedkit = false;
	MedkitRemaining = 0.f;
}

void ASoldier::UpdateMedkit(float DeltaSeconds)
{
	ASoldier* Patient = MedkitPatient.Get();
	if (!Patient)
	{
		if (!MedkitPatient.IsExplicitlyNull())
		{
			CancelMedkit(); // Patient was destroyed.
		}
		return;
	}

	// Anything that would stop the treatment from starting also stops it part-way (the medkit is kept).
	const FString Reason = GetMedkitBlockReason(Patient);
	if (!Reason.IsEmpty())
	{
		CancelMedkit();
		ShowMessage(Reason);
		return;
	}

	if (!bApplyingMedkit)
	{
		// Walking over. Start once both soldiers are standing still within reach.
		if (IsMoving() || Patient->bStepping)
		{
			return;
		}
		if (IsInMedkitReach(Patient))
		{
			BeginMedkitTreatment(Patient);
			return;
		}
		// Arrived but the patient crawled off (or the squad limit cut the walk short): try again a few times.
		if (++MedkitApproachTries > MaxMedkitApproachTries || !ApproachPatient(Patient))
		{
			CancelMedkit();
			ShowMessage(TEXT("CAN'T REACH"));
		}
		return;
	}

	// Treating: the patient crawling away (or being moved) interrupts it.
	if (!IsInMedkitReach(Patient))
	{
		CancelMedkit();
		ShowMessage(TEXT("TREATMENT STOPPED"));
		return;
	}
	MedkitRemaining -= DeltaSeconds;
	if (MedkitRemaining <= 0.f)
	{
		--Inventory.Medkits; // GDD §5.2: used up once applied.
		if (USquadMatchStats* Stats = USquadMatchStats::Get(this))
		{
			Stats->RecordMedkit(*this, *Patient);
		}
		CancelMedkit();
		Patient->RecoverFromInjury();
		ShowMessage(FString::Printf(TEXT("TREATED %d"), Patient->GetSquadNumber()));
	}
}

void ASoldier::RecoverFromInjury()
{
	if (bDead || !bInjured)
	{
		return;
	}
	// GDD §5.2: back to Active. A further hit from beyond 5 m injures again rather than killing.
	bInjured = false;
	BleedOutRemaining = 0.f;
	InjuredBy = nullptr;
	MedkitTreatedBy = nullptr;
	Stance = ESoldierStance::Prone; // Still on the ground; the player picks the stance from here.
	ApplyPostureVisuals();
	ShowMessage(TEXT("BACK IN ACTION"));
}

void ASoldier::StopMoving()
{
	BlockedWaitTime = 0.f;
	Path.Reset();
	bStepping = false;
	DestinationCell = CurrentCell;
}

void ASoldier::StopAfterCurrentStep()
{
	Path.Reset();
	DestinationCell = bStepping ? StepCell : CurrentCell;
}

// ------------------------------------------------------------------------------------------------ Combat

void ASoldier::ClearAttack()
{
	AttackTarget = nullptr;
	bAutoTarget = false;
	LastHitChance = -1.f;
	AimRemaining = 0.f;
	ShotCooldown = 0.f;
	BurstRoundsFired = 0;
}

void ASoldier::UpdateAutoTarget(float DeltaSeconds)
{
	// GDD §5.3: a soldier with no explicit target engages the enemy in its line of sight with the highest hit
	// chance. It doesn't stop a move to do so: it opens fire once it stands still.
	if (bHoldFire)
	{
		if (bAutoTarget)
		{
			ClearAttack(); // GDD §5.3: never keep an automatic target while holding fire.
		}
		return;
	}
	if (!CanAct() || !Grid || IsMoving() || IsDragging())
	{
		return;
	}
	if (AttackTarget.IsValid() && !bAutoTarget)
	{
		return; // Explicit attack order in progress.
	}
	if (GetMagazineAmmo() <= 0 && GetReserveAmmo() <= 0)
	{
		return;
	}
	AutoTargetScanRemaining -= DeltaSeconds;
	if (AutoTargetScanRemaining > 0.f)
	{
		return;
	}
	AutoTargetScanRemaining = AutoTargetScanInterval;

	ASoldier* Best = nullptr;
	float BestChance = -1.f;
	float CurrentChance = -1.f;
	ASoldier* Current = AttackTarget.Get();
	// GDD §6.1: a set-up machine gun prefers targets inside its arc, so it doesn't tear down to swing round
	// while there is still something to shoot at in front of it.
	const bool bPreferArc = bWeaponSetUp && HasSetupWeapon();
	ASoldier* BestInArc = nullptr;
	float BestInArcChance = -1.f;
	for (const TWeakObjectPtr<ASoldier>& Weak : Grid->GetSoldiers())
	{
		ASoldier* Other = Weak.Get();
		if (!Other || Other == this || Other->Team == Team || Other->IsDead())
		{
			continue;
		}
		const FHitChanceInfo Chance = GetHitChanceAgainst(Other);
		if (!Chance.CanFire() || (Chance.bBallistic && Chance.ExposurePercent <= 0.f))
		{
			continue; // GDD §16: automatic fire only at enemies some part of which shows.
		}
		if (Chance.FinalChance > BestChance)
		{
			BestChance = Chance.FinalChance;
			Best = Other;
		}
		if (bPreferArc && Chance.FinalChance > BestInArcChance
			&& IsYawInSetupArc((Other->GetActorLocation() - GetActorLocation()).Rotation().Yaw))
		{
			BestInArcChance = Chance.FinalChance;
			BestInArc = Other;
		}
		if (Other == Current)
		{
			CurrentChance = Chance.FinalChance;
		}
	}
	if (BestInArc)
	{
		Best = BestInArc;
		BestChance = BestInArcChance;
	}
	if (!Best || Best == Current)
	{
		return; // Nothing in sight (UpdateAttack drops a lost target), or already on the best one.
	}
	if (Current && CurrentChance >= 0.f && CurrentChance >= BestChance - AutoTargetSwitchMargin)
	{
		return; // Not worth re-aiming for a marginally better shot.
	}
	AttackTarget = Best;
	bAutoTarget = true;
	AimRemaining = GetWeaponData()->AimTime * GetStressEffects().AimTimeMultiplier;
}

void ASoldier::UpdateAttack(float DeltaSeconds)
{
	if (!CanAct())
	{
		return;
	}

	if (bReloading)
	{
		ReloadRemaining -= DeltaSeconds;
		if (ReloadRemaining <= 0.f)
		{
			FinishReload();
		}
		return;
	}

	ASoldier* Target = AttackTarget.Get();
	if (!Target)
	{
		return;
	}
	if (Target->IsDead())
	{
		ClearAttack();
		return;
	}

	if (GetMagazineAmmo() <= 0)
	{
		if (GetReserveAmmo() > 0)
		{
			StartReload();
		}
		else
		{
			ShowMessage(TEXT("NO AMMO"));
			ClearAttack();
		}
		return;
	}

	if (bStepping)
	{
		return; // Finishing the step in progress before opening fire.
	}

	const FHitChanceInfo Chance = GetHitChanceAgainst(Target);
	if (bAutoTarget && Chance.CanFire() && Chance.bBallistic && Chance.ExposurePercent <= 0.f)
	{
		ClearAttack(); // GDD §16: an automatic target that ducks fully behind cover is dropped until it shows again.
		return;
	}
	if (!Chance.CanFire())
	{
		if (!bAutoTarget) // An automatic target just drops out of sight; only explicit orders report why.
		{
			ShowMessage(Chance.bInRange ? TEXT("NO LINE OF SIGHT") : TEXT("OUT OF RANGE"));
		}
		ClearAttack();
		return;
	}
	LastHitChance = Chance.FinalChance;

	if (!IsFacingYaw((Target->GetActorLocation() - GetActorLocation()).Rotation().Yaw))
	{
		return;
	}
	if (HasSetupWeapon() && !bWeaponSetUp)
	{
		return; // GDD §6.1: a machine gun must be set up first (UpdateWeaponSetup).
	}
	if (AimRemaining > 0.f)
	{
		AimRemaining -= DeltaSeconds;
		return;
	}

	ShotCooldown -= DeltaSeconds;
	// GDD §6.1: a burst's rounds go out at the cyclic rate, then the burst pause. A rifle's "burst" is one
	// round, so it waits the shot interval. Stress stretches the wait between trigger pulls (§8.1). Rounds due
	// within one frame are all fired (an MG 42 fires a round every 0.05 s, faster than a slow frame).
	const UWeaponData* W = GetWeaponData();
	while (ShotCooldown <= 0.f)
	{
		FireAt(Target, Chance);
		++BurstRoundsFired;
		if (BurstRoundsFired < W->RoundsPerBurst && GetMagazineAmmo() > 0 && !Target->IsDead())
		{
			ShotCooldown += W->GetShotInterval(); // Keeps the time left over, so the cyclic rate holds at any frame rate.
			continue;
		}
		BurstRoundsFired = 0;
		// Carry the time left over (frames rarely land on the exact shot time), but never fire a second trigger
		// pull in the same frame after a hitch.
		ShotCooldown = FMath::Max(ShotCooldown + FMath::Max(W->GetShotInterval(), W->BurstPause) * GetStressEffects().FireIntervalMultiplier, KINDA_SMALL_NUMBER);
	}
}

FString FHitChanceInfo::Describe() const
{
	if (!bInRange)
	{
		return TEXT("OUT OF RANGE");
	}
	if (!bLineOfSight)
	{
		return TEXT("NO LINE OF SIGHT");
	}
	const TCHAR* StressTag = StressAccuracyMultiplier < 1.f ? TEXT(" (STRESSED)") : TEXT("");
	if (bBallistic)
	{
		// e.g. "34% - 40% EXPOSED (BRACED)"
		if (ExposurePercent <= 0.f)
		{
			return FString::Printf(TEXT("%.0f%% - HIDDEN%s%s"), FinalChance, bBraced ? TEXT(" (BRACED)") : TEXT(""), StressTag);
		}
		return FString::Printf(TEXT("%.0f%% - %.0f%% EXPOSED%s%s"), FinalChance, ExposurePercent,
			bBraced ? TEXT(" (BRACED)") : TEXT(""), StressTag);
	}
	if (Cover == ECoverType::None)
	{
		return FString::Printf(TEXT("%.0f%% EXPOSED%s"), FinalChance, StressTag);
	}
	return FString::Printf(TEXT("%.0f%%%s -%.0f %s%s = %.0f%%"), BaseChance, StressTag, CoverReduction, UCoverData::GetCoverName(Cover),
		bCoverLeaning ? TEXT("") : TEXT(" (BACK TURNED)"), FinalChance);
}

FHitChanceInfo ASoldier::GetHitChanceAgainst(const ASoldier* Target) const
{
	FHitChanceInfo Info;
	if (!Target)
	{
		return Info;
	}
	Info.DistanceMetres = FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()) / UUPerMetre;
	Info.BaseChance = GetWeaponData()->GetAccuracyAtRange(Info.DistanceMetres);
	Info.bInRange = Info.BaseChance >= 0.f;
	// GDD §8.1: a stressed shooter's accuracy drops before cover is taken off.
	Info.StressAccuracyMultiplier = GetStressEffects().AccuracyMultiplier;
	Info.BaseChance = FMath::Max(Info.BaseChance, 0.f) * Info.StressAccuracyMultiplier;
	Info.FinalChance = Info.BaseChance;
	if (!Grid)
	{
		return Info;
	}

	// GDD §9: cover is judged on grid cells. A soldier can only shoot what it can see (§9.3).
	const FIntPoint From = CurrentCell;
	const FIntPoint To = Target->GetCurrentCell();
	Info.bLineOfSight = CanSee(Target);

	// The target leans on cover it faces; cover at its back counts for less (§9.2).
	const FCoverResult Cover = Grid->GetCoverAgainst(To, From, TOptional<float>(Target->GetActorRotation().Yaw));
	if (Cover.Type != ECoverType::None)
	{
		Info.Cover = Cover.Type;
		Info.CoverReduction = Cover.Reduction;
		Info.bCoverLeaning = Cover.bLeaning;
		Info.FinalChance = Grid->GetCoverData()->ApplyReduction(Info.BaseChance, Cover.Reduction);
	}
	// GDD §16: in the projectile model the chance is estimated from sampled rounds against the target's body;
	// the grid cover above is kept only for stress and the readout's cover name.
	if (Info.CanFire() && UBallisticsSubsystem::IsEnabled(this))
	{
		FillBallisticChance(*Target, Info);
	}
	return Info;
}

// ------------------------------------------------------------------------------------------------ Vision

FIntPoint ASoldier::GetSightCell() const
{
	FIntPoint Cell = CurrentCell;
	if (bStepping && Grid)
	{
		Grid->WorldToCell(GetActorLocation(), Cell);
	}
	return Cell;
}

float ASoldier::GetSightHeight() const
{
	const UCoverData* Data = Grid ? Grid->GetCoverData() : GetDefault<UCoverData>();
	return Data->GetSightHeight(bDead ? ESoldierStance::Prone : GetEffectiveStance());
}

bool ASoldier::CanSee(const ASoldier* Target) const
{
	if (!Target || Target == this)
	{
		return Target != nullptr;
	}
	if (!Grid)
	{
		return true;
	}
	const FIntPoint MyCell = GetSightCell();
	const FIntPoint TheirCell = Target->GetSightCell();
	// GDD §9.3: no one sees further than the maximum sight range (symmetrical, measured between sight cells).
	const float MaxSight = Grid->GetCoverData()->MaxSightRange;
	if (MaxSight > 0.f && FVector2D(MyCell - TheirCell).Size() > MaxSight)
	{
		return false;
	}
	const float MyHeight = GetSightHeight();
	const float TheirHeight = Target->GetSightHeight();
	if (Grid->HasLineOfSight(MyCell, TheirCell, MyHeight, TheirHeight))
	{
		return true;
	}

	// GDD §9.2: a soldier leaning out past a wall's edge sees (and is seen) from the open cell beside the
	// wall's end. Both sides are checked so sight stays symmetrical.
	TArray<FIntPoint, TInlineAllocator<3>> Mine;
	TArray<FIntPoint, TInlineAllocator<3>> Theirs;
	GetSightOrigins(TheirCell, Mine);
	Target->GetSightOrigins(MyCell, Theirs);
	if (Mine.Num() == 1 && Theirs.Num() == 1)
	{
		return false;
	}
	for (const FIntPoint& A : Mine)
	{
		for (const FIntPoint& B : Theirs)
		{
			if ((A != MyCell || B != TheirCell) && Grid->HasLineOfSight(A, B, MyHeight, TheirHeight))
			{
				return true;
			}
		}
	}
	return false;
}

void ASoldier::GetSightOrigins(FIntPoint OtherCell, TArray<FIntPoint, TInlineAllocator<3>>& OutCells) const
{
	OutCells.Reset();
	OutCells.Add(GetSightCell());
	if (!Grid || bStepping || bDead || IsDragging() || IsBeingDragged())
	{
		return; // Only a soldier standing still on its cell can lean out.
	}
	TArray<FWallPeek> Peeks;
	Grid->GetWallPeeks(CurrentCell, GetActorRotation().Yaw, Peeks);
	for (const FWallPeek& Peek : Peeks)
	{
		if (Peek.Covers(OtherCell))
		{
			OutCells.AddUnique(Peek.PeekCell);
		}
	}
}

bool ASoldier::IsLeaningOnCover() const
{
	if (!Grid || bStepping || bDead)
	{
		return false;
	}
	FIntPoint Side;
	return Grid->FindCoverToLeanOn(CurrentCell, GetActorRotation().Yaw, Side)
		&& Grid->IsLeaningToward(Side, GetActorRotation().Yaw);
}

FVector ASoldier::GetMuzzleLocation() const
{
	// Tracers start at the rifle's muzzle when the soldier has a model (cosmetic only: hits are rolled, not traced).
	if (HasModel() && WeaponModel->GetStaticMesh() && WeaponModel->DoesSocketExist(VisualData->MuzzleSocket))
	{
		return WeaponModel->GetSocketLocation(VisualData->MuzzleSocket);
	}
	return GetAimPoint() + GetActorForwardVector() * (BeanRadius + 5.f);
}

float ASoldier::GetGrenadeWindupAlpha() const
{
	if (GrenadeWindupRemaining <= 0.f || GrenadeWindupDuration <= 0.f)
	{
		return 0.f;
	}
	return FMath::Clamp(1.f - GrenadeWindupRemaining / GrenadeWindupDuration, 0.f, 1.f);
}

float ASoldier::GetGrenadeStepBackAlpha() const
{
	if (!IsGrenadeSteppingBack() || !Grid)
	{
		return 0.f;
	}
	const FVector Goal = Grid->CellToWorld(StepCell);
	const float Total = FVector::Dist2D(Grid->CellToWorld(GrenadeStepBackFrom), Goal);
	if (Total <= KINDA_SMALL_NUMBER)
	{
		return 1.f;
	}
	return FMath::Clamp(1.f - FVector::Dist2D(GetActorLocation(), Goal) / Total, 0.f, 1.f);
}

bool ASoldier::GetActivePeek(FWallPeek& OutPeek) const
{
	if (!Grid || bStepping || bDead || IsDragging() || IsBeingDragged())
	{
		return false;
	}
	TArray<FWallPeek> Peeks;
	Grid->GetWallPeeks(CurrentCell, GetActorRotation().Yaw, Peeks);
	if (Peeks.Num() == 0)
	{
		return false;
	}
	// Edge is a grid step out past the wall's end; grid X/Y run along world X/Y. With both sides of a wall's end
	// open, lean out the side the soldier is turned towards.
	const FVector Forward = GetActorForwardVector();
	const FWallPeek* Chosen = &Peeks[0];
	for (const FWallPeek& Peek : Peeks)
	{
		if (FVector::DotProduct(FVector(Peek.Edge.X, Peek.Edge.Y, 0.f), Forward)
			> FVector::DotProduct(FVector(Chosen->Edge.X, Chosen->Edge.Y, 0.f), Forward) + KINDA_SMALL_NUMBER)
		{
			Chosen = &Peek;
		}
	}
	OutPeek = *Chosen;
	return true;
}

int32 ASoldier::GetPeekSide() const
{
	FWallPeek Peek;
	if (!GetActivePeek(Peek))
	{
		return 0;
	}
	return FVector::DotProduct(FVector(Peek.Edge.X, Peek.Edge.Y, 0.f), GetActorRightVector()) >= 0.f ? 1 : -1;
}

bool ASoldier::IsAiming() const
{
	const ASoldier* Target = AttackTarget.Get();
	if (!Target || bReloading || bDead)
	{
		return false;
	}
	// Mirrors UpdateAttack: finish the step, turn to face, then wait out the aim time before firing.
	return bStepping || AimRemaining > 0.f || (HasSetupWeapon() && !bWeaponSetUp)
		|| !IsFacingYaw((Target->GetActorLocation() - GetActorLocation()).Rotation().Yaw);
}

ECoverType ASoldier::GetLeaningCoverType() const
{
	if (!Grid || bStepping || bDead)
	{
		return ECoverType::None;
	}
	const float Yaw = GetActorRotation().Yaw;
	FIntPoint Side;
	if (!Grid->FindCoverToLeanOn(CurrentCell, Yaw, Side) || !Grid->IsLeaningToward(Side, Yaw))
	{
		return ECoverType::None;
	}
	const FGridCoverCell* Cover = Grid->GetCoverCell(CurrentCell + Side);
	return Cover ? Cover->Type : ECoverType::None;
}

void ASoldier::UpdateLocalVisibility()
{
	// Hide enemies none of the local player's soldiers can see (GDD §9.3). Visibility itself is decided by
	// the server (bSpottedByEnemy); this only renders it. Collision goes too so the cursor can't find them.
	const UWorld* World = GetWorld();
	const ASquadPlayerController* PC = World ? Cast<ASquadPlayerController>(World->GetFirstPlayerController()) : nullptr;
	if (!PC || !PC->IsLocalController())
	{
		return; // Dedicated server: nothing to render.
	}
	const bool bHide = CVarRevealAllSoldiers.GetValueOnGameThread() == 0 && !IsVisibleToTeam(PC->PlayerTeam);
	if (bHide != IsHidden())
	{
		SetActorHiddenInGame(bHide);
		SetActorEnableCollision(!bHide);
	}
}

void ASoldier::ShowHitPreview(const FString& Text)
{
	HitPreview = Text;
	HitPreviewRemaining = 0.15f;
}

void ASoldier::FireAt(ASoldier* Target, const FHitChanceInfo& Chance)
{
	--Inventory.Weapons[0].LoadedRounds; // Weapons[0] always exists after BeginPlay.
	++ShotCount;
	++RoundsFiredStat;

	if (UBallisticsSubsystem::IsEnabled(this))
	{
		FireBallisticRound(Target, Chance); // GDD §16: a projectile instead of a roll.
		return;
	}

	// GDD §5.3: roll 0..100,000 inclusive; hit if roll <= hit chance (0.001% resolution).
	const int32 Roll = Rng.RandRange(0, 100000);
	const int32 Threshold = FMath::RoundToInt(Chance.FinalChance * 1000.f);
	const bool bHit = Roll <= Threshold;
	const float DistanceMetres = Chance.DistanceMetres;

	const FString ShotLog = FString::Printf(TEXT("%s %d -> %s %d at %.1fm: %s, roll %.3f -> %s"),
		Team == ESquadTeam::Allies ? TEXT("Allied") : TEXT("Axis"), GetSquadNumber(),
		Target->Team == ESquadTeam::Allies ? TEXT("Allied") : TEXT("Axis"), Target->GetSquadNumber(),
		DistanceMetres, *Chance.Describe(), Roll / 1000.f, bHit ? TEXT("HIT") : TEXT("miss"));
	UE_LOG(LogTemp, Log, TEXT("%s"), *ShotLog);
	USquadMatchStats* Stats = USquadMatchStats::Get(this);
	if (Stats)
	{
		Stats->RecordShot(*this, *Target, Chance, Roll / 1000.f, bHit);
	}
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.f, bHit ? FColor(255, 120, 80) : FColor(200, 200, 200), ShotLog);
	}
#endif

	const FVector Muzzle = GetMuzzleLocation();
	FVector Impact = Target->GetAimPoint();
	if (!bHit)
	{
		// Visibly wide of the target, carrying on past it.
		const FVector Dir = (Impact - Muzzle).GetSafeNormal();
		const FVector Side = FVector::CrossProduct(Dir, FVector::UpVector).GetSafeNormal();
		const float Lateral = Rng.FRandRange(40.f, 90.f) * (Rng.FRand() < 0.5f ? -1.f : 1.f);
		Impact += Side * Lateral + FVector(0.f, 0.f, Rng.FRandRange(-20.f, 40.f)) + Dir * 300.f;

		// Cover is solid: a miss that runs into a wall, hay bale or tree stops there.
		FHitResult Blocked;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(ShotTracer), false, this);
		Query.AddIgnoredActor(Target);
		if (GetWorld()->LineTraceSingleByChannel(Blocked, Muzzle, Impact, ECC_WorldStatic, Query))
		{
			Impact = Blocked.ImpactPoint;
		}
	}
	MulticastShot(Muzzle, Impact, bHit); // Tracer and gunshot on every machine.

	// GDD §8.2: every round fired at a soldier stresses it, hit or miss; cover reduces that (§9).
	const USoldierData* D = GetSoldierData();
	const float CoverFactor = FMath::Clamp(1.f - Chance.CoverReduction / 100.f * D->CoverStressReductionScale, 0.f, 1.f);
	Target->AddStress(Target->GetSoldierData()->StressPerIncomingShot * CoverFactor);
	Target->UnderFireRemaining = UnderFireTime;
	ApplyCrossfireStress(Target);

	if (bHit)
	{
		++HitsLanded;
		FKillCause Cause;
		Cause.Killer = this;
		Cause.Method = EKillMethod::Rifle;
		Cause.Weapon = GetWeaponData();
		Cause.ChancePercent = Chance.FinalChance;
		const bool bWasDead = Target->IsDead();
		const bool bWasInjured = Target->IsInjured();
		Target->ReceiveHit(DistanceMetres, Cause);
		if (Stats)
		{
			Stats->RecordHitOutcome(bWasDead ? TEXT("none") : Target->IsDead() ? TEXT("killed")
				: (Target->IsInjured() && !bWasInjured) ? TEXT("injured") : TEXT("none"));
		}
	}
	if (OnShotResolved.IsBound())
	{
		FSoldierShotReport Report;
		Report.Shooter = this;
		Report.Target = Target;
		Report.Victim = bHit ? Target : nullptr;
		Report.bHitTarget = bHit;
		Report.Chance = Chance;
		Report.Outcome = bHit ? TEXT("hit") : TEXT("miss");
		OnShotResolved.Broadcast(Report);
	}
}

void ASoldier::ShowTracer(const FVector& From, const FVector& To, bool bHit)
{
	const FVector Span = To - From;
	const float Length = Span.Size();
	if (Length < 1.f)
	{
		return;
	}
	Tracer->SetWorldLocationAndRotation((From + To) * 0.5f, Span.Rotation());
	Tracer->SetWorldScale3D(FVector(Length / 100.f, 0.03f, 0.03f));
	SetPartColor(Tracer, this, bHit ? FLinearColor(1.f, 0.3f, 0.1f) : FLinearColor(1.f, 0.85f, 0.3f));
	Tracer->SetVisibility(true);
	TracerRemaining = 0.08f;
}

void ASoldier::StartReload()
{
	bReloading = true;
	BurstRoundsFired = 0;
	ReloadRemaining = GetWeaponData()->ReloadTime * GetStressEffects().ReloadTimeMultiplier;
	ReloadDuration = ReloadRemaining;
	MulticastReloadSound();
}

void ASoldier::NotifyWeaponChanged()
{
	// The new rifle comes with whatever was loaded in it; start over with aiming and any reload.
	bReloading = false;
	ReloadRemaining = 0.f;
	ClearAttack();
	TearDownWeapon(); // GDD §6.1: a machine gun picked up must be set up before it fires.
	RefreshWeaponModel();
}

void ASoldier::FinishReload()
{
	// Only loose rounds for the rifle actually held can be loaded.
	FLootWeapon& Held = Inventory.Weapons[0];
	const int32 Wanted = FMath::Max(0, GetWeaponData()->MagazineCapacity - Held.LoadedRounds);
	Held.LoadedRounds += Inventory.RemoveAmmo(Held.Weapon, Wanted);
	bReloading = false;
	AimRemaining = GetWeaponData()->AimTime * GetStressEffects().AimTimeMultiplier;
}

FVector ASoldier::GetAimPoint() const
{
	float Height;
	switch (GetEffectiveStance())
	{
	case ESoldierStance::Crouched: Height = BeanHeight * CrouchHeightScale * 0.7f; break;
	case ESoldierStance::Prone:    Height = BeanRadius; break;
	default:                       Height = BeanHeight * 0.7f; break;
	}
	return GetActorLocation() + FVector(0.f, 0.f, bDead ? BeanRadius : Height);
}

// ------------------------------------------------------------------------------------------------ Visuals

void ASoldier::SetSelected(bool bInSelected)
{
	bSelected = bInSelected && !bDead;
	SelectionRing->SetVisibility(bSelected);
}

void ASoldier::SetBodyColor(const FLinearColor& Color)
{
	SetPartColor(BeanBottom, this, Color);
	SetPartColor(BeanBody, this, Color);
	SetPartColor(BeanTop, this, Color);
}

bool ASoldier::HasModel() const
{
	return VisualData && Model && Model->GetSkinnedAsset() != nullptr;
}

void ASoldier::UpdateModel()
{
	USkeletalMesh* Mesh = VisualData ? VisualData->GetSoldierMesh(Team) : nullptr;
	if (Model->GetSkinnedAsset() != Mesh)
	{
		Model->SetSkinnedAssetAndUpdate(Mesh);
		bModelTintedAsCorpse = false;
	}
	Model->SetVisibility(Mesh != nullptr);
	if (!Mesh)
	{
		WeaponModel->SetStaticMesh(nullptr);
		GrenadeModel->SetStaticMesh(nullptr);
		return;
	}
	if (Model->GetAnimClass() != USoldierAnimInstance::StaticClass())
	{
		Model->SetAnimInstanceClass(USoldierAnimInstance::StaticClass());
	}

	WeaponModel->AttachToComponent(Model, FAttachmentTransformRules::KeepRelativeTransform, VisualData->WeaponBone);
	WeaponModel->SetRelativeTransform(VisualData->WeaponOffset);
	GrenadeModel->AttachToComponent(Model, FAttachmentTransformRules::KeepRelativeTransform, VisualData->GrenadeBone);
	GrenadeModel->SetRelativeTransform(VisualData->GetGrenadeOffset(Team));
	GrenadeModel->SetStaticMesh(VisualData->GetGrenadeMesh(Team));
	GrenadeModel->SetVisibility(false);
	RefreshWeaponModel();
}

void ASoldier::RefreshWeaponModel()
{
	if (!HasModel())
	{
		return;
	}
	// Before BeginPlay (editor preview) the inventory is empty: show the starting rifle.
	const UWeaponData* Held = Inventory.Weapons.Num() > 0 ? Inventory.Weapons[0].Weapon.Get() : (HasActorBegunPlay() ? nullptr : Weapon.Get());
	UStaticMesh* Mesh = Held ? Held->Mesh.Get() : nullptr;
	if (WeaponModel->GetStaticMesh() != Mesh)
	{
		WeaponModel->SetStaticMesh(Mesh);
	}
	WeaponModel->SetVisibility(Mesh != nullptr);
}

void ASoldier::ApplyModelTint(bool bCorpse)
{
	if (!HasModel() || bModelTintedAsCorpse == bCorpse)
	{
		return;
	}
	bModelTintedAsCorpse = bCorpse;
	static const FName BaseColorParam(TEXT("BaseColor"));
	for (int32 Slot = 0; Slot < Model->GetNumMaterials(); ++Slot)
	{
		UMaterialInterface* Current = Model->GetMaterial(Slot);
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Current);
		UMaterialInterface* Source = MID ? MID->Parent.Get() : Current;
		if (!Source)
		{
			continue;
		}
		FLinearColor Base;
		if (!Source->GetVectorParameterValue(FHashedMaterialParameterInfo(BaseColorParam), Base))
		{
			continue;
		}
		if (!MID)
		{
			MID = Model->CreateDynamicMaterialInstance(Slot, Source);
		}
		if (MID)
		{
			// Desaturate and darken: a grey corpse that still reads as the right uniform up close.
			const float Grey = Base.GetLuminance();
			const FLinearColor Tinted = FMath::Lerp(Base, FLinearColor(Grey, Grey, Grey), 0.75f) * VisualData->CorpseTint * 2.f;
			MID->SetVectorParameterValue(BaseColorParam, bCorpse ? Tinted : Base);
		}
	}
}

void ASoldier::ApplyPostureVisuals()
{
	// Soldiers saved before BodyRoot existed still have their bean parts attached to the actor root
	// (serialized attachment wins over the constructor). Re-parent them so posing BodyRoot moves them.
	for (UStaticMeshComponent* Part : { BeanBottom.Get(), BeanBody.Get(), BeanTop.Get(), Visor.Get() })
	{
		if (Part && Part->GetAttachParent() != BodyRoot)
		{
			Part->AttachToComponent(BodyRoot, FAttachmentTransformRules::KeepRelativeTransform);
		}
	}

	// Pose the bean to stand in for missing models/animations:
	//   Standing - upright; Crouched - squashed to 60% height;
	//   Prone - lying face-down along its facing; Dead - lying face-up (visor pointing at the sky), grey.
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	FVector Scale = FVector::OneVector;
	float LabelHeight = BeanHeight + 30.f;

	if (bDead)
	{
		Rotation = FRotator(90.f, 0.f, 0.f);              // Up axis now points backwards.
		Location = FVector(BeanHeight * 0.5f, 0.f, BeanRadius);
		LabelHeight = BeanRadius * 2.f + 40.f;
	}
	else
	{
		switch (GetEffectiveStance())
		{
		case ESoldierStance::Crouched:
			Scale = FVector(1.f, 1.f, CrouchHeightScale);
			LabelHeight = BeanHeight * CrouchHeightScale + 30.f;
			break;
		case ESoldierStance::Prone:
			Rotation = FRotator(-90.f, 0.f, 0.f);         // Up axis now points forwards.
			Location = FVector(-BeanHeight * 0.5f, 0.f, BeanRadius);
			LabelHeight = BeanRadius * 2.f + 40.f;
			break;
		default:
			break;
		}
	}

	BodyRoot->SetRelativeLocationAndRotation(Location, Rotation);
	BodyRoot->SetRelativeScale3D(Scale);

	// With a model, the posed bean stays only as an invisible click target that matches the posture.
	const bool bShowBean = !HasModel();
	for (UStaticMeshComponent* Part : { BeanBottom.Get(), BeanBody.Get(), BeanTop.Get(), Visor.Get() })
	{
		Part->SetVisibility(bShowBean);
	}
	ApplyModelTint(bDead);
	LabelBaseHeight = LabelHeight;
	if (!HasLabels())
	{
		return;
	}
	NumberLabel->SetRelativeLocation(FVector(0.f, 0.f, LabelHeight));
	HoldFireLabel->SetRelativeLocation(FVector(0.f, 0.f, LabelHeight + 30.f));
	StatusLabel->SetRelativeLocation(FVector(0.f, 0.f, LabelHeight + 32.f)); // UpdateHoldFireLabel restacks it while holding.
}

void ASoldier::UpdateHoldFireLabel()
{
	if (!HasLabels())
	{
		return;
	}
	// GDD §5.3: shown to the soldier's own team only, so the enemy can't read who is holding fire.
	const UWorld* World = GetWorld();
	const ASquadPlayerController* PC = World ? Cast<ASquadPlayerController>(World->GetFirstPlayerController()) : nullptr;
	const bool bShow = IsHoldingFire() && PC && PC->IsLocalController() && PC->PlayerTeam == Team;
	if (HoldFireLabel->IsVisible() != bShow)
	{
		HoldFireLabel->SetVisibility(bShow);
		HoldFireLabel->SetText(FText::FromString(HoldFireLabelText));
		HoldFireLabel->SetWorldSize(HoldFireLabelSize);
		// The squad number turns red too: readable even when labels crowd together.
		NumberLabel->SetTextRenderColor(bShow ? FColor(235, 10, 0) : (Team == ESquadTeam::Allies ? FColor::White : FColor(255, 170, 150)));
		if (!bShow)
		{
			StatusLabel->SetRelativeLocation(FVector(0.f, 0.f, LabelBaseHeight + 32.f));
		}
	}
	if (bShow)
	{
		FaceCamera(HoldFireLabel);
		// Stack number -> tag -> status line along the camera's up axis, not world Z: the camera looks steeply down,
		// so world-Z gaps shrink on screen and the lines would overlap.
		const FVector Up = HoldFireLabel->GetUpVector();
		const FVector Base = GetActorLocation() + FVector(0.f, 0.f, LabelBaseHeight);
		HoldFireLabel->SetWorldLocation(Base + Up * 30.f);
		StatusLabel->SetWorldLocation(Base + Up * HoldFireStatusOffset);
	}
}

void ASoldier::ShowMessage(const FString& InMessage, float Seconds)
{
	Message = InMessage;
	MessageRemaining = Seconds;
}

void ASoldier::UpdateStatusLabel()
{
	if (!HasLabels())
	{
		return;
	}
	FString Text;
	FColor Color = FColor::White;

	if (HitPreviewRemaining > 0.f && !bDead)
	{
		Text = HitPreview;
		Color = FColor(255, 235, 120);
	}
	else if (StunRemaining > 0.f)
	{
		Text = FString::Printf(TEXT("STUNNED %d"), FMath::CeilToInt(StunRemaining));
		Color = FColor(255, 220, 40);
	}
	else if (MessageRemaining > 0.f)
	{
		Text = Message;
		Color = FColor(255, 150, 40);
	}
	else if (IsShocked())
	{
		Text = TEXT("SHOCKED");
		Color = FColor(230, 60, 255);
	}
	else if (const ASoldier* Dragged = DraggedSoldier.Get())
	{
		Text = FString::Printf(TEXT("DRAGGING %d"), Dragged->GetSquadNumber());
		Color = FColor(120, 200, 255);
	}
	else if (IsBeingDragged())
	{
		Text = TEXT("BEING DRAGGED");
		Color = FColor(120, 200, 255);
	}
	else if (const ASoldier* Patient = MedkitPatient.Get())
	{
		Text = IsApplyingMedkit()
			? FString::Printf(TEXT("MEDKIT ON %d  %ds"), Patient->GetSquadNumber(), FMath::CeilToInt(MedkitRemaining))
			: FString::Printf(TEXT("TO MEDIC %d"), Patient->GetSquadNumber());
		Color = FColor(110, 230, 120);
	}
	else if (const ASoldier* Medic = bInjured ? MedkitTreatedBy.Get() : nullptr)
	{
		Text = Medic->IsApplyingMedkit()
			? FString::Printf(TEXT("BEING TREATED  %ds"), FMath::CeilToInt(Medic->GetMedkitRemaining()))
			: FString::Printf(TEXT("MEDIC %d COMING"), Medic->GetSquadNumber());
		Color = FColor(110, 230, 120);
	}
	else if (HasBuildOrder())
	{
		// GDD §13.3: placing C4 / a reinforcement (the HUD also draws a progress bar over the soldier).
		Text = IsBuilding()
			? FString::Printf(TEXT("%s  %ds"), SquadBuild::KindTag(BuildKind), FMath::CeilToInt(BuildRemaining))
			: FString::Printf(TEXT("TO WALL (%s)"), SquadBuild::KindTag(BuildKind));
		Color = FColor(255, 200, 90);
	}
	else if (bSettingUpWeapon)
	{
		Text = FString::Printf(TEXT("SETTING UP %ds"), FMath::CeilToInt(SetupRemaining)); // GDD §6.1.
		Color = FColor(255, 200, 90);
	}
	else if (bReloading)
	{
		Text = TEXT("RELOADING");
	}
	else if (bInjured)
	{
		Text = BleedOutRemaining > 0.f ? FString::Printf(TEXT("INJURED - BLEEDING %ds"), FMath::CeilToInt(BleedOutRemaining))
			: FString(TEXT("INJURED - CRAWLING"));
		Color = FColor(255, 60, 60);
	}
	else
	{
		// Kept to two short rows so labels of soldiers 1 m apart don't run into each other.
		static const TCHAR* StanceNames[] = { TEXT(""), TEXT("CROUCH"), TEXT("PRONE") };
		Text = StanceNames[static_cast<int32>(Stance)];
		if (AttackTarget.IsValid() && LastHitChance >= 0.f)
		{
			Text = FString::Printf(TEXT("%s%sFIRE %.0f%%"), *Text, Text.IsEmpty() ? TEXT("") : TEXT(" "), LastHitChance);
		}
		// Tint by stress tier (GDD §8.1) so stressed soldiers stand out.
		switch (GetStressTier())
		{
		case EStressTier::Minor: Color = FColor(255, 230, 120); break;
		case EStressTier::Major: Color = FColor(255, 140, 60); break;
		default:                 Color = FColor(225, 225, 225); break;
		}
	}

	// Soldiers the local player doesn't command (allied players' and enemies') carry their owner's rank and name on top.
	if (!bDead)
	{
		if (!IsOwnedByLocalPlayer())
		{
			const FKillFeedPlayer Commander = GetKillFeedIdentity();
			const FString OwnerTag = FString::Printf(TEXT("%s %s"), *Commander.Rank, *Commander.Name);
			Text = Text.IsEmpty() ? OwnerTag : OwnerTag + TEXT("\n") + Text;
		}
	}

	StatusLabel->SetText(FText::FromString(Text));
	StatusLabel->SetTextRenderColor(Color);
}

void ASoldier::ShowMoveMarker(FIntPoint Cell)
{
	// Move orders are private: only the commanding player sees where their soldiers are heading.
	if (!Grid || !IsOwnedByLocalPlayer() || !HasLabels())
	{
		return;
	}
	const FVector CellCenter = Grid->CellToWorld(Cell);
	const float Scale = Grid->CellSize / 100.f * 0.9f; // Plane mesh is 100x100 cm.
	MoveMarker->SetWorldLocationAndRotation(CellCenter + FVector(0.f, 0.f, 2.f), FRotator::ZeroRotator);
	MoveMarker->SetWorldScale3D(FVector(Scale, Scale, 1.f));
	MoveMarkerLabel->SetWorldLocation(CellCenter + FVector(0.f, 0.f, 40.f));
	MoveMarkerLabel->SetVisibility(true);
	MoveMarker->SetVisibility(true);
	MoveMarkerTime = 0.f;
	FaceCamera(MoveMarkerLabel);
}

void ASoldier::GetRemainingRoute(TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();
	if (!Grid || !IsMoving())
	{
		return;
	}
	const FVector Here = GetActorLocation();
	OutPoints.Add(FVector(Here.X, Here.Y, Grid->CellToWorld(CurrentCell).Z));
	if (bStepping)
	{
		OutPoints.Add(Grid->CellToWorld(StepCell));
	}
	for (const FIntPoint& Cell : Path)
	{
		OutPoints.Add(Grid->CellToWorld(Cell));
	}
}

void ASoldier::UpdateMoveMarker(float DeltaSeconds)
{
	if (!HasLabels() || !MoveMarkerLabel->IsVisible())
	{
		return;
	}
	if (!IsMoving())
	{
		// Arrived, or the order was cancelled.
		MoveMarker->SetVisibility(false);
		MoveMarkerLabel->SetVisibility(false);
		return;
	}
	// The square blinks on/off; the number stays up so the order is always readable.
	MoveMarkerTime += DeltaSeconds;
	MoveMarker->SetVisibility(FMath::Frac(MoveMarkerTime * MoveMarkerFlashRate) < 0.5f);
	FaceCamera(MoveMarkerLabel);
}

void ASoldier::FaceCamera(UTextRenderComponent* Label) const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!Label || !PC || !PC->PlayerCameraManager)
	{
		return;
	}
	// Text renders in the component's YZ plane and reads from +X, so point +X at the camera.
	const FVector ToCamera = PC->PlayerCameraManager->GetCameraLocation() - Label->GetComponentLocation();
	Label->SetWorldRotation(ToCamera.Rotation());
}
