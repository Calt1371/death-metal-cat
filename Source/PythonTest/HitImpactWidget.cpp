#include "HitImpactWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"

namespace
{
	// Same engine-provided 1x1 white texture every other tinted-overlay in this project uses,
	// tinted per spark via SetColorAndOpacity rather than needing a dedicated art asset.
	const TCHAR* HitImpactWhiteSquarePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");

	// Six sparks, evenly spaced around the full circle -- reads as a radiating burst rather than a
	// directional slash, which is correct here since sword hits land from every angle/combo variant
	// (ground combo, Uppy, Double Whammy, Spinny Down all share this same effect).
	constexpr int32 SparkCount = 6;
	constexpr float SparkWidth = 8.f;
	constexpr float SparkHeight = 46.f;

	// The two ends of the scale-up: sparks start small and near the point of impact, then shoot
	// outward as they fade -- the "burst" read comes entirely from this growth, not from movement.
	constexpr float SparkStartScale = 0.35f;
	constexpr float SparkEndScale = 1.35f;
}

bool UHitImpactWidget::Initialize()
{
	const bool bSuperResult = Super::Initialize();
	if (!bSuperResult)
	{
		return false;
	}

	if (Sparks.Num() == 0)
	{
		UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
		WidgetTree->RootWidget = RootCanvas;

		UTexture2D* WhiteTexture = LoadObject<UTexture2D>(nullptr, HitImpactWhiteSquarePath);

		for (int32 Index = 0; Index < SparkCount; ++Index)
		{
			UImage* Spark = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), *FString::Printf(TEXT("Spark%d"), Index));
			if (WhiteTexture)
			{
				Spark->SetBrushFromTexture(WhiteTexture);
			}

			if (UCanvasPanelSlot* ChildSlot = RootCanvas->AddChildToCanvas(Spark))
			{
				ChildSlot->SetAnchors(FAnchors(0.5f, 0.5f));
				ChildSlot->SetAlignment(FVector2D(0.5f, 0.5f));
				ChildSlot->SetPosition(FVector2D(0.f, 0.f));
				ChildSlot->SetSize(FVector2D(SparkWidth, SparkHeight));
				ChildSlot->SetAutoSize(false);
			}

			// Fixed for this spark's whole lifetime -- only scale/opacity move, driven per-frame from
			// UpdateImpactAlpha.
			Spark->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
			Spark->SetRenderTransformAngle(Index * (360.f / SparkCount));

			Sparks.Add(Spark);
		}
	}

	return true;
}

void UHitImpactWidget::InitHitImpact(EDamageTier Tier)
{
	// Same three-color convention as UDamageNumberWidget::ApplyDamageText -- a Critical sword hit
	// bursts red, a Weakness hit bursts gold, everything else a plain hot white-yellow flash.
	FLinearColor Color = FLinearColor(1.f, 0.92f, 0.7f); // warm white -- reads as a hot spark
	switch (Tier)
	{
	case EDamageTier::Weakness:
		Color = FLinearColor(1.f, 0.85f, 0.1f); // yellow/gold
		break;
	case EDamageTier::Critical:
		Color = FLinearColor(1.f, 0.25f, 0.15f); // red
		break;
	case EDamageTier::Normal:
	default:
		break;
	}

	for (UImage* Spark : Sparks)
	{
		if (Spark)
		{
			Spark->SetColorAndOpacity(Color);
			Spark->SetRenderScale(FVector2D(SparkStartScale, SparkStartScale));
			Spark->SetRenderOpacity(1.f);
		}
	}
}

void UHitImpactWidget::UpdateImpactAlpha(float Alpha)
{
	Alpha = FMath::Clamp(Alpha, 0.f, 1.f);

	// Ease-out (decelerating growth) reads as a punchier, more "snappy" burst than a linear scale
	// ramp would -- most of the growth happens immediately, then it settles.
	const float EasedAlpha = 1.f - FMath::Pow(1.f - Alpha, 3.f);
	const float Scale = FMath::Lerp(SparkStartScale, SparkEndScale, EasedAlpha);
	const float Opacity = 1.f - Alpha;

	for (UImage* Spark : Sparks)
	{
		if (Spark)
		{
			Spark->SetRenderScale(FVector2D(Scale, Scale));
			Spark->SetRenderOpacity(Opacity);
		}
	}
}
