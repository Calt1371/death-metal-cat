#include "HitImpactEffectActor.h"

#include "Components/WidgetComponent.h"
#include "HitImpactWidget.h"

AHitImpactEffectActor::AHitImpactEffectActor()
{
	PrimaryActorTick.bCanEverTick = true;

	WidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("WidgetComp"));
	RootComponent = WidgetComp;
	WidgetComp->SetWidgetSpace(EWidgetSpace::Screen);
	WidgetComp->SetDrawSize(FVector2D(120.f, 120.f));
	WidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WidgetComp->SetGenerateOverlapEvents(false);
	WidgetComp->SetWidgetClass(UHitImpactWidget::StaticClass());
}

void AHitImpactEffectActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ElapsedTime += DeltaSeconds;
	const float Alpha = FMath::Clamp(ElapsedTime / Lifetime, 0.f, 1.f);

	if (!CachedWidget && WidgetComp)
	{
		CachedWidget = Cast<UHitImpactWidget>(WidgetComp->GetUserWidgetObject());
	}
	if (CachedWidget)
	{
		CachedWidget->UpdateImpactAlpha(Alpha);
	}

	if (ElapsedTime >= Lifetime)
	{
		Destroy();
	}
}

void AHitImpactEffectActor::InitHitImpact(EDamageTier Tier)
{
	ElapsedTime = 0.f;

	if (!CachedWidget && WidgetComp)
	{
		CachedWidget = Cast<UHitImpactWidget>(WidgetComp->GetUserWidgetObject());
	}

	if (CachedWidget)
	{
		CachedWidget->InitHitImpact(Tier);
	}
}
