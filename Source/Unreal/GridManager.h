#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CoverData.h"
#include "GridManager.generated.h"

class ASoldier;
class UStaticMeshComponent;
class UMaterialInterface;

/** What a cover object puts on one grid cell. The cell can't be walked through (unless bPassable). */
struct FGridCoverCell
{
	ECoverType Type = ECoverType::None;
	bool bBlocksLineOfSight = false;
	/** The piece runs along world X (else along Y). Walls are thin slabs across the cell in this direction. */
	bool bRunsAlongX = true;
	/**
	 * Rubble of a breached wall (GDD §9.4): soldiers can walk onto / through the cell, and it still gives its cover
	 * type (low wall) to adjacent soldiers and blocks sight / grenades like that type. Counts as open ground for
	 * wall-end checks (corners, leaning out).
	 */
	bool bPassable = false;
	/**
	 * The cover is a solid block filling the whole cell (vehicles, wells, crate stacks), not a slab across it:
	 * grenades are stopped anywhere in the cell (below the type's height), and maps draw a full square.
	 */
	bool bFillsCell = false;
	TWeakObjectPtr<AActor> Owner;
};

/** The cover a target has against one attacker. */
struct FCoverResult
{
	ECoverType Type = ECoverType::None;
	/** Cell of the cover object providing it. */
	FIntPoint CoverCell = FIntPoint::ZeroValue;
	/** Percentage points taken off the attacker's hit chance (already reduced if the target isn't leaning). */
	float Reduction = 0.f;
	/** The target faces the cover and leans on it (full value). False: its back is to the cover. */
	bool bLeaning = true;
};

/**
 * A soldier leaning on the end cell of a full wall looks out past the wall's edge (GDD §9.2). It sees, and
 * can be seen from, the far side of the wall as if standing on PeekCell, the open cell beside the wall's end.
 */
struct FWallPeek
{
	/** The wall cell the soldier leans on. */
	FIntPoint WallCell = FIntPoint::ZeroValue;
	/** Open cell beside the wall's end, in the wall's row: where sight lines start. */
	FIntPoint PeekCell = FIntPoint::ZeroValue;
	/** From the soldier towards the wall (one step). */
	FIntPoint Toward = FIntPoint::ZeroValue;
	/** From the wall's end cell out past its edge (one step). */
	FIntPoint Edge = FIntPoint::ZeroValue;
	/**
	 * The wall carries on away from the soldier beyond WallCell (it stands at a building corner or the end of a
	 * wall running the way it faces): it leans out and looks down the wall's outer face instead of over it.
	 */
	bool bAlongWall = false;

	/**
	 * The area the soldier sees by leaning out: everything beyond the wall's row, from the open column just
	 * past the edge back along the wall - or, looking down a wall's face (bAlongWall), from that column outwards.
	 */
	bool Covers(FIntPoint Cell) const
	{
		const FIntPoint Rel = Cell - WallCell;
		const int32 Out = Rel.X * Edge.X + Rel.Y * Edge.Y;
		return Rel.X * Toward.X + Rel.Y * Toward.Y >= 1 && (bAlongWall ? Out >= 1 : Out <= 1);
	}
};

/**
 * Owns the flat ground and the movement grid. One cell = CellSize cm (100 = 1 metre).
 * The actor's location is the centre of the grid; the ground mesh is sized to match the grid.
 */
UCLASS()
class UNREAL_API AGridManager : public AActor
{
	GENERATED_BODY()

public:
	AGridManager();

	virtual void OnConstruction(const FTransform& Transform) override;

	/** Edge length of one grid cell, in cm. 100 = 1 metre. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "10"))
	float CellSize = 100.f;

	/** Number of cells along X. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1"))
	int32 GridSizeX = 30;

	/** Number of cells along Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1"))
	int32 GridSizeY = 30;

	/** Material for the ground. If it has "GridOrigin" (vector) and "CellSize" (scalar) parameters they are filled in. */
	UPROPERTY(EditAnywhere, Category = "Grid")
	TObjectPtr<UMaterialInterface> GroundMaterial;

	UPROPERTY(VisibleAnywhere, Category = "Grid")
	TObjectPtr<UStaticMeshComponent> Ground;

	/** Cover reductions and arc. Falls back to UCoverData class defaults (GDD §9.1) if unset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid|Cover")
	TObjectPtr<UCoverData> CoverData;

	/** Square drawn on the cell under the mouse cursor. */
	UPROPERTY(VisibleAnywhere, Category = "Grid")
	TObjectPtr<UStaticMeshComponent> HoverHighlight;

	/** World position of the centre of a cell, at ground height. */
	UFUNCTION(BlueprintPure, Category = "Grid")
	FVector CellToWorld(FIntPoint Cell) const;

	/** Cell containing a world position. Returns false if outside the grid. */
	UFUNCTION(BlueprintPure, Category = "Grid")
	bool WorldToCell(const FVector& WorldLocation, FIntPoint& OutCell) const;

	UFUNCTION(BlueprintPure, Category = "Grid")
	bool IsInBounds(FIntPoint Cell) const;

	/** True if a soldier (other than Ignore) is standing in or stepping into the cell. */
	bool IsCellOccupied(FIntPoint Cell, const ASoldier* Ignore = nullptr) const;

	/** True if a cover object (wall, hay bale, tree...) stands on the cell. */
	bool IsCellBlockedByCover(FIntPoint Cell) const
	{
		const FGridCoverCell* Cover = CoverCells.Find(Cell);
		return Cover && !Cover->bPassable; // Rubble (GDD §9.4) gives cover but can be walked over.
	}

	/** In bounds, no cover object and no soldier (other than Ignore). */
	bool IsCellPassable(FIntPoint Cell, const ASoldier* Ignore = nullptr) const;

	/** True if the cell is passable and not another soldier's destination. */
	bool IsCellFreeAsDestination(FIntPoint Cell, const ASoldier* Ignore = nullptr) const;

	/** 4-directional BFS path from Start to Goal (Start excluded, Goal included), routing around soldiers. */
	bool FindPath(FIntPoint Start, FIntPoint Goal, const ASoldier* Mover, TArray<FIntPoint>& OutPath) const;

	/** Nearest free cell to Desired (spiral search). */
	bool FindNearestFreeCell(FIntPoint Desired, const ASoldier* Ignore, FIntPoint& OutCell) const;

	// ---------------------------------------------------------------- Cover (GDD §9)

	void RegisterCoverCell(FIntPoint Cell, ECoverType Type, bool bBlocksLineOfSight, AActor* CoverOwner, bool bRunsAlongX = true, bool bPassable = false, bool bFillsCell = false);
	void UnregisterCover(const AActor* CoverOwner);

	/**
	 * Call after cover changes during play (a wall breached, GDD §9.4): bumps the cover revision and fires
	 * OnCoverChanged so anything caching cover / paths / sight (HUD previews, bots) can refresh.
	 */
	void NotifyCoverChanged();

	/** Increments whenever cover changes during play. */
	int32 GetCoverRevision() const { return CoverRevision; }

	FSimpleMulticastDelegate OnCoverChanged;

	const FGridCoverCell* GetCoverCell(FIntPoint Cell) const { return CoverCells.Find(Cell); }

	/** Every cell a cover object stands on (for the minimap). */
	const TMap<FIntPoint, FGridCoverCell>& GetCoverCells() const { return CoverCells; }

	const UCoverData* GetCoverData() const;

	/**
	 * Best cover a soldier on TargetCell has against fire from AttackerCell: a cover object on one of the
	 * four orthogonally adjacent cells counts if the attacker is within CoverArcHalfAngle of that side, and
	 * the attacker isn't touching that same cover from the other side. TargetYaw is the target's facing: cover
	 * it isn't facing (not leaning on) only gives part of its value. Pass no yaw to ignore facing.
	 */
	FCoverResult GetCoverAgainst(FIntPoint TargetCell, FIntPoint AttackerCell, TOptional<float> TargetYaw = {}) const;

	/** True if a soldier facing Yaw leans on the cover on the orthogonal neighbour Side of its cell. */
	bool IsLeaningToward(FIntPoint Side, float Yaw) const;

	/** Wall edges a soldier on Cell facing Yaw is leaning out past (usually none, at most two). */
	void GetWallPeeks(FIntPoint Cell, float Yaw, TArray<FWallPeek>& OutPeeks) const;

	/**
	 * True if the cover on Cell + Side is a cover piece in its own right (for the HUD overlays): hay bales, low walls,
	 * windows, trees, and a full wall only at a corner or wall end (open ground beside the wall cell along the face the
	 * soldier is against). The middle of a full wall's face blocks sight but isn't shown as cover. False if no cover.
	 */
	bool IsCoverPiece(FIntPoint Cell, FIntPoint Side) const;

	/** Direction (orthogonal neighbour offset) of the adjacent cover a soldier on Cell facing Yaw should lean on, if any. */
	bool FindCoverToLeanOn(FIntPoint Cell, float Yaw, FIntPoint& OutSide) const;

	/**
	 * GDD §9.3: true if the sight line from From's centre at FromHeight (cm) to To's centre at ToHeight
	 * passes no cover. Full walls (and trees) always block; hay bales and low walls block where the line
	 * runs below their top; windows only let it through between sill and top. A line running exactly
	 * through a grid corner is only blocked if both cells beside the corner block it.
	 */
	bool HasLineOfSight(FIntPoint From, FIntPoint To, float FromHeight, float ToHeight) const;

	/**
	 * GDD §7.1: true if cover on the line from BlastCell's centre to TargetCell's centre shields a soldier on
	 * TargetCell whose head is at TargetHeight (cm, its sight height) from a grenade going off on BlastCell. Full
	 * walls, wall corners and trees always shield. Hay bales, low walls and windows shield too, unless the cover cell
	 * touches TargetCell (diagonals included) and TargetHeight is above its top / the window sill: the soldier is
	 * peering over or through it, so standing and crouched soldiers there are caught and prone ones are not.
	 * A line exactly through a grid corner is only shielded if both cells beside the corner shield.
	 */
	bool IsBlastShielded(FIntPoint BlastCell, FIntPoint TargetCell, float TargetHeight) const;

	/**
	 * GDD §7: true if a grenade flying from Start to End along a parabola bulging ArcHeight (cm) above the
	 * straight line between them touches no cover (see UCoverData::BlocksGrenade). Heights are measured from
	 * the grid's ground.
	 */
	bool IsGrenadeArcClear(const FVector& Start, const FVector& End, float ArcHeight) const;

	virtual void Tick(float DeltaSeconds) override;

	void RegisterSoldier(ASoldier* Soldier);
	void UnregisterSoldier(ASoldier* Soldier);

	/** Every soldier on the grid (living and dead). Entries may be stale. */
	const TArray<TWeakObjectPtr<ASoldier>>& GetSoldiers() const { return Soldiers; }

	void SetHoveredCell(bool bVisible, FIntPoint Cell = FIntPoint::ZeroValue);

	/** The battlefield cell under the cursor (local player), if any. */
	bool GetHoveredCell(FIntPoint& OutCell) const { OutCell = HoveredCell; return bHoverVisible; }

	/** First grid manager in the world. */
	static AGridManager* Find(const UObject* WorldContextObject);

private:
	/**
	 * Walks the cells the segment between From's and To's centres crosses (not the two end cells) and returns false as
	 * soon as Blocks says one blocks. TIn / TOut (0..1 along the segment) are where the line enters and leaves the cell.
	 * Exactly through a grid corner, it is blocked only if both cells beside the corner block.
	 */
	bool IsGridLineClear(FIntPoint From, FIntPoint To, TFunctionRef<bool(FIntPoint Cell, double TIn, double TOut)> Blocks) const;

	/** World position of the grid's minimum corner. */
	FVector GetGridOrigin() const;

	/** Server: mark every soldier spotted / unspotted by the opposing team (GDD §9.3). */
	void UpdateTeamVision();

	TArray<TWeakObjectPtr<ASoldier>> Soldiers;

	bool bHoverVisible = false;
	FIntPoint HoveredCell = FIntPoint::ZeroValue;

	TMap<FIntPoint, FGridCoverCell> CoverCells;

	int32 CoverRevision = 0;
};
