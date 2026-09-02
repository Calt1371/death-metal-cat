#pragma once

#include "CoreMinimal.h"
#include "Engine/TargetPoint.h"
#include "ManualEnemySpawnMarker.generated.h"

class ADeathMetalCatEnemyBase;

/**
 * Hand-placed spawn point for the manual, non-JSON-pipeline room population workflow -- see
 * AgentScripts/ue_spawn_manual_room_enemies.py. Purely a data marker with no spawn logic of its
 * own, same zero-footprint ATargetPoint-derived pattern as AEncounterSpawnMarker (free editor
 * icon, nothing at runtime). Unlike that marker (addressed by MarkerID from Level & Encounter
 * Designer's JSON, always spawning the single hardcoded BP_EnemyBase), this one carries its own
 * EnemyClass directly: drag one into a hand-built room's viewport, point EnemyClass at whichever
 * enemy Blueprint belongs there, and the spawn script does the rest. Exists specifically because
 * the hand-built rooms (Room1-Room6/4A/4B) have no live EncounterSpawnMarker actors and can never
 * safely get any via the geometry-import pipeline (see Docs/golden_room_script_README.md) --
 * this sidesteps that pipeline entirely.
 */
UCLASS()
class PYTHONTEST_API AManualEnemySpawnMarker : public ATargetPoint
{
	GENERATED_BODY()

public:
	/**
	 * Which enemy Blueprint to spawn at this marker's exact location -- pick any subclass of
	 * ADeathMetalCatEnemyBase (e.g. BP_EnemyDeathBotWalking, BP_EnemyDeathBotFlying,
	 * BP_EnemyDeathBotHeavy, BP_EnemyDeathBotCrawler, or the generic BP_EnemyBase). Left unset,
	 * the spawn script skips this marker with a warning rather than erroring the whole room.
	 */
	UPROPERTY(EditAnywhere, Category = "Manual Enemy Spawn")
	TSubclassOf<ADeathMetalCatEnemyBase> EnemyClass;
};
