#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AdjustmentMenuGameMode.generated.h"

class UAdjustmentMenuWidget;
class UTitleIntroCombinedWidget;
class UAudioComponent;
class USoundBase;
class AAdjustmentMenuPlayerController;

/**
 * Game mode for L_TitleScreen -- this project's actual entry point (GameDefaultMap). Owns the WHOLE
 * pre-gameplay experience in one continuous level/World: Adjustment Menu first, then (once
 * confirmed) the title loop + intro cinematic via UTitleIntroCombinedWidget, then real gameplay.
 * Was two separate levels (L_AdjustmentMenu -> OpenLevel -> L_TitleScreen) in an earlier draft --
 * merged into ONE level for the exact same reason title and intro were already merged into ONE
 * widget/MediaPlayer (see UTitleIntroCombinedWidget's class comment): confirmed live (2026-09-04)
 * that inserting a level transition ahead of UTitleIntroCombinedWidget's OpenSource call breaks
 * playback the same way a second OpenSource call does -- the title video came up with IsPlaying=1
 * but Time stuck at 0.000 and a 2x2 placeholder texture for the whole run. Merging into a single
 * level (this one) was NOT enough on its own, though -- confirmed live (2026-09-04) a SECOND time:
 * even with zero OpenLevel calls anywhere, creating CombinedWidget only once the player confirms out
 * of the Adjustment Menu (tens of seconds into the process's life, however long the player spends on
 * that screen) reproduced the exact same broken Time=0.000 symptom. The real constraint is stricter
 * than "no level transition" -- OpenSource has to happen within roughly the first couple of seconds
 * of the whole process's life, full stop, which is what made it reliable back when Title Screen was
 * simply the very first level (GameDefaultMap) with nothing ahead of it.
 *
 * So CombinedWidget is now created and added to the viewport immediately in BeginPlay, same instant
 * as before this screen existed -- its OpenSource call happens right away, satisfying that window --
 * but held fully silent and invisible (UFullscreenVideoWidgetBase::SetHeldForReveal(true) plus
 * RenderOpacity 0 / HitTestInvisible) behind the Adjustment Menu widget, which is created and shown
 * on top of it. The video keeps decoding and the title loop keeps cycling (Play/Freeze/Restart) the
 * whole time underneath, same as it always has -- there is nothing to "catch up" once revealed,
 * which is exactly why simply revealing it reads as seamless regardless of how long the Adjustment
 * Menu was up. CurrentPhase tracks which part of this combined experience is showing. SFX/Music are
 * force-reset to 0 (silent) and DMC_Music starts looping (silent) in BeginPlay, same as before -- see
 * MusicAudioComponent's own comment. Once the player confirms out of the Adjustment Menu,
 * TransitionToTitleIntro() destroys that widget and reveals the ALREADY-CREATED CombinedWidget (the
 * same object ATitleScreenGameMode used to create fresh -- that class still exists, unused by this
 * flow now, left in place per this project's established "don't delete still-referenced classes"
 * convention), and arms AAdjustmentMenuPlayerController's any-key detection for the title loop/
 * intro-skip input the combined widget expects.
 *
 * There is deliberately no pawn: DefaultPawnClass is null, same reasoning as the old
 * ATitleScreenGameMode (the whole screen is whichever widget is currently up;
 * AAdjustmentMenuPlayerController needs no pawn to receive raw key binds).
 */
UCLASS()
class PYTHONTEST_API AAdjustmentMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAdjustmentMenuGameMode();

	/** Which part of the combined pre-gameplay experience is currently showing. */
	enum class EPhase : uint8
	{
		AdjustmentMenu,
		TitleIntro,
	};

	// -- Forwarded from AAdjustmentMenuPlayerController's narrow raw key handlers. No-ops once
	// CurrentPhase has moved past AdjustmentMenu. --
	void HandleMenuUp();
	void HandleMenuDown();
	void HandleMenuLeft();
	void HandleMenuRight();

	/** First call (AdjustmentMenu phase): transitions to the title/intro widget. Later calls are no-ops here -- once in TitleIntro phase, Enter/Space/A are among the many keys AAdjustmentMenuPlayerController's any-key detection now catches instead, which routes to HandleAnyInput below. */
	void HandleMenuConfirm();

	/** Called by AAdjustmentMenuPlayerController's any-key handler, only armed once TitleIntro phase begins. Forwarded straight to CombinedWidget, which decides what "any input" currently means (leave the title loop, or skip the intro portion) -- see UTitleIntroCombinedWidget::NotifyAnyInput. Re-arms the controller's any-key detection afterward since this one continuous widget can legitimately receive it twice. */
	void HandleAnyInput();

protected:
	virtual void BeginPlay() override;

private:
	/** AdjustmentMenu -> TitleIntro: destroys the Adjustment Menu widget, reveals the already-created CombinedWidget (see class comment), arms any-key detection. Guarded to run at most once. */
	void TransitionToTitleIntro();

	/** Bound to CombinedWidget's OnReadyForGameplayDelegate -- fires exactly once, whether the intro portion was skipped or reached naturally. Starts the widget's fade to black and, once it completes, opens the real gameplay map. Idempotent. */
	void HandleReadyForGameplay();

	/** Fires EndFadeDuration after HandleReadyForGameplay -- opens the real gameplay map. */
	void FinishToGameplay();

	EPhase CurrentPhase = EPhase::AdjustmentMenu;

	UPROPERTY()
	TObjectPtr<UAdjustmentMenuWidget> AdjustmentMenuWidgetInstance;

	UPROPERTY()
	TObjectPtr<UTitleIntroCombinedWidget> CombinedWidget;

	UPROPERTY()
	TObjectPtr<UAudioComponent> MusicAudioComponent;

	FTimerHandle GameplayTransitionTimer;

	/** Latched by the first HandleReadyForGameplay call. */
	bool bReadyForGameplayTriggered = false;
};
