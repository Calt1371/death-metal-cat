#include "RoomProgressionManager.h"

#include "RoomShell.h"
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
