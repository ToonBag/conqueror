// Master volume and mute (GDD §13): the HUD's audio widget at the top left. Local-only settings, never sent to the server.

#include "SquadPlayerController.h"

#include "SquadHUD.h"
#include "AudioDevice.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"

namespace SquadAudioSettings
{
	const TCHAR* const Section = TEXT("/Script/Unreal.SquadAudio");
	const TCHAR* const VolumeKey = TEXT("MasterVolume");
	const TCHAR* const MutedKey = TEXT("bMuted");
}

void ASquadPlayerController::LoadAudioSettings()
{
	using namespace SquadAudioSettings;
	if (GConfig)
	{
		GConfig->GetFloat(Section, VolumeKey, MasterVolume, GGameUserSettingsIni);
		GConfig->GetBool(Section, MutedKey, bAudioMuted, GGameUserSettingsIni);
	}
	MasterVolume = FMath::Clamp(MasterVolume, 0.f, 1.f);
	ApplyAudioVolume();
}

void ASquadPlayerController::SaveAudioSettings() const
{
	using namespace SquadAudioSettings;
	if (GConfig)
	{
		GConfig->SetFloat(Section, VolumeKey, MasterVolume, GGameUserSettingsIni);
		GConfig->SetBool(Section, MutedKey, bAudioMuted, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void ASquadPlayerController::ApplyAudioVolume() const
{
	const UWorld* World = GetWorld();
	if (FAudioDeviceHandle Device = World ? World->GetAudioDevice() : FAudioDeviceHandle(); Device.IsValid())
	{
		Device->SetTransientPrimaryVolume(bAudioMuted ? 0.f : MasterVolume);
	}
}

void ASquadPlayerController::SetMasterVolume(float Volume)
{
	MasterVolume = FMath::Clamp(Volume, 0.f, 1.f);
	bAudioMuted = false; // Moving the slider unmutes.
	ApplyAudioVolume();
}

void ASquadPlayerController::SetAudioMuted(bool bMuted)
{
	bAudioMuted = bMuted;
	ApplyAudioVolume();
	SaveAudioSettings();
}

bool ASquadPlayerController::HandleAudioMouse()
{
	const ASquadHUD* SquadHUD = Cast<ASquadHUD>(GetHUD());
	float MouseX = 0.f, MouseY = 0.f;
	if (!SquadHUD || !GetMousePosition(MouseX, MouseY))
	{
		bVolumeDragging = false;
		return false;
	}
	const FVector2D Mouse(MouseX, MouseY);
	const bool bLeftPressed = WasInputKeyJustPressed(EKeys::LeftMouseButton);

	if (bVolumeDragging)
	{
		// A drag keeps following the cursor even off the slider, until the button is let go.
		float Volume;
		if (SquadHUD->GetVolumeSliderValueAt(Mouse, /*bClamp*/ true, Volume))
		{
			SetMasterVolume(Volume);
		}
		if (!IsInputKeyDown(EKeys::LeftMouseButton))
		{
			bVolumeDragging = false;
			SaveAudioSettings();
		}
		return true;
	}
	if (!SquadHUD->IsOverAudioControls(Mouse))
	{
		return false;
	}
	if (bLeftPressed)
	{
		float Volume;
		if (SquadHUD->IsOverMuteButton(Mouse))
		{
			SetAudioMuted(!bAudioMuted);
		}
		else if (SquadHUD->GetVolumeSliderValueAt(Mouse, /*bClamp*/ false, Volume))
		{
			bVolumeDragging = true;
			SetMasterVolume(Volume);
		}
	}
	return true; // Nothing under the widget gets the click.
}
