#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TitleScreenPlayerController.generated.h"

/**
 * Player controller for the title screen, whose entire job is to notice that the player pressed
 * *something* and tell ATitleScreenGameMode to get on with it.
 *
 * Deliberately NOT built on Enhanced Input, unlike the rest of the project (see
 * ADeathMetalCatCharacter::SetupPlayerInputComponent). At the title screen the player hasn't
 * started the game and their action bindings are irrelevant -- the requirement is to catch
 * literally any key or button, including ones bound to nothing at all. So instead of binding an
 * input action, SetupInputComponent enumerates EKeys::GetAllKeys() and binds every digital key
 * individually.
 *
 * EKeys::AnyKey alone would not do: it covers keyboard and mouse but is not reliable for gamepad
 * buttons, and gamepad support is explicitly required here. Enumerating every key covers keyboard,
 * mouse, gamepad face/shoulder/d-pad/stick-click buttons and stick-direction keys in one pass.
 * Analog keys (thumbstick and trigger axes) are skipped on purpose -- they report continuously and
 * a resting controller's drift would fire the moment the screen appeared.
 */
UCLASS()
class PYTHONTEST_API ATitleScreenPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATitleScreenPlayerController();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	/** Bound to every digital key -- see the class comment. Guards itself so the many bindings can't fire the transition more than once. */
	void HandleAnyInput();

	/** Best-effort lookup of which key is currently down, purely so the log line names the actual button that started the game. BindKey handlers receive no key argument, hence the scan. */
	FString DescribePressedKeys() const;

	/** Latched on the first accepted press, so a mash of several buttons in the same frame still only starts the game once. */
	bool bInputConsumed = false;
};
