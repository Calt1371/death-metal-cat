#pragma once

#include "CoreMinimal.h"
#include "PaperCharacter.h"
#include "TimerManager.h"
#include "DeathMetalCatCharacter.generated.h"

class UInputAction;
class UInputMappingContext;
class UPaperFlipbook;
class USpringArmComponent;
class UCameraComponent;
struct FInputActionValue;

/**
 * Base gameplay character for Death Metal Cat.
 *
 * Derives from APaperCharacter (Paper2D's 2D-specific character class) rather than plain
 * ACharacter: APaperCharacter already owns a UPaperFlipbookComponent as its visual root
 * (accessible via GetSprite()) in place of ACharacter's skeletal mesh, which is what a
 * sprite-based side-scroller needs. Movement is locked to the X-Z plane (X = left/right,
 * Z = up/down) since a 2D side-scroller has no use for depth (Y) movement.
 */
UCLASS()
class PYTHONTEST_API ADeathMetalCatCharacter : public APaperCharacter
{
	GENERATED_BODY()

public:
	ADeathMetalCatCharacter();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;

	/** Bound to the MoveRightAction; Value is a 1D axis in [-1, 1] (left .. right). */
	void HandleMoveRight(const FInputActionValue& Value);

	/** Bound to the JumpAction's Started event; just forwards to ACharacter::Jump(). */
	void HandleJump(const FInputActionValue& Value);

	/** Bound to the DodgeAction's Started event; launches the character and starts the dodge/i-frame timers. */
	void HandleDodge(const FInputActionValue& Value);

	/** Timer callback: ends the dodge movement/animation state. */
	void ClearDodgeState();

	/** Timer callback: ends the invincibility window (may outlast or be shorter than the dodge state itself). */
	void ClearInvincibility();

	/** Picks Idle/Walk/Run/Jump/Dodge based on current state, and flips the sprite to face travel direction. */
	void UpdateAnimation();

public:
	// -- Input --

	/** Mapping context applied to this character's EnhancedInput subsystem once it's possessed by a player controller. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputMappingContext> MoveMappingContext;

	/** 1D axis action: -1 (left) .. +1 (right). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> MoveRightAction;

	/** Digital (bool) action, triggers a jump on press. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	/** Digital (bool) action, triggers a dodge on press. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> DodgeAction;

	// -- Movement --

	/** Top horizontal move speed in uu/s; drives CharacterMovementComponent::MaxWalkSpeed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0"))
	float MaxMoveSpeed = 600.f;

	/** Horizontal speed (uu/s) at/above which the Run flipbook is used instead of Walk. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0"))
	float WalkSpeedThreshold = 300.f;

	// -- Animation --

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> IdleFlipbook;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> WalkFlipbook;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> RunFlipbook;

	/** Shown whenever the character is airborne (falling or jumping), regardless of horizontal speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> JumpFlipbook;

	// -- Dodge --

	/** How long the dodge movement/animation state lasts, in seconds. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dodge", meta = (ClampMin = "0"))
	float DodgeDuration = 0.35f;

	/**
	 * How long invincibility (CanTakeDamage() == false) lasts, in seconds. Tracked independently
	 * of DodgeDuration -- i-frames don't have to match the movement/animation window exactly (e.g.
	 * you may want i-frames to end slightly before or after the visual dodge finishes).
	 * Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dodge", meta = (ClampMin = "0"))
	float IFrameDuration = 0.35f;

	/** Instantaneous velocity (uu/s) applied along the facing direction when a dodge starts. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dodge", meta = (ClampMin = "0"))
	float DodgeImpulseStrength = 1200.f;

	/** True for DodgeDuration seconds after a dodge starts. */
	UPROPERTY(BlueprintReadOnly, Category = "Dodge")
	bool bIsDodging = false;

	/**
	 * Stub for the future damage system: returns false while invincibility frames are active.
	 * No damage system exists yet -- this just needs to be correctly wired (true/false at the
	 * right times) so damage-dealing code has something to check once it exists.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dodge")
	bool CanTakeDamage() const;

private:
	/** Avoids calling SetFlipbook every tick when the animation state hasn't changed. */
	UPROPERTY(Transient)
	TObjectPtr<UPaperFlipbook> CurrentFlipbook = nullptr;

	/** Backs CanTakeDamage(); cleared independently of bIsDodging by IFrameTimerHandle. */
	bool bIsInvincible = false;

	FTimerHandle DodgeTimerHandle;
	FTimerHandle IFrameTimerHandle;

	// -- Camera --

	/** Holds the camera out along the depth (Y) axis so it views the X-Z movement plane side-on. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** Fixed side-view camera; does not rotate with player/controller input. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> SideViewCamera;
};
