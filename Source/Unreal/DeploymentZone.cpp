#include "DeploymentZone.h"

#include "MapZoneColors.h"
#include "EngineUtils.h"

ADeploymentZone::ADeploymentZone()
{
	SizeX = 8;
	SizeY = 8;
}

ADeploymentZone* ADeploymentZone::FindForTeam(const UObject* WorldContextObject, ESquadTeam InTeam)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ADeploymentZone> It(World); It; ++It)
	{
		if (It->Team == InTeam)
		{
			return *It;
		}
	}
	return nullptr;
}

FText ADeploymentZone::GetLabelText() const
{
	return Team == ESquadTeam::Allies ? NSLOCTEXT("Squad", "AlliedDZ", "ALLIED DZ") : NSLOCTEXT("Squad", "AxisDZ", "AXIS DZ");
}

FLinearColor ADeploymentZone::GetZoneColor() const
{
	return MapZoneColors::ForTeam(Team);
}
