#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BiomeEndMarker.generated.h"

class UBoxComponent;
class UPrimitiveComponent;
struct FHitResult;

/**
 * Placeholder "end of biome" marker for Room8, which has no boss yet. Deliberately NOT an
 * ARoomExitTrigger -- there is no Room9 to advance to, so this just logs/announces completion
 * once (bHasFired guard, same as ARoomExitTrigger) rather than reusing NextRoomID-based
 * progression logic for a case that isn't progression at all. Replace with a real boss encounter
 * trigger later; this stretch-goal placeholder only proves the biome's far end is reachable.
 */
UCLASS()
class PYTHONTEST_API ABiomeEndMarker : public AActor
{
	GENERATED_BODY()

public:
	ABiomeEndMarker();

private:
	UPROPERTY(VisibleAnywhere, Category = "Room Progression")
	TObjectPtr<UBoxComponent> TriggerVolume;

	bool bHasFired = false;

	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
