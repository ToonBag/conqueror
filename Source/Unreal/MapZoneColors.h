#pragma once

#include "CoreMinimal.h"
#include "SquadTypes.h"

/** Placeholder colours for zones on the map (deployment zones, victory points). */
namespace MapZoneColors
{
	inline const FLinearColor Allies(0.15f, 0.40f, 1.0f);
	inline const FLinearColor Axis(0.85f, 0.12f, 0.08f);
	inline const FLinearColor Neutral(0.85f, 0.85f, 0.80f);
	inline const FLinearColor Contested(1.0f, 0.70f, 0.10f);

	inline FLinearColor ForTeam(ESquadTeam Team) { return Team == ESquadTeam::Allies ? Allies : Axis; }
}
