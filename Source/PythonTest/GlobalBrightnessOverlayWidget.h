#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GlobalBrightnessOverlayWidget.generated.h"

class UImage;

/**
 * The entire on-screen effect of the Options page's Brightness slider: one full-screen tinted
 * UImage, added to the viewport once by UDMCGameInstance (see that class's comment for why it owns
 * this rather than any per-level widget) and left there for the rest of the process's life, always
 * rendering above every other widget in the game.
 *
 * Deliberately a screen-space overlay rather than a post-process volume/exposure adjustment -- both
 * were viable, but this reuses the exact "tinted engine white-square UImage, opacity as the only
 * moving part" idiom already proven three times over in this project (UGnarlyRankHUDWidget's
 * LowHealthTintImage/RoomFadeImage/DeathScreenBackdrop, UFullscreenVideoWidgetBase's
 * BlackOverlayImage), whereas driving a post-process volume's AutoExposureBias correctly from code
 * with no in-editor visual tuning pass wasn't something to gamble on getting right on the first try
 * tonight.
 */
UCLASS()
class PYTHONTEST_API UGlobalBrightnessOverlayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;

	/**
	 * Value is [0,1], 0.5 = neutral (fully transparent, no tint at all). Below 0.5 fades in a BLACK
	 * tint (darkening), above 0.5 fades in a WHITE tint (lightening) -- a two-directional
	 * "brightness" control rather than a darken-only one, using a single UImage whose color AND
	 * opacity both change rather than two separate overlays, since only one direction is ever active
	 * at a time.
	 */
	void SetBrightness(float Value);

private:
	UPROPERTY()
	TObjectPtr<UImage> OverlayImage;
};
