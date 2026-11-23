#pragma once

#include "CoreMinimal.h"
#include "LootTypes.h"

class AActor;
class ASoldier;

/**
 * Area looting and trading (GDD §5.5).
 *
 * Everything lootable within reach of a soldier is gathered into one list: dead soldiers' bodies (either
 * team) and dropped-item piles on the soldier's own cell or any of the 8 cells around it. From that list the
 * soldier can swap its rifle, take ammo / grenades / medkits, or drop its own items onto its cell for a
 * squad-mate to pick up. All changes happen on the server; clients only send FLootActions.
 */
namespace SquadLoot
{
	/** Something holding items within reach of the looter. */
	struct FSource
	{
		TWeakObjectPtr<AActor> Actor;
		FLootInventory* Inventory = nullptr;
		FIntPoint Cell = FIntPoint::ZeroValue;
		/** e.g. "BODY: SOLDIER 3", "ENEMY BODY 2", "DROPPED ITEMS". */
		FString Label;
		bool bOnLooterCell = false;
	};

	/** Items held by a body (dead ASoldier) or a pile; nullptr for anything else, including the living. */
	FLootInventory* GetInventory(AActor* Actor);

	/** True if Looter may loot or trade right now. Otherwise OutReason says why ("STUNNED", ...). */
	bool CanLoot(const ASoldier* Looter, FString* OutReason = nullptr);

	/** Bodies and piles on the looter's cell or next to it (diagonals included), own cell first. */
	TArray<FSource> GatherSources(ASoldier* Looter);

	/** Server: carry out one loot-menu action for Looter. Feedback goes on the soldier's status line. */
	bool Execute(ASoldier* Looter, const FLootAction& Action);

	/** Shift-click amount: one clip for ammo, otherwise one item. */
	int32 GetSingleCount(ELootItemType Item, const class UWeaponData* AmmoFor);

	/** Short display name of a rifle, e.g. "M1 Garand". */
	FString WeaponName(const class UWeaponData* Weapon);
}
