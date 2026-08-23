#include "RoomShell.h"

#include "HAL/IConsoleManager.h"

// Single on/off switch for whether a cleared-room barrier is allowed to actually block the player
// -- see IsRoomBarrierGateEnabled's own doc comment. Defaults false (off/test mode): rooms play
// exactly as they did before this system existed until explicitly turned on with
// "DMC.SetRoomBarrierEnabled 1" in the console.
static TAutoConsoleVariable<bool> CVarRoomBarrierEnabled(
	TEXT("DMC.SetRoomBarrierEnabled"),
	false,
	TEXT("Whether cleared-room barriers (ARoomBarrier) and their matching ARoomExitTrigger actually block the player. Enemy kill-tracking always runs regardless of this. Defaults off."),
	ECVF_Default);

bool ARoomShell::IsRoomBarrierGateEnabled()
{
	return CVarRoomBarrierEnabled.GetValueOnGameThread();
}

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

void ARoomShell::RegisterEnemy()
{
	++EnemiesRemaining;
}

void ARoomShell::NotifyEnemyDefeated()
{
	EnemiesRemaining = FMath::Max(0, EnemiesRemaining - 1);
}
