#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LootTypes.h"
#include "LootPile.generated.h"

class AGridManager;
class UStaticMeshComponent;

/**
 * Items dropped on one grid cell (GDD §5.5). Created the first time something is put down on a cell, shared
 * by everything later dropped there, and removed once emptied. Persists until collected or the match ends.
 * Placeholder look: a small olive crate, with a rifle lying across it while it holds a weapon.
 */
UCLASS()
class UNREAL_API ALootPile : public AActor
{
	GENERATED_BODY()

public:
	ALootPile();

	UPROPERTY(ReplicatedUsing = OnRep_Contents, VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	FLootInventory Contents;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Loot")
	FIntPoint GetCell() const { return Cell; }

	/** Server: the pile on Cell, creating an empty one if there is none. */
	static ALootPile* FindOrSpawnAt(AGridManager& Grid, FIntPoint Cell);

	/** Server: call after changing Contents. Updates the look, and destroys the pile once it is empty. */
	void OnContentsChanged();

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Contents();

	UPROPERTY(VisibleAnywhere, Category = "Loot")
	TObjectPtr<UStaticMeshComponent> Crate;

	UPROPERTY(VisibleAnywhere, Category = "Loot")
	TObjectPtr<UStaticMeshComponent> Rifle;

private:
	UPROPERTY(Replicated)
	FIntPoint Cell = FIntPoint::ZeroValue;

	/** Tint the crate and rifle (server and clients). */
	void ApplyLook();
};
