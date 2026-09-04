#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TitleScreenGameMode.generated.h"

class UTitleIntroCombinedWidget;

/**
 * Game mode for L_TitleScreen. Owns the ONE combined title+intro widget and the hand-off into real
 * gameplay once it finishes.
 *
 * The title loop and the intro cinematic both live in UTitleIntroCombinedWidget now, played back-
 * to-back from a single merged video file through a single UMediaPlayer -- see that class's comment
 * for the full story of why. Confirmed live (2026-09-04), across FOUR separate mitigations and TWO
 * media backends: whichever UMediaPlayer opens a source SECOND in this process never surfaces real
 * playback state again, even though the underlying native player is genuinely still decoding. The
 * only reliable fix was to never open a second source at all, so this GameMode -- unlike its
 * earlier two-widget/two-phase design -- now only ever creates ONE video widget for the whole
 * pre-gameplay experience.
 *
 * L_IntroCinematic, L_TitleScreen's old two-widget flow, and AIntroCinematicGameMode/
 * AIntroCinematicPlayerController/UTitleScreenWidget/UIntroCinematicWidget still exist but are no
 * longer part of this flow -- left in place rather than deleted, since BP_IntroCinematicGameMode (a
 * Blueprint asset) derives from AIntroCinematicGameMode and losing that parent class out from under
 * it would corrupt the asset.
 *
 * There is deliberately no pawn: DefaultPawnClass is null, so the player controller exists on its
 * own with nothing to possess. The whole screen is the widget, and ATitleScreenPlayerController
 * needs no pawn to receive raw key binds. The SAME controller instance is re-armed after every
 * press (see AAnyInputPlayerControllerBase::Rearm) since this one continuous screen can legitimately
 * receive "any input" twice -- once to leave the title loop, once to skip the intro portion.
 */
UCLASS()
class PYTHONTEST_API ATitleScreenGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATitleScreenGameMode();

	/** Called by ATitleScreenPlayerController on every "any input" press. Forwarded straight to the widget, which decides what that means for its own current state -- see UTitleIntroCombinedWidget::NotifyAnyInput. */
	void HandleAnyInput();

protected:
	virtual void BeginPlay() override;

	/** The real gameplay map, opened once the intro portion ends (skipped or played to completion) and its fade completes. Set to L_ControllerTestRange on BP_TitleScreenGameMode. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Title Screen")
	TSoftObjectPtr<UWorld> GameplayMap;

	/** How long the fade to black takes once the intro portion ends (skipped or completed). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Title Screen")
	float EndFadeDuration = 0.5f;

private:
	/**
	 * Bound to the widget's OnReadyForGameplayDelegate -- fires exactly once, whether the intro
	 * portion was skipped or reached naturally. Starts the widget's fade to black and, once it
	 * completes, opens GameplayMap. Idempotent.
	 */
	void HandleReadyForGameplay();

	/** Fires EndFadeDuration after HandleReadyForGameplay -- see FinishToGameplay. */
	void FinishToGameplay();

	UPROPERTY()
	TObjectPtr<UTitleIntroCombinedWidget> CombinedWidget;

	FTimerHandle GameplayTransitionTimer;

	/** Latched by the first HandleReadyForGameplay call. */
	bool bReadyForGameplayTriggered = false;
};
