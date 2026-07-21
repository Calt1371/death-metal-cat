#pragma once

#include "CoreMinimal.h"
#include "Engine/TargetPoint.h"
#include "RoomTypes.h"
#include "RoomShell.generated.h"

/**
 * Marks the origin and activation group of one room in the city biome's progression chain (see
 * ERoomID). Derives from ATargetPoint purely for the free editor-visible placement icon --
 * ATargetPoint already renders as nothing in-game and has no gameplay footprint of its own.
 *
 * Everything that should appear/collide only while this room is the active one -- floor geometry,
 * walls, RoomExitTrigger volumes, EncounterSpawnMarkers -- is grouped simply by attaching it to
 * this actor in the World Outliner (drag-and-drop, or right-click "Attach To"). No tagging or
 * manual registration needed: SetRoomActive recurses over GetAttachedActors() and toggles every
 * descendant along with this actor itself. ARoomProgressionManager finds every placed RoomShell
 * via GetAllActorsOfClass at BeginPlay and activates only the starting room.
 */
UCLASS()
class PYTHONTEST_API ARoomShell : public ATargetPoint
{
	GENERATED_BODY()

public:
	/** Which room this shell represents -- must be unique per placed RoomShell in a level. */
	UPROPERTY(EditAnywhere, Category = "Room Progression")
	ERoomID RoomID = ERoomID::Room1;

	/**
	 * Shows (collision enabled) or hides (collision disabled) this actor and every actor attached
	 * to it, recursively -- the same SetActorHiddenInGame + SetActorEnableCollision(false) idiom
	 * ADeathMetalCatEnemyBase::HandleDeath already uses to retire a dead enemy, applied here to a
	 * whole room's worth of content at once instead of a single actor.
	 */
	void SetRoomActive(bool bActive);
};
