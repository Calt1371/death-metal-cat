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
class UBoxComponent;
class UPrimitiveComponent;
struct FInputActionValue;
struct FHitResult;

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

	/** Bound to the SwordAttackAction's Started event; plays the swing and arms the hitbox timers. */
	void HandleSwordAttack(const FInputActionValue& Value);

	/** Timer callback: enables the sword hitbox's collision and arms the disable timer. */
	void EnableSwordHitbox();

	/** Timer callback: disables the sword hitbox's collision. */
	void DisableSwordHitbox();

	/** Timer callback: ends the attacking state, allowing another attack to be triggered. */
	void ClearAttackState();

	/** Bound to SwordHitbox's OnComponentBeginOverlap; the hook a future damage system plugs into. */
	UFUNCTION()
	void OnSwordHitboxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Picks Idle/Walk/Run/Jump/Dodge/SwordAttack based on current state, and flips the sprite to face travel direction. */
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

	/** Digital (bool) action, triggers a sword attack on press. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> SwordAttackAction;

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

	/** Shown for the duration of a sword attack. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> SwordAttackFlipbook;

	/** Shown for the duration of a dodge. Real dedicated art (was a placeholder reusing JumpFlipbook). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> DodgeFlipbook;

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

	// -- Sword Attack --

	/**
	 * Total lockout duration of the attack, in seconds -- doubles as the cooldown, since
	 * HandleSwordAttack ignores re-triggers for as long as bIsAttacking is true. Placeholder
	 * value, tune freely. Grounded against FB_DeathMetalCat_SwordAttack's actual length
	 * (4 frames @ 13fps =~ 0.31s) with a little recovery margin added on top.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float AttackDuration = 0.4f;

	/**
	 * Seconds after the attack starts before the hitbox turns on -- the wind-up frames
	 * (SwordAttack_01/02) shouldn't count as active hit frames. Not explicitly requested as a
	 * separate tunable, but needed to implement "middle portion only" -- see also
	 * HitboxActiveDuration. Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float HitboxActiveDelay = 0.15f;

	/**
	 * Seconds the hitbox stays enabled once active, roughly covering the strike frame
	 * (SwordAttack_03, the one with the slash-swirl VFX) without extending into full recovery.
	 * Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float HitboxActiveDuration = 0.12f;

	/** True for AttackDuration seconds after a sword attack starts; blocks re-triggering. */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	bool bIsAttacking = false;

private:
	/** Avoids calling SetFlipbook every tick when the animation state hasn't changed. */
	UPROPERTY(Transient)
	TObjectPtr<UPaperFlipbook> CurrentFlipbook = nullptr;

	/** Backs CanTakeDamage(); cleared independently of bIsDodging by IFrameTimerHandle. */
	bool bIsInvincible = false;

	FTimerHandle DodgeTimerHandle;
	FTimerHandle IFrameTimerHandle;

	FTimerHandle SwordHitboxEnableTimerHandle;
	FTimerHandle SwordHitboxDisableTimerHandle;
	FTimerHandle SwordAttackEndTimerHandle;

	/** TEMP diagnostic: world time HandleSwordAttack started, so EnableSwordHitbox can log actual elapsed delay. */
	float SwordAttackStartTime = 0.f;

	/**
	 * Overlap-only hitbox for the sword swing, attached to the (unscaled) root rather than the
	 * sprite: attaching to the sprite would have it automatically mirror for free via the
	 * sprite's negative-X facing flip, but negative-scaled collision shapes are a known source of
	 * subtle physics-engine quirks, so this is positioned manually in HandleSwordAttack instead.
	 * No collision by default; only enabled during the active window of the swing.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> SwordHitbox;

	// -- Camera --

	/** Holds the camera out along the depth (Y) axis so it views the X-Z movement plane side-on. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** Fixed side-view camera; does not rotate with player/controller input. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> SideViewCamera;
};
