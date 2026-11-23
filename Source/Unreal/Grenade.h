#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Grenade.generated.h"

class AGridManager;
class USoldierData;
class UStaticMesh;
class UStaticMeshComponent;
enum class EKillMethod : uint8;

/** A throw that clears all cover: the flight arc to the bounce point, then a short bounce onto the target cell. */
struct FGrenadeThrow
{
	FVector Start = FVector::ZeroVector;
	FVector BouncePoint = FVector::ZeroVector;
	FVector Rest = FVector::ZeroVector;
	/** How far (cm) the flight bulges above the straight line from Start to BouncePoint. */
	float ArcHeight = 100.f;
	float FlightDuration = 1.f;
};

/** What a client needs to fly a grenade the server launched: the same arc, fuse and look. */
USTRUCT()
struct FGrenadeNetLaunch
{
	GENERATED_BODY()

	UPROPERTY() FVector_NetQuantize10 Start = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantize10 BouncePoint = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantize10 Rest = FVector::ZeroVector;
	UPROPERTY() float ArcHeight = 100.f;
	UPROPERTY() float FlightDuration = 1.f;
	UPROPERTY() float Fuse = 0.5f;
	UPROPERTY() float KillRadius = 1.f;
	UPROPERTY() float StunRadius = 5.f;
	UPROPERTY() TObjectPtr<UStaticMesh> BodyMesh;
	/** Bumped by Launch; 0 = not launched yet. */
	UPROPERTY() uint8 Serial = 0;
};

/**
 * Fragmentation grenade (GDD §7). Flies a ballistic arc, bounces once onto its landing cell,
 * detonates after the fuse and kills/stuns every soldier in range (both teams).
 */
UCLASS()
class UNREAL_API AGrenade : public AActor
{
	GENERATED_BODY()

public:
	AGrenade();

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * GDD §7: find a throw from From (release point) to Landing (target cell centre on the ground) that no
	 * cover stops. Tries the natural arc first, then flatter and higher ones (e.g. flat enough to go through
	 * a window). Returns false if every arc hits cover - for example a full wall in the way.
	 */
	static bool PlanThrow(const AGridManager& Grid, const FVector& From, const FVector& Landing, const USoldierData& Data, FGrenadeThrow& OutThrow);

	void Launch(const FGrenadeThrow& Throw, const USoldierData& Data);

	/** Show the thrower's own grenade model (Mk 2 / Stielhandgranate) instead of the placeholder ball. Cosmetic. */
	void SetBodyMesh(class UStaticMesh* Mesh);

	/** In the air, bouncing or on its fuse: it has yet to go off. */
	bool IsLive() const { return Phase == EPhase::Flight || Phase == EPhase::Bounce || Phase == EPhase::Fuse; }

	/** Where the grenade comes to rest and detonates (world space). */
	FVector GetLandingLocation() const { return Rest; }

	/** Stun radius in metres (GDD §7.1), known once launched. */
	float GetStunRadius() const { return StunRadius; }

	/**
	 * Server: GDD §7.1 blast effects round Origin on every living soldier, friend or foe (distance on the ground, no
	 * cover in the way counts unless bCoverShields, see AGridManager::IsBlastShielded). Shared by grenades (shielded) and C4
	 * (GDD §7.2, not shielded). Radii in metres; Instigator is credited with kills.
	 */
	static void ApplyBlast(UWorld* World, const FVector& Origin, float InKillRadius, float InKillChance, float InCloseStunDuration,
		float InStunRadius, float InStunDuration, const class ASoldier* Instigator, EKillMethod Method, bool bCoverShields = false);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Grenade")
	TObjectPtr<UStaticMeshComponent> Body;

	/** Fireball shown on detonation. */
	UPROPERTY(VisibleAnywhere, Category = "Grenade")
	TObjectPtr<UStaticMeshComponent> Blast;

	/** Flat disc showing the stun radius for a moment after detonation. */
	UPROPERTY(VisibleAnywhere, Category = "Grenade")
	TObjectPtr<UStaticMeshComponent> StunRing;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	enum class EPhase : uint8 { Idle, Flight, Bounce, Fuse, Exploding };

	/** Server: set by Launch. Clients fly their own copy from it; only the server's blast does anything. */
	UPROPERTY(ReplicatedUsing = OnRep_NetLaunch)
	FGrenadeNetLaunch NetLaunch;

	UFUNCTION()
	void OnRep_NetLaunch();

	/** Start the flight from NetLaunch (server and clients). */
	void BeginFlight();

	bool bFinished = false;

	void Explode();

	EPhase Phase = EPhase::Idle;
	float PhaseTime = 0.f;
	float FlightDuration = 1.f;
	float FlightArc = 100.f;
	FVector Start = FVector::ZeroVector;
	FVector BouncePoint = FVector::ZeroVector;
	FVector Rest = FVector::ZeroVector;

	// Copied from USoldierData at launch.
	float KillRadius = 1.f;
	float KillChance = 0.1f;
	float CloseStunDuration = 7.f;
	float StunRadius = 5.f;
	float StunDuration = 5.f;
	float Fuse = 0.5f;
};
