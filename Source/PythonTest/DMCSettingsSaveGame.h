#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "DMCSettingsSaveGame.generated.h"

/**
 * Persists the two settings the pause screen's Options page exposes: master volume and screen
 * brightness. Deliberately just these two flat floats -- there's nothing else to save yet, so this
 * doesn't try to anticipate a future options/settings system.
 */
UCLASS()
class PYTHONTEST_API UDMCSettingsSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** [0, 1] linear volume multiplier applied to the engine's Master sound class -- see UDMCGameInstance::ApplyCachedAudioVolume. */
	UPROPERTY()
	float MasterVolume = 1.f;

	/** [0, 1], 0.5 = neutral (no overlay). Below 0.5 darkens toward black, above 0.5 lightens toward white -- see UGlobalBrightnessOverlayWidget::SetBrightness. */
	UPROPERTY()
	float Brightness = 0.5f;
};
