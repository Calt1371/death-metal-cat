#include "AdjustmentMenuPlayerController.h"

#include "AdjustmentMenuGameMode.h"
#include "Components/InputComponent.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	// Same 0.35s window AAnyInputPlayerControllerBase uses -- see that class's InputArmDelay comment
	// for why this exists (guards against the press that dismissed the previous screen/phase still
	// being physically down the instant the new binding set arms).
	constexpr float AnyKeyArmDelay = 0.35f;
}

AAdjustmentMenuPlayerController::AAdjustmentMenuPlayerController()
{
	bShowMouseCursor = false;
}

void AAdjustmentMenuPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[ADJUSTMENT MENU] No InputComponent -- menu will not work."));
		return;
	}

	// Up/Down -- adjust the selected slider's value.
	for (const FKey& Key : { EKeys::Up, EKeys::W, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_LeftStick_Up })
	{
		InputComponent->BindKey(Key, IE_Pressed, this, &AAdjustmentMenuPlayerController::HandleMenuUp);
		InputComponent->BindKey(Key, IE_Repeat, this, &AAdjustmentMenuPlayerController::HandleMenuUp);
	}
	for (const FKey& Key : { EKeys::Down, EKeys::S, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_LeftStick_Down })
	{
		InputComponent->BindKey(Key, IE_Pressed, this, &AAdjustmentMenuPlayerController::HandleMenuDown);
		InputComponent->BindKey(Key, IE_Repeat, this, &AAdjustmentMenuPlayerController::HandleMenuDown);
	}

	// Left/Right -- select Brightness/SFX/Music.
	for (const FKey& Key : { EKeys::Left, EKeys::A, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_LeftStick_Left })
	{
		InputComponent->BindKey(Key, IE_Pressed, this, &AAdjustmentMenuPlayerController::HandleMenuLeft);
		InputComponent->BindKey(Key, IE_Repeat, this, &AAdjustmentMenuPlayerController::HandleMenuLeft);
	}
	for (const FKey& Key : { EKeys::Right, EKeys::D, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_LeftStick_Right })
	{
		InputComponent->BindKey(Key, IE_Pressed, this, &AAdjustmentMenuPlayerController::HandleMenuRight);
		InputComponent->BindKey(Key, IE_Repeat, this, &AAdjustmentMenuPlayerController::HandleMenuRight);
	}

	// Confirm -- leave the Adjustment Menu for the title/intro experience.
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AAdjustmentMenuPlayerController::HandleMenuConfirm);
	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AAdjustmentMenuPlayerController::HandleMenuConfirm);
	InputComponent->BindKey(EKeys::Gamepad_FaceButton_Bottom, IE_Pressed, this, &AAdjustmentMenuPlayerController::HandleMenuConfirm);

	UE_LOG(LogTemp, Log, TEXT("[ADJUSTMENT MENU] Input bound."));

	// The broad any-key set is NOT bound here -- see class comment. EnableAnyKeyDetection binds it
	// later, on demand.
}

void AAdjustmentMenuPlayerController::EnableAnyKeyDetection()
{
	if (bAnyKeyDetectionEnabled)
	{
		return;
	}
	bAnyKeyDetectionEnabled = true;

	if (!InputComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[TITLE] No InputComponent -- any-key input will not work."));
		return;
	}

	TArray<FKey> AllKeys;
	EKeys::GetAllKeys(AllKeys);

	int32 BoundCount = 0;
	int32 GamepadCount = 0;
	for (const FKey& Key : AllKeys)
	{
		// Thumbstick/trigger axes report continuously; a resting controller's drift would fire
		// instantly. Their digital counterparts (Gamepad_LeftTrigger, Gamepad_LeftStick_Up and
		// friends) are still bound below, so triggers and stick pushes do work.
		if (Key.IsAnalog())
		{
			continue;
		}

		if (!Key.IsBindableInBlueprints())
		{
			continue;
		}

		InputComponent->BindKey(Key, IE_Pressed, this, &AAdjustmentMenuPlayerController::HandleAnyKeyPress);
		++BoundCount;
		if (Key.IsGamepadKey())
		{
			++GamepadCount;
		}
	}

	RearmAnyKey();

	UE_LOG(LogTemp, Log, TEXT("[TITLE] Bound %d digital keys for any-button input (%d of them gamepad)."), BoundCount, GamepadCount);
}

void AAdjustmentMenuPlayerController::RearmAnyKey()
{
	bAnyKeyConsumed = false;

	if (const UWorld* World = GetWorld())
	{
		AnyKeyArmTime = World->GetTimeSeconds() + AnyKeyArmDelay;
	}
}

void AAdjustmentMenuPlayerController::HandleAnyKeyPress()
{
	if (bAnyKeyConsumed)
	{
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		if (World->GetTimeSeconds() < AnyKeyArmTime)
		{
			// Still within the grace window -- almost certainly the Confirm press that ended the
			// Adjustment Menu leaking across, not a genuine new press. See AnyKeyArmTime.
			return;
		}
	}

	bAnyKeyConsumed = true;

	UE_LOG(LogTemp, Log, TEXT("[TITLE] Input detected."));

	if (AAdjustmentMenuGameMode* GameMode = Cast<AAdjustmentMenuGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		GameMode->HandleAnyInput();
	}
}

void AAdjustmentMenuPlayerController::HandleMenuUp()
{
	if (AAdjustmentMenuGameMode* GameMode = Cast<AAdjustmentMenuGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		GameMode->HandleMenuUp();
	}
}

void AAdjustmentMenuPlayerController::HandleMenuDown()
{
	if (AAdjustmentMenuGameMode* GameMode = Cast<AAdjustmentMenuGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		GameMode->HandleMenuDown();
	}
}

void AAdjustmentMenuPlayerController::HandleMenuLeft()
{
	if (AAdjustmentMenuGameMode* GameMode = Cast<AAdjustmentMenuGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		GameMode->HandleMenuLeft();
	}
}

void AAdjustmentMenuPlayerController::HandleMenuRight()
{
	if (AAdjustmentMenuGameMode* GameMode = Cast<AAdjustmentMenuGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		GameMode->HandleMenuRight();
	}
}

void AAdjustmentMenuPlayerController::HandleMenuConfirm()
{
	if (AAdjustmentMenuGameMode* GameMode = Cast<AAdjustmentMenuGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		GameMode->HandleMenuConfirm();
	}
}
