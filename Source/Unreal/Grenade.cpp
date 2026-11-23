#include "Grenade.h"

#include "GridManager.h"
#include "Soldier.h"
#include "SoldierData.h"
#include "SoldierVisualData.h"
#include "Algo/AllOf.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float UUPerMetre = 100.f;
	constexpr float BounceDuration = 0.25f;
	constexpr float BounceHeight = 25.f;
	constexpr float ExplosionDuration = 0.45f;
	constexpr float GrenadeRestHeight = 6.f;

	// Throw planning (cm). The bounce stays inside the target cell (cells are 100 cm, centre to edge 50).
	constexpr float MaxBounceLength = 15.f;
	constexpr float MinArc = 60.f;
	constexpr float NaturalArcMax = 600.f;
	/** Highest lob tried when the natural arc is blocked (10 m). */
	constexpr float MaxArc = 1000.f;
	constexpr float ArcStep = 20.f;

	UStaticMeshComponent* MakePart(AActor* Owner, FName Name, UStaticMesh* Mesh, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Part = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetStaticMesh(Mesh);
		Part->SetMaterial(0, Material);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(false);
		return Part;
	}

	void SetPartColor(UStaticMeshComponent* Mesh, UObject* Outer, const FLinearColor& Color)
	{
		if (UMaterialInterface* Base = Mesh->GetMaterial(0))
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

AGrenade::AGrenade()
{
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Body = MakePart(this, TEXT("Body"), SphereMesh.Object, ShapeMaterial.Object);
	Body->SetupAttachment(RootComponent);
	Body->SetRelativeScale3D(FVector(0.12f));

	Blast = MakePart(this, TEXT("Blast"), SphereMesh.Object, ShapeMaterial.Object);
	Blast->SetupAttachment(RootComponent);
	Blast->SetVisibility(false);

	StunRing = MakePart(this, TEXT("StunRing"), CylinderMesh.Object, ShapeMaterial.Object);
	StunRing->SetupAttachment(RootComponent);
	StunRing->SetVisibility(false);
}

bool AGrenade::PlanThrow(const AGridManager& Grid, const FVector& From, const FVector& Landing, const USoldierData& Data, FGrenadeThrow& OutThrow)
{
	OutThrow.Start = From;
	OutThrow.Rest = Landing + FVector(0.f, 0.f, GrenadeRestHeight);
	// First touchdown just short of the cell centre; the single small bounce carries it to rest. Keeping the
	// bounce near the centre means it never hops into cover and the grenade can drop in right behind a wall.
	const FVector Flat(From.X, From.Y, OutThrow.Rest.Z);
	const float Horizontal = FVector::Dist2D(Flat, OutThrow.Rest);
	const float BounceLength = FMath::Min(Horizontal * 0.15f, MaxBounceLength);
	OutThrow.BouncePoint = OutThrow.Rest + (Flat - OutThrow.Rest).GetSafeNormal2D() * BounceLength;

	const float HorizontalMetres = FVector::Dist2D(From, OutThrow.BouncePoint) / UUPerMetre;
	OutThrow.FlightDuration = FMath::Max(0.25f, HorizontalMetres / FMath::Max(Data.GrenadeFlightSpeed, 1.f));

	// Natural lob first, then the other candidates closest to it (lower arcs fit through windows,
	// higher ones clear hay bales and low walls close to the thrower).
	const float NaturalArc = FMath::Clamp(HorizontalMetres * 25.f, MinArc, NaturalArcMax);
	TArray<float, TInlineAllocator<64>> Arcs;
	for (float Arc = 0.f; Arc <= MaxArc; Arc += ArcStep)
	{
		Arcs.Add(Arc);
	}
	Arcs.Sort([NaturalArc](float A, float B) { return FMath::Abs(A - NaturalArc) < FMath::Abs(B - NaturalArc); });
	Arcs.Insert(NaturalArc, 0);

	for (const float Arc : Arcs)
	{
		if (Grid.IsGrenadeArcClear(From, OutThrow.BouncePoint, Arc)
			&& Grid.IsGrenadeArcClear(OutThrow.BouncePoint, OutThrow.Rest, BounceHeight))
		{
			OutThrow.ArcHeight = Arc;
			return true;
		}
	}
	return false;
}

void AGrenade::Launch(const FGrenadeThrow& Throw, const USoldierData& Data)
{
	KillRadius = Data.GrenadeKillRadius;
	KillChance = Data.GrenadeKillChance;
	CloseStunDuration = Data.GrenadeCloseStunDuration;
	StunRadius = Data.GrenadeStunRadius;
	StunDuration = Data.GrenadeStunDuration;
	Fuse = Data.GrenadeFuseAfterBounce;

	Start = Throw.Start;
	BouncePoint = Throw.BouncePoint;
	Rest = Throw.Rest;
	FlightDuration = Throw.FlightDuration;
	FlightArc = Throw.ArcHeight;

	SetPartColor(Body, this, FLinearColor(0.12f, 0.16f, 0.08f));
	SetPartColor(Blast, this, FLinearColor(1.f, 0.45f, 0.05f));
	SetPartColor(StunRing, this, FLinearColor(0.6f, 0.25f, 0.05f));

	SetActorLocation(Start);
	Phase = EPhase::Flight;
	PhaseTime = 0.f;
}

void AGrenade::SetBodyMesh(UStaticMesh* Mesh)
{
	if (!Mesh || !Body)
	{
		return;
	}
	Body->SetStaticMesh(Mesh);
	Body->EmptyOverrideMaterials(); // Drop the placeholder tint: the model has its own materials.
	Body->SetRelativeScale3D(FVector::OneVector);
}

void AGrenade::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PhaseTime += DeltaSeconds;

	switch (Phase)
	{
	case EPhase::Flight:
	{
		const float Alpha = FMath::Min(PhaseTime / FlightDuration, 1.f);
		FVector P = FMath::Lerp(Start, BouncePoint, Alpha);
		P.Z += FlightArc * 4.f * Alpha * (1.f - Alpha);
		SetActorLocation(P);
		if (Alpha >= 1.f)
		{
			Phase = EPhase::Bounce;
			PhaseTime = 0.f;
		}
		break;
	}
	case EPhase::Bounce:
	{
		const float Alpha = FMath::Min(PhaseTime / BounceDuration, 1.f);
		FVector P = FMath::Lerp(BouncePoint, Rest, Alpha);
		P.Z += BounceHeight * 4.f * Alpha * (1.f - Alpha);
		SetActorLocation(P);
		if (Alpha >= 1.f)
		{
			Phase = EPhase::Fuse;
			PhaseTime = 0.f;
		}
		break;
	}
	case EPhase::Fuse:
		if (PhaseTime >= Fuse)
		{
			Explode();
		}
		break;
	case EPhase::Exploding:
	{
		const float Alpha = FMath::Min(PhaseTime / ExplosionDuration, 1.f);
		// Fireball grows to the kill radius then shrinks away.
		const float FireballDiameter = KillRadius * 2.f * FMath::Sin(Alpha * PI);
		Blast->SetRelativeScale3D(FVector(FMath::Max(FireballDiameter, 0.01f)));
		if (Alpha >= 1.f)
		{
			Destroy();
		}
		break;
	}
	default:
		break;
	}
}

void AGrenade::Explode()
{
	Phase = EPhase::Exploding;
	PhaseTime = 0.f;
	Body->SetVisibility(false);
	Blast->SetVisibility(true);
	StunRing->SetVisibility(true);
	// Cylinder mesh is 100 cm across: scale = diameter in metres.
	StunRing->SetRelativeLocation(FVector(0.f, 0.f, -GrenadeRestHeight + 1.f));
	StunRing->SetRelativeScale3D(FVector(StunRadius * 2.f, StunRadius * 2.f, 0.01f));
	if (const USoldierVisualData* Visuals = USoldierVisualData::GetDefaultVisuals())
	{
		Visuals->PlayCombatSound(this, Visuals->ExplosionSound, Rest);
	}

	if (!HasAuthority())
	{
		return;
	}

	// The thrower (spawned with Owner = thrower) is credited with any kill.
	ApplyBlast(GetWorld(), Rest, KillRadius, KillChance, CloseStunDuration, StunRadius, StunDuration, Cast<ASoldier>(GetOwner()),
		EKillMethod::Grenade, /*bCoverShields*/ true);
}

void AGrenade::ApplyBlast(UWorld* World, const FVector& Origin, float InKillRadius, float InKillChance, float InCloseStunDuration,
	float InStunRadius, float InStunDuration, const ASoldier* Instigator, EKillMethod Method, bool bCoverShields)
{
	if (!World)
	{
		return;
	}
	const AGridManager* Grid = bCoverShields ? AGridManager::Find(World) : nullptr;
	FIntPoint BlastCell = FIntPoint::ZeroValue;
	const bool bShieldable = Grid && Grid->WorldToCell(Origin, BlastCell);

	// GDD §7.1: within the kill radius (1 m) - 10% kill, else 7 s stun; within the stun radius (grenade 2.5 m,
	// C4 4 m) - 5 s stun; beyond - nothing. Friend or foe. A grenade's blast is stopped by cover between it and the
	// soldier, judged when it goes off: a soldier peering over low cover or through a window is caught unless it
	// got down prone in time.
	for (TActorIterator<ASoldier> It(World); It; ++It)
	{
		ASoldier* Soldier = *It;
		if (Soldier->IsDead() || Soldier->IsDebugInvulnerable())
		{
			continue;
		}
		const float Metres = FVector::Dist2D(Origin, Soldier->GetActorLocation()) / UUPerMetre;
		if (Metres > InStunRadius && Metres > InKillRadius)
		{
			continue;
		}
		if (bShieldable)
		{
			// Leaning out past a wall's edge exposes the soldier from the open cell beside it too (GDD §9.2).
			TArray<FIntPoint, TInlineAllocator<3>> Origins;
			Soldier->GetSightOrigins(BlastCell, Origins);
			const float Height = Soldier->GetSightHeight();
			const bool bShielded = Origins.Num() > 0 && Algo::AllOf(Origins, [&](FIntPoint Cell)
			{
				return Grid->IsBlastShielded(BlastCell, Cell, Height);
			});
			if (bShielded)
			{
				continue;
			}
		}
		if (Metres <= InKillRadius)
		{
			if (FMath::FRand() < InKillChance)
			{
				FKillCause Cause;
				Cause.Killer = Instigator;
				Cause.Method = Method;
				Cause.ChancePercent = InKillChance * 100.f;
				Soldier->Die(Cause);
			}
			else
			{
				Soldier->ApplyStun(InCloseStunDuration);
			}
		}
		else if (Metres <= InStunRadius)
		{
			Soldier->ApplyStun(InStunDuration);
		}
	}
}
