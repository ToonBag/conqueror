#pragma once

#include "CoreMinimal.h"
#include "GridZoneActor.h"
#include "SquadTypes.h"
#include "DeploymentZone.generated.h"

/**
 * Deployment Zone (GDD §10.3): the block of cells, 8x8 by default, at a team's edge of the map where its
 * squad starts the match. Each map has one per team.
 */
UCLASS()
class UNREAL_API ADeploymentZone : public AGridZoneActor
{
	GENERATED_BODY()

public:
	ADeploymentZone();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
	ESquadTeam Team = ESquadTeam::Allies;

	/** The deployment zone of a team in this world, if any. */
	static ADeploymentZone* FindForTeam(const UObject* WorldContextObject, ESquadTeam Team);

protected:
	virtual FText GetLabelText() const override;
	virtual FLinearColor GetZoneColor() const override;
};
