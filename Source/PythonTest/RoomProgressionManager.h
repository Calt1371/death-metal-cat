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
 * and deactivates every other room. ARoomExitTrigger volumes call AdvanceToRoom directly when the
 * player walks through them, which is the only way CurrentRoomID ever changes -- there is no path
 * back to a previously-active room once its ARoomShell has been deactivated (see
 * ARoomShell::SetRoomActive), which is what makes progression one-way.
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
	 * CurrentRoomID. Called by ARoomExitTrigger on player overlap. No-ops (with a warning) if
	 * NewRoomID has no corresponding placed ARoomShell.
	 */
	void AdvanceToRoom(ERoomID NewRoomID);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TMap<ERoomID, TObjectPtr<ARoomShell>> RoomShellsByID;
};
