#pragma once

#include "CoreMinimal.h"
#include "RoomTypes.generated.h"

/**
 * Identifies each room shell in the city biome's progression chain. A single playthrough passes
 * through 8 of these 9: Room1 -> Room2 -> Room3 -> (Room4A or Room4B) -> Room5 -> Room6 -> Room7
 * -> Room8. Room4A/Room4B are the branch choice at the end of Room3 and both reconverge into
 * Room5 -- whichever branch is taken, the other is never activated (see ARoomProgressionManager).
 */
UENUM(BlueprintType)
enum class ERoomID : uint8
{
	Room1,
	Room2,
	Room3,
	Room4A,
	Room4B,
	Room5,
	Room6,
	Room7,
	Room8,
};

/**
 * What an AEncounterSpawnMarker denotes -- purely descriptive metadata for the future Level &
 * Encounter Designer agent's JSON-driven population pass. Markers never spawn anything
 * themselves regardless of this value.
 */
UENUM(BlueprintType)
enum class EEncounterMarkerType : uint8
{
	EnemySpawn,
	PickupSpawn,
};
