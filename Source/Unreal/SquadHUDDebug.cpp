// Debug menu panel (F6, non-shipping builds), top left. Rows come from ASquadPlayerController (SquadPlayerControllerDebug.cpp).

#include "SquadHUD.h"

#include "SquadPlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

namespace SquadDebugMenu
{
	// Sizes at 1080p; scaled with the screen height.
	constexpr float Margin = 12.f;
	constexpr float Pad = 8.f;
	constexpr float RowHeight = 21.f;
	constexpr float HeaderHeight = 22.f;
	constexpr float LabelWidth = 130.f;
	constexpr float ButtonGap = 4.f;
	constexpr float ButtonPadX = 7.f;

	const FLinearColor Background(0.02f, 0.02f, 0.03f, 0.82f);
	const FLinearColor Border(0.95f, 0.75f, 0.2f, 0.9f);
	const FLinearColor Title(1.f, 0.82f, 0.25f);
	const FLinearColor Info(0.7f, 0.75f, 0.8f);
	const FLinearColor HeaderText(0.95f, 0.6f, 0.25f);
	const FLinearColor LabelText(0.85f, 0.85f, 0.85f);
	const FLinearColor ButtonFill(0.16f, 0.17f, 0.2f, 0.95f);
	const FLinearColor ButtonHover(0.28f, 0.3f, 0.36f, 0.95f);
	const FLinearColor ButtonActive(0.15f, 0.45f, 0.2f, 0.95f);
	const FLinearColor ButtonActiveHover(0.22f, 0.58f, 0.28f, 0.95f);
	const FLinearColor ButtonDisabled(0.1f, 0.1f, 0.11f, 0.9f);
	const FLinearColor DisabledText(0.4f, 0.4f, 0.4f);
}

bool ASquadHUD::GetDebugMenuButtonAt(const FVector2D& ScreenPosition, FIntPoint& OutIndex) const
{
	if (!bDebugMenuVisible)
	{
		return false;
	}
	for (const FDebugButtonBox& Button : DebugButtonBoxes)
	{
		if (Button.bEnabled && Button.Box.IsInside(ScreenPosition))
		{
			OutIndex = Button.Index;
			return true;
		}
	}
	return false;
}

void ASquadHUD::DrawDebugMenu(ASquadPlayerController& PC)
{
#if !UE_BUILD_SHIPPING
	using namespace SquadDebugMenu;
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}
	const float S = LastScale;
	auto Measure = [&](const FString& Str)
	{
		float W = 0.f, H = 0.f;
		GetTextSize(Str, W, H, Font, S);
		return FVector2D(W, H);
	};
	auto Border2D = [&](const FBox2D& Box, const FLinearColor& Color)
	{
		DrawLine(Box.Min.X, Box.Min.Y, Box.Max.X, Box.Min.Y, Color, 1.f);
		DrawLine(Box.Max.X, Box.Min.Y, Box.Max.X, Box.Max.Y, Color, 1.f);
		DrawLine(Box.Max.X, Box.Max.Y, Box.Min.X, Box.Max.Y, Color, 1.f);
		DrawLine(Box.Min.X, Box.Max.Y, Box.Min.X, Box.Min.Y, Color, 1.f);
	};
	const float TextH = Measure(TEXT("Ag")).Y;
	const float X0 = Margin * S, Y0 = Margin * S;

	// The picked click tool, shown even with the menu closed.
	const ASquadPlayerController::EDebugClickTool Tool = PC.GetDebugClickTool();
	const FString ToolHint = Tool == ASquadPlayerController::EDebugClickTool::None ? FString()
		: Tool == ASquadPlayerController::EDebugClickTool::Teleport
			? TEXT("DEBUG TOOL: Teleport - left click the map to move the selection there  (right click / Esc stops)")
			: FString::Printf(TEXT("DEBUG TOOL: %s - left click a soldier  (right click / Esc stops)"), *ASquadPlayerController::GetDebugClickToolName(Tool).ToUpper());

	if (!PC.IsDebugMenuOpen())
	{
		if (!ToolHint.IsEmpty())
		{
			const FVector2D Size = Measure(ToolHint);
			const FBox2D Box(FVector2D(X0, Y0), FVector2D(X0 + Size.X + 2.f * Pad * S, Y0 + Size.Y + Pad * S));
			DrawRect(Background, Box.Min.X, Box.Min.Y, Box.GetSize().X, Box.GetSize().Y);
			Border2D(Box, Border);
			DrawText(ToolHint, Title, Box.Min.X + Pad * S, Box.Min.Y + 0.5f * Pad * S, Font, S);
		}
		return;
	}

	const TArray<ASquadPlayerController::FDebugMenuRow>& Rows = PC.GetDebugMenuRows();
	TArray<FString> InfoLines = PC.GetDebugInfoLines();
	const FString TitleText = TEXT("DEBUG MENU  (F6 closes)");

	// Size the panel to its widest line.
	float ContentW = Measure(TitleText).X;
	for (const FString& Line : InfoLines)
	{
		ContentW = FMath::Max(ContentW, Measure(Line).X);
	}
	float ContentH = TextH + 4.f * S + InfoLines.Num() * TextH + 4.f * S;
	for (const ASquadPlayerController::FDebugMenuRow& Row : Rows)
	{
		if (Row.bHeader)
		{
			ContentW = FMath::Max(ContentW, Measure(Row.Label).X);
			ContentH += HeaderHeight * S;
			continue;
		}
		float W = LabelWidth * S;
		for (const ASquadPlayerController::FDebugMenuButton& Button : Row.Buttons)
		{
			W += Measure(Button.Label).X + 2.f * ButtonPadX * S + ButtonGap * S;
		}
		ContentW = FMath::Max(ContentW, W);
		ContentH += RowHeight * S;
	}
	if (!ToolHint.IsEmpty())
	{
		ContentW = FMath::Max(ContentW, Measure(ToolHint).X);
		ContentH += TextH + 6.f * S;
	}

	DebugMenuBox = FBox2D(FVector2D(X0, Y0), FVector2D(X0 + ContentW + 2.f * Pad * S, Y0 + ContentH + 2.f * Pad * S));
	bDebugMenuVisible = true;
	DrawRect(Background, DebugMenuBox.Min.X, DebugMenuBox.Min.Y, DebugMenuBox.GetSize().X, DebugMenuBox.GetSize().Y);
	Border2D(DebugMenuBox, Border);

	float MouseX = 0.f, MouseY = 0.f;
	PC.GetMousePosition(MouseX, MouseY);
	const FVector2D Mouse(MouseX, MouseY);

	const float X = X0 + Pad * S;
	float Y = Y0 + Pad * S;
	DrawText(TitleText, Title, X, Y, Font, S);
	Y += TextH + 4.f * S;
	for (const FString& Line : InfoLines)
	{
		DrawText(Line, Info, X, Y, Font, S);
		Y += TextH;
	}
	Y += 4.f * S;

	for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
	{
		const ASquadPlayerController::FDebugMenuRow& Row = Rows[RowIndex];
		if (Row.bHeader)
		{
			const float LineY = Y + 3.f * S;
			DrawLine(X, LineY, DebugMenuBox.Max.X - Pad * S, LineY, FLinearColor(HeaderText.R, HeaderText.G, HeaderText.B, 0.35f), 1.f);
			DrawText(Row.Label, HeaderText, X, Y + (HeaderHeight * S - TextH) * 0.5f + 2.f * S, Font, S);
			Y += HeaderHeight * S;
			continue;
		}
		const float TextY = Y + (RowHeight * S - TextH) * 0.5f;
		DrawText(Row.Label, LabelText, X, TextY, Font, S);
		float BX = X + LabelWidth * S;
		for (int32 ButtonIndex = 0; ButtonIndex < Row.Buttons.Num(); ++ButtonIndex)
		{
			const ASquadPlayerController::FDebugMenuButton& Button = Row.Buttons[ButtonIndex];
			const float BW = Measure(Button.Label).X + 2.f * ButtonPadX * S;
			const FBox2D Box(FVector2D(BX, Y + 1.f * S), FVector2D(BX + BW, Y + (RowHeight - 2.f) * S));
			const bool bHover = Button.bEnabled && Box.IsInside(Mouse);
			const FLinearColor Fill = !Button.bEnabled ? ButtonDisabled
				: Button.bActive ? (bHover ? ButtonActiveHover : ButtonActive)
				: (bHover ? ButtonHover : ButtonFill);
			DrawRect(Fill, Box.Min.X, Box.Min.Y, BW, Box.GetSize().Y);
			DrawText(Button.Label, Button.bEnabled ? FLinearColor::White : DisabledText, BX + ButtonPadX * S, TextY, Font, S);

			FDebugButtonBox& Hit = DebugButtonBoxes.AddDefaulted_GetRef();
			Hit.Box = Box;
			Hit.Index = FIntPoint(RowIndex, ButtonIndex);
			Hit.bEnabled = Button.bEnabled;
			BX += BW + ButtonGap * S;
		}
		Y += RowHeight * S;
	}

	if (!ToolHint.IsEmpty())
	{
		Y += 6.f * S;
		DrawText(ToolHint, Title, X, Y, Font, S);
	}
#endif
}
