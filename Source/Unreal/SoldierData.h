#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SquadTypes.h"
#include "BuildableTypes.h"
#include "SoldierData.generated.h"

/**
 * Combat penalties for one stress tier (GDD §8.1). 1 = no change. The amounts are provisional:
 * the GDD names the affected stats but not by how much.
 */
USTRUCT(BlueprintType)
struct FStressTierEffects
{
	GENERATED_BODY()

	FStressTierEffects() = default;
	FStressTierEffects(float InFireInterval, float InReloadTime, float InAimTime, float InAccuracy)
		: FireIntervalMultiplier(InFireInterval), ReloadTimeMultiplier(InReloadTime)
		, AimTimeMultiplier(InAimTime), AccuracyMultiplier(InAccuracy)
	{
	}

	/** Time between shots is multiplied by this (2 = half the fire rate). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress", meta = (ClampMin = "1"))
	float FireIntervalMultiplier = 1.f;

	/** Reload time is multiplied by this ("fumbled reloads"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress", meta = (ClampMin = "1"))
	float ReloadTimeMultiplier = 1.f;

	/** Aim time before the first shot (and after a reload) is multiplied by this ("aim stability"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress", meta = (ClampMin = "1"))
	float AimTimeMultiplier = 1.f;

	/** The weapon's range-band accuracy is multiplied by this before cover is subtracted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress", meta = (ClampMin = "0", ClampMax = "1"))
	float AccuracyMultiplier = 1.f;
};

/** Per-soldier tuning shared by the Standard Squad (GDD §4.2, §5, §7, §10.2). Distances in metres. */
UCLASS(BlueprintType)
class UNREAL_API USoldierData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loadout", meta = (ClampMin = "0"))
	int32 StartingAmmo = 70;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loadout", meta = (ClampMin = "0"))
	int32 StartingGrenades = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loadout", meta = (ClampMin = "0"))
	int32 StartingMedkits = 1;

	/**
	 * GDD §5.5: bodies and dropped items this many cells away or closer (diagonals count as one) can be looted.
	 * 1 = the soldier's own cell and the 8 around it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0"))
	int32 LootReachCells = 1;

	/** Fraction of standing speed while crouched. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0", ClampMax = "1"))
	float CrouchedSpeedMultiplier = 0.66f;

	/** Fraction of standing speed while prone (also used for crawling when Injured). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0", ClampMax = "1"))
	float ProneSpeedMultiplier = 0.33f;

	/**
	 * GDD §4.3 squad cohesion: the two furthest-apart living soldiers of a squad may be at most this many metres
	 * apart (cell centre to cell centre), judged on where each soldier is heading. 0 turns the limit off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Squad", meta = (ClampMin = "0"))
	float SquadCohesionLimit = 30.f;

	/** GDD §5.4: a soldier must be within this distance (centre to centre) of a downed ally to start dragging it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0"))
	float DragStartRange = 1.f;

	/**
	 * GDD §5.2: seconds a soldier spends applying a medkit to an injured ally. It can't shoot meanwhile, and the
	 * medkit is only used up when the treatment finishes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Medkit", meta = (ClampMin = "0", Units = "s"))
	float MedkitChannelTime = 7.f;

	/**
	 * GDD §5.2: a medkit reaches an injured ally this many cells away or closer (diagonals count as one).
	 * 1 = the medic's own cell and the 8 around it, the same reach as looting.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Medkit", meta = (ClampMin = "1"))
	int32 MedkitReachCells = 1;

	/** A rifle hit from this distance or closer kills; further away it injures. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0"))
	float CloseRangeKillDistance = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade", meta = (ClampMin = "0"))
	float GrenadeKillRadius = 1.f;

	/** Chance (0-1) that a soldier within GrenadeKillRadius is killed; otherwise stunned for GrenadeCloseStunDuration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade", meta = (ClampMin = "0", ClampMax = "1"))
	float GrenadeKillChance = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade", meta = (ClampMin = "0"))
	float GrenadeCloseStunDuration = 7.f;

	/** GDD §7.1: the blast is 5 m across, so soldiers within 2.5 m of the grenade are stunned. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade", meta = (ClampMin = "0"))
	float GrenadeStunRadius = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade", meta = (ClampMin = "0"))
	float GrenadeStunDuration = 5.f;

	/** Seconds from the bounce to detonation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade", meta = (ClampMin = "0"))
	float GrenadeFuseAfterBounce = 0.5f;

	/** GDD §7: furthest a grenade can be thrown (metres), measured from the cell the soldier steps back to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade", meta = (ClampMin = "1"))
	float GrenadeMaxThrowRange = 10.f;

	/**
	 * GDD §7: wind-up (throwing action) between the end of the step back and the grenade leaving the hand. The pin
	 * is pulled during the step back; a soldier that can't step back pulls it during the wind-up.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade", meta = (ClampMin = "0", Units = "s"))
	float GrenadeThrowDelay = 0.5f;

	/**
	 * GDD §7 (provisional): after the grenade leaves the hand the soldier finishes the throwing motion before it
	 * shoulders its rifle again. Firing stays paused (and the soldier keeps facing the throw) for this long.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade", meta = (ClampMin = "0", Units = "s"))
	float GrenadeFollowThroughTime = 0.4f;

	/** Provisional (not in GDD): minimum seconds between throws by one soldier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade", meta = (ClampMin = "0"))
	float GrenadeThrowCooldown = 1.f;

	/** Provisional (not in GDD): horizontal flight speed in metres/second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade", meta = (ClampMin = "1"))
	float GrenadeFlightSpeed = 12.f;

	// ---------------------------------------------------------------- Buildables (GDD §4.2, §7.2, §13.3)

	/**
	 * GDD §13.3: what each soldier carries to build on walls, in Build sub-wheel order: kind, name, count per soldier
	 * and placement (channel) time. Standard Squad: 1 C4 and 1 wall reinforcement, 7 s each.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buildables")
	TArray<FBuildableSpec> Buildables = {
		FBuildableSpec(EBuildableKind::C4, TEXT("C4"), 1, 7.f),
		FBuildableSpec(EBuildableKind::WallReinforcement, TEXT("Reinforce"), 1, 7.f)
	};

	/** GDD §7.2 (provisional): seconds from the C4 being placed to it detonating. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buildables|C4", meta = (ClampMin = "0", Units = "s"))
	float C4FuseTime = 5.f;

	/**
	 * GDD §7.2: C4 blast radii (metres), centred on the charge. Kill chance and stun durations are the grenade's
	 * (GDD §7.1), but the radii are C4's own (1 m / 4 m) and don't follow the grenade's.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buildables|C4", meta = (ClampMin = "0"))
	float C4KillRadius = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buildables|C4", meta = (ClampMin = "0"))
	float C4StunRadius = 4.f;

	const FBuildableSpec* FindBuildable(EBuildableKind Kind) const
	{
		return Buildables.FindByPredicate([Kind](const FBuildableSpec& Spec) { return Spec.Kind == Kind; });
	}

	// ---------------------------------------------------------------- Stress (GDD §8)

	/** Full stress. A soldier at this value is Shocked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress", meta = (ClampMin = "1"))
	float MaxStress = 100.f;

	/** Stress at or above this is the Minor tier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress", meta = (ClampMin = "0"))
	float MinorStressThreshold = 33.f;

	/** Stress at or above this is the Major tier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress", meta = (ClampMin = "0"))
	float MajorStressThreshold = 66.f;

	/** Stress added by each round fired at the soldier, hit or miss, before cover. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress", meta = (ClampMin = "0"))
	float StressPerIncomingShot = 5.f;

	/**
	 * GDD §9: cover reduces stress from incoming fire. A shot's stress is multiplied by
	 * (1 - cover reduction / 100 x this), using the same cover (and back-turned halving) as the hit chance.
	 * Provisional: 1 means a low wall (50) halves the stress; 0 means cover doesn't reduce stress.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress", meta = (ClampMin = "0", ClampMax = "1"))
	float CoverStressReductionScale = 1.f;

	/** Provisional: stress added to a soldier when a squad-mate behind it fires past it at a target in front. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress|Cross-fire", meta = (ClampMin = "0"))
	float CrossfireStressPerShot = 2.5f;

	/** Provisional: friendly soldiers this close (metres) to the line of fire, between shooter and target, take cross-fire stress. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress|Cross-fire", meta = (ClampMin = "0"))
	float CrossfireRadius = 1.5f;

	/** Provisional: soldiers less than this far (metres, along the line of fire) in front of the shooter are beside it, not in front. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress|Cross-fire", meta = (ClampMin = "0"))
	float CrossfireMinDistanceFromShooter = 1.f;

	/** Stress lost per second while nothing is adding stress. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress|Decay", meta = (ClampMin = "0"))
	float StressDecayPerSecond = 1.f;

	/** Provisional: seconds after the last stress gain before decay starts ("no stress-gain triggers active"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress|Decay", meta = (ClampMin = "0"))
	float StressDecayDelay = 1.f;

	/** Provisional amounts for GDD §8.1 "minor reductions to fire rate, fumbled reloads, and aim stability". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress|Effects")
	FStressTierEffects MinorStressEffects = FStressTierEffects(1.15f, 1.25f, 1.25f, 0.95f);

	/** Provisional amounts for GDD §8.1 "severe reductions to fire rate, reloads, and accuracy". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress|Effects")
	FStressTierEffects MajorStressEffects = FStressTierEffects(1.5f, 1.5f, 1.5f, 0.75f);

	/** Provisional amounts for GDD §8.1 "maximum performance penalties". Shocked soldiers also cannot move. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress|Effects")
	FStressTierEffects ShockedStressEffects = FStressTierEffects(2.f, 2.f, 2.f, 0.5f);

	EStressTier GetStressTier(float Stress) const
	{
		if (Stress >= MaxStress - KINDA_SMALL_NUMBER) { return EStressTier::Shocked; }
		if (Stress >= MajorStressThreshold)           { return EStressTier::Major; }
		if (Stress >= MinorStressThreshold)           { return EStressTier::Minor; }
		return EStressTier::None;
	}

	const FStressTierEffects& GetStressEffects(EStressTier Tier) const
	{
		static const FStressTierEffects NoEffects;
		switch (Tier)
		{
		case EStressTier::Minor:   return MinorStressEffects;
		case EStressTier::Major:   return MajorStressEffects;
		case EStressTier::Shocked: return ShockedStressEffects;
		default:                   return NoEffects;
		}
	}
};
