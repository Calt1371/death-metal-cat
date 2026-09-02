#include "ScrapCoinEffectActor.h"

#include "Components/WidgetComponent.h"
#include "ScrapCoinWidget.h"

AScrapCoinEffectActor::AScrapCoinEffectActor()
{
	PrimaryActorTick.bCanEverTick = true;

	WidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("WidgetComp"));
	RootComponent = WidgetComp;
	WidgetComp->SetWidgetSpace(EWidgetSpace::Screen);
	WidgetComp->SetDrawSize(FVector2D(32.f, 32.f));
	WidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WidgetComp->SetGenerateOverlapEvents(false);
	WidgetComp->SetWidgetClass(UScrapCoinWidget::StaticClass());
}

void AScrapCoinEffectActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ElapsedTime += DeltaSeconds;
	const float Alpha = FMath::Clamp(ElapsedTime / Lifetime, 0.f, 1.f);

	if (TargetActor)
	{
		SetActorLocation(FMath::Lerp(StartLocation, TargetActor->GetActorLocation(), Alpha));
	}

	if (!CachedWidget && WidgetComp)
	{
		CachedWidget = Cast<UScrapCoinWidget>(WidgetComp->GetUserWidgetObject());
	}
	if (CachedWidget)
	{
		CachedWidget->SetRenderOpacity(1.f - Alpha);
	}

	if (ElapsedTime >= Lifetime)
	{
		Destroy();
	}
}

void AScrapCoinEffectActor::InitScrapCoin(AActor* InTargetActor)
{
	StartLocation = GetActorLocation();
	TargetActor = InTargetActor;
	ElapsedTime = 0.f;
}
