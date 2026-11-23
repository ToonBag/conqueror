#pragma once

#include "CoreMinimal.h"
#include "SquadDebugTypes.generated.h"

/**
 * What the in-game debug menu (F6, non-shipping builds) asks the server to do. Sent with
 * ASquadPlayerController::ServerDebugAction; Param / Cell / Targets mean what each entry says.
 */
UENUM()
enum class ESquadDebugAction : uint8
{
	None,
	// Console variables (Param = new value). Also set on the client, where some of them are read.
	RevealAll,
	Bot,
	BotDebug,
	GodMode,
	VPWinTime,
	// World.
	/** Param = time dilation. */
	GameSpeed,
	/** Param = 1 pause, 0 resume. */
	Pause,
	BreachAllWalls,
	// Targets (the selection, or the soldier clicked with a click tool).
	/** Param = stress 0-100. */
	SetStress,
	Heal,
	Injure,
	/** Param = seconds. */
	Stun,
	Kill,
	Restock,
	/** Targets[0] goes to Cell, the others keep their offsets from it. */
	Teleport,
	// Every enemy of the requesting player's team.
	InjureEnemies,
	KillEnemies,
	/** Param = stress 0-100. */
	StressEnemies
};
