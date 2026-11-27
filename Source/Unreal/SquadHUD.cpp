#include "SquadHUD.h"

#include "LootSystem.h"
#include "Soldier.h"
#include "SoldierData.h"
#include "SquadGameState.h"
#include "SquadJoinError.h"
#include "SquadPlayerController.h"
#include "MapZoneColors.h"
#include "WeaponData.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

namespace
{
	constexpr float PanelWidth = 860.f;
	constexpr float ColumnWidth = 400.f;
	constexpr float Padding = 20.f;
	constexpr float RowHeight = 26.f;
	constexpr float ButtonWidth = 70.f;
	constexpr float TitleHeight = 40.f;
	constexpr float FooterHeight = 64.f;
	/** Squad-bar cards draw one pip per round up to this magazine size, and a fill bar above it (MG belts). */
	constexpr int32 MaxAmmoPips = 20;

	const FLinearColor PanelColor(0.03f, 0.035f, 0.03f, 0.88f);
	const FLinearColor TitleColor(0.12f, 0.14f, 0.09f, 0.95f);
	const FLinearColor HeaderText(1.f, 0.85f, 0.4f);
	const FLinearColor NormalText(0.9f, 0.9f, 0.9f);
	const FLinearColor DimText(0.55f, 0.55f, 0.55f);
	const FLinearColor ErrorText(1.f, 0.35f, 0.3f);
	const FLinearColor ButtonColor(0.22f, 0.27f, 0.16f, 1.f);
	const FLinearColor ButtonHover(0.36f, 0.45f, 0.22f, 1.f);
	const FLinearColor ButtonDisabled(0.15f, 0.15f, 0.15f, 1.f);
}

void ASquadHUD::DrawHUD()
{
	Super::DrawHUD();

	Buttons.Reset();
	bPanelVisible = false;
	SquadCards.Reset();
	bSquadBarVisible = false;
	bMinimapVisible = false;
	TacButtons.Reset();
	bTacMapVisible = false;
	ContextItemBoxes.Reset();
	bContextMenuVisible = false;
	DebugButtonBoxes.Reset();
	bDebugMenuVisible = false;
	bAudioVisible = false;

	ASquadPlayerController* PC = Cast<ASquadPlayerController>(GetOwningPlayerController());
	if (!PC || !Canvas)
	{
		return;
	}
	LastScale = FMath::Clamp(Canvas->ClipY / 1080.f, 0.9f, 1.5f);
	UpdatePlanNotices(*PC);
	DrawMovePaths(*PC); // Under the aim lines and panels.
	DrawAimLines(*PC); // Under the panels.
	if (!PC->IsTacticalMapOpen())
	{
		DrawBattlefieldOverlays(*PC); // Move range, cover preview, cover vision cones: under the plan marks.
		DrawWorldPlan(*PC); // GDD §13.2: plan marks and pings on the battlefield, under the panels.
		DrawBuildOverlays(*PC); // GDD §13.3: placement progress over soldiers.
	}
	DrawKillFeed(*PC);
	if (PC->IsTacticalMapOpen())
	{
		// GDD §13.1: the maximised map replaces the squad bar, minimap and loot menu while it is open.
		DrawTacticalMap(*PC);
	}
	else
	{
		DrawSquadBar(*PC); // Before the loot menu, which sits on top of it.
		DrawMinimap(*PC); // After the squad bar: sized to stay clear of it.
		if (PC->IsLootMenuOpen())
		{
			DrawLootMenu(*PC);
		}
		if (PC->IsContextMenuOpen())
		{
			DrawContextMenu(*PC); // On top of the panels, at the cursor.
		}
	}
	DrawCommandUI(*PC); // Command wheel over the panels.
	DrawPlanNotices();
	DrawAudioControls(*PC);
	DrawMatchStatus(*PC); // On top of everything...
	DrawDebugMenu(*PC); // ...but the debug menu.
	DrawJoinError();
}

void ASquadHUD::DrawJoinError()
{
	// Only on the local map the engine falls back to after a failed join; a connected client has joined fine.
	const USquadJoinError* JoinError = GetNetMode() == NM_Standalone ? USquadJoinError::Get(this) : nullptr;
	if (!JoinError || JoinError->GetMessage().IsEmpty())
	{
		return;
	}
	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	UFont* Small = GEngine ? GEngine->GetSmallFont() : nullptr;
	const float S = FMath::Clamp(Canvas->ClipY / 1080.f, 0.9f, 1.5f);
	TArray<FString> Lines;
	JoinError->GetMessage().ParseIntoArray(Lines, TEXT(". "));
	Lines.Insert(TEXT("NOT CONNECTED: this is an offline copy of the map."), 0);

	float BoxW = 0.f;
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		float TW = 0.f, TH = 0.f;
		GetTextSize(Lines[i], TW, TH, i == 0 ? Font : Small, i == 0 ? S : 1.2f * S);
		BoxW = FMath::Max(BoxW, TW);
	}
	BoxW += 40.f * S;
	const float LineH = 26.f * S;
	const float BoxH = 24.f * S + LineH * Lines.Num();
	const float X0 = (Canvas->ClipX - BoxW) * 0.5f;
	const float Y0 = Canvas->ClipY * 0.3f;
	DrawRect(FLinearColor(0.08f, 0.01f, 0.01f, 0.85f), X0, Y0, BoxW, BoxH);
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		float TW = 0.f, TH = 0.f;
		UFont* LineFont = i == 0 ? Font : Small;
		const float Scale = i == 0 ? S : 1.2f * S;
		GetTextSize(Lines[i], TW, TH, LineFont, Scale);
		DrawText(Lines[i], i == 0 ? FLinearColor(1.f, 0.45f, 0.35f) : FLinearColor(0.95f, 0.93f, 0.9f),
			(Canvas->ClipX - TW) * 0.5f, Y0 + 12.f * S + LineH * i, LineFont, Scale);
	}
}

bool ASquadHUD::IsOverSquadBar(const FVector2D& ScreenPosition) const
{
	return bSquadBarVisible && SquadBarBox.IsInside(ScreenPosition);
}

ASoldier* ASquadHUD::GetSquadCardAt(const FVector2D& ScreenPosition) const
{
	for (const FSquadCard& Card : SquadCards)
	{
		if (Card.Box.IsInside(ScreenPosition))
		{
			return Card.Soldier.Get();
		}
	}
	return nullptr;
}

// ------------------------------------------------------------------------------------------------ Squad bar

namespace SquadBar
{
	// Sizes at 1080p; scaled with the screen height.
	constexpr float CardWidth = 244.f;
	constexpr float CardHeight = 128.f;
	constexpr float Gap = 8.f;
	constexpr float BottomMargin = 12.f;
	/** Selected cards sit this much higher than the rest. */
	constexpr float SelectedRaise = 6.f;
	constexpr float Pad = 8.f;

	// Fairly transparent so the battlefield shows through; selected cards are a little more solid.
	const FLinearColor CardBg(0.02f, 0.025f, 0.02f, 0.38f);
	const FLinearColor CardBgHover(0.04f, 0.05f, 0.035f, 0.48f);
	const FLinearColor CardBgSelected(0.10f, 0.11f, 0.05f, 0.58f);
	const FLinearColor CardBgDead(0.02f, 0.02f, 0.02f, 0.22f);
	const FLinearColor BorderIdle(1.f, 1.f, 1.f, 0.10f);
	const FLinearColor BorderSelected(1.f, 0.85f, 0.4f, 0.95f);
	const FLinearColor BorderDanger(1.f, 0.25f, 0.2f, 0.9f);

	const FLinearColor Text(0.92f, 0.92f, 0.9f, 1.f);
	const FLinearColor Dim(0.6f, 0.6f, 0.58f, 0.9f);
	const FLinearColor Gold(1.f, 0.85f, 0.4f, 1.f);
	const FLinearColor Good(0.55f, 0.88f, 0.45f, 1.f);
	const FLinearColor Warn(1.f, 0.85f, 0.25f, 1.f);
	const FLinearColor Danger(1.f, 0.32f, 0.27f, 1.f);
	const FLinearColor Shock(0.85f, 0.35f, 1.f, 1.f);
	const FLinearColor Info(0.5f, 0.78f, 1.f, 1.f);
	const FLinearColor Orange(1.f, 0.58f, 0.22f, 1.f);
	/** GDD §5.3 hold fire: the top stripe and the solid chip. */
	const FLinearColor HoldFire(0.86f, 0.16f, 0.12f, 1.f);

	const FLinearColor PipLoaded(1.f, 0.8f, 0.35f, 0.95f);
	const FLinearColor PipEmpty(1.f, 1.f, 1.f, 0.12f);
	const FLinearColor TrackBg(1.f, 1.f, 1.f, 0.10f);

	FLinearColor WithAlpha(FLinearColor C, float A) { C.A = A; return C; }

	const TCHAR* CoverName(ECoverType Type)
	{
		switch (Type)
		{
		case ECoverType::Soft:       return TEXT("HAY");
		case ECoverType::LowWall:    return TEXT("LOW WALL");
		case ECoverType::WallCorner: return TEXT("CORNER");
		case ECoverType::Window:     return TEXT("WINDOW");
		case ECoverType::Tree:       return TEXT("TREE");
		default:                     return TEXT("");
		}
	}
}

void ASquadHUD::DrawAimLines(ASquadPlayerController& PC)
{
	using namespace SquadBar;

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	const float Time = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
	const float S = FMath::Clamp(Canvas->ClipY / 1080.f, 0.9f, 1.5f);

	for (const ASoldier* Soldier : PC.GetSquad())
	{
		const ASoldier* Target = Soldier ? Soldier->GetAttackTarget() : nullptr;
		if (!Target || Soldier->IsDead() || Target->IsDead() || Target->IsHidden())
		{
			continue;
		}
		// Project both ends; skip if either is behind the camera.
		const FVector From3 = Canvas->Project(Soldier->GetMuzzleLocation(), true);
		const FVector To3 = Canvas->Project(Target->GetAimPoint(), true);
		if (From3.Z <= 0.f || To3.Z <= 0.f)
		{
			continue;
		}
		const FVector2D From(From3.X, From3.Y);
		const FVector2D To(To3.X, To3.Y);
		const FVector2D Span = To - From;
		const float Length = Span.Size();
		if (Length < 4.f)
		{
			continue;
		}
		const FVector2D Dir = Span / Length;

		const bool bReloading = Soldier->IsReloading();
		const bool bAiming = Soldier->IsAiming();
		FLinearColor Color = bReloading ? WithAlpha(Dim, 0.45f) : (bAiming ? WithAlpha(Gold, 0.7f) : WithAlpha(Danger, 0.8f));
		if (Soldier->IsSelected())
		{
			Color.A = FMath::Min(1.f, Color.A + 0.2f);
		}
		const float Thickness = (bAiming || bReloading ? 1.5f : 2.f) * S;

		if (bAiming || bReloading)
		{
			// Dashes flowing from the muzzle toward the target.
			const float Dash = 10.f * S, GapLen = 7.f * S, Period = Dash + GapLen;
			const float Offset = bReloading ? 0.f : FMath::Fmod(Time * 60.f * S, Period);
			for (float D = Offset - Period; D < Length; D += Period)
			{
				const float A = FMath::Max(0.f, D), B = FMath::Min(Length, D + Dash);
				if (B > A)
				{
					DrawLine(From.X + Dir.X * A, From.Y + Dir.Y * A, From.X + Dir.X * B, From.Y + Dir.Y * B, Color, Thickness);
				}
			}
		}
		else
		{
			DrawLine(From.X, From.Y, To.X, To.Y, Color, Thickness);
		}

		// Target end: corner brackets round the aim point, closing in while aiming, plus the hit chance.
		const float Close = bAiming ? 0.5f + 0.5f * FMath::Sin(Time * 8.f) : 0.f;
		// Sized to the target on screen (about a body-width box), never smaller than a readable minimum.
		const FVector Top3 = Canvas->Project(Target->GetAimPoint() + FVector(0.f, 0.f, 40.f), true);
		const float TargetR = Top3.Z > 0.f ? FVector2D::Distance(FVector2D(Top3.X, Top3.Y), To) : 0.f;
		const float BaseR = FMath::Max(10.f * S, TargetR);
		const float R = BaseR * (1.f + 0.35f * Close), L = FMath::Max(5.f * S, BaseR * 0.4f);
		for (const FVector2D Corner : { FVector2D(-1, -1), FVector2D(1, -1), FVector2D(1, 1), FVector2D(-1, 1) })
		{
			const FVector2D P = To + Corner * R;
			DrawLine(P.X, P.Y, P.X - Corner.X * L, P.Y, Color, Thickness);
			DrawLine(P.X, P.Y, P.X, P.Y - Corner.Y * L, Color, Thickness);
		}
		const float HitChance = Soldier->GetCurrentHitChance();
		if (HitChance >= 0.f)
		{
			const FString Label = FString::Printf(TEXT("%d: %.0f%%"), Soldier->GetSquadNumber(), HitChance);
			DrawText(Label, WithAlpha(Color, 1.f), To.X + R + 4.f * S, To.Y - R - 4.f * S + (Soldier->GetSquadNumber() - 1) * 14.f * S,
				Font, 0.9f * S);
		}
	}
}

void ASquadHUD::DrawMovePaths(ASquadPlayerController& PC)
{
	using namespace SquadBar;

	if (PC.IsTacticalMapOpen())
	{
		return;
	}
	const float Time = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
	const float S = FMath::Clamp(Canvas->ClipY / 1080.f, 0.9f, 1.5f);
	// Same red family as the flashing destination square, so the line reads as leading to it.
	const FLinearColor PathColor(1.f, 0.3f, 0.25f, 1.f);
	TArray<FVector> Route;
	TArray<FVector2D> Screen;

	// Move orders are private (like the destination square): only the player's own squad is drawn.
	for (const ASoldier* Soldier : PC.GetSquad())
	{
		if (!Soldier || Soldier->IsDead() || !Soldier->IsMoving())
		{
			continue;
		}
		Soldier->GetRemainingRoute(Route);
		if (Route.Num() < 2)
		{
			continue;
		}
		// Project the route just above the ground; skip the line if any point is behind the camera.
		Screen.Reset();
		bool bOnScreen = true;
		for (const FVector& P : Route)
		{
			const FVector P3 = Canvas->Project(P + FVector(0.f, 0.f, 3.f), true);
			if (P3.Z <= 0.f)
			{
				bOnScreen = false;
				break;
			}
			Screen.Add(FVector2D(P3.X, P3.Y));
		}
		if (!bOnScreen)
		{
			continue;
		}

		const FLinearColor Color = WithAlpha(PathColor, Soldier->IsSelected() ? 0.85f : 0.5f);
		const float Thickness = (Soldier->IsSelected() ? 2.5f : 1.8f) * S;

		// Dashes flowing along the route toward the destination, continuous round the corners.
		const float Dash = 9.f * S, GapLen = 6.f * S, Period = Dash + GapLen;
		float Travelled = Period - FMath::Fmod(Time * 45.f * S, Period);
		for (int32 I = 0; I + 1 < Screen.Num(); ++I)
		{
			const FVector2D A = Screen[I], B = Screen[I + 1];
			const float Length = FVector2D::Distance(A, B);
			if (Length < KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const FVector2D Dir = (B - A) / Length;
			const float Phase = FMath::Fmod(Travelled, Period); // Where this leg starts in the dash pattern.
			for (float D = -Phase; D < Length; D += Period)
			{
				const float From = FMath::Max(0.f, D), To = FMath::Min(Length, D + Dash);
				if (To > From)
				{
					DrawLine(A.X + Dir.X * From, A.Y + Dir.Y * From, A.X + Dir.X * To, A.Y + Dir.Y * To, Color, Thickness);
				}
			}
			Travelled += Length;
		}

		// Arrowhead on the last leg, pointing into the destination cell.
		const FVector2D Tip = Screen.Last();
		FVector2D Back = Screen[Screen.Num() - 2] - Tip;
		if (Back.Normalize())
		{
			const float Head = 9.f * S;
			const FVector2D Side(-Back.Y, Back.X);
			const FVector2D L = Tip + (Back + Side * 0.6f) * Head, R = Tip + (Back - Side * 0.6f) * Head;
			DrawLine(Tip.X, Tip.Y, L.X, L.Y, Color, Thickness);
			DrawLine(Tip.X, Tip.Y, R.X, R.Y, Color, Thickness);
		}
	}
}

void ASquadHUD::DrawSquadBar(ASquadPlayerController& PC)
{
	using namespace SquadBar;

	TArray<ASoldier*, TInlineAllocator<4>> Soldiers;
	for (ASoldier* Soldier : PC.GetSquad())
	{
		if (Soldier)
		{
			Soldiers.Add(Soldier);
		}
	}
	if (Soldiers.Num() == 0)
	{
		return;
	}

	const float S = FMath::Clamp(Canvas->ClipY / 1080.f, 0.9f, 1.5f);
	const float W = CardWidth * S;
	const float H = CardHeight * S;
	const float G = Gap * S;
	const float Total = Soldiers.Num() * W + (Soldiers.Num() - 1) * G;
	float X = FMath::Max(0.f, (Canvas->ClipX - Total) * 0.5f);
	const float BaseY = Canvas->ClipY - BottomMargin * S - H;
	SquadBarBox = FBox2D(FVector2D(X, BaseY - SelectedRaise * S), FVector2D(X + Total, BaseY + H));
	bSquadBarVisible = true;

	float MouseX = 0.f, MouseY = 0.f;
	PC.GetMousePosition(MouseX, MouseY);
	const FVector2D Mouse(MouseX, MouseY);

	for (ASoldier* Soldier : Soldiers)
	{
		const float Y = Soldier->IsSelected() ? BaseY - SelectedRaise * S : BaseY;
		FSquadCard Card;
		Card.Box = FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
		Card.Soldier = Soldier;
		DrawSquadCard(*Soldier, X, Y, W, H, S, Card.Box.IsInside(Mouse));
		SquadCards.Add(Card);
		X += W + G;
	}
}

void ASquadHUD::DrawSquadCard(const ASoldier& Soldier, float X, float Y, float W, float H, float S, bool bHover)
{
	using namespace SquadBar;

	UFont* Small = GEngine ? GEngine->GetSmallFont() : nullptr;
	// The engine's Medium font renders smaller than Small, so everything is the Small font at different scales.
	const float Time = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * 10.f);

	// Card-local helpers, in unscaled 1080p units.
	auto Rect = [&](float LX, float LY, float LW, float LH, const FLinearColor& C) { DrawRect(C, X + LX * S, Y + LY * S, LW * S, LH * S); };
	auto TextSize = [&](const FString& Str, UFont* Font, float FontScale = 1.f)
	{
		float TW = 0.f, TH = 0.f;
		GetTextSize(Str, TW, TH, Font, S * FontScale);
		return FVector2D(TW / S, TH / S);
	};
	auto Txt = [&](const FString& Str, const FLinearColor& C, float LX, float LY, UFont* Font, float FontScale = 1.f)
	{
		DrawText(Str, C, X + LX * S, Y + LY * S, Font, S * FontScale);
	};
	auto TxtRight = [&](const FString& Str, const FLinearColor& C, float RightX, float LY, UFont* Font, float FontScale = 1.f)
	{
		Txt(Str, C, RightX - TextSize(Str, Font, FontScale).X, LY, Font, FontScale);
	};
	auto Border = [&](const FLinearColor& C, float T)
	{
		const float LW = W / S, LH = H / S;
		Rect(0.f, 0.f, LW, T, C);
		Rect(0.f, LH - T, LW, T, C);
		Rect(0.f, T, T, LH - 2.f * T, C);
		Rect(LW - T, T, T, LH - 2.f * T, C);
	};

	const float LW = W / S; // Card width in local units.
	const bool bDead = Soldier.IsDead();
	const bool bSelected = Soldier.IsSelected();
	const ESoldierHealth Health = Soldier.GetHealthState();

	// ---- Background, damage flash and health accent strip
	Rect(0.f, 0.f, LW, CardHeight, bDead ? CardBgDead : (bSelected ? CardBgSelected : (bHover ? CardBgHover : CardBg)));
	const float Flash = Soldier.GetDamageFlash();
	if (Flash > 0.f)
	{
		Rect(0.f, 0.f, LW, CardHeight, WithAlpha(Danger, 0.45f * Flash));
	}
	FLinearColor Accent = Good;
	FString HealthText = TEXT("ACTIVE");
	switch (Health)
	{
	case ESoldierHealth::Stunned: Accent = Warn;   HealthText = FString::Printf(TEXT("STUNNED %.1fs"), Soldier.GetStunRemaining()); break;
	case ESoldierHealth::Injured:
		Accent = Danger;
		// GDD §5.1: seconds until it bleeds out ("paused" while a medic is applying a medkit).
		HealthText = Soldier.GetBleedOutRemaining() <= 0.f ? FString(TEXT("INJURED"))
			: Soldier.IsBleedOutPaused() ? FString::Printf(TEXT("INJURED %ds (MEDIC)"), FMath::CeilToInt(Soldier.GetBleedOutRemaining()))
			: FString::Printf(TEXT("BLEEDING %ds"), FMath::CeilToInt(Soldier.GetBleedOutRemaining()));
		break;
	case ESoldierHealth::Dead:    Accent = Dim;    HealthText = TEXT("KIA"); break;
	default: break;
	}
	Rect(0.f, 0.f, 3.f, CardHeight, WithAlpha(Accent, bDead ? 0.3f : 0.85f));
	const bool bHoldFire = Soldier.IsHoldingFire();
	if (bHoldFire)
	{
		Rect(0.f, 0.f, LW, 5.f, HoldFire); // Red stripe along the top: weapon safe (GDD §5.3). Thicker than the selected border.
	}

	// ---- Header: squad number, rifle, health
	const FLinearColor NumberBg = bDead ? FLinearColor(0.2f, 0.2f, 0.2f, 0.6f) : WithAlpha(Soldier.SoldierColor, 0.85f);
	Rect(Pad, 6.f, 24.f, 24.f, NumberBg);
	const FString Number = FString::FromInt(Soldier.GetSquadNumber());
	const FVector2D NumSize = TextSize(Number, Small, 1.2f);
	Txt(Number, FLinearColor::White, Pad + (24.f - NumSize.X) * 0.5f, 6.f + (24.f - NumSize.Y) * 0.5f, Small, 1.2f);

	const UWeaponData* Weapon = Soldier.GetWeaponData();
	Txt(SquadLoot::WeaponName(Weapon), bDead ? Dim : Text, Pad + 32.f, 11.f, Small);
	TxtRight(HealthText, Health == ESoldierHealth::Active ? WithAlpha(Good, 0.85f) : Accent, LW - Pad, 11.f, Small);

	if (bDead)
	{
		const FString Kia = TEXT("KILLED IN ACTION");
		const FVector2D Size = TextSize(Kia, Small);
		Txt(Kia, Dim, (LW - Size.X) * 0.5f, (CardHeight - Size.Y) * 0.5f + 8.f, Small);
		Border(BorderIdle, 1.f);
		return;
	}

	// ---- Ammo: loaded count / spare rounds, then a pip per round in the magazine (or the reload bar)
	const int32 Loaded = Soldier.GetMagazineAmmo();
	const int32 Spare = Soldier.GetReserveAmmo();
	const int32 Capacity = Weapon ? FMath::Max(1, Weapon->MagazineCapacity) : 8;
	const float AmmoY = 36.f;
	const FString LoadedText = FString::FromInt(Loaded);
	const FLinearColor LoadedColor = Loaded == 0 ? Danger : (Loaded * 4 <= Capacity ? Warn : Text);
	Txt(LoadedText, LoadedColor, Pad + 4.f, AmmoY - 4.f, Small, 1.6f);
	const float SpareX = Pad + 4.f + TextSize(LoadedText, Small, 1.6f).X + 3.f;
	const FString SpareText = FString::Printf(TEXT("/ %d"), Spare);
	Txt(SpareText, Spare == 0 ? Danger : Dim, SpareX, AmmoY + 4.f, Small);

	const float PipX = FMath::Max(84.f, SpareX + TextSize(SpareText, Small).X + 8.f);
	const float PipArea = LW - Pad - PipX;
	const float PipH = 14.f;
	const float PipY = AmmoY + 3.f;
	if (Soldier.IsReloading())
	{
		Rect(PipX, PipY, PipArea, PipH, TrackBg);
		Rect(PipX, PipY, PipArea * Soldier.GetReloadProgress(), PipH, WithAlpha(Info, 0.75f));
		const FString Reload = TEXT("RELOADING");
		const FVector2D Size = TextSize(Reload, Small, 0.85f);
		Txt(Reload, FLinearColor::White, PipX + (PipArea - Size.X) * 0.5f, PipY + (PipH - Size.Y) * 0.5f, Small, 0.85f);
	}
	else if (Loaded == 0 && Spare == 0)
	{
		Rect(PipX, PipY, PipArea, PipH, WithAlpha(Danger, 0.18f + 0.12f * Pulse));
		const FString NoAmmo = TEXT("NO AMMO");
		const FVector2D Size = TextSize(NoAmmo, Small, 0.85f);
		Txt(NoAmmo, Danger, PipX + (PipArea - Size.X) * 0.5f, PipY + (PipH - Size.Y) * 0.5f, Small, 0.85f);
	}
	else if (Capacity > MaxAmmoPips)
	{
		// A machine-gun belt (GDD §6.1) has too many rounds for pips: a bar filled by the rounds left in it.
		Rect(PipX, PipY, PipArea, PipH, PipEmpty);
		Rect(PipX, PipY, PipArea * FMath::Clamp(static_cast<float>(Loaded) / Capacity, 0.f, 1.f), PipH, PipLoaded);
	}
	else
	{
		const float PipGap = 3.f;
		const float PipW = FMath::Min(12.f, (PipArea - (Capacity - 1) * PipGap) / Capacity);
		for (int32 i = 0; i < Capacity; ++i)
		{
			Rect(PipX + i * (PipW + PipGap), PipY, PipW, PipH, i < Loaded ? PipLoaded : PipEmpty);
		}
	}

	// ---- Kit: grenades, medkits, stance
	const float KitY = 64.f;
	// Grenade: olive body with a darker lever.
	Rect(Pad + 4.f, KitY + 3.f, 9.f, 11.f, FLinearColor(0.42f, 0.48f, 0.26f, 0.95f));
	Rect(Pad + 6.f, KitY + 1.f, 5.f, 2.f, FLinearColor(0.25f, 0.28f, 0.16f, 0.95f));
	const int32 Grenades = Soldier.GetGrenades();
	Txt(FString::Printf(TEXT("x%d"), Grenades), Grenades > 0 ? Text : Dim, Pad + 17.f, KitY, Small);
	// Medkit: pale box with a red cross.
	const float MedX = Pad + 62.f;
	Rect(MedX, KitY + 2.f, 12.f, 12.f, FLinearColor(0.9f, 0.88f, 0.82f, 0.9f));
	Rect(MedX + 5.f, KitY + 4.f, 2.f, 8.f, FLinearColor(0.85f, 0.12f, 0.1f, 1.f));
	Rect(MedX + 2.f, KitY + 7.f, 8.f, 2.f, FLinearColor(0.85f, 0.12f, 0.1f, 1.f));
	const int32 Medkits = Soldier.GetMedkits();
	Txt(FString::Printf(TEXT("x%d"), Medkits), Medkits > 0 ? Text : Dim, MedX + 16.f, KitY, Small);

	static const TCHAR* StanceNames[] = { TEXT("STANDING"), TEXT("CROUCHED"), TEXT("PRONE") };
	TxtRight(StanceNames[static_cast<int32>(Soldier.GetEffectiveStance())], Dim, LW - Pad, KitY, Small);

	// ---- Stress bar (GDD §8.1), tier thresholds ticked
	const float StressY = 86.f;
	Txt(TEXT("STRESS"), Dim, Pad + 4.f, StressY - 2.f, Small, 0.85f);
	const USoldierData* Data = Soldier.GetSoldierData();
	const float MaxStress = Data ? FMath::Max(1.f, Data->MaxStress) : 100.f;
	const float BarX = 56.f;
	const float BarW = LW - Pad - BarX;
	const float Ratio = FMath::Clamp(Soldier.GetStress() / MaxStress, 0.f, 1.f);
	FLinearColor StressColor = Good;
	switch (Soldier.GetStressTier())
	{
	case EStressTier::Minor:   StressColor = Warn; break;
	case EStressTier::Major:   StressColor = Orange; break;
	case EStressTier::Shocked: StressColor = WithAlpha(Shock, 0.7f + 0.3f * Pulse); break;
	default: break;
	}
	Rect(BarX, StressY, BarW, 6.f, TrackBg);
	Rect(BarX, StressY, BarW * Ratio, 6.f, StressColor);
	if (Data)
	{
		for (const float Threshold : { Data->MinorStressThreshold, Data->MajorStressThreshold })
		{
			Rect(BarX + BarW * FMath::Clamp(Threshold / MaxStress, 0.f, 1.f) - 0.5f, StressY - 1.f, 1.f, 8.f, FLinearColor(0.f, 0.f, 0.f, 0.6f));
		}
	}

	// ---- Status chips: the most urgent first, as many as fit on one row
	TArray<TPair<FString, FLinearColor>, TInlineAllocator<8>> Chips;
	if (Soldier.IsShocked())
	{
		Chips.Emplace(TEXT("SHOCKED"), Shock);
	}
	if (Soldier.IsUnderFire())
	{
		Chips.Emplace(TEXT("UNDER FIRE"), WithAlpha(Danger, 0.6f + 0.4f * Pulse));
	}
	if (Soldier.IsBeingDragged())
	{
		Chips.Emplace(TEXT("DRAGGED"), Info);
	}
	if (const ASoldier* Dragged = Soldier.GetDraggedSoldier())
	{
		Chips.Emplace(FString::Printf(TEXT("DRAGGING %d"), Dragged->GetSquadNumber()), Info);
	}
	if (const ASoldier* Patient = Soldier.GetMedkitPatient())
	{
		// GDD §5.2: treating an injured ally (or walking over to).
		Chips.Emplace(Soldier.IsApplyingMedkit()
			? FString::Printf(TEXT("MEDKIT %d  %.0fs"), Patient->GetSquadNumber(), FMath::CeilToFloat(Soldier.GetMedkitRemaining()))
			: FString::Printf(TEXT("TO MEDIC %d"), Patient->GetSquadNumber()), Good);
	}
	if (const ASoldier* Medic = Soldier.IsInjured() ? Soldier.GetTreatedBy() : nullptr)
	{
		Chips.Emplace(Medic->IsApplyingMedkit()
			? FString::Printf(TEXT("TREATED  %.0fs"), FMath::CeilToFloat(Medic->GetMedkitRemaining()))
			: FString(TEXT("MEDIC COMING")), Good);
	}
	if (Soldier.IsThrowingGrenade())
	{
		Chips.Emplace(TEXT("GRENADE OUT"), Warn);
	}
	if (Soldier.IsSettingUpWeapon())
	{
		// GDD §6.1: the machine gun can't fire until it is set up.
		Chips.Emplace(FString::Printf(TEXT("SETTING UP  %.0fs"), FMath::CeilToFloat(Soldier.GetSetupRemaining())), Warn);
	}
	else if (Soldier.IsWeaponSetUp())
	{
		Chips.Emplace(TEXT("MG SET UP"), Good);
	}
	const float HitChance = Soldier.GetCurrentHitChance();
	if (HitChance >= 0.f)
	{
		Chips.Emplace(FString::Printf(TEXT("FIRING %.0f%%"), HitChance), Gold);
	}
	const ECoverType Cover = Soldier.GetLeaningCoverType();
	if (Cover != ECoverType::None)
	{
		Chips.Emplace(FString::Printf(TEXT("COVER: %s"), CoverName(Cover)), Good);
		if (Soldier.IsSpottedByEnemy())
		{
			Chips.Emplace(TEXT("SPOTTED"), Orange);
		}
	}
	else if (Soldier.IsSpottedByEnemy())
	{
		Chips.Emplace(TEXT("EXPOSED"), Orange);
	}
	if (Soldier.IsMoving())
	{
		Chips.Emplace(TEXT("MOVING"), Info);
	}
	if (Chips.Num() == 0 && !bHoldFire)
	{
		Chips.Emplace(Soldier.CanAct() ? TEXT("READY") : TEXT("DOWN"), Dim);
	}

	const float ChipY = 102.f;
	const float ChipH = 17.f;
	float ChipX = Pad;
	if (bHoldFire)
	{
		// Always first and solid-filled so it never gets pushed off the row or blends in with the other tags.
		const FString HoldText = TEXT("HOLD FIRE");
		const FVector2D Size = TextSize(HoldText, Small, 0.85f);
		const float ChipW = Size.X + 10.f;
		Rect(ChipX, ChipY, ChipW, ChipH, HoldFire);
		Txt(HoldText, FLinearColor::White, ChipX + 5.f, ChipY + (ChipH - Size.Y) * 0.5f - 1.f, Small, 0.85f);
		ChipX += ChipW + 4.f;
	}
	for (const TPair<FString, FLinearColor>& Chip : Chips)
	{
		const FVector2D Size = TextSize(Chip.Key, Small, 0.85f);
		const float ChipW = Size.X + 10.f;
		if (ChipX + ChipW > LW - Pad)
		{
			break;
		}
		Rect(ChipX, ChipY, ChipW, ChipH, WithAlpha(Chip.Value, 0.22f * Chip.Value.A));
		Rect(ChipX, ChipY + ChipH - 2.f, ChipW, 2.f, Chip.Value);
		Txt(Chip.Key, WithAlpha(Chip.Value, 1.f), ChipX + 5.f, ChipY + (ChipH - Size.Y) * 0.5f - 1.f, Small, 0.85f);
		ChipX += ChipW + 4.f;
	}

	// ---- Border: gold when selected, flashing red while under fire
	if (bSelected)
	{
		Border(BorderSelected, 2.f);
	}
	else if (Soldier.IsUnderFire())
	{
		Border(WithAlpha(BorderDanger, 0.35f + 0.55f * Pulse), 1.f);
	}
	else
	{
		Border(bHover ? WithAlpha(BorderIdle, 0.25f) : BorderIdle, 1.f);
	}
}

// ------------------------------------------------------------------------------------------------ Kill feed

namespace KillFeed
{
	// Sizes at 1080p; scaled with the screen height like the squad bar.
	constexpr float RightMargin = 16.f;
	constexpr float TopMargin = 16.f;
	constexpr float FeedRowHeight = 40.f;
	constexpr float RowGap = 4.f;
	constexpr float PadX = 10.f;
	/** Space between the killer, the weapon column and the victim. */
	constexpr float ColumnGap = 12.f;
	constexpr int32 MaxRows = 6;
	/** Seconds a line stays up, the last FadeTime of them fading out. */
	constexpr float Lifetime = 8.f;
	constexpr float FadeTime = 1.f;
	/** Seconds a new line takes to slide in from the right. */
	constexpr float SlideTime = 0.15f;

	const FLinearColor RowBg(0.02f, 0.025f, 0.02f, 0.55f);
	const FLinearColor RankText(0.85f, 0.85f, 0.82f, 1.f);
	const FLinearColor FriendlyName(0.62f, 0.8f, 1.f, 1.f);
	const FLinearColor EnemyName(1.f, 0.36f, 0.3f, 1.f);
	const FLinearColor ChanceText(0.92f, 0.92f, 0.9f, 1.f);
	const FLinearColor WeaponText(0.6f, 0.6f, 0.58f, 1.f);
	const FLinearColor IconLight(0.88f, 0.88f, 0.85f, 1.f);
	const FLinearColor IconDark(0.45f, 0.45f, 0.42f, 1.f);

	FLinearColor Faded(FLinearColor C, float Alpha) { C.A *= Alpha; return C; }

	/** "1SG [LER]" or just "PVT". */
	FString RankLine(const FKillFeedPlayer& P)
	{
		return P.Regiment.IsEmpty() ? P.Rank : FString::Printf(TEXT("%s [%s]"), *P.Rank, *P.Regiment);
	}

	/** "21%", or one decimal place below 10% when it isn't a whole number ("2.5%"). */
	FString ChanceLabel(float Percent)
	{
		const bool bWhole = FMath::IsNearlyEqual(Percent, FMath::RoundToFloat(Percent), 0.05f);
		return (Percent < 10.f && !bWhole) ? FString::Printf(TEXT("%.1f%%"), Percent) : FString::Printf(TEXT("%.0f%%"), Percent);
	}

	constexpr float RifleIconWidth = 40.f;
	constexpr float GrenadeIconWidth = 16.f;
	constexpr float IconHeight = 18.f;
}

void ASquadHUD::DrawKillFeed(ASquadPlayerController& PC)
{
	using namespace KillFeed;

	const ASquadGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ASquadGameState>() : nullptr;
	if (!GameState || GameState->GetKillFeed().Num() == 0)
	{
		return;
	}
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	const float Now = GetWorld()->GetRealTimeSeconds();
	const float S = FMath::Clamp(Canvas->ClipY / 1080.f, 0.9f, 1.5f);

	auto Measure = [&](const FString& Str, float FontScale)
	{
		float TW = 0.f, TH = 0.f;
		GetTextSize(Str, TW, TH, Font, S * FontScale);
		return FVector2D(TW, TH);
	};

	const TArray<ASquadGameState::FKillFeedItem>& Items = GameState->GetKillFeed();
	float Y = TopMargin * S;
	int32 Rows = 0;
	for (int32 i = Items.Num() - 1; i >= 0 && Rows < MaxRows; --i) // Newest first.
	{
		const ASquadGameState::FKillFeedItem& Item = Items[i];
		const float Age = Now - Item.ReceivedTime;
		if (Age > Lifetime)
		{
			break; // Older ones have expired too.
		}
		const float Alpha = FMath::Clamp((Lifetime - Age) / FadeTime, 0.f, 1.f);
		const float Slide = 1.f - FMath::Clamp(Age / SlideTime, 0.f, 1.f);
		const FKillFeedEntry& E = Item.Entry;

		const FString KillerRank = RankLine(E.Killer);
		const FString VictimRank = RankLine(E.Victim);
		const FString Chance = ChanceLabel(E.ChancePercent);
		const bool bGrenade = E.Method != EKillMethod::Rifle; // Grenades and C4 (GDD §7.2) share the explosive icon.
		FString WeaponLabel = E.Method == EKillMethod::C4 ? FString(TEXT("C4")) : bGrenade ? FString(TEXT("Grenade")) : E.WeaponName;
		if (E.bBledOut)
		{
			WeaponLabel = WeaponLabel.IsEmpty() ? FString(TEXT("Bled out")) : FString::Printf(TEXT("%s - bled out"), *WeaponLabel);
		}

		// Column widths (pixels).
		const float KillerW = FMath::Max(Measure(KillerRank, 0.85f).X, Measure(E.Killer.Name, 1.1f).X);
		const float VictimW = FMath::Max(Measure(VictimRank, 0.85f).X, Measure(E.Victim.Name, 1.1f).X);
		const float IconW = (bGrenade ? GrenadeIconWidth : RifleIconWidth) * S;
		const FVector2D ChanceSize = Measure(Chance, 1.1f);
		const float WeaponTopW = IconW + 5.f * S + ChanceSize.X;
		const float WeaponW = FMath::Max(WeaponTopW, Measure(WeaponLabel, 0.75f).X);

		const float RowW = PadX * S * 2.f + KillerW + VictimW + WeaponW + ColumnGap * S * 2.f;
		const float RowH = FeedRowHeight * S;
		const float X = Canvas->ClipX - RightMargin * S - RowW + Slide * (RowW * 0.5f);

		DrawRect(Faded(RowBg, Alpha), X, Y, RowW, RowH);
		// Accent strip on the left in the killer's colour.
		const bool bKillerFriendly = E.Killer.Team == PC.PlayerTeam;
		const bool bVictimFriendly = E.Victim.Team == PC.PlayerTeam;
		DrawRect(Faded(bKillerFriendly ? FriendlyName : EnemyName, 0.85f * Alpha), X, Y, 2.f * S, RowH);

		const float TopY = Y + 4.f * S;
		const float NameY = Y + 17.f * S;

		// ---- Killer: right-aligned against the weapon column.
		const float KillerRight = X + PadX * S + KillerW;
		DrawText(KillerRank, Faded(RankText, Alpha), KillerRight - Measure(KillerRank, 0.85f).X, TopY, Font, 0.85f * S);
		DrawText(E.Killer.Name, Faded(bKillerFriendly ? FriendlyName : EnemyName, Alpha),
			KillerRight - Measure(E.Killer.Name, 1.1f).X, NameY, Font, 1.1f * S);

		// ---- Weapon: icon and chance on top, weapon name underneath.
		const float WX = KillerRight + ColumnGap * S + (WeaponW - WeaponTopW) * 0.5f;
		const float IconY = Y + 5.f * S;
		auto R = [&](float LX, float LY, float LW, float LH, const FLinearColor& C)
		{
			DrawRect(Faded(C, Alpha), WX + LX * S, IconY + LY * S, LW * S, LH * S);
		};
		if (bGrenade)
		{
			// Frag grenade: ribbed body, fuse cap, lever down the side, pin ring.
			R(2.f, 5.f, 12.f, 13.f, IconLight);
			R(3.f, 4.f, 10.f, 1.f, IconLight);
			R(3.f, 18.f, 10.f, 1.f, IconLight);
			R(2.f, 9.f, 12.f, 1.f, IconDark);
			R(2.f, 13.f, 12.f, 1.f, IconDark);
			R(7.5f, 5.f, 1.f, 13.f, IconDark);
			R(5.f, 1.f, 6.f, 3.f, IconLight);
			R(11.f, 1.f, 3.f, 2.f, IconLight);
			R(13.f, 3.f, 2.f, 8.f, IconLight);
			R(0.f, 0.f, 4.f, 1.f, IconLight);
			R(0.f, 0.f, 1.f, 3.f, IconLight);
			R(0.f, 3.f, 4.f, 1.f, IconLight);
			R(4.f, 0.f, 1.f, 4.f, IconLight);
		}
		else
		{
			// Rifle in profile, muzzle pointing at the victim: stock, wrist, receiver, barrel, sights, trigger guard.
			R(0.f, 6.f, 4.f, 8.f, IconLight);
			R(4.f, 7.f, 6.f, 5.f, IconLight);
			R(10.f, 6.f, 8.f, 4.f, IconLight);
			R(18.f, 7.f, 14.f, 3.f, IconLight);
			R(32.f, 7.5f, 8.f, 1.5f, IconLight);
			R(37.f, 5.5f, 1.f, 2.f, IconLight);
			R(15.f, 4.5f, 3.f, 1.5f, IconLight);
			R(12.f, 10.f, 1.f, 3.f, IconLight);
			R(12.f, 13.f, 4.f, 1.f, IconLight);
			R(16.f, 10.f, 1.f, 4.f, IconLight);
			R(18.f, 9.f, 14.f, 1.f, IconDark);
		}
		DrawText(Chance, Faded(ChanceText, Alpha), WX + IconW + 5.f * S, IconY + (IconHeight * S - ChanceSize.Y) * 0.5f, Font, 1.1f * S);
		const float WeaponColX = KillerRight + ColumnGap * S;
		DrawText(WeaponLabel, Faded(WeaponText, Alpha), WeaponColX + (WeaponW - Measure(WeaponLabel, 0.75f).X) * 0.5f,
			Y + RowH - 4.f * S - Measure(WeaponLabel, 0.75f).Y, Font, 0.75f * S);

		// ---- Victim: left-aligned after the weapon column.
		const float VictimX = WeaponColX + WeaponW + ColumnGap * S;
		DrawText(VictimRank, Faded(RankText, Alpha), VictimX, TopY, Font, 0.85f * S);
		DrawText(E.Victim.Name, Faded(bVictimFriendly ? FriendlyName : EnemyName, Alpha), VictimX, NameY, Font, 1.1f * S);

		Y += RowH + RowGap * S;
		++Rows;
	}
}

// ------------------------------------------------------------------------------------------------ Context menu

int32 ASquadHUD::GetContextMenuItemAt(const FVector2D& ScreenPosition) const
{
	if (!bContextMenuVisible)
	{
		return INDEX_NONE;
	}
	for (int32 i = 0; i < ContextItemBoxes.Num(); ++i)
	{
		if (ContextItemBoxes[i].IsInside(ScreenPosition))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void ASquadHUD::DrawContextMenu(ASquadPlayerController& PC)
{
	const ASquadPlayerController::FContextMenu& Menu = PC.GetContextMenu();
	const ASoldier* Target = Menu.Target.Get();
	if (!Menu.bOpen || !Target || Menu.Items.Num() == 0)
	{
		return;
	}
	const float S = LastScale;
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	UFont* Small = GEngine ? GEngine->GetSmallFont() : Font;
	float MouseX = 0.f, MouseY = 0.f;
	PC.GetMousePosition(MouseX, MouseY);
	const FVector2D Mouse(MouseX, MouseY);

	const float Width = 250.f * S;
	const float TitleH = 26.f * S;
	const float ItemH = 44.f * S;
	const float Pad = 8.f * S;
	const float Height = TitleH + Menu.Items.Num() * ItemH + Pad;

	// Opens down-right of the cursor, flipped to stay on screen.
	float X = Menu.ScreenPosition.X + 2.f;
	float Y = Menu.ScreenPosition.Y + 2.f;
	if (X + Width > Canvas->ClipX)
	{
		X = FMath::Max(0.f, Menu.ScreenPosition.X - Width - 2.f);
	}
	if (Y + Height > Canvas->ClipY)
	{
		Y = FMath::Max(0.f, Menu.ScreenPosition.Y - Height - 2.f);
	}
	ContextMenuBox = FBox2D(FVector2D(X, Y), FVector2D(X + Width, Y + Height));
	bContextMenuVisible = true;

	DrawRect(PanelColor, X, Y, Width, Height);
	DrawRect(TitleColor, X, Y, Width, TitleH);
	DrawRect(ErrorText, X, Y, 3.f * S, TitleH); // Red accent: this soldier is injured.
	const bool bOwnSquad = PC.GetSquad().Contains(Target);
	const FString Title = bOwnSquad ? FString::Printf(TEXT("SOLDIER %d  -  INJURED"), Target->GetSquadNumber()) : FString(TEXT("ALLY  -  INJURED"));
	float TW = 0.f, TH = 0.f;
	GetTextSize(Title, TW, TH, Small, S);
	DrawText(Title, HeaderText, X + Pad + 2.f * S, Y + (TitleH - TH) * 0.5f, Small, S);

	for (int32 i = 0; i < Menu.Items.Num(); ++i)
	{
		const ASquadPlayerController::FContextMenuItem& Item = Menu.Items[i];
		const FBox2D Box(FVector2D(X + Pad * 0.5f, Y + TitleH + i * ItemH + Pad * 0.5f),
			FVector2D(X + Width - Pad * 0.5f, Y + TitleH + (i + 1) * ItemH));
		ContextItemBoxes.Add(Box);
		const bool bHover = Box.IsInside(Mouse);
		if (Item.bEnabled)
		{
			DrawRect(bHover ? ButtonHover : ButtonColor, Box.Min.X, Box.Min.Y, Box.GetSize().X, Box.GetSize().Y);
		}
		else
		{
			DrawRect(ButtonDisabled, Box.Min.X, Box.Min.Y, Box.GetSize().X, Box.GetSize().Y);
		}

		// Small medic cross in front of the label.
		const float IconX = Box.Min.X + 8.f * S;
		const float IconY = Box.Min.Y + 7.f * S;
		const FLinearColor CrossColor = Item.bEnabled ? FLinearColor(0.95f, 0.95f, 0.92f) : DimText;
		DrawRect(CrossColor, IconX, IconY, 14.f * S, 14.f * S);
		const FLinearColor CrossRed = Item.bEnabled ? FLinearColor(0.85f, 0.12f, 0.1f) : FLinearColor(0.35f, 0.35f, 0.35f);
		DrawRect(CrossRed, IconX + 5.5f * S, IconY + 2.f * S, 3.f * S, 10.f * S);
		DrawRect(CrossRed, IconX + 2.f * S, IconY + 5.5f * S, 10.f * S, 3.f * S);

		const float TextX = IconX + 22.f * S;
		DrawText(Item.Label, Item.bEnabled ? NormalText : DimText, TextX, Box.Min.Y + 3.f * S, Font, S);
		DrawText(Item.Detail, Item.bEnabled ? DimText : ErrorText, TextX, Box.Min.Y + 23.f * S, Small, 0.9f * S);
	}
}

bool ASquadHUD::IsOverLootMenu(const FVector2D& ScreenPosition) const
{
	return bPanelVisible && PanelBox.IsInside(ScreenPosition);
}

bool ASquadHUD::GetLootActionAt(const FVector2D& ScreenPosition, bool bSingle, FLootAction& OutAction) const
{
	for (const FButton& Button : Buttons)
	{
		if (!Button.bEnabled || !Button.Box.IsInside(ScreenPosition))
		{
			continue;
		}
		OutAction = FLootAction();
		OutAction.Action = Button.Action;
		OutAction.Item = Button.Item;
		OutAction.WeaponIndex = Button.WeaponIndex;
		OutAction.Source = Button.Source.Get();
		OutAction.AmmoFor = Button.AmmoFor.Get();
		OutAction.Count = bSingle ? Button.SingleCount : 0; // 0 = the whole stack.
		const bool bNeedsSource = Button.Action == ELootActionType::Take || Button.Action == ELootActionType::SwapWeapon;
		return !bNeedsSource || OutAction.Source != nullptr;
	}
	return false;
}

void ASquadHUD::DrawLootMenu(ASquadPlayerController& PC)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	ASoldier* Looter = PC.GetSelectedSoldier();
	FString Reason;
	const bool bCanLoot = SquadLoot::CanLoot(Looter, &Reason);

	float MouseX = 0.f, MouseY = 0.f;
	PC.GetMousePosition(MouseX, MouseY);
	const FVector2D Mouse(MouseX, MouseY);

	auto Header = [](const FString& Text)
	{
		FRow Row;
		Row.Text = Text;
		Row.Color = HeaderText;
		Row.bHeader = true;
		return Row;
	};
	auto Line = [](const FString& Text, const FLinearColor& Color)
	{
		FRow Row;
		Row.Text = Text;
		Row.Color = Color;
		return Row;
	};
	auto WithButton = [bCanLoot](FRow Row, const TCHAR* Label, ELootActionType Action, ELootItemType Item, AActor* Source,
		UWeaponData* AmmoFor, int32 WeaponIndex)
	{
		Row.bHasButton = true;
		Row.Button.Label = Label;
		Row.Button.Action = Action;
		Row.Button.Item = Item;
		Row.Button.Source = Source;
		Row.Button.AmmoFor = AmmoFor;
		Row.Button.WeaponIndex = WeaponIndex;
		Row.Button.SingleCount = Item == ELootItemType::Weapon ? 0 : SquadLoot::GetSingleCount(Item, AmmoFor);
		Row.Button.bEnabled = bCanLoot;
		return Row;
	};
	auto AmmoLine = [](const FLootAmmo& Stack, const UWeaponData* Held)
	{
		const bool bFits = Stack.Weapon == Held;
		return FString::Printf(TEXT("%s rounds   %d%s"), *SquadLoot::WeaponName(Stack.Weapon), Stack.Rounds,
			bFits ? TEXT("") : TEXT("   (other rifle)"));
	};

	TArray<FRow> Left;
	TArray<FRow> Right;
	bool bAnythingToTake = false;

	if (Looter)
	{
		// ---- Left: what the looter carries. Rounds, grenades and medkits can be put down for a squad-mate.
		const FLootInventory& Mine = Looter->GetInventory();
		const UWeaponData* Held = Looter->GetWeaponData();
		Left.Add(Header(TEXT("CARRYING")));
		Left.Add(Line(FString::Printf(TEXT("%s   (%d loaded)"), *SquadLoot::WeaponName(Held), Looter->GetMagazineAmmo()), NormalText));
		for (const FLootAmmo& Stack : Mine.Ammo)
		{
			Left.Add(WithButton(Line(AmmoLine(Stack, Held), Stack.Weapon == Held ? NormalText : DimText),
				TEXT("Drop"), ELootActionType::Drop, ELootItemType::Ammo, nullptr, Stack.Weapon, 0));
		}
		if (Mine.Grenades > 0)
		{
			Left.Add(WithButton(Line(FString::Printf(TEXT("Grenades   %d"), Mine.Grenades), NormalText),
				TEXT("Drop"), ELootActionType::Drop, ELootItemType::Grenade, nullptr, nullptr, 0));
		}
		if (Mine.Medkits > 0)
		{
			Left.Add(WithButton(Line(FString::Printf(TEXT("Medkits   %d"), Mine.Medkits), NormalText),
				TEXT("Drop"), ELootActionType::Drop, ELootItemType::Medkit, nullptr, nullptr, 0));
		}

		// ---- Right: every body and pile on the looter's cell or next to it, in one list.
		const TArray<SquadLoot::FSource> Sources = SquadLoot::GatherSources(Looter);
		if (Sources.Num() == 0)
		{
			Right.Add(Header(TEXT("WITHIN REACH")));
			Right.Add(Line(TEXT("Nothing here."), DimText));
			Right.Add(Line(TEXT("Bodies and dropped items on this cell"), DimText));
			Right.Add(Line(TEXT("or next to it show up here."), DimText));
		}
		for (const SquadLoot::FSource& Source : Sources)
		{
			AActor* Actor = Source.Actor.Get();
			const FLootInventory& Theirs = *Source.Inventory;
			Right.Add(Header(Source.Label + (Source.bOnLooterCell ? TEXT("  (this cell)") : TEXT(""))));
			for (int32 i = 0; i < Theirs.Weapons.Num(); ++i)
			{
				const FLootWeapon& W = Theirs.Weapons[i];
				Right.Add(WithButton(Line(FString::Printf(TEXT("%s   (%d loaded)"), *SquadLoot::WeaponName(W.Weapon), W.LoadedRounds), NormalText),
					TEXT("Swap"), ELootActionType::SwapWeapon, ELootItemType::Weapon, Actor, nullptr, i));
			}
			for (const FLootAmmo& Stack : Theirs.Ammo)
			{
				Right.Add(WithButton(Line(AmmoLine(Stack, Held), Stack.Weapon == Held ? NormalText : DimText),
					TEXT("Take"), ELootActionType::Take, ELootItemType::Ammo, Actor, Stack.Weapon, 0));
				bAnythingToTake = true;
			}
			if (Theirs.Grenades > 0)
			{
				Right.Add(WithButton(Line(FString::Printf(TEXT("Grenades   %d"), Theirs.Grenades), NormalText),
					TEXT("Take"), ELootActionType::Take, ELootItemType::Grenade, Actor, nullptr, 0));
				bAnythingToTake = true;
			}
			if (Theirs.Medkits > 0)
			{
				Right.Add(WithButton(Line(FString::Printf(TEXT("Medkits   %d"), Theirs.Medkits), NormalText),
					TEXT("Take"), ELootActionType::Take, ELootItemType::Medkit, Actor, nullptr, 0));
				bAnythingToTake = true;
			}
			if (Theirs.IsEmpty())
			{
				Right.Add(Line(TEXT("(empty)"), DimText));
			}
		}
	}

	// ---- Panel
	const int32 Rows = FMath::Max3(Left.Num(), Right.Num(), 1);
	const float Height = TitleHeight + Padding + Rows * RowHeight + Padding + FooterHeight;
	const float X = FMath::Max(0.f, (Canvas->ClipX - PanelWidth) * 0.5f);
	// Anchored just above the squad bar so the battlefield in the middle stays visible.
	const float Bottom = bSquadBarVisible ? SquadBarBox.Min.Y - 10.f : Canvas->ClipY - 16.f;
	const float Y = FMath::Max(0.f, Bottom - Height);
	PanelBox = FBox2D(FVector2D(X, Y), FVector2D(X + PanelWidth, Y + Height));
	bPanelVisible = true;

	DrawRect(PanelColor, X, Y, PanelWidth, Height);
	DrawRect(TitleColor, X, Y, PanelWidth, TitleHeight);
	const FString Title = Looter ? FString::Printf(TEXT("LOOT / TRADE  -  SOLDIER %d"), Looter->GetSquadNumber()) : FString(TEXT("LOOT / TRADE"));
	DrawText(Title, HeaderText, X + Padding, Y + 10.f, Font);
	float CloseW = 0.f, CloseH = 0.f;
	const FString CloseHint = TEXT("[Tab] close");
	GetTextSize(CloseHint, CloseW, CloseH, Font);
	DrawText(CloseHint, DimText, X + PanelWidth - Padding - CloseW, Y + 10.f, Font);

	const float BodyY = Y + TitleHeight + Padding;
	if (!Looter)
	{
		DrawText(TEXT("Select a soldier (1-4) to loot or trade with."), NormalText, X + Padding, BodyY, Font);
	}
	DrawColumn(Left, X + Padding, BodyY, ColumnWidth, Mouse, Font);
	DrawColumn(Right, X + PanelWidth - Padding - ColumnWidth, BodyY, ColumnWidth, Mouse, Font);

	// ---- Footer: Take all, why looting is blocked, and how clicks work.
	const float FooterY = Y + Height - FooterHeight;
	if (Looter)
	{
		FButton TakeAll;
		TakeAll.Label = TEXT("Take all");
		TakeAll.Action = ELootActionType::TakeAll;
		TakeAll.bEnabled = bCanLoot && bAnythingToTake;
		TakeAll.Box = FBox2D(FVector2D(X + PanelWidth - Padding - 110.f, FooterY), FVector2D(X + PanelWidth - Padding, FooterY + RowHeight - 2.f));
		DrawButton(TakeAll, Mouse, Font);
	}
	if (Looter && !bCanLoot)
	{
		DrawText(FString::Printf(TEXT("Can't loot: %s"), *Reason), ErrorText, X + Padding, FooterY, Font);
	}
	DrawText(TEXT("Click: whole stack     Shift+click: one (ammo: one clip)     Swap: exchange rifles, loaded rounds included"),
		DimText, X + Padding, FooterY + RowHeight + 6.f, GEngine ? GEngine->GetSmallFont() : Font);
}

void ASquadHUD::DrawColumn(const TArray<FRow>& Rows, float X, float Y, float Width, const FVector2D& Mouse, UFont* Font)
{
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		const FRow& Row = Rows[i];
		const float RowY = Y + i * RowHeight;
		DrawText(Row.Text, Row.Color, X + (Row.bHeader ? 0.f : 12.f), RowY + 3.f, Font);
		if (Row.bHasButton)
		{
			FButton Button = Row.Button;
			Button.Box = FBox2D(FVector2D(X + Width - ButtonWidth, RowY + 1.f), FVector2D(X + Width, RowY + RowHeight - 2.f));
			DrawButton(Button, Mouse, Font);
		}
	}
}

void ASquadHUD::DrawButton(FButton Button, const FVector2D& Mouse, UFont* Font)
{
	const FVector2D Size = Button.Box.GetSize();
	const bool bHover = Button.bEnabled && Button.Box.IsInside(Mouse);
	DrawRect(!Button.bEnabled ? ButtonDisabled : (bHover ? ButtonHover : ButtonColor), Button.Box.Min.X, Button.Box.Min.Y, Size.X, Size.Y);

	float TextW = 0.f, TextH = 0.f;
	GetTextSize(Button.Label, TextW, TextH, Font);
	DrawText(Button.Label, Button.bEnabled ? NormalText : DimText,
		Button.Box.Min.X + (Size.X - TextW) * 0.5f, Button.Box.Min.Y + (Size.Y - TextH) * 0.5f, Font);
	Buttons.Add(MoveTemp(Button));
}

// ------------------------------------------------------------------------------------------------ Match status

void ASquadHUD::DrawMatchStatus(ASquadPlayerController& PC)
{
	const ASquadGameState* GS = GetWorld() ? GetWorld()->GetGameState<ASquadGameState>() : nullptr;
	if (!GS)
	{
		return;
	}
	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	UFont* Small = GEngine ? GEngine->GetSmallFont() : nullptr;
	const float S = FMath::Clamp(Canvas->ClipY / 1080.f, 0.9f, 1.5f);

	auto DrawCentred = [&](const FString& Str, const FLinearColor& Color, float Y, UFont* InFont, float Scale)
	{
		float TW = 0.f, TH = 0.f;
		GetTextSize(Str, TW, TH, InFont, Scale);
		DrawText(Str, Color, (Canvas->ClipX - TW) * 0.5f, Y, InFont, Scale);
		return FVector2D(TW, TH);
	};
	const TCHAR* const Teams[] = { TEXT("ALLIES"), TEXT("AXIS") };

	// ---- Score bar (GDD §11): Allied points left, Axis right, the bar split by share, the match clock between.
	const int32 AlliedScore = GS->GetScore(ESquadTeam::Allies);
	const int32 AxisScore = GS->GetScore(ESquadTeam::Axis);
	float BarBottom = 0.f;
	{
		const float BarW = 360.f * S;
		const float BarH = 8.f * S;
		const float X0 = (Canvas->ClipX - BarW) * 0.5f;
		const float Y0 = 12.f * S;
		const float BoxH = 34.f * S;
		const FLinearColor AlliedColor = MapZoneColors::ForTeam(ESquadTeam::Allies);
		const FLinearColor AxisColor = MapZoneColors::ForTeam(ESquadTeam::Axis);
		DrawRect(FLinearColor(0.02f, 0.025f, 0.02f, 0.6f), X0, Y0, BarW, BoxH);

		// Scores at the ends; your own team's in brighter text.
		const FLinearColor Mine(1.f, 1.f, 1.f), Theirs(0.75f, 0.75f, 0.72f);
		const FString AlliedStr = FString::Printf(TEXT("ALLIES  %d"), AlliedScore);
		const FString AxisStr = FString::Printf(TEXT("%d  AXIS"), AxisScore);
		float TW = 0.f, TH = 0.f;
		const float TextY = Y0 + 4.f * S;
		DrawText(AlliedStr, PC.PlayerTeam == ESquadTeam::Allies ? Mine : Theirs, X0 + 8.f * S, TextY, Small, 1.1f * S);
		GetTextSize(AxisStr, TW, TH, Small, 1.1f * S);
		DrawText(AxisStr, PC.PlayerTeam == ESquadTeam::Axis ? Mine : Theirs, X0 + BarW - 8.f * S - TW, TextY, Small, 1.1f * S);

		if (GS->HasMatchTimeLimit())
		{
			const int32 Secs = FMath::CeilToInt(GS->GetMatchTimeRemaining());
			DrawCentred(FString::Printf(TEXT("%d:%02d"), Secs / 60, Secs % 60),
				Secs <= 60 ? FLinearColor(1.f, 0.75f, 0.35f) : FLinearColor(0.92f, 0.92f, 0.9f), TextY, Small, 1.1f * S);
		}

		// Tug of war: half each at 0-0.
		const int32 Total = AlliedScore + AxisScore;
		const float AlliedShare = Total > 0 ? static_cast<float>(AlliedScore) / Total : 0.5f;
		const float BarY = Y0 + BoxH - BarH;
		DrawRect(AlliedColor, X0, BarY, BarW * AlliedShare, BarH);
		DrawRect(AxisColor, X0 + BarW * AlliedShare, BarY, BarW * (1.f - AlliedShare), BarH);
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.8f), X0 + BarW * 0.5f - 1.f * S, BarY - 2.f * S, 2.f * S, BarH + 2.f * S); // Midpoint.
		BarBottom = Y0 + BoxH;
	}

	// ---- VP hold timer, under the score bar.
	if (GS->IsVPHeld() && !GS->IsMatchOver())
	{
		const int32 Secs = FMath::CeilToInt(GS->GetVPHoldRemaining());
		const FString Str = FString::Printf(TEXT("%s HOLD ALL VICTORY POINTS   %d:%02d"),
			Teams[static_cast<int32>(GS->GetVPHoldingTeam())], Secs / 60, Secs % 60);
		float TW = 0.f, TH = 0.f;
		GetTextSize(Str, TW, TH, Small, 1.2f * S);
		const float Pad = 10.f * S;
		const float Y = BarBottom + 6.f * S;
		const FLinearColor TeamColor = MapZoneColors::ForTeam(GS->GetVPHoldingTeam());
		DrawRect(FLinearColor(0.02f, 0.025f, 0.02f, 0.6f), (Canvas->ClipX - TW) * 0.5f - Pad, Y, TW + Pad * 2.f, TH + Pad);
		DrawRect(TeamColor, (Canvas->ClipX - TW) * 0.5f - Pad, Y + TH + Pad - 2.f * S, TW + Pad * 2.f, 2.f * S);
		DrawCentred(Str, GS->GetVPHoldingTeam() == PC.PlayerTeam ? FLinearColor(0.6f, 0.8f, 1.f) : FLinearColor(1.f, 0.5f, 0.45f),
			Y + Pad * 0.5f, Small, 1.2f * S);
	}

	// ---- Result banner.
	if (!GS->IsMatchOver())
	{
		return;
	}
	const EVictoryReason Reason = GS->GetVictoryReason();
	const bool bDraw = GS->IsDraw();
	const bool bWon = !bDraw && GS->GetWinningTeam() == PC.PlayerTeam;
	const FString Title = bDraw ? TEXT("DRAW") : (bWon ? TEXT("VICTORY") : TEXT("DEFEAT"));
	const TCHAR* WinnerName = Teams[static_cast<int32>(GS->GetWinningTeam())];
	FString Sub;
	switch (Reason)
	{
	case EVictoryReason::Draw:          Sub = TEXT("Both squads wiped out"); break;
	case EVictoryReason::TimeDraw:      Sub = FString::Printf(TEXT("Time up - points level %d : %d"), AlliedScore, AxisScore); break;
	case EVictoryReason::Annihilation:  Sub = FString::Printf(TEXT("%s win - enemy squad annihilated"), WinnerName); break;
	case EVictoryReason::Points:
		Sub = FString::Printf(TEXT("Time up - %s win %d : %d"), WinnerName, FMath::Max(AlliedScore, AxisScore), FMath::Min(AlliedScore, AxisScore));
		break;
	default:                            Sub = FString::Printf(TEXT("%s win - held every Victory Point"), WinnerName); break;
	}
	// Allies / Axis, whatever ended the match.
	const FString Breakdown = FString::Printf(TEXT("ALLIES %d  (kill points %d, VP points %d)      AXIS %d  (kill points %d, VP points %d)"),
		AlliedScore, FMath::RoundToInt(GS->GetKillPoints(ESquadTeam::Allies)), FMath::FloorToInt(GS->GetVPPoints(ESquadTeam::Allies)),
		AxisScore, FMath::RoundToInt(GS->GetKillPoints(ESquadTeam::Axis)), FMath::FloorToInt(GS->GetVPPoints(ESquadTeam::Axis)));
	const FLinearColor BannerColor = bDraw ? FLinearColor(0.9f, 0.9f, 0.85f) : (bWon ? FLinearColor(1.f, 0.85f, 0.4f) : FLinearColor(1.f, 0.32f, 0.27f));

	const float BandH = 180.f * S;
	const float BandY = Canvas->ClipY * 0.32f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.65f), 0.f, BandY, Canvas->ClipX, BandH);
	DrawRect(BannerColor * FLinearColor(1.f, 1.f, 1.f, 0.8f), 0.f, BandY, Canvas->ClipX, 2.f * S);
	DrawRect(BannerColor * FLinearColor(1.f, 1.f, 1.f, 0.8f), 0.f, BandY + BandH - 2.f * S, Canvas->ClipX, 2.f * S);
	DrawCentred(Title, BannerColor, BandY + 22.f * S, Font, 2.6f * S);
	DrawCentred(Sub, FLinearColor(0.92f, 0.92f, 0.9f), BandY + 100.f * S, Small, 1.3f * S);
	DrawCentred(Breakdown, FLinearColor(0.75f, 0.75f, 0.72f), BandY + 135.f * S, Small, 1.1f * S);
}
