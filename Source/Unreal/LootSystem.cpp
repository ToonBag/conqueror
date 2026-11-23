#include "LootSystem.h"

#include "GridManager.h"
#include "LootPile.h"
#include "Soldier.h"
#include "SoldierData.h"
#include "WeaponData.h"
#include "EngineUtils.h"

// ------------------------------------------------------------------------------------------------ FLootInventory

int32 FLootInventory::GetAmmo(const UWeaponData* Weapon) const
{
	for (const FLootAmmo& Stack : Ammo)
	{
		if (Stack.Weapon == Weapon)
		{
			return Stack.Rounds;
		}
	}
	return 0;
}

void FLootInventory::AddAmmo(UWeaponData* Weapon, int32 Rounds)
{
	if (!Weapon || Rounds <= 0)
	{
		return;
	}
	for (FLootAmmo& Stack : Ammo)
	{
		if (Stack.Weapon == Weapon)
		{
			Stack.Rounds += Rounds;
			return;
		}
	}
	FLootAmmo& Stack = Ammo.AddDefaulted_GetRef();
	Stack.Weapon = Weapon;
	Stack.Rounds = Rounds;
}

int32 FLootInventory::RemoveAmmo(const UWeaponData* Weapon, int32 MaxRounds)
{
	for (int32 i = 0; i < Ammo.Num(); ++i)
	{
		if (Ammo[i].Weapon == Weapon)
		{
			const int32 Removed = FMath::Clamp(MaxRounds, 0, Ammo[i].Rounds);
			Ammo[i].Rounds -= Removed;
			if (Ammo[i].Rounds <= 0)
			{
				Ammo.RemoveAt(i);
			}
			return Removed;
		}
	}
	return 0;
}

bool FLootInventory::IsEmpty() const
{
	return Weapons.Num() == 0 && Ammo.Num() == 0 && Grenades <= 0 && Medkits <= 0;
}

// ------------------------------------------------------------------------------------------------ Helpers

namespace
{
	bool GetSourceCell(const AActor* Actor, FIntPoint& OutCell)
	{
		if (const ASoldier* Body = Cast<ASoldier>(Actor))
		{
			OutCell = Body->GetCurrentCell();
			return true;
		}
		if (const ALootPile* Pile = Cast<ALootPile>(Actor))
		{
			OutCell = Pile->GetCell();
			return true;
		}
		return false;
	}

	bool IsInReach(const ASoldier& Looter, FIntPoint Cell)
	{
		// Own cell or any of the 8 around it (Chebyshev distance), per GDD §5.5 "adjacent or occupied".
		const FIntPoint Delta = Cell - Looter.GetCurrentCell();
		return FMath::Max(FMath::Abs(Delta.X), FMath::Abs(Delta.Y)) <= Looter.GetSoldierData()->LootReachCells;
	}

	/** Moves rounds / grenades / medkits. Count <= 0 moves everything. Returns how many moved. */
	int32 MoveItems(FLootInventory& From, FLootInventory& To, ELootItemType Item, UWeaponData* AmmoFor, int32 Count)
	{
		const int32 Want = Count > 0 ? Count : MAX_int32;
		switch (Item)
		{
		case ELootItemType::Ammo:
		{
			const int32 Moved = From.RemoveAmmo(AmmoFor, Want);
			To.AddAmmo(AmmoFor, Moved);
			return Moved;
		}
		case ELootItemType::Grenade:
		{
			const int32 Moved = FMath::Min(Want, From.Grenades);
			From.Grenades -= Moved;
			To.Grenades += Moved;
			return Moved;
		}
		case ELootItemType::Medkit:
		{
			const int32 Moved = FMath::Min(Want, From.Medkits);
			From.Medkits -= Moved;
			To.Medkits += Moved;
			return Moved;
		}
		default:
			return 0;
		}
	}

	void RefreshSource(AActor* Actor)
	{
		if (ALootPile* Pile = Cast<ALootPile>(Actor))
		{
			Pile->OnContentsChanged();
		}
	}

	FString DescribeItems(ELootItemType Item, const UWeaponData* AmmoFor, int32 Count)
	{
		switch (Item)
		{
		case ELootItemType::Ammo:    return FString::Printf(TEXT("%d %s RDS"), Count, *SquadLoot::WeaponName(AmmoFor).ToUpper());
		case ELootItemType::Grenade: return FString::Printf(TEXT("%d GRENADE%s"), Count, Count == 1 ? TEXT("") : TEXT("S"));
		case ELootItemType::Medkit:  return FString::Printf(TEXT("%d MEDKIT%s"), Count, Count == 1 ? TEXT("") : TEXT("S"));
		default:                     return FString();
		}
	}
}

// ------------------------------------------------------------------------------------------------ SquadLoot

FString SquadLoot::WeaponName(const UWeaponData* Weapon)
{
	return Weapon ? Weapon->DisplayName.ToString() : FString(TEXT("Rifle"));
}

int32 SquadLoot::GetSingleCount(ELootItemType Item, const UWeaponData* AmmoFor)
{
	return Item == ELootItemType::Ammo && AmmoFor ? AmmoFor->MagazineCapacity : 1;
}

FLootInventory* SquadLoot::GetInventory(AActor* Actor)
{
	if (ASoldier* Body = Cast<ASoldier>(Actor))
	{
		// Only the dead can be looted; living soldiers keep their kit.
		return Body->IsDead() ? &Body->GetLootInventory() : nullptr;
	}
	if (ALootPile* Pile = Cast<ALootPile>(Actor))
	{
		return IsValid(Pile) ? &Pile->Contents : nullptr;
	}
	return nullptr;
}

bool SquadLoot::CanLoot(const ASoldier* Looter, FString* OutReason)
{
	auto Fail = [OutReason](const TCHAR* Reason)
	{
		if (OutReason)
		{
			*OutReason = Reason;
		}
		return false;
	};
	if (!Looter)
	{
		return Fail(TEXT("NO SOLDIER SELECTED"));
	}
	// GDD §5.1: only Active soldiers trade.
	switch (Looter->GetHealthState())
	{
	case ESoldierHealth::Dead:    return Fail(TEXT("DEAD"));
	case ESoldierHealth::Stunned: return Fail(TEXT("STUNNED"));
	case ESoldierHealth::Injured: return Fail(TEXT("INJURED"));
	default: break;
	}
	if (Looter->IsDragging())
	{
		return Fail(TEXT("DRAGGING"));
	}
	if (Looter->IsBeingDragged())
	{
		return Fail(TEXT("BEING DRAGGED"));
	}
	return true;
}

TArray<SquadLoot::FSource> SquadLoot::GatherSources(ASoldier* Looter)
{
	TArray<FSource> Sources;
	if (!Looter || !Looter->GetWorld())
	{
		return Sources;
	}
	UWorld* World = Looter->GetWorld();

	auto Consider = [&](AActor* Actor, const FString& Label)
	{
		FIntPoint Cell;
		FLootInventory* Inventory = GetInventory(Actor);
		if (Actor == Looter || !Inventory || !GetSourceCell(Actor, Cell) || !IsInReach(*Looter, Cell))
		{
			return;
		}
		FSource& Source = Sources.AddDefaulted_GetRef();
		Source.Actor = Actor;
		Source.Inventory = Inventory;
		Source.Cell = Cell;
		Source.Label = Label;
		Source.bOnLooterCell = Cell == Looter->GetCurrentCell();
	};

	for (TActorIterator<ASoldier> It(World); It; ++It)
	{
		if (It->IsDead())
		{
			Consider(*It, It->Team == Looter->Team
				? FString::Printf(TEXT("BODY: SOLDIER %d"), It->GetSquadNumber())
				: FString::Printf(TEXT("ENEMY BODY %d"), It->GetSquadNumber()));
		}
	}
	for (TActorIterator<ALootPile> It(World); It; ++It)
	{
		Consider(*It, TEXT("DROPPED ITEMS"));
	}

	// Own cell first, then a stable order so rows don't jump around between frames.
	Sources.StableSort([](const FSource& A, const FSource& B)
	{
		if (A.bOnLooterCell != B.bOnLooterCell)
		{
			return A.bOnLooterCell;
		}
		return A.Cell.X != B.Cell.X ? A.Cell.X < B.Cell.X : A.Cell.Y < B.Cell.Y;
	});
	return Sources;
}

bool SquadLoot::Execute(ASoldier* Looter, const FLootAction& Action)
{
	FString Reason;
	if (!CanLoot(Looter, &Reason))
	{
		if (Looter)
		{
			Looter->ShowMessage(Reason);
		}
		return false;
	}
	FLootInventory& Mine = Looter->GetLootInventory();

	switch (Action.Action)
	{
	case ELootActionType::TakeAll:
	{
		int32 Rounds = 0, Grenades = 0, Medkits = 0;
		for (const FSource& Source : GatherSources(Looter))
		{
			FLootInventory& Theirs = *Source.Inventory;
			const TArray<FLootAmmo> Stacks = Theirs.Ammo; // Copy: MoveItems removes emptied stacks.
			for (const FLootAmmo& Stack : Stacks)
			{
				Rounds += MoveItems(Theirs, Mine, ELootItemType::Ammo, Stack.Weapon, 0);
			}
			Grenades += MoveItems(Theirs, Mine, ELootItemType::Grenade, nullptr, 0);
			Medkits += MoveItems(Theirs, Mine, ELootItemType::Medkit, nullptr, 0);
			RefreshSource(Source.Actor.Get());
		}
		if (Rounds + Grenades + Medkits == 0)
		{
			Looter->ShowMessage(TEXT("NOTHING TO TAKE"));
			return false;
		}
		TArray<FString> Parts;
		if (Rounds > 0)   { Parts.Add(FString::Printf(TEXT("+%d RDS"), Rounds)); }
		if (Grenades > 0) { Parts.Add(FString::Printf(TEXT("+%d GREN"), Grenades)); }
		if (Medkits > 0)  { Parts.Add(FString::Printf(TEXT("+%d MED"), Medkits)); }
		Looter->ShowMessage(FString::Join(Parts, TEXT(" ")));
		return true;
	}

	case ELootActionType::Drop:
	{
		// A soldier always keeps one rifle; it changes hands only by swapping.
		AGridManager* Grid = AGridManager::Find(Looter);
		if (Action.Item == ELootItemType::Weapon || !Grid)
		{
			return false;
		}
		ALootPile* Pile = ALootPile::FindOrSpawnAt(*Grid, Looter->GetCurrentCell());
		if (!Pile)
		{
			return false;
		}
		const int32 Moved = MoveItems(Mine, Pile->Contents, Action.Item, Action.AmmoFor, Action.Count);
		Pile->OnContentsChanged(); // Removes the pile again if nothing went in.
		if (Moved <= 0)
		{
			return false;
		}
		Looter->ShowMessage(TEXT("DROPPED ") + DescribeItems(Action.Item, Action.AmmoFor, Moved));
		return true;
	}

	case ELootActionType::Take:
	case ELootActionType::SwapWeapon:
	{
		AActor* SourceActor = Action.Source.Get();
		FLootInventory* Theirs = GetInventory(SourceActor);
		FIntPoint Cell;
		if (!Theirs || SourceActor == Looter || !GetSourceCell(SourceActor, Cell))
		{
			return false;
		}
		if (!IsInReach(*Looter, Cell))
		{
			Looter->ShowMessage(TEXT("OUT OF REACH"));
			return false;
		}

		if (Action.Action == ELootActionType::SwapWeapon || Action.Item == ELootItemType::Weapon)
		{
			if (!Theirs->Weapons.IsValidIndex(Action.WeaponIndex))
			{
				return false;
			}
			// Each rifle keeps the rounds loaded in it.
			if (Mine.Weapons.Num() == 0)
			{
				Mine.Weapons.Add(Theirs->Weapons[Action.WeaponIndex]);
				Theirs->Weapons.RemoveAt(Action.WeaponIndex);
			}
			else
			{
				Swap(Mine.Weapons[0], Theirs->Weapons[Action.WeaponIndex]);
			}
			Looter->NotifyWeaponChanged();
			Looter->ShowMessage(TEXT("TOOK ") + WeaponName(Mine.Weapons[0].Weapon).ToUpper());
		}
		else
		{
			const int32 Moved = MoveItems(*Theirs, Mine, Action.Item, Action.AmmoFor, Action.Count);
			if (Moved <= 0)
			{
				return false;
			}
			Looter->ShowMessage(TEXT("+") + DescribeItems(Action.Item, Action.AmmoFor, Moved));
		}
		RefreshSource(SourceActor);
		return true;
	}
	}
	return false;
}
