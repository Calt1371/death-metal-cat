#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "AnyInputPlayerControllerBase.generated.h"

/**
 * Shared "catch literally any digital input" controller for screens that come before real gameplay
 * input matters -- currently the title screen and the intro cinematic. Both need to react to a
 * key/button the player hasn't bound to anything yet, so this deliberately bypasses the project's
 * Enhanced Input setup entirely (see SetupInputComponent) rather than binding an input action.
 *
 * EKeys::AnyKey alone would not do: it covers keyboard and mouse but is not reliable for gamepad
 * buttons, and gamepad support is required on both screens that use this. So SetupInputComponent
 * instead enumerates EKeys::GetAllKeys() and binds every digital key individually. Analog keys
 * (thumbstick and trigger axes) are skipped on purpose -- they report continuously, and a resting
 * controller's drift would fire the moment the screen appeared.
 *
 * Subclasses implement OnAnyInputDetected() with whatever that screen should actually do (advance
 * past the title, skip the cinematic); detecting and de-duplicating the press itself lives here so
 * it is implemented once rather than once per screen.
 */
UCLASS(Abstract)
class PYTHONTEST_API AAnyInputPlayerControllerBase : public APlayerController
{
	GENERATED_BODY()

public:
	AAnyInputPlayerControllerBase();

	/**
	 * Resets the "already consumed" latch and restarts the leaked-press grace window (see
	 * InputArmTime), so this SAME controller instance can catch a second, later "any input" press.
	 * Needed because ATitleScreenGameMode now hands off from the title screen to the intro cinematic
	 * in-place, in the same level (see that class's comment for why), rather than via a level
	 * transition that would hand the job to a fresh controller instance the way it used to.
	 */
	void Rearm();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/** Called exactly once, the first time any bound key is pressed -- see HandleAnyInput. */
	virtual void OnAnyInputDetected() PURE_VIRTUAL(AAnyInputPlayerControllerBase::OnAnyInputDetected, );

	/** Short tag prefixing this controller's log lines, e.g. "[TITLE]" or "[INTRO]". */
	virtual const TCHAR* GetLogTag() const { return TEXT("[ANY-INPUT]"); }

private:
	/** Bound to every digital key -- see the class comment. Guards itself so the many bindings can't fire OnAnyInputDetected more than once. */
	void HandleAnyInput();

	/** Best-effort lookup of which key is currently down, purely so the log line names the actual button that fired. BindKey handlers receive no key argument, hence the scan. */
	FString DescribePressedKeys() const;

	/** Latched on the first accepted press, so a mash of several buttons in the same frame still only fires once. */
	bool bInputConsumed = false;

	/**
	 * World time (seconds) before which HandleAnyInput ignores every press -- see BeginPlay. Guards
	 * against the key that dismissed the PREVIOUS screen (e.g. title) still being physically down, or
	 * its down-event still in flight, at the exact moment THIS screen's InputComponent binds during a
	 * level transition -- without this, that same press can immediately re-trigger here too, skipping
	 * the screen before the player ever sees it. Confirmed live (2026-09-03): pressing Enter to leave
	 * the title screen produced a second, unwanted "Input detected" on the intro cinematic's own
	 * controller ~1.3s later, skipping straight to gameplay.
	 */
	float InputArmTime = 0.f;
};
