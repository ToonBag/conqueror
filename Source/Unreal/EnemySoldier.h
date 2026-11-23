#pragma once

#include "CoreMinimal.h"
#include "Soldier.h"
#include "EnemySoldier.generated.h"

/**
 * Axis soldier belonging to the opposing player. For now these are placed in the level as
 * targets: they take hits and grenades but nobody controls them (no networking/AI yet).
 */
UCLASS()
class UNREAL_API AEnemySoldier : public ASoldier
{
	GENERATED_BODY()

public:
	AEnemySoldier();
};
