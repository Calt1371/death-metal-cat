#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomTypes.h"
#include "RoomProgressionManager.generated.h"

class ARoomShell;

/**
 * Single source of truth for which room in the city biome is currently active. Place exactly one
 * of these in a level alongside its ARoomShell actors. At BeginPlay, finds every placed
 * ARoomShell via GetAllActorsOfClass (no manual wiring needed), activates only StartingRoomID,
 * and deactivates every other room. There is no path back to a previously-active room once its
 * ARoomShell has been deactivated (see ARoomShell::SetRoomActive), which is what makes forward
 * progression one-way. The one deliberate exception is ResetToStartingRoom, called on player
 * death to fully restart the biome run rather than just respawning in place.
 *
 * ROOM TRANSITIONS (Hollow Knight-style fade-to-black, not continuous same-world walking):
 * all 9 RoomShells now share the exact same world position (collapsed from the old
 * dynamic-spacing layout -- see AgentScripts/update_room_spacing.py's deprecation note), since
 * only one room is ever active/visible at a time anyway (SetRoomActive already guarantees this).
 * ARoomExitTrigger/the Room3 branch triggers/ResetToStartingRoom all call BeginRoomTransition
 * instead of AdvanceToRoom directly now -- AdvanceToRoom itself is unchanged (still just the raw
 * activate/deactivate swap) but is no longer called directly from anywhere except
 * BeginRoomTransition's own internal callback, since every room change now needs the
 * fade/teleport treatment, not just the instant swap.
 */
UCLASS()
class PYTHONTEST_API ARoomProgressionManager : public AActor
{
	GENERATED_BODY()

public:
	ARoomProgressionManager();

	/** Which room is active the moment the level starts. Room1 for a normal playthrough. */
	UPROPERTY(EditAnywhere, Category = "Room Progression")
	ERoomID StartingRoomID = ERoomID::Room1;

	/** Read-only -- which room is active right now. Updated only by AdvanceToRoom. */
	UPROPERTY(BlueprintReadOnly, Category = "Room Progression")
	ERoomID CurrentRoomID = ERoomID::Room1;

	/**
	 * Deactivates the ARoomShell for CurrentRoomID, activates the one for NewRoomID, and updates
	 * CurrentRoomID. No-ops (with a warning) if NewRoomID has no corresponding placed ARoomShell.
	 * Purely the instant activate/deactivate swap -- does NOT fade, teleport, or touch input.
	 * Called only from BeginRoomTransition's own internal callback now; ARoomExitTrigger and
	 * ResetToStartingRoom go through BeginRoomTransition instead of calling this directly, so every
	 * room change gets the fade/teleport treatment.
	 */
	void AdvanceToRoom(ERoomID NewRoomID);

	/**
	 * Called on player death: begins a fade-to-black transition (via BeginRoomTransition) straight
	 * back to StartingRoomID, safe for a non-adjacent jump -- it doesn't care how far apart the
	 * rooms are. Also re-arms every ARoomExitTrigger and ABiomeEndMarker in the level (resets their
	 * one-shot bHasFired latch), timed to happen during the same black pause as the actual room
	 * switch. That re-arming is essential and not covered by AdvanceToRoom alone: reactivating a
	 * room's ARoomShell restores its exit trigger's collision, but the trigger would still
	 * silently ignore the overlap without this, since it fired once already on the original pass
	 * through.
	 */
	void ResetToStartingRoom();

	/**
	 * Begins a Hollow Knight-style fade-to-black transition to TargetRoomID: fades the HUD's
	 * full-screen overlay to black (FadeOutDuration), holds at full black for a deliberate
	 * "loading beat" (BlackPauseDuration) during which the actual room switch (AdvanceToRoom) and
	 * player teleport happen invisibly, then fades back in (FadeInDuration). Disables player input
	 * for the whole transition and re-enables it once the fade-in completes. This is what
	 * ARoomExitTrigger (including both Room3 branch triggers) and ResetToStartingRoom call instead
	 * of AdvanceToRoom directly, so every room change -- normal progression, branching, and death
	 * restart alike -- gets the same treatment.
	 *
	 * bIsFullRestart additionally re-arms every ARoomExitTrigger/ABiomeEndMarker's bHasFired latch
	 * during the same black-pause callback that does the room switch -- set true only by
	 * ResetToStartingRoom, since normal forward progression must never re-arm anything (that's
	 * exactly what would break the one-way-progression guarantee).
	 */
	void BeginRoomTransition(ERoomID TargetRoomID, bool bIsFullRestart = false);

	/** How long (seconds) the fade-to-black takes at the start of a room transition. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Room Transition", meta = (ClampMin = "0"))
	float FadeOutDuration = 0.35f;

	/** How long (seconds) the screen holds at full black before fading back in -- the deliberate "loading beat". Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Room Transition", meta = (ClampMin = "0"))
	float BlackPauseDuration = 0.4f;

	/** How long (seconds) the fade-in from black takes once the new room is ready. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Room Transition", meta = (ClampMin = "0"))
	float FadeInDuration = 0.35f;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TMap<ERoomID, TObjectPtr<ARoomShell>> RoomShellsByID;

	/** Timer callback fired FadeOutDuration seconds after BeginRoomTransition starts -- screen is now fully black. Does the actual AdvanceToRoom + player teleport (+ trigger re-arming if bIsFullRestart), then arms BlackPauseDuration before fading back in. */
	void HandleRoomTransitionFadeOutComplete();

	/** Timer callback fired BlackPauseDuration seconds after HandleRoomTransitionFadeOutComplete -- starts the fade back in and arms FadeInDuration before re-enabling input. */
	void HandleRoomTransitionBlackPauseComplete();

	/** Timer callback fired FadeInDuration seconds after HandleRoomTransitionBlackPauseComplete -- the transition is fully done; re-enables player input. */
	void HandleRoomTransitionFadeInComplete();

	/** Which room BeginRoomTransition is currently transitioning TO -- read by HandleRoomTransitionFadeOutComplete once the black pause begins. */
	ERoomID PendingTransitionRoomID = ERoomID::Room1;

	/** Set by BeginRoomTransition, read by HandleRoomTransitionFadeOutComplete -- whether this transition should also re-arm every exit trigger/biome marker (the ResetToStartingRoom death-restart case) or not (normal forward progression). */
	bool bPendingTransitionIsFullRestart = false;

	FTimerHandle RoomTransitionTimerHandle;
};
