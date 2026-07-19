#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DamageTypes.h"
#include "DamageNumberActor.generated.h"

class UWidgetComponent;
class UDamageNumberWidget;

/**
 * Short-lived floating combat-text actor: shows a single number color-coded by EDamageTier,
 * rises a short distance and fades out over Lifetime seconds, then destroys itself.
 *
 * Spawned directly from the damage-dealing call sites (sword overlap, gun hitscan) rather than
 * from inside TakeDamage -- keeps this decoupled from exactly which actor's TakeDamage fired,
 * and avoids needing to smuggle EDamageTier through the engine's generic FDamageEvent/UDamageType
 * plumbing just to recover it on the receiving end.
 *
 * Uses a Screen Space (not World Space) UWidgetComponent specifically to sidestep needing to
 * orient a world-space widget plane to face this project's camera: Screen space just projects
 * this actor's world location to a 2D screen overlay, correct regardless of camera orientation.
 */
UCLASS()
class PYTHONTEST_API ADamageNumberActor : public AActor
{
	GENERATED_BODY()

public:
	ADamageNumberActor();

	virtual void Tick(float DeltaSeconds) override;

	/** Sets the displayed text/color for DamageAmount's rolled Tier and starts the rise/fade lifetime. Call immediately after spawning. */
	void InitDamageNumber(float DamageAmount, EDamageTier Tier);

	/** How far (world units) the number rises over its lifetime. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, Category = "Damage Number", meta = (ClampMin = "0"))
	float RiseDistance = 60.f;

	/** How long the number is visible before destroying itself, in seconds. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, Category = "Damage Number", meta = (ClampMin = "0.01"))
	float Lifetime = 1.f;

private:
	UPROPERTY(VisibleAnywhere, Category = "Damage Number", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWidgetComponent> WidgetComp;

	/** Fetched from WidgetComp->GetUserWidgetObject() the first time it's needed (InitDamageNumber), not cached from the constructor -- WidgetComponent constructs its WidgetClass instance during registration, which isn't guaranteed complete yet at construction time. */
	UPROPERTY(Transient)
	TObjectPtr<UDamageNumberWidget> CachedWidget;

	FVector StartLocation = FVector::ZeroVector;
	float ElapsedTime = 0.f;
};
