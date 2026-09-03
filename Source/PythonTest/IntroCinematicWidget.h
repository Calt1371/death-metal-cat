#pragma once

#include "CoreMinimal.h"
#include "FullscreenVideoWidgetBase.h"
#include "IntroCinematicWidget.generated.h"

class AIntroCinematicGameMode;

/**
 * Drives the intro cinematic's one-shot backstory video, on top of UFullscreenVideoWidgetBase's
 * shared playback/fade scaffolding (see that class's comment for what's shared vs. what lives
 * here). Unlike the title screen, there is no loop: the video plays exactly once, and either the
 * player skips it (any input, handled by AIntroCinematicPlayerController) or it plays to
 * completion on its own (HandleMediaEndReached below) -- both funnel into
 * AIntroCinematicGameMode::HandleCinematicEnd so skipping and playing to the end look and behave
 * identically from that point on, exactly as the brief asks for.
 */
UCLASS()
class PYTHONTEST_API UIntroCinematicWidget : public UFullscreenVideoWidgetBase
{
	GENERATED_BODY()

public:
	/** Set once, right after CreateWidget -- lets HandleMediaEndReached call back into the game mode when the cinematic finishes on its own. Same "push a pointer in after creation" pattern as UGnarlyRankHUDWidget::SetOwningCharacter. */
	void SetOwningGameMode(AIntroCinematicGameMode* InGameMode);

protected:
	virtual const TCHAR* GetMediaPlayerAssetPath() const override;
	virtual const TCHAR* GetMediaSourceAssetPath() const override;
	virtual const TCHAR* GetMediaTextureAssetPath() const override;
	virtual FString GetHintTextString() const override;

	/** Natural completion (the player never skipped) -- funnels into the exact same hand-off AIntroCinematicPlayerController's skip path uses. */
	virtual void HandleMediaEndReached() override;

private:
	UPROPERTY()
	TObjectPtr<AIntroCinematicGameMode> OwningGameMode;
};
