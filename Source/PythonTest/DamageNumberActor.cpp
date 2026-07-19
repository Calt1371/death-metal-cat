#include "DamageNumberActor.h"

#include "Components/WidgetComponent.h"
#include "DamageNumberWidget.h"

ADamageNumberActor::ADamageNumberActor()
{
	PrimaryActorTick.bCanEverTick = true;

	WidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("WidgetComp"));
	RootComponent = WidgetComp;
	WidgetComp->SetWidgetSpace(EWidgetSpace::Screen);
	WidgetComp->SetDrawSize(FVector2D(150.f, 50.f));
	WidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WidgetComp->SetGenerateOverlapEvents(false);
	WidgetComp->SetWidgetClass(UDamageNumberWidget::StaticClass());
}

void ADamageNumberActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ElapsedTime += DeltaSeconds;
	const float Alpha = FMath::Clamp(ElapsedTime / Lifetime, 0.f, 1.f);

	SetActorLocation(StartLocation + FVector(0.f, 0.f, RiseDistance * Alpha));

	if (!CachedWidget && WidgetComp)
	{
		CachedWidget = Cast<UDamageNumberWidget>(WidgetComp->GetUserWidgetObject());
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

void ADamageNumberActor::InitDamageNumber(float DamageAmount, EDamageTier Tier)
{
	StartLocation = GetActorLocation();
	ElapsedTime = 0.f;

	if (!CachedWidget && WidgetComp)
	{
		CachedWidget = Cast<UDamageNumberWidget>(WidgetComp->GetUserWidgetObject());
	}

	if (CachedWidget)
	{
		CachedWidget->SetDamageText(DamageAmount, Tier);
	}
}
