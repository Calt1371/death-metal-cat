#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DamageTypes.h"
#include "DamageNumberWidget.generated.h"

class UTextBlock;

/**
 * Single centered text block showing a damage number, color-coded by EDamageTier. Built entirely
 * in Initialize() (WidgetTree->ConstructWidget) rather than via a Blueprint-designed widget tree
 * -- this whole project is driven from C++/Python scripting with no interactive UMG designer
 * step, and a single text block doesn't need one either.
 *
 * Deliberately built in Initialize(), not NativeConstruct(): Initialize() runs before UMG builds
 * this UUserWidget's underlying Slate widget tree from WidgetTree->RootWidget, while
 * NativeConstruct() runs after -- assigning RootWidget in NativeConstruct was confirmed (via
 * DesiredSize always reading 0,0) to be too late for that first build, leaving TextBlock a valid
 * UObject with correct text/color that Slate never actually knew to measure or render.
 */
UCLASS()
class PYTHONTEST_API UDamageNumberWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;

	/**
	 * Sets the displayed text/color for DamageAmount's Tier. Defensively safe to call before
	 * Initialize() has run, stashing the values to apply once TextBlock exists -- not expected to
	 * trigger in practice now that Initialize() builds TextBlock synchronously and up front, but
	 * kept as a fallback so this doesn't silently drop data if that call order ever changes.
	 */
	void SetDamageText(float DamageAmount, EDamageTier Tier);

private:
	/** Actually writes DamageAmount/Tier to TextBlock. Only ever called once TextBlock is known to be valid -- from SetDamageText directly if already constructed, or from Initialize() for a pending call that arrived too early. */
	void ApplyDamageText(float DamageAmount, EDamageTier Tier);

	UPROPERTY()
	TObjectPtr<UTextBlock> TextBlock;

	bool bHasPendingDamage = false;
	float PendingDamageAmount = 0.f;
	EDamageTier PendingTier = EDamageTier::Normal;
};
