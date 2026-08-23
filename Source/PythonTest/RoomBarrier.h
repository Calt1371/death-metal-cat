#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomBarrier.generated.h"

class UBoxComponent;
class UPaperFlipbookComponent;
class UPaperFlipbook;
class ARoomShell;

/**
 * Placed at a room's exit doorway, attached to that room's RoomShell (same "everything attaches to
 * its room" convention RoomShell's own doc comment describes for floors/walls/RoomExitTrigger/
 * EncounterSpawnMarker). Physically blocks the player with a real collision volume while the room
 * isn't cleared (ARoomShell::IsRoomCleared) AND the DMC.SetRoomBarrierEnabled console variable is
 * on; ARoomExitTrigger separately refuses to fire the room transition under that same condition
 * (see its own OnTriggerBeginOverlap), so the player can never advance past a locked door even if
 * they somehow squeeze past this actor's collision -- belt and suspenders, not load-bearing on its
 * own.
 *
 * DMC.SetRoomBarrierEnabled defaults to false (off): with it off, both this actor's blocking
 * collision and ARoomExitTrigger's gate are unconditionally bypassed, so rooms play exactly as they
 * did before this system existed -- deliberately, so testing/iterating on a room doesn't require
 * clearing every enemy first. Toggle live in PIE with the console command
 * "DMC.SetRoomBarrierEnabled 1" (or "0"); enemy registration/tracking (RoomShell::RegisterEnemy/
 * NotifyEnemyDefeated) runs unconditionally regardless of this variable, only the actual blocking
 * is gated by it.
 *
 * Visual: FB_Trap_RoomBarrier is a 25-frame flipbook of the barrier's ambient "active" crackle loop
 * -- confirmed via visual review there's no distinct baked-in "opening" animation (the barrier's
 * silhouette and skull emblem stay in the same place across all 25 frames, only the lightning/glow
 * flickers), so "opening" here means hiding the actor and dropping its collision, not animating
 * forward through the sheet.
 */
UCLASS()
class PYTHONTEST_API ARoomBarrier : public AActor
{
	GENERATED_BODY()

public:
	ARoomBarrier();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/**
	 * Recomputes whether this barrier should currently be blocking (DMC.SetRoomBarrierEnabled &&
	 * OwningRoomShell && !OwningRoomShell->IsRoomCleared()) and unconditionally re-applies
	 * BlockingVolume's collision and the actor's visibility to match -- called every Tick rather
	 * than only in response to a death/console-command event. Deliberately NOT guarded on "only
	 * when the desired state changed": ARoomShell::SetRoomActive unconditionally un-hides/
	 * re-enables collision on every actor attached to a room the instant it activates (this barrier
	 * included, since it attaches to its room like everything else), which can happen either before
	 * or after this actor's own BeginPlay depending on actor iteration order. Reasserting every Tick
	 * means that external interference gets corrected within one frame regardless of ordering,
	 * rather than a one-shot guard silently leaving a supposedly-closed barrier visibly open.
	 */
	void RefreshBlockingState();

	UPROPERTY(VisibleAnywhere, Category = "Room Barrier")
	TObjectPtr<UBoxComponent> BlockingVolume;

	UPROPERTY(VisibleAnywhere, Category = "Room Barrier")
	TObjectPtr<UPaperFlipbookComponent> FlipbookComponent;

	/** FB_Trap_RoomBarrier, set on the Blueprint/CDO. Played looping whenever the barrier is actively blocking. */
	UPROPERTY(EditDefaultsOnly, Category = "Room Barrier")
	TObjectPtr<UPaperFlipbook> BarrierFlipbook;

	/** Cached once in BeginPlay via Cast<ARoomShell>(GetAttachParentActor()) -- this barrier does nothing (never blocks) if it isn't attached to a RoomShell, same fail-open reasoning as ARoomExitTrigger's own missing-manager guard. */
	UPROPERTY(Transient)
	TObjectPtr<ARoomShell> OwningRoomShell;
};
