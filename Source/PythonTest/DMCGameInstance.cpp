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
	// Created by AgentScripts/ue_create_adjustment_menu_assets.py.
	const TCHAR* SFXVolumeMixPath = TEXT("/Game/Audio/MIX_SFXVolume.MIX_SFXVolume");
	const TCHAR* SFXSoundClassPath = TEXT("/Game/Audio/SC_SFX.SC_SFX");
	const TCHAR* MusicVolumeMixPath = TEXT("/Game/Audio/MIX_MusicVolume.MIX_MusicVolume");
	const TCHAR* MusicSoundClassPath = TEXT("/Game/Audio/SC_Music.SC_Music");
}

void UDMCGameInstance::Init()
{
	Super::Init();

	// Data only -- see class comment for why this deliberately doesn't touch audio/widgets yet.
	if (UDMCSettingsSaveGame* Loaded = Cast<UDMCSettingsSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0)))
	{
		CachedSFXVolume = Loaded->SFXVolume;
		CachedMusicVolume = Loaded->MusicVolume;
		CachedBrightness = Loaded->Brightness;
		UE_LOG(LogTemp, Log, TEXT("[SETTINGS] Loaded save: SFXVolume=%.2f MusicVolume=%.2f Brightness=%.2f"), CachedSFXVolume, CachedMusicVolume, CachedBrightness);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[SETTINGS] No save found -- using defaults: SFXVolume=%.2f MusicVolume=%.2f Brightness=%.2f"), CachedSFXVolume, CachedMusicVolume, CachedBrightness);
	}
}

void UDMCGameInstance::ApplyStartupSettings(const UObject* WorldContextObject)
{
	if (!SFXSoundClass)
	{
		SFXSoundClass = LoadObject<USoundClass>(nullptr, SFXSoundClassPath);
		if (!SFXSoundClass)
		{
			UE_LOG(LogTemp, Error, TEXT("[SETTINGS] Failed to load SFX sound class: %s"), SFXSoundClassPath);
		}
	}

	if (!SFXVolumeSoundMix)
	{
		SFXVolumeSoundMix = LoadObject<USoundMix>(nullptr, SFXVolumeMixPath);
		if (!SFXVolumeSoundMix)
		{
			UE_LOG(LogTemp, Error, TEXT("[SETTINGS] Failed to load SFX volume sound mix: %s"), SFXVolumeMixPath);
		}
	}

	if (SFXSoundClass && SFXVolumeSoundMix && WorldContextObject)
	{
		UGameplayStatics::SetSoundMixClassOverride(
			WorldContextObject, SFXVolumeSoundMix, SFXSoundClass, CachedSFXVolume, /*Pitch=*/1.f, /*FadeInTime=*/0.f, /*bApplyToChildren=*/true);
		UGameplayStatics::PushSoundMixModifier(WorldContextObject, SFXVolumeSoundMix);
	}

	if (!MusicSoundClass)
	{
		MusicSoundClass = LoadObject<USoundClass>(nullptr, MusicSoundClassPath);
		if (!MusicSoundClass)
		{
			UE_LOG(LogTemp, Error, TEXT("[SETTINGS] Failed to load Music sound class: %s"), MusicSoundClassPath);
		}
	}

	if (!MusicVolumeSoundMix)
	{
		MusicVolumeSoundMix = LoadObject<USoundMix>(nullptr, MusicVolumeMixPath);
		if (!MusicVolumeSoundMix)
		{
			UE_LOG(LogTemp, Error, TEXT("[SETTINGS] Failed to load Music volume sound mix: %s"), MusicVolumeMixPath);
		}
	}

	if (MusicSoundClass && MusicVolumeSoundMix && WorldContextObject)
	{
		UGameplayStatics::SetSoundMixClassOverride(
			WorldContextObject, MusicVolumeSoundMix, MusicSoundClass, CachedMusicVolume, /*Pitch=*/1.f, /*FadeInTime=*/0.f, /*bApplyToChildren=*/true);
		UGameplayStatics::PushSoundMixModifier(WorldContextObject, MusicVolumeSoundMix);
	}

	if (!GlobalBrightnessOverlay && WorldContextObject && WorldContextObject->GetWorld())
	{
		GlobalBrightnessOverlay = CreateWidget<UGlobalBrightnessOverlayWidget>(WorldContextObject->GetWorld(), UGlobalBrightnessOverlayWidget::StaticClass());
		if (GlobalBrightnessOverlay)
		{
			// Very high Z-order so it renders above every screen's own widgets (adjustment
			// menu/title/intro/pause alike), for the rest of the process's life -- see class comment.
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

void UDMCGameInstance::SetSFXVolume(const UObject* WorldContextObject, float NewVolume)
{
	CachedSFXVolume = FMath::Clamp(NewVolume, 0.f, 1.f);
	ApplyStartupSettings(WorldContextObject);
	SaveSettings();
}

void UDMCGameInstance::SetMusicVolume(const UObject* WorldContextObject, float NewVolume)
{
	CachedMusicVolume = FMath::Clamp(NewVolume, 0.f, 1.f);
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

	SaveObject->SFXVolume = CachedSFXVolume;
	SaveObject->MusicVolume = CachedMusicVolume;
	SaveObject->Brightness = CachedBrightness;

	if (!UGameplayStatics::SaveGameToSlot(SaveObject, SaveSlotName, 0))
	{
		UE_LOG(LogTemp, Error, TEXT("[SETTINGS] SaveGameToSlot failed for slot %s."), SaveSlotName);
	}
}
