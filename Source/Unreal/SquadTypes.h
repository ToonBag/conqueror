#pragma once

#include "CoreMinimal.h"
#include "SquadTypes.generated.h"

/** Which side a soldier fights for. */
UENUM(BlueprintType)
enum class ESquadTeam : uint8
{
	Allies,
	Axis
};

/** Body posture (GDD §10.2). Affects movement speed. */
UENUM(BlueprintType)
enum class ESoldierStance : uint8
{
	Standing,
	Crouched,
	Prone
};

/** Health state (GDD §5.1 / §5.2). */
UENUM(BlueprintType)
enum class ESoldierHealth : uint8
{
	Active,
	Stunned,
	Injured,
	Dead
};

/** Stress tier from a soldier's 0-100 stress (GDD §8.1). */
UENUM(BlueprintType)
enum class EStressTier : uint8
{
	/** Below 33: normal operation. */
	None,
	/** 33-66: minor penalties to fire rate, reloads and aim. */
	Minor,
	/** 66-99: severe penalties to fire rate, reloads and accuracy. */
	Major,
	/** 100: cannot move, maximum penalties. */
	Shocked
};
