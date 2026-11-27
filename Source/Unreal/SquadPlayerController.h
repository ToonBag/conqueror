#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SquadTypes.h"
#include "LootTypes.h"
#include "TacticalPlanTypes.h"
#include "BuildableTypes.h"
#include "SquadDebugTypes.h"
#include "Soldier.h"
#include "SquadPlayerController.generated.h"

class ABreachableWall;
class ASoldier;
class AGridManager;
class ASquadHUD;
class ASquadPlayerState;
class ATacticalPlan;

/**
 * Controls the squad (GDD §13).
 *   1-4 / Left click soldier     - select that soldier       ` (backtick) - select all
 *   Left click ground            - move; the lowest-numbered selected soldier goes to the click and
 *                                  the rest keep their formation around it
 *                                  (each destination flashes red with the soldier's number)
 *   Right click injured ally     - context menu at the cursor (battlefield or squad-bar card): Use medkit
 *                                  (GDD §5.2). Left click an entry to choose it; a click elsewhere, right click
 *                                  elsewhere or Esc closes it. Right click never moves soldiers.
 *   Shift + Left click ground    - face that direction
 *   Shift + Left click enemy     - explicit attack (every selected soldier)
 *   Alt + Left click             - every selected soldier throws a grenade at that cell
 *   Hover enemy                  - shows each selected soldier's hit chance against it, cover included
 *   Ctrl + Left click ally       - start dragging a downed (stunned/injured/dead) ally within 1 m,
 *                                  or let go of the one being dragged (GDD §5.4)
 *   C                            - cycle stance Standing -> Crouched -> Prone
 *   Tab                          - open / close the loot & trade menu for the selected soldier (GDD §5.5);
 *                                  Esc also closes it. Clicks on the menu don't reach the map.
 *   WASD / arrows                - pan camera, Q/E rotate, mouse wheel zoom
 *   M / minimap expand button    - open / close the tactical map (GDD §13.1); Esc also closes it. While it is
 *                                  open no click reaches the battlefield. CO / XO / planners draw on it with
 *                                  the chosen tool; right click erases a mark or cancels a stroke.
 *   Middle mouse (hold)          - command wheel at the cursor (battlefield, minimap or tactical map, GDD §13.2):
 *                                  let go over a slice to choose it, tap for a quick Ping. Spot / Ping for everyone,
 *                                  plan marks / Erase for planners, Request orders for non-CO / XO. Push / Hold / Route
 *                                  then follow the cursor until a left click places them; right click / Esc cancels.
 *                                  Build (everyone) reopens it in place as the Build sub-wheel (GDD §13.3): left click
 *                                  C4 / Reinforce to send the nearest selected soldier carrying one to the wall there.
 *   F6 (non-shipping builds)     - debug menu, top left (SquadPlayerControllerDebug.cpp): toggles (reveal all, bot AI,
 *                                  god mode, game speed, pause, ...), actions on the selection / all enemies, and click
 *                                  tools (teleport, heal, injure, stun, kill on the next left click). Right click / Esc
 *                                  drops a click tool.
 */
UCLASS()
class UNREAL_API ASquadPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASquadPlayerController();

	virtual void PlayerTick(float DeltaTime) override;

	/** Team whose soldiers this player commands. Set by the server's game mode when the player joins. */
	UPROPERTY(ReplicatedUsing = OnRep_PlayerTeam, EditAnywhere, BlueprintReadOnly, Category = "Squad")
	ESquadTeam PlayerTeam = ESquadTeam::Allies;

	/** Which of PlayerTeam's squads this player commands (ASoldier::SquadId; one squad per player, GDD §4.1). */
	UPROPERTY(ReplicatedUsing = OnRep_PlayerTeam, EditAnywhere, BlueprintReadOnly, Category = "Squad")
	int32 PlayerSquadId = 0;

	/** True if Soldier is in this player's own squad (team and squad). */
	bool IsOwnSquad(const ASoldier* Soldier) const { return Soldier && Soldier->Team == PlayerTeam && Soldier->SquadId == PlayerSquadId; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: put this player on Team and give it that team's squad SquadId. */
	void AssignTeam(ESquadTeam Team, int32 SquadId = 0);

	/**
	 * Client: send Order to the server on this controller's next tick. Queued rather than sent at once so an order
	 * issued from editor Python also goes over the network (UE runs RPCs called from editor scripts locally).
	 */
	void QueueSoldierOrder(const FSoldierOrder& Order) { CallServer([this, Order]() { ServerSoldierOrder(Order); }); }

	/** Client: make a Server RPC call on this controller's next tick (see QueueSoldierOrder for why). */
	void CallServer(TFunction<void()>&& Call) { PendingServerCalls.Add(MoveTemp(Call)); }

	/** Server: an order for one of this player's soldiers (sent by ASoldier::ForwardOrder on the client). */
	UFUNCTION(Server, Reliable)
	void ServerSoldierOrder(const FSoldierOrder& Order);

	/** Server: a loot or trade action by one of this player's soldiers (GDD §5.5). */
	UFUNCTION(Server, Reliable)
	void ServerLootAction(ASoldier* Looter, const FLootAction& Action);

	/** Two taps of the same number key (1-4) within this many seconds centre the camera on that soldier (GDD §13). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Squad|Input", meta = (ClampMin = "0.05", ClampMax = "1", Units = "s"))
	float DoubleTapWindow = 0.3f;

	/** Select just this soldier (nullptr clears the selection). */
	UFUNCTION(BlueprintCallable, Category = "Squad")
	void SelectSoldier(ASoldier* Soldier);

	UFUNCTION(BlueprintCallable, Category = "Squad")
	void SelectAll();

	/** Move order for the current selection, exactly as a left click on Goal (group formation, cohesion limit). Used by tests. */
	UFUNCTION(BlueprintCallable, Category = "Squad")
	void OrderMoveSelected(FIntPoint Goal) { IssueMove(Goal); }

	/** First selected soldier (lowest number), or nullptr. */
	UFUNCTION(BlueprintPure, Category = "Squad")
	ASoldier* GetSelectedSoldier() const;

	UFUNCTION(BlueprintPure, Category = "Squad")
	TArray<ASoldier*> GetSelectedSoldiers() const;

	/** The player's squad, ordered by squad number (dead soldiers included). */
	const TArray<TObjectPtr<ASoldier>>& GetSquad() const { return Squad; }

	// ---------------------------------------------------------------- Loot & trade (GDD §5.5)

	UFUNCTION(BlueprintPure, Category = "Squad|Loot")
	bool IsLootMenuOpen() const { return bLootMenuOpen; }

	UFUNCTION(BlueprintCallable, Category = "Squad|Loot")
	void SetLootMenuOpen(bool bOpen) { bLootMenuOpen = bOpen; }

	/**
	 * Ask for a loot-menu action for the selected soldier (the first one, if several are selected).
	 * This is the single entry point for looting: once soldiers replicate it becomes a Server RPC, and the
	 * server re-checks state and reach before moving anything.
	 */
	UFUNCTION(BlueprintCallable, Category = "Squad|Loot")
	void RequestLootAction(const FLootAction& Action);

	// ---------------------------------------------------------------- Context menu (GDD §13)

	enum class EContextAction : uint8
	{
		/** GDD §5.2: a squad soldier treats the injured ally the menu was opened on. */
		UseMedkit
	};

	struct FContextMenuItem
	{
		EContextAction Action = EContextAction::UseMedkit;
		FString Label;
		/** Second line: who will do it, or why it can't be done. */
		FString Detail;
		bool bEnabled = false;
	};

	/** The right-click menu: where it opened, which soldier it is about, and its entries (rebuilt every frame). */
	struct FContextMenu
	{
		bool bOpen = false;
		FVector2D ScreenPosition = FVector2D::ZeroVector;
		TWeakObjectPtr<ASoldier> Target;
		TArray<FContextMenuItem> Items;
	};

	const FContextMenu& GetContextMenu() const { return ContextMenu; }

	UFUNCTION(BlueprintPure, Category = "Squad|Context Menu")
	bool IsContextMenuOpen() const { return ContextMenu.bOpen; }

	/** Opens the right-click menu on Target at a screen position. Only injured allies have entries for now (GDD §5.2). */
	UFUNCTION(BlueprintCallable, Category = "Squad|Context Menu")
	bool OpenContextMenu(ASoldier* Target, FVector2D ScreenPosition);

	UFUNCTION(BlueprintCallable, Category = "Squad|Context Menu")
	void CloseContextMenu();

	/**
	 * The soldier that would treat Patient: the nearest selected soldier able to, else the nearest one in the squad.
	 * nullptr (with the reason) if nobody can.
	 */
	ASoldier* FindMedicFor(const ASoldier* Patient, FString* OutReason = nullptr) const;

	/** Same as choosing Use medkit from the context menu on Patient. Used by tests. */
	UFUNCTION(BlueprintCallable, Category = "Squad|Context Menu")
	bool OrderMedkitOn(ASoldier* Patient);

	// ---------------------------------------------------------------- Tactical map (GDD §13.1)

	/** A mark being drawn with the mouse (not yet sent to the server), for the HUD's preview. */
	struct FPlanDraft
	{
		bool bActive = false;
		ETacticalMarkerType Type = ETacticalMarkerType::Push;
		FVector2D Start = FVector2D::ZeroVector;
		FVector2D End = FVector2D::ZeroVector;
		TArray<FVector2D> Points;
	};

	UFUNCTION(BlueprintPure, Category = "Squad|Tactical Map")
	bool IsTacticalMapOpen() const { return bTacticalMapOpen; }

	/** Master volume (0-1) and mute: local settings from the HUD's audio widget, saved in GameUserSettings.ini. */
	float GetMasterVolume() const { return MasterVolume; }
	bool IsAudioMuted() const { return bAudioMuted; }
	void SetMasterVolume(float Volume);
	void SetAudioMuted(bool bMuted);

	UFUNCTION(BlueprintCallable, Category = "Squad|Tactical Map")
	void SetTacticalMapOpen(bool bOpen);

	UFUNCTION(BlueprintPure, Category = "Squad|Tactical Map")
	ETacticalTool GetTacticalTool() const { return TacticalTool; }

	/** Drawing tools fall back to View for players without planning rights. */
	UFUNCTION(BlueprintCallable, Category = "Squad|Tactical Map")
	void SetTacticalTool(ETacticalTool Tool);

	const FPlanDraft& GetPlanDraft() const { return PlanDraft; }

	// ---------------------------------------------------------------- Command wheel (GDD §13.2)

	/** What the middle-mouse command wheel is doing. */
	enum class ECommandMode : uint8
	{
		None,
		/** Wheel open (middle mouse held). */
		Radial,
		/** A Push / Hold / Route chosen from the wheel: the line follows the cursor until a left click places it. */
		Drawing,
		/** Erase chosen from the wheel: left click removes the mark under the cursor until cancelled. */
		Erasing
	};

	/** One entry of the Build sub-wheel (GDD §13.3), rebuilt every frame while it is open. */
	struct FBuildWheelItem
	{
		EBuildableKind Kind = EBuildableKind::C4;
		FString Label;
		/** Who will place it and how many are left, or (greyed out) why it can't be placed here. */
		FString Detail;
		/** Left across the squad's living soldiers. */
		int32 SquadCount = 0;
		bool bEnabled = false;
		TWeakObjectPtr<ASoldier> Builder;
	};

	struct FRadialMenu
	{
		/** Where the wheel opened, on screen and on the ground (world XY). */
		FVector2D ScreenCenter = FVector2D::ZeroVector;
		FVector2D WorldOrigin = FVector2D::ZeroVector;
		TArray<ERadialAction> Items;
		int32 Hover = INDEX_NONE;
		double OpenTime = 0.0;
		/**
		 * GDD §13.3: the Build sub-wheel, opened in place by the Build slice. It stays open once the middle mouse is let go:
		 * left click (or press and release the middle mouse over) an entry to choose it; right click, Esc or a click in
		 * the centre closes it. BuildItems replace Items while it is open.
		 */
		bool bBuildMenu = false;
		TArray<FBuildWheelItem> BuildItems;
	};

	/**
	 * Tests / screenshots (console: squad.OpenWheel X Y [build]): open the command wheel centred on ScreenPosition for
	 * the ground point WorldPoint. The Build sub-wheel stays open like the real one (left click a slice, right click / Esc
	 * closes); the main wheel closes again unless the middle mouse is held.
	 */
	void OpenCommandWheelAt(FVector2D ScreenPosition, FVector2D WorldPoint, bool bBuildMenu);

	/** The main wheel's slices for this player plus the Build sub-wheel's entries at WorldPoint, as text. For tests. */
	TArray<FString> DescribeCommandWheel(FVector2D WorldPoint) const;

	/** Build sub-wheel entries for a wheel opened on WorldPoint (data-driven from the soldiers' loadout). */
	TArray<FBuildWheelItem> GetBuildWheelItems(const FVector2D& WorldPoint) const;

	/** The breachable wall at (or right next to) a ground point, and the wall cell picked. */
	ABreachableWall* FindBreachableWallAt(const FVector2D& WorldPoint, FIntPoint& OutWallCell) const;

	/**
	 * GDD §13.3: the soldier among Candidates that would place Kind on Wall: the nearest one carrying it that can reach
	 * the wall's face. nullptr (with the reason) if none can.
	 */
	ASoldier* FindBuilderFor(EBuildableKind Kind, const ABreachableWall* Wall, FIntPoint WallCell, const TArray<ASoldier*>& Candidates, FString* OutReason = nullptr) const;

	/** Same as choosing Kind from the Build sub-wheel opened on WorldPoint, with the current selection. Used by tests. */
	UFUNCTION(BlueprintCallable, Category = "Squad|Build")
	void OrderBuildSelected(EBuildableKind Kind, FVector2D WorldPoint);

	/** Server: Kind placed on the wall at WorldPoint by the nearest able soldier among Candidates (this player's own). */
	UFUNCTION(Server, Reliable)
	void ServerOrderBuild(EBuildableKind Kind, FVector2D WorldPoint, const TArray<ASoldier*>& Candidates);

	/** Short refusal shown at the wheel's point ("NO WALL", "SELECT A SOLDIER", ...), like a grenade's BLOCKED. */
	struct FBuildFeedback
	{
		FString Text;
		FVector2D World = FVector2D::ZeroVector;
		double Time = -100.0;
	};
	const FBuildFeedback& GetBuildFeedback() const { return BuildFeedback; }

	ECommandMode GetCommandMode() const { return CommandMode; }
	const FRadialMenu& GetRadialMenu() const { return Radial; }
	/** The line being placed from the wheel (CommandMode == Drawing). */
	const FPlanDraft& GetCommandDraft() const { return CommandDraft; }

	/** The wheel's slices for this player: pings for everyone, plan tools for planners, Request orders for non-CO / XO. */
	TArray<ERadialAction> GetRadialActions() const;

	/** Drop a ping for the team (GDD §13.2). Everyone may. */
	UFUNCTION(BlueprintCallable, Category = "Squad|Tactical Map")
	void AddPing(ETacticalPingType Type, FVector2D Location) { CallServer([=, this]() { ServerAddPing(Type, Location); }); }

	UFUNCTION(Server, Reliable)
	void ServerAddPing(ETacticalPingType Type, FVector2D Location);

	/** Pings up on this player's copy of the team plan (-1: no plan here). For tests. */
	UFUNCTION(BlueprintPure, Category = "Squad|Tactical Map")
	int32 GetPingCount() const;

	ASquadPlayerState* GetSquadPlayerState() const;

	/** This team's plan (replicated to this team only), or nullptr. */
	ATacticalPlan* GetTacticalPlan() const;

	/** Ask the CO / XO for orders, marked at this player's squad (GDD §13.1). */
	UFUNCTION(BlueprintCallable, Category = "Squad|Tactical Map")
	void RequestOrders() { CallServer([=, this]() { ServerRequestOrders(); }); }

	/**
	 * Draw a mark on the team's plan, exactly as the tactical map's tools do (world XY in cm). Start is the point for
	 * point marks; Push / HoldLine use Start-End; Route uses RoutePoints. The server checks planning rights.
	 */
	UFUNCTION(BlueprintCallable, Category = "Squad|Tactical Map")
	void AddTacticalMarker(ETacticalMarkerType Type, FVector2D Start, FVector2D End, const TArray<FVector2D>& RoutePoints);

	UFUNCTION(BlueprintCallable, Category = "Squad|Tactical Map")
	void RemoveTacticalMarker(int32 MarkerId) { CallServer([=, this]() { ServerRemoveTacticalMarker(MarkerId); }); }

	UFUNCTION(BlueprintCallable, Category = "Squad|Tactical Map")
	void ClearTacticalPlan() { CallServer([=, this]() { ServerClearTacticalMarkers(); }); }

	UFUNCTION(BlueprintCallable, Category = "Squad|Tactical Map")
	void AcknowledgeOrderRequest(int32 RequestId) { CallServer([=, this]() { ServerAcknowledgeOrderRequest(RequestId); }); }

	UFUNCTION(BlueprintCallable, Category = "Squad|Tactical Map")
	void SetPlanningDelegated(ASquadPlayerState* Target, bool bDelegated) { CallServer([=, this]() { ServerSetPlanningDelegated(Target, bDelegated); }); }

	/** Number of marks / open requests on this player's copy of the team plan (-1: no plan here). For tests. */
	UFUNCTION(BlueprintPure, Category = "Squad|Tactical Map")
	int32 GetTacticalMarkerCount() const;

	UFUNCTION(BlueprintPure, Category = "Squad|Tactical Map")
	int32 GetOpenOrderRequestCount() const;

	// Every tactical-plan change goes through the server, which checks team and role (ATacticalPlan).
	UFUNCTION(Server, Reliable)
	void ServerAddTacticalMarker(const FTacticalMarker& Marker);

	UFUNCTION(Server, Reliable)
	void ServerRemoveTacticalMarker(int32 MarkerId);

	UFUNCTION(Server, Reliable)
	void ServerClearTacticalMarkers();

	UFUNCTION(Server, Reliable)
	void ServerRequestOrders();

	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeOrderRequest(int32 RequestId);

	/** CO / XO only: give or take back another team-mate's planning rights. */
	UFUNCTION(Server, Reliable)
	void ServerSetPlanningDelegated(ASquadPlayerState* Target, bool bDelegated);

	// ---------------------------------------------------------------- Debug menu (non-shipping, SquadPlayerControllerDebug.cpp)

	/** What the next left click on the map does while a debug click tool is picked. */
	enum class EDebugClickTool : uint8
	{
		None,
		Teleport,
		Heal,
		Injure,
		Stun,
		Kill
	};

	/** One button on a debug-menu row. */
	struct FDebugMenuButton
	{
		FString Label;
		ESquadDebugAction Action = ESquadDebugAction::None;
		float Param = 0.f;
		EDebugClickTool Tool = EDebugClickTool::None;
		/** Restart the level (local: RestartLevel). */
		bool bRestart = false;
		/** Drawn highlighted: the current value of a setting, or the picked click tool. */
		bool bActive = false;
		bool bEnabled = true;
	};

	/** A row of the debug menu: a section header, or a label with buttons. Rebuilt every frame while open. */
	struct FDebugMenuRow
	{
		FString Label;
		bool bHeader = false;
		TArray<FDebugMenuButton> Buttons;
	};

	UFUNCTION(BlueprintPure, Category = "Squad|Debug")
	bool IsDebugMenuOpen() const { return bDebugMenuOpen; }

	UFUNCTION(BlueprintCallable, Category = "Squad|Debug")
	void SetDebugMenuOpen(bool bOpen);

	const TArray<FDebugMenuRow>& GetDebugMenuRows() const { return DebugRows; }
	EDebugClickTool GetDebugClickTool() const { return DebugClickTool; }
	static FString GetDebugClickToolName(EDebugClickTool Tool);

	/** Lines of live info at the top of the debug menu (net mode, game speed, hovered cell and soldier). */
	TArray<FString> GetDebugInfoLines() const;

	/** Same as pressing that debug-menu button (Row / Button index as drawn). For tests. */
	UFUNCTION(BlueprintCallable, Category = "Squad|Debug")
	bool PressDebugMenuButton(int32 Row, int32 Button);

	/** Debug action on the server. Refused in shipping builds and on a dedicated server outside the editor unless squad.AllowDebugMenu 1. */
	UFUNCTION(Server, Reliable)
	void ServerDebugAction(ESquadDebugAction Action, float Param, FIntPoint Cell, const TArray<ASoldier*>& Targets);

protected:
	UFUNCTION()
	void OnRep_PlayerTeam();

	virtual void BeginPlay() override;

private:
	/** The camera has been put over this player's squad (PlayerTick, once the team is known). */
	bool bCameraOnOwnSquad = false;

	/** Server RPC calls waiting for PlayerTick (CallServer). */
	TArray<TFunction<void()>> PendingServerCalls;

	// Audio settings (SquadPlayerControllerAudio.cpp).
	float MasterVolume = 1.f;
	bool bAudioMuted = false;
	bool bVolumeDragging = false;
	void LoadAudioSettings();
	void SaveAudioSettings() const;
	void ApplyAudioVolume() const;
	/** The HUD's mute button and volume slider. True if it used the mouse this frame. */
	bool HandleAudioMouse();

	bool bDebugMenuOpen = false;
	EDebugClickTool DebugClickTool = EDebugClickTool::None;
	TArray<FDebugMenuRow> DebugRows;
	void RebuildDebugMenu();
	void ExecuteDebugButton(const FDebugMenuButton& Button);
	/** Debug menu clicks, and the click tool's left click on the map. True if it used the mouse this frame. */
	bool HandleDebugMouse();
	/** The living (or, bAllowDead, any) soldier under the cursor: its model, or failing that the cell under the cursor. */
	ASoldier* FindSoldierUnderCursor(bool bAllowDead) const;

	void GatherSquad();
	void SetSelection(const TArray<ASoldier*>& NewSelection);
	void PruneSelection();
	void SelectBySquadNumber(int32 Number);

	/** Moves the camera over the soldier with this squad number (living or dead). */
	void CentreCameraOnSquadNumber(int32 Number);

	/** Turns the camera to look from the player's own deployment zone towards the middle of the map. */
	void FaceCameraFromOwnZone();

	/** Number key tapped last (0 = none) and when (real seconds), for double-tap detection. */
	int32 LastNumberTapped = 0;
	double LastNumberTapTime = 0.0;
	void HandleCamera(float DeltaTime);
	void HandleKeys();
	void HandleMouse();

	/** The full-wall or window-wall cell whose slab the cursor ray meets first (before the ground), and how far along the ray. */
	bool FindFullWallUnderCursor(FIntPoint& OutCell, float& OutDistance) const;
	/** Clicks over the open loot menu go to it. Returns true if the cursor is on the menu. */
	bool HandleLootMenuMouse();

	/** Clicks on the squad bar select soldiers (Shift adds / removes). True if the cursor is on the bar. */
	bool HandleSquadBarMouse();

	/**
	 * Left click / drag on the minimap centres the camera on that point; a drag started there keeps following
	 * the cursor (clamped to the map) until the button is released. True if the cursor is on the minimap or a
	 * minimap drag is in progress: nothing reaches the map.
	 */
	bool HandleMinimapMouse();

	bool bMinimapDragging = false;

	/**
	 * GDD §13.1: while the tactical map is open it takes every click. Buttons on its side panel, then the map:
	 * View tool pans the camera, drawing tools draw (point marks on click, arrows / lines / routes by dragging),
	 * Erase or right click removes the mark under the cursor. Always true while open.
	 */
	bool HandleTacticalMapMouse();
	/** Sends the drawn line / route to the server and clears the draft (the server drops strokes that are too short). */
	void FinishPlanDraft(FPlanDraft& Draft);

	/**
	 * GDD §13.2: middle mouse opens the command wheel where the cursor is (battlefield, minimap or tactical map); let go
	 * over a slice to choose it, or tap to Ping. Then runs live drawing / erasing. True while it is using the mouse.
	 */
	bool HandleCommandMouse();

	/** Ground point (world XY) under the cursor: on the tactical map, the minimap, or the battlefield. False off the map or over a panel. */
	bool GetCursorWorld2D(FVector2D& OutWorld) const;
	void ExecuteRadialAction(ERadialAction Action, const FVector2D& Origin);
	void CancelCommand();

	ECommandMode CommandMode = ECommandMode::None;
	FRadialMenu Radial;
	FPlanDraft CommandDraft;
	FBuildFeedback BuildFeedback;
	void ShowBuildFeedback(const FString& Text, const FVector2D& World);

	/** Server -> owning client: a build order was refused (GDD §13.3), shown at the wheel's point. */
	UFUNCTION(Client, Reliable)
	void ClientBuildFeedback(const FString& Text, FVector2D World);

	/** Middle mouse released this soon without choosing a slice: a quick Ping. */
	static constexpr double QuickTapTime = 0.25;
	/** Spot snaps onto a visible enemy within this distance of the point (cm). */
	static constexpr float SpotSnapDistance = 150.f;

	bool bTacticalMapOpen = false;
	ETacticalTool TacticalTool = ETacticalTool::View;
	FPlanDraft PlanDraft;
	/** View tool: dragging the camera around on the tactical map. */
	bool bTacticalMapPanning = false;

	bool bLootMenuOpen = false;

	FContextMenu ContextMenu;
	/** Rebuilds the menu's entries, closing it if its soldier no longer has any (healed, killed, gone). */
	void RefreshContextMenu();
	/** While the menu is open: clicks on it choose an entry; any click elsewhere closes it. True if the click was used. */
	bool HandleContextMenuMouse();
	void ExecuteContextAction(EContextAction Action, ASoldier* Target);

	void IssueMove(FIntPoint Goal);
	void IssueFace(FIntPoint Cell);
	void IssueAttack(ASoldier* Target);
	void IssueGrenade(FIntPoint Cell);
	void IssueStanceCycle();
	/** GDD §5.3 / §13: H toggles hold fire for the selection (all on unless every selected soldier already holds). */
	void IssueHoldFireToggle();
	void IssueDragToggle(ASoldier* Target);

	bool IsHeldBySquadMate(FIntPoint Cell, const ASoldier* Soldier, const TArray<ASoldier*>& Movers) const;
	bool FindFallbackCell(FIntPoint Slot, const ASoldier* Soldier, const TArray<FIntPoint>& Reserved, FIntPoint& OutCell) const;

	/**
	 * GDD §4.3: the cell nearest Goal the group's anchor can be sent to with every mover (at Goal + its offset) inside
	 * the squad cohesion limit of the soldiers staying behind. Returns Goal when it already fits.
	 */
	FIntPoint ClampGroupGoal(FIntPoint Goal, FIntPoint AnchorCell, const TArray<ASoldier*>& Movers, const TArray<FIntPoint>& Offsets) const;
	void UpdatePendingMoves(float DeltaTime);
	void CancelPendingMoves();

	/** A group-move slot that a squad-mate still occupies; retried each tick until it clears. */
	struct FPendingMove
	{
		TWeakObjectPtr<ASoldier> Soldier;
		FIntPoint Slot;
		float TimeLeft;
	};
	TArray<FPendingMove> PendingMoves;

	/** Seconds to wait for a squad-mate to clear a formation slot before taking the nearest free cell. */
	static constexpr float PendingMoveTimeout = 3.f;

	/** The player's own soldiers, sorted by squad number. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ASoldier>> Squad;

	/** Currently selected soldiers, sorted by squad number. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ASoldier>> Selected;

	UPROPERTY(Transient)
	TObjectPtr<AGridManager> Grid;
};
