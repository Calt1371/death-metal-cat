#include "TitleScreenPlayerController.h"

#include "TitleScreenGameMode.h"
#include "Components/InputComponent.h"
#include "GameFramework/InputSettings.h"
#include "Kismet/GameplayStatics.h"

ATitleScreenPlayerController::ATitleScreenPlayerController()
{
	bShowMouseCursor = false;
	bAutoManageActiveCameraTarget = false;
}

void ATitleScreenPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// The title widget is not focusable, so game-only input keeps every key routed here rather than
	// letting Slate swallow it.
	SetInputMode(FInputModeGameOnly());
}

void ATitleScreenPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[TITLE] No InputComponent on the title screen controller -- any-key start will not work."));
		return;
	}

	// Enumerate and bind every digital key rather than binding an action -- see the class comment
	// for why this screen ignores the project's Enhanced Input setup entirely.
	TArray<FKey> AllKeys;
	EKeys::GetAllKeys(AllKeys);

	int32 BoundCount = 0;
	int32 GamepadCount = 0;
	for (const FKey& Key : AllKeys)
	{
		// Thumbstick/trigger axes report continuously; a resting controller's drift would start the
		// game instantly. Their digital counterparts (Gamepad_LeftTrigger, Gamepad_LeftStick_Up and
		// friends) are still bound below, so triggers and stick pushes do work.
		if (Key.IsAnalog())
		{
			continue;
		}

		if (!Key.IsBindableInBlueprints())
		{
			continue;
		}

		InputComponent->BindKey(Key, IE_Pressed, this, &ATitleScreenPlayerController::HandleAnyInput);
		++BoundCount;
		if (Key.IsGamepadKey())
		{
			++GamepadCount;
		}
	}

	// The gamepad figure is called out separately because gamepad coverage is the specific thing
	// EKeys::AnyKey would have silently failed at -- a zero here means controllers won't start the
	// game and this screen is broken, even though keyboard would still appear to work fine.
	UE_LOG(LogTemp, Log, TEXT("[TITLE] Bound %d digital keys for any-button start (%d of them gamepad)."),
		BoundCount, GamepadCount);
}

FString ATitleScreenPlayerController::DescribePressedKeys() const
{
	TArray<FKey> AllKeys;
	EKeys::GetAllKeys(AllKeys);

	TArray<FString> Pressed;
	for (const FKey& Key : AllKeys)
	{
		if (!Key.IsAnalog() && Key.IsBindableInBlueprints() && IsInputKeyDown(Key))
		{
			Pressed.Add(Key.ToString());
		}
	}

	return Pressed.Num() > 0 ? FString::Join(Pressed, TEXT(", ")) : TEXT("<already released>");
}

void ATitleScreenPlayerController::HandleAnyInput()
{
	// Every bound key routes here, and several can fire in the same frame (a key plus AnyKey, or a
	// genuine multi-button mash), so this must be idempotent.
	if (bInputConsumed)
	{
		return;
	}
	bInputConsumed = true;

	UE_LOG(LogTemp, Log, TEXT("[TITLE] Input detected (%s) -- leaving the title screen."), *DescribePressedKeys());

	if (ATitleScreenGameMode* TitleGameMode = Cast<ATitleScreenGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		TitleGameMode->HandleStartPressed();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[TITLE] Start pressed but the game mode is not an ATitleScreenGameMode -- check the map's World Settings."));
	}
}
