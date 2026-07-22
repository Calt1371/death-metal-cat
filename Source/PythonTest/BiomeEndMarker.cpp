#include "BiomeEndMarker.h"

#include "Components/BoxComponent.h"
#include "DeathMetalCatCharacter.h"
#include "Engine/Engine.h"

ABiomeEndMarker::ABiomeEndMarker()
{
	PrimaryActorTick.bCanEverTick = false;

	TriggerVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerVolume"));
	RootComponent = TriggerVolume;
	TriggerVolume->SetBoxExtent(FVector(50.f, 200.f, 150.f));
	TriggerVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerVolume->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	TriggerVolume->SetGenerateOverlapEvents(true);
	TriggerVolume->OnComponentBeginOverlap.AddDynamic(this, &ABiomeEndMarker::OnTriggerBeginOverlap);
}

void ABiomeEndMarker::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (bHasFired || !OtherActor || !OtherActor->IsA<ADeathMetalCatCharacter>())
	{
		return;
	}

	bHasFired = true;

	UE_LOG(LogTemp, Warning, TEXT("[ROOM PROGRESSION] End of biome reached (Room8) -- no boss wired up yet, stretch goal"));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, TEXT("END OF BIOME (placeholder -- no boss yet)"));
	}
}

void ABiomeEndMarker::ResetTrigger()
{
	bHasFired = false;
}
