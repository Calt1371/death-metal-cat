#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "DeathMetalCatEnemyBase.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class ADeathMetalCatCharacter;
class UPaperFlipbookComponent;
class UPaperFlipbook;
class AEnemyProjectile;

/** Internal-only (not exposed to Blueprint) phase tracking for the ranged-attack burst sequence -- same pattern as ADeathMetalCatCharacter's own EShootPhase. */
enum class EEnemyShootPhase : uint8
{
	None,
	Drawing,
	Firing,
};

/**
 * Base enemy class: Character-derived (capsule collision + movement component, for whatever
 * future AI/pathfinding work needs them) but with no skeletal mesh or animation -- real enemy
 * art is a separate future task. Stands in for now with a plain colored placeholder mesh that
 * flashes color on hit.
 *
 * At BeginPlay, this actor snaps its own Y/Z onto the player character's exact Y/Z (fixed for the
 * whole session by the player's plane-constrained movement) before caching that as its spawn
 * transform -- corrects for placement drift from eyeballing depth in the 3D viewport, so any
 * enemy dragged in roughly the right spot lands exactly on the gameplay plane regardless. This
 * fixes large-scale placement drift as a category, distinct from (and in addition to) the gun's
 * own small-radius sphere-trace aim tolerance, which only covers minor misalignment.
 *
 * At 0 health, rather than being destroyed outright, the actor hides itself, disables its own
 * collision, and auto-respawns at its cached (plane-corrected) spawn location after RespawnDelay
 * -- a testing convenience so the same enemy can be repeatedly killed (for XP/GnarlyRank/damage
 * testing) without manually re-placing one in the level each time. This is purely a visual/state
 * reset: the death/XP-awarding logic in TakeDamage runs exactly once per kill regardless,
 * unaffected by however many times the enemy subsequently respawns.
 *
 * Minimal contact-attack behavior, deliberately no AI/pathfinding system: every Tick, if the
 * player is within DetectionRadius (a plain 3D distance check, not vision/line-of-sight), this
 * actor calls AddMovementInput straight toward the player's X position (the only axis that
 * matters -- Y/Z are already locked to the player's plane, see the BeginPlay snap above) until
 * within ShootRange, then stops (see the three concentric bands below). Contact damage is gated
 * purely on plain 3D distance to the player each Tick (distance <= MeleeRange), combined with
 * ContactDamageCooldown -- deliberately NOT overlap-event-driven: the enemy and player capsules
 * both use the stock "Pawn" collision profile, which is a Block/Block relationship (see
 * UCharacterMovementComponent default pawn collision), so they can never actually generate a
 * BeginOverlap/EndOverlap against each other in the first place -- an earlier revision of this
 * class bound contact damage to the capsule's own OnComponentBeginOverlap/EndOverlap, which in
 * practice only ever fired from the player's SwordHitbox (a separate, deliberately
 * overlap-everything component) grazing the capsule mid-swing, misidentified as "player contact"
 * since the check only compared OtherActor, not OtherComp. Distance-based gating avoids that
 * whole class of collision-response mismatch. Damage applies via the same
 * UGameplayStatics::ApplyDamage path everything else in this game uses; the player's own
 * TakeDamage handles CanTakeDamage()/i-frames entirely, this class never checks or duplicates
 * that.
 *
 * Three concentric distance bands (MeleeRange < ShootRange < DetectionRadius), checked every Tick:
 *   0..MeleeRange           -- contact damage (as above), stopped, no ranged attack.
 *   MeleeRange..ShootRange  -- stopped, plays the ranged-attack burst (see BeginRangedAttack).
 *   ShootRange..DetectionRadius -- advances toward the player (the old single-band chase).
 * Facing (flipbook's Scale.X sign) is updated whenever the player is within DetectionRadius,
 * regardless of which band -- same convention ADeathMetalCatCharacter's own UpdateAnimation uses
 * for its sprite (Scale.X >= 0 faces +X/right, < 0 faces -X/left), so the enemy always faces
 * whichever direction the player currently is, not just while physically advancing.
 *
 * Ranged attack (BeginRangedAttack/BeginBurstLoop/FireOneShot): mirrors
 * ADeathMetalCatCharacter's own Drawing/HoldFiring hold-fire architecture. A single-frame windup
 * (ShootDrawFlipbook) plays once for ShootDrawDuration, then the loop flipbook (ShootLoopFlipbook)
 * takes over and fires one projectile per loop frame shown (a fixed-count burst, not continuous
 * fire), after which ShootBurstCooldown gates the next burst attempt. Projectiles are real
 * traveling actors (AEnemyProjectile, UProjectileMovementComponent-driven, non-homing, aimed at
 * the player's position at the instant each shot fires), not hitscan -- deliberately dodgeable,
 * unlike the player's own instant-hitscan gun.
 */
UCLASS()
class PYTHONTEST_API ADeathMetalCatEnemyBase : public ACharacter
{
	GENERATED_BODY()

public:
	ADeathMetalCatEnemyBase();

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0"))
	float MaxHealth = 100.f;

	UPROPERTY(BlueprintReadOnly, Category = "Health")
	float Health = 100.f;

	/** XP awarded to whoever lands the killing blow (TakeDamage's DamageCauser, cast to ADeathMetalCatCharacter) when this enemy dies. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0"))
	float XPReward = 10.f;

	/** How long (seconds) the enemy stays hidden/dead before auto-respawning at its cached spawn location. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0"))
	float RespawnDelay = 3.f;

	/** Plain 3D distance (uu) within which this enemy notices the player and starts advancing -- not vision/line-of-sight, just a radius check. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float DetectionRadius = 800.f;

	/** Straight-line advance speed (uu/s) toward the player once detected -- drives CharacterMovementComponent::MaxWalkSpeed; no pathfinding, just AddMovementInput along X (and Z too, if bFliesFreely). Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float MoveSpeed = 200.f;

	/**
	 * Per-subclass opt-in for free vertical movement -- same pattern as bSpriteFacesReversed.
	 * False (default) is the normal ground-enemy behavior: MOVE_Walking, full gravity, chase
	 * logic advances along X only, exactly as it already did before this property existed.
	 * True switches BeginPlay to MOVE_Flying with zero gravity, and Tick's chase logic additionally
	 * computes Z distance to the player and moves toward it -- still plain direct-approach movement
	 * (no pathfinding/obstacle avoidance), just on two axes instead of one. Set true only on
	 * BP_EnemyDeathBotFlying; ground enemies (BP_EnemyDeathBotWalking, BP_EnemyBase) must stay at
	 * this default so their movement is byte-for-byte unchanged.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat")
	bool bFliesFreely = false;

	/**
	 * Plain 3D distance (uu) within which the enemy stops to deal contact damage instead of
	 * advancing or ranged-attacking. Must stay smaller than ShootRange -- see the class comment's
	 * three-band breakdown. Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float MeleeRange = 100.f;

	/** Damage applied to the player, via the existing ApplyDamage/TakeDamage path, whenever plain 3D distance to the player is within MeleeRange (gated by ContactDamageCooldown). The player's own CanTakeDamage()/i-frame check is not duplicated here. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float ContactDamage = 10.f;

	/** Minimum seconds between contact-damage applications while continuously within MeleeRange of the player. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float ContactDamageCooldown = 1.0f;

	/**
	 * Plain 3D distance (uu) within which the enemy stops advancing and starts the ranged-attack
	 * burst instead, as long as the player is also outside MeleeRange (see the class comment's
	 * three-band breakdown). Must stay larger than MeleeRange and smaller than or equal to
	 * DetectionRadius. Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Ranged Attack", meta = (ClampMin = "0"))
	float ShootRange = 500.f;

	/** Speed (uu/s) each spawned projectile travels in a straight line -- not homing, aimed at the player's position at the instant it's fired. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Ranged Attack", meta = (ClampMin = "0"))
	float ProjectileSpeed = 800.f;

	/** Damage a projectile deals on hitting the player, via the same ApplyDamage path as contact damage. Halved from the original 8 -- enemy ranged damage was landing too hard. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Ranged Attack", meta = (ClampMin = "0"))
	float ProjectileDamage = 4.f;

	/** Seconds a projectile survives before self-destructing if it never hits the player -- at a constant ProjectileSpeed this also caps its effective max range. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Ranged Attack", meta = (ClampMin = "0"))
	float ProjectileLifetime = 2.0f;

	/** Seconds of windup (ShootDrawFlipbook shown, not yet firing) before the burst loop starts. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Ranged Attack", meta = (ClampMin = "0"))
	float ShootDrawDuration = 0.15f;

	/** Minimum seconds after a burst ends before the enemy can begin another one. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Ranged Attack", meta = (ClampMin = "0"))
	float ShootBurstCooldown = 2.0f;

	/** Actor class spawned by each shot in a burst -- defaults to the plain AEnemyProjectile C++ class (a small placeholder-colored sphere) in the constructor; override per-subclass if a Blueprint variant with real art is made later. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Ranged Attack")
	TSubclassOf<AEnemyProjectile> ProjectileClass;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Timer callback: reverts the placeholder mesh's color from HitFlashColor back to BaseColor. */
	void ClearHitFlash();

	/**
	 * Called once, from TakeDamage, the moment Health reaches 0 (after XP has already been
	 * awarded). Disables collision immediately (no more hits/contact damage while dying), force-
	 * resets the hit-flash color (canceling its own pending clear-timer rather than leaving it
	 * mid-transition into the death sequence), and starts the death-blink toggle instead of hiding
	 * outright -- see TickDeathBlink.
	 */
	void HandleDeath();

	/**
	 * Timer callback, repeating every DeathBlinkInterval: toggles visibility on/off (a "blink"
	 * effect) DeathBlinkCount times, then hides for good and arms the respawn timer. Runs after
	 * collision is already disabled (see HandleDeath), so the blinking corpse can't be hit again or
	 * still deal contact damage.
	 */
	void TickDeathBlink();

	/** Timer callback from HandleDeath: resets Health to MaxHealth, re-enables collision/visibility, and restores the actor to InitialSpawnTransform. */
	void HandleRespawn();

	/** Starts a ranged-attack burst: sets ShootPhase to Drawing, shows ShootDrawFlipbook (or skips straight to BeginBurstLoop if unset), arms a ShootDrawDuration timer. Called from Tick once the player enters the ShootRange band with the burst cooldown elapsed. */
	void BeginRangedAttack();

	/** Timer callback from BeginRangedAttack: sets ShootPhase to Firing, shows ShootLoopFlipbook looping, resets ShotsFiredInBurst, and fires the first shot immediately before arming the repeating per-shot timer. */
	void BeginBurstLoop();

	/** Repeating timer callback during Firing (interval = 1 / ShootLoopFlipbook's own frame rate, so each shot lines up with a flash frame): spawns one AEnemyProjectile aimed at the player's current position, increments ShotsFiredInBurst, and ends the burst (clearing the timer, ShootPhase back to None, arming ShootBurstCooldown) once the loop flipbook's frame count has been matched. */
	void FireOneShot();

	/** Placeholder visual (an engine basic-shape mesh) standing in for real enemy art. Scaled to roughly fill the default ACharacter capsule; retune if capsule size changes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> PlaceholderMesh;

	/** Material assigned to PlaceholderMesh; must expose a "Color" vector parameter (see M_EnemyPlaceholder) for the hit-flash and BaseColor to have any visible effect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<UMaterialInterface> PlaceholderMaterial;

	/** Resting (non-flashed) tint of the placeholder mesh. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual")
	FLinearColor BaseColor = FLinearColor(0.6f, 0.1f, 0.1f); // dull red, reads as "hostile" at rest

	/** Color the placeholder mesh flashes to for HitFlashDuration after taking damage. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual")
	FLinearColor HitFlashColor = FLinearColor::White;

	/** How long the hit-flash color lasts, in seconds. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual", meta = (ClampMin = "0"))
	float HitFlashDuration = 0.15f;

	/**
	 * Per-subclass override for a sprite sheet whose art was authored facing -X (left) by
	 * default rather than +X (right) -- Tick's facing-toward-player logic assumes unmirrored
	 * (Scale.X positive) art faces +X, same convention ADeathMetalCatCharacter's own sprite
	 * uses; confirmed via live PIE testing that BP_EnemyDeathBotWalking's art is authored the
	 * opposite way (it visibly faced away from the player with the "normal" sign mapping,
	 * while the underlying XDistance-to-Scale.X calculation itself was verified correct).
	 * Defaults false (matches the normal/Cayde convention) so this must be explicitly opted
	 * into per-Blueprint rather than assumed uniform across enemy types -- DeathBotFlying's own
	 * art convention was NOT confirmed either way (its sprite has no clear front/back tell to
	 * verify from a screenshot), so it deliberately stays at this default rather than being
	 * lumped in with DeathBotWalking's confirmed reversal.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual")
	bool bSpriteFacesReversed = false;

	/** How many on/off visibility toggles the death-blink does before disappearing for good. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual", meta = (ClampMin = "0"))
	int32 DeathBlinkCount = 6;

	/** Seconds between each death-blink visibility toggle. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual", meta = (ClampMin = "0"))
	float DeathBlinkInterval = 0.08f;

	/**
	 * Flipbook shown while not advancing on or attacking the player. Optional -- subclasses with no
	 * real art yet (or no visible difference between standing still and moving, e.g. a hovering
	 * flyer) can leave this and the two below unset; Tick's flipbook switching only ever touches
	 * whatever PaperFlipbookComponent it finds on the actor (native or Blueprint-added, doesn't
	 * matter -- found generically via FindComponentByClass) and no-ops entirely if none is present,
	 * so this is purely additive and doesn't affect PlaceholderMesh-only subclasses at all.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<UPaperFlipbook> IdleFlipbook = nullptr;

	/** Flipbook shown while actively advancing toward the player (see the DetectionRadius/MeleeRange chase logic in Tick). Optional -- falls back to whatever's currently playing if unset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<UPaperFlipbook> WalkFlipbook = nullptr;

	/** Flipbook shown while within MeleeRange dealing contact damage (see the three-band breakdown in the class comment). Optional -- unset by default so existing melee-range behavior on subclasses with no dedicated attack art (BP_EnemyDeathBotWalking, BP_EnemyDeathBotFlying, both of which attack at range instead) is unchanged; falls back to Idle/Walk selection same as before. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<UPaperFlipbook> AttackFlipbook = nullptr;

	/** Single-frame windup pose shown once at the start of a ranged-attack burst, before the loop takes over. Optional -- if unset, BeginRangedAttack skips straight to the loop with no windup delay. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<UPaperFlipbook> ShootDrawFlipbook = nullptr;

	/** Looping muzzle-flash animation shown for the sustained part of a ranged-attack burst -- one projectile fires per loop frame shown. Optional -- falls back to whatever's currently playing if unset (the burst still fires normally, just without a matching animation). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<UPaperFlipbook> ShootLoopFlipbook = nullptr;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

	FTimerHandle HitFlashTimerHandle;
	FTimerHandle RespawnTimerHandle;
	FTimerHandle DeathBlinkTimerHandle;

	/** Remaining toggles left in the current death-blink sequence; counts down to 0 in TickDeathBlink. */
	int32 RemainingDeathBlinks = 0;

	/** Actor transform cached in BeginPlay -- where HandleRespawn puts the actor back after RespawnDelay. */
	FTransform InitialSpawnTransform;

	/** True from the moment Health reaches 0 (TakeDamage) until HandleRespawn resets it. Guards against TakeDamage running again (and double-awarding XP) during the hidden/respawn window -- collision is disabled then, so this shouldn't normally be reachable, but it's a cheap, explicit guarantee rather than relying solely on that. */
	bool bIsDead = false;

	/** Cached once in BeginPlay via UGameplayStatics::GetPlayerCharacter -- same lookup already used there for the plane-snap, reused here for detection/chase/contact-damage each Tick instead of querying every frame. A plain UPROPERTY pointer, so it's automatically nulled by GC if the player is ever destroyed. */
	UPROPERTY(Transient)
	TObjectPtr<ADeathMetalCatCharacter> CachedPlayerCharacter;

	/** World time (GetWorld()->GetTimeSeconds()) contact damage was last applied; compared against ContactDamageCooldown. Starts far enough negative that the very first contact always damages immediately; reset to that same sentinel in HandleDeath so a respawned enemy doesn't inherit a stale cooldown from its previous life. */
	float LastContactDamageTime = -1000.f;

	/**
	 * Resolved lazily in Tick via FindComponentByClass -- finds whatever PaperFlipbookComponent
	 * exists on this actor regardless of whether it's a native C++ member or added via a Blueprint
	 * subclass's construction script (both show up identically at the instance level). Stays null
	 * (and the flipbook-switching block in Tick just no-ops) for subclasses with no such component,
	 * e.g. the original placeholder-cylinder-only BP_EnemyBase.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UPaperFlipbookComponent> CachedFlipbookComponent;

	EEnemyShootPhase ShootPhase = EEnemyShootPhase::None;

	/** Shots fired so far in the current burst; compared against ShootLoopFlipbook's own frame count in FireOneShot to know when the burst ends. */
	int32 ShotsFiredInBurst = 0;

	FTimerHandle ShootDrawTimerHandle;
	FTimerHandle ShootBurstTimerHandle;

	/** World time (GetWorld()->GetTimeSeconds()) the last burst ended; compared against ShootBurstCooldown, same sentinel-start/reset-on-death convention as LastContactDamageTime. */
	float LastBurstEndTime = -1000.f;
};
