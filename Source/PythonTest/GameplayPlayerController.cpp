#include "GameplayPlayerController.h"

#include "PauseMenuWidget.h"
#include "DeathMetalCatCharacter.h"
#include "DMCGameInstance.h"
#include "Components/InputComponent.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	// Hardcoded rather than an EditDefaultsOnly TSoftObjectPtr (as ATitleScreenGameMode's
	// IntroCinematicMap / AIntroCinematicGameMode's GameplayMap both are), because this controller
	// is assigned directly as BP_DeathMetalCatGameMode's PlayerControllerClass via its raw C++ class
	// (same as ATitleScreenPlayerController), with no Blueprint wrapper of its own to hold an
	// editable field on. If L_TitleScreen is ever renamed, update this to match.
	const TCHAR* TitleScreenLevelPath = TEXT("/Game/Maps/L_TitleScreen");
}

AGameplayPlayerController::AGameplayPlayerController()
{
	bShowMouseCursor = false;
}

void AGameplayPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (UDMCGameInstance* GameInstance = Cast<UDMCGameInstance>(GetGameInstance()))
	{
		GameInstance->ApplyStartupSettings(this);
	}
}

void AGameplayPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[PAUSE] No InputComponent on the gameplay controller -- pause menu will not work."));
		return;
	}

	// Pause toggle -- see class comment for why this (and everything else here) is a raw key bind
	// rather than an Enhanced Input action.
	//
	// Every BindKey call below routes through this helper rather than calling BindKey directly,
	// because UInputComponent bindings default bExecuteWhenPaused to FALSE: the moment
	// SetGamePaused(true) fires (i.e. from the very first successful pause onward), the engine
	// silently stops delivering input to any binding that hasn't explicitly opted in, INCLUDING the
	// pause-toggle key itself. Confirmed live: pausing worked, but nothing -- not Escape again, not
	// navigation, nothing -- could get back out, because every single one of these bindings was
	// being dropped the instant the game was actually paused. This one flag on every binding here is
	// what makes the pause screen usable at all once opened.
	auto BindPauseKey = [this](const FKey& Key, EInputEvent Event, void (AGameplayPlayerController::*Handler)())
	{
		FInputKeyBinding& Binding = InputComponent->BindKey(Key, Event, this, Handler);
		Binding.bExecuteWhenPaused = true;
	};

	BindPauseKey(EKeys::Escape, IE_Pressed, &AGameplayPlayerController::HandlePauseToggle);
	BindPauseKey(EKeys::Gamepad_Special_Right, IE_Pressed, &AGameplayPlayerController::HandlePauseToggle);

	// Menu Up/Down -- a genuinely new input concept (see class comment), so no existing gameplay
	// action's keys to reuse.
	for (const FKey& Key : { EKeys::Up, EKeys::W, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_LeftStick_Up })
	{
		BindPauseKey(Key, IE_Pressed, &AGameplayPlayerController::HandleMenuUp);
		BindPauseKey(Key, IE_Repeat, &AGameplayPlayerController::HandleMenuUp);
	}
	for (const FKey& Key : { EKeys::Down, EKeys::S, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_LeftStick_Down })
	{
		BindPauseKey(Key, IE_Pressed, &AGameplayPlayerController::HandleMenuDown);
		BindPauseKey(Key, IE_Repeat, &AGameplayPlayerController::HandleMenuDown);
	}

	// Menu Left/Right -- same physical keys as MoveRightAction's left/right, reused here to nudge
	// the Options page's sliders (see class comment).
	for (const FKey& Key : { EKeys::Left, EKeys::A, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_LeftStick_Left })
	{
		BindPauseKey(Key, IE_Pressed, &AGameplayPlayerController::HandleMenuLeft);
		BindPauseKey(Key, IE_Repeat, &AGameplayPlayerController::HandleMenuLeft);
	}
	for (const FKey& Key : { EKeys::Right, EKeys::D, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_LeftStick_Right })
	{
		BindPauseKey(Key, IE_Pressed, &AGameplayPlayerController::HandleMenuRight);
		BindPauseKey(Key, IE_Repeat, &AGameplayPlayerController::HandleMenuRight);
	}

	// Menu Confirm -- same physical key as JumpAction.
	BindPauseKey(EKeys::Enter, IE_Pressed, &AGameplayPlayerController::HandleMenuConfirm);
	BindPauseKey(EKeys::SpaceBar, IE_Pressed, &AGameplayPlayerController::HandleMenuConfirm);
	BindPauseKey(EKeys::Gamepad_FaceButton_Bottom, IE_Pressed, &AGameplayPlayerController::HandleMenuConfirm);

	UE_LOG(LogTemp, Log, TEXT("[PAUSE] Pause/menu input bound."));
}

void AGameplayPlayerController::HandlePauseToggle()
{
	if (!bIsPaused)
	{
		OpenPause();
		return;
	}

	// Paused, and on a sub-page: back out one level rather than closing the whole menu. See class
	// comment for why the pause key itself doubles as Back.
	if (PauseMenuWidgetInstance && PauseMenuWidgetInstance->GetCurrentPage() != EPausePage::Main)
	{
		PauseMenuWidgetInstance->GoBack();
		return;
	}

	ClosePause();
}

void AGameplayPlayerController::HandleMenuUp()
{
	if (bIsPaused && PauseMenuWidgetInstance)
	{
		PauseMenuWidgetInstance->NavigateUp();
	}
}

void AGameplayPlayerController::HandleMenuDown()
{
	if (bIsPaused && PauseMenuWidgetInstance)
	{
		PauseMenuWidgetInstance->NavigateDown();
	}
}

void AGameplayPlayerController::HandleMenuLeft()
{
	if (bIsPaused && PauseMenuWidgetInstance)
	{
		PauseMenuWidgetInstance->NavigateLeft();
	}
}

void AGameplayPlayerController::HandleMenuRight()
{
	if (bIsPaused && PauseMenuWidgetInstance)
	{
		PauseMenuWidgetInstance->NavigateRight();
	}
}

void AGameplayPlayerController::HandleMenuConfirm()
{
	if (bIsPaused && PauseMenuWidgetInstance)
	{
		PauseMenuWidgetInstance->Confirm();
	}
}

void AGameplayPlayerController::OpenPause()
{
	bIsPaused = true;
	UGameplayStatics::SetGamePaused(this, true);

	if (ADeathMetalCatCharacter* PlayerCharacter = GetPawn<ADeathMetalCatCharacter>())
	{
		// The one call that makes pausing airtight -- see class comment for why this, not
		// per-handler guards, and why it can't also disable this controller's own pause/menu input.
		PlayerCharacter->DisableInput(this);
	}

	if (!PauseMenuWidgetInstance)
	{
		PauseMenuWidgetInstance = CreateWidget<UPauseMenuWidget>(this, UPauseMenuWidget::StaticClass());
		if (PauseMenuWidgetInstance)
		{
			PauseMenuWidgetInstance->SetOwningController(this);
			PauseMenuWidgetInstance->AddToViewport();
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[PAUSE] Failed to create the pause menu widget."));
		}
	}

	if (PauseMenuWidgetInstance)
	{
		PauseMenuWidgetInstance->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		PauseMenuWidgetInstance->ShowMainPage();
	}

	UE_LOG(LogTemp, Log, TEXT("[PAUSE] Paused."));
}

void AGameplayPlayerController::ClosePause()
{
	bIsPaused = false;
	UGameplayStatics::SetGamePaused(this, false);

	if (ADeathMetalCatCharacter* PlayerCharacter = GetPawn<ADeathMetalCatCharacter>())
	{
		PlayerCharacter->EnableInput(this);
	}

	if (PauseMenuWidgetInstance)
	{
		PauseMenuWidgetInstance->SetVisibility(ESlateVisibility::Collapsed);
	}

	UE_LOG(LogTemp, Log, TEXT("[PAUSE] Resumed."));
}

void AGameplayPlayerController::RequestResume()
{
	ClosePause();
}

void AGameplayPlayerController::RequestQuitToTitle()
{
	// A freshly-loaded level should never inherit a paused world.
	bIsPaused = false;
	UGameplayStatics::SetGamePaused(this, false);

	UE_LOG(LogTemp, Log, TEXT("[PAUSE] Quitting to title: %s"), TitleScreenLevelPath);
	UGameplayStatics::OpenLevel(this, FName(TitleScreenLevelPath));
}
