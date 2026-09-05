#include "AdjustmentMenuGameMode.h"

#include "AdjustmentMenuPlayerController.h"
#include "AdjustmentMenuWidget.h"
#include "TitleIntroCombinedWidget.h"
#include "DMCGameInstance.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace
{
	// Hardcoded rather than an EditDefaultsOnly TSoftObjectPtr, same reasoning as
	// AGameplayPlayerController's TitleScreenLevelPath: this GameMode is assigned directly as
	// L_TitleScreen's World Settings GameMode Override via its raw C++ class, with no Blueprint
	// wrapper of its own to hold an editable field on.
	const TCHAR* RealGameplayMapPath = TEXT("/Game/L_ControllerTestRange");

	// Same track ARoomProgressionManager loops during real gameplay -- see class comment.
	const TCHAR* BackgroundMusicPath = TEXT("/Game/Audio/Music/DMC_Music.DMC_Music");

	constexpr float EndFadeDuration = 0.5f;
}

AAdjustmentMenuGameMode::AAdjustmentMenuGameMode()
{
	PlayerControllerClass = AAdjustmentMenuPlayerController::StaticClass();

	// No pawn on this screen -- see class comment.
	DefaultPawnClass = nullptr;

	// AGameModeBase has no root component of its own, so this becomes it outright -- its own
	// transform is irrelevant anyway (bIsUISound: a plain 2D sound, no spatial attenuation). Same
	// constructor pattern as ARoomProgressionManager::MusicAudioComponent. bAutoActivate false: the
	// sound asset isn't loaded yet this early, so BeginPlay calls Play() explicitly once it is.
	MusicAudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("MusicAudioComponent"));
	RootComponent = MusicAudioComponent;
	MusicAudioComponent->bAutoActivate = false;
	MusicAudioComponent->bIsUISound = true;
}

void AAdjustmentMenuGameMode::BeginPlay()
{
	Super::BeginPlay();

	UDMCGameInstance* GameInstance = Cast<UDMCGameInstance>(GetGameInstance());
	if (GameInstance)
	{
		// Force SFX/Music silent every session regardless of what was saved -- see
		// UAdjustmentMenuWidget's class comment. Each setter applies immediately
		// (SetSoundMixClassOverride) and re-applies Brightness too (ApplyStartupSettings handles all
		// three), so no separate ApplyStartupSettings call is needed here.
		GameInstance->SetSFXVolume(this, 0.f);
		GameInstance->SetMusicVolume(this, 0.f);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ADJUSTMENT MENU] GameInstance is not a UDMCGameInstance -- settings will not be force-reset or applied."));
	}

	// A real owned AudioComponent, not PlaySound2D's fire-and-forget call -- see class comment.
	if (USoundBase* BackgroundMusic = LoadObject<USoundBase>(nullptr, BackgroundMusicPath))
	{
		MusicAudioComponent->SetSound(BackgroundMusic);
		MusicAudioComponent->Play();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ADJUSTMENT MENU] Failed to load background music: %s"), BackgroundMusicPath);
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		UE_LOG(LogTemp, Error, TEXT("[ADJUSTMENT MENU] No player controller at BeginPlay -- widgets not created."));
		return;
	}

	// Created and opened immediately -- right here, at the earliest possible moment in the whole
	// process -- but held fully silent/invisible until the player confirms out of the Adjustment
	// Menu. See class comment for why this can't simply wait until TransitionToTitleIntro.
	CombinedWidget = CreateWidget<UTitleIntroCombinedWidget>(PC, UTitleIntroCombinedWidget::StaticClass());
	if (CombinedWidget)
	{
		CombinedWidget->SetOnReadyForGameplayDelegate(FSimpleDelegate::CreateUObject(this, &AAdjustmentMenuGameMode::HandleReadyForGameplay));
		CombinedWidget->SetHeldForReveal(true);
		CombinedWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
		CombinedWidget->SetRenderOpacity(0.f);
		CombinedWidget->AddToViewport(0);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[TITLE] Failed to create the title/intro widget."));
	}

	AdjustmentMenuWidgetInstance = CreateWidget<UAdjustmentMenuWidget>(PC, UAdjustmentMenuWidget::StaticClass());
	if (!AdjustmentMenuWidgetInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("[ADJUSTMENT MENU] Failed to create the adjustment menu widget."));
		return;
	}

	AdjustmentMenuWidgetInstance->AddToViewport(10);

	UE_LOG(LogTemp, Log, TEXT("[ADJUSTMENT MENU] Up -- SFX/Music forced silent, music preview looping, title video pre-opened silently, waiting for input."));
}

void AAdjustmentMenuGameMode::HandleMenuUp()
{
	if (CurrentPhase == EPhase::AdjustmentMenu && AdjustmentMenuWidgetInstance)
	{
		AdjustmentMenuWidgetInstance->NavigateUp();
	}
}

void AAdjustmentMenuGameMode::HandleMenuDown()
{
	if (CurrentPhase == EPhase::AdjustmentMenu && AdjustmentMenuWidgetInstance)
	{
		AdjustmentMenuWidgetInstance->NavigateDown();
	}
}

void AAdjustmentMenuGameMode::HandleMenuLeft()
{
	if (CurrentPhase == EPhase::AdjustmentMenu && AdjustmentMenuWidgetInstance)
	{
		AdjustmentMenuWidgetInstance->NavigateLeft();
	}
}

void AAdjustmentMenuGameMode::HandleMenuRight()
{
	if (CurrentPhase == EPhase::AdjustmentMenu && AdjustmentMenuWidgetInstance)
	{
		AdjustmentMenuWidgetInstance->NavigateRight();
	}
}

void AAdjustmentMenuGameMode::HandleMenuConfirm()
{
	if (CurrentPhase != EPhase::AdjustmentMenu)
	{
		// Already past the Adjustment Menu -- Enter/Space/A are now among the many keys the any-key
		// detection catches instead (see HandleAnyInput), so there's nothing left for this narrow
		// binding to do.
		return;
	}

	TransitionToTitleIntro();
}

void AAdjustmentMenuGameMode::TransitionToTitleIntro()
{
	CurrentPhase = EPhase::TitleIntro;

	if (AdjustmentMenuWidgetInstance)
	{
		AdjustmentMenuWidgetInstance->RemoveFromParent();
		AdjustmentMenuWidgetInstance = nullptr;
	}

	// Already created and playing silently in the background since BeginPlay -- see class comment.
	// Reveal it in place rather than creating it fresh; there is no second OpenSource call here.
	if (CombinedWidget)
	{
		CombinedWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		CombinedWidget->SetRenderOpacity(1.f);
		CombinedWidget->SetHeldForReveal(false);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[TITLE] CombinedWidget was never created at BeginPlay -- nothing to reveal."));
	}

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (AAdjustmentMenuPlayerController* AdjustmentMenuPC = Cast<AAdjustmentMenuPlayerController>(PC))
		{
			AdjustmentMenuPC->EnableAnyKeyDetection();
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[TITLE] Title screen revealed -- video cycle running, waiting for any input."));
}

void AAdjustmentMenuGameMode::HandleAnyInput()
{
	if (CombinedWidget)
	{
		CombinedWidget->NotifyAnyInput();
	}

	// Re-arms the SAME controller instance for the NEXT "any input" press -- this one continuous
	// screen can legitimately receive it twice (leave the title loop, then skip the intro portion).
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (AAdjustmentMenuPlayerController* AdjustmentMenuPC = Cast<AAdjustmentMenuPlayerController>(PC))
		{
			AdjustmentMenuPC->RearmAnyKey();
		}
	}
}

void AAdjustmentMenuGameMode::HandleReadyForGameplay()
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
		GameplayTransitionTimer, this, &AAdjustmentMenuGameMode::FinishToGameplay, EndFadeDuration, false);
}

void AAdjustmentMenuGameMode::FinishToGameplay()
{
	UE_LOG(LogTemp, Log, TEXT("[INTRO] Opening gameplay map: %s"), RealGameplayMapPath);
	UGameplayStatics::OpenLevel(this, FName(RealGameplayMapPath));
}
