#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "IntroCinematicGameMode.generated.h"

class UIntroCinematicWidget;

/**
 * Game mode for L_IntroCinematic. Owns the cinematic widget and the hand-off into real gameplay.
 *
 * There is deliberately no pawn, same as ATitleScreenGameMode and for the same reason:
 * DefaultPawnClass is null, so the player controller exists on its own with nothing to possess,
 * and the whole screen is the widget.
 */
UCLASS()
class PYTHONTEST_API AIntroCinematicGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AIntroCinematicGameMode();

	/**
	 * The single hand-off point out of the cinematic, called from two different places that must
	 * behave identically from here on: AIntroCinematicPlayerController::OnAnyInputDetected (the
	 * player skipped) and UIntroCinematicWidget::HandleMediaEndReached (the video finished on its
	 * own). Starts the widget's fade to black and, once it completes, opens GameplayMap. Idempotent
	 * -- whichever of the two fires first wins; the other is a no-op.
	 */
	void HandleCinematicEnd();

protected:
	virtual void BeginPlay() override;

	/**
	 * The real gameplay map, opened once the fade completes. Set to L_ControllerTestRange (the
	 * hand-built live level -- see CLAUDE.md's "Room1 in the live level" notes) on
	 * BP_IntroCinematicGameMode by AgentScripts/ue_create_intro_cinematic_assets.py.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Intro Cinematic")
	TSoftObjectPtr<UWorld> GameplayMap;

	/** How long the fade to black takes once the cinematic ends (skipped or completed). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Intro Cinematic")
	float EndFadeDuration = 0.5f;

private:
	/** Fires EndFadeDuration after HandleCinematicEnd -- see FinishCinematicEnd. */
	void FinishCinematicEnd();

	UPROPERTY()
	TObjectPtr<UIntroCinematicWidget> CinematicWidget;

	FTimerHandle EndTransitionTimer;

	/** Latched by the first HandleCinematicEnd call. */
	bool bEndTriggered = false;
};
