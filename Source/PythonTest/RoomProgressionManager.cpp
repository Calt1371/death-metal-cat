#include "RoomProgressionManager.h"

#include "RoomShell.h"
#include "RoomExitTrigger.h"
#include "BiomeEndMarker.h"
#include "Kismet/GameplayStatics.h"

ARoomProgressionManager::ARoomProgressionManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ARoomProgressionManager::BeginPlay()
{
	Super::BeginPlay();

	TArray<AActor*> FoundShells;
	UGameplayStatics::GetAllActorsOfClass(this, ARoomShell::StaticClass(), FoundShells);

	for (AActor* Actor : FoundShells)
	{
		if (ARoomShell* Shell = Cast<ARoomShell>(Actor))
		{
			RoomShellsByID.Add(Shell->RoomID, Shell);
		}
	}

	CurrentRoomID = StartingRoomID;

	// Only the starting room is active at level start -- every other placed RoomShell (including
	// both branch rooms) starts hidden/collision-disabled until AdvanceToRoom reaches it.
	for (const TPair<ERoomID, TObjectPtr<ARoomShell>>& Pair : RoomShellsByID)
	{
		if (Pair.Value)
		{
			Pair.Value->SetRoomActive(Pair.Key == CurrentRoomID);
		}
	}
}

void ARoomProgressionManager::AdvanceToRoom(ERoomID NewRoomID)
{
	if (TObjectPtr<ARoomShell>* CurrentShell = RoomShellsByID.Find(CurrentRoomID))
	{
		if (*CurrentShell)
		{
			(*CurrentShell)->SetRoomActive(false);
		}
	}

	if (TObjectPtr<ARoomShell>* NextShell = RoomShellsByID.Find(NewRoomID))
	{
		if (*NextShell)
		{
			(*NextShell)->SetRoomActive(true);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ROOM PROGRESSION] AdvanceToRoom: no RoomShell placed for %s"), *UEnum::GetValueAsString(NewRoomID));
	}

	const ERoomID PreviousRoomID = CurrentRoomID;
	CurrentRoomID = NewRoomID;

	UE_LOG(LogTemp, Warning, TEXT("[ROOM PROGRESSION] %s -> %s"), *UEnum::GetValueAsString(PreviousRoomID), *UEnum::GetValueAsString(CurrentRoomID));
}

void ARoomProgressionManager::ResetToStartingRoom()
{
	AdvanceToRoom(StartingRoomID);

	// Re-arm every one-shot exit trigger and the biome-end marker so a second pass through
	// already-cleared rooms works -- AdvanceToRoom above only handles room activation/collision,
	// not each trigger's own bHasFired latch from the original playthrough.
	TArray<AActor*> FoundTriggers;
	UGameplayStatics::GetAllActorsOfClass(this, ARoomExitTrigger::StaticClass(), FoundTriggers);
	for (AActor* Actor : FoundTriggers)
	{
		if (ARoomExitTrigger* Trigger = Cast<ARoomExitTrigger>(Actor))
		{
			Trigger->ResetTrigger();
		}
	}

	TArray<AActor*> FoundMarkers;
	UGameplayStatics::GetAllActorsOfClass(this, ABiomeEndMarker::StaticClass(), FoundMarkers);
	for (AActor* Actor : FoundMarkers)
	{
		if (ABiomeEndMarker* Marker = Cast<ABiomeEndMarker>(Actor))
		{
			Marker->ResetTrigger();
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[ROOM PROGRESSION] ResetToStartingRoom: back to %s, %d exit trigger(s) and %d biome marker(s) re-armed"),
		*UEnum::GetValueAsString(StartingRoomID), FoundTriggers.Num(), FoundMarkers.Num());
}
