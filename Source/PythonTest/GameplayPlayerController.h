#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayPlayerController.generated.h"

class UPauseMenuWidget;

/**
 * Player controller for real gameplay (set as BP_DeathMetalCatGameMode's PlayerControllerClass by
 * AgentScripts/ue_create_pause_screen_assets.py). Its entire job beyond the engine default is
 * owning the pause screen: opening/closing it, and routing its own raw key input to it.
 *
 * Pause/menu-navigation input is bound here with plain InputComponent->BindKey calls, NOT through
 * the project's Enhanced Input setup (IMC_PlayerControls, added by ADeathMetalCatCharacter) --
 * deliberately, for two reasons:
 *
 *  1. Blocking gameplay input while paused needs to be airtight, and DisableInput(PlayerController)
 *     is the one built-in mechanism that guarantees it: it flips bBlockInput on whichever
 *     InputComponent it's called against, silently dropping every key/action bound there, Enhanced
 *     Input included. Called against the CHARACTER's own component (Character->DisableInput(this)),
 *     it can't also disable pause/menu-navigation input, because those live on a DIFFERENT
 *     InputComponent -- this controller's own, which DisableInput on the character never touches.
 *     Auditing and individually gating every one of ADeathMetalCatCharacter's ~13 Enhanced-Input-
 *     bound handlers instead would be far more invasive and easy to miss one of.
 *
 *  2. APlayerController keeps receiving and processing input even while UGameplayStatics::
 *     SetGamePaused(true) has frozen the World tick -- that's the whole mechanism pause menus rely
 *     on. Raw BindKey bindings on THIS controller's InputComponent work with that guarantee
 *     directly; they don't depend on which Enhanced Input mapping contexts happen to be active,
 *     which side-steps ever having to reason about IMC priority/removal ordering for this feature.
 *
 * Key layout (each also documents which existing gameplay control it deliberately reuses the
 * physical key for, since the brief asked to reuse the established bindings rather than invent an
 * unrelated new layout):
 *   Pause toggle : Escape, Gamepad Start        -- new; genre-standard, nothing else uses these.
 *   Menu Up/Down : Arrows/W,S, D-Pad, Left Stick -- new; there's no existing vertical axis in
 *                                                    gameplay to reuse (movement is 2D-platformer
 *                                                    horizontal-only), so this introduces the only
 *                                                    genuinely new input concept here.
 *   Menu Left/Right : Arrows/A,D, D-Pad, Left Stick -- SAME physical keys as MoveRightAction's
 *                                                    left/right, reused for nudging the Options
 *                                                    page's Sound/Brightness sliders.
 *   Menu Confirm : Enter/Space, Gamepad FaceButton_Bottom -- SAME physical key as JumpAction.
 *   Menu Back    : the pause-toggle key itself, not a separate binding -- see HandlePauseToggle.
 *                  Chosen over a dedicated cancel button because the pause key is already the one
 *                  input guaranteed to mean "get me out of here" at every depth of this menu, and
 *                  it keeps the key count down; a player who presses it from a sub-page lands on
 *                  the main list (one step out), and pressing it again from there closes the menu
 *                  entirely (fully out) -- each press backs out exactly one level, which is the
 *                  standard, predictable shape for this kind of back navigation.
 */
UCLASS()
class PYTHONTEST_API AGameplayPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AGameplayPlayerController();

	/** Called by UPauseMenuWidget::Confirm() when RESUME is selected. */
	void RequestResume();

	/** Called by UPauseMenuWidget::Confirm() when QUIT TO TITLE is selected -- unpauses (a fresh level should never inherit a paused world) and opens L_TitleScreen. */
	void RequestQuitToTitle();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	/** If not paused, opens the pause menu. If paused and on a sub-page, backs out to the main list. If paused and already on the main list, closes the menu (same as RequestResume). See class comment. */
	void HandlePauseToggle();

	void HandleMenuUp();
	void HandleMenuDown();
	void HandleMenuLeft();
	void HandleMenuRight();
	void HandleMenuConfirm();

	/** SetGamePaused(true), disables the character's own input, lazily creates PauseMenuWidget if needed, and shows its main page. */
	void OpenPause();

	/** SetGamePaused(false), re-enables the character's input, hides PauseMenuWidget. */
	void ClosePause();

	UPROPERTY()
	TObjectPtr<UPauseMenuWidget> PauseMenuWidgetInstance;

	bool bIsPaused = false;
};
