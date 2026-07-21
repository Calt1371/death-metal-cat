#pragma once

#include "CoreMinimal.h"
#include "Engine/TargetPoint.h"
#include "RoomTypes.h"
#include "EncounterSpawnMarker.generated.h"

/**
 * Authored, placeable position for a future enemy or pickup spawn -- purely a data marker with no
 * spawn logic of its own (mirrors ARoomShell in deriving from ATargetPoint for the free
 * editor-visible icon and zero in-game footprint). Hand-placed one per intended spawn point
 * within a room's greybox; MarkerID must be unique within that room so the Level & Encounter
 * Designer agent's JSON output can address it by name. A marker with no matching JSON entry
 * simply stays empty -- nothing spawns there.
 */
UCLASS()
class PYTHONTEST_API AEncounterSpawnMarker : public ATargetPoint
{
	GENERATED_BODY()

public:
	/** Unique (within its room) identifier the Level & Encounter Designer agent's JSON addresses this marker by. */
	UPROPERTY(EditAnywhere, Category = "Encounter Marker")
	FString MarkerID;

	/** Whether this marker is a candidate spawn point for an enemy or a pickup -- purely descriptive, doesn't affect behavior here. */
	UPROPERTY(EditAnywhere, Category = "Encounter Marker")
	EEncounterMarkerType MarkerType = EEncounterMarkerType::EnemySpawn;
};
