#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SquadTypes.h"
#include "CoverData.generated.h"

/** Kind of cover a grid cell provides (GDD §9.1). */
UENUM(BlueprintType)
enum class ECoverType : uint8
{
	None,
	/** Hay bales. */
	Soft,
	/** Low wall / half wall. */
	LowWall,
	/** Full-height wall. Blocks line of sight; a soldier peeking past its edge gets corner cover. */
	WallCorner,
	/** Building window: a full wall you can see and shoot through. */
	Window,
	/** Tree trunk. Provisional value (not in GDD §9.1 yet). */
	Tree
};

/** How a cover reduction is applied to the shooter's hit chance. */
UENUM(BlueprintType)
enum class ECoverReductionMode : uint8
{
	/** GDD §9.1 "flat percentages": hit chance minus the reduction, in percentage points (65% - 50 = 15%). */
	Flat,
	/** Alternative for balancing tests: hit chance scaled by (1 - reduction) (65% x 0.5 = 32.5%). */
	Multiplicative
};

/** Cover tuning (GDD §9). Class defaults match the GDD; an asset assigned to AGridManager::CoverData overrides them. */
UCLASS(BlueprintType)
class UNREAL_API UCoverData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Hay bales. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Reduction", meta = (ClampMin = "0", ClampMax = "100"))
	float SoftCoverReduction = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Reduction", meta = (ClampMin = "0", ClampMax = "100"))
	float LowWallReduction = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Reduction", meta = (ClampMin = "0", ClampMax = "100"))
	float WallCornerReduction = 60.f;

	/** A shooter in a window is fairly exposed: less than a low wall. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Reduction", meta = (ClampMin = "0", ClampMax = "100"))
	float WindowReduction = 40.f;

	/** Provisional (not in GDD §9.1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Reduction", meta = (ClampMin = "0", ClampMax = "100"))
	float TreeReduction = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover")
	ECoverReductionMode ReductionMode = ECoverReductionMode::Flat;

	/**
	 * Cover in an orthogonally adjacent cell protects against fire arriving within this many degrees
	 * of the direction to that cell. Fire from further round (flank or rear) ignores it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover", meta = (ClampMin = "0", ClampMax = "90"))
	float CoverArcHalfAngle = 60.f;

	/**
	 * A soldier leans on (is "in") adjacent cover while facing within this many degrees of it. Facing the
	 * corner of a wall (45 degrees) counts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover", meta = (ClampMin = "0", ClampMax = "180"))
	float LeanFacingHalfAngle = 75.f;

	/** Cover the soldier has its back to (not leaning on it) gives only this fraction of its reduction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover", meta = (ClampMin = "0", ClampMax = "1"))
	float NotLeaningCoverMultiplier = 0.5f;

	// ---------------------------------------------------------------- Line of sight (GDD §9.3)
	// Sight lines run cell centre to cell centre, from the viewer's sight height to the target's.
	// Cover is solid: a line that passes through a cover cell below the cover's top is blocked.

	/** Height (cm) a standing soldier sees from and can be seen at. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line of Sight", meta = (ClampMin = "1", Units = "cm"))
	float StandingSightHeight = 160.f;

	/** Crouched sight height (cm). Must clear low walls and hay bales so crouched soldiers see over them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line of Sight", meta = (ClampMin = "1", Units = "cm"))
	float CrouchedSightHeight = 110.f;

	/** Prone sight height (cm). Below half-height cover, so prone soldiers hide behind it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line of Sight", meta = (ClampMin = "1", Units = "cm"))
	float ProneSightHeight = 40.f;

	/** Top of a hay bale (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line of Sight", meta = (ClampMin = "0", Units = "cm"))
	float HayBaleHeight = 75.f;

	/** Top of a low wall (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line of Sight", meta = (ClampMin = "0", Units = "cm"))
	float LowWallHeight = 90.f;

	/** A window cell can only be seen through between its sill and its top (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line of Sight", meta = (ClampMin = "0", Units = "cm"))
	float WindowSillHeight = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line of Sight", meta = (ClampMin = "0", Units = "cm"))
	float WindowTopHeight = 200.f;

	/**
	 * Trees block sight like a full wall. Off by default (GDD §9.3, v0.70): a soldier leaning on a tree peers round
	 * it, so nobody can hide behind one; the trunk still gives cover against fire from in front.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line of Sight")
	bool bTreesBlockLineOfSight = false;

	/** Furthest a soldier can see (metres, cell centre to cell centre). Matches the rifles' Long range max (GDD §5.3, §9.3). 0 = unlimited. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line of Sight", meta = (ClampMin = "0", Units = "m"))
	float MaxSightRange = 40.f;

	// ---------------------------------------------------------------- Grenades (GDD §7)

	/** Grenades are stopped by a tree cell below this height (cm): trunk plus canopy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenades", meta = (ClampMin = "0", Units = "cm"))
	float TreeGrenadeBlockHeight = 420.f;

	/**
	 * Half the thickness (cm, for 1 m cells) of the slab a cover piece occupies across its cell, for grenade
	 * collisions. Matches the cover meshes (SourceArt/Environment); trees fill the whole cell (canopy).
	 */
	static float GetGrenadeSlabHalfThickness(ECoverType Type)
	{
		switch (Type)
		{
		case ECoverType::Soft:       return 42.f; // Two bales side by side.
		case ECoverType::LowWall:    return 20.f;
		case ECoverType::WallCorner: return 15.f;
		case ECoverType::Window:     return 15.f;
		default:                     return 50.f;
		}
	}

	/**
	 * True if a grenade at Height (cm above ground) inside a cell of this cover type would hit it.
	 * Full walls (and the wall around a window) stop grenades at any height: they can't be thrown through or
	 * over. Hay bales and low walls can be thrown over; windows only through the opening.
	 */
	bool BlocksGrenade(ECoverType Type, bool bFullHeight, float Height) const
	{
		if (bFullHeight)
		{
			return true;
		}
		switch (Type)
		{
		case ECoverType::Soft:       return Height < HayBaleHeight;
		case ECoverType::LowWall:    return Height < LowWallHeight;
		case ECoverType::WallCorner: return true;
		case ECoverType::Window:     return Height < WindowSillHeight || Height > WindowTopHeight;
		case ECoverType::Tree:       return Height < TreeGrenadeBlockHeight;
		default:                     return false;
		}
	}

	float GetSightHeight(ESoldierStance Stance) const
	{
		switch (Stance)
		{
		case ESoldierStance::Crouched: return CrouchedSightHeight;
		case ESoldierStance::Prone:    return ProneSightHeight;
		default:                       return StandingSightHeight;
		}
	}

	/**
	 * True if a sight line crossing a cell of this cover type is blocked, given the line's height (cm) where
	 * it enters and leaves the cell. bFullHeight marks wall cells flagged as blocking line of sight.
	 */
	bool BlocksSight(ECoverType Type, bool bFullHeight, float HeightIn, float HeightOut) const
	{
		if (bFullHeight)
		{
			return true;
		}
		const float Low = FMath::Min(HeightIn, HeightOut);
		const float High = FMath::Max(HeightIn, HeightOut);
		switch (Type)
		{
		case ECoverType::Soft:       return Low < HayBaleHeight;
		case ECoverType::LowWall:    return Low < LowWallHeight;
		case ECoverType::WallCorner: return true;
		case ECoverType::Window:     return Low < WindowSillHeight || High > WindowTopHeight;
		case ECoverType::Tree:       return bTreesBlockLineOfSight;
		default:                     return false;
		}
	}

	/**
	 * Top (cm) of cover a soldier standing right against it can look over, or -1 for cover that can't be
	 * looked over (full walls, windows, trees).
	 */
	float GetSeeOverHeight(ECoverType Type) const
	{
		switch (Type)
		{
		case ECoverType::Soft:    return HayBaleHeight;
		case ECoverType::LowWall: return LowWallHeight;
		default:                  return -1.f;
		}
	}

	/** Reduction in percentage points for a cover type. */
	float GetReduction(ECoverType Type) const
	{
		switch (Type)
		{
		case ECoverType::Soft:       return SoftCoverReduction;
		case ECoverType::LowWall:    return LowWallReduction;
		case ECoverType::WallCorner: return WallCornerReduction;
		case ECoverType::Window:     return WindowReduction;
		case ECoverType::Tree:       return TreeReduction;
		default:                     return 0.f;
		}
	}

	/** Hit chance (percent) after cover of the given type. */
	float ApplyCover(float HitChance, ECoverType Type) const
	{
		return ApplyReduction(HitChance, GetReduction(Type));
	}

	/** Hit chance (percent) after a reduction of Reduction (percentage points in Flat mode). */
	float ApplyReduction(float HitChance, float Reduction) const
	{
		const float Result = ReductionMode == ECoverReductionMode::Flat
			? HitChance - Reduction
			: HitChance * (1.f - Reduction / 100.f);
		return FMath::Clamp(Result, 0.f, 100.f);
	}

	static const TCHAR* GetCoverName(ECoverType Type)
	{
		switch (Type)
		{
		case ECoverType::Soft:       return TEXT("HAY");
		case ECoverType::LowWall:    return TEXT("LOW WALL");
		case ECoverType::WallCorner: return TEXT("WALL EDGE");
		case ECoverType::Window:     return TEXT("WINDOW");
		case ECoverType::Tree:       return TEXT("TREE");
		default:                     return TEXT("EXPOSED");
		}
	}
};
