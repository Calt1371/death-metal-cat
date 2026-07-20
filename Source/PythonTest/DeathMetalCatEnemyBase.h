#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "DeathMetalCatEnemyBase.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

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

protected:
	virtual void BeginPlay() override;

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
};
