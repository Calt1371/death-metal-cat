#include "TitleScreenGameMode.h"

#include "TitleScreenPlayerController.h"
#include "TitleScreenWidget.h"
#include "DMCGameInstance.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

ATitleScreenGameMode::ATitleScreenGameMode()
{
	PlayerControllerClass = ATitleScreenPlayerController::StaticClass();

	// No pawn on the title screen -- see the class comment.
	DefaultPawnClass = nullptr;
}

void ATitleScreenGameMode::BeginPlay()
{
	Super::BeginPlay();

	// L_TitleScreen is this game's actual entry point (GameDefaultMap), so this is the earliest a
	// valid World/audio device exists to apply the player's saved volume/brightness against -- see
	// UDMCGameInstance's class comment for why Init() itself can't do this.
	if (UDMCGameInstance* GameInstance = Cast<UDMCGameInstance>(GetGameInstance()))
	{
		GameInstance->ApplyStartupSettings(this);
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		UE_LOG(LogTemp, Error, TEXT("[TITLE] No player controller at BeginPlay -- title widget not created."));
		return;
	}

	TitleWidget = CreateWidget<UTitleScreenWidget>(PC, UTitleScreenWidget::StaticClass());
	if (!TitleWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("[TITLE] Failed to create the title screen widget."));
		return;
	}

	TitleWidget->AddToViewport();
	UE_LOG(LogTemp, Log, TEXT("[TITLE] Title screen up -- video cycle running, waiting for any input."));
}

void ATitleScreenGameMode::HandleStartPressed()
{
	if (bStartTriggered)
	{
		return;
	}
	bStartTriggered = true;

	if (TitleWidget)
	{
		TitleWidget->BeginFadeToBlack(StartFadeDuration);
	}

	GetWorldTimerManager().SetTimer(
		StartTransitionTimer, this, &ATitleScreenGameMode::FinishStartTransition, StartFadeDuration, false);
}

void ATitleScreenGameMode::FinishStartTransition()
{
	if (IntroCinematicMap.IsNull())
	{
		// Falls back to holding on black if BP_TitleScreenGameMode's IntroCinematicMap somehow ended
		// up unset -- AgentScripts/ue_create_title_screen_assets.py points it at L_IntroCinematic,
		// but this keeps a bad/cleared reference from hard-failing instead of just logging.
		UE_LOG(LogTemp, Warning,
			TEXT("[TITLE] >>> START TRIGGERED -- would proceed to the intro cinematic here. ")
			TEXT("No IntroCinematicMap set, holding on black. Set it on BP_TitleScreenGameMode once the cinematic map exists."));
		return;
	}

	const FString PackageName = IntroCinematicMap.ToSoftObjectPath().GetLongPackageName();
	UE_LOG(LogTemp, Log, TEXT("[TITLE] Opening intro cinematic map: %s"), *PackageName);
	UGameplayStatics::OpenLevel(this, FName(*PackageName));
}
