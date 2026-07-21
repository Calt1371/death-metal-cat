#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomTypes.h"
#include "RoomExitTrigger.generated.h"

class UBoxComponent;
class UPrimitiveComponent;
struct FHitResult;

/**
 * Placed at a room's exit doorway. The first time the player character overlaps this volume, it
 * fires ARoomProgressionManager::AdvanceToRoom(NextRoomID) and then never fires again (bHasFired
 * guard) -- redundant with NextRoomID's own room being deactivated behind the player anyway once
 * they've moved on, but cheap insurance against a same-frame double-overlap.
 *
 * Room3's branch choice is just two of these placed side by side (or stacked as an upper/lower
 * doorway) with NextRoomID set to Room4A on one and Room4B on the other -- whichever the player
 * physically walks through fires, and since only that branch's ARoomShell ever gets activated,
 * the other branch's own exit trigger is never reachable again.
 */
UCLASS()
class PYTHONTEST_API ARoomExitTrigger : public AActor
{
	GENERATED_BODY()

public:
	ARoomExitTrigger();

	/** Which room this exit leads to. Set two of these to Room4A/Room4B on Room3's pair of exits to implement the branch choice. */
	UPROPERTY(EditAnywhere, Category = "Room Progression")
	ERoomID NextRoomID = ERoomID::Room2;

private:
	UPROPERTY(VisibleAnywhere, Category = "Room Progression")
	TObjectPtr<UBoxComponent> TriggerVolume;

	/** True after this trigger has fired once; ignores every overlap after the first. */
	bool bHasFired = false;

	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
