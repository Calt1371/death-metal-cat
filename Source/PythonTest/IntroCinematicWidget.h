#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "FullscreenVideoWidgetBase.h"
#include "IntroCinematicWidget.generated.h"

/**
 * Drives the intro cinematic's one-shot backstory video, on top of UFullscreenVideoWidgetBase's
 * shared playback/fade scaffolding (see that class's comment for what's shared vs. what lives
 * here). Unlike the title screen, there is no loop: the video plays exactly once, and either the
 * player skips it (any input) or it plays to completion on its own (HandleMediaEndReached below)
 * -- both funnel into the same OnCinematicEndDelegate so skipping and playing to the end look and
 * behave identically from that point on, exactly as the brief asks for.
 *
 * Owned by ATitleScreenGameMode, not a dedicated intro-specific game mode -- see that class's
 * comment for why (this cinematic now plays in-place, in the SAME level as the title screen,
 * rather than via a level transition to L_IntroCinematic). A delegate rather than a typed GameMode
 * pointer keeps this widget from needing to know that.
 */
UCLASS()
class PYTHONTEST_API UIntroCinematicWidget : public UFullscreenVideoWidgetBase
{
	GENERATED_BODY()

public:
	/** Set once, right after CreateWidget -- lets HandleMediaEndReached call back out when the cinematic finishes on its own. Same "push state in after creation" pattern as UGnarlyRankHUDWidget::SetOwningCharacter used for a raw pointer. */
	void SetOnCinematicEndDelegate(FSimpleDelegate InDelegate);

protected:
	virtual const TCHAR* GetMediaPlayerAssetPath() const override;
	virtual const TCHAR* GetMediaSourceAssetPath() const override;
	virtual const TCHAR* GetMediaTextureAssetPath() const override;
	virtual FString GetHintTextString() const override;
	virtual FName GetDesiredMediaPlayerName() const override { return FName(TEXT("ElectraPlayer")); }

	/** Natural completion (the player never skipped) -- funnels into the exact same hand-off AIntroCinematicPlayerController's skip path uses. */
	virtual void HandleMediaEndReached() override;

private:
	FSimpleDelegate OnCinematicEndDelegate;
};
