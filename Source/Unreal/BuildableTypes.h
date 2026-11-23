#pragma once

#include "CoreMinimal.h"
#include "BuildableTypes.generated.h"

/**
 * What a buildable does when its placement channel finishes (GDD §13.3). The list of buildables a soldier carries
 * is data (USoldierData::Buildables); each entry names one of these effects. New buildables add a value here plus
 * the code for its effect in ASoldier::FinishBuild.
 */
UENUM(BlueprintType)
enum class EBuildableKind : uint8
{
	/** Breaching charge stuck to a breachable wall's face: arms, then breaches the wall and blasts soldiers (GDD §7.2). */
	C4,
	/** Bracing fitted to a breachable wall: it can no longer be breached, and C4 still on its fuse becomes a dud. */
	WallReinforcement
};

/** One entry of the Build sub-wheel and the soldier's buildable loadout (GDD §4.2, §13.3). */
USTRUCT(BlueprintType)
struct FBuildableSpec
{
	GENERATED_BODY()

	FBuildableSpec() = default;
	FBuildableSpec(EBuildableKind InKind, const FString& InName, int32 InCount, float InPlaceTime)
		: Kind(InKind), DisplayName(InName), StartingCount(InCount), PlaceTime(InPlaceTime)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buildable")
	EBuildableKind Kind = EBuildableKind::C4;

	/** Name on the Build sub-wheel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buildable")
	FString DisplayName;

	/** How many each soldier starts with. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buildable", meta = (ClampMin = "0"))
	int32 StartingCount = 1;

	/** Seconds of channelling (kneeling at the wall) to place it. The item is only used up when it finishes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buildable", meta = (ClampMin = "0", Units = "s"))
	float PlaceTime = 7.f;
};

namespace SquadBuild
{
	inline const TCHAR* KindTag(EBuildableKind Kind)
	{
		return Kind == EBuildableKind::C4 ? TEXT("C4") : TEXT("REINFORCE");
	}
}
