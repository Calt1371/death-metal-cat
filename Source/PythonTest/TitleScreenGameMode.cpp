#include "TitleScreenGameMode.h"

#include "AnyInputPlayerControllerBase.h"
#include "TitleScreenPlayerController.h"
#include "TitleIntroCombinedWidget.h"
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
		UE_LOG(LogTemp, Error, TEXT("[TITLE] No player controller at BeginPlay -- widget not created."));
		return;
	}

	CombinedWidget = CreateWidget<UTitleIntroCombinedWidget>(PC, UTitleIntroCombinedWidget::StaticClass());
	if (!CombinedWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("[TITLE] Failed to create the title/intro widget."));
		return;
	}

	CombinedWidget->SetOnReadyForGameplayDelegate(FSimpleDelegate::CreateUObject(this, &ATitleScreenGameMode::HandleReadyForGameplay));
	CombinedWidget->AddToViewport();

	UE_LOG(LogTemp, Log, TEXT("[TITLE] Title screen up -- video cycle running, waiting for any input."));
}

void ATitleScreenGameMode::HandleAnyInput()
{
	if (CombinedWidget)
	{
		CombinedWidget->NotifyAnyInput();
	}

	// Re-arms the SAME controller instance for the NEXT "any input" press -- this one continuous
	// screen can legitimately receive it twice (leave the title loop, then skip the intro portion),
	// unlike the one-shot-per-screen shape AAnyInputPlayerControllerBase was originally built for.
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (AAnyInputPlayerControllerBase* AnyInputPC = Cast<AAnyInputPlayerControllerBase>(PC))
		{
			AnyInputPC->Rearm();
		}
	}
}

void ATitleScreenGameMode::HandleReadyForGameplay()
{
	if (bReadyForGameplayTriggered)
	{
		return;
	}
	bReadyForGameplayTriggered = true;

	if (CombinedWidget)
	{
		CombinedWidget->BeginFadeToBlack(EndFadeDuration);
	}

	GetWorldTimerManager().SetTimer(
		GameplayTransitionTimer, this, &ATitleScreenGameMode::FinishToGameplay, EndFadeDuration, false);
}

void ATitleScreenGameMode::FinishToGameplay()
{
	if (GameplayMap.IsNull())
	{
		// Falls back to holding on black if BP_TitleScreenGameMode's GameplayMap somehow ended up
		// unset -- AgentScripts/ue_create_intro_cinematic_assets.py points it at L_ControllerTestRange,
		// but this keeps a bad/cleared reference from hard-failing instead of just logging.
		UE_LOG(LogTemp, Warning,
			TEXT("[INTRO] >>> READY FOR GAMEPLAY -- would proceed here. ")
			TEXT("No GameplayMap set, holding on black. Set it on BP_TitleScreenGameMode."));
		return;
	}

	const FString PackageName = GameplayMap.ToSoftObjectPath().GetLongPackageName();
	UE_LOG(LogTemp, Log, TEXT("[INTRO] Opening gameplay map: %s"), *PackageName);
	UGameplayStatics::OpenLevel(this, FName(*PackageName));
}
