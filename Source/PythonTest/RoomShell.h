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

	/**
	 * Called once from ADeathMetalCatEnemyBase::BeginPlay for every enemy attached to this room
	 * (spawn_encounter_actors.py already attaches every spawned enemy to its room's RoomShell, same
	 * as floors/walls/markers -- see that script's own doc comment -- so an enemy finding its
	 * RoomShell via GetAttachParentActor() at BeginPlay needs no separate registration step from the
	 * spawn script itself). Increments EnemiesRemaining.
	 */
	void RegisterEnemy();

	/**
	 * Called exactly once per real kill, from the same TakeDamage block that awards XP -- NOT from
	 * HandleDeath/HandleRespawn, which are purely a visual/state reset that can run repeatedly for
	 * the same enemy (see ADeathMetalCatEnemyBase's own respawn-for-testing convenience). Decrements
	 * EnemiesRemaining and clamps at 0; deliberately never re-incremented on respawn, so a cleared
	 * room stays cleared even if its enemies come back for further testing -- respawn is a testing
	 * convenience, not something a barrier should re-lock behind the player over.
	 */
	void NotifyEnemyDefeated();

	/** True once every enemy registered against this room (via RegisterEnemy) has been defeated (via NotifyEnemyDefeated) -- also true, from the start, for a room with zero registered enemies. Read by ARoomBarrier/ARoomExitTrigger to decide whether to block the player. */
	bool IsRoomCleared() const { return EnemiesRemaining <= 0; }

	/**
	 * Wraps the DMC.SetRoomBarrierEnabled console variable (defined in RoomShell.cpp) -- the single
	 * on/off switch ARoomBarrier and ARoomExitTrigger both check before actually blocking anything.
	 * Defaults false (off) so rooms play exactly as before this system existed until explicitly
	 * turned on; toggle live in PIE with "DMC.SetRoomBarrierEnabled 1" / "...0". Enemy registration/
	 * tracking (RegisterEnemy/NotifyEnemyDefeated) is NOT gated by this -- it always runs, this only
	 * controls whether that tracked state is actually allowed to block the player.
	 */
	static bool IsRoomBarrierGateEnabled();

private:
	/** Enemies registered against this room minus enemies defeated so far -- see RegisterEnemy/NotifyEnemyDefeated. Never goes below 0. */
	UPROPERTY(VisibleAnywhere, Category = "Room Progression")
	int32 EnemiesRemaining = 0;
};
