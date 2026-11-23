#include "ExplosionFX.h"

#include "GridManager.h"
#include "SoldierVisualData.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"

namespace
{
	constexpr float UUPerMetre = 100.f;
	constexpr float Gravity = 980.f;
	constexpr float ScorchLife = 14.f;
	constexpr float FlashLightLife = 0.3f;

	TAutoConsoleVariable<float> CVarExplosionFXFreeze(
		TEXT("squad.ExplosionFXFreeze"), -1.f,
		TEXT("Debug: hold every explosion effect at this age in seconds (-1 = play normally)."),
		ECVF_Cheat);

	// Custom primitive data slots read by M_FX_Master (see SourceArt/Effects/import_explosion_fx_unreal.py).
	constexpr int32 OpacitySlot = 0;
	constexpr int32 EmissiveSlot = 1;
	constexpr int32 ColorSlot = 2;

	float EaseOut(float A) { return 1.f - FMath::Square(1.f - A); }

	/** Fades from 1 to 0 over the last (1 - From) of a 0-1 life. */
	float FadeAfter(float A, float From) { return A <= From ? 1.f : FMath::Clamp(1.f - (A - From) / (1.f - From), 0.f, 1.f); }

	FLinearColor Lerp3(const FLinearColor& A, const FLinearColor& B, const FLinearColor& C, float T)
	{
		return T < 0.5f ? FMath::Lerp(A, B, T * 2.f) : FMath::Lerp(B, C, T * 2.f - 1.f);
	}

	FVector RandomUpperDir(float MinZ, float MaxZ)
	{
		const float Yaw = FMath::FRandRange(0.f, 2.f * PI);
		const float Z = FMath::FRandRange(MinZ, MaxZ);
		const float R = FMath::Sqrt(FMath::Max(0.f, 1.f - Z * Z));
		return FVector(R * FMath::Cos(Yaw), R * FMath::Sin(Yaw), Z);
	}

	FQuat RandomRotation()
	{
		return FRotator(FMath::FRandRange(-180.f, 180.f), FMath::FRandRange(-180.f, 180.f), FMath::FRandRange(-180.f, 180.f)).Quaternion();
	}

	// Palette (linear).
	const FLinearColor FlashWhite(1.f, 0.92f, 0.7f);
	const FLinearColor FireYellow(1.f, 0.58f, 0.12f);
	const FLinearColor FireOrange(0.95f, 0.24f, 0.025f);
	const FLinearColor FireRed(0.38f, 0.045f, 0.012f);
	const FLinearColor Soot(0.035f, 0.03f, 0.028f);
	const FLinearColor SmokeDark(0.1f, 0.088f, 0.075f);
	const FLinearColor SmokeLight(0.42f, 0.4f, 0.36f);
	const FLinearColor Earth(0.075f, 0.052f, 0.034f);
	const FLinearColor Dust(0.19f, 0.155f, 0.105f);
	const FLinearColor Scorched(0.012f, 0.011f, 0.01f);
}

AExplosionFX::AExplosionFX()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetCanBeDamaged(false);

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Flash = CreateDefaultSubobject<UPointLightComponent>(TEXT("Flash"));
	Flash->SetupAttachment(RootComponent);
	Flash->SetCastShadows(false);
	Flash->SetIntensityUnits(ELightUnits::Candelas);
	Flash->SetIntensity(0.f);
	Flash->SetLightColor(FLinearColor(1.f, 0.62f, 0.3f));
	Flash->SetVisibility(false);
}

AExplosionFX* AExplosionFX::SpawnAt(UWorld* World, const FVector& InGround, float InScale, float InDustRadius)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	const USoldierVisualData* Visuals = USoldierVisualData::GetDefaultVisuals();
	if (!Visuals || !Visuals->ExplosionPuffMesh || !Visuals->ExplosionMaterial)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	AExplosionFX* FX = World->SpawnActor<AExplosionFX>(InGround, FRotator::ZeroRotator, Params);
	if (FX && !FX->Build(*Visuals, InScale, InDustRadius))
	{
		FX->Destroy();
		return nullptr;
	}
	return FX;
}

UStaticMeshComponent* AExplosionFX::AddPart(UStaticMesh* Mesh, UMaterialInterface* Material, bool bCastShadow)
{
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	Part->SetStaticMesh(Mesh);
	Part->SetMaterial(0, Material);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetGenerateOverlapEvents(false);
	Part->SetCanEverAffectNavigation(false);
	Part->SetCastShadow(bCastShadow);
	Part->SetMobility(EComponentMobility::Movable);
	Part->SetupAttachment(RootComponent);
	Part->SetUsingAbsoluteLocation(true);
	Part->SetUsingAbsoluteRotation(true);
	Part->SetUsingAbsoluteScale(true);
	Part->SetVisibility(false);
	Part->RegisterComponent();
	Parts.Add(Part);
	return Part;
}

bool AExplosionFX::Build(const USoldierVisualData& Visuals, float InScale, float InDustRadius)
{
	UMaterialInterface* Material = Visuals.ExplosionMaterial;
	UStaticMesh* Puff = Visuals.ExplosionPuffMesh;
	UStaticMesh* Spike = Visuals.ExplosionSpikeMesh ? Visuals.ExplosionSpikeMesh.Get() : Puff;
	UStaticMesh* Clod = Visuals.ExplosionClodMesh ? Visuals.ExplosionClodMesh.Get() : Puff;
	if (!Material || !Puff)
	{
		return false;
	}
	Ground = GetActorLocation();
	Scale = FMath::Max(InScale, 0.1f);
	const float S = Scale;
	const float WindYaw = FMath::FRandRange(0.f, 2.f * PI);
	Wind = FVector(FMath::Cos(WindYaw), FMath::Sin(WindYaw), 0.f) * FMath::FRandRange(40.f, 90.f) * S;

	auto Add = [this](EKind Kind, UStaticMeshComponent* Mesh) -> FPiece&
	{
		FPiece& P = Pieces.AddDefaulted_GetRef();
		P.Kind = Kind;
		P.Mesh = Mesh;
		P.Rotation = RandomRotation();
		return P;
	};

	// Scorch mark first: drawn under everything else.
	if (Visuals.ExplosionScorchMesh)
	{
		FPiece& P = Add(EKind::Scorch, AddPart(Visuals.ExplosionScorchMesh, Material, false));
		P.Life = ScorchLife;
		P.Size = 1.7f * S;
		P.Rotation = FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Quaternion();
	}

	// White-hot core: the first two or three frames.
	{
		FPiece& P = Add(EKind::Flash, AddPart(Puff, Material, false));
		P.Life = 0.09f;
		P.Size = 1.15f * S;
		P.Offset = FVector(0.f, 0.f, 35.f * S);
	}

	// Streaks shooting out of the flash, mostly up and sideways (the ground stops the rest).
	for (int32 i = 0; i < 8; ++i)
	{
		FPiece& P = Add(EKind::Streak, AddPart(Spike, Material, false));
		P.Life = FMath::FRandRange(0.14f, 0.22f);
		P.Size = FMath::FRandRange(1.0f, 1.9f) * S;
		P.Dir = RandomUpperDir(0.05f, 0.95f);
		P.Offset = FVector(0.f, 0.f, 25.f * S);
		P.Rotation = FRotationMatrix::MakeFromZ(P.Dir).ToQuat();
	}

	// Fireball: a lumpy cluster that swells, rolls upward and burns down to soot.
	for (int32 i = 0; i < 7; ++i)
	{
		FPiece& P = Add(EKind::Fire, AddPart(Puff, Material, false));
		P.Delay = FMath::FRandRange(0.f, 0.04f);
		P.Life = FMath::FRandRange(0.65f, 0.95f);
		P.Size = FMath::FRandRange(0.8f, 1.2f) * S;
		P.Dir = RandomUpperDir(0.1f, 0.8f);
		P.Offset = FVector(0.f, 0.f, 40.f * S) + P.Dir * FMath::FRandRange(10.f, 35.f) * S;
		P.SpinAxis = FMath::VRand();
		P.SpinRate = FMath::FRandRange(-2.f, 2.f);
	}

	// Smoke: dark at first, rising and spreading, greying as it thins out.
	for (int32 i = 0; i < 8; ++i)
	{
		FPiece& P = Add(EKind::Smoke, AddPart(Puff, Material, true));
		P.Delay = FMath::FRandRange(0.04f, 0.22f);
		P.Life = FMath::FRandRange(2.8f, 4.2f);
		P.Size = FMath::FRandRange(1.1f, 1.8f) * S;
		P.Dir = RandomUpperDir(0.f, 0.6f);
		P.Offset = FVector(0.f, 0.f, 45.f * S) + P.Dir * FMath::FRandRange(10.f, 55.f) * S;
		P.Velocity = FVector(P.Dir.X, P.Dir.Y, 0.f) * FMath::FRandRange(30.f, 70.f) * S // Spread (cm over the life).
			+ FVector(0.f, 0.f, FMath::FRandRange(170.f, 300.f) * S); // Rise.
		P.SpinAxis = FMath::VRand();
		P.SpinRate = FMath::FRandRange(-0.6f, 0.6f);
	}

	// Dirt clods thrown up and out, falling back and lying a moment.
	for (int32 i = 0; i < 12; ++i)
	{
		FPiece& P = Add(EKind::Clod, AddPart(Clod, Material, true));
		P.Life = FMath::FRandRange(2.0f, 3.0f);
		P.Size = FMath::FRandRange(0.07f, 0.16f) * FMath::Sqrt(S);
		const FVector Out = RandomUpperDir(0.f, 0.f);
		P.Offset = Out * 15.f * S + FVector(0.f, 0.f, 8.f);
		P.Velocity = Out * FMath::FRandRange(120.f, 380.f) * S + FVector(0.f, 0.f, FMath::FRandRange(280.f, 620.f) * FMath::Sqrt(S));
		P.SpinAxis = FMath::VRand();
		P.SpinRate = FMath::FRandRange(6.f, 16.f);
	}

	// Low flattened puffs rolling out along the ground to about the stun radius.
	const int32 DustPuffs = InDustRadius > 0.f ? 10 : 0;
	for (int32 i = 0; i < DustPuffs; ++i)
	{
		FPiece& P = Add(EKind::Dust, AddPart(Puff, Material, false));
		const float Yaw = (i + FMath::FRandRange(-0.35f, 0.35f)) * 2.f * PI / DustPuffs;
		P.Dir = FVector(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.f);
		P.Delay = FMath::FRandRange(0.f, 0.05f);
		P.Life = FMath::FRandRange(0.9f, 1.4f);
		P.Size = FMath::FRandRange(0.55f, 0.9f) * S;
		P.Offset = P.Dir * 30.f * S + FVector(0.f, 0.f, 12.f * S);
		P.Velocity = P.Dir * FMath::Max(InDustRadius * UUPerMetre * FMath::FRandRange(0.75f, 1.f) - 30.f * S, 0.f)
			+ FVector(0.f, 0.f, FMath::FRandRange(15.f, 45.f) * S); // Travel over the life.
		P.Rotation = FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Quaternion(); // Yaw only: squashed flat.
		P.SpinAxis = FVector::UpVector;
		P.SpinRate = FMath::FRandRange(-1.5f, 1.5f);
	}

	Flash->SetAttenuationRadius(900.f * S);
	Flash->SetRelativeLocation(FVector(0.f, 0.f, 60.f * S));
	Flash->SetVisibility(true);

	float Longest = FlashLightLife;
	for (const FPiece& P : Pieces)
	{
		Longest = FMath::Max(Longest, P.Delay + P.Life);
	}
	SetLifeSpan(Longest + 0.25f);
	return true;
}

void AExplosionFX::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (const float Freeze = CVarExplosionFXFreeze.GetValueOnGameThread(); Freeze >= 0.f)
	{
		Age = Freeze;
		SetLifeSpan(5.f); // Kept while frozen.
	}

	const float LightAlpha = FMath::Min(Age / FlashLightLife, 1.f);
	Flash->SetIntensity(400.f * Scale * FMath::Square(1.f - LightAlpha));
	if (LightAlpha >= 1.f && Flash->IsVisible())
	{
		Flash->SetVisibility(false);
	}

	for (const FPiece& Piece : Pieces)
	{
		UpdatePiece(Piece);
	}
}

void AExplosionFX::UpdatePiece(const FPiece& P)
{
	// Everything is a function of the age alone, so squad.ExplosionFXFreeze can hold any moment.
	UStaticMeshComponent* Mesh = P.Mesh;
	const float T = Age - P.Delay;
	if (!Mesh || T < 0.f || T > P.Life)
	{
		if (Mesh && Mesh->IsVisible())
		{
			Mesh->SetVisibility(false);
		}
		return;
	}
	const float A = T / P.Life;
	FQuat Rotation = P.SpinRate != 0.f ? FQuat(P.SpinAxis, P.SpinRate * T) * P.Rotation : P.Rotation;

	FVector Location = Ground + P.Offset;
	FVector Size(P.Size);
	FLinearColor Color = FLinearColor::White;
	float Emissive = 0.f;
	float Opacity = 1.f;

	switch (P.Kind)
	{
	case EKind::Flash:
		Size *= FMath::Lerp(0.35f, 1.f, EaseOut(FMath::Min(A * 2.f, 1.f)));
		Color = FMath::Lerp(FlashWhite, FireYellow, A);
		Emissive = 14.f * FMath::Square(1.f - A);
		Opacity = FadeAfter(A, 0.5f);
		break;

	case EKind::Streak:
	{
		// Shoots out fast, then thins and burns away.
		const float Length = P.Size * FMath::Pow(FMath::Sin(PI * FMath::Min(A * 1.4f, 1.f)), 0.5f) + 0.01f;
		const float Thick = P.Size * 0.55f * (1.f - 0.6f * A);
		Size = FVector(Thick, Thick, Length);
		Location += P.Dir * UUPerMetre * P.Size * 0.25f * A;
		Color = FMath::Lerp(FireYellow, FireOrange, A);
		Emissive = 9.f * (1.f - A);
		Opacity = FadeAfter(A, 0.6f);
		break;
	}

	case EKind::Fire:
	{
		const float Grow = EaseOut(FMath::Min(A * 3.f, 1.f));
		Size *= FMath::Lerp(0.3f, 1.f, Grow) * FMath::Lerp(1.f, 0.65f, FMath::Max(A - 0.6f, 0.f) / 0.4f);
		Location += P.Dir * UUPerMetre * Scale * 0.45f * Grow + FVector(0.f, 0.f, 70.f * Scale * A * A);
		const float Burn = FMath::Min(A / 0.55f, 1.f);
		Color = Burn < 1.f ? Lerp3(FireYellow, FireOrange, FireRed, Burn) : FMath::Lerp(FireRed, Soot, FMath::Min((A - 0.55f) / 0.25f, 1.f));
		Emissive = 7.f * FMath::Pow(1.f - Burn, 1.4f);
		Opacity = FadeAfter(A, 0.7f);
		break;
	}

	case EKind::Smoke:
	{
		const float Rise = EaseOut(A);
		Size *= FMath::Lerp(0.35f, 1.f, Rise);
		Location += P.Velocity * Rise + Wind * A * A * 2.f;
		Color = FMath::Lerp(SmokeDark, SmokeLight, FMath::Min(A * 1.4f, 1.f));
		Emissive = T < 0.15f ? 1.5f * (1.f - T / 0.15f) : 0.f; // Lit from below by the fireball at first.
		if (Emissive > 0.f)
		{
			Color = FMath::Lerp(Color, FireOrange, 0.3f * (1.f - T / 0.15f));
		}
		Opacity = FadeAfter(A, 0.5f);
		break;
	}

	case EKind::Clod:
	{
		// Ballistic until it comes down to rest on the ground, then lies still.
		const float Rest = P.Size * UUPerMetre * 0.3f;
		const float Vz = P.Velocity.Z;
		const float Land = (Vz + FMath::Sqrt(FMath::Max(Vz * Vz + 2.f * Gravity * (P.Offset.Z - Rest), 0.f))) / Gravity;
		const float Fly = FMath::Min(T, Land);
		Location = Ground + P.Offset + P.Velocity * Fly - FVector(0.f, 0.f, 0.5f * Gravity * Fly * Fly);
		Rotation = FQuat(P.SpinAxis, P.SpinRate * Fly) * P.Rotation;
		Color = Earth;
		Opacity = FadeAfter(A, 0.8f);
		break;
	}

	case EKind::Scorch:
		Size = FVector(P.Size, P.Size, 1.f);
		Location.Z += 1.5f;
		Color = Scorched;
		Opacity = FMath::Min(T / 0.1f, 1.f) * FadeAfter(A, 0.7f);
		break;

	case EKind::Dust:
	{
		const float Out = 1.f - FMath::Pow(1.f - A, 3.f); // Fast out, then hangs.
		const float Spread = FMath::Lerp(0.5f, 1.4f, Out);
		Size = FVector(P.Size * Spread, P.Size * Spread, P.Size * Spread * 0.45f);
		Location += P.Velocity * Out;
		Color = Dust;
		Opacity = 0.85f * FadeAfter(A, 0.2f);
		break;
	}
	}

	if (Opacity <= 0.01f)
	{
		Mesh->SetVisibility(false);
		return;
	}
	Mesh->SetWorldLocationAndRotation(Location, Rotation);
	Mesh->SetWorldScale3D(Size);
	SetLook(Mesh, Color, Emissive, Opacity);
	if (!Mesh->IsVisible())
	{
		Mesh->SetVisibility(true);
	}
}

void AExplosionFX::SetLook(UStaticMeshComponent* Mesh, const FLinearColor& Color, float Emissive, float Opacity)
{
	Mesh->SetCustomPrimitiveDataFloat(OpacitySlot, Opacity);
	Mesh->SetCustomPrimitiveDataFloat(EmissiveSlot, Emissive);
	Mesh->SetCustomPrimitiveDataVector3(ColorSlot, FVector(Color.R, Color.G, Color.B));
}

namespace
{
	FAutoConsoleCommandWithWorldAndArgs CmdTestExplosion(
		TEXT("squad.TestExplosion"),
		TEXT("squad.TestExplosion <CellX> <CellY> [Scale=1] [DustRadius=2.5]: play the explosion effect on a grid cell, on this machine only (debug, cosmetic)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const AGridManager* Grid = AGridManager::Find(World);
			if (!Grid || Args.Num() < 2)
			{
				return;
			}
			const FIntPoint Cell(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]));
			const float FXScale = Args.IsValidIndex(2) ? FCString::Atof(*Args[2]) : 1.f;
			const float DustRadius = Args.IsValidIndex(3) ? FCString::Atof(*Args[3]) : 2.5f;
			AExplosionFX::SpawnAt(World, Grid->CellToWorld(Cell), FXScale, DustRadius);
		}),
		ECVF_Cheat);
}
