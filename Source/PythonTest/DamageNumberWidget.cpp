#include "DamageNumberWidget.h"

#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"

bool UDamageNumberWidget::Initialize()
{
	const bool bSuperResult = Super::Initialize();
	if (!bSuperResult)
	{
		return false;
	}

	// Build the tree here, not in NativeConstruct: Initialize() runs before UMG builds this
	// widget's underlying Slate representation from WidgetTree->RootWidget. WidgetTree can already
	// have a RootWidget (e.g. if Initialize() somehow runs twice) -- guard against rebuilding.
	if (!TextBlock)
	{
		TextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DamageText"));

		// Default UTextBlock font size reads fine in a UI panel but is too small for a screen-space
		// world overlay meant to be read at a glance mid-combat.
		FSlateFontInfo Font = TextBlock->GetFont();
		Font.Size = 24;
		TextBlock->SetFont(Font);
		TextBlock->SetJustification(ETextJustify::Center);

		WidgetTree->RootWidget = TextBlock;
	}

	// A SetDamageText call can land here before Initialize() runs, if WidgetComponent's
	// construction order ever changes -- stays as a defensive fallback so this works regardless
	// of call order, not because it's expected to trigger now that TextBlock is built up front.
	if (bHasPendingDamage)
	{
		ApplyDamageText(PendingDamageAmount, PendingTier);
		bHasPendingDamage = false;
	}

	return true;
}

void UDamageNumberWidget::SetDamageText(float DamageAmount, EDamageTier Tier)
{
	if (TextBlock)
	{
		ApplyDamageText(DamageAmount, Tier);
	}
	else
	{
		// NativeConstruct hasn't run yet -- stash and apply once it has, instead of silently
		// dropping this on a null TextBlock.
		PendingDamageAmount = DamageAmount;
		PendingTier = Tier;
		bHasPendingDamage = true;
	}
}

void UDamageNumberWidget::ApplyDamageText(float DamageAmount, EDamageTier Tier)
{
	if (!TextBlock)
	{
		UE_LOG(LogTemp, Error, TEXT("[DAMAGE NUMBER] ApplyDamageText: TextBlock is NULL"));
		return;
	}

	const FString TextString = FString::Printf(TEXT("%d"), FMath::RoundToInt(DamageAmount));
	TextBlock->SetText(FText::FromString(TextString));

	FLinearColor Color = FLinearColor::White;
	switch (Tier)
	{
	case EDamageTier::Weakness:
		Color = FLinearColor(1.f, 0.85f, 0.1f); // yellow/gold
		break;
	case EDamageTier::Critical:
		Color = FLinearColor::Red;
		break;
	case EDamageTier::Normal:
	default:
		Color = FLinearColor::White;
		break;
	}
	TextBlock->SetColorAndOpacity(FSlateColor(Color));
}
