#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "DeathMetalCatEnemyBase.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class ADeathMetalCatCharacter;

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
 * within MeleeRange, then stops. Contact damage is gated purely on plain 3D distance to the
 * player each Tick (distance <= MeleeRange), combined with ContactDamageCooldown -- deliberately
 * NOT overlap-event-driven: the enemy and player capsules both use the stock "Pawn" collision
 * profile, which is a Block/Block relationship (see UCharacterMovementComponent default pawn
 * collision), so they can never actually generate a BeginOverlap/EndOverlap against each other in
 * the first place -- an earlier revision of this class bound contact damage to the capsule's own
 * OnComponentBeginOverlap/EndOverlap, which in practice only ever fired from the player's
 * SwordHitbox (a separate, deliberately overlap-everything component) grazing the capsule
 * mid-swing, misidentified as "player contact" since the check only compared OtherActor, not
 * OtherComp. Distance-based gating avoids that whole class of collision-response mismatch.
 * Damage applies via the same UGameplayStatics::ApplyDamage path everything else in this game
 * uses; the player's own TakeDamage handles CanTakeDamage()/i-frames entirely, this class never
 * checks or duplicates that. No attack animation in this pass -- contact is the whole attack,
 * animation is a separate future task.
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

	/** Straight-line advance speed (uu/s) toward the player once detected -- drives CharacterMovementComponent::MaxWalkSpeed; no pathfinding, just AddMovementInput along X. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float MoveSpeed = 200.f;

	/**
	 * Dual-purpose range (uu), checked every Tick: (1) the X-distance at which the enemy stops
	 * advancing, so it doesn't overshoot past the player, and (2) the plain 3D distance within
	 * which contact damage is allowed to apply (gated by ContactDamageCooldown). Deliberately the
	 * same property for both -- "close enough to stop chasing" and "close enough to hit" are the
	 * same concept for this minimal contact-attack enemy. Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float MeleeRange = 100.f;

	/** Damage applied to the player, via the existing ApplyDamage/TakeDamage path, whenever plain 3D distance to the player is within MeleeRange (gated by ContactDamageCooldown). The player's own CanTakeDamage()/i-frame check is not duplicated here. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float ContactDamage = 10.f;

	/** Minimum seconds between contact-damage applications while continuously within MeleeRange of the player. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float ContactDamageCooldown = 1.0f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Timer callback: reverts the placeholder mesh's color from HitFlashColor back to BaseColor. */
	void ClearHitFlash();

	/**
	 * Called once, from TakeDamage, the moment Health reaches 0 (after XP has already been
	 * awarded). Hides the actor, disables its collision, force-resets the hit-flash color
	 * (canceling its own pending clear-timer rather than leaving it mid-transition into the
	 * hidden/respawn window), and arms the respawn timer.
	 */
	void HandleDeath();

	/** Timer callback from HandleDeath: resets Health to MaxHealth, re-enables collision/visibility, and restores the actor to InitialSpawnTransform. */
	void HandleRespawn();

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

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

	FTimerHandle HitFlashTimerHandle;
	FTimerHandle RespawnTimerHandle;

	/** Actor transform cached in BeginPlay -- where HandleRespawn puts the actor back after RespawnDelay. */
	FTransform InitialSpawnTransform;

	/** True from the moment Health reaches 0 (TakeDamage) until HandleRespawn resets it. Guards against TakeDamage running again (and double-awarding XP) during the hidden/respawn window -- collision is disabled then, so this shouldn't normally be reachable, but it's a cheap, explicit guarantee rather than relying solely on that. */
	bool bIsDead = false;

	/** Cached once in BeginPlay via UGameplayStatics::GetPlayerCharacter -- same lookup already used there for the plane-snap, reused here for detection/chase/contact-damage each Tick instead of querying every frame. A plain UPROPERTY pointer, so it's automatically nulled by GC if the player is ever destroyed. */
	UPROPERTY(Transient)
	TObjectPtr<ADeathMetalCatCharacter> CachedPlayerCharacter;

	/** World time (GetWorld()->GetTimeSeconds()) contact damage was last applied; compared against ContactDamageCooldown. Starts far enough negative that the very first contact always damages immediately; reset to that same sentinel in HandleDeath so a respawned enemy doesn't inherit a stale cooldown from its previous life. */
	float LastContactDamageTime = -1000.f;
};
