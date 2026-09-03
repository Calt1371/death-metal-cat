#include "DMCGameInstance.h"

#include "DMCSettingsSaveGame.h"
#include "GlobalBrightnessOverlayWidget.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundClass.h"

const TCHAR* UDMCGameInstance::SaveSlotName = TEXT("DMCSettings");

namespace
{
	// Created by AgentScripts/ue_create_pause_screen_assets.py.
	const TCHAR* MasterVolumeMixPath = TEXT("/Game/UI/PauseMenu/MIX_MasterVolume.MIX_MasterVolume");

	// Stock engine asset -- every sound routes through this by default, so overriding it affects
	// overall game audio without this project needing its own SoundClass hierarchy (there are no
	// SFX/music assets in the project yet to route through one anyway).
	const TCHAR* EngineMasterSoundClassPath = TEXT("/Engine/EngineSounds/Master.Master");
}

void UDMCGameInstance::Init()
{
	Super::Init();

	// Data only -- see class comment for why this deliberately doesn't touch audio/widgets yet.
	if (UDMCSettingsSaveGame* Loaded = Cast<UDMCSettingsSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0)))
	{
		CachedMasterVolume = Loaded->MasterVolume;
		CachedBrightness = Loaded->Brightness;
		UE_LOG(LogTemp, Log, TEXT("[SETTINGS] Loaded save: MasterVolume=%.2f Brightness=%.2f"), CachedMasterVolume, CachedBrightness);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[SETTINGS] No save found -- using defaults: MasterVolume=%.2f Brightness=%.2f"), CachedMasterVolume, CachedBrightness);
	}
}

void UDMCGameInstance::ApplyStartupSettings(const UObject* WorldContextObject)
{
	if (!MasterSoundClass)
	{
		MasterSoundClass = LoadObject<USoundClass>(nullptr, EngineMasterSoundClassPath);
		if (!MasterSoundClass)
		{
			UE_LOG(LogTemp, Error, TEXT("[SETTINGS] Failed to load engine master sound class: %s"), EngineMasterSoundClassPath);
		}
	}

	if (!MasterVolumeSoundMix)
	{
		MasterVolumeSoundMix = LoadObject<USoundMix>(nullptr, MasterVolumeMixPath);
		if (!MasterVolumeSoundMix)
		{
			UE_LOG(LogTemp, Error, TEXT("[SETTINGS] Failed to load master volume sound mix: %s"), MasterVolumeMixPath);
		}
	}

	if (MasterSoundClass && MasterVolumeSoundMix && WorldContextObject)
	{
		UGameplayStatics::SetSoundMixClassOverride(
			WorldContextObject, MasterVolumeSoundMix, MasterSoundClass, CachedMasterVolume, /*Pitch=*/1.f, /*FadeInTime=*/0.f, /*bApplyToChildren=*/true);
		UGameplayStatics::PushSoundMixModifier(WorldContextObject, MasterVolumeSoundMix);
	}

	if (!GlobalBrightnessOverlay && WorldContextObject && WorldContextObject->GetWorld())
	{
		GlobalBrightnessOverlay = CreateWidget<UGlobalBrightnessOverlayWidget>(WorldContextObject->GetWorld(), UGlobalBrightnessOverlayWidget::StaticClass());
		if (GlobalBrightnessOverlay)
		{
			// Very high Z-order so it renders above every screen's own widgets (title/intro/pause
			// alike), for the rest of the process's life -- see class comment.
			GlobalBrightnessOverlay->AddToViewport(1000);
			UE_LOG(LogTemp, Log, TEXT("[SETTINGS] Global brightness overlay created."));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[SETTINGS] Failed to create the global brightness overlay widget."));
		}
	}

	if (GlobalBrightnessOverlay)
	{
		GlobalBrightnessOverlay->SetBrightness(CachedBrightness);
	}
}

void UDMCGameInstance::SetMasterVolume(const UObject* WorldContextObject, float NewVolume)
{
	CachedMasterVolume = FMath::Clamp(NewVolume, 0.f, 1.f);
	ApplyStartupSettings(WorldContextObject);
	SaveSettings();
}

void UDMCGameInstance::SetBrightness(float NewBrightness)
{
	CachedBrightness = FMath::Clamp(NewBrightness, 0.f, 1.f);
	if (GlobalBrightnessOverlay)
	{
		GlobalBrightnessOverlay->SetBrightness(CachedBrightness);
	}
	SaveSettings();
}

void UDMCGameInstance::SaveSettings()
{
	UDMCSettingsSaveGame* SaveObject = Cast<UDMCSettingsSaveGame>(UGameplayStatics::CreateSaveGameObject(UDMCSettingsSaveGame::StaticClass()));
	if (!SaveObject)
	{
		UE_LOG(LogTemp, Error, TEXT("[SETTINGS] Failed to create save game object."));
		return;
	}

	SaveObject->MasterVolume = CachedMasterVolume;
	SaveObject->Brightness = CachedBrightness;

	if (!UGameplayStatics::SaveGameToSlot(SaveObject, SaveSlotName, 0))
	{
		UE_LOG(LogTemp, Error, TEXT("[SETTINGS] SaveGameToSlot failed for slot %s."), SaveSlotName);
	}
}
