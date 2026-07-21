#include "RoomShell.h"

void ARoomShell::SetRoomActive(bool bActive)
{
	SetActorHiddenInGame(!bActive);
	SetActorEnableCollision(bActive);

	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors, false, true);
	for (AActor* Attached : AttachedActors)
	{
		if (Attached)
		{
			Attached->SetActorHiddenInGame(!bActive);
			Attached->SetActorEnableCollision(bActive);
		}
	}
}
