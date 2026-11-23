#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SquadTypes.h"
#include "C4Charge.generated.h"

class ABreachableWall;
class ASoldier;
class USoldierData;
class UStaticMeshComponent;
class UTextRenderComponent;

UENUM()
enum class EC4State : uint8
{
	/** Stuck to the wall, fuse burning. */
	Armed,
	/** Went off (wall breached unless it was already reinforced when the charge was placed). */
	Detonated,
	/** The wall was reinforced while the fuse burned: it fizzles out and does nothing. */
	Dud
};

/**
 * A C4 breaching charge stuck to one face of a breachable wall (GDD §7.2). Spawned by the server when a soldier
 * finishes placing it. After C4FuseTime it detonates: breaches the wall (ABreachableWall::Breach) and blasts every
 * soldier round the charge with the frag grenade's effects (GDD §7.1, with C4's own radii C4KillRadius / C4StunRadius), friend or foe.
 * The wall it is on does not shield anyone, since it is the thing being blown apart.
 *   - Wall already reinforced when the charge was placed: still explodes (soldier effects) but doesn't breach.
 *   - Wall reinforced while the fuse burns: the charge is a dud and fizzles (no breach, no blast).
 * Seen by the enemy only while one of their living soldiers has line of sight to the face it is on (GDD §9.3).
 */
UCLASS()
class UNREAL_API AC4Charge : public AActor
{
	GENERATED_BODY()

public:
	AC4Charge();

	virtual void Tick(float DeltaSeconds) override;

	/** Server: stick it to Wall's WallCell on the face towards StandCell and light the fuse. Placer is credited with kills. */
	void Arm(ABreachableWall* InWall, FIntPoint InWallCell, FIntPoint InStandCell, ASoldier* InPlacer, const USoldierData& Data);

	/** World location of a charge stuck to WallCell on the face towards StandCell. */
	static FVector GetFaceLocation(const UObject* WorldContext, FIntPoint WallCell, FIntPoint StandCell);

	EC4State GetState() const { return State; }
	bool IsArmed() const { return State == EC4State::Armed; }
	float GetFuseRemaining() const { return FuseRemaining; }
	ESquadTeam GetTeam() const { return Team; }
	ABreachableWall* GetWall() const { return Wall; }

	/** Placing team always; the other team only while it has line of sight to the charge (decided by the server). */
	bool IsVisibleToTeam(ESquadTeam Viewer) const { return Viewer == Team || bSpottedByEnemy; }

	/** Armed charges stuck to Wall. */
	static void GetChargesOnWall(const UObject* WorldContext, const ABreachableWall* Wall, TArray<AC4Charge*>& OutCharges);

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, Category = "C4")
	TObjectPtr<UStaticMeshComponent> Body;

	/** Small red light that blinks faster as the fuse runs down. */
	UPROPERTY(VisibleAnywhere, Category = "C4")
	TObjectPtr<UStaticMeshComponent> Light;

	UPROPERTY(VisibleAnywhere, Category = "C4")
	TObjectPtr<UStaticMeshComponent> Blast;

	UPROPERTY(VisibleAnywhere, Category = "C4")
	TObjectPtr<UStaticMeshComponent> StunRing;

	/** Fuse seconds while armed, "DUD" once it fizzles. */
	UPROPERTY(VisibleAnywhere, Category = "C4")
	TObjectPtr<UTextRenderComponent> Label;

private:
	UFUNCTION()
	void OnRep_State();

	void Detonate();
	void PlayExplosionSound() const;
	void Fizzle();
	void UpdateSpotted();
	void UpdateLocalVisibility();
	void ApplyStateVisuals();

	UPROPERTY(ReplicatedUsing = OnRep_State)
	EC4State State = EC4State::Armed;

	UPROPERTY(Replicated)
	ESquadTeam Team = ESquadTeam::Allies;

	UPROPERTY(Replicated)
	bool bSpottedByEnemy = false;

	UPROPERTY(Replicated)
	TObjectPtr<ABreachableWall> Wall;

	/** Replicated once at arming; clients count it down themselves. */
	UPROPERTY(Replicated)
	float FuseRemaining = 5.f;

	FIntPoint WallCell = FIntPoint::ZeroValue;
	FIntPoint StandCell = FIntPoint::ZeroValue;
	bool bWallReinforcedAtPlant = false;
	TWeakObjectPtr<ASoldier> Placer;

	// Blast (copied from USoldierData at arming).
	float KillRadius = 1.f;
	float KillChance = 0.1f;
	float CloseStunDuration = 7.f;
	float StunRadius = 4.f;
	float StunDuration = 5.f;

	float StateTime = 0.f;
	float SpotCheckRemaining = 0.f;
	bool bArmed = false;
	static constexpr float SpotCheckInterval = 0.2f;
	static constexpr float ExplosionDuration = 0.6f;
	static constexpr float DudLingerTime = 4.f;
};
