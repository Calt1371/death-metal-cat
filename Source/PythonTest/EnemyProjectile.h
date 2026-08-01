#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class UMaterialInstanceDynamic;

/**
 * Small non-homing traveling projectile fired by ADeathMetalCatEnemyBase's ranged-attack burst
 * (see BeginRangedAttack/FireOneShot there). Deliberately NOT hitscan -- moves in a straight line
 * at a fixed speed via UProjectileMovementComponent (no re-aiming after spawn), so unlike the
 * player's own instant-hitscan gun this is a real dodgeable threat with visible travel time.
 *
 * Placeholder visual: a small StaticMeshComponent sphere with a dynamic-material Color parameter,
 * same convention ADeathMetalCatEnemyBase::PlaceholderMesh already uses for its own placeholder
 * (reuses the same M_EnemyPlaceholder material rather than inventing a new one), not a Paper2D
 * sprite -- swap for real art later by changing the mesh/material on this class or a Blueprint
 * subclass of it.
 *
 * Collision is overlap-only against the player specifically (checked by class, matching the
 * OtherActor-type-check convention used elsewhere in this project -- RoomExitTrigger,
 * EncounterSpawnMarker) -- deals ProjectileDamage via the same ApplyDamage path contact damage
 * uses, then destroys itself. Also self-destructs after Lifetime seconds if it never hits the
 * player, which (at a constant Speed) doubles as an effective max-range cap without needing a
 * separate distance-tracking mechanism.
 */
UCLASS()
class PYTHONTEST_API AEnemyProjectile : public AActor
{
	GENERATED_BODY()

public:
	AEnemyProjectile();

	/**
	 * Sets this projectile in motion. Must be called once, right after SpawnActor, before this
	 * projectile does anything else -- Direction is normalized internally. InstigatorController
	 * and DamageCauserActor are passed straight through to ApplyDamage's EventInstigator/
	 * DamageCauser params, same as ADeathMetalCatEnemyBase's own contact damage call
	 * (GetController() and `this` respectively) -- passed in explicitly rather than re-derived
	 * here, since AActor has no generic "get owning pawn's controller" the way a Pawn does.
	 */
	void InitializeProjectile(const FVector& Direction, float InSpeed, float InDamage, float InLifetime, AController* InstigatorController, AActor* DamageCauserActor);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Projectile", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> CollisionComp;

	UPROPERTY(VisibleAnywhere, Category = "Projectile", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> ProjectileMesh;

	UPROPERTY(VisibleAnywhere, Category = "Projectile", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	/** Same placeholder material convention as ADeathMetalCatEnemyBase::PlaceholderMaterial -- must expose a "Color" vector parameter. Left unset, this mesh just renders with the engine default material instead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<UMaterialInterface> ProjectileMaterial;

	/** Tint applied via the dynamic material instance -- bright/saturated by design so a real dodgeable threat reads clearly against the scene. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual")
	FLinearColor ProjectileColor = FLinearColor(1.0f, 0.35f, 0.05f); // bright orange

	UFUNCTION()
	void OnCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

private:
	void DestroyProjectile();

	float Damage = 8.f;

	UPROPERTY(Transient)
	TObjectPtr<AController> DamageInstigatorController;

	UPROPERTY(Transient)
	TObjectPtr<AActor> DamageCauser;

	FTimerHandle LifetimeTimerHandle;
};
