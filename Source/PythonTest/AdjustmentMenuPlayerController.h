#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "AdjustmentMenuPlayerController.generated.h"

/**
 * Player controller for L_TitleScreen (this project's GameDefaultMap, hosting the combined
 * Adjustment Menu -> title loop -> intro cinematic experience -- see AAdjustmentMenuGameMode's class
 * comment). Forwards raw key input to AAdjustmentMenuGameMode, which owns the actual widget/
 * settings/phase/level-transition logic -- same "controller just forwards, GameMode decides" shape
 * as the old ATitleScreenPlayerController -> ATitleScreenGameMode.
 *
 * Binds TWO independent input schemes, active at different times:
 *
 *  1. A narrow Up/Down/Left/Right/Confirm set, bound from the start (SetupInputComponent) and only
 *     meaningful while AAdjustmentMenuGameMode::CurrentPhase is AdjustmentMenu (the GameMode itself
 *     guards this, so these bindings are simply inert once the phase moves on -- no need to unbind
 *     them here).
 *
 *  2. A broad "catch literally any digital key" set, deliberately NOT bound until
 *     EnableAnyKeyDetection() is called (by AAdjustmentMenuGameMode::TransitionToTitleIntro, exactly
 *     once, when the player confirms out of the Adjustment Menu) -- this is the same enumerate-and-
 *     bind-every-key technique AAnyInputPlayerControllerBase uses for the title loop/intro-skip
 *     input UTitleIntroCombinedWidget expects, reimplemented locally here (rather than inheriting
 *     that base) specifically so it can be bound ON DEMAND partway through this controller's life
 *     instead of unconditionally from BeginPlay -- binding it from the start would make the very
 *     first Up/Down/Left/Right/Enter press on the Adjustment Menu ALSO register as "any input" and
 *     skip straight past the title/intro before the player ever saw it. The same debounce + arm-
 *     delay pattern AAnyInputPlayerControllerBase uses (bAnyKeyConsumed/AnyKeyArmTime) guards against
 *     the very keypress that confirmed the Adjustment Menu (Enter/Space/A) still being "down" the
 *     instant this second scheme arms.
 *
 * Raw InputComponent->BindKey throughout, NOT Enhanced Input: there is no pawn anywhere on this
 * screen at all (DefaultPawnClass is null), so there's no Enhanced Input mapping context to route
 * through in the first place.
 */
UCLASS()
class PYTHONTEST_API AAdjustmentMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AAdjustmentMenuPlayerController();

	/** Called once by AAdjustmentMenuGameMode::TransitionToTitleIntro -- binds the broad any-key set and starts its arm-delay window. See class comment. */
	void EnableAnyKeyDetection();

	/** Resets the any-key "already consumed" latch and restarts the arm-delay window, so this same controller can catch a second "any input" press (leave the title loop, then separately skip the intro portion). Mirrors AAnyInputPlayerControllerBase::Rearm. */
	void RearmAnyKey();

protected:
	virtual void SetupInputComponent() override;

private:
	void HandleMenuUp();
	void HandleMenuDown();
	void HandleMenuLeft();
	void HandleMenuRight();
	void HandleMenuConfirm();

	/** Bound to every digital key once EnableAnyKeyDetection runs. Debounce-guarded so a mash of several buttons in the same frame still only fires AAdjustmentMenuGameMode::HandleAnyInput once. */
	void HandleAnyKeyPress();

	/** Latched on the first accepted any-key press since the last EnableAnyKeyDetection/RearmAnyKey. */
	bool bAnyKeyConsumed = false;

	/** World time (seconds) before which HandleAnyKeyPress ignores every press -- guards against the Enter/Space/A press that confirmed the Adjustment Menu still being physically down (or its down-event still in flight) the instant the any-key set binds. Same 0.35s window AAnyInputPlayerControllerBase uses. */
	float AnyKeyArmTime = 0.f;

	/** Set once by EnableAnyKeyDetection so a second call (there shouldn't be one, but just in case) doesn't double-bind every key. */
	bool bAnyKeyDetectionEnabled = false;
};
