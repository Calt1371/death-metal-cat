#include "RoomProgressionManager.h"

#include "RoomShell.h"
#include "RoomExitTrigger.h"
#include "BiomeEndMarker.h"
#include "DeathMetalCatCharacter.h"
#include "GnarlyRankHUDWidget.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
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

	// Every placed RoomShell is active from level start now -- the "hide every room but the
	// current one" behavior below was built back when all rooms shared one physical world
	// position (see AdvanceToRoom's own teleport comment) and needed hiding so overlapping rooms
	// didn't render on top of each other. Rooms are now hand-built at real, separate locations,
	// so nothing needs hiding for that reason anymore, and the player can't see or reach a room
	// they haven't been teleported to yet regardless. AdvanceToRoom's teleport-on-exit, the fade
	// transition, and RegisterEnemy/NotifyEnemyDefeated/barrier gating are all untouched -- this
	// only changes the level-start default.
	for (const TPair<ERoomID, TObjectPtr<ARoomShell>>& Pair : RoomShellsByID)
	{
		if (Pair.Value)
		{
			Pair.Value->SetRoomActive(true);
		}
	}
}

void ARoomProgressionManager::AdvanceToRoom(ERoomID NewRoomID)
{
	// The room being left is deliberately NOT deactivated anymore -- see BeginPlay's own comment.
	// Every room stays active/visible once the level starts; this only still activates the target
	// room explicitly as a safety net (e.g. if a shell somehow wasn't in RoomShellsByID yet at
	// BeginPlay), not because anything actually needs re-activating in the normal case.
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
	// The actual room switch + trigger re-arming now happen inside BeginRoomTransition's black-pause
	// callback (HandleRoomTransitionFadeOutComplete), not synchronously here -- see bIsFullRestart.
	BeginRoomTransition(StartingRoomID, true);
}

void ARoomProgressionManager::BeginRoomTransition(ERoomID TargetRoomID, bool bIsFullRestart)
{
	PendingTransitionRoomID = TargetRoomID;
	bPendingTransitionIsFullRestart = bIsFullRestart;

	ADeathMetalCatCharacter* PlayerCharacter = Cast<ADeathMetalCatCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
	if (!PlayerCharacter)
	{
		UE_LOG(LogTemp, Error, TEXT("[ROOM PROGRESSION] BeginRoomTransition: no player character found -- aborting transition to %s"), *UEnum::GetValueAsString(TargetRoomID));
		return;
	}

	// Disabled for the whole transition (fade-out + black pause + fade-in); re-enabled only once
	// HandleRoomTransitionFadeInComplete confirms the fade back in has finished.
	PlayerCharacter->DisableInput(Cast<APlayerController>(PlayerCharacter->GetController()));

	if (UGnarlyRankHUDWidget* HUDWidget = PlayerCharacter->GetGnarlyRankHUDWidget())
	{
		HUDWidget->StartRoomFadeOut(FadeOutDuration);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ROOM PROGRESSION] BeginRoomTransition: no HUD widget found -- room switch/teleport will still happen on schedule, just without the visible fade"));
	}

	GetWorldTimerManager().SetTimer(RoomTransitionTimerHandle, this, &ARoomProgressionManager::HandleRoomTransitionFadeOutComplete, FadeOutDuration, false);
}

void ARoomProgressionManager::HandleRoomTransitionFadeOutComplete()
{
	// Screen is now fully black -- safe to do the actual room switch and teleport invisibly.
	AdvanceToRoom(PendingTransitionRoomID);

	if (bPendingTransitionIsFullRestart)
	{
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

		UE_LOG(LogTemp, Warning, TEXT("[ROOM PROGRESSION] Full restart: %d exit trigger(s) and %d biome marker(s) re-armed"), FoundTriggers.Num(), FoundMarkers.Num());
	}

	ADeathMetalCatCharacter* PlayerCharacter = Cast<ADeathMetalCatCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
	if (PlayerCharacter)
	{
		if (TObjectPtr<ARoomShell>* TargetShell = RoomShellsByID.Find(PendingTransitionRoomID))
		{
			if (*TargetShell)
			{
				// All 9 RoomShells share one world position now (see this class's header comment),
				// so this is simply "teleport to the shared origin" in practice -- looking it up via
				// the target room's own shell rather than a hardcoded constant still keeps this
				// correct if that ever stops being true for some future room.
				PlayerCharacter->SetActorLocation((*TargetShell)->GetActorLocation(), false, nullptr, ETeleportType::TeleportPhysics);
			}
		}

		// Stop any leftover fall/knockback/movement velocity from carrying over through the
		// teleport -- e.g. walking into an exit trigger mid-jump shouldn't have the character
		// resume falling with pre-teleport velocity once placed in the new room.
		if (UCharacterMovementComponent* MoveComp = PlayerCharacter->GetCharacterMovement())
		{
			MoveComp->StopMovementImmediately();
		}
	}

	GetWorldTimerManager().SetTimer(RoomTransitionTimerHandle, this, &ARoomProgressionManager::HandleRoomTransitionBlackPauseComplete, BlackPauseDuration, false);
}

void ARoomProgressionManager::HandleRoomTransitionBlackPauseComplete()
{
	ADeathMetalCatCharacter* PlayerCharacter = Cast<ADeathMetalCatCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
	if (PlayerCharacter)
	{
		if (UGnarlyRankHUDWidget* HUDWidget = PlayerCharacter->GetGnarlyRankHUDWidget())
		{
			HUDWidget->StartRoomFadeIn(FadeInDuration);
		}
	}

	GetWorldTimerManager().SetTimer(RoomTransitionTimerHandle, this, &ARoomProgressionManager::HandleRoomTransitionFadeInComplete, FadeInDuration, false);
}

void ARoomProgressionManager::HandleRoomTransitionFadeInComplete()
{
	if (ADeathMetalCatCharacter* PlayerCharacter = Cast<ADeathMetalCatCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
	{
		PlayerCharacter->EnableInput(Cast<APlayerController>(PlayerCharacter->GetController()));
	}

	UE_LOG(LogTemp, Warning, TEXT("[ROOM PROGRESSION] Transition to %s complete"), *UEnum::GetValueAsString(PendingTransitionRoomID));
}
