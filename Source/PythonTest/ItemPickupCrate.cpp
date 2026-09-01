#include "ItemPickupCrate.h"

#include "Components/BoxComponent.h"
#include "PaperSpriteComponent.h"
#include "DeathMetalCatCharacter.h"

AItemPickupCrate::AItemPickupCrate()
{
	PrimaryActorTick.bCanEverTick = false;

	OverlapVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("OverlapVolume"));
	RootComponent = OverlapVolume;
	OverlapVolume->SetBoxExtent(FVector(60.f, 60.f, 60.f));
	// Overlap-only (no blocking) -- the crate resolves and disappears the instant the player
	// touches it, so there's no reason to physically block movement in the meantime.
	OverlapVolume->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	OverlapVolume->SetGenerateOverlapEvents(true);
	OverlapVolume->OnComponentBeginOverlap.AddDynamic(this, &AItemPickupCrate::OnOverlapBegin);

	SpriteComponent = CreateDefaultSubobject<UPaperSpriteComponent>(TEXT("SpriteComponent"));
	SpriteComponent->SetupAttachment(RootComponent);
	SpriteComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpriteComponent->SetGenerateOverlapEvents(false);
	SpriteComponent->SetSpriteColor(SpriteTintColor);

	// Documented placeholder weighting -- NineLifeStim deliberately the rarest ("harder to find"
	// per the design ask), Scraps the common baseline drop, everything else roughly mid-tier.
	// Doesn't need to sum to 100 -- each entry's share is Weight / (sum of every Weight below).
	DropTable = {
		{EPickupResultType::Scraps, 40.f},
		{EPickupResultType::ScratchPatch, 15.f},
		{EPickupResultType::NineLifeStim, 4.f},
		{EPickupResultType::DeathDefier, 8.f},
		{EPickupResultType::RazorFang, 6.f},
		{EPickupResultType::DeadshotRounds, 6.f},
		{EPickupResultType::SteelFur, 6.f},
		{EPickupResultType::FancyFeed, 5.f},
		{EPickupResultType::GnarlyAmp, 5.f},
		{EPickupResultType::MirrorClaw, 4.f},
		{EPickupResultType::CatNip, 1.f},
	};
}

void AItemPickupCrate::BeginPlay()
{
	Super::BeginPlay();

	if (SpriteComponent)
	{
		SpriteComponent->SetSpriteColor(SpriteTintColor);
	}
}

void AItemPickupCrate::OnOverlapBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (ADeathMetalCatCharacter* Player = Cast<ADeathMetalCatCharacter>(OtherActor))
	{
		ResolveAndDestroy(Player);
	}
}

void AItemPickupCrate::ResolveAndDestroy(ADeathMetalCatCharacter* Player)
{
	float TotalWeight = 0.f;
	for (const FCrateDropEntry& Entry : DropTable)
	{
		TotalWeight += FMath::Max(0.f, Entry.Weight);
	}

	if (DropTable.Num() > 0 && TotalWeight > 0.f)
	{
		float Roll = FMath::FRandRange(0.f, TotalWeight);
		EPickupResultType Result = DropTable[0].Type;
		for (const FCrateDropEntry& Entry : DropTable)
		{
			Roll -= FMath::Max(0.f, Entry.Weight);
			if (Roll <= 0.f)
			{
				Result = Entry.Type;
				break;
			}
		}

		if (Result == EPickupResultType::Scraps)
		{
			Player->AddScraps(ScrapsPerDrop);
			UE_LOG(LogTemp, Warning, TEXT("[ITEM CRATE] %s dropped %d Scraps"), *GetName(), ScrapsPerDrop);
		}
		else
		{
			Player->ResolvePickup(Result);
			UE_LOG(LogTemp, Warning, TEXT("[ITEM CRATE] %s dropped item type %d"), *GetName(), static_cast<int32>(Result));
		}
	}

	Destroy();
}
