#include "TrapHazard.h"

#include "Components/BoxComponent.h"
#include "PaperFlipbookComponent.h"
#include "PaperFlipbook.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "DeathMetalCatCharacter.h"

ATrapHazard::ATrapHazard()
{
	PrimaryActorTick.bCanEverTick = true;

	DamageBox = CreateDefaultSubobject<UBoxComponent>(TEXT("DamageBox"));
	RootComponent = DamageBox;
	DamageBox->SetBoxExtent(DamageBoxExtent);
	DamageBox->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	DamageBox->SetGenerateOverlapEvents(true);
	DamageBox->OnComponentBeginOverlap.AddDynamic(this, &ATrapHazard::OnDamageBoxBeginOverlap);
	DamageBox->OnComponentEndOverlap.AddDynamic(this, &ATrapHazard::OnDamageBoxEndOverlap);

	// Purely visual -- collision stays solely on DamageBox above, same "cosmetic sprite attached to
	// the real collision root" pattern AOneWayPlatform's PlatformSprite uses.
	FlipbookComponent = CreateDefaultSubobject<UPaperFlipbookComponent>(TEXT("FlipbookComponent"));
	FlipbookComponent->SetupAttachment(DamageBox);
	FlipbookComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Absolute scale only -- same reasoning as PlatformSprite: resizing DamageBox via the actor's
	// own Transform (e.g. per-instance tuning) shouldn't also resize the visible flipbook. Location/
	// rotation stay relative (follows the box's placement); resize the flipbook via ITS OWN
	// component Transform instead.
	FlipbookComponent->SetAbsolute(false, false, true);
}

void ATrapHazard::BeginPlay()
{
	Super::BeginPlay();

	if (Flipbook)
	{
		FlipbookComponent->SetFlipbook(Flipbook);
		FlipbookComponent->SetLooping(true);
		FlipbookComponent->PlayFromStart();
	}
}

void ATrapHazard::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bPlayerOverlapping || !FlipbookComponent)
	{
		return;
	}

	const int32 CurrentFrame = FlipbookComponent->GetPlaybackPositionInFrames();
	const bool bInDangerousFrame = CurrentFrame >= DangerousFrameStart && CurrentFrame <= DangerousFrameEnd;
	if (!bInDangerousFrame)
	{
		return;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastContactDamageTime < ContactDamageCooldown)
	{
		return;
	}

	if (ADeathMetalCatCharacter* PlayerCharacter = Cast<ADeathMetalCatCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
	{
		// No controller/instigator -- a stationary trap isn't a Pawn and has none, same as how a
		// projectile or other non-Pawn damage causer would pass null here.
		UGameplayStatics::ApplyDamage(PlayerCharacter, ContactDamage, nullptr, this, UDamageType::StaticClass());
		LastContactDamageTime = CurrentTime;
	}
}

void ATrapHazard::OnDamageBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (OtherActor && OtherActor->IsA<ADeathMetalCatCharacter>())
	{
		bPlayerOverlapping = true;
	}
}

void ATrapHazard::OnDamageBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (OtherActor && OtherActor->IsA<ADeathMetalCatCharacter>())
	{
		bPlayerOverlapping = false;
	}
}
