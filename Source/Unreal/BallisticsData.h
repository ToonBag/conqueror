#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BallisticsData.generated.h"

/**
 * Tuning for the experimental projectile combat model (GDD §16, squad.Ballistics 1). Every rifle round is a
 * server-simulated projectile that leaves the muzzle with a random angular error (spread) and stops on the
 * ground, on cover or on a soldier's hit shapes. Cover protects by physically hiding body parts.
 *
 * Starting values only: they are meant to be tuned from AI test matches (GDD §11.2).
 */
UCLASS(BlueprintType)
class UNREAL_API UBallisticsData : public UDataAsset
{
	GENERATED_BODY()

public:
	// ------------------------------------------------------------------ Flight

	/** Muzzle velocity (cm/s). 85,000 = 850 m/s (M1 Garand / Gewehr 43). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight", meta = (ClampMin = "1000", Units = "cm/s"))
	float MuzzleVelocity = 85000.f;

	/** A round that has hit nothing after this distance is dropped (beyond the 40 m firing range). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight", meta = (ClampMin = "100", Units = "cm"))
	float MaxRoundTravel = 6000.f;

	/** Chance (0-1) that a round passes through a hay bale cell instead of stopping in it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight", meta = (ClampMin = "0", ClampMax = "1"))
	float HayPassThroughChance = 0.7f;

	/** Radius of a tree trunk (cm); rounds above the canopy block height pass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight", meta = (ClampMin = "1", Units = "cm"))
	float TreeTrunkRadius = 22.f;

	// ------------------------------------------------------------------ Spread
	// One-sigma angular error = weapon SpreadMrad x posture x stress x injury, plus recoil.

	/** Braced: resting the rifle on low cover (hay, low wall, window sill). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0.1"))
	float BracedSpreadMultiplier = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0.1"))
	float CrouchedSpreadMultiplier = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0.1"))
	float ProneSpreadMultiplier = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0.1"))
	float InjuredSpreadMultiplier = 1.5f;

	/**
	 * Stress widens spread by (1 / tier AccuracyMultiplier) ^ this. With the GDD §8.1 tiers (1.0 / 0.95 / 0.75 / 0.5)
	 * and exponent 1 that is x1.0 / x1.05 / x1.33 / x2.0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0"))
	float StressSpreadExponent = 1.f;

	/** Recoil added per round fired (mrad). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0"))
	float RecoilPerShotMrad = 2.f;

	/** Recoil recovered per second (mrad/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0"))
	float RecoilRecoveryMradPerSecond = 8.f;

	/** Recoil never builds above this (mrad). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0"))
	float MaxRecoilMrad = 10.f;

	/** Samples used to estimate the hit chance shown in the HUD and used by target selection and bots. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "4", ClampMax = "128"))
	int32 EstimateSamples = 32;

	// ------------------------------------------------------------------ Posture (cm)

	/** Braced: head centre this far above the top of the cover rested on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Posture", meta = (Units = "cm"))
	float BracedHeadAboveCover = 18.f;

	/** Braced: muzzle this far above the top of the cover rested on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Posture", meta = (Units = "cm"))
	float BracedMuzzleAboveCover = 12.f;

	/** Braced: the body leans this far towards the cover from the cell centre. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Posture", meta = (Units = "cm"))
	float BracedLeanForward = 25.f;

	/** Braced and reloading: the whole body drops this far, below the top of the cover. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Posture", meta = (Units = "cm"))
	float DuckDrop = 45.f;

	/** Leaning out past a wall edge: how far the head moves sideways past the soldier's cell centre. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Posture", meta = (Units = "cm"))
	float PeekSideways = 62.f;

	// ------------------------------------------------------------------ Stress (GDD §8.2 reinterpreted)

	/** A round passing within this distance (cm) of a soldier's chest stresses it, like an incoming shot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress", meta = (ClampMin = "0", Units = "cm"))
	float NearMissRadius = 150.f;

	// ------------------------------------------------------------------ Friendly fire

	/** A round hitting a friendly soldier injures it (never kills). False: friendly rounds pass through. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Friendly Fire")
	bool bFriendlyFireInjures = true;
};
