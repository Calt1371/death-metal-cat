#pragma once

#include "CoreMinimal.h"
#include "PaperCharacter.h"
#include "TimerManager.h"
#include "DamageTypes.h"
#include "QuipTypes.h"
#include "ItemTypes.h"
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
class UDataTable;
class UInputComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
struct FInputActionValue;
struct FHitResult;

/** Internal-only (not exposed to Blueprint) phase tracking for the held-fire animation sequence. */
enum class EShootPhase : uint8
{
	None,
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

	/** Bound to the MoveRightAction's Completed/Canceled events: zeroes LastMoveRightAxisValue on release. Without this, releasing movement input leaves LastMoveRightAxisValue stuck at its last held (nonzero) value forever, since Triggered simply stops firing rather than reporting 0 -- DetectWallForSlide would then keep testing a stale, no-longer-held direction indefinitely. */
	void HandleMoveRightReleased(const FInputActionValue& Value);

	/** Bound to the JumpAction's Started event; wall-jumps (launch up and away from the wall, brief reduced-authority commitment window before full air control -- see WallJumpCommitmentDuration) if currently wall-sliding, otherwise just forwards to ACharacter::Jump() as before. */
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

	/**
	 * Called every Tick, before UpdateAnimation. Detects a slidable wall via DetectWallForSlide
	 * and, only while airborne AND holding movement input toward that wall, clamps
	 * MoveComp->Velocity.Z toward -WallSlideSpeed (rather than letting normal gravity fall speed
	 * build up further) and sets bIsWallSliding/WallSlideFacingSign. Clears bIsWallSliding
	 * whenever any of those conditions stop holding (grounded, no wall, input released/reversed).
	 */
	void UpdateWallSlide();

	/**
	 * Short forward sweep from the capsule, in the direction of LastMoveRightAxisValue's sign (so
	 * it works facing either way) -- returns true and writes that sign to OutWallSign if it hits
	 * something within WallCheckDistance. Returns false (OutWallSign left untouched) if no
	 * movement input is currently held, or nothing was hit.
	 */
	bool DetectWallForSlide(float& OutWallSign) const;

	/** Timer callback: ends the brief post-wall-jump reduced-authority commitment window, restoring full air control (see HandleJump/HandleMoveRight). */
	void ClearWallJumpCommitmentWindow();

	/**
	 * Bound to the SwordAttackAction's Started event. Branches to one of four attack variants
	 * based on current state, all sharing the same SwordHitbox/damage pipeline:
	 *  - Airborne + holding Down -> The Spinny Down Thing (ground-pound style downward attack).
	 *  - Airborne, not holding Down -> Double Whammy (airborne sword swing).
	 *  - Grounded + holding Back (opposite of current facing) -> Uppy (launches the hit enemy up).
	 *  - Grounded, no modifier -> the 3-hit ground combo (see StartSwordComboStage).
	 * While already mid-swing (bIsAttacking), a repress only ever buffers into the ground combo
	 * (see bComboInputBuffered) -- it never re-triggers Uppy/Double Whammy/Spinny Down, since those
	 * are one-off moves, not chain participants.
	 */
	void HandleSwordAttack(const FInputActionValue& Value);

	/** Starts ground combo stage StageIndex (0=sword-v2, 1=sword_2, 2=sword_3) -- see SwordComboIndex. */
	void StartSwordComboStage(int32 StageIndex);

	/** Starts Uppy: grounded forward swing that also launches the hit enemy upward on a landed hit -- see UppyLaunchVelocityZ. */
	void StartUppy();

	/** Starts Double Whammy: airborne forward swing, otherwise identical to a ground combo swing. */
	void StartDoubleWhammy();

	/** Starts The Spinny Down Thing: airborne downward swing with a small hitbox below Cayde and a fast forced descent -- see SpinnyDownFallSpeed. */
	void StartSpinnyDown();

	/** Timer callback: enables the sword hitbox's collision and arms the disable timer. */
	void EnableSwordHitbox();

	/** Timer callback: disables the sword hitbox's collision. */
	void DisableSwordHitbox();

	/**
	 * Timer callback: ends the attacking state. For a ground-combo stage (bLastAttackWasComboEligible),
	 * either immediately advances into the next buffered stage (bComboInputBuffered, covers "pressed
	 * again during recovery frames") or opens the SwordComboBufferWindow for a following press (covers
	 * "pressed again within the window after it finishes") -- see HandleSwordAttack/SwordComboIndex.
	 * Uppy/Double Whammy/Spinny Down never open the combo window; they're one-off moves.
	 */
	void ClearAttackState();

	/** Timer callback: closes the ground-combo buffered-input window and resets SwordComboIndex back to the base swing. */
	void ResetSwordComboWindow();

	/** Bound to SwordHitbox's OnComponentBeginOverlap: rolls a damage tier, applies damage via UGameplayStatics::ApplyDamage, and spawns a floating damage number on whatever it hits. */
	UFUNCTION()
	void OnSwordHitboxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/**
	 * Bound to the ShootAction's Started event: enters the held-fire state and begins the hold-fire
	 * loop. While bIsTransformed, this is also the ultimate's rainbow-laser attack -- Gun Fire is
	 * repurposed for the duration of the ride, but goes through this exact same continuous
	 * hold-to-fire path as the regular gun (see UpdateHoldFireFlipbook's own bIsTransformed branch),
	 * not a separate single-press case.
	 */
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

	/**
	 * Called directly from HandleShootStarted (no separate draw/windup phase -- see
	 * FB_DeathMetalCat_Shoot's own removal note on HoldFireFlipbook): switches the sprite straight
	 * to the dedicated FB_DeathMetalCat_HoldFire flipbook and plays it looping (engine-driven, not
	 * manual frame-index jumping -- that flipbook's own frames already cycle through the
	 * muzzle-flash variations), then fires the first shot.
	 */
	void BeginHoldFireLoop();

	/**
	 * The actual hitscan trace, gated by the FireCooldown cooldown. Aims a straight horizontal
	 * shot while grounded, or a fixed 45-degrees-downward shot (still respecting facing for the
	 * horizontal component) whenever the character is airborne at the moment this fires --
	 * checked fresh per shot, so jumping or landing mid-hold correctly changes the very next shot.
	 * Damage/hit logic is otherwise identical between the two. Also calls UpdateHoldFireFlipbook
	 * so the sustained loop animation (HoldFire vs. AirDownShot) stays in sync with that same
	 * per-shot airborne check.
	 */
	void FireShotTrace();

	/**
	 * Sets the sustained hold-fire loop's flipbook to HoldFireFlipbook (grounded), AirDownShotFlipbook
	 * (airborne, not holding Down), or AirShotAngledFlipbook (airborne AND holding Down) -- only
	 * reassigning/restarting playback when it actually needs to change. Called once from
	 * BeginHoldFireLoop and again on every repeat shot from FireShotTrace, so transitioning airborne
	 * state OR the Down modifier mid-hold updates the loop on the next shot rather than being stuck
	 * with whichever animation started it.
	 */
	void UpdateHoldFireFlipbook(bool bAirborne, bool bAngled);

	/**
	 * Dante/DMC-style "float on shoot": called from FireShotTrace whenever a shot fires while
	 * airborne (the same condition that triggers the 45-degree air-down-shot -- this is a brief
	 * gravity reduction layered on top of that, not a competing system; it never touches
	 * FireDirection or the flipbook, only MoveComp->GravityScale). Reduces gravity to
	 * DefaultGravityScale * AirFireGravityScaleMultiplier and (re-)arms a AirFireFloatDuration
	 * timer that restores normal gravity -- a floaty hang, not a full stop, and capped so it can
	 * never turn into infinite hover. Continuous fire re-arms (extends) the same timer rather than
	 * re-applying the multiplier on top of itself, so rapid shots don't compound toward zero gravity.
	 */
	void ApplyAirFireFloat();

	/** Timer callback: restores MoveComp->GravityScale to DefaultGravityScale, ending the air-fire float window. */
	void ClearAirFireFloat();

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

	/** Bound to the AimDownAction's Started event: marks Down as held (drives the airborne angled-shot/Spinny-Down modifiers). */
	void HandleAimDownStarted(const FInputActionValue& Value);

	/** Bound to the AimDownAction's Completed/Canceled events: marks Down as released. */
	void HandleAimDownReleased(const FInputActionValue& Value);

	/**
	 * Bound to the BlockAction's Started event. Ignored while dodging or dashing (mutually exclusive
	 * movement states). While held, CanTakeDamage() reports false (same protection Dodge's i-frames
	 * give -- full block, no partial, and TakeDamage's early-out means GnarlyRank isn't reset either)
	 * and HandleMoveRight is locked out (a standing block, not a moving one).
	 */
	void HandleBlockStarted(const FInputActionValue& Value);

	/** Bound to the BlockAction's Completed/Canceled events: ends the block immediately, no recovery lockout. */
	void HandleBlockReleased(const FInputActionValue& Value);

	/**
	 * Bound to the InvulnDashAction's Started event. Ignored while dodging or blocking. Launches Cayde
	 * FORWARD (toward current facing -- the opposite direction from Dodge's backward retreat) using
	 * the same LaunchCharacter pattern Dodge's own impulse uses, and grants full invincibility for
	 * InvulnDashDuration via the same shared bIsInvincible/IFrameTimerHandle/ClearInvincibility
	 * mechanism Dodge and post-respawn invuln both already use. A separate ability from Dodge, not a
	 * replacement -- both coexist with their own buttons.
	 */
	void HandleInvulnDash(const FInputActionValue& Value);

	/** Timer callback: ends the dash movement/animation state (invincibility itself ends separately via ClearInvincibility, armed for the same duration). */
	void ClearInvulnDashState();

	/**
	 * Called every Tick while bIsDashing: reasserts MoveComp->Velocity.X to
	 * DashFacingSignAtStart * InvulnDashImpulseStrength (leaving Z untouched, so gravity still
	 * applies normally if airborne). A single one-shot LaunchCharacter impulse (the original
	 * implementation) is entirely at the mercy of CharacterMovementComponent's own per-mode
	 * deceleration -- confirmed via direct measurement that the identical impulse covered ~129uu
	 * grounded (GroundFriction/BrakingDecelerationWalking kill it almost immediately) vs ~669uu
	 * airborne (5.2x farther, far weaker falling deceleration) -- neither matches "fixed-distance,
	 * fixed-duration dash" from the spec, and the grounded case also visibly stopped moving well
	 * before InvulnDashFlipbook finished playing. Reasserting a constant velocity every tick for
	 * the whole window guarantees the same fixed distance (InvulnDashImpulseStrength *
	 * InvulnDashDuration) regardless of movement mode.
	 */
	void UpdateInvulnDash();

	/**
	 * Adds to RageMeter, clamped to [0, RageMax] -- called from both OnSwordHitboxBeginOverlap/
	 * FireShotTrace (damage DEALT, scaled by RageGainPerDamageDealt) and TakeDamage (damage TAKEN,
	 * scaled by RageGainPerDamageTaken) so either side of combat builds toward the ultimate. Both
	 * scale with the actual damage amount rather than a flat per-hit tick (deliberately different
	 * from GnarlyRank's flat-count model).
	 */
	void AddRage(float DamageAmount, float GainPerDamage);

	/**
	 * Bound to the RageActivateAction's Started event. Ignored unless RageMeter is full and Cayde
	 * isn't already transformed -- see IsRageFull(). Kicks off the ultimate sequence: spawns the
	 * rainbow beam effect and starts fading regular Cayde out (see UpdateUltimateFadeOut); RageMeter
	 * itself is NOT reset here -- only at the end of the 10-second window, in
	 * EndUltimateTransformation, per spec ("after 10 seconds: revert... and reset Rage to 0").
	 */
	void HandleUltimateActivate(const FInputActionValue& Value);

public:
	/** Input-agnostic entry point sharing HandleUltimateActivate's own guard/trigger logic -- exists so the ultimate sequence can be triggered directly (Python/editor scripting, automation) without needing a real Enhanced Input event. Returns false if the guard rejects it (not full / already transformed / mid-fade). */
	UFUNCTION(BlueprintCallable, Category = "Rage")
	bool TryActivateUltimate();

protected:

	/** Called every Tick while bIsFadingOutForUltimate: shrinks the sprite's scale toward 0 over UltimateFadeOutDuration, then calls BeginUltimateTransformation once it reaches 0. */
	void UpdateUltimateFadeOut(float DeltaSeconds);

	/** Swaps into the "riding Fancy Pants" state: sets bIsTransformed, restores sprite scale to 1 (now showing FancyIdleFlipbook instead of the just-shrunk normal sprite), and arms the 10-second UltimateDurationTimerHandle. */
	void BeginUltimateTransformation();

	/** Timer callback (fired UltimateDuration seconds after BeginUltimateTransformation): reverts bIsTransformed to false (an instant swap back, not a mirrored fade -- see its own doc comment for why) and resets RageMeter to 0. */
	void EndUltimateTransformation();

	/**
	 * Spawns the rainbow beam-from-the-sky visual: a simple scaling cylinder mesh using
	 * M_RainbowBeam (a small unlit translucent material created for this, with a BeamColor vector
	 * parameter) positioned above Cayde, dropped down over its own short lifetime and hue-cycled
	 * every tick via UpdateRageBeamEffect. A simple scaling mesh rather than a full Niagara system --
	 * see class doc / summary report for why.
	 */
	void SpawnRageBeamEffect();

	/** Per-tick hue-cycle and drop-in animation for the active beam effect actor -- no-ops once RageBeamActor has been destroyed (its lifespan already expired). */
	void UpdateRageBeamEffect(float DeltaSeconds);

	/**
	 * Spawns (or repositions, if already created) the Fancy Attack laser: a thin cylinder mesh using
	 * the same M_RainbowBeam material as the ultimate's sky-beam, stretched from BeamStart to BeamEnd
	 * and hue-cycled/faded out over FancyAttackBeamLifetime via UpdateFancyAttackBeamEffect. Called
	 * from FireShotTrace only while bIsTransformed, reusing that same call's already-computed hitscan
	 * geometry (BeamEnd is the actual hit location if something was hit, or the max-range point
	 * otherwise) so the visual always matches exactly where the shot really went.
	 */
	void SpawnFancyAttackBeam(const FVector& BeamStart, const FVector& BeamEnd);

	/** Per-tick hue-cycle + fade-out for the active Fancy Attack beam -- no-ops entirely while bFancyAttackBeamActive is false. */
	void UpdateFancyAttackBeamEffect(float DeltaSeconds);

	/** Picks Idle/Walk/Run/Jump/Dodge/Dash/Block/SwordAttack/Shoot based on current state, and flips the sprite to face travel direction. */
	void UpdateAnimation();

	/**
	 * Standard AActor::TakeDamage override. Ignores the hit entirely (no health change, no Hurt
	 * animation) while CanTakeDamage() is false (i.e. mid-dodge i-frames or the post-respawn
	 * invuln window). On a hit that lands: deducts Health, plays HurtFlipbook briefly, and on
	 * reaching 0 logs "PLAYER DIED" and calls HandleDeath (input disable + respawn timer).
	 */
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Timer callback: ends the brief Hurt animation beat, letting UpdateAnimation's normal Idle/Walk/Run/etc. logic resume. */
	void ClearHurtState();

	/**
	 * Called once, from TakeDamage, the moment Health reaches 0 (GnarlyRank has already been reset
	 * by that same TakeDamage call, same as any other landed hit -- see ResetGnarlyRank). Logs
	 * "PLAYER DIED", disables input, shows the death screen, and pushes DeathContinueInputComponent
	 * so a button press can advance past it -- mirrors ADeathMetalCatEnemyBase::HandleDeath, minus
	 * the hide/disable-collision step (the player stays visible, playing out the Hurt pose
	 * TakeDamage already started). No more timer-driven auto-respawn -- see HandleRespawn.
	 */
	void HandleDeath();

	/**
	 * Bound to DeathContinueInputComponent's EKeys::AnyKey binding -- see HandleDeath. Ignores the
	 * press entirely while GnarlyRankHUDWidgetInstance still reports the death-screen fade-in in
	 * progress (so mashing a button the instant Health hits 0 can't skip/cut the animation short);
	 * once the fade has fully finished, the next press calls HandleRespawn.
	 */
	void HandleDeathContinuePressed();

	/**
	 * Called from HandleDeathContinuePressed once the death-screen fade-in has finished and the
	 * player has pressed a button to continue (previously a fixed-delay timer callback -- now
	 * purely input-driven, no auto-advance). Pops/destroys DeathContinueInputComponent, hides the
	 * death screen, resets Health to MaxHealth, clears bIsDead, and starts a brief post-respawn
	 * invincibility window (see PostRespawnInvulnDuration) by reusing the exact same
	 * bIsInvincible/IFrameTimerHandle/ClearInvincibility mechanism Dodge's i-frames use, so
	 * respawning doesn't immediately re-die standing in the same spot. Does NOT teleport the
	 * character or re-enable input directly -- those happen inside
	 * ARoomProgressionManager::BeginRoomTransition (teleport during the black pause, input
	 * re-enabled at fade-in-complete), triggered here via ResetToStartingRoom(), so the death
	 * restart gets the same fade-to-black treatment as every other room change. InitialSpawnTransform
	 * is still cached in BeginPlay but is no longer read anywhere -- kept in case something else
	 * needs the raw PlayerStart-derived transform later.
	 */
	void HandleRespawn();

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

	/**
	 * Recomputes XPToNextLevel from the current CurrentLevel via the flat CurrentLevel *
	 * XPPerLevelBase curve. Called once in BeginPlay and again after every level-up in AddXP.
	 */
	void RecalculateXPToNextLevel();

	/**
	 * Re-derives attribute effects that aren't computed inline at their point of use elsewhere.
	 * Currently just Speed -> CharacterMovementComponent::MaxWalkSpeed (Strength/Dexterity/Defense
	 * are all applied inline in OnSwordHitboxBeginOverlap/FireShotTrace/TakeDamage instead, since
	 * those need a fresh read each time rather than a cached derived value). Called once in
	 * BeginPlay and again after every level-up in AddXP.
	 */
	void ApplyAttributeEffects();

public:
	/**
	 * Awards Amount XP toward the next level. No-ops entirely once CurrentLevel has reached
	 * MaxLevel (per design: XP gain and leveling both stop past the cap). Handles one or more
	 * level-ups in a single call if Amount crosses more than one threshold at once; each level-up
	 * increases all four attributes by AttributeGainPerLevel, banks SkillPointsPerLevel skill
	 * points (stored only -- no spend/allocation system exists yet, that's a separate future task),
	 * and re-derives attribute effects via ApplyAttributeEffects.
	 */
	void AddXP(float Amount);

	/**
	 * Attempts to fire a Cayde quip line for TriggerType (see QuipTypes.h / QuipLibrary.h): pulls a
	 * fully random matching row from QuipDataTable and pushes it to
	 * GnarlyRankHUDWidgetInstance->ShowQuip, gated by TWO independent cooldown layers that must
	 * BOTH pass -- the per-type cooldown (QuipCooldown_Kill/Damage/Environment, fully independent
	 * per type) and the short GlobalQuipDebounce shared across all types (so two different trigger
	 * types can't fire back-to-back). A suppressed or failed attempt (cooldown not yet elapsed, no
	 * table, no matching row) never updates either LastQuipShownTime or the per-type timestamp --
	 * only an actual fired quip consumes the cooldown.
	 */
	void TriggerQuip(EQuipTriggerType TriggerType);

	/** Dev/testing console command (Exec) -- calls TriggerQuip for the named trigger type ("Kill"/"Damage"/"Environment", case-insensitive) so the two-layer cooldown system can be exercised manually from the in-game console without waiting for real kill/damage/environment events. */
	UFUNCTION(Exec)
	void TestQuip(const FString& TriggerTypeName);

	/**
	 * TEMP dev/testing console command (Exec) -- empirically measures real jump distance for the
	 * Room Geometry Designer's movement constants, since JumpZVelocity/AirControl changed since
	 * rooms were originally validated. "Running" sets horizontal velocity to the current
	 * MoveComp->MaxWalkSpeed at takeoff (matching a full run-up); "Standing" starts at zero
	 * horizontal velocity (pure air-control-driven acceleration for the whole arc). Either way,
	 * forward input is held for the entire flight (see UpdateJumpDistanceTest), matching how a
	 * player actually clears a gap -- not just the takeoff instant. Logs StartX/EndX/Distance the
	 * moment MovementMode returns to Walking. Strip once the new constants are captured.
	 */
	UFUNCTION(Exec)
	void TestJumpDistance(const FString& JumpTypeName);

	/** Public accessor for GnarlyRankHUDWidgetInstance -- ARoomProgressionManager::BeginRoomTransition needs to drive the room-transition fade (StartRoomFadeOut/StartRoomFadeIn) on it, and can't reach a private member of another class directly. May return nullptr before NotifyControllerChanged has ever created the widget. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	UGnarlyRankHUDWidget* GetGnarlyRankHUDWidget() const { return GnarlyRankHUDWidgetInstance; }

	/** Tick-driven follow-up to TestJumpDistance -- holds forward input every frame and detects landing (see TestJumpDistance's own doc comment). No-ops entirely while bJumpDistanceTestActive is false. */
	void UpdateJumpDistanceTest();

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

	/**
	 * Digital (bool) action, held to signal "aiming down" -- drives the airborne angled-shot
	 * (Gun Fire) and Spinny-Down (Sword Attack) directional modifiers. No standalone effect on its
	 * own; this project has no vertical movement axis, so unlike "Back" (derived from the existing
	 * MoveRightAction against current facing) there was nothing existing to reuse for "Down" --
	 * added as its own held-modifier action rather than a movement-triggering button.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> AimDownAction;

	/** Digital (bool) action, full block while held. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> BlockAction;

	/** Digital (bool) action, triggers the invulnerable forward dash on press. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> InvulnDashAction;

	/** Digital (bool) action, activates the Rage ultimate on press -- only takes effect while RageMeter is full (see HandleUltimateActivate). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> RageActivateAction;

	// -- Movement --

	/** Top horizontal move speed in uu/s; drives CharacterMovementComponent::MaxWalkSpeed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0"))
	float MaxMoveSpeed = 600.f;

	/**
	 * Initial upward velocity (uu/s) applied by a regular jump -- drives
	 * CharacterMovementComponent::JumpZVelocity. Placeholder value, tune freely -- raised from the
	 * engine default of 420 (max jump height ~90uu, confirmed too low during platforming testing:
	 * some Room Geometry Designer ledge_step pieces sit right at that old ceiling, leaving zero
	 * margin for anything less than frame-perfect play) to 600, then further retuned via live PIE
	 * testing to 708.65, then to the current 773.22265625 (max jump height increases accordingly
	 * each time). Tools/room_geometry_designer.py's own MAX_JUMP_HEIGHT constant was derived from
	 * an OLDER value and needs re-querying/updating (and existing rooms re-validated against the
	 * new, larger ceiling) if this is tuned further -- raising it doesn't invalidate anything
	 * already generated (a smaller old ceiling is a strict subset of a larger new one), it just
	 * means future room generation isn't using all the newly-available headroom until that
	 * constant is refreshed.
	 *
	 * CORRECTED [this session]: this default had drifted to a stale 708.652466 while the actual,
	 * intentionally-tuned live value (confirmed via Foundation Extractor's live-CDO-vs-header-
	 * default check, and via multiple earlier live PIE checks this project has already run) was
	 * 773.22265625 -- i.e. this header was the stale side of the mismatch, not the Blueprint CDO.
	 * Updated to match the real, current, intentional tuning.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0"))
	float JumpZVelocity = 773.22265625f;

	/**
	 * Drives CharacterMovementComponent::AirControl (0-1) -- the engine's own horizontal-steering
	 * authority multiplier while airborne. This project never explicitly set it, so it sat at the
	 * engine default of 0.05 the whole time: AddMovementInput calls while falling were registering
	 * correctly (confirmed directly -- HandleMoveRight has no airborne gate, and the Enhanced Input
	 * MoveRightAction asset has no Triggers/Modifiers that could suppress it), but the resulting
	 * steering effect was reduced to ~5% of normal, reading as "no air control at all" on every
	 * jump. Raised to 1.0 (full, Mario-style air steering) as the new baseline for every jump; the
	 * wall-jump commitment window (WallJumpCommitmentInputScale) still layers on top of THIS value
	 * for its own brief reduced-authority period after a wall jump specifically, rather than being
	 * the only place air control exists. Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0", ClampMax = "1"))
	float AirControl = 1.f;

	// -- Animation --

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> IdleFlipbook;

	/**
	 * Shown for any nonzero horizontal speed while grounded -- Walk was removed as a separate state
	 * (it briefly flashed on-screen right at movement start before Run took over, and served no
	 * purpose once Run's own art covers slow-to-fast movement fine on its own): Move input now goes
	 * straight from Idle to Run with no intermediate state or speed threshold.
	 */
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

	/**
	 * Dedicated held-fire loop: a steady gun-extended stance with muzzle-flash variations across
	 * its 4 frames, played looping for the entire duration of a hold, starting immediately on
	 * press. FB_DeathMetalCat_Shoot (the old draw-pose row this used to hand off from after a brief
	 * windup) has been fully removed -- its frames didn't include a pose that stays fully extended
	 * while flashing, which made reusing it for held-fire look like a repeated draw motion, and the
	 * windup itself was the source of a visible flash-then-swap the player could see on every shot.
	 * See git history for both the original investigation and the removal.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> HoldFireFlipbook;

	/** Shown briefly (HurtDuration seconds) when a hit lands (TakeDamage), then hands back to normal Idle/Walk/Run/etc. logic. Already imported as part of the v2 sprite sheet pass -- just needs assigning here. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> HurtFlipbook;

	/** Shown looping while bIsWallSliding is true -- see UpdateWallSlide. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> WallSlideFlipbook;

	/**
	 * Shown looping for the sustained hold-fire loop instead of HoldFireFlipbook whenever a shot
	 * fires while airborne WITHOUT holding Down -- see FireShotTrace/UpdateHoldFireFlipbook. Despite
	 * the name (kept for Blueprint-reference stability), this is now the "regular" airborne shot,
	 * firing horizontally like the grounded shot -- AirShotAngledFlipbook below is the one that
	 * actually fires at a fixed 45 degrees down. See AirShotAngledFlipbook's doc comment for why the
	 * angle moved.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> AirDownShotFlipbook;

	/**
	 * Shown looping instead of AirDownShotFlipbook whenever an airborne shot fires WHILE holding
	 * Down (or Forward+Down) -- fires at a fixed 45-degrees-down angle. Before this was added, EVERY
	 * airborne shot fired at that fixed 45-degree angle unconditionally; since the assignment
	 * explicitly distinguishes "the normal air-shot" from this "instead of" variant, the default
	 * airborne trajectory was changed to horizontal (matching the grounded shot) so the two are
	 * actually distinct -- see FireShotTrace.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> AirShotAngledFlipbook;

	/** Ground combo stage 2 (chained from the base swing) -- see SwordComboIndex/StartSwordComboStage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> SwordCombo2Flipbook;

	/** Ground combo stage 3 (chained from stage 2, loops back to the base swing after) -- see SwordComboIndex/StartSwordComboStage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> SwordCombo3Flipbook;

	/** Grounded forward swing triggered by holding Back + Sword Attack -- launches the hit enemy upward on a landed hit. See StartUppy/UppyLaunchVelocityZ. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> UppyFlipbook;

	/** Airborne forward swing (Sword Attack while airborne, not holding Down). See StartDoubleWhammy. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> DoubleWhammyFlipbook;

	/** Airborne downward swing (Sword Attack while airborne AND holding Down) -- ground-pound style fast descent. See StartSpinnyDown/SpinnyDownFallSpeed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> SpinnyDownFlipbook;

	/** Shown looping while bIsBlocking is true. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> BlockFlipbook;

	/**
	 * Shown once for the duration of the invuln dash. This sheet's after-image/speed-trail effect is
	 * authored directly into its frames -- played as-is via normal SetFlipbook/PlayFromStart, no
	 * extra ghost-trail code layered on top, so the effect stays exactly as authored.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> InvulnDashFlipbook;

	// -- Rage / Ultimate ("riding Fancy Pants") --

	/** Shown looping while bIsTransformed and grounded/stationary -- replaces IdleFlipbook for the duration of the ultimate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> FancyIdleFlipbook;

	/** Shown looping while bIsTransformed and moving horizontally -- replaces RunFlipbook for the duration of the ultimate. No separate walk/run split for the mount, same as solo Cayde post-Walk-removal. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> FancyGallopFlipbook;

	/** Shown once (non-looping) for the rainbow-laser-eyes attack -- Gun Fire is repurposed to trigger this instead of the normal shoot sequence while bIsTransformed; see HandleShootStarted. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> FancyAttackFlipbook;

	/**
	 * Shown looping in place of IdleFlipbook while bIsCatNipActive and standing still -- part of the
	 * "Super Cayde" Cat Nip moveset alongside CatNipWalkFlipbook/CatNipAttackFlipbook/
	 * CatNipJumpFlipbook below. Sword Attack/Dodge/Block/etc. are still completely unaffected --
	 * only Idle, Run, Gun Fire (CatNipAttackFlipbook overrides HoldFire, see its own doc comment),
	 * and the airborne pose swap while Cat Nip is active. bIsTransformed still wins if somehow both
	 * are active at once (FancyIdleFlipbook takes
	 * priority) -- riding Fancy Pants already replaces the whole moveset, so it stays the more
	 * dominant state -- see UpdateAnimation.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> CatNipIdleFlipbook;

	/** Shown looping in place of RunFlipbook while bIsCatNipActive and moving -- Super Cayde has no separate walk-vs-run distinction (this project doesn't have one at baseline either, see UpdateAnimation's "Walk removed as a separate state" comment), one sheet covers both. See CatNipIdleFlipbook's own doc comment for the rest of the Cat Nip moveset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> CatNipWalkFlipbook;

	/** Shown for BOTH attack inputs while bIsCatNipActive -- in place of HoldFireFlipbook/AirShotAngledFlipbook/AirDownShotFlipbook for the Shoot input (UpdateHoldFireFlipbook, same pattern as FancyAttackFlipbook while riding Fancy Pants), and in place of SwordAttackFlipbook/SwordCombo2Flipbook/SwordCombo3Flipbook for the Attack input's base ground combo (StartSwordComboStage). There's only one Cat Nip attack sheet (a gun-fire pose -- Cayde is drawn holding his gun, sword sheathed on his back, in every Cat Nip flipbook), so it's Super Cayde's one attack visual regardless of which button triggers it. Uppy/Double Whammy/Spinny Down (the airborne/backward Attack-input special moves) keep their normal art -- no Cat Nip art exists for those. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> CatNipAttackFlipbook;

	/**
	 * Shown airborne while bIsCatNipActive, in place of JumpFlipbook -- unlike JumpFlipbook (a
	 * 2-frame rising/falling pose pair explicitly driven by Velocity.Z, see UpdateAnimation), this
	 * is a full multi-frame animated arc like every other Cat Nip flipbook, so it's just played
	 * looping like Idle/Run rather than frame-locked to velocity -- there's no reliable way to know
	 * which frame index of a full animated sequence corresponds to "rising" vs "falling" the way
	 * JumpFlipbook's own two poses were explicitly authored to.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> CatNipJumpFlipbook;

	/**
	 * Pre-mirrored (per-cell horizontally flipped source art, not a runtime Scale.X mirror)
	 * counterpart to CatNipJumpFlipbook -- confirmed live (2026-09-01) that Scale.X-based mirroring
	 * doesn't reliably flip CatNipJumpFlipbook's render, so airborne Cat Nip picks between the two
	 * assets directly by movement direction instead of relying on the sprite's general
	 * velocity-based Scale.X flip the way every other flipbook in the game does. See UpdateAnimation.
	 *
	 * Counter-intuitively, THIS is the asset shown while moving RIGHT, not CatNipJumpFlipbook --
	 * this project's Paper2D sprite rendering mirrors ALL raw RawAssets/Allies art relative to its
	 * source PNG (the same systemic issue ue_fix_cayde_facing_flip.py's own docstring documents for
	 * the base moveset), so the never-flipped CatNipJumpFlipbook actually renders facing LEFT, and
	 * this pre-flipped-source asset renders facing RIGHT once that systemic mirror lands on top of
	 * it. Don't rename or "fix" this pairing without re-confirming that mirror is still in effect.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UPaperFlipbook> CatNipJumpFlipbookMirrored;

	/** Frame index CatNipJumpFlipbook/CatNipJumpFlipbookMirrored starts playing from when Cayde first goes airborne -- the sheet's earlier frames are a grounded crouch/wind-up pose (per the design ask), not an airborne one, so starting at frame 0 would show him crouching while already in mid-air. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0"))
	int32 CatNipJumpStartFrame = 13;

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

	/** Returns false while invincibility frames OR a block are active; TakeDamage() checks this and ignores any hit entirely while it's false. */
	UFUNCTION(BlueprintCallable, Category = "Dodge")
	bool CanTakeDamage() const;

	// -- Block --

	/** True while the Block input is held. Full block (no partial): CanTakeDamage() returns false the whole time, same protection Dodge's i-frames give -- see HandleBlockStarted/CanTakeDamage. */
	UPROPERTY(BlueprintReadOnly, Category = "Block")
	bool bIsBlocking = false;

	// -- Invuln Dash --

	/** True for InvulnDashDuration seconds after a dash starts (movement/animation window; invincibility itself is tracked via the shared bIsInvincible flag). */
	UPROPERTY(BlueprintReadOnly, Category = "InvulnDash")
	bool bIsDashing = false;

	/** Forward velocity (uu/s) applied by the invuln dash, via LaunchCharacter -- same impulse-based approach as DodgeImpulseStrength, just toward facing instead of away from it. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "InvulnDash", meta = (ClampMin = "0"))
	float InvulnDashImpulseStrength = 1600.f;

	/**
	 * Minimum floor (seconds) for how long the dash's movement/animation state AND its invincibility
	 * both last -- HandleInvulnDash actually arms both timers for
	 * max(InvulnDashDuration, InvulnDashFlipbook->GetTotalDuration() + 0.05s), so retiming the
	 * flipbook's fps/frame count later can never silently cut it short again the way a fixed value
	 * did before (25 frames @ 30fps = 0.833s of animation was getting cut off at the old fixed
	 * 0.3s). Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "InvulnDash", meta = (ClampMin = "0"))
	float InvulnDashDuration = 0.3f;

	// -- Wall Slide / Wall Jump --

	/** Max downward fall speed (uu/s) while wall-sliding -- MoveComp->Velocity.Z is clamped toward -this instead of letting normal gravity fall speed build up further. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "WallSlide", meta = (ClampMin = "0"))
	float WallSlideSpeed = 150.f;

	/** Short forward sweep distance (uu) from the capsule used to detect a wall to slide on, tried in whichever direction the player is currently holding movement input toward -- see DetectWallForSlide. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "WallSlide", meta = (ClampMin = "0"))
	float WallCheckDistance = 45.f;

	/** Sweep sphere radius (uu) for the wall-detection trace above -- same "small sphere, not a zero-thickness line" tolerance reasoning as the gun's own hitscan trace, see GunTraceRadius. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "WallSlide", meta = (ClampMin = "0"))
	float WallCheckRadius = 10.f;

	/** Outward (away from the wall) horizontal velocity component (uu/s) applied by a wall jump. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "WallSlide", meta = (ClampMin = "0"))
	float WallJumpForceHorizontal = 600.f;

	/** Upward vertical velocity component (uu/s) applied by a wall jump. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "WallSlide", meta = (ClampMin = "0"))
	float WallJumpForceVertical = 700.f;

	/**
	 * How long (seconds) after a wall jump movement input is scaled by WallJumpCommitmentInputScale
	 * rather than applied at full authority, before returning to normal air control. Was previously
	 * a full input lockout (movement ignored entirely) -- replaced because that left the player on
	 * rails until landing, with no way to chain a wall jump into a second wall or platform. A brief
	 * reduced-authority window (rather than jumping straight to full control) still gives the
	 * outward launch some resistance against being instantly canceled by holding back toward the
	 * wall, without blocking genuine redirection. Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "WallSlide", meta = (ClampMin = "0"))
	float WallJumpCommitmentDuration = 0.15f;

	/** Movement input scale (0-1) during the WallJumpCommitmentDuration window after a wall jump -- 1.0 would be full immediate air control, lower values leave a brief reduced-authority "commitment" beat before full control kicks in. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "WallSlide", meta = (ClampMin = "0", ClampMax = "1"))
	float WallJumpCommitmentInputScale = 0.5f;

	/** True while airborne, pressed against a detected wall, and holding movement input toward it -- see UpdateWallSlide. */
	UPROPERTY(BlueprintReadOnly, Category = "WallSlide")
	bool bIsWallSliding = false;

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

	/** Fraction of MaxHealth (0-1) below which the HUD's low-health screen tint starts fading in -- 0 opacity right at this threshold, ramping smoothly to LowHealthTintMaxOpacity at Health == 0. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0", ClampMax = "1"))
	float LowHealthThreshold = 0.25f;

	/** Max opacity of the low-health screen tint, reached exactly at Health == 0. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0", ClampMax = "1"))
	float LowHealthTintMaxOpacity = 0.5f;

	/** How long (seconds) CanTakeDamage() stays false right after respawning -- reuses Dodge's own i-frame mechanism (bIsInvincible/IFrameTimerHandle), just armed for this duration instead of IFrameDuration, so respawning doesn't immediately re-die standing in the same spot. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0"))
	float PostRespawnInvulnDuration = 1.0f;

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

	// -- Leveling --
	//
	// Simplified XP/leveling: no interactive allocation screen. Every level-up auto-applies a flat
	// increase to all four attributes below; SkillPoints is purely banked (counted, stored) for a
	// future combo/special-move system that doesn't exist yet -- no spend/allocation UI in this pass.

	/** Current level, starting at 1. Capped at MaxLevel -- XP gain and leveling both stop past the cap, see AddXP. */
	UPROPERTY(BlueprintReadOnly, Category = "Leveling")
	int32 CurrentLevel = 1;

	/** Level cap. Placeholder value (30 per spec), tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Leveling", meta = (ClampMin = "1"))
	int32 MaxLevel = 30;

	/** Current banked XP toward the next level; consumed (via AddXP's threshold loop) as CurrentLevel increases. */
	UPROPERTY(BlueprintReadOnly, Category = "Leveling")
	float CurrentXP = 0.f;

	/** XP required to advance from CurrentLevel to CurrentLevel+1 -- recomputed by RecalculateXPToNextLevel as CurrentLevel * XPPerLevelBase (a flat curve, not a complex one). */
	UPROPERTY(BlueprintReadOnly, Category = "Leveling")
	float XPToNextLevel = 50.f;

	/** Scaling factor for the flat XPToNextLevel curve (CurrentLevel * this). Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Leveling", meta = (ClampMin = "0"))
	float XPPerLevelBase = 50.f;

	/** Flat amount each of Speed/Strength/Dexterity/Defense increases by on every level-up. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Leveling", meta = (ClampMin = "0"))
	float AttributeGainPerLevel = 2.f;

	/** Skill points banked per level-up. Purely stored (SkillPoints below) -- no spend/allocation system exists yet. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Leveling", meta = (ClampMin = "0"))
	int32 SkillPointsPerLevel = 1;

	/** Banked skill points, incremented by SkillPointsPerLevel on every level-up. No spend/allocation UI in this pass -- for a future combo/special-move system. */
	UPROPERTY(BlueprintReadOnly, Category = "Leveling")
	int32 SkillPoints = 0;

	// -- Attributes --
	//
	// Each starts at 0 (meaning "no bonus yet, purely earned via leveling") and grows by
	// AttributeGainPerLevel per level-up. Effects are applied inline at their point of use
	// (OnSwordHitboxBeginOverlap, FireShotTrace, TakeDamage) rather than cached, except Speed
	// (see ApplyAttributeEffects) which drives CharacterMovementComponent::MaxWalkSpeed directly.

	/** Move-speed attribute; final MaxWalkSpeed = MaxMoveSpeed * (1 + Speed * SpeedMultiplierPerPoint) -- see ApplyAttributeEffects. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attributes", meta = (ClampMin = "0"))
	float Speed = 0.f;

	/** Move-speed multiplier scaling per Speed point. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attributes", meta = (ClampMin = "0"))
	float SpeedMultiplierPerPoint = 0.01f;

	/**
	 * Sword-damage attribute. Its multiplier (1 + Strength * StrengthMultiplierPerPoint) MULTIPLIES
	 * with GnarlyRank's melee multiplier in OnSwordHitboxBeginOverlap (not additive) -- keeps both
	 * systems meaningfully impactful rather than one diluting the other.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attributes", meta = (ClampMin = "0"))
	float Strength = 0.f;

	/** Sword-damage multiplier scaling per Strength point. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attributes", meta = (ClampMin = "0"))
	float StrengthMultiplierPerPoint = 0.02f;

	/**
	 * Gun-damage attribute. Applied the same multiplicative way as Strength (RolledDamage *=
	 * DexterityMultiplier in FireShotTrace) for consistency, even though gun currently has no
	 * Gnarly-style second multiplier to stack against -- keeps the pattern ready if that changes.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attributes", meta = (ClampMin = "0"))
	float Dexterity = 0.f;

	/** Gun-damage multiplier scaling per Dexterity point. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attributes", meta = (ClampMin = "0"))
	float DexterityMultiplierPerPoint = 0.02f;

	/** Damage-resistance attribute: TakeDamage multiplies incoming damage by (1 - Defense * DefenseResistPerPoint), clamped so resistance can never reach 100%. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attributes", meta = (ClampMin = "0"))
	float Defense = 0.f;

	/** Damage-resistance fraction per Defense point, before the 90%-resistance clamp in TakeDamage. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attributes", meta = (ClampMin = "0"))
	float DefenseResistPerPoint = 0.01f;

	// -- Sword Attack --

	/**
	 * Total lockout duration of the attack, in seconds -- doubles as the cooldown, since
	 * HandleSwordAttack ignores re-triggers for as long as bIsAttacking is true. This (along with
	 * HitboxActiveDelay/HitboxActiveDuration below) is a FIXED REAL-TIME value, NOT driven by the
	 * sword flipbooks' own frame/notify progress -- when all six sword flipbooks were slowed to 75%
	 * speed, this had to be scaled by the same 1/0.75 factor to keep the hitbox window in sync with
	 * the now-slower swing (confirmed via code review this wasn't already frame-driven, so slowing
	 * the animation alone would NOT have carried hit timing along with it). Placeholder value, tune
	 * freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float AttackDuration = 0.5333333f;

	/**
	 * Seconds after the attack starts before the hitbox turns on -- the wind-up frames
	 * (SwordAttack_01/02) shouldn't count as active hit frames. Not explicitly requested as a
	 * separate tunable, but needed to implement "middle portion only" -- see also
	 * HitboxActiveDuration. Scaled by 1/0.75 along with AttackDuration when the sword flipbooks were
	 * slowed to 75% speed -- see AttackDuration's doc comment. Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float HitboxActiveDelay = 0.2f;

	/**
	 * Seconds the hitbox stays enabled once active, roughly covering the strike frame
	 * (SwordAttack_03, the one with the slash-swirl VFX) without extending into full recovery.
	 * Scaled by 1/0.75 along with AttackDuration when the sword flipbooks were slowed to 75% speed
	 * -- see AttackDuration's doc comment. Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float HitboxActiveDuration = 0.16f;

	/** True for AttackDuration seconds after a sword attack starts; blocks re-triggering. */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	bool bIsAttacking = false;

	/** Base damage a sword hit deals, before RollDamage's tier multiplier. All ground-combo stages, Uppy, and Double Whammy use this same base -- no per-stage escalation (see SwordComboIndex). Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float SwordBaseDamage = 20.f;

	/**
	 * Named, easily-tunable buffered-input window (seconds) for the 3-hit ground sword combo: a
	 * Sword Attack press this soon after the current stage finishes (or during its recovery frames)
	 * chains into the next stage instead of resetting to the base swing. 0.4s default per spec --
	 * likely needs feel-tuning once playtested. See HandleSwordAttack/ClearAttackState/SwordComboIndex.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float SwordComboBufferWindow = 0.4f;

	/**
	 * Upward velocity (uu/s) applied to an enemy hit by Uppy, via LaunchCharacter, on top of normal
	 * damage. ~450 gives roughly a second of hang time at default gravity -- enough to matter for a
	 * future juggle/air-combo follow-up (Double Whammy / Spinny Down) without being a full launch.
	 * Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float UppyLaunchVelocityZ = 450.f;

	/**
	 * Downward velocity (uu/s) forced onto Cayde for the duration of The Spinny Down Thing -- a
	 * ground-pound-style fast descent, clearly faster than a normal fall. Placeholder value (no
	 * spec'd exact speed given), tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float SpinnyDownFallSpeed = 1500.f;

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

	/** Max hitscan trace distance in uu. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GunFire", meta = (ClampMin = "0"))
	float MaxTraceRange = 2000.f;

	/**
	 * Radius (uu) of the sphere swept along the hitscan trace, replacing what was originally a
	 * zero-thickness line trace. A raw line trace has zero tolerance for the target's exact
	 * Y-position matching the trace's fixed Y-line -- confirmed as the actual cause of gun fire
	 * whiffing against a correctly-aimed, correctly-collision-configured enemy that was merely a
	 * small amount off that exact line. A small sphere is standard practice for hitscan weapons
	 * specifically to avoid needing pixel/unit-perfect aim alignment. Placeholder value (10-15uu
	 * suggested), tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GunFire", meta = (ClampMin = "0"))
	float GunTraceRadius = 12.f;

	/** True for FireCooldown seconds after a shot is fired; blocks re-triggering. */
	UPROPERTY(BlueprintReadOnly, Category = "GunFire")
	bool bIsShooting = false;

	/** Base damage a gunshot deals, before RollDamage's tier multiplier. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GunFire", meta = (ClampMin = "0"))
	float GunBaseDamage = 10.f;

	/** Gravity multiplier applied while the air-fire float window is active (e.g. 0.35 = 35% of normal gravity -- a floaty hang, not a full stop). Placeholder value, tune freely. See ApplyAirFireFloat. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GunFire", meta = (ClampMin = "0", ClampMax = "1"))
	float AirFireGravityScaleMultiplier = 0.35f;

	/** How long (seconds) the air-fire float window lasts after an airborne shot before gravity returns to normal -- continuous fire re-arms (extends) this rather than stacking the multiplier further, so it can never turn into infinite hover. Placeholder value, tune freely. See ApplyAirFireFloat. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GunFire", meta = (ClampMin = "0"))
	float AirFireFloatDuration = 0.3f;

	// -- Quip --

	/** Offline-imported Cayde quip lines (see QuipTypes.h / AgentScripts/ue_import_quip_datatable.py) -- points at /Game/Data/DT_Quips. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Quip")
	TObjectPtr<UDataTable> QuipDataTable;

	/** How long (seconds) a triggered quip stays fully visible in the dialogue box before fading -- passed straight through to UGnarlyRankHUDWidget::ShowQuip. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Quip", meta = (ClampMin = "0"))
	float QuipDisplayDuration = 3.f;

	/** Per-type cooldown (seconds) for Kill-triggered quips -- fully independent of the Damage/Environment cooldowns below. Placeholder value (5 min), tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Quip", meta = (ClampMin = "0"))
	float QuipCooldown_Kill = 300.f;

	/** Per-type cooldown (seconds) for Damage-triggered quips -- fully independent of the Kill/Environment cooldowns. Placeholder value (5 min), tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Quip", meta = (ClampMin = "0"))
	float QuipCooldown_Damage = 300.f;

	/** Per-type cooldown (seconds) for Environment-triggered quips -- fully independent of the Kill/Damage cooldowns. Placeholder value (5 min), tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Quip", meta = (ClampMin = "0"))
	float QuipCooldown_Environment = 300.f;

	/** Short debounce (seconds) shared across ALL trigger types, on top of each type's own cooldown above -- stops two DIFFERENT trigger types from firing back-to-back even if neither's own per-type cooldown has started yet. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Quip", meta = (ClampMin = "0"))
	float GlobalQuipDebounce = 6.f;

	// -- Rage / Ultimate --
	//
	// Fills from damage Cayde deals OR receives (see AddRage), scaling with actual damage amounts
	// rather than a flat per-hit tick (deliberately different from GnarlyRank's flat-count model,
	// since Rage is meant to reflect the intensity of a fight, not just its length). Activating
	// while full drops a rainbow beam, fades Cayde out, and swaps him into a 10-second "riding Fancy
	// Pants" state (see RageActivateAction/HandleUltimateActivate).

	/** Current Rage, 0..RageMax. Reset to 0 only when the 10-second transformed window ends (EndUltimateTransformation), NOT at activation -- it stays visually full for the whole ultimate. */
	UPROPERTY(BlueprintReadOnly, Category = "Rage")
	float RageMeter = 0.f;

	/** Rage is full (and the ultimate can be activated) at this value. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Rage", meta = (ClampMin = "0"))
	float RageMax = 100.f;

	/**
	 * Rage gained per point of damage Cayde DEALS (sword or gun, post-multiplier actual damage
	 * applied) -- e.g. a 20-base sword hit at RageGainPerDamageDealt=0.5 grants 10 Rage. Chosen so a
	 * string of solid sword hits fills the meter in roughly the same ballpark of hits as a GnarlyRank
	 * rank-up (10-ish), keeping the two systems' pacing consistent with each other. Placeholder
	 * value, tune freely; document any change here since this is clearly a first-pass number.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Rage", meta = (ClampMin = "0"))
	float RageGainPerDamageDealt = 0.5f;

	/**
	 * Rage gained per point of damage Cayde TAKES (post-Defense-resistance actual damage, i.e. only
	 * damage that actually lands -- CanTakeDamage()==false, like blocking or i-frames, grants none,
	 * same gate TakeDamage already uses for GnarlyRank resets). Same 0.5 scale as
	 * RageGainPerDamageDealt for now -- no strong reason found to weight taking damage differently
	 * from dealing it, so kept symmetric rather than inventing an arbitrary asymmetry. Placeholder
	 * value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Rage", meta = (ClampMin = "0"))
	float RageGainPerDamageTaken = 0.5f;

	/** How long (seconds) the "riding Fancy Pants" transformed state lasts once activated. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Rage", meta = (ClampMin = "0"))
	float UltimateDuration = 12.f;

	/**
	 * How long (seconds) regular Cayde's sprite takes to shrink away once the ultimate is activated,
	 * before FancyIdleFlipbook takes over -- see UpdateUltimateFadeOut. A scale-down, not an alpha
	 * fade: the sprite material (MaskedUnlitSpriteMaterial) is Masked, not Translucent, so alpha only
	 * acts as a binary cutout threshold and can't produce a smooth visual fade -- shrinking to zero
	 * reads as "fades away quickly" without needing a different material.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Rage", meta = (ClampMin = "0"))
	float UltimateFadeOutDuration = 0.25f;

	/** How long (seconds) the rainbow beam-from-the-sky effect actor lives before self-destructing -- see SpawnRageBeamEffect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Rage", meta = (ClampMin = "0"))
	float RageBeamLifetime = 1.f;

	/** Degrees per second the beam's BeamColor material parameter cycles through the hue wheel -- see UpdateRageBeamEffect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Rage", meta = (ClampMin = "0"))
	float RageBeamHueCycleSpeed = 360.f;

	/**
	 * Vertical offset (uu) above GetActorLocation() the Fancy Attack laser starts from -- CAN be
	 * negative (the measured value is), so deliberately no ClampMin. Measured directly from
	 * FancyAttackFlipbook's own source art rather than guessed: found the beam's actual origin
	 * flare (the unicorn's glowing eye) at pixel (121, 212) in the frame's 384x384 canvas via a
	 * brightness search, confirmed against a marker overlay, then converted to world space using
	 * the sprite component's own known relative transform (RelativeLocation.Z = -10, queried live
	 * from the CDO) as the canvas-center anchor. The previous 60 (guessed, "above actor origin")
	 * was landing near the top of the mount's head, well above the actual eye -- this corrected
	 * value is BELOW actor origin, matching where the eye really sits on a creature this tall.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Rage")
	float FancyAttackBeamEyeHeight = -30.f;

	/** Horizontal offset (uu), in the current facing direction, added on top of FancyAttackBeamEyeHeight so the laser visually originates just in front of the mount's face rather than from inside its body. Measured the same way as FancyAttackBeamEyeHeight -- the eye flare sits 71px from canvas-center horizontally at this flipbook's PPU (1.0), so 71uu, not the previous guessed 30. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Rage", meta = (ClampMin = "0"))
	float FancyAttackBeamForwardOffset = 71.f;

	/** Radius (uu) of the Fancy Attack laser's cylinder mesh. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Rage", meta = (ClampMin = "0"))
	float FancyAttackBeamThickness = 8.f;

	/** How long (seconds) each individual shot's laser flash stays visible (hue-cycling, fading out) before hiding. Set slightly ABOVE FireCooldown (0.3s) rather than below it: FireShotTrace re-triggers SpawnFancyAttackBeam on every repeat shot while the button is held (same continuous hold-fire pattern as the regular gun -- see HandleShootHeld), so each new shot's flash refreshes this one back to full opacity before it fully fades, reading as one continuous beam rather than a series of gapped strobes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Rage", meta = (ClampMin = "0"))
	float FancyAttackBeamLifetime = 0.35f;

	/** Degrees per second the Fancy Attack laser's color cycles through the hue wheel -- faster than RageBeamHueCycleSpeed since this beam is on screen so much more briefly. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Rage", meta = (ClampMin = "0"))
	float FancyAttackBeamHueCycleSpeed = 720.f;

	/** True while riding Fancy Pants (the 10-second ultimate window) -- UpdateAnimation swaps Idle/Run for FancyIdle/FancyGallop, and HandleShootStarted repurposes Gun Fire into the laser attack, while this is true. */
	UPROPERTY(BlueprintReadOnly, Category = "Rage")
	bool bIsTransformed = false;

	/** Returns true once RageMeter has reached RageMax -- HandleUltimateActivate gates on this, and the HUD gates the pulsate/shake effect on it too. */
	UFUNCTION(BlueprintCallable, Category = "Rage")
	bool IsRageFull() const { return RageMeter >= RageMax; }

	/**
	 * Sets (or overwrites) how far to shift the sprite's own relative Z, in world units, whenever
	 * Flipbook is the one currently showing -- see FlipbookFeetOffsetCorrections' own doc comment
	 * for why this exists. A UFUNCTION specifically so it can be populated from editor scripting
	 * (measured per-flipbook via the source art's own pixel data, not guessed) without needing
	 * direct Python manipulation of a TMap-of-UObject-keys property, which is unreliable to
	 * round-trip. Idle itself is never given an entry (its correction is 0 by definition -- it's
	 * the baseline every other flipbook's correction was measured against).
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation")
	void SetFeetOffsetCorrection(UPaperFlipbook* Flipbook, float ZOffsetCorrection);

	/**
	 * Dev/testing entry point: forces CurrentFlipbook to Flipbook (also pushing it onto the sprite
	 * component directly) and reapplies ApplyFeetOffsetCorrection immediately, so floor-contact
	 * alignment for any animation state can be verified from editor/Python scripting without
	 * needing to actually trigger that state via real input -- mirrors TryActivateUltimate's own
	 * reason for existing (no LocalPlayer/EnhancedInputLocalPlayerSubsystem access from Python).
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation")
	void Debug_ForceFlipbookForFeetTest(UPaperFlipbook* Flipbook);

	/**
	 * Dev/testing entry point: starts (bStart=true) or cleanly ends (bStart=false) the continuous
	 * hold-fire loop exactly as HandleShootStarted/HandleShootReleased would from a real held Shoot
	 * input -- including the Fancy Attack beam path while bIsTransformed, since that now goes
	 * through this same continuous loop. Exists for the same reason as
	 * Debug_ForceFlipbookForFeetTest -- real Enhanced Input can't be injected from Python.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rage")
	void Debug_SetHoldFireForTest(bool bStart);

	/** Dev/testing entry point: fires the Cat Nip depletion beam directly, without needing a full activate/wait/expire cycle -- same reasoning as TryActivateUltimate and the other Debug_ helpers (no LocalPlayer/EnhancedInputLocalPlayerSubsystem access from Python, and this also sidesteps the real-time wait a natural CatNipDuration expiry would need). */
	UFUNCTION(BlueprintCallable, Category = "Items")
	void Debug_ForceCatNipDepletionEffect();

	/**
	 * Dev/testing entry point: calls HandleRespawn() directly, same reason every other Debug_* entry
	 * point exists -- lets editor/Python tooling verify the death-triggered reset (Scraps, every
	 * Item*Bonus, Health/bIsDead) without needing to actually route through the real death-screen
	 * "press any key to continue" input flow, which (like held Shoot input) can't be injected from
	 * Python. Also runs whatever else HandleRespawn does (room reset, post-respawn i-frames, etc.) --
	 * it's the exact same function real death uses, not a stripped-down copy.
	 */
	UFUNCTION(BlueprintCallable, Category = "Items")
	void Debug_ForceRespawnForTest();

	// -- Items / Scraps / Cat Nip --

	/** In-game currency. No spend/shop mechanic exists yet -- just tracked and displayed on the HUD (see UGnarlyRankHUDWidget). Resets to 0 on death, same as Health/GnarlyRank/room progress -- see HandleRespawn. */
	UPROPERTY(BlueprintReadOnly, Category = "Items")
	int32 Scraps = 0;

	/** Adds Amount to Scraps (clamped so a negative Amount can never go below 0, though nothing currently grants negative Scraps). Called by AItemPickupCrate when its weighted drop table resolves to Scraps rather than a special item. */
	UFUNCTION(BlueprintCallable, Category = "Items")
	void AddScraps(int32 Amount);

	/**
	 * Single entry point AItemPickupCrate calls once its weighted drop table resolves to a special
	 * item (everything in EPickupResultType except Scraps, which goes through AddScraps directly
	 * instead) -- centralizes what each item actually does in one place rather than scattering
	 * per-item logic across the crate class. The three heal items (ScratchPatch/NineLifeStim/
	 * DeathDefier) heal a percentage of MaxHealth immediately; the six persistent stat items
	 * (RazorFang/DeadshotRounds/SteelFur/FancyFeed/GnarlyAmp/MirrorClaw) each ADD to their own
	 * running bonus below (multiple pickups of the same item stack additively, uncapped -- no
	 * design reason found yet to cap them) and last until death, same reset rule as Scraps; CatNip
	 * starts the 12-second invincibility window (see BeginCatNip).
	 */
	UFUNCTION(BlueprintCallable, Category = "Items")
	void ResolvePickup(EPickupResultType Type);

	/** Fraction of MaxHealth restored by ScratchPatch (the common/small heal tier). Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items", meta = (ClampMin = "0", ClampMax = "1"))
	float ScratchPatchHealPercent = 0.15f;

	/** Fraction of MaxHealth restored by NineLifeStim (the rare/high tier -- see AItemPickupCrate's drop weights, this is the one explicitly called out as "harder to find"). Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items", meta = (ClampMin = "0", ClampMax = "1"))
	float NineLifeStimHealPercent = 0.35f;

	/** Fraction of MaxHealth restored by DeathDefier (the large/uncommon tier). Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items", meta = (ClampMin = "0", ClampMax = "1"))
	float DeathDefierHealPercent = 0.60f;

	/** How much each RazorFang pickup adds to the sword-damage multiplier (RolledDamage *= 1 + ItemSwordDamageBonus, alongside GnarlyMultiplier/StrengthMultiplier in OnSwordHitboxBeginOverlap) -- stacks additively per pickup. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items", meta = (ClampMin = "0"))
	float RazorFangBonusPerPickup = 0.15f;

	/** How much each DeadshotRounds pickup adds to the gun-damage multiplier (RolledDamage *= 1 + ItemGunDamageBonus, alongside DexterityMultiplier in FireShotTrace) -- stacks additively per pickup. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items", meta = (ClampMin = "0"))
	float DeadshotRoundsBonusPerPickup = 0.15f;

	/** How much each SteelFur pickup adds directly to the damage-resistance fraction TakeDamage clamps to 0.9 (on top of Defense*DefenseResistPerPoint) -- "slightly" per the design ask, so a small flat bump rather than a big per-point attribute. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items", meta = (ClampMin = "0"))
	float SteelFurBonusPerPickup = 0.03f;

	/** How many extra seconds each FancyFeed pickup adds to UltimateDuration when BeginUltimateTransformation next arms UltimateDurationTimerHandle. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items", meta = (ClampMin = "0"))
	float FancyFeedBonusPerPickup = 3.f;

	/** How many extra points each GnarlyAmp pickup adds to GnarlyHitCount's per-hit increment in RegisterGnarlyHit (normally a flat +1) -- makes GnarlyRank climb faster for the rest of the run. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items", meta = (ClampMin = "0"))
	int32 GnarlyAmpBonusPerPickup = 1;

	/** How much each MirrorClaw pickup adds to ItemMirrorClawChance (see TakeDamage) -- a "small chance", so kept modest per stack. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items", meta = (ClampMin = "0", ClampMax = "1"))
	float MirrorClawChancePerPickup = 0.08f;

	/** Running total from RazorFang pickups this run -- see ResolvePickup/OnSwordHitboxBeginOverlap. Resets to 0 on death (HandleRespawn). */
	UPROPERTY(BlueprintReadOnly, Category = "Items")
	float ItemSwordDamageBonus = 0.f;

	/** Running total from DeadshotRounds pickups this run -- see ResolvePickup/FireShotTrace. Resets to 0 on death. */
	UPROPERTY(BlueprintReadOnly, Category = "Items")
	float ItemGunDamageBonus = 0.f;

	/** Running total from SteelFur pickups this run -- see ResolvePickup/TakeDamage. Resets to 0 on death. */
	UPROPERTY(BlueprintReadOnly, Category = "Items")
	float ItemDefenseResistBonus = 0.f;

	/** Running total from FancyFeed pickups this run -- see ResolvePickup/BeginUltimateTransformation. Resets to 0 on death. */
	UPROPERTY(BlueprintReadOnly, Category = "Items")
	float ItemUltimateDurationBonus = 0.f;

	/** Running total from GnarlyAmp pickups this run -- see ResolvePickup/RegisterGnarlyHit. Resets to 0 on death. */
	UPROPERTY(BlueprintReadOnly, Category = "Items")
	int32 ItemGnarlyAmpBonus = 0;

	/** Running total from MirrorClaw pickups this run (a chance [0-1], clamped in ResolvePickup so stacking many can never exceed a guaranteed deflect) -- see TakeDamage. Resets to 0 on death. */
	UPROPERTY(BlueprintReadOnly, Category = "Items")
	float ItemMirrorClawChance = 0.f;

	/** True for CatNipDuration seconds after picking up CatNip -- CanTakeDamage() reports false unconditionally while this is true (a fully separate invincibility source from bIsInvincible/dodge-i-frames, deliberately not sharing that flag/timer so the two can never cancel each other early -- see CanTakeDamage), UpdateAnimation swaps Idle to CatNipIdleFlipbook, GetSprite()'s tint changes to CatNipTintColor, and Tick applies passive contact damage to any enemy within touching distance. */
	UPROPERTY(BlueprintReadOnly, Category = "Items")
	bool bIsCatNipActive = false;

	/** How long (seconds) Cat Nip's invincibility window lasts. Placeholder value, tune freely (the design ask specifies 12). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items", meta = (ClampMin = "0"))
	float CatNipDuration = 12.f;

	/** Damage dealt automatically (via UGameplayStatics::ApplyDamage, same path everything else uses) to any enemy that touches Cayde while Cat Nip is active -- see UpdateCatNipContactDamage. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items", meta = (ClampMin = "0"))
	float CatNipContactDamage = 15.f;

	/** Minimum seconds between contact-damage applications to the SAME enemy while Cat Nip is active (so standing inside an enemy doesn't deal damage 60+ times/sec) -- same cooldown-gating idea as ADeathMetalCatEnemyBase's own ContactDamageCooldown, just tracked per-enemy here via CatNipLastDamageTime since multiple enemies can be touching at once. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items", meta = (ClampMin = "0"))
	float CatNipContactDamageCooldown = 0.5f;

	/** How close (uu, center-to-center) an enemy must be to count as "touching" Cayde for Cat Nip's passive contact damage -- compared against actual capsule-radius sums at runtime instead of a flat guess, see UpdateCatNipContactDamage. Small positive buffer added on top so it reads as a genuine touch, not an exact overlap. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items", meta = (ClampMin = "0"))
	float CatNipContactRadiusBuffer = 10.f;

	/**
	 * Sprite tint applied for the full Cat Nip duration as the simple visual "invincibility is
	 * active" indicator (a Mario-star-style glow, per the design ask) -- reverted to
	 * BaseSpriteTintColor (solo Cayde's own default tint, see its own doc comment below) the
	 * moment Cat Nip ends. Kept well below full brightness (unlike a naive 1.0-channel gold):
	 * confirmed live that Paper2D's sprite material renders SpriteColor through Emissive (expected
	 * for an Unlit shading model, where BaseColor has no lighting to modulate it), so a
	 * full-bright tint blooms out to a solid white blob rather than reading as a color tint at
	 * all. Cut further (0.45/0.38/0.05 -> 0.12/0.10/0.012) after the design ask flagged Super
	 * Cayde as still too bright -- confirmed live that the baked-in flame art's own bloom
	 * plateaus well before this value (0.10 vs 0.06 read as visually identical), so going lower
	 * only darkens Cayde's own body for no further bloom reduction; this is close to the floor of
	 * what SpriteColor alone can fix. Flattened to match BaseSpriteTintColor's own flat 0.02/0.02/0.02
	 * per the design ask, trading the gold hue for consistency with solo Cayde's tint. Placeholder
	 * value, tune freely -- but keep components well under 1.0 if raising it.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items")
	FLinearColor CatNipTintColor = FLinearColor(0.030f, 0.030f, 0.030f, 1.f);

	/**
	 * Default sprite tint for solo Cayde -- applied in BeginPlay and whatever EndCatNip reverts to,
	 * replacing a literal opaque white. Same root cause as CatNipTintColor's own doc comment
	 * (Unlit/Emissive-driven Paper2D material blooming badly once combined with this level's
	 * dynamic auto-exposure): confirmed live (2026-08-25) that solo Cayde at full-bright white was
	 * ALSO blowing out to a near-featureless blob, unrelated to Cat Nip entirely. Fixed here
	 * (per-sprite tint) rather than by touching the camera's exposure/bloom settings, which was
	 * tried first and reverted -- that affected the whole level's rendering, not just the
	 * character. Flat neutral grey (not a color tint) so it dims brightness without shifting hue.
	 * Placeholder value, tune freely -- cut further to 0.02 per the design ask.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items")
	FLinearColor BaseSpriteTintColor = FLinearColor(0.02f, 0.02f, 0.02f, 1.f);

	/**
	 * How much taller Super Cayde's collision capsule is than solo Cayde's own capsule --
	 * BeginCatNip/EndCatNip resize GetCapsuleComponent()'s half-height by this around a fixed
	 * floor-contact point (same "grow/shrink while keeping feet planted" idiom ACharacter's own
	 * Crouch/UnCrouch uses). Matches CatNipIdle/Walk/Attack/Jump's own doubled visual scale (PPU
	 * 0.75 vs regular Idle's 1.5) -- Super Cayde is meant to read as an entirely different, bigger
	 * character while active, not solo Cayde's own capsule with a bigger sprite loosely draped over
	 * it. Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items", meta = (ClampMin = "0.1"))
	float CatNipCapsuleHalfHeightScale = 2.0f;

	/**
	 * How much wider Super Cayde's collision capsule is than solo Cayde's own -- deliberately left
	 * at 1.0 (unchanged), NOT matched to CatNipCapsuleHalfHeightScale/the visual doubling. Originally
	 * scaled both radius and half-height together, but that made the capsule dramatically wider too
	 * (34uu -> 68uu radius), which read live as a phantom "invisible wall" -- Super Cayde was
	 * colliding with Room1's geometry (the far-left building especially) from much further away than
	 * solo Cayde ever did, well before the visual sprite looked like it should be blocked yet. The
	 * vertical sinking bug this capsule resize was fixing was purely a floor-contact (half-height)
	 * problem, so only that axis needs to grow -- widening navigation-relevant horizontal collision
	 * to match the visual isn't worth reintroducing that bug for. Placeholder value, tune freely if
	 * Super Cayde's hitbox ever needs to read as visibly wider too.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items", meta = (ClampMin = "0.1"))
	float CatNipCapsuleRadiusScale = 1.0f;

	/**
	 * How long (seconds) the Cat Nip depletion effect actor lives before self-destructing -- see
	 * SpawnCatNipDepletionEffect. Deliberately shorter than RageBeamLifetime: Fancy Pants' beam
	 * marks an ARRIVAL (the ultimate is just starting, no rush), while this marks Super Cayde's
	 * power finishing running OUT, so a quicker read fits the moment better.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items", meta = (ClampMin = "0"))
	float CatNipDepletionEffectLifetime = 0.6f;

	/**
	 * Tint for the Cat Nip depletion beam's BeamColor material parameter -- matches CatNipTintColor's
	 * own gold hue (just brighter, since this material isn't subject to the Paper2D sprite bloom
	 * issue CatNipTintColor/BaseSpriteTintColor were cut down for -- see their own doc comments) so
	 * the effect reads as "the same power that was glowing him now visibly leaving." Placeholder
	 * value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Items")
	FLinearColor CatNipDepletionBeamColor = FLinearColor(1.0f, 0.75f, 0.1f, 1.f);

private:
	/** Starts the Cat Nip invincibility window: sets bIsCatNipActive, tints the sprite, arms CatNipTimerHandle for CatNipDuration. Called from ResolvePickup. */
	void BeginCatNip();

	/** Timer callback (CatNipTimerHandle, after CatNipDuration): clears bIsCatNipActive and reverts the sprite tint to opaque white. UpdateAnimation naturally falls back off CatNipIdleFlipbook the very next tick once bIsCatNipActive is false. */
	void EndCatNip();

	/**
	 * Spawns the Cat Nip depletion visual: same technique as SpawnRageBeamEffect (a simple scaling
	 * cylinder mesh using M_RainbowBeam, this time held to a fixed gold color instead of hue-cycled)
	 * but inverted -- starts at Cayde's own height and rises/fades OUT over
	 * CatNipDepletionEffectLifetime instead of dropping in from above, reading as "the power that was
	 * surrounding him escapes upward and disperses" rather than "a beam arrives". Called from
	 * EndCatNip so it fires both on the natural CatNipTimerHandle timeout and the death-mid-Cat-Nip
	 * safety net (both paths already run through EndCatNip).
	 */
	void SpawnCatNipDepletionEffect();

	/** Per-tick rise/fade animation for the active Cat Nip depletion effect -- no-ops entirely while bCatNipDepletionBeamActive is false, same pattern as UpdateRageBeamEffect. */
	void UpdateCatNipDepletionEffect(float DeltaSeconds);

	/** Called every Tick while bIsCatNipActive: finds every ADeathMetalCatEnemyBase within (own capsule radius + that enemy's capsule radius + CatNipContactRadiusBuffer) and applies CatNipContactDamage to it, gated per-enemy by CatNipContactDamageCooldown via CatNipLastDamageTime so a single stationary touch doesn't repeat every frame. No-ops entirely while bIsCatNipActive is false. */
	void UpdateCatNipContactDamage();

	/** GetWorld()->GetTimeSeconds() of the last Cat Nip contact-damage hit against each enemy actor, keyed by a weak pointer so a destroyed enemy's stale entry is harmless -- see UpdateCatNipContactDamage/CatNipContactDamageCooldown. Purely a runtime gameplay cache, never touched from editor/Python tooling, so it doesn't carry the CDO-population caveat FlipbookFeetOffsetCorrections' own comment describes. */
	UPROPERTY(Transient)
	TMap<TWeakObjectPtr<AActor>, float> CatNipLastDamageTime;

	FTimerHandle CatNipTimerHandle;

	/** Solo Cayde's own capsule radius, captured once in BeginPlay before Cat Nip can ever resize it -- see CatNipCapsuleScale. */
	float OriginalCapsuleRadius = 0.f;

	/** Solo Cayde's own capsule half-height, captured once in BeginPlay before Cat Nip can ever resize it -- see CatNipCapsuleScale. */
	float OriginalCapsuleHalfHeight = 0.f;

	/** Lazily created the first time Cat Nip ever expires, then reused every time after -- same reasoning as RageBeamMeshComponent (a simple attached mesh, not a separate actor, so it automatically follows Cayde with no extra tracking). See SpawnCatNipDepletionEffect. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> CatNipDepletionBeamMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CatNipDepletionBeamMID;

	/** True for CatNipDepletionEffectLifetime seconds after SpawnCatNipDepletionEffect fires; UpdateCatNipDepletionEffect no-ops entirely while false. */
	bool bCatNipDepletionBeamActive = false;

	float CatNipDepletionBeamStartTime = 0.f;

	/** Avoids calling SetFlipbook every tick when the animation state hasn't changed. */
	UPROPERTY(Transient)
	TObjectPtr<UPaperFlipbook> CurrentFlipbook = nullptr;

	/**
	 * Per-flipbook vertical correction (world units), added on top of the sprite component's own
	 * base relative Z whenever that flipbook is CurrentFlipbook -- see ApplyFeetOffsetCorrection.
	 * Exists because matched overall render SIZE and a shared CENTER_CENTER pivot on every flipbook
	 * do NOT guarantee matched floor contact: CENTER_CENTER finds each frame's own canvas center,
	 * but different sheets have different amounts of empty transparent padding above/below the
	 * character (and different poses extend differently within their canvas -- a standing Idle pose
	 * reaches lower toward its own canvas edge than a tucked airborne pose does toward its own), so
	 * the true pixel distance from "canvas center" to "where the feet actually are" varies between
	 * flipbooks even at identical cell size and pivot mode. Measured directly from each flipbook's
	 * own source art (lowest non-transparent pixel row of a representative frame, converted to world
	 * units via that flipbook's own PixelsPerUnrealUnit) and set once via SetFeetOffsetCorrection,
	 * not guessed or hand-tuned per animation. A flipbook with no entry here (e.g. Idle itself, or
	 * any Fancy Pants flipbook) applies no correction -- falls back to the sprite's plain base
	 * relative Z untouched. Deliberately NOT Transient -- this needs to persist with the Blueprint
	 * across editor restarts, not be rebuilt by re-running a Python script every session.
	 */
	UPROPERTY()
	TMap<TObjectPtr<UPaperFlipbook>, float> FlipbookFeetOffsetCorrections;

	/** Captured once in BeginPlay via GetSprite()->GetRelativeLocation() -- the sprite component's own designed-in offset (e.g. nudging it down slightly from the capsule center) before any per-flipbook feet correction is layered on top. ApplyFeetOffsetCorrection always corrects relative to this captured base, never relative to whatever the Z happened to be last tick, so corrections can never compound. */
	FVector BaseSpriteRelativeLocation = FVector::ZeroVector;

	/** Called once at the end of UpdateAnimation every Tick (after CurrentFlipbook is authoritative for the frame): sets the sprite's relative Z to BaseSpriteRelativeLocation.Z plus whatever correction FlipbookFeetOffsetCorrections has for CurrentFlipbook (0 if none), so every flipbook's feet land at the same world-space contact height regardless of which one is currently showing. */
	void ApplyFeetOffsetCorrection();

	/** Called once from BeginPlay: fills FlipbookFeetOffsetCorrections from literal measured constants keyed off the already-assigned flipbook UPROPERTYs -- see its own doc comment in the .cpp for why this replaced editor/Python-driven CDO population. */
	void PopulateFeetOffsetCorrections();

	/** Backs CanTakeDamage(); cleared independently of bIsDodging by IFrameTimerHandle. */
	bool bIsInvincible = false;

	/** True for HurtDuration seconds after a hit lands; UpdateAnimation checks this first (highest visual priority) so a hit reaction always shows even mid-swing/mid-shot. */
	bool bIsHurt = false;

	FTimerHandle HurtTimerHandle;

	/** True once Health reaches 0; TakeDamage uses this to only log "PLAYER DIED" / call HandleDeath once. Cleared by HandleRespawn. */
	bool bIsDead = false;

	/**
	 * Separate InputComponent pushed onto the player controller's own input stack in HandleDeath,
	 * bound only to EKeys::AnyKey -- deliberately NOT bound on this character's normal
	 * InputComponent (the one DisableInput blocks in HandleDeath), so listening for the continue
	 * press can never accidentally re-enable movement/combat input early. Popped and destroyed in
	 * HandleRespawn.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> DeathContinueInputComponent = nullptr;

	/** Actor transform cached in BeginPlay -- where HandleRespawn puts the character back on death. Mirrors ADeathMetalCatEnemyBase::InitialSpawnTransform. */
	FTransform InitialSpawnTransform;

	FTimerHandle DodgeTimerHandle;
	FTimerHandle IFrameTimerHandle;
	FTimerHandle DodgeFrameTimerHandle;

	/** Cached from the most recent HandleMoveRight call regardless of whether that input was actually allowed to move the character -- DetectWallForSlide reads this to know which direction the player is holding toward. */
	float LastMoveRightAxisValue = 0.f;

	/** Which direction (matches Scale.X's sign convention) the wall currently being slid on is in -- set by UpdateWallSlide; also used to face the sprite into the wall and to know which way is "outward" for a wall jump. */
	float WallSlideFacingSign = 1.f;

	/** True for WallJumpCommitmentDuration seconds after a wall jump; HandleMoveRight scales movement input by WallJumpCommitmentInputScale while true, instead of blocking it entirely. */
	bool bInWallJumpCommitmentWindow = false;

	FTimerHandle WallJumpCommitmentTimerHandle;

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

	/** True while the AimDownAction is physically held -- drives the airborne angled-shot (Gun Fire) and Spinny-Down (Sword Attack) modifiers. */
	bool bIsHoldingDownInput = false;

	/** Which ground-combo stage plays next (0=base swing, 1=sword_2, 2=sword_3) -- see StartSwordComboStage/HandleSwordAttack. Only meaningful for the ground combo; Uppy/Double Whammy/Spinny Down don't touch it. */
	int32 SwordComboIndex = 0;

	/** True during the SwordComboBufferWindow after a ground-combo stage finishes, without a repress yet -- see ClearAttackState/ResetSwordComboWindow. */
	bool bSwordComboWindowOpen = false;

	/** True if Sword Attack was pressed again while still mid-swing on a ground-combo stage -- consumed (and the next stage started) by ClearAttackState the moment the current stage ends. */
	bool bComboInputBuffered = false;

	/** True only while the currently-playing attack is a ground-combo stage (StartSwordComboStage) -- false for Uppy/Double Whammy/Spinny Down, so ClearAttackState knows not to open the combo window or honor a buffered press for those one-off moves. */
	bool bLastAttackWasComboEligible = false;

	/** True only while the currently-playing attack is Uppy -- OnSwordHitboxBeginOverlap checks this to additionally LaunchCharacter the hit enemy upward on top of normal damage. */
	bool bCurrentAttackLaunchesTarget = false;

	/** True only while The Spinny Down Thing is playing -- SwordHitbox sits below Cayde instead of forward for the duration; restored to its normal forward-mirrored position in ClearAttackState. */
	bool bIsSpinnyDownAttackActive = false;

	FTimerHandle SwordComboWindowTimerHandle;

	/** Facing sign (matches Scale.X's sign convention) captured the instant a block starts, so UpdateAnimation can hold the sprite facing that direction for as long as Block is held, same reasoning as DodgeFacingSignAtStart. */
	float BlockFacingSignAtStart = 1.f;

	/** Facing sign (matches Scale.X's sign convention) captured the instant a dash starts, so UpdateAnimation can hold the sprite facing that direction for the whole dash, same reasoning as DodgeFacingSignAtStart. */
	float DashFacingSignAtStart = 1.f;

	FTimerHandle InvulnDashTimerHandle;

	/** True from HandleUltimateActivate until the shrink-to-zero scale-down finishes; UpdateUltimateFadeOut no-ops entirely while false, and is what actually calls BeginUltimateTransformation once the scale hits 0. */
	bool bIsFadingOutForUltimate = false;

	/** GetWorld()->GetTimeSeconds() when the current ultimate fade-out started -- UpdateUltimateFadeOut measures elapsed time from this rather than accumulating DeltaSeconds itself. */
	float UltimateFadeOutStartTime = 0.f;

	FTimerHandle UltimateDurationTimerHandle;

	/** Lazily created the first time the ultimate fires, then reused (moved/rescaled/re-lifetimed) every activation rather than spawned fresh each time -- a simple attached mesh component, not a separate actor, so it automatically follows Cayde's position with no extra tracking logic. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> RageBeamMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RageBeamMID;

	/** True for RageBeamLifetime seconds after SpawnRageBeamEffect fires; UpdateRageBeamEffect no-ops entirely while false. */
	bool bRageBeamActive = false;

	float RageBeamStartTime = 0.f;

	/** Lazily created the first time the Fancy Attack laser fires, then reused (repositioned/rescaled/re-lifetimed) every shot -- same reasoning as RageBeamMeshComponent. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> FancyAttackBeamMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FancyAttackBeamMID;

	/** True for FancyAttackBeamLifetime seconds after SpawnFancyAttackBeam fires; UpdateFancyAttackBeamEffect no-ops entirely while false. */
	bool bFancyAttackBeamActive = false;

	float FancyAttackBeamStartTime = 0.f;

	FTimerHandle ShootTimerHandle;

	/** MoveComp->GravityScale as of BeginPlay, before any air-fire float has ever touched it -- the value ApplyAirFireFloat multiplies down and ClearAirFireFloat restores back to. */
	float DefaultGravityScale = 1.f;

	/** True while the air-fire float window is active; guards ApplyAirFireFloat against re-applying the gravity multiplier on top of itself on repeat shots. */
	bool bAirFireFloatActive = false;

	FTimerHandle AirFireFloatTimerHandle;

	/** TEMP (see TestJumpDistance) -- true from the moment a jump-distance test starts until it detects landing. */
	bool bJumpDistanceTestActive = false;

	/** TEMP -- true once the jump-distance test has actually observed MoveComp->IsFalling(), so a landing is only ever detected AFTER genuinely having left the ground (avoids a false-positive landing on the very first tick, before CharacterMovementComponent has processed the jump yet). */
	bool bJumpDistanceTestHasLeftGround = false;

	/** TEMP -- actor X at the moment the current jump-distance test started. */
	float JumpDistanceTestStartX = 0.f;

	/** TEMP -- "Running" or "Standing", echoed back in the landing log line. */
	FString JumpDistanceTestLabel;

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

	/** GetWorld()->GetTimeSeconds() at the moment the most recently shown quip fired, regardless of its trigger type -- the GlobalQuipDebounce sentinel. Never updated by a suppressed/failed TriggerQuip attempt. */
	float LastQuipShownTime = -1000.f;

	/** Per-type last-fired timestamps for the QuipCooldown_Kill/Damage/Environment cooldowns -- fully independent of each other and of LastQuipShownTime above. */
	float LastKillQuipTime = -1000.f;
	float LastDamageQuipTime = -1000.f;
	float LastEnvironmentQuipTime = -1000.f;

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
