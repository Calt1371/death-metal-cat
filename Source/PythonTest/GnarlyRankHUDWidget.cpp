#include "GnarlyRankHUDWidget.h"

#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "DeathMetalCatCharacter.h"

namespace
{
	// GnarlyRank 0 (no rank yet) through 4 (max, matching the default 4-entry GnarlyRankThresholds).
	const TCHAR* GnarlyRankLetters[] = { TEXT("D"), TEXT("C"), TEXT("B"), TEXT("A"), TEXT("S") };

	// One portrait per rank, direct 1:1 mapping -- all 5 (ranks 0-4) exist, no reuse/clamping needed.
	const TCHAR* GnarlyPortraitPaths[] = {
		TEXT("/Game/UI/GnarlyRank/T_GnarlyRank_0.T_GnarlyRank_0"),
		TEXT("/Game/UI/GnarlyRank/T_GnarlyRank_1.T_GnarlyRank_1"),
		TEXT("/Game/UI/GnarlyRank/T_GnarlyRank_2.T_GnarlyRank_2"),
		TEXT("/Game/UI/GnarlyRank/T_GnarlyRank_3.T_GnarlyRank_3"),
		TEXT("/Game/UI/GnarlyRank/T_GnarlyRank_4.T_GnarlyRank_4"),
	};

	const TCHAR* GnarlyLogoPath = TEXT("/Game/UI/GnarlyRank/T_GnarlyRank_Logo.T_GnarlyRank_Logo");

	// Engine-provided 1x1 white texture -- tinted via UImage::SetColorAndOpacity to get a plain
	// solid-color fill (health bar background/fill and the full-screen low-health tint) without
	// needing a dedicated texture asset of our own.
	const TCHAR* WhiteSquarePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");

	// Anchor point for the health-bar color gradient: Health% == this shows pure Yellow, blending
	// toward Green above it and Red below it -- a smooth two-segment lerp, not a hard color-band
	// snap (there's no discrete jump at any percentage, just a change in slope at this midpoint).
	constexpr float HealthBarMidPercent = 0.5f;

	FLinearColor LerpHealthBarColor(float HealthPercent)
	{
		const FLinearColor Green(0.15f, 0.8f, 0.2f);
		const FLinearColor Yellow(0.95f, 0.85f, 0.1f);
		const FLinearColor Red(0.85f, 0.1f, 0.1f);

		if (HealthPercent >= HealthBarMidPercent)
		{
			const float Alpha = (HealthPercent - HealthBarMidPercent) / (1.f - HealthBarMidPercent);
			return FMath::Lerp(Yellow, Green, Alpha);
		}

		const float Alpha = HealthPercent / HealthBarMidPercent;
		return FMath::Lerp(Red, Yellow, Alpha);
	}
}

bool UGnarlyRankHUDWidget::Initialize()
{
	const bool bSuperResult = Super::Initialize();
	if (!bSuperResult)
	{
		return false;
	}

	// Built here, not in NativeConstruct -- see this class's header comment for why.
	if (!RankText)
	{
		UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
		WidgetTree->RootWidget = RootCanvas;

		// Low-health tint: added FIRST, before anything else below, so every other element on this
		// HUD paints on top of it instead of being obscured -- canvas children later in add-order
		// render on top of earlier ones. Stretched to fill the whole screen (anchors 0,0 to 1,1,
		// zero offsets) rather than a fixed position/size like everything else here. Starts fully
		// transparent (alpha 0); RefreshDisplay drives its opacity from Health.
		LowHealthTintImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("LowHealthTintImage"));
		if (UTexture2D* WhiteTexture = LoadObject<UTexture2D>(nullptr, WhiteSquarePath))
		{
			LowHealthTintImage->SetBrushFromTexture(WhiteTexture);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[GNARLY RANK] Failed to load low-health tint texture: %s"), WhiteSquarePath);
		}
		LowHealthTintImage->SetColorAndOpacity(FLinearColor(0.4f, 0.02f, 0.02f, 0.f));

		if (UCanvasPanelSlot* TintSlot = RootCanvas->AddChildToCanvas(LowHealthTintImage))
		{
			TintSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			TintSlot->SetOffsets(FMargin(0.f));
		}

		// Static "GNARLY RANK" logo, above the dynamic text. Own UImage, not shared with
		// PortraitImage -- set once here and never touched again in RefreshDisplay, unlike the
		// portrait (which does change with rank). UImage's default Stretch (ScaleToFit) preserves
		// the source's own aspect ratio inside the fixed 240x120 box below, rather than
		// stretching/squashing it to match exactly.
		LogoImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("GnarlyLogoImage"));

		UTexture2D* LogoTexture = LoadObject<UTexture2D>(nullptr, GnarlyLogoPath);
		if (LogoTexture)
		{
			LogoImage->SetBrushFromTexture(LogoTexture);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[GNARLY RANK] Failed to load logo texture: %s"), GnarlyLogoPath);
		}

		if (UCanvasPanelSlot* LogoSlot = RootCanvas->AddChildToCanvas(LogoImage))
		{
			LogoSlot->SetAnchors(FAnchors(0.f, 0.f));
			LogoSlot->SetAlignment(FVector2D(0.f, 0.f));
			LogoSlot->SetPosition(FVector2D(20.f, 10.f));
			LogoSlot->SetSize(FVector2D(240.f, 120.f));
			LogoSlot->SetAutoSize(false);
		}

		// Rank text, moved below the logo (was previously the topmost element at Y=30 before the
		// logo was added above it).
		RankText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GnarlyRankText"));
		FSlateFontInfo Font = RankText->GetFont();
		Font.Size = 28;
		RankText->SetFont(Font);
		RankText->SetColorAndOpacity(FSlateColor(FLinearColor::White));

		if (UCanvasPanelSlot* RankTextSlot = RootCanvas->AddChildToCanvas(RankText))
		{
			RankTextSlot->SetAnchors(FAnchors(0.f, 0.f));
			RankTextSlot->SetAlignment(FVector2D(0.f, 0.f));
			RankTextSlot->SetPosition(FVector2D(30.f, 140.f));
			RankTextSlot->SetAutoSize(true);
		}

		// Portrait, framed in a simple dark UBorder, positioned to the right of the rank text
		// (moved down to the same row as RankText's new position, alongside it).
		// PortraitImage is nested as the border's Content (not a bare canvas child) -- SetContent()
		// is the correct, standard way to parent a child into a UContentWidget's slot, same as
		// WidgetTree->RootWidget/AddChildToCanvas above; this is the "child of a panel" attachment
		// pattern the DamageNumberWidget bug report specifically called out.
		UBorder* PortraitBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("GnarlyPortraitBorder"));
		PortraitBorder->SetBrushColor(FLinearColor(0.1f, 0.1f, 0.1f, 0.9f));
		PortraitBorder->SetPadding(FMargin(4.f));

		PortraitImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("GnarlyPortraitImage"));
		PortraitBorder->SetContent(PortraitImage);

		if (UCanvasPanelSlot* PortraitSlot = RootCanvas->AddChildToCanvas(PortraitBorder))
		{
			PortraitSlot->SetAnchors(FAnchors(0.f, 0.f));
			PortraitSlot->SetAlignment(FVector2D(0.f, 0.f));
			PortraitSlot->SetPosition(FVector2D(250.f, 135.f));
			PortraitSlot->SetSize(FVector2D(88.f, 88.f));
			PortraitSlot->SetAutoSize(false);
		}

		// Loaded once here (not per-refresh) -- one LoadObject per rank, direct 1:1 mapping.
		RankPortraitTextures.SetNum(UE_ARRAY_COUNT(GnarlyPortraitPaths));
		for (int32 i = 0; i < UE_ARRAY_COUNT(GnarlyPortraitPaths); ++i)
		{
			RankPortraitTextures[i] = LoadObject<UTexture2D>(nullptr, GnarlyPortraitPaths[i]);
			if (!RankPortraitTextures[i])
			{
				UE_LOG(LogTemp, Error, TEXT("[GNARLY RANK] Failed to load portrait texture: %s"), GnarlyPortraitPaths[i]);
			}
		}

		// Level/XP readout, below the rank text/portrait row -- reuses this same HUD widget rather
		// than a separate one, per design.
		LevelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LevelText"));
		FSlateFontInfo LevelFont = LevelText->GetFont();
		LevelFont.Size = 20;
		LevelText->SetFont(LevelFont);
		LevelText->SetColorAndOpacity(FSlateColor(FLinearColor::White));

		if (UCanvasPanelSlot* LevelTextSlot = RootCanvas->AddChildToCanvas(LevelText))
		{
			LevelTextSlot->SetAnchors(FAnchors(0.f, 0.f));
			LevelTextSlot->SetAlignment(FVector2D(0.f, 0.f));
			LevelTextSlot->SetPosition(FVector2D(30.f, 230.f));
			LevelTextSlot->SetAutoSize(true);
		}

		// Health bar, below the Level/XP row -- reuses this same HUD widget, same as Level/XP does.
		HealthBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("HealthBar"));
		HealthBar->SetPercent(1.f);

		if (UCanvasPanelSlot* HealthBarSlot = RootCanvas->AddChildToCanvas(HealthBar))
		{
			HealthBarSlot->SetAnchors(FAnchors(0.f, 0.f));
			HealthBarSlot->SetAlignment(FVector2D(0.f, 0.f));
			HealthBarSlot->SetPosition(FVector2D(30.f, 270.f));
			HealthBarSlot->SetSize(FVector2D(220.f, 24.f));
			HealthBarSlot->SetAutoSize(false);
		}

		// Numeric readout, to the right of the bar rather than overlapping it.
		HealthText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HealthText"));
		FSlateFontInfo HealthFont = HealthText->GetFont();
		HealthFont.Size = 20;
		HealthText->SetFont(HealthFont);
		HealthText->SetColorAndOpacity(FSlateColor(FLinearColor::White));

		if (UCanvasPanelSlot* HealthTextSlot = RootCanvas->AddChildToCanvas(HealthText))
		{
			HealthTextSlot->SetAnchors(FAnchors(0.f, 0.f));
			HealthTextSlot->SetAlignment(FVector2D(0.f, 0.f));
			HealthTextSlot->SetPosition(FVector2D(260.f, 270.f));
			HealthTextSlot->SetAutoSize(true);
		}
	}

	RefreshDisplay();
	return true;
}

void UGnarlyRankHUDWidget::SetOwningCharacter(ADeathMetalCatCharacter* InCharacter)
{
	OwningCharacter = InCharacter;
	RefreshDisplay();
}

void UGnarlyRankHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshDisplay();
}

void UGnarlyRankHUDWidget::RefreshDisplay()
{
	if (!OwningCharacter)
	{
		return;
	}

	// -- Gnarly rank text + portrait --
	const int32 CurrentRank = OwningCharacter->GnarlyRank;
	const int32 CurrentHitCount = OwningCharacter->GnarlyHitCount;

	if (RankText && (CurrentRank != LastSeenRank || CurrentHitCount != LastSeenHitCount))
	{
		LastSeenRank = CurrentRank;
		LastSeenHitCount = CurrentHitCount;

		const int32 ClampedRank = FMath::Clamp(CurrentRank, 0, UE_ARRAY_COUNT(GnarlyRankLetters) - 1);
		const FString RankLetter = GnarlyRankLetters[ClampedRank];

		const TArray<int32>& Thresholds = OwningCharacter->GnarlyRankThresholds;
		const FString ProgressText = (CurrentRank < Thresholds.Num())
			? FString::Printf(TEXT(" (%d/%d)"), CurrentHitCount, Thresholds[CurrentRank])
			: TEXT(" (MAX)");

		// No "GNARLY:" prefix -- the logo image above now conveys that; this shows just the
		// changing part (e.g. "D (0/5)", "S (MAX)").
		RankText->SetText(FText::FromString(FString::Printf(TEXT("%s%s"), *RankLetter, *ProgressText)));

		// Portrait: direct 1:1 rank-to-texture mapping (all 5, ranks 0-4, exist -- no reuse needed).
		if (PortraitImage && RankPortraitTextures.IsValidIndex(ClampedRank) && RankPortraitTextures[ClampedRank])
		{
			PortraitImage->SetBrushFromTexture(RankPortraitTextures[ClampedRank]);
		}
	}

	// -- Level/XP -- gated independently of GnarlyRank/GnarlyHitCount above, since the two change
	// on unrelated triggers (kills/hits vs. XP awards), but still driven from this same single
	// polling function, not a separate/parallel update mechanism.
	const int32 CurrentLevel = OwningCharacter->CurrentLevel;
	const float CurrentXPValue = OwningCharacter->CurrentXP;

	if (LevelText && (CurrentLevel != LastSeenLevel || !FMath::IsNearlyEqual(CurrentXPValue, LastSeenXP)))
	{
		LastSeenLevel = CurrentLevel;
		LastSeenXP = CurrentXPValue;

		const FString XPText = (CurrentLevel < OwningCharacter->MaxLevel)
			? FString::Printf(TEXT("%d/%d XP"), FMath::RoundToInt(CurrentXPValue), FMath::RoundToInt(OwningCharacter->XPToNextLevel))
			: TEXT("MAX");

		LevelText->SetText(FText::FromString(FString::Printf(TEXT("LVL %d (%s)"), CurrentLevel, *XPText)));
	}

	// -- Health bar / text / low-health tint -- all three gated together since all three derive
	// from these same two source values, unlike GnarlyRank/Level above which change independently.
	const float CurrentHealth = OwningCharacter->Health;
	const float CurrentMaxHealth = OwningCharacter->MaxHealth;

	if (!FMath::IsNearlyEqual(CurrentHealth, LastSeenHealth) || !FMath::IsNearlyEqual(CurrentMaxHealth, LastSeenMaxHealth))
	{
		LastSeenHealth = CurrentHealth;
		LastSeenMaxHealth = CurrentMaxHealth;

		const float HealthPercent = (CurrentMaxHealth > 0.f) ? FMath::Clamp(CurrentHealth / CurrentMaxHealth, 0.f, 1.f) : 0.f;

		if (HealthBar)
		{
			HealthBar->SetPercent(HealthPercent);
			HealthBar->SetFillColorAndOpacity(LerpHealthBarColor(HealthPercent));
		}

		if (HealthText)
		{
			HealthText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), FMath::RoundToInt(CurrentHealth), FMath::RoundToInt(CurrentMaxHealth))));
		}

		if (LowHealthTintImage)
		{
			const float LowHealthThreshold = OwningCharacter->LowHealthThreshold;

			// 0 opacity right at the threshold, ramping linearly to LowHealthTintMaxOpacity at
			// Health == 0 -- a smooth fade, not a hard snap-on, and it fades back out the same way
			// as Health recovers back above the threshold.
			float TintOpacity = 0.f;
			if (LowHealthThreshold > 0.f && HealthPercent < LowHealthThreshold)
			{
				TintOpacity = (1.f - HealthPercent / LowHealthThreshold) * OwningCharacter->LowHealthTintMaxOpacity;
			}

			const FLinearColor CurrentTint = LowHealthTintImage->GetColorAndOpacity();
			LowHealthTintImage->SetColorAndOpacity(FLinearColor(CurrentTint.R, CurrentTint.G, CurrentTint.B, TintOpacity));
		}
	}
}
