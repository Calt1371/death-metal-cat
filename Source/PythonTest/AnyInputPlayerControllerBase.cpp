#include "AnyInputPlayerControllerBase.h"

#include "Components/InputComponent.h"
#include "GameFramework/InputSettings.h"

AAnyInputPlayerControllerBase::AAnyInputPlayerControllerBase()
{
	bShowMouseCursor = false;
	bAutoManageActiveCameraTarget = false;
}

void AAnyInputPlayerControllerBase::BeginPlay()
{
	Super::BeginPlay();

	// These screens have no focusable widget, so game-only input keeps every key routed here rather
	// than letting Slate swallow it.
	SetInputMode(FInputModeGameOnly());
}

void AAnyInputPlayerControllerBase::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("%s No InputComponent -- any-key input will not work."), GetLogTag());
		return;
	}

	// Enumerate and bind every digital key rather than binding an action -- see the class comment
	// for why this bypasses the project's Enhanced Input setup entirely.
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

		InputComponent->BindKey(Key, IE_Pressed, this, &AAnyInputPlayerControllerBase::HandleAnyInput);
		++BoundCount;
		if (Key.IsGamepadKey())
		{
			++GamepadCount;
		}
	}

	// The gamepad figure is called out separately because gamepad coverage is the specific thing
	// EKeys::AnyKey would have silently failed at -- a zero here means controllers won't fire this
	// screen's input, even though keyboard would still appear to work fine.
	UE_LOG(LogTemp, Log, TEXT("%s Bound %d digital keys for any-button input (%d of them gamepad)."),
		GetLogTag(), BoundCount, GamepadCount);
}

FString AAnyInputPlayerControllerBase::DescribePressedKeys() const
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

void AAnyInputPlayerControllerBase::HandleAnyInput()
{
	// Every bound key routes here, and several can fire in the same frame (a key plus AnyKey, or a
	// genuine multi-button mash), so this must be idempotent.
	if (bInputConsumed)
	{
		return;
	}
	bInputConsumed = true;

	UE_LOG(LogTemp, Log, TEXT("%s Input detected (%s)."), GetLogTag(), *DescribePressedKeys());

	OnAnyInputDetected();
}
