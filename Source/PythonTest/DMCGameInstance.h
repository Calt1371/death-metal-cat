#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "DMCGameInstance.generated.h"

class UGlobalBrightnessOverlayWidget;
class USoundMix;
class USoundClass;

/**
 * Project-wide GameInstance. Its whole job is settings persistence: load the saved
 * SFXVolume/MusicVolume/Brightness (or fall back to defaults if no save exists yet) in Init() -- as
 * early as a GameInstance can act, before any level has loaded -- and apply/save them again whenever
 * the Adjustment Menu screen or the pause screen's Options page changes any of the three. Both
 * screens read/write these same three cached values and the same SaveGame slot, so a change on
 * either one is reflected on the other.
 *
 * Init() only loads the raw values into CachedSFXVolume/CachedMusicVolume/CachedBrightness; it
 * deliberately does NOT touch the audio device or create the brightness widget there. Neither has a
 * reliably valid World/audio device to attach to that early in the engine boot sequence
 * (World-independent GameInstance::Init() timing relative to the audio device and viewport isn't
 * something to gamble on without being able to verify it live). Instead,
 * ApplyStartupSettings(WorldContextObject) is a small idempotent call each screen's GameMode makes
 * once from its own BeginPlay (see AAdjustmentMenuGameMode, ATitleScreenGameMode,
 * AIntroCinematicGameMode, AGameplayPlayerController) -- which by then definitely has a valid world.
 * For L_AdjustmentMenu specifically (the game's actual entry point, per GameDefaultMap), that's as
 * early as this project's architecture can apply it, so brightness/volume are correct before the
 * very first frame the player actually sees, and every later screen/transition just re-applies the
 * same already-loaded values (a no-op in practice, since nothing changed them) -- EXCEPT
 * AAdjustmentMenuGameMode itself, which deliberately forces SFX/Music back to silence right before
 * calling ApplyStartupSettings -- see that class's comment for why.
 *
 * SFX and Music route through their own SoundClasses (SC_SFX/SC_Music, created by
 * AgentScripts/ue_create_adjustment_menu_assets.py) rather than the engine's stock Master class this
 * used to share a single volume through -- splitting them out is what makes independent live preview
 * (a gunshot blip as SFX changes, the level's own music track fading live as Music changes) and
 * independent starting-silent behavior possible in the first place.
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
	 * Re-applies CachedSFXVolume/CachedMusicVolume via SetSoundMixClassOverride, and lazily creates
	 * (on the first call only) + re-applies CachedBrightness to GlobalBrightnessOverlay.
	 * Safe/idempotent to call repeatedly -- every screen's GameMode calls this once from its own
	 * BeginPlay (see class comment for why Init() itself can't do this).
	 */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ApplyStartupSettings(const UObject* WorldContextObject);

	/** Sets SFX volume [0,1], applies it immediately via SetSoundMixClassOverride on SC_SFX, and saves. Called live from the Adjustment Menu and the pause screen's Options page as the player adjusts it -- by then a world always exists, so this applies unconditionally (unlike ApplyStartupSettings, which is also called before that's guaranteed). */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetSFXVolume(const UObject* WorldContextObject, float NewVolume);

	/** Sets music volume [0,1], applies it immediately via SetSoundMixClassOverride on SC_Music, and saves. Same call sites/reasoning as SetSFXVolume. Since the volume change is a class-level mix override, it live-updates any Music-class sound already playing (e.g. the Adjustment Menu's own preview track, or the gameplay level's background music) without needing to touch that sound's AudioComponent directly. */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetMusicVolume(const UObject* WorldContextObject, float NewVolume);

	/** Sets brightness [0,1] (0.5 = neutral), applies it immediately to GlobalBrightnessOverlay, and saves. Called live from the Adjustment Menu and the pause screen's Options page as the player adjusts it. */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetBrightness(float NewBrightness);

	float GetSFXVolume() const { return CachedSFXVolume; }
	float GetMusicVolume() const { return CachedMusicVolume; }
	float GetBrightness() const { return CachedBrightness; }

private:
	/** Writes CachedSFXVolume/CachedMusicVolume/CachedBrightness to the SaveGame slot. Called from every setter on every change -- cheap enough (a three-float SaveGame object) that there's no reason to batch/defer it. */
	void SaveSettings();

	/** SC_SFX -- created by AgentScripts/ue_create_adjustment_menu_assets.py. Every imported SFX SoundWave (jump/dodge/sword/gun/enemy/level-up/death stingers) has its Sound Class Object set to this, so overriding it via MIX_SFXVolume affects all of them uniformly. */
	UPROPERTY()
	TObjectPtr<USoundClass> SFXSoundClass;

	/** MIX_SFXVolume -- its class override on SFXSoundClass is what SetSoundMixClassOverride actually adjusts. */
	UPROPERTY()
	TObjectPtr<USoundMix> SFXVolumeSoundMix;

	/** SC_Music -- created by AgentScripts/ue_create_adjustment_menu_assets.py. DMC_Music (the gameplay level's background track) has its Sound Class Object set to this. */
	UPROPERTY()
	TObjectPtr<USoundClass> MusicSoundClass;

	/** MIX_MusicVolume -- its class override on MusicSoundClass is what SetSoundMixClassOverride actually adjusts. */
	UPROPERTY()
	TObjectPtr<USoundMix> MusicVolumeSoundMix;

	/** Created here in Init() and added to the viewport once; persists across every level transition for the rest of the process's life -- see class comment. */
	UPROPERTY()
	TObjectPtr<UGlobalBrightnessOverlayWidget> GlobalBrightnessOverlay;

	/** Defaults to 0 (silent) -- see AAdjustmentMenuGameMode's comment for why SFX/Music start silent every session regardless of what's saved. */
	float CachedSFXVolume = 0.f;

	/** Defaults to 0 (silent) -- see CachedSFXVolume. */
	float CachedMusicVolume = 0.f;

	float CachedBrightness = 0.5f;

	static const TCHAR* SaveSlotName;
};
