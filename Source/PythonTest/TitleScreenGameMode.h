#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TitleScreenGameMode.generated.h"

class UTitleScreenWidget;

/**
 * Game mode for L_TitleScreen. Owns the title widget and the hand-off out of the title screen.
 *
 * There is deliberately no pawn: DefaultPawnClass is null, so the player controller exists on its
 * own with nothing to possess. The whole screen is the widget, and ATitleScreenPlayerController
 * needs no pawn to receive raw key binds.
 */
UCLASS()
class PYTHONTEST_API ATitleScreenGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATitleScreenGameMode();

	/**
	 * Called by ATitleScreenPlayerController the moment any key or button is pressed. Starts the
	 * widget's fade to black and, once it completes, opens IntroCinematicMap (or just holds on
	 * black if that hasn't been set yet). Idempotent -- later presses during the fade are ignored.
	 */
	void HandleStartPressed();

protected:
	virtual void BeginPlay() override;

	/**
	 * The level to open once the fade completes. Left unset for now: the intro cinematic doesn't
	 * exist yet, so out of the box this screen fades to black and logs that it got there. Set this
	 * on BP_TitleScreenGameMode once the cinematic map exists and no code change is needed.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Title Screen")
	TSoftObjectPtr<UWorld> IntroCinematicMap;

	/** How long the fade to black takes once the player presses something. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Title Screen")
	float StartFadeDuration = 0.5f;

private:
	/** Fires StartFadeDuration after HandleStartPressed -- see FinishStartTransition. */
	void FinishStartTransition();

	UPROPERTY()
	TObjectPtr<UTitleScreenWidget> TitleWidget;

	FTimerHandle StartTransitionTimer;

	/** Latched by the first HandleStartPressed call. */
	bool bStartTriggered = false;
};
