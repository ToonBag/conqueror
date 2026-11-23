// Debug-menu hooks on ASoldier (ASquadPlayerController's debug menu, SquadPlayerControllerDebug.cpp). Server only.

#include "Soldier.h"

#include "GridManager.h"
#include "SoldierData.h"
#include "WeaponData.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarGodMode(
		TEXT("squad.GodMode"), 0,
		TEXT("1: soldiers commanded by a player can't be injured, killed or stunned by rifle fire, grenades or C4 (debug). Bot / test squads are unaffected."),
		ECVF_Cheat);
}

bool ASoldier::IsDebugInvulnerable() const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return !bDebugIgnoreGodMode && (bDebugForceInvulnerable || (OwningPlayer.IsValid() && CVarGodMode.GetValueOnGameThread() != 0));
#endif
}

void ASoldier::DebugHeal()
{
	if (bDead)
	{
		return;
	}
	StunRemaining = 0.f;
	if (bInjured)
	{
		RecoverFromInjury(); // Back to Active, prone (as after a medkit, GDD §5.2).
	}
	SetStress(0.f);
	ApplyPostureVisuals();
	ShowMessage(TEXT("DEBUG HEAL"));
}

void ASoldier::DebugRestock()
{
	if (bDead)
	{
		return;
	}
	// GDD §4.2 loadout, as in BeginPlay, but keeping the rifle actually held (it may have been swapped by looting).
	const USoldierData* D = GetSoldierData();
	if (Inventory.Weapons.Num() == 0)
	{
		Inventory.Weapons.AddDefaulted_GetRef().Weapon = Weapon ? Weapon.Get() : GetMutableDefault<UWeaponData>();
	}
	FLootWeapon& Held = Inventory.Weapons[0];
	if (!Held.Weapon)
	{
		Held.Weapon = Weapon ? Weapon.Get() : GetMutableDefault<UWeaponData>();
	}
	const int32 Rounds = Held.Weapon->GetStartingRounds(D->StartingAmmo);
	Held.LoadedRounds = FMath::Min(Held.Weapon->MagazineCapacity, Rounds);
	Inventory.RemoveAmmo(Held.Weapon, MAX_int32);
	Inventory.AddAmmo(Held.Weapon, Rounds - Held.LoadedRounds);
	Inventory.Grenades = D->StartingGrenades;
	Inventory.Medkits = D->StartingMedkits;
	InitBuildables();
	bReloading = false;
	ReloadRemaining = 0.f;
	ShowMessage(TEXT("RESTOCKED"));
}

bool ASoldier::DebugTeleportTo(FIntPoint Cell)
{
	if (bDead || !Grid || !Grid->IsCellFreeAsDestination(Cell, this))
	{
		return false;
	}
	if (ASoldier* Dragger = DraggedBy.Get())
	{
		Dragger->StopDragging();
	}
	StopDragging();
	CancelMedkit();
	CancelBuild();
	ClearAttack();
	bHasFacingOrder = false;
	CancelGrenadeThrow();
	CurrentCell = Cell;
	bHasPreviousCell = false; // Teleported: no cell to step back to.
	StopMoving(); // Path cleared, destination = the new cell.
	SetActorLocation(Grid->CellToWorld(Cell));
	return true;
}

void ASoldier::DebugInjure()
{
	TGuardValue<bool> Bypass(bDebugIgnoreGodMode, true);
	ReceiveHit(30.f, FKillCause()); // Beyond the close-range kill distance: injures (kills if already injured).
}

void ASoldier::DebugStun(float Seconds)
{
	TGuardValue<bool> Bypass(bDebugIgnoreGodMode, true);
	ApplyStun(Seconds);
}

void ASoldier::DebugKill()
{
	Die(FKillCause());
}
