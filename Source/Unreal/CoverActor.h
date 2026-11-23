#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CoverData.h"
#include "CoverActor.generated.h"

class AGridManager;
class UInstancedStaticMeshComponent;
class UStaticMesh;

/** One grid cell of a cover object, relative to the object's anchor cell (before rotation). */
USTRUCT(BlueprintType)
struct FCoverCellSpec
{
	GENERATED_BODY()

	/** Cell offset in the actor's local frame: X runs along the object, Y across it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover")
	FIntPoint Offset = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover")
	ECoverType Type = ECoverType::LowWall;

	/** Full-height pieces (walls) stop shots passing through this cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover")
	bool bBlocksLineOfSight = false;

	/** Solid block filling the cell (props, vehicles) rather than a slab across it: stops grenades anywhere in the cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover")
	bool bFillsCell = false;
};

/**
 * Something soldiers can take cover behind (GDD §9). Occupies one or more grid cells, which can't be
 * walked through; a soldier on an orthogonally adjacent cell gets the cell's cover type against fire
 * from that side. Snaps to the grid and to 90° turns when placed.
 *
 * Each cell is drawn with a low-poly mesh from /Game/Environment (built by SourceArt/Environment/
 * build_ww2_cover.py), picked from the per-type variant lists below. The subclasses below are ready-made
 * pieces; new kinds of cover can subclass this and override GenerateCells / AddCellVisual.
 */
UCLASS(Abstract)
class UNREAL_API ACoverActor : public AActor
{
	GENERATED_BODY()

public:
	ACoverActor();

	virtual void OnConstruction(const FTransform& Transform) override;

	/** Number of cells along the actor's local X axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover", meta = (ClampMin = "1", ClampMax = "32"))
	int32 Length = 1;

	/** Cover type given by every cell (subclasses with mixed cells, e.g. windows, ignore it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover")
	ECoverType CoverType = ECoverType::LowWall;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover")
	bool bBlocksLineOfSight = false;

	/**
	 * Lengthens (+) or shortens (-) the slab of the first cell (local -X end), in cm, so wall ends can meet
	 * neatly at corners. Visual and physical only: the cells the piece claims on the grid don't change.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Corners", meta = (ClampMin = "-100", ClampMax = "100"))
	float StartExtension = 0.f;

	/** As StartExtension, for the last cell (local +X end). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Corners", meta = (ClampMin = "-100", ClampMax = "100"))
	float EndExtension = 0.f;

	/** Grid cells this object covers, in world grid coordinates. */
	UFUNCTION(BlueprintPure, Category = "Cover")
	TArray<FIntPoint> GetOccupiedCells() const;

protected:
	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Fills the cell layout. Default: Length cells of CoverType in a row along local X, centred on the actor. */
	virtual void GenerateCells(TArray<FCoverCellSpec>& OutCells) const;

	/**
	 * Adds the mesh for one cell. Location is the cell centre in actor space at ground level.
	 * ExtendBack / ExtendFront lengthen (+) or shorten (-) wall slabs at the cell's local -X / +X side, in cm.
	 */
	virtual void AddCellVisual(const FCoverCellSpec& Cell, const FVector& Location, float ExtendBack = 0.f, float ExtendFront = 0.f);

	/** Called once after the per-cell visuals, for pieces drawn as one mesh over all their cells. Default: nothing. */
	virtual void AddPieceVisual(const TArray<FCoverCellSpec>& Cells) {}

	/** One of Variants, chosen by Seed (unloadable / empty entries skipped). Null if none can be loaded. */
	static UStaticMesh* PickMesh(const TArray<TSoftObjectPtr<UStaticMesh>>& Variants, uint32 Seed);

	/** Stable per-world-cell random seed for the cell at LocalLocation (actor space), so variants don't change between builds. */
	uint32 GetCellSeed(const FVector& LocalLocation, uint32 Salt) const;

	/**
	 * Instanced set for Mesh carrying Tag, created on first use. bSolid: blocks physics / stray rounds like cover
	 * (else no collision). bConstructionScript: made by OnConstruction, so the editor rebuilds it with the actor.
	 */
	UInstancedStaticMeshComponent* GetMeshSet(UStaticMesh* Mesh, FName Tag, bool bSolid, bool bConstructionScript);

	/** Tag on the instanced sets that draw the intact cover cells. */
	static const FName CoverVisualTag;

	/** Sets made by GetMeshSet since the last construction (not saved: find saved ones by tag). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> MeshSets;

	// Legacy placeholder shape sets. Always empty; kept only so levels saved before the art pass load cleanly.
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> HayShapes;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> StoneShapes;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> WallShapes;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> WoodShapes;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> FoliageShapes;

public:
	// ---------------------------------------------------------------- Visuals (cosmetic only; never affect play)
	// Each cell picks one variant from its grid position, so a piece looks the same every time it is built.
	// Gameplay heights / thicknesses come from UCoverData and the grid, never from these meshes.

	/** Hay bale cells (Soft). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Visuals")
	TArray<TSoftObjectPtr<UStaticMesh>> HayMeshes;

	/** Low wall cells. Any 1 m x 0.4 m x 0.9 m piece works (stone, brick, sandbags). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Visuals")
	TArray<TSoftObjectPtr<UStaticMesh>> LowWallMeshes;

	/** Full wall cells (1 m x 0.3 m x 2.6 m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Visuals")
	TArray<TSoftObjectPtr<UStaticMesh>> WallMeshes;

	/** Window cells (opening between the 0.9 m sill and the 2.0 m top). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Visuals")
	TArray<TSoftObjectPtr<UStaticMesh>> WindowMeshes;

	/** Tree cells. Each tree is also turned to a random heading and scaled by up to ±TreeScaleJitter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Visuals")
	TArray<TSoftObjectPtr<UStaticMesh>> TreeMeshes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Visuals", meta = (ClampMin = "0", ClampMax = "0.5"))
	float TreeScaleJitter = 0.1f;

	/** Turn wall, low wall and hay cells end for end at random (per cell) so repeated pieces don't line up identically. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Visuals")
	bool bRandomFlip = true;

private:
	void SnapToGrid(AGridManager* Grid);
	FIntPoint RotateOffset(FIntPoint Offset) const;

	UPROPERTY(Transient)
	TObjectPtr<AGridManager> RegisteredGrid;
};

/** Soft cover: a row of hay bales (30%). */
UCLASS()
class UNREAL_API AHayBaleCover : public ACoverActor
{
	GENERATED_BODY()
public:
	AHayBaleCover();
};

/** Low wall / half wall (50%). */
UCLASS()
class UNREAL_API ALowWallCover : public ACoverActor
{
	GENERATED_BODY()
public:
	ALowWallCover();
};

/**
 * Full-height wall. Blocks line of sight; a soldier beside it is only exposed to shots that get
 * round its edge, and gets wall corner / edge cover (60%) against those.
 */
UCLASS()
class UNREAL_API AWallCover : public ACoverActor
{
	GENERATED_BODY()
public:
	AWallCover();
};

/** Building wall with windows: solid wall cells either side of each window cell (window = 70%). */
UCLASS()
class UNREAL_API AWindowWallCover : public ACoverActor
{
	GENERATED_BODY()
public:
	AWindowWallCover();

	/** Every Nth cell along the wall is a window (counting from the second cell). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover", meta = (ClampMin = "2"))
	int32 WindowSpacing = 2;

protected:
	virtual void GenerateCells(TArray<FCoverCellSpec>& OutCells) const override;
};

/** A tree (provisional 40%). */
UCLASS()
class UNREAL_API ATreeCover : public ACoverActor
{
	GENERATED_BODY()
public:
	ATreeCover();
};

/**
 * A cover object drawn as one mesh over a solid block of Length x Width cells (wells, crate stacks, vehicles; GDD §9.5).
 * Every cell gives CoverType and fills the whole cell (grenades can't drop between its cells). The mesh is centred
 * on the block; its pivot must be the centre of its footprint at ground level. Use the presets below, or set the
 * size, cover type and mesh yourself for a custom prop.
 */
UCLASS()
class UNREAL_API APropCover : public ACoverActor
{
	GENERATED_BODY()
public:
	APropCover();

	/** Cells across the piece (local Y). Length is the cells along it (local X). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover", meta = (ClampMin = "1", ClampMax = "8"))
	int32 Width = 1;

	/** One variant is picked per piece (from its grid position). Square 1-cell props also get a random quarter turn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Visuals")
	TArray<TSoftObjectPtr<UStaticMesh>> PropMeshes;

protected:
	virtual void GenerateCells(TArray<FCoverCellSpec>& OutCells) const override;
	virtual void AddCellVisual(const FCoverCellSpec& Cell, const FVector& Location, float ExtendBack = 0.f, float ExtendFront = 0.f) override {}
	virtual void AddPieceVisual(const TArray<FCoverCellSpec>& Cells) override;
};

/** Stone well, 1 cell, low wall cover. */
UCLASS()
class UNREAL_API AWellCover : public APropCover
{
	GENERATED_BODY()
public:
	AWellCover();
};

/** Stone water trough, 2 x 1 cells, low wall cover. */
UCLASS()
class UNREAL_API AWaterTroughCover : public APropCover
{
	GENERATED_BODY()
public:
	AWaterTroughCover();
};

/** Stacked crates, 1 cell, low wall cover. */
UCLASS()
class UNREAL_API ACrateStackCover : public APropCover
{
	GENERATED_BODY()
public:
	ACrateStackCover();
};

/** A group of barrels, 1 cell, low wall cover. */
UCLASS()
class UNREAL_API ABarrelCover : public APropCover
{
	GENERATED_BODY()
public:
	ABarrelCover();
};

/** Stacked firewood, 1 cell, low wall cover. */
UCLASS()
class UNREAL_API AWoodpileCover : public APropCover
{
	GENERATED_BODY()
public:
	AWoodpileCover();
};

/** Farm cart loaded with hay, 3 x 2 cells, soft cover. */
UCLASS()
class UNREAL_API AHayCartCover : public APropCover
{
	GENERATED_BODY()
public:
	AHayCartCover();
};

/** Abandoned open-top light car, 4 x 2 cells, low wall cover. */
UCLASS()
class UNREAL_API ACarCover : public APropCover
{
	GENERATED_BODY()
public:
	ACarCover();
};

/** Abandoned cargo truck, 6 x 2 cells: full cover that blocks line of sight, like a wall. */
UCLASS()
class UNREAL_API ATruckCover : public APropCover
{
	GENERATED_BODY()
public:
	ATruckCover();
};

/** Knocked-out tank hulk, 6 x 3 cells: full cover that blocks line of sight, like a wall. */
UCLASS()
class UNREAL_API ATankWreckCover : public APropCover
{
	GENERATED_BODY()
public:
	ATankWreckCover();
};

/** A row of sandbags: a low wall (50%) drawn with the sandbag mesh. */
UCLASS()
class UNREAL_API ASandbagCover : public ALowWallCover
{
	GENERATED_BODY()
public:
	ASandbagCover();
};
