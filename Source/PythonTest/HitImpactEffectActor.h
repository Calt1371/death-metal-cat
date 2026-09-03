#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DamageTypes.h"
#include "HitImpactEffectActor.generated.h"

class UWidgetComponent;
class UHitImpactWidget;

/**
 * Short-lived impact-burst effect: a radial spark flash that grows and fades out over Lifetime,
 * then destroys itself. Spawned at the hit location from
 * ADeathMetalCatCharacter::OnSwordHitboxBeginOverlap on every landed sword hit -- the visible
 * "you just hit something hard" feedback the sword swings were missing.
 *
 * Architecturally a near-exact copy of ADamageNumberActor: same Screen Space (not World Space)
 * UWidgetComponent, for the same reason -- it sidesteps needing to orient a world-space widget
 * plane to face this project's camera, since screen space just projects this actor's world location
 * to a 2D overlay regardless of camera orientation. The only real difference is what the widget
 * shows (a burst of sparks instead of a number) and that the actor's Tick drives a scale as well as
 * an opacity.
 */
UCLASS()
class PYTHONTEST_API AHitImpactEffectActor : public AActor
{
	GENERATED_BODY()

public:
	AHitImpactEffectActor();

	virtual void Tick(float DeltaSeconds) override;

	/** Sets the burst's tier color and starts its grow/fade lifetime. Call immediately after spawning, at the hit location. */
	void InitHitImpact(EDamageTier Tier);

	/** How long the burst is visible before destroying itself, in seconds. Deliberately quick -- this needs to read as a snap of impact, not a lingering effect. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, Category = "Hit Impact", meta = (ClampMin = "0.01"))
	float Lifetime = 0.22f;

private:
	UPROPERTY(VisibleAnywhere, Category = "Hit Impact", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWidgetComponent> WidgetComp;

	/** Fetched from WidgetComp->GetUserWidgetObject() the first time it's needed -- same reasoning as ADamageNumberActor's own CachedWidget (WidgetComponent's WidgetClass instance isn't guaranteed constructed yet at this actor's own construction time). */
	UPROPERTY(Transient)
	TObjectPtr<UHitImpactWidget> CachedWidget;

	float ElapsedTime = 0.f;
};
