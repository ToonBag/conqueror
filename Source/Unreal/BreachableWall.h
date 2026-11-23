#pragma once

#include "CoreMinimal.h"
#include "CoverActor.h"
#include "BreachableWall.generated.h"

class UInstancedStaticMeshComponent;

/**
 * A weakened full wall that C4 can destroy (GDD §9.4). Behaves like a normal full wall (blocks movement,
 * LOS, grenades; gives full-wall / corner cover) until breached, then its cells become walkable rubble
 * that gives low cover, and debris scatters around it.
 *
 * SHARED CONTRACT between the destructible-terrain work (owns this class's implementation, the grid /
 * cover / LOS / nav updates and rubble) and the buildables work (C4 + wall reinforcement, which only
 * calls the public API below). Keep these signatures stable; add to them rather than change them.
 * All mutating calls are server-only (authority); state replicates to clients.
 */
UCLASS()
class UNREAL_API ABreachableWall : public ACoverActor
{
	GENERATED_BODY()

public:
	ABreachableWall();

	/** True until the wall has been breached. */
	UFUNCTION(BlueprintPure, Category = "Breach")
	bool IsIntact() const { return !bBreached; }

	/** True once a wall reinforcement has been fitted. A reinforced wall can't be breached. */
	UFUNCTION(BlueprintPure, Category = "Breach")
	bool IsReinforced() const { return bReinforced; }

	/**
	 * The open cell a soldier must stand on, and the wall cell it faces, to place something on this wall
	 * from that side. Returns false if StandCell isn't orthogonally adjacent to one of the wall's cells
	 * across its face (i.e. not "on the side of the wall"), or the wall is breached.
	 */
	UFUNCTION(BlueprintPure, Category = "Breach")
	bool GetFaceCellFor(FIntPoint StandCell, FIntPoint& OutWallCell) const;

	/** Server: fit the reinforcement. Idempotent. */
	void SetReinforced();

	/**
	 * Server: destroy the wall. Does nothing if already breached or reinforced (returns false).
	 * BlastOrigin (world space) is used to throw rubble away from the charge.
	 * Updates the grid (cells become passable, LowWall cover), LOS, grenade blocking and replicates.
	 */
	bool Breach(const FVector& BlastOrigin);

	/** Breachable wall occupying Cell, or nullptr. */
	static ABreachableWall* FindAtCell(const UObject* WorldContext, FIntPoint Cell);

	/** Fired on server and clients when the wall is breached. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnBreached, ABreachableWall*);
	FOnBreached OnBreached;

	// ---------------------------------------------------------------- Additions (beyond the original contract)

	/** True if the wall runs along world X (its faces look along ±Y), else along world Y. */
	bool RunsAlongX() const;

	/** Every breachable wall in the world (for debug commands / UI). */
	static void GetAll(const UObject* WorldContext, TArray<ABreachableWall*>& OutWalls);

	// ---------------------------------------------------------------- Tunables (cosmetic only; never affect gameplay)

	// The intact weakened look (darker plaster, cracks, chipped patches, notched top) is modelled into the wall
	// mesh: see WallMeshes (SM_Wall_Plaster_Weakened).

	/** Broken stub of the wall with its masonry heap, one per breached cell (all below the 90 cm low-wall height). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breach|Visuals")
	TArray<TSoftObjectPtr<UStaticMesh>> StubMeshes;

	/** Debris chunks: 1 m cubes centred on their pivot, scaled to ChunkSize per chunk. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breach|Visuals")
	TArray<TSoftObjectPtr<UStaticMesh>> ChunkMeshes;

	/** Steel plates, bolts and raking braces fitted to a reinforced wall. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breach|Visuals")
	FLinearColor PlateColor = FLinearColor(0.22f, 0.24f, 0.26f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breach|Visuals")
	FLinearColor DustColor = FLinearColor(0.62f, 0.56f, 0.48f);

	/** Chunks that drop onto each breached wall cell from where the wall stood, on top of the stub mesh's own heap. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breach|Visuals", meta = (ClampMin = "0", ClampMax = "40"))
	int32 MoundChunksPerCell = 4;

	/** Chunks thrown away from the blast, per wall cell. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breach|Visuals", meta = (ClampMin = "0", ClampMax = "60"))
	int32 ScatterChunksPerCell = 9;

	/** Furthest a thrown chunk lands from the wall (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breach|Visuals", meta = (ClampMin = "50", Units = "cm"))
	float ScatterRadius = 350.f;

	/** Spread of thrown chunks either side of the blast direction (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breach|Visuals", meta = (ClampMin = "0", ClampMax = "180"))
	float ScatterSpreadAngle = 55.f;

	/** Edge length range of a chunk (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breach|Visuals", meta = (ClampMin = "2", Units = "cm"))
	FVector2D ChunkSize = FVector2D(14.f, 38.f);

	/** Top of the rubble heap on the wall cells (cm). Keep at or under UCoverData::LowWallHeight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breach|Visuals", meta = (ClampMin = "0", Units = "cm"))
	float MoundHeight = 75.f;

	/** How long the chunks fly before settling (s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breach|Visuals", meta = (ClampMin = "0.05", Units = "s"))
	float ScatterDuration = 0.7f;

	/** Peak height of a thrown chunk's arc (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breach|Visuals", meta = (ClampMin = "0", Units = "cm"))
	float ScatterArcHeight = 160.f;

	/** Dust puffs per wall cell. 0 = no dust. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breach|Visuals", meta = (ClampMin = "0", ClampMax = "10"))
	int32 DustPuffsPerCell = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breach|Visuals", meta = (ClampMin = "0.05", Units = "s"))
	float DustDuration = 1.6f;

	/** Clients joining (or receiving the breach) later than this after it happened skip the animation (s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breach|Visuals", meta = (ClampMin = "0", Units = "s"))
	float LateJoinSkipAnimationAfter = 2.f;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_Breached, BlueprintReadOnly, Category = "Breach")
	bool bBreached = false;

	UPROPERTY(ReplicatedUsing = OnRep_Reinforced, BlueprintReadOnly, Category = "Breach")
	bool bReinforced = false;

	/** Where the charge went off (whole cm, so the server's and clients' rubble land in the same places). */
	UPROPERTY(Replicated)
	FVector_NetQuantize BreachOrigin = FVector::ZeroVector;

	/** Server world time of the breach (late joiners skip the animation). */
	UPROPERTY(Replicated)
	float BreachServerTime = 0.f;

	UFUNCTION()
	void OnRep_Breached();

	UFUNCTION()
	void OnRep_Reinforced();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	/** Steel plates, bolts and raking braces of a reinforced wall. */
	UPROPERTY(VisibleAnywhere, Category = "Breach")
	TObjectPtr<UInstancedStaticMeshComponent> PlateShapes;

	UPROPERTY(VisibleAnywhere, Category = "Breach")
	TObjectPtr<UInstancedStaticMeshComponent> DustShapes;

	// Legacy placeholder sets (cracks, rubble cubes). Always empty; kept only so levels saved before the art pass load cleanly.
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> DamageShapes;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> RubbleShapes;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> RubbleDarkShapes;

private:
	/** Grid, cover and visuals for the breached state. bAnimate: throw the chunks and puff dust. */
	void ApplyBreached(bool bAnimate);
	void ApplyReinforcedVisual();
	void BuildRubble(bool bAnimate);
	FIntPoint FaceNormal() const;

	bool bBreachApplied = false;
	bool bReinforcedVisualApplied = false;

	/** A chunk in flight (component-space transforms). */
	struct FFlyingChunk
	{
		UInstancedStaticMeshComponent* Set = nullptr;
		int32 Instance = INDEX_NONE;
		FTransform From;
		FTransform To;
		FRotator Spin;
		float Delay = 0.f;
	};
	TArray<FFlyingChunk> FlyingChunks;

	struct FDustPuff
	{
		int32 Instance = INDEX_NONE;
		FVector Location;
		FVector Drift;
		float Size = 100.f;
		float Delay = 0.f;
	};
	TArray<FDustPuff> DustPuffs;

	float AnimTime = 0.f;
};
