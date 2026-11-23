#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GridZoneActor.generated.h"

class AGridManager;
class UInstancedStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;

/**
 * A rectangular block of grid cells marked on the ground (GDD §10): deployment zones and victory points.
 * The actor's location is the centre of the zone; it snaps so the zone lines up with whole cells (an odd
 * size centres on a cell, an even size on a cell corner). Zones don't block anything.
 *
 * Placeholder visuals: a border drawn just inside the zone's outer cells and a label lying on the ground.
 */
UCLASS(Abstract)
class UNREAL_API AGridZoneActor : public AActor
{
	GENERATED_BODY()

public:
	AGridZoneActor();

	virtual void OnConstruction(const FTransform& Transform) override;

	/** Width of the zone in cells (along world X). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone", meta = (ClampMin = "1", ClampMax = "64"))
	int32 SizeX = 8;

	/** Depth of the zone in cells (along world Y). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone", meta = (ClampMin = "1", ClampMax = "64"))
	int32 SizeY = 8;

	/** Lowest (min X, min Y) cell of the zone. */
	UFUNCTION(BlueprintPure, Category = "Zone")
	FIntPoint GetMinCell() const;

	UFUNCTION(BlueprintPure, Category = "Zone")
	bool ContainsCell(FIntPoint Cell) const;

	/** Every cell in the zone. */
	UFUNCTION(BlueprintPure, Category = "Zone")
	TArray<FIntPoint> GetCells() const;

protected:
	/** Text shown on the ground label. */
	virtual FText GetLabelText() const { return FText::GetEmpty(); }

	/** Colour of the border and label. */
	virtual FLinearColor GetZoneColor() const { return FLinearColor::White; }

	/** Re-tints the border and label (call when the colour changes at runtime). */
	void RefreshColor();

	/** Rebuilds the border and label. */
	virtual void BuildVisuals();

	/**
	 * False on a dedicated server: the visuals are cosmetic, and the server can load this actor without its text
	 * render component (a null Label crashed the server on map load).
	 */
	bool HasVisuals() const { return !IsRunningDedicatedServer() && Border && Label; }

	UPROPERTY(VisibleAnywhere, Category = "Zone")
	TObjectPtr<UInstancedStaticMeshComponent> Border;

	UPROPERTY(VisibleAnywhere, Category = "Zone")
	TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BorderMaterial;

private:
	void SnapToGrid(AGridManager* Grid);
};
