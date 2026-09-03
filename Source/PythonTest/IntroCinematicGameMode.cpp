#include "IntroCinematicGameMode.h"

#include "IntroCinematicPlayerController.h"
#include "IntroCinematicWidget.h"
#include "DMCGameInstance.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AIntroCinematicGameMode::AIntroCinematicGameMode()
{
	PlayerControllerClass = AIntroCinematicPlayerController::StaticClass();

	// No pawn during the cinematic -- see the class comment.
	DefaultPawnClass = nullptr;
}

void AIntroCinematicGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Idempotent re-application (already applied once in ATitleScreenGameMode::BeginPlay) -- see
	// UDMCGameInstance's class comment.
	if (UDMCGameInstance* GameInstance = Cast<UDMCGameInstance>(GetGameInstance()))
	{
		GameInstance->ApplyStartupSettings(this);
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		UE_LOG(LogTemp, Error, TEXT("[INTRO] No player controller at BeginPlay -- cinematic widget not created."));
		return;
	}

	CinematicWidget = CreateWidget<UIntroCinematicWidget>(PC, UIntroCinematicWidget::StaticClass());
	if (!CinematicWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("[INTRO] Failed to create the intro cinematic widget."));
		return;
	}

	CinematicWidget->SetOwningGameMode(this);
	CinematicWidget->AddToViewport();
	UE_LOG(LogTemp, Log, TEXT("[INTRO] Intro cinematic up -- playing, waiting for any input to skip."));
}

void AIntroCinematicGameMode::HandleCinematicEnd()
{
	if (bEndTriggered)
	{
		return;
	}
	bEndTriggered = true;

	if (CinematicWidget)
	{
		CinematicWidget->BeginFadeToBlack(EndFadeDuration);
	}

	GetWorldTimerManager().SetTimer(
		EndTransitionTimer, this, &AIntroCinematicGameMode::FinishCinematicEnd, EndFadeDuration, false);
}

void AIntroCinematicGameMode::FinishCinematicEnd()
{
	if (GameplayMap.IsNull())
	{
		// Falls back to holding on black if BP_IntroCinematicGameMode's GameplayMap somehow ended up
		// unset -- AgentScripts/ue_create_intro_cinematic_assets.py points it at L_ControllerTestRange,
		// but this keeps a bad/cleared reference from hard-failing instead of just logging.
		UE_LOG(LogTemp, Warning,
			TEXT("[INTRO] >>> CINEMATIC END TRIGGERED -- would proceed to gameplay here. ")
			TEXT("No GameplayMap set, holding on black. Set it on BP_IntroCinematicGameMode."));
		return;
	}

	const FString PackageName = GameplayMap.ToSoftObjectPath().GetLongPackageName();
	UE_LOG(LogTemp, Log, TEXT("[INTRO] Opening gameplay map: %s"), *PackageName);
	UGameplayStatics::OpenLevel(this, FName(*PackageName));
}
