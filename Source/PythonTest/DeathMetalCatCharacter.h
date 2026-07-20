#pragma once

#include "CoreMinimal.h"
#include "PaperCharacter.h"
#include "TimerManager.h"
#include "DamageTypes.h"
#include "DeathMetalCatCharacter.generated.h"

class UInputAction;
class UInputMappingContext;
class UPaperFlipbook;
class USpringArmComponent;
class UCameraComponent;
class UBoxComponent;
class UPrimitiveComponent;
class ADamageNumberActor;
class UGnarlyRankHUDWidget;
struct FInputActionValue;
struct FHitResult;

/** Internal-only (not exposed to Blueprint) phase tracking for the held-fire animation sequence. */
enum class EShootPhase : uint8
{
	None,
	Drawing,
	HoldFiring,
};

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

	/** Bound to the DodgeAction's Started event; launches the character and starts the dodge/i-frame/frame-advance timers. */
	void HandleDodge(const FInputActionValue& Value);

	/**
	 * Timer callback: advances DodgeCurrentFrame to the next of the flipbook's 5 frames, fires the
	 * deferred backward impulse on the transition into frame 1 specifically, then re-arms itself
	 * (via ScheduleNextDodgeFrame) with that new frame's own hold duration -- a chain of one-shot
	 * timers rather than a single repeating one, since frame 1's hold time now differs from the
	 * rest (see DodgeWindUpFrameDuration).
	 */
	void AdvanceDodgeFrame();

	/**
	 * Arms DodgeFrameTimerHandle to call AdvanceDodgeFrame() after DodgeCurrentFrame's own hold
	 * duration (see GetDodgeFrameHoldDuration) -- unless DodgeCurrentFrame is already the last
	 * frame (4), in which case there's nothing left to advance to.
	 */
	void ScheduleNextDodgeFrame();

	/**
	 * Hold duration (seconds) for the given Dodge flipbook frame index: DodgeWindUpFrameDuration
	 * for frame 1 (crouch/wind-up), DodgeDuration / 5 for every other frame.
	 */
	float GetDodgeFrameHoldDuration(int32 FrameIndex) const;

	/** Sets the sprite to DodgeFlipbook (once) and pins it to the given frame index, code-driven like Jump's/Shoot's. */
	void SetDodgeFrame(int32 FrameIndex, const TCHAR* Reason);

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

	/** Bound to SwordHitbox's OnComponentBeginOverlap: rolls a damage tier, applies damage via UGameplayStatics::ApplyDamage, and spawns a floating damage number on whatever it hits. */
	UFUNCTION()
	void OnSwordHitboxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Bound to the ShootAction's Started event: enters the held-fire state and begins the quick-draw pose. */
	void HandleShootStarted(const FInputActionValue& Value);

	/**
	 * Bound to the ShootAction's Triggered event (fires every tick while held, including the same
	 * tick as Started -- FireShotTrace()'s own bIsShooting cooldown gate naturally no-ops that
	 * redundant first call, and the ShootAnimPhase check below skips it too). Attempts a repeat
	 * shot once the hold-fire loop has taken over from the initial draw.
	 */
	void HandleShootHeld(const FInputActionValue& Value);

	/** Bound to the ShootAction's Completed/Canceled events: marks the button released. */
	void HandleShootReleased(const FInputActionValue& Value);

	/** Shows the draw pose (from the regular Shoot row), then arms a timer that calls BeginHoldFireLoop() once it's done. */
	void BeginDraw();

	/**
	 * Timer callback from BeginDraw(): switches the sprite to the dedicated FB_DeathMetalCat_HoldFire
	 * flipbook and plays it looping (engine-driven, not manual frame-index jumping -- that flipbook's
	 * own frames already cycle through the muzzle-flash variations), then fires the first shot.
	 */
	void BeginHoldFireLoop();

	/** The actual hitscan trace (unchanged logic from the original single-shot version), gated by
	 * the FireCooldown cooldown. Purely gameplay -- the hold-fire loop's visuals are event-driven
	 * separately via BeginHoldFireLoop()'s Play(), not tied per-shot to this call. */
	void FireShotTrace();

	/** Sets the sprite to ShootFlipbook (once) and pins it to the given frame index, code-driven like Jump's. */
	void SetShootFrame(int32 FrameIndex, const TCHAR* Reason);

	/**
	 * Unconditionally clears all shoot-related state: ShootAnimPhase back to None, bIsShooting
	 * back to false, and cancels the draw/flash/cooldown timers. Called from HandleShootReleased
	 * on every release regardless of phase, and defensively from HandleShootStarted if it finds
	 * stale non-None state left over -- rapid press/release timing is exactly where partial
	 * cleanup (e.g. only resetting on the Aiming case) left ShootAnimPhase stuck indefinitely.
	 */
	void ResetShootState();

	/** Timer callback: ends the fire-rate cooldown / shoot animation window, allowing another shot. */
	void ClearShootingState();

	/** Picks Idle/Walk/Run/Jump/Dodge/SwordAttack/Shoot based on current state, and flips the sprite to face travel direction. */
	void UpdateAnimation();

	/**
	 * Standard AActor::TakeDamage override. Ignores the hit entirely (no health change, no Hurt
	 * animation) while CanTakeDamage() is false (i.e. mid-dodge i-frames). On a hit that lands:
	 * deducts Health, plays HurtFlipbook briefly, and on reaching 0 logs "PLAYER DIED" and disables
	 * further input via DisableInput(). No death/respawn flow yet -- a separate future task.
	 */
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Timer callback: ends the brief Hurt animation beat, letting UpdateAnimation's normal Idle/Walk/Run/etc. logic resume. */
	void ClearHurtState();

	/**
	 * Rolls a damage tier (WeaknessChance / CriticalChance, remainder is Normal) and returns
	 * BaseDamage scaled by that tier's multiplier; OutTier receives which tier was rolled.
	 * Centralizes the roll here so OnSwordHitboxBeginOverlap and FireShotTrace both apply
	 * identical tier logic via one function instead of each rolling independently.
	 */
	float RollDamage(float BaseDamage, EDamageTier& OutTier) const;

	/** Spawns an ADamageNumberActor at Location showing DamageAmount color-coded by Tier. */
	void SpawnDamageNumber(const FVector& Location, float DamageAmount, EDamageTier Tier);

	/**
	 * Called whenever a sword or gun hit successfully lands (DamageApplied > 0): increments
	 * GnarlyHitCount and advances GnarlyRank across any thresholds just crossed. Named
	 * "GnarlyRank"/"GnarlyHitCount" throughout (never just "tier" or "rank" alone) to stay clearly
	 * distinct from the unrelated EDamageTier (Normal/Weakness/Critical) damage-roll system.
	 */
	void RegisterGnarlyHit();

	/**
	 * Called from TakeDamage on any real damage taken (past the i-frame check): fully resets
	 * GnarlyHitCount and GnarlyRank to 0. Deliberate high-risk/high-reward design per the GDD --
	 * not a bug to soften later (e.g. into a partial-decay system) without an explicit design change.
	 */
	void ResetGnarlyRank();

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

	/** Digital (bool) action, fires the gun on press. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> ShootAction;

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

	/**
	 * 5-frame back-handspring sequence played once over DodgeDuration (neutral -> wind-up ->
	 * mid-flip tumble -> landing crouch -> neutral), evenly split and code-driven via
	 * AdvanceDodgeFrame() -- see HandleDodge.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> DodgeFlipbook;

	/** Shown briefly for the quick-draw pose at the start of a shoot sequence (frame index 2 -- see ShootFrame_Draw). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> ShootFlipbook;

	/**
	 * Dedicated held-fire loop: a steady gun-extended stance with muzzle-flash variations across
	 * its 4 frames, played looping for the duration of a hold once the initial draw finishes.
	 * Separate art from ShootFlipbook -- that row's frames don't include a pose that stays fully
	 * extended while flashing, which is what made reusing it for held-fire look like a repeated
	 * draw motion; see git history for the investigation.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> HoldFireFlipbook;

	/** Shown briefly (HurtDuration seconds) when a hit lands (TakeDamage), then hands back to normal Idle/Walk/Run/etc. logic. Already imported as part of the v2 sprite sheet pass -- just needs assigning here. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> HurtFlipbook;

	// -- Dodge --

	/**
	 * How long the dodge movement/animation state lasts, in seconds -- also determines the default
	 * per-frame hold time for the 5-frame handspring sequence (DodgeDuration / 5 each -- see
	 * GetDodgeFrameHoldDuration), EXCEPT frame 1 (crouch/wind-up), which uses
	 * DodgeWindUpFrameDuration instead of this flat split. 0.35s (~0.07s/frame) was too fast to
	 * read; 1.25s (~0.25s/frame) was confirmed readable but felt sluggish; 0.75s (~0.15s/frame)
	 * settled as the base rate for frames 0/2/3/4. Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dodge", meta = (ClampMin = "0"))
	float DodgeDuration = 0.75f;

	/**
	 * Hold duration (seconds) specifically for frame 1 (crouch/wind-up, the pose right before the
	 * flip) -- overrides the flat DodgeDuration/5 split used by every other frame, so this one
	 * pose reads as a quick beat rather than an equal-length hold like the rest. See
	 * GetDodgeFrameHoldDuration. Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dodge", meta = (ClampMin = "0"))
	float DodgeWindUpFrameDuration = 0.06f;

	/**
	 * How long invincibility (CanTakeDamage() == false) lasts, in seconds. Tracked independently
	 * of DodgeDuration -- i-frames don't have to match the movement/animation window exactly (e.g.
	 * you may want i-frames to end slightly before or after the visual dodge finishes).
	 * Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dodge", meta = (ClampMin = "0"))
	float IFrameDuration = 0.35f;

	/** Instantaneous velocity (uu/s) applied AWAY from the facing direction (a retreat, not a dash toward the threat) when a dodge starts. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dodge", meta = (ClampMin = "0"))
	float DodgeImpulseStrength = 1200.f;

	/** True for DodgeDuration seconds after a dodge starts. */
	UPROPERTY(BlueprintReadOnly, Category = "Dodge")
	bool bIsDodging = false;

	/** Returns false while invincibility frames are active; TakeDamage() checks this and ignores any hit entirely while it's false. */
	UFUNCTION(BlueprintCallable, Category = "Dodge")
	bool CanTakeDamage() const;

	// -- Health / Damage --

	/** Max health. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0"))
	float MaxHealth = 100.f;

	/** Current health; set to MaxHealth in BeginPlay. Reaching 0 triggers the "PLAYER DIED" path in TakeDamage. */
	UPROPERTY(BlueprintReadOnly, Category = "Health")
	float Health = 100.f;

	/** How long HurtFlipbook shows after a hit lands before returning to normal animation. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0"))
	float HurtDuration = 0.4f;

	/**
	 * Chance [0-1] a damage roll lands on the Weakness tier (see RollDamage). Normal is the
	 * implicit remainder after Weakness and Critical -- same "last tier gets whatever's left"
	 * convention used elsewhere in this class (e.g. Dodge's Standing phase). Placeholder value,
	 * tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0", ClampMax = "1"))
	float WeaknessChance = 0.20f;

	/** Chance [0-1] a damage roll lands on the Critical tier (see RollDamage). Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0", ClampMax = "1"))
	float CriticalChance = 0.10f;

	/** Damage multiplier applied on a Weakness roll. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "1"))
	float WeaknessMultiplier = 1.25f;

	/** Damage multiplier applied on a Critical roll. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "1"))
	float CriticalMultiplier = 1.75f;

	/** Vertical offset (world units) above the hit point a floating damage number spawns at, so it doesn't render exactly at the target's feet/center. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0"))
	float DamageNumberSpawnHeight = 60.f;

	// -- Gnarly Rank --
	//
	// Deliberately named "GnarlyRank"/"GnarlyHitCount"/"GnarlyRankThresholds" throughout, never
	// just "tier" or "rank" alone -- this is a wholly separate system from EDamageTier
	// (Normal/Weakness/Critical, used for floating damage-number colors) and the naming keeps the
	// two unambiguous in code, logs, and UI text.

	/** Accumulated hits (sword or gun) toward the next GnarlyRank. Fully resets to 0 on any real damage taken -- see ResetGnarlyRank. */
	UPROPERTY(BlueprintReadOnly, Category = "GnarlyRank")
	int32 GnarlyHitCount = 0;

	/**
	 * Current Gnarly rank: 0 (no rank yet) through GnarlyRankThresholds.Num() (max rank, 4 with
	 * the default thresholds). Grants a melee-ONLY damage multiplier -- see
	 * GnarlyRankMeleeDamageBonusPerRank -- and fully resets to 0 on any real damage taken.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "GnarlyRank")
	int32 GnarlyRank = 0;

	/**
	 * Accumulated-hit thresholds to reach GnarlyRank 1, 2, 3, 4 respectively (index 0 = the
	 * GnarlyHitCount needed to reach rank 1, etc.) -- see RegisterGnarlyHit. Doubled from the
	 * original placeholder (5/10/20/35) to 10/20/40/70. Placeholder values, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GnarlyRank")
	TArray<int32> GnarlyRankThresholds = { 10, 20, 40, 70 };

	/**
	 * Melee (sword only -- gun is deliberately excluded per the GDD) damage multiplier bonus per
	 * GnarlyRank: final multiplier is (1 + GnarlyRank * this), applied on top of RollDamage's
	 * Normal/Weakness/Critical tier multiplier, not replacing it. Placeholder value (5%/rank per
	 * the GDD), tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GnarlyRank", meta = (ClampMin = "0"))
	float GnarlyRankMeleeDamageBonusPerRank = 0.05f;

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

	/** Base damage a sword hit deals, before RollDamage's tier multiplier. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float SwordBaseDamage = 20.f;

	// -- Gun Fire --

	/**
	 * Minimum seconds between actual shots (the hitscan trace itself), independent of how the
	 * held-fire animation is paced. Unchanged in role from the original single-shot version:
	 * FireShotTrace() ignores a new shot attempt for as long as bIsShooting is true, and
	 * ClearShootingState() (fired by a timer of this duration) is what allows the next one.
	 * Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GunFire", meta = (ClampMin = "0"))
	float FireCooldown = 0.3f;

	/** Max hitscan line-trace distance in uu. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GunFire", meta = (ClampMin = "0"))
	float MaxTraceRange = 2000.f;

	/**
	 * How long the quick draw pose (frame index 2) shows before the hold-fire loop takes over. Not
	 * part of the original single-shot spec -- needed to implement "draws fast" as an actual
	 * visible beat rather than an instant snap. Kept short on purpose. Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GunFire", meta = (ClampMin = "0"))
	float DrawDuration = 0.1f;

	/** True for FireCooldown seconds after a shot is fired; blocks re-triggering. */
	UPROPERTY(BlueprintReadOnly, Category = "GunFire")
	bool bIsShooting = false;

	/** Base damage a gunshot deals, before RollDamage's tier multiplier. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GunFire", meta = (ClampMin = "0"))
	float GunBaseDamage = 10.f;

private:
	/** Avoids calling SetFlipbook every tick when the animation state hasn't changed. */
	UPROPERTY(Transient)
	TObjectPtr<UPaperFlipbook> CurrentFlipbook = nullptr;

	/** Backs CanTakeDamage(); cleared independently of bIsDodging by IFrameTimerHandle. */
	bool bIsInvincible = false;

	/** True for HurtDuration seconds after a hit lands; UpdateAnimation checks this first (highest visual priority) so a hit reaction always shows even mid-swing/mid-shot. */
	bool bIsHurt = false;

	FTimerHandle HurtTimerHandle;

	/** True once Health reaches 0; TakeDamage uses this to only log "PLAYER DIED" / call DisableInput once. */
	bool bIsDead = false;

	FTimerHandle DodgeTimerHandle;
	FTimerHandle IFrameTimerHandle;
	FTimerHandle DodgeFrameTimerHandle;

	/** Which of the 5 Dodge flipbook frames is currently showing; advanced by AdvanceDodgeFrame(). */
	int32 DodgeCurrentFrame = 0;

	/**
	 * Facing sign (matches Scale.X's sign convention) captured the instant a dodge starts, so
	 * UpdateAnimation can hold the sprite facing this direction for the whole dodge instead of
	 * letting the normal velocity-based flip logic re-face it toward the (backward) dodge
	 * movement direction.
	 */
	float DodgeFacingSignAtStart = 1.f;

	FTimerHandle SwordHitboxEnableTimerHandle;
	FTimerHandle SwordHitboxDisableTimerHandle;
	FTimerHandle SwordAttackEndTimerHandle;

	FTimerHandle ShootTimerHandle;
	FTimerHandle ShootDrawTimerHandle;

	/** True while the Shoot input action is physically held down (Started..Completed/Canceled). */
	bool bIsHoldingShootButton = false;

	/** Drives Draw vs. HoldFiring animation selection; None means no shoot animation is in progress. */
	EShootPhase ShootAnimPhase = EShootPhase::None;

	/**
	 * Created once, the first time this character is possessed by a player controller (see
	 * NotifyControllerChanged) -- a persistent HUD element added to the viewport, not a
	 * spawned/destroyed actor like ADamageNumberActor's floating numbers. Polls
	 * GnarlyRank/GnarlyHitCount itself every tick rather than this class pushing updates to it.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UGnarlyRankHUDWidget> GnarlyRankHUDWidgetInstance;

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
