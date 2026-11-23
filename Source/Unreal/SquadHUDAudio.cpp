// Audio widget (GDD §13), top left: a speaker button that mutes / unmutes everything, a master volume slider and
// the volume in percent. Clicks are handled by ASquadPlayerController::HandleAudioMouse.

#include "SquadHUD.h"

#include "CanvasItem.h"
#include "SquadHUDDraw.h"
#include "SquadPlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"

namespace SquadAudioWidget
{
	// Sizes at 1080p; scaled with the screen height.
	constexpr float Margin = 12.f;
	constexpr float Height = 30.f;
	constexpr float Pad = 4.f;
	constexpr float ButtonSize = 22.f;
	constexpr float TrackWidth = 110.f;
	constexpr float TrackHeight = 4.f;
	constexpr float KnobRadius = 6.f;
	constexpr float LabelWidth = 46.f;

	const FLinearColor Background(0.02f, 0.02f, 0.03f, 0.6f);
	const FLinearColor ButtonFill(0.16f, 0.17f, 0.2f, 0.9f);
	const FLinearColor ButtonHover(0.28f, 0.3f, 0.36f, 0.95f);
	const FLinearColor Icon(0.9f, 0.9f, 0.9f, 1.f);
	const FLinearColor MutedRed(0.95f, 0.3f, 0.25f, 1.f);
	const FLinearColor TrackBack(1.f, 1.f, 1.f, 0.18f);
	const FLinearColor TrackFill(1.f, 0.78f, 0.3f, 0.95f);
	const FLinearColor TrackFillMuted(0.6f, 0.6f, 0.6f, 0.5f);
}

bool ASquadHUD::GetVolumeSliderValueAt(const FVector2D& ScreenPosition, bool bClamp, float& OutVolume) const
{
	if (!bAudioVisible || VolumeTrackMaxX <= VolumeTrackMinX || (!bClamp && !VolumeSliderBox.IsInside(ScreenPosition)))
	{
		return false;
	}
	OutVolume = FMath::Clamp(static_cast<float>((ScreenPosition.X - VolumeTrackMinX) / (VolumeTrackMaxX - VolumeTrackMinX)), 0.f, 1.f);
	return true;
}

void ASquadHUD::DrawAudioControls(ASquadPlayerController& PC)
{
	using namespace SquadAudioWidget;
	using namespace SquadHUDDraw;

	// The debug menu and its click-tool hint use this corner while they are up.
	if (PC.IsDebugMenuOpen() || PC.GetDebugClickTool() != ASquadPlayerController::EDebugClickTool::None)
	{
		return;
	}
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}
	const float S = LastScale;
	const bool bMuted = PC.IsAudioMuted();
	const float Volume = PC.GetMasterVolume();

	float MouseX = 0.f, MouseY = 0.f;
	PC.GetMousePosition(MouseX, MouseY);
	const FVector2D Mouse(MouseX, MouseY);

	const float X0 = Margin * S, Y0 = Margin * S;
	const float H = Height * S;
	const float W = (Pad + ButtonSize + 2.f * Pad + KnobRadius + TrackWidth + KnobRadius + 2.f * Pad + LabelWidth) * S;
	AudioPanelBox = FBox2D(FVector2D(X0, Y0), FVector2D(X0 + W, Y0 + H));
	bAudioVisible = true;
	DrawRect(Background, X0, Y0, W, H);

	// Mute button with a speaker icon: sound waves when on, a red cross when muted.
	const float BS = ButtonSize * S;
	const FVector2D BMin(X0 + Pad * S, Y0 + (H - BS) * 0.5f);
	MuteButtonBox = FBox2D(BMin, BMin + FVector2D(BS, BS));
	DrawRect(MuteButtonBox.IsInside(Mouse) ? ButtonHover : ButtonFill, BMin.X, BMin.Y, BS, BS);

	TArray<FCanvasUVTri> Tris;
	const FVector2D C = MuteButtonBox.GetCenter() - FVector2D(3.f * S, 0.f);
	const FLinearColor SpeakerColor = bMuted ? MutedRed : Icon;
	// Speaker: a small box and a cone opening to the right.
	AddPolygon(Tris, { C + FVector2D(-5.f, -2.5f) * S, C + FVector2D(-2.f, -2.5f) * S, C + FVector2D(-2.f, 2.5f) * S, C + FVector2D(-5.f, 2.5f) * S }, SpeakerColor);
	AddPolygon(Tris, { C + FVector2D(-2.f, -2.5f) * S, C + FVector2D(2.5f, -6.5f) * S, C + FVector2D(2.5f, 6.5f) * S, C + FVector2D(-2.f, 2.5f) * S }, SpeakerColor);
	if (bMuted)
	{
		const FVector2D XC = C + FVector2D(7.5f, 0.f) * S;
		AddSegment(Tris, XC + FVector2D(-3.f, -3.f) * S, XC + FVector2D(3.f, 3.f) * S, 1.6f * S, MutedRed, false);
		AddSegment(Tris, XC + FVector2D(-3.f, 3.f) * S, XC + FVector2D(3.f, -3.f) * S, 1.6f * S, MutedRed, false);
	}
	else
	{
		// One to three waves by volume.
		const int32 Waves = Volume <= 0.f ? 0 : Volume < 0.34f ? 1 : Volume < 0.67f ? 2 : 3;
		for (int32 I = 0; I < Waves; ++I)
		{
			const float R = (5.f + 3.f * I) * S;
			const FVector2D WC = C + FVector2D(1.5f, 0.f) * S;
			constexpr int32 Segs = 6;
			for (int32 K = 0; K < Segs; ++K)
			{
				const float A0 = FMath::DegreesToRadians(-45.f + 90.f * K / Segs), A1 = FMath::DegreesToRadians(-45.f + 90.f * (K + 1) / Segs);
				AddSegment(Tris, WC + R * FVector2D(FMath::Cos(A0), FMath::Sin(A0)), WC + R * FVector2D(FMath::Cos(A1), FMath::Sin(A1)), 1.4f * S, Icon, false);
			}
		}
	}

	// Slider: the track, filled up to the volume (grey while muted), with a round knob.
	VolumeTrackMinX = MuteButtonBox.Max.X + (2.f * Pad + KnobRadius) * S;
	VolumeTrackMaxX = VolumeTrackMinX + TrackWidth * S;
	const float TrackY = Y0 + H * 0.5f;
	VolumeSliderBox = FBox2D(FVector2D(VolumeTrackMinX - KnobRadius * S, Y0), FVector2D(VolumeTrackMaxX + KnobRadius * S, Y0 + H));
	const float KnobX = FMath::Lerp(VolumeTrackMinX, VolumeTrackMaxX, Volume);
	DrawRect(TrackBack, VolumeTrackMinX, TrackY - TrackHeight * 0.5f * S, TrackWidth * S, TrackHeight * S);
	DrawRect(bMuted ? TrackFillMuted : TrackFill, VolumeTrackMinX, TrackY - TrackHeight * 0.5f * S, KnobX - VolumeTrackMinX, TrackHeight * S);
	const bool bKnobHover = VolumeSliderBox.IsInside(Mouse);
	AddDisc(Tris, FVector2D(KnobX, TrackY), (KnobRadius + (bKnobHover ? 1.f : 0.f)) * S, bMuted ? TrackFillMuted : FLinearColor::White, 14);

	FCanvasTriangleItem TriItem(Tris, Canvas->DefaultTexture->GetResource());
	TriItem.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(TriItem);

	// Percentage, or MUTED.
	const FString Label = bMuted ? FString(TEXT("MUTED")) : FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Volume * 100.f));
	float TW = 0.f, TH = 0.f;
	GetTextSize(Label, TW, TH, Font, S);
	DrawText(Label, bMuted ? MutedRed : Icon, VolumeTrackMaxX + (KnobRadius + 2.f * Pad) * S, Y0 + (H - TH) * 0.5f, Font, S);
}
