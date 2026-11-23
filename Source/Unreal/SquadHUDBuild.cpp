// ASquadHUD: Build sub-wheel and buildable overlays (GDD §13.3).

#include "SquadHUD.h"

#include "CanvasItem.h"
#include "Soldier.h"
#include "SquadHUDDraw.h"
#include "SquadPlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"

using namespace SquadHUDDraw;

namespace BuildHUD
{
	// Same geometry as the main command wheel (SquadHUDTacticalMap.cpp TacMap::Radial*), so the sub-wheel opens in place.
	constexpr float RadialDeadzone = 26.f;
	constexpr float RadialInner = 34.f;
	constexpr float RadialOuter = 122.f;
	constexpr double FeedbackLifetime = 1.8;

	/** C4: an olive block with a red light. */
	void AddC4Icon(TArray<FCanvasUVTri>& Tris, const FVector2D& C, float R, float Alpha)
	{
		const FLinearColor Body(0.55f, 0.6f, 0.3f, Alpha), Light(1.f, 0.15f, 0.1f, Alpha);
		const FVector2D H(R * 0.95f, R * 0.6f);
		AddTri(Tris, C - H, FVector2D(C.X + H.X, C.Y - H.Y), C + H, Body);
		AddTri(Tris, C - H, C + H, FVector2D(C.X - H.X, C.Y + H.Y), Body);
		AddDisc(Tris, C + FVector2D(R * 0.45f, -R * 0.15f), R * 0.22f, Light, 10);
	}

	/** Reinforcement: a wall with an X brace across it. */
	void AddReinforceIcon(TArray<FCanvasUVTri>& Tris, const FVector2D& C, float R, float W, float Alpha)
	{
		const FLinearColor Wall(0.7f, 0.7f, 0.72f, Alpha), Brace(0.75f, 0.5f, 0.25f, Alpha);
		const FVector2D H(R * 0.9f, R * 0.8f);
		AddOutline(Tris, { C - H, FVector2D(C.X + H.X, C.Y - H.Y), C + H, FVector2D(C.X - H.X, C.Y + H.Y) }, W * 0.6f, Wall);
		AddSegment(Tris, C - H * 0.8f, C + H * 0.8f, W, Brace);
		AddSegment(Tris, FVector2D(C.X + H.X * 0.8f, C.Y - H.Y * 0.8f), FVector2D(C.X - H.X * 0.8f, C.Y + H.Y * 0.8f), W, Brace);
	}
}

void ASquadHUD::DrawBuildWheel(ASquadPlayerController& PC)
{
	using namespace BuildHUD;

	const ASquadPlayerController::FRadialMenu& Menu = PC.GetRadialMenu();
	const int32 N = Menu.BuildItems.Num();
	if (N == 0 || !Canvas || !Canvas->DefaultTexture)
	{
		return;
	}
	const float S = LastScale;
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	const FVector2D C = Menu.ScreenCenter;
	const float Inner = RadialInner * S, Outer = RadialOuter * S;
	const float Step = 2.f * PI / N;

	TArray<FCanvasUVTri> Tris;
	for (int32 I = 0; I < N; ++I)
	{
		const ASquadPlayerController::FBuildWheelItem& Item = Menu.BuildItems[I];
		const bool bHover = I == Menu.Hover;
		const float Mid = -0.5f * PI + I * Step;
		const float A0 = Mid - Step * 0.5f + 0.02f, A1 = Mid + Step * 0.5f - 0.02f;
		// Greyed out (and never highlighted green) when it can't be placed here.
		const FLinearColor Fill = !Item.bEnabled ? FLinearColor(0.08f, 0.08f, 0.08f, bHover ? 0.85f : 0.7f)
			: bHover ? FLinearColor(0.42f, 0.5f, 0.26f, 0.92f) : FLinearColor(0.04f, 0.05f, 0.04f, 0.82f);
		const float O = bHover ? Outer * 1.06f : Outer;
		constexpr int32 Sub = 16;
		for (int32 K = 0; K < Sub; ++K)
		{
			const float B0 = FMath::Lerp(A0, A1, static_cast<float>(K) / Sub), B1 = FMath::Lerp(A0, A1, static_cast<float>(K + 1) / Sub);
			const FVector2D D0(FMath::Cos(B0), FMath::Sin(B0)), D1(FMath::Cos(B1), FMath::Sin(B1));
			AddTri(Tris, C + D0 * Inner, C + D0 * O, C + D1 * O, Fill);
			AddTri(Tris, C + D0 * Inner, C + D1 * O, C + D1 * Inner, Fill);
		}
		const FVector2D IconC = C + FVector2D(FMath::Cos(Mid), FMath::Sin(Mid)) * (Inner + Outer) * 0.5f - FVector2D(0.f, 12.f * S);
		const float Alpha = Item.bEnabled ? 1.f : 0.35f;
		if (Item.Kind == EBuildableKind::C4)
		{
			AddC4Icon(Tris, IconC, 11.f * S, Alpha);
		}
		else
		{
			AddReinforceIcon(Tris, IconC, 11.f * S, 3.f * S, Alpha);
		}
	}
	// Centre: closes the sub-wheel.
	AddDisc(Tris, C, RadialDeadzone * S, FLinearColor(0.f, 0.f, 0.f, 0.55f), 20);
	AddRing(Tris, C, RadialDeadzone * S - 1.5f * S, RadialDeadzone * S, FLinearColor(1.f, 1.f, 1.f, 0.3f), 20);
	{
		FCanvasTriangleItem TriItem(Tris, Canvas->DefaultTexture->GetResource());
		TriItem.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(TriItem);
	}

	// Labels: name and squad count, then who places it / why not.
	for (int32 I = 0; I < N; ++I)
	{
		const ASquadPlayerController::FBuildWheelItem& Item = Menu.BuildItems[I];
		const float Mid = -0.5f * PI + I * Step;
		const FVector2D Slice = C + FVector2D(FMath::Cos(Mid), FMath::Sin(Mid)) * (Inner + Outer) * 0.5f;
		const FString Name = FString::Printf(TEXT("%s  x%d"), *Item.Label, Item.SquadCount);
		const FLinearColor NameColor = !Item.bEnabled ? FLinearColor(0.5f, 0.5f, 0.5f) : I == Menu.Hover ? FLinearColor::White : FLinearColor(0.85f, 0.85f, 0.85f);
		float TW = 0.f, TH = 0.f;
		GetTextSize(Name, TW, TH, Font, 0.9f * S);
		DrawText(Name, NameColor, Slice.X - TW * 0.5f, Slice.Y + 2.f * S, Font, 0.9f * S);
		float DW = 0.f, DH = 0.f;
		GetTextSize(Item.Detail, DW, DH, Font, 0.7f * S);
		DrawText(Item.Detail, Item.bEnabled ? FLinearColor(0.7f, 0.85f, 0.6f) : FLinearColor(0.95f, 0.5f, 0.4f),
			Slice.X - DW * 0.5f, Slice.Y + 2.f * S + TH, Font, 0.7f * S);
	}
	{
		const FString Back = TEXT("X");
		float TW = 0.f, TH = 0.f;
		GetTextSize(Back, TW, TH, Font, 1.f * S);
		DrawText(Back, FLinearColor(0.85f, 0.85f, 0.85f), C.X - TW * 0.5f, C.Y - TH * 0.5f, Font, 1.f * S);
	}
	const FString Caption = Menu.BuildItems.IsValidIndex(Menu.Hover)
		? FString::Printf(TEXT("BUILD: %s  -  %s"), *Menu.BuildItems[Menu.Hover].Label, *Menu.BuildItems[Menu.Hover].Detail)
		: FString(TEXT("BUILD  -  click an item    right click / Esc / centre: cancel"));
	float TW = 0.f, TH = 0.f;
	GetTextSize(Caption, TW, TH, Font, 0.9f * S);
	const float CY = C.Y + Outer + 10.f * S;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.7f), C.X - TW * 0.5f - 6.f * S, CY, TW + 12.f * S, TH + 4.f * S);
	DrawText(Caption, FLinearColor::White, C.X - TW * 0.5f, CY + 2.f * S, Font, 0.9f * S);
}

void ASquadHUD::DrawBuildOverlays(ASquadPlayerController& PC)
{
	using namespace BuildHUD;
	if (!Canvas)
	{
		return;
	}
	const float S = LastScale;
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;

	// Placement progress over own soldiers (enemies never see it; a channelling enemy looks like any soldier).
	for (const ASoldier* Soldier : PC.GetSquad())
	{
		if (!Soldier || Soldier->IsDead() || !Soldier->IsBuilding())
		{
			continue;
		}
		FVector2D P;
		if (!PC.ProjectWorldLocationToScreen(Soldier->GetActorLocation() + FVector(0.f, 0.f, 285.f), P, false))
		{
			continue;
		}
		const float W = 64.f * S, H = 7.f * S;
		// Above the soldier's status line, which already reads e.g. "C4  5s".
		const float X = P.X - W * 0.5f, Y = P.Y - 18.f * S;
		const FLinearColor Fill = Soldier->GetBuildKind() == EBuildableKind::C4 ? FLinearColor(1.f, 0.55f, 0.15f) : FLinearColor(0.6f, 0.8f, 1.f);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.7f), X - 1.f * S, Y - 1.f * S, W + 2.f * S, H + 2.f * S);
		DrawRect(Fill, X, Y, W * Soldier->GetBuildProgress(), H);
	}

	// Refusal at the wheel's point ("NO BREACHABLE WALL HERE", "SELECT A SOLDIER", ...), like a grenade's BLOCKED.
	const ASquadPlayerController::FBuildFeedback& Feedback = PC.GetBuildFeedback();
	const double Age = GetWorld() ? GetWorld()->GetRealTimeSeconds() - Feedback.Time : FeedbackLifetime;
	FVector2D P;
	if (!Feedback.Text.IsEmpty() && Age >= 0.0 && Age < FeedbackLifetime && ProjectGround(PC, Feedback.World, P))
	{
		const float Alpha = FMath::Clamp(static_cast<float>((FeedbackLifetime - Age) / 0.4), 0.f, 1.f);
		float TW = 0.f, TH = 0.f;
		GetTextSize(Feedback.Text, TW, TH, Font, 1.f * S);
		const float Y = P.Y - 40.f * S - static_cast<float>(Age) * 12.f * S;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.65f * Alpha), P.X - TW * 0.5f - 6.f * S, Y - 2.f * S, TW + 12.f * S, TH + 4.f * S);
		DrawText(Feedback.Text, FLinearColor(1.f, 0.6f, 0.15f, Alpha), P.X - TW * 0.5f, Y, Font, 1.f * S);
	}
}
