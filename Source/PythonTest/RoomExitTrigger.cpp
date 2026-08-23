#include "RoomExitTrigger.h"

#include "Components/BoxComponent.h"
#include "Kismet/GameplayStatics.h"
#include "DeathMetalCatCharacter.h"
#include "RoomProgressionManager.h"
#include "RoomShell.h"

ARoomExitTrigger::ARoomExitTrigger()
{
	PrimaryActorTick.bCanEverTick = false;

	// Overlap-only against the player, same "OverlapAllDynamic" profile ADeathMetalCatCharacter's
	// own SwordHitbox uses -- this only ever needs to report an overlap, never physically block.
	TriggerVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerVolume"));
	RootComponent = TriggerVolume;
	TriggerVolume->SetBoxExtent(FVector(50.f, 200.f, 150.f));
	TriggerVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerVolume->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	TriggerVolume->SetGenerateOverlapEvents(true);
	TriggerVolume->OnComponentBeginOverlap.AddDynamic(this, &ARoomExitTrigger::OnTriggerBeginOverlap);
}

void ARoomExitTrigger::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (bHasFired || !OtherActor || !OtherActor->IsA<ADeathMetalCatCharacter>())
	{
		return;
	}

	// Belt-and-suspenders against ARoomBarrier's own physical blocking collision -- refuses to fire
	// the transition while this room isn't cleared and the gate is on, so the player can never
	// advance past a locked door even if they somehow squeeze past the barrier's collision volume.
	// Off (the default) or a RoomShell-less trigger (e.g. one not yet wired into this system)
	// behaves exactly as before this system existed -- fires unconditionally.
	if (ARoomShell* OwningRoomShell = Cast<ARoomShell>(GetAttachParentActor()))
	{
		if (ARoomShell::IsRoomBarrierGateEnabled() && !OwningRoomShell->IsRoomCleared())
		{
			return;
		}
	}

	ARoomProgressionManager* Manager = Cast<ARoomProgressionManager>(UGameplayStatics::GetActorOfClass(this, ARoomProgressionManager::StaticClass()));
	if (!Manager)
	{
		UE_LOG(LogTemp, Error, TEXT("[ROOM PROGRESSION] RoomExitTrigger %s overlapped but found no ARoomProgressionManager in the level"), *GetName());
		return;
	}

	bHasFired = true;
	Manager->BeginRoomTransition(NextRoomID);
}

void ARoomExitTrigger::ResetTrigger()
{
	bHasFired = false;
}
