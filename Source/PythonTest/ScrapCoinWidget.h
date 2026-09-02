#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ScrapCoinWidget.generated.h"

class UImage;

/**
 * Single centered image showing the Scraps currency icon (T_ScrapsCurrency) -- built entirely in
 * Initialize() (WidgetTree->ConstructWidget), same reasoning and construction-order caveat as
 * UDamageNumberWidget's own doc comment: this whole project is driven from C++/Python scripting
 * with no interactive UMG designer step, and a single image doesn't need one either. Unlike that
 * widget, nothing here varies per-instance (always the same icon), so there's no pending-data
 * fallback to worry about -- Initialize() is fully self-sufficient.
 */
UCLASS()
class PYTHONTEST_API UScrapCoinWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;

private:
	UPROPERTY()
	TObjectPtr<UImage> CoinImage;
};
