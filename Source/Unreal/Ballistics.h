#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BallisticsTypes.h"
#include "Soldier.h"
#include "Ballistics.generated.h"

class AGridManager;
class UBallisticsData;

/** A rifle round in flight (server only). */
struct FBallisticRound
{
	TWeakObjectPtr<ASoldier> Shooter;
	TWeakObjectPtr<ASoldier> Target;
	ESquadTeam ShooterTeam = ESquadTeam::Allies;
	FVector Muzzle = FVector::ZeroVector;
	FVector Position = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	float Travelled = 0.f;
	/** The firing weapon's StressScale (GDD §6.2), taken when the round was fired. */
	float StressScale = 1.f;
	/** The hit-chance estimate when the round was fired (stats, kill feed). */
	FHitChanceInfo Chance;
	/** Soldiers already stressed by this round passing them. */
	TArray<TWeakObjectPtr<ASoldier>, TInlineAllocator<4>> Stressed;
	/** Hay cells this round has already rolled to pass. */
	TArray<FIntPoint, TInlineAllocator<2>> HayPassed;
};

/**
 * Projectile combat (GDD §16), the default combat model (squad.Ballistics 1 on the server; 0 for hit-chance rolls). Every rifle round
 * is simulated here as a fast projectile, stepped each server tick, and stops on the ground, on cover (the grid's
 * cover cells, using the same slab shapes and heights as grenades) or on a soldier's analytic hit shapes.
 *
 * The same straight-line trace estimates hit chances for the HUD, target choice and bots on any machine.
 */
UCLASS()
class UNREAL_API UBallisticsSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UBallisticsSubsystem* Get(const UObject* WorldContextObject);

	/** True if this world uses the projectile model: the squad.Ballistics cvar on the server, replicated to clients. */
	static bool IsEnabled(const UObject* WorldContextObject);

	static const UBallisticsData* GetData();

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	/** Server: launch one round. Direction must be normalised. */
	void FireRound(ASoldier* Shooter, ASoldier* Target, const FVector& Muzzle, const FVector& Direction, const FHitChanceInfo& Chance);

	/**
	 * First thing the segment A -> B hits. Rng set: hay bales are rolled (and remembered in HayPassed so a round
	 * rolls each bale once); null: hay is passed and PassWeight carries the chance.
	 */
	FBallisticTrace Trace(const FVector& A, const FVector& B, const ASoldier* Ignore, FRandomStream* Rng = nullptr,
		TArray<FIntPoint, TInlineAllocator<2>>* HayPassed = nullptr) const;

	/** Rounds in flight (debug). */
	int32 GetRoundsInFlight() const { return Rounds.Num(); }

private:
	AGridManager* GetGrid() const;
	/** Earliest T in [TIn, TOut] at which the segment is inside the cover piece in Cell, or -1. */
	float CoverBlockT(const AGridManager& Grid, FIntPoint Cell, const FVector& A, const FVector& Seg, float TIn, float TOut) const;
	void StressNearMisses(FBallisticRound& Round, const FVector& From, const FVector& To);

	TArray<FBallisticRound> Rounds;
	FRandomStream Rng;
	bool bSeeded = false;
	mutable TWeakObjectPtr<AGridManager> CachedGrid;
};
