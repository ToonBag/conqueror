#pragma once

#include "CoreMinimal.h"
#include "GridZoneActor.h"
#include "SquadTypes.h"
#include "VictoryPoint.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * Victory Point (GDD §10.3): a capture zone of grid cells. It belongs to the last team that had soldiers in it
 * while the other team had none. With both teams inside it is contested and keeps its current owner; when it
 * empties it also keeps its owner. Starts neutral. The server decides ownership; clients see the replicated
 * result.
 *
 * Placeholder visuals: a border and letter on the ground and a flag pole in the middle; the border and flag take
 * the owner's colour (neutral when unowned), and the border turns amber while contested.
 */
UCLASS()
class UNREAL_API AVictoryPoint : public AGridZoneActor
{
	GENERATED_BODY()

public:
	AVictoryPoint();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Short name shown on the ground and in the UI, e.g. "A". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Victory Point")
	FText PointName = NSLOCTEXT("Squad", "VPDefaultName", "A");

	/**
	 * Provisional: whether stunned and injured soldiers count towards capturing and contesting. Off: only
	 * soldiers able to act hold ground. Dead soldiers never count.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Victory Point")
	bool bDownedSoldiersCount = false;

	/** Seconds between the server's checks of who is in the zone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Victory Point", meta = (ClampMin = "0.05"))
	float CheckInterval = 0.2f;

	UFUNCTION(BlueprintPure, Category = "Victory Point")
	bool IsOwned() const { return bOwned; }

	/** Only meaningful when IsOwned(). */
	UFUNCTION(BlueprintPure, Category = "Victory Point")
	ESquadTeam GetOwningTeam() const { return OwningTeam; }

	UFUNCTION(BlueprintPure, Category = "Victory Point")
	bool IsContested() const { return bContested; }

	/** True if Team owns the point. */
	UFUNCTION(BlueprintPure, Category = "Victory Point")
	bool IsOwnedBy(ESquadTeam Team) const { return bOwned && OwningTeam == Team; }

protected:
	virtual void BeginPlay() override;
	virtual FText GetLabelText() const override { return PointName; }
	virtual FLinearColor GetZoneColor() const override;
	virtual void BuildVisuals() override;

	UPROPERTY(VisibleAnywhere, Category = "Victory Point")
	TObjectPtr<UStaticMeshComponent> FlagPole;

	UPROPERTY(VisibleAnywhere, Category = "Victory Point")
	TObjectPtr<UStaticMeshComponent> Flag;

private:
	/** Server: count the soldiers of each team in the zone and update ownership. */
	void UpdateOwnership();

	void RefreshFlag();

	UFUNCTION()
	void OnRep_State();

	UPROPERTY(ReplicatedUsing = OnRep_State)
	bool bOwned = false;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	ESquadTeam OwningTeam = ESquadTeam::Allies;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	bool bContested = false;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlagMaterial;

	float TimeToNextCheck = 0.f;
};
