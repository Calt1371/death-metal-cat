#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "DMCSettingsSaveGame.generated.h"

/**
 * Persists the settings the Adjustment Menu / Pause Screen Options page expose: SFX volume, music
 * volume, and screen brightness. Was a single flat MasterVolume float until the Adjustment Menu
 * screen needed SFX and Music independently controllable (with independent live-preview behavior),
 * which meant giving this project an actual SoundClass split (SC_SFX/SC_Music, see
 * AgentScripts/ue_create_adjustment_menu_assets.py) instead of routing everything through the
 * engine's one stock Master class. Deliberately just these three flat floats -- there's nothing
 * else to save yet, so this doesn't try to anticipate a future options/settings system.
 */
UCLASS()
class PYTHONTEST_API UDMCSettingsSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** [0, 1] linear volume multiplier applied to the SC_SFX sound class -- see UDMCGameInstance::ApplyStartupSettings. Defaults to 0 (silent) to match the Adjustment Menu screen's own starting value, though in practice AAdjustmentMenuGameMode always force-resets this to 0 at the start of every session regardless of what's saved here -- see that class's comment. */
	UPROPERTY()
	float SFXVolume = 0.f;

	/** [0, 1] linear volume multiplier applied to the SC_Music sound class -- same idea as SFXVolume, just for the SC_Music class. */
	UPROPERTY()
	float MusicVolume = 0.f;

	/** [0, 1], 0.5 = neutral (no overlay). Below 0.5 darkens toward black, above 0.5 lightens toward white -- see UGlobalBrightnessOverlayWidget::SetBrightness. Unlike SFXVolume/MusicVolume, this is NOT force-reset every session -- brightness keeps whatever the player last chose. */
	UPROPERTY()
	float Brightness = 0.5f;
};
