#include "GlobalBrightnessOverlayWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"

namespace
{
	// Same engine-provided 1x1 white texture every other tinted-overlay in this project uses.
	const TCHAR* BrightnessWhiteSquarePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");

	// How dark the overlay can get at Brightness=0 -- capped short of fully opaque black so the
	// darkest setting dims rather than blacks out the screen entirely.
	constexpr float MaxDarkenOpacity = 0.75f;

	// How strong the overlay can get at Brightness=1 -- kept modest since a full-strength white
	// wash looks bad well before full opacity would be reached.
	constexpr float MaxLightenOpacity = 0.35f;

	constexpr float NeutralBrightness = 0.5f;
}

bool UGlobalBrightnessOverlayWidget::Initialize()
{
	const bool bSuperResult = Super::Initialize();
	if (!bSuperResult)
	{
		return false;
	}

	if (!OverlayImage)
	{
		UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
		WidgetTree->RootWidget = RootCanvas;

		OverlayImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BrightnessOverlayImage"));
		if (UTexture2D* WhiteTexture = LoadObject<UTexture2D>(nullptr, BrightnessWhiteSquarePath))
		{
			OverlayImage->SetBrushFromTexture(WhiteTexture);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[VIDEO] Failed to load white square texture: %s"), BrightnessWhiteSquarePath);
		}
		OverlayImage->SetColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.f));

		if (UCanvasPanelSlot* ChildSlot = RootCanvas->AddChildToCanvas(OverlayImage))
		{
			ChildSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			ChildSlot->SetOffsets(FMargin(0.f));
		}
	}

	// Never intercepts input -- this sits on top of literally everything, including gameplay, the
	// title/intro screens, and the pause menu itself, and none of those should ever have their
	// input eaten by a purely visual tint.
	SetVisibility(ESlateVisibility::HitTestInvisible);

	return true;
}

void UGlobalBrightnessOverlayWidget::SetBrightness(float Value)
{
	if (!OverlayImage)
	{
		return;
	}

	Value = FMath::Clamp(Value, 0.f, 1.f);

	if (Value < NeutralBrightness)
	{
		const float Alpha = (NeutralBrightness - Value) / NeutralBrightness * MaxDarkenOpacity;
		OverlayImage->SetColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, Alpha));
	}
	else
	{
		const float Alpha = (Value - NeutralBrightness) / (1.f - NeutralBrightness) * MaxLightenOpacity;
		OverlayImage->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, Alpha));
	}
}
