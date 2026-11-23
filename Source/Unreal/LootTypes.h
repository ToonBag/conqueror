#pragma once

#include "CoreMinimal.h"
#include "LootTypes.generated.h"

class AActor;
class UWeaponData;

/** A rifle and the rounds loaded in it (GDD §5.5). The loaded rounds travel with the weapon when it changes hands. */
USTRUCT(BlueprintType)
struct FLootWeapon
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<UWeaponData> Weapon;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 LoadedRounds = 0;
};

/**
 * Loose rounds for one rifle. Ammo is specific to the weapon that fires it (M1 Garand .30-06 vs Gewehr 43
 * 7.92 mm), so it is keyed by the weapon data asset. A soldier can carry rounds for a rifle it doesn't hold;
 * they can be traded but not loaded.
 */
USTRUCT(BlueprintType)
struct FLootAmmo
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<UWeaponData> Weapon;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 Rounds = 0;
};

/**
 * Everything a soldier carries, or everything lying in a dropped-item pile (GDD §4.2, §5.5).
 * A soldier always holds exactly one weapon (Weapons[0], the one it fires); a pile can hold any number.
 */
USTRUCT(BlueprintType)
struct UNREAL_API FLootInventory
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	TArray<FLootWeapon> Weapons;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	TArray<FLootAmmo> Ammo;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 Grenades = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 Medkits = 0;

	/** Loose rounds for this weapon. */
	int32 GetAmmo(const UWeaponData* Weapon) const;

	void AddAmmo(UWeaponData* Weapon, int32 Rounds);

	/** Removes up to MaxRounds rounds for this weapon and returns how many were removed. Empty stacks are dropped. */
	int32 RemoveAmmo(const UWeaponData* Weapon, int32 MaxRounds);

	/** Nothing left: no weapon, no rounds, no grenades, no medkits. */
	bool IsEmpty() const;
};

/** Kinds of item shown in the loot / trade menu. */
UENUM(BlueprintType)
enum class ELootItemType : uint8
{
	Weapon,
	Ammo,
	Grenade,
	Medkit
};

/** What a loot-menu button does. */
UENUM(BlueprintType)
enum class ELootActionType : uint8
{
	/** Move items from a nearby body or pile to the looter. */
	Take,
	/** Exchange the looter's rifle (with its loaded rounds) for a nearby one. */
	SwapWeapon,
	/** Put the looter's items down on its own cell. */
	Drop,
	/** Take every round, grenade and medkit from everything in reach (weapons are left alone). */
	TakeAll
};

/** One loot-menu request. On a networked build this is what the client sends to the server. */
USTRUCT(BlueprintType)
struct FLootAction
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Loot")
	ELootActionType Action = ELootActionType::Take;

	/** Body (dead ASoldier) or ALootPile the item comes from. Unused for Drop and TakeAll. */
	UPROPERTY(BlueprintReadWrite, Category = "Loot")
	TObjectPtr<AActor> Source;

	UPROPERTY(BlueprintReadWrite, Category = "Loot")
	ELootItemType Item = ELootItemType::Ammo;

	/** Weapon: index into the source's Weapons. Ammo: the rifle the rounds are for. */
	UPROPERTY(BlueprintReadWrite, Category = "Loot")
	int32 WeaponIndex = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Loot")
	TObjectPtr<UWeaponData> AmmoFor;

	/** How many to move; clamped to what is there. */
	UPROPERTY(BlueprintReadWrite, Category = "Loot")
	int32 Count = 0;
};
