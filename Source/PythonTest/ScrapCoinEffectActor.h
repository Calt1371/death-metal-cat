#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ScrapCoinEffectActor.generated.h"

class UWidgetComponent;
class UScrapCoinWidget;

/**
 * Short-lived "coin flying toward Cayde" feedback actor for the Scraps-received effect (see
 * ADeathMetalCatCharacter::SpawnScrapCoinEffect) -- mirrors ADamageNumberActor's own architecture
 * closely (same Screen Space UWidgetComponent trick to sidestep needing to orient a world-space
 * plane to face this project's camera, same Tick-driven lerp + fade + self-destruct lifecycle),
 * just converging on a moving target actor instead of rising a fixed distance.
 *
 * Spawned at a small randomized offset around Cayde, then lerps its own world location toward
 * TargetActor's current location (re-read every Tick, not a captured snapshot, so it still
 * converges correctly if Cayde moves during the brief animation) while fading out, and destroys
 * itself once Lifetime elapses.
 */
UCLASS()
class PYTHONTEST_API AScrapCoinEffectActor : public AActor
{
	GENERATED_BODY()

public:
	AScrapCoinEffectActor();

	virtual void Tick(float DeltaSeconds) override;

	/** Sets the actor this coin flies toward and starts its short lifetime. Call immediately after spawning, at the coin's own randomized start position. */
	void InitScrapCoin(AActor* InTargetActor);

	/** How long (seconds) the coin takes to fly in and fully fade out before self-destructing. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, Category = "Scrap Coin", meta = (ClampMin = "0.01"))
	float Lifetime = 0.4f;

private:
	UPROPERTY(VisibleAnywhere, Category = "Scrap Coin", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWidgetComponent> WidgetComp;

	/** Fetched from WidgetComp->GetUserWidgetObject() the first time it's needed -- same reasoning as ADamageNumberActor's own CachedWidget (WidgetComponent's WidgetClass instance isn't guaranteed constructed yet at this actor's own construction time). */
	UPROPERTY(Transient)
	TObjectPtr<UScrapCoinWidget> CachedWidget;

	UPROPERTY(Transient)
	TObjectPtr<AActor> TargetActor;

	FVector StartLocation = FVector::ZeroVector;
	float ElapsedTime = 0.f;
};
