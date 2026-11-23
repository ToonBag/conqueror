#include "EnemySoldier.h"

#include "WeaponData.h"
#include "UObject/ConstructorHelpers.h"

AEnemySoldier::AEnemySoldier()
{
	Team = ESquadTeam::Axis;
	SoldierColor = FLinearColor(0.55f, 0.12f, 0.1f);

	static ConstructorHelpers::FObjectFinder<UWeaponData> Gewehr43(TEXT("/Game/Data/DA_Gewehr43.DA_Gewehr43"));
	if (Gewehr43.Succeeded())
	{
		Weapon = Gewehr43.Object;
	}
}
