#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExplosionFX.generated.h"

class UMaterialInterface;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class USoldierVisualData;

/**
 * Cosmetic low-poly explosion (grenade, C4): a white flash with streaks, a fireball that burns down to soot,
 * rising smoke, thrown dirt clods, low dust puffs rolling out to the stun radius and a scorch mark that fades
 * after a while. Built from the flat-shaded SourceArt/Effects pieces named on USoldierVisualData (Effects).
 *
 * Purely local: never replicated and never spawned on a dedicated server. Each machine spawns its own when it
 * plays the blast (the server's AGrenade / AC4Charge and their client copies), with its own random layout.
 */
UCLASS(NotPlaceable, Transient)
class UNREAL_API AExplosionFX : public AActor
{
	GENERATED_BODY()

public:
	AExplosionFX();

	/**
	 * Spawn an explosion with Ground the point on the ground under the blast. Scale sizes everything (1 = hand
	 * grenade); the ground dust reaches about DustRadius metres. Returns null on a dedicated server or when the effect
	 * meshes are missing from the visual data (callers then keep their old placeholder look).
	 */
	static AExplosionFX* SpawnAt(UWorld* World, const FVector& Ground, float Scale, float DustRadius);

	virtual void Tick(float DeltaSeconds) override;

private:
	enum class EKind : uint8 { Flash, Streak, Fire, Smoke, Clod, Scorch, Dust };

	struct FPiece
	{
		EKind Kind = EKind::Flash;
		UStaticMeshComponent* Mesh = nullptr; // Kept alive by Parts.
		float Delay = 0.f;
		float Life = 1.f;
		float Size = 1.f;  // Metres.
		FVector Dir = FVector::UpVector;
		FVector Offset = FVector::ZeroVector; // cm from Ground.
		FVector Velocity = FVector::ZeroVector; // cm/s (clods), drift (smoke).
		FQuat Rotation = FQuat::Identity;
		FVector SpinAxis = FVector::UpVector;
		float SpinRate = 0.f; // Radians per second.
	};

	bool Build(const USoldierVisualData& Visuals, float InScale, float InDustRadius);
	UStaticMeshComponent* AddPart(UStaticMesh* Mesh, UMaterialInterface* Material, bool bCastShadow);
	void UpdatePiece(const FPiece& Piece);

	/** Colour, glow and dithered opacity, read by M_FX_Master from custom primitive data. */
	static void SetLook(UStaticMeshComponent* Mesh, const FLinearColor& Color, float Emissive, float Opacity);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> Flash;

	TArray<FPiece> Pieces;
	FVector Ground = FVector::ZeroVector;
	FVector Wind = FVector::ZeroVector;
	float Scale = 1.f;
	float Age = 0.f;
};
