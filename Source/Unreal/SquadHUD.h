#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "LootTypes.h"
#include "TacticalPlanTypes.h"
#include "SquadHUD.generated.h"

class ASoldier;
class ASquadPlayerController;
class UFont;

/**
 * Screen overlay (placeholder canvas UI until the game has proper UMG widgets):
 *  - Squad bar: always along the bottom of the screen, one translucent card per soldier (1-4) with rifle,
 *    loaded / spare rounds, reload progress, grenades, medkits, stress and status (stunned, in cover, under
 *    fire, ...). Selected soldiers' cards are highlighted; clicking a card selects that soldier.
 *  - Kill feed: top right, who killed whom, with what, and the chance of the roll that did it.
 *  - Minimap: bottom left, the whole grid with cover, victory points, the player's soldiers (with facing
 *    cones), allied soldiers and the enemies the team can currently see. Own deployment zone at the bottom.
 *  - Tactical map (GDD §13.1), M or the minimap's expand button: the map maximised in the middle of the screen
 *    with the team's plan (arrows, shields, routes, ...) drawn by the CO / XO / planners, and a side panel with
 *    the drawing tools, the team's roles (delegate planning) and requests for orders. Plan marks also show on
 *    the minimap.
 *  - Loot / trade menu (GDD §5.5), opened with Tab, drawn just above the squad bar: the selected soldier's kit
 *    on the left, and everything lootable on or next to its cell on the right, in one list.
 * The controller asks it which button / card is under the cursor.
 */
UCLASS()
class UNREAL_API ASquadHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** True if the loot menu was drawn last frame and covers this screen position (clicks there don't reach the map). */
	bool IsOverLootMenu(const FVector2D& ScreenPosition) const;

	/** The enabled loot-menu button at this position, if any. bSingle: Shift-click, move one item / one clip only. */
	bool GetLootActionAt(const FVector2D& ScreenPosition, bool bSingle, FLootAction& OutAction) const;

	/** True if the right-click context menu was drawn last frame and covers this screen position. */
	bool IsOverContextMenu(const FVector2D& ScreenPosition) const { return bContextMenuVisible && ContextMenuBox.IsInside(ScreenPosition); }

	/** Index of the context-menu entry at this position as drawn last frame (enabled or not), or INDEX_NONE. */
	int32 GetContextMenuItemAt(const FVector2D& ScreenPosition) const;

	/** True if the squad bar was drawn last frame and covers this screen position. */
	bool IsOverSquadBar(const FVector2D& ScreenPosition) const;

	/** True if the minimap was drawn last frame and covers this screen position (clicks there don't reach the map). */
	bool IsOverMinimap(const FVector2D& ScreenPosition) const { return bMinimapVisible && MinimapBox.IsInside(ScreenPosition); }

	/**
	 * The ground point (grid height) under this screen position on the minimap as drawn last frame, clamped to
	 * the map. False if the minimap wasn't drawn.
	 */
	bool MinimapToWorld(const FVector2D& ScreenPosition, FVector& OutWorld) const;

	/** True if the debug menu (F6) was drawn last frame and covers this screen position. */
	bool IsOverDebugMenu(const FVector2D& ScreenPosition) const { return bDebugMenuVisible && DebugMenuBox.IsInside(ScreenPosition); }

	/** The enabled debug-menu button at this position as drawn last frame: X = row, Y = button. */
	bool GetDebugMenuButtonAt(const FVector2D& ScreenPosition, FIntPoint& OutIndex) const;

	/** Audio widget (top left): mute button and master volume slider, as drawn last frame. */
	bool IsOverAudioControls(const FVector2D& ScreenPosition) const { return bAudioVisible && AudioPanelBox.IsInside(ScreenPosition); }
	bool IsOverMuteButton(const FVector2D& ScreenPosition) const { return bAudioVisible && MuteButtonBox.IsInside(ScreenPosition); }
	/** Volume (0-1) for this position on the slider. Without bClamp, false unless the position is on the slider. */
	bool GetVolumeSliderValueAt(const FVector2D& ScreenPosition, bool bClamp, float& OutVolume) const;

	/** The soldier whose squad-bar card is at this position, if any. */
	class ASoldier* GetSquadCardAt(const FVector2D& ScreenPosition) const;

	// ---------------------------------------------------------------- Tactical map (GDD §13.1)

	enum class ETacMapAction : uint8
	{
		Close,
		SelectTool,
		ClearPlan,
		RequestOrders,
		Acknowledge,
		/** Move the camera to a request's location. */
		FocusRequest,
		ToggleDelegate
	};

	/** A button on the tactical map's side panel, as drawn last frame. */
	struct FTacMapButton
	{
		FBox2D Box = FBox2D(ForceInit);
		ETacMapAction Action = ETacMapAction::Close;
		ETacticalTool Tool = ETacticalTool::View;
		/** Request id for Acknowledge. */
		int32 Param = 0;
		FVector2D Location = FVector2D::ZeroVector;
		TWeakObjectPtr<class ASquadPlayerState> Player;
		bool bEnabled = true;
	};

	/** The small expand button in the minimap's corner (opens the tactical map). */
	bool IsOverMinimapExpandButton(const FVector2D& ScreenPosition) const { return bMinimapVisible && MinimapExpandBox.IsInside(ScreenPosition); }

	/**
	 * World XY under this screen position on the tactical map as drawn last frame. Outside the map: false, unless
	 * bClamp (then the nearest point on the map's edge). False if the tactical map wasn't drawn.
	 */
	bool TacticalMapToWorld(const FVector2D& ScreenPosition, FVector2D& OutWorld, bool bClamp) const;

	/** The enabled side-panel button at this position, if any. */
	bool GetTacticalButtonAt(const FVector2D& ScreenPosition, FTacMapButton& OutButton) const;

	/** Id of the plan mark under this position on the tactical map (nearest within a few pixels), or INDEX_NONE. */
	int32 FindTacticalMarkerAt(const FVector2D& ScreenPosition) const;

	/** Id of the plan mark drawn on the battlefield under this position (GDD §13.2), or INDEX_NONE. */
	int32 FindWorldMarkerAt(const FVector2D& ScreenPosition) const;

	/** Command wheel (GDD §13.2): which of NumItems slices of a wheel centred at Center the cursor points at (INDEX_NONE in the middle). */
	int32 GetRadialItemAt(const FVector2D& Center, const FVector2D& Mouse, int32 NumItems) const;

	/** World XY (cm) <-> screen pixels for a plan view of the map, turned in 90° steps (never mirrored). */
	struct FMapXf
	{
		FBox2D MapBox = FBox2D(ForceInit);
		FVector2D WorldCenter = FVector2D::ZeroVector;
		/** World directions (unit, along X or Y) that run to the right and down the map. */
		FVector2D Right = FVector2D(0.f, 1.f);
		FVector2D Down = FVector2D(-1.f, 0.f);
		float PxPerCm = 0.01f;
		float GroundZ = 0.f;

		FVector2D ToScreen(const FVector2D& World) const
		{
			const FVector2D Offset = World - WorldCenter;
			return MapBox.GetCenter() + FVector2D(Offset | Right, Offset | Down) * PxPerCm;
		}
		FVector2D ToScreen(const FVector& World) const { return ToScreen(FVector2D(World)); }
		FVector2D ToWorld(const FVector2D& Screen) const
		{
			const FVector2D Offset = (Screen - MapBox.GetCenter()) / PxPerCm;
			return WorldCenter + Right * Offset.X + Down * Offset.Y;
		}
		FVector2D DirToScreen(const FVector2D& WorldDir) const { return FVector2D(WorldDir | Right, WorldDir | Down); }
		/** True if world X runs across the map (left-right). */
		bool XIsHorizontal() const { return FMath::Abs(Right.X) > 0.5f; }
	};

private:
	struct FSquadCard
	{
		FBox2D Box = FBox2D(ForceInit);
		TWeakObjectPtr<class ASoldier> Soldier;
	};

	/** Line from each of the player's attacking soldiers' muzzle to its target (aiming: dashed, firing: solid). */
	void DrawAimLines(ASquadPlayerController& PC);
	/** GDD §13: dashed line along the route each of the player's moving soldiers is taking to its destination cell. */
	void DrawMovePaths(ASquadPlayerController& PC);
	void DrawSquadBar(ASquadPlayerController& PC);
	void DrawSquadCard(const class ASoldier& Soldier, float X, float Y, float Width, float Height, float Scale, bool bHover);

	/**
	 * Kill feed (GDD §13), top right, newest on top: killer's rank [regiment] and name, how (rifle / grenade
	 * icon) with the chance of the roll that killed, then the victim. Lines fade out after a few seconds.
	 */
	void DrawKillFeed(ASquadPlayerController& PC);

	/** Top centre: which team holds every VP and the time left on the VP win timer; centre: the match result (GDD §11). */
	void DrawMatchStatus(ASquadPlayerController& PC);

	/** After a failed join (e.g. an out-of-date build): why, over the offline map the engine fell back to. */
	void DrawJoinError();

	/**
	 * Minimap (GDD §13), bottom left: cover, victory points, deployment zones, the camera's view, the player's
	 * soldiers with facing cones, allied soldiers and visible enemies. Turned so the player's own deployment
	 * zone is at the bottom.
	 */
	void DrawMinimap(ASquadPlayerController& PC);

	/**
	 * Which world direction runs down a plan view of the map for this player: towards their own deployment zone,
	 * so it sits at the bottom. Right is a quarter turn from it.
	 */
	void GetMapOrientation(const ASquadPlayerController& PC, const class AGridManager& Grid, FMapXf& Xf) const;

	/**
	 * The map itself, shared by the minimap and the tactical map: background, reference grid, deployment zones,
	 * victory points, cover, the camera's view, allied soldiers, visible enemies and the player's own soldiers with
	 * facing cones. Detail scales markers and soldier dots (1 = minimap size).
	 */
	void DrawMapBase(ASquadPlayerController& PC, const class AGridManager& Grid, const FMapXf& Xf, float S, float Detail);

	/**
	 * The team's plan marks and open order requests over a map (GDD §13.1). HighlightId is drawn emphasised (hover).
	 * The tactical map also shows the stroke being drawn.
	 */
	void DrawPlanOverlay(ASquadPlayerController& PC, const FMapXf& Xf, float S, float Detail, int32 HighlightId, bool bTacticalMap);

	/**
	 * GDD §13.2: the team's plan marks, the line being placed from the command wheel, open order requests (CO / XO) and
	 * pings, drawn over the battlefield where they are on the ground. Pings off screen get an arrow at the screen edge.
	 */
	void DrawWorldPlan(ASquadPlayerController& PC);

	/** GDD §13.2: the command wheel while middle mouse is held, and a hint while placing a line or erasing. */
	void DrawCommandUI(ASquadPlayerController& PC);
	/** GDD §13.3: the Build sub-wheel (drawn by DrawCommandUI while it is open). SquadHUDBuild.cpp. */
	void DrawBuildWheel(ASquadPlayerController& PC);
	/** GDD §13.3: placement progress bars over own soldiers, and the refusal message at the wheel's point. */
	void DrawBuildOverlays(ASquadPlayerController& PC);

	// ---------------------------------------------------------------- Battlefield overlays (SquadHUDOverlays.cpp)

	/** Move range outline, cover-direction preview and cover vision flashes, under the panels. */
	void DrawBattlefieldOverlays(ASquadPlayerController& PC);

	/**
	 * GDD §4.3 / §13: a faint outline round the cells the selection can be sent to without hitting the squad
	 * cohesion limit (for a group: where its lead soldier can go with the formation kept). Hidden when nothing
	 * limits the move (the whole living squad is selected).
	 */
	void AddMoveRange(ASquadPlayerController& PC, TArray<struct FCanvasUVTri>& Tris);

	/**
	 * GDD §9.2 / §13: hovering a free cell next to cover with a soldier selected shows, round that cell, the arc of
	 * fire each adjacent piece of cover protects against. The cover the soldier will lean on when it arrives
	 * (full value) is bright; the rest (its back to them: reduced value) is dim.
	 */
	void AddCoverPreview(ASquadPlayerController& PC, TArray<struct FCanvasUVTri>& Tris, TArray<TPair<FString, FVector2D>>& Labels, TArray<FLinearColor>& LabelColors);

	/** GDD §9.3 / §13: a brief cone of what the soldier can see, flashed when one of the player's soldiers settles into cover. */
	void UpdateVisionFlashes(ASquadPlayerController& PC);
	void AddVisionFlashes(ASquadPlayerController& PC, TArray<struct FCanvasUVTri>& Tris);
	/** One fan of a vision flash: rays from Apex over [FirstYaw, FirstYaw + SpanYaw], each as far as the soldier sees. */
	void AddVisionFan(const class AGridManager& Grid, const ASoldier& Soldier, const FVector2D& Apex, float FirstYaw, float SpanYaw,
		float MinReach, float Height, float TargetHeight, float MaxSight, float Now);

	/** Move range edges (world XY segments), rebuilt a few times a second. */
	TArray<TPair<FVector2D, FVector2D>> MoveRangeEdges;
	float MoveRangeRefreshTime = -1.f;

	/** Cover preview: arrival facing cached per (soldier, start cell, hovered cell) so the path is found once. */
	TWeakObjectPtr<ASoldier> CoverPreviewSoldier;
	FIntPoint CoverPreviewFrom = FIntPoint(MAX_int32, MAX_int32);
	FIntPoint CoverPreviewCell = FIntPoint(MAX_int32, MAX_int32);
	float CoverPreviewArrivalYaw = 0.f;
	bool bCoverPreviewReachable = false;
	/** AGridManager::GetCoverRevision() the cached preview was found with. */
	int32 CoverPreviewRevision = -1;

	struct FVisionFlash
	{
		TWeakObjectPtr<ASoldier> Soldier;
		float StartTime = 0.f;
		FVector2D Apex = FVector2D::ZeroVector;
		/** Far edge of the cone, ray by ray, in world XY. */
		TArray<FVector2D> Edge;
	};
	TArray<FVisionFlash> VisionFlashes;
	/** Cell each of the player's soldiers last flashed its cone on; forgotten when it moves off. */
	TMap<TWeakObjectPtr<ASoldier>, FIntPoint> VisionFlashCells;
	/** Each soldier's facing last frame and since when (game time) it has held it, so the cone waits for the lean. */
	struct FFacingSettle { float Yaw = 0.f; float Since = 0.f; };
	TMap<TWeakObjectPtr<ASoldier>, FFacingSettle> VisionFacing;

	/** Seconds a cover vision cone stays up (fading in and out). */
	UPROPERTY(EditAnywhere, Category = "HUD|Overlays", meta = (ClampMin = "0.2", Units = "s"))
	float VisionFlashDuration = 2.2f;

	/** Half-angle of the cover vision cone either side of the soldier's facing. */
	UPROPERTY(EditAnywhere, Category = "HUD|Overlays", meta = (ClampMin = "5", ClampMax = "180", Units = "deg"))
	float VisionFlashHalfAngle = 60.f;

	/** World XY on the ground to screen pixels; false if behind the camera. */
	bool ProjectGround(const ASquadPlayerController& PC, const FVector2D& World, FVector2D& OutScreen) const;

	/** HUD scale used last frame (1 at 1080p). */
	float LastScale = 1.f;

	/** GDD §13.1: the maximised map, centred, with its side panel (tools, team roles, order requests). */
	void DrawTacticalMap(ASquadPlayerController& PC);
	void DrawTacticalPanel(ASquadPlayerController& PC, const FBox2D& SideBox, float S, int32 HoverMarkerId);
	void DrawTacButton(const FTacMapButton& Button, const FString& Label, const FVector2D& Mouse, float S, bool bActive, ETacticalTool IconTool = ETacticalTool::View);

	/**
	 * Short notices at the top of the screen (GDD §13.1): new orders on the plan, order requests (CO / XO), your
	 * request acknowledged, and your role changing. Found by comparing what the plan and player state hold with
	 * what this HUD saw last frame.
	 */
	void UpdatePlanNotices(ASquadPlayerController& PC);
	void DrawPlanNotices();

	FBox2D MinimapBox = FBox2D(ForceInit);
	FBox2D MinimapExpandBox = FBox2D(ForceInit);
	bool bMinimapVisible = false;

	/** Minimap mapping as drawn last frame, for turning clicks back into world positions. */
	FMapXf MinimapXf;

	/** Tactical map as drawn last frame. */
	FMapXf TacMapXf;
	bool bTacMapVisible = false;
	/** Plan mark sizes on the tactical map last frame (pixels), for hit testing. */
	float TacMapIconRadius = 10.f;
	float TacMapLineWidth = 4.f;
	TArray<FTacMapButton> TacButtons;

	struct FNotice
	{
		FString Text;
		FLinearColor Color = FLinearColor::White;
		float Time = 0.f;
	};
	TArray<FNotice> Notices;
	bool bNoticesPrimed = false;
	TSet<int32> SeenMarkerIds;
	TSet<int32> SeenRequestIds;
	TSet<int32> SeenAcknowledgedIds;
	ETacticalRole SeenRole = ETacticalRole::Soldier;

	/** Squad-bar cards drawn last frame, in screen pixels. */
	TArray<FSquadCard> SquadCards;
	FBox2D SquadBarBox = FBox2D(ForceInit);
	bool bSquadBarVisible = false;

	struct FButton
	{
		FBox2D Box = FBox2D(ForceInit);
		FString Label;
		ELootActionType Action = ELootActionType::Take;
		ELootItemType Item = ELootItemType::Ammo;
		int32 WeaponIndex = 0;
		TWeakObjectPtr<AActor> Source;
		TWeakObjectPtr<class UWeaponData> AmmoFor;
		/** Shift-click amount (0 = Shift does nothing different). */
		int32 SingleCount = 0;
		bool bEnabled = true;
	};

	struct FRow
	{
		FString Text;
		FLinearColor Color = FLinearColor::White;
		bool bHeader = false;
		bool bHasButton = false;
		FButton Button;
	};

	/** Right-click menu (GDD §13) at the cursor, on top of the other panels: the soldier it's about and its entries. */
	void DrawContextMenu(ASquadPlayerController& PC);
	FBox2D ContextMenuBox = FBox2D(ForceInit);
	TArray<FBox2D> ContextItemBoxes;
	bool bContextMenuVisible = false;

	/** Debug menu (F6, non-shipping) top left, on top of everything, and the picked click tool's hint. SquadHUDDebug.cpp. */
	void DrawDebugMenu(ASquadPlayerController& PC);

	/** Audio widget, top left: mute button, master volume slider and percentage. SquadHUDAudio.cpp. */
	void DrawAudioControls(ASquadPlayerController& PC);
	FBox2D AudioPanelBox = FBox2D(ForceInit);
	FBox2D MuteButtonBox = FBox2D(ForceInit);
	FBox2D VolumeSliderBox = FBox2D(ForceInit); // Hit area; the track runs along its middle.
	float VolumeTrackMinX = 0.f;
	float VolumeTrackMaxX = 0.f;
	bool bAudioVisible = false;
	struct FDebugButtonBox
	{
		FBox2D Box = FBox2D(ForceInit);
		FIntPoint Index = FIntPoint::ZeroValue;
		bool bEnabled = true;
	};
	TArray<FDebugButtonBox> DebugButtonBoxes;
	FBox2D DebugMenuBox = FBox2D(ForceInit);
	bool bDebugMenuVisible = false;

	void DrawLootMenu(ASquadPlayerController& PC);
	void DrawColumn(const TArray<FRow>& Rows, float X, float Y, float Width, const FVector2D& Mouse, UFont* Font);
	void DrawButton(FButton Button, const FVector2D& Mouse, UFont* Font);

	/** Buttons drawn last frame, in screen pixels. */
	TArray<FButton> Buttons;
	FBox2D PanelBox = FBox2D(ForceInit);
	bool bPanelVisible = false;
};
