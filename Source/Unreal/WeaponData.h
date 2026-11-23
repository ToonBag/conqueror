#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponData.generated.h"

/**
 * Rifle stats (GDD §6). Class defaults are the M1 Garand; the Gewehr 43 is a data asset
 * with its own values. Range bands are from GDD §5.3.
 */
UCLASS(BlueprintType)
class UNREAL_API UWeaponData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FText DisplayName = NSLOCTEXT("Weapon", "M1Garand", "M1 Garand");

	/** Model held by the soldier (origin at the grip, muzzle along +X, with a "Muzzle" socket). Cosmetic. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Visuals")
	TObjectPtr<class UStaticMesh> Mesh;

	/** Played at the muzzle for every shot. Cosmetic. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Audio")
	TObjectPtr<class USoundBase> FireSound;

	/** Played at the soldier when a reload starts. Cosmetic. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Audio")
	TObjectPtr<class USoundBase> ReloadSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "1"))
	float RoundsPerMinute = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "1"))
	int32 MagazineCapacity = 8;

	/** Hit chance in percent (0-100) within ShortRangeMax. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Accuracy", meta = (ClampMin = "0", ClampMax = "100"))
	float ShortRangeAccuracy = 85.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Accuracy", meta = (ClampMin = "0", ClampMax = "100"))
	float MidRangeAccuracy = 65.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Accuracy", meta = (ClampMin = "0", ClampMax = "100"))
	float LongRangeAccuracy = 35.f;

	/** Upper edge of the short range band, in metres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Range", meta = (ClampMin = "0"))
	float ShortRangeMax = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Range", meta = (ClampMin = "0"))
	float MidRangeMax = 20.f;

	/** Maximum firing range, in metres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Range", meta = (ClampMin = "0"))
	float LongRangeMax = 40.f;

	/** Not used yet: its meaning is not defined (GDD §8.2). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	float ShortRangeSuppression = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0"))
	float AimTime = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0"))
	float ReloadTime = 2.5f;

	/**
	 * Rounds a soldier starting with this weapon carries in all, loaded plus spare. 0 uses the soldier data's
	 * StartingAmmo (the Standard Squad's 70 rifle rounds, GDD §4.2).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0"))
	int32 StartingRounds = 0;

	// ---------------------------------------------------------------- Machine guns (GDD §6.1)

	/**
	 * Rounds per trigger pull. 1 for rifles. Above 1 the weapon fires bursts: the rounds of a burst go out at
	 * RoundsPerMinute (the cyclic rate), each with its own hit roll and stress, then BurstPause follows.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Machine Gun", meta = (ClampMin = "1"))
	int32 RoundsPerBurst = 1;

	/** Seconds between the last round of one burst and the first of the next (scaled by stress). 0 for rifles. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Machine Gun", meta = (ClampMin = "0"))
	float BurstPause = 0.f;

	/**
	 * Seconds a soldier must stand still, facing where it will fire, before it can fire (setting up the bipod).
	 * Moving tears the weapon down again. 0: no set-up (rifles).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Machine Gun", meta = (ClampMin = "0"))
	float SetupTime = 0.f;

	/**
	 * Width in degrees of the arc a set-up weapon can fire across, centred on the way the soldier faced when the
	 * set-up finished. A target or facing order outside it tears the weapon down. Only used when SetupTime > 0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Machine Gun", meta = (ClampMin = "1", ClampMax = "360"))
	float SetupArcDegrees = 90.f;

	/**
	 * Cosmetic: where the bipod feet touch the ground, in the mesh's own space (cm, origin at the grip). Set up, the
	 * gun pivots on the gunner's grip until the feet (or the barrel, see BarrelUndersideHeight) rest on whatever is
	 * beneath them: the ground when prone, the top of the cover in front otherwise. X = 0 disables the rest pose.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Machine Gun|Visuals")
	FVector BipodFoot = FVector::ZeroVector;

	/** Cosmetic: height of the barrel's underside in mesh space (cm); the barrel can rest on a wall top instead of the feet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Machine Gun|Visuals")
	float BarrelUndersideHeight = 3.f;

	/** Cosmetic: how far the gun kicks back along its barrel per round while set up (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Machine Gun|Visuals", meta = (ClampMin = "0"))
	float SetupRecoil = 1.5f;

	bool NeedsSetup() const { return SetupTime > 0.f; }

	/** Rounds this weapon's carrier starts with, loaded plus spare. */
	int32 GetStartingRounds(int32 SoldierDefault) const { return StartingRounds > 0 ? StartingRounds : SoldierDefault; }

	/** Hit chance in percent for a target this far away, or a negative value if out of range. */
	float GetAccuracyAtRange(float Metres) const
	{
		if (Metres <= ShortRangeMax) { return ShortRangeAccuracy; }
		if (Metres <= MidRangeMax)   { return MidRangeAccuracy; }
		if (Metres <= LongRangeMax)  { return LongRangeAccuracy; }
		return -1.f;
	}

	float GetShotInterval() const { return 60.f / FMath::Max(RoundsPerMinute, 1.f); }
};
