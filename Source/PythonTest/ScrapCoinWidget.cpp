#include "ScrapCoinWidget.h"

#include "Components/Image.h"
#include "Blueprint/WidgetTree.h"

bool UScrapCoinWidget::Initialize()
{
	const bool bSuperResult = Super::Initialize();
	if (!bSuperResult)
	{
		return false;
	}

	// Build the tree here, not in NativeConstruct -- see UDamageNumberWidget's own comment for why
	// (Initialize() runs before UMG builds this widget's underlying Slate representation from
	// WidgetTree->RootWidget; NativeConstruct runs too late). Guard against rebuilding in case
	// Initialize() somehow runs twice.
	if (!CoinImage)
	{
		CoinImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CoinImage"));

		if (UTexture2D* CoinTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Items/T_ScrapsCurrency.T_ScrapsCurrency")))
		{
			CoinImage->SetBrushFromTexture(CoinTexture, true);
		}

		WidgetTree->RootWidget = CoinImage;
	}

	return true;
}
