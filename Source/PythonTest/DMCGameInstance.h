#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "DMCGameInstance.generated.h"

class UGlobalBrightnessOverlayWidget;
class USoundMix;
class USoundClass;

/**
 * Project-wide GameInstance. Its whole job is settings persistence: load the saved
 * MasterVolume/Brightness (or fall back to defaults if no save exists yet) in Init() -- as early as
 * a GameInstance can act, before any level has loaded -- and apply/save them again whenever the
 * pause screen's Options page changes either one.
 *
 * Init() only loads the raw values into CachedMasterVolume/CachedBrightness; it deliberately does
 * NOT touch the audio device or create the brightness widget there. Neither has a reliably valid
 * World/audio device to attach to that early in the engine boot sequence (World-independent
 * GameInstance::Init() timing relative to the audio device and viewport isn't something to gamble
 * on without being able to verify it live). Instead, ApplyStartupSettings(WorldContextObject) is a
 * small idempotent call each screen's GameMode makes once from its own BeginPlay (see
 * ATitleScreenGameMode, AIntroCinematicGameMode, AGameplayPlayerController) -- which by then
 * definitely has a valid world. For L_TitleScreen specifically (the game's actual entry point,
 * per GameDefaultMap), that's as early as this project's architecture can apply it, so brightness
 * is correct before the very first frame the player actually sees, and every later
 * screen/transition just re-applies the same already-loaded values (a no-op in practice, since
 * nothing changed them). Volume works the same way, applied via SetSoundMixClassOverride, which
 * does need a live audio device -- also unavailable at Init() time.
 *
 * The brightness widget itself (GlobalBrightnessOverlay) is created once, lazily, on the first
 * ApplyStartupSettings call, and is owned by THIS object rather than by any per-level
 * GameMode/widget specifically because a GameInstance -- unlike everything else in this project --
 * survives level transitions, so the same overlay instance keeps working (and keeps reflecting live
 * Options-page changes) across every subsequent level load without needing to be recreated.
 */
UCLASS()
class PYTHONTEST_API UDMCGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;

	/**
	 * Re-applies CachedMasterVolume via SetSoundMixClassOverride, and lazily creates (on the first
	 * call only) + re-applies CachedBrightness to GlobalBrightnessOverlay. Safe/idempotent to call
	 * repeatedly -- every screen's GameMode calls this once from its own BeginPlay (see class
	 * comment for why Init() itself can't do this).
	 */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ApplyStartupSettings(const UObject* WorldContextObject);

	/** Sets the master volume [0,1], applies it immediately via SetSoundMixClassOverride, and saves. Called live from the pause screen's Options page as the player adjusts it -- by then a world always exists, so this applies unconditionally (unlike ApplyStartupSettings, which is also called before that's guaranteed). */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetMasterVolume(const UObject* WorldContextObject, float NewVolume);

	/** Sets brightness [0,1] (0.5 = neutral), applies it immediately to GlobalBrightnessOverlay, and saves. Called live from the pause screen's Options page as the player adjusts it. */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetBrightness(float NewBrightness);

	float GetMasterVolume() const { return CachedMasterVolume; }
	float GetBrightness() const { return CachedBrightness; }

private:
	/** Writes CachedMasterVolume/CachedBrightness to the SaveGame slot. Called from both setters on every change -- cheap enough (a two-float SaveGame object) that there's no reason to batch/defer it. */
	void SaveSettings();

	/** /Engine/EngineSounds/Master.Master -- the engine's own stock master sound class. All sounds route through it by default, so overriding it via MasterVolumeSoundMix affects overall game audio without this project needing its own SoundClass hierarchy (there are no SFX/music assets in the project yet to route through one anyway). */
	UPROPERTY()
	TObjectPtr<USoundClass> MasterSoundClass;

	/** MIX_MasterVolume -- created by AgentScripts/ue_create_pause_screen_assets.py. Its class override on MasterSoundClass is what SetSoundMixClassOverride actually adjusts. */
	UPROPERTY()
	TObjectPtr<USoundMix> MasterVolumeSoundMix;

	/** Created here in Init() and added to the viewport once; persists across every level transition for the rest of the process's life -- see class comment. */
	UPROPERTY()
	TObjectPtr<UGlobalBrightnessOverlayWidget> GlobalBrightnessOverlay;

	float CachedMasterVolume = 1.f;
	float CachedBrightness = 0.5f;

	static const TCHAR* SaveSlotName;
};
