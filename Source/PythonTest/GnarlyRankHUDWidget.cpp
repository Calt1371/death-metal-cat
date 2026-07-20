#include "GnarlyRankHUDWidget.h"

#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Border.h"
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
	if (!RankText || !OwningCharacter)
	{
		return;
	}

	const int32 CurrentRank = OwningCharacter->GnarlyRank;
	const int32 CurrentHitCount = OwningCharacter->GnarlyHitCount;

	if (CurrentRank == LastSeenRank && CurrentHitCount == LastSeenHitCount)
	{
		return;
	}
	LastSeenRank = CurrentRank;
	LastSeenHitCount = CurrentHitCount;

	const int32 ClampedRank = FMath::Clamp(CurrentRank, 0, UE_ARRAY_COUNT(GnarlyRankLetters) - 1);
	const FString RankLetter = GnarlyRankLetters[ClampedRank];

	const TArray<int32>& Thresholds = OwningCharacter->GnarlyRankThresholds;
	const FString ProgressText = (CurrentRank < Thresholds.Num())
		? FString::Printf(TEXT(" (%d/%d)"), CurrentHitCount, Thresholds[CurrentRank])
		: TEXT(" (MAX)");

	// No "GNARLY:" prefix -- the logo image above now conveys that; this shows just the changing
	// part (e.g. "D (0/5)", "S (MAX)").
	RankText->SetText(FText::FromString(FString::Printf(TEXT("%s%s"), *RankLetter, *ProgressText)));

	// Portrait: direct 1:1 rank-to-texture mapping (all 5, ranks 0-4, exist -- no reuse needed).
	// Driven by the same CurrentRank read and the same "only update on actual change" gate above
	// as the rank text/meter -- not a separate/parallel update path.
	if (PortraitImage && RankPortraitTextures.IsValidIndex(ClampedRank) && RankPortraitTextures[ClampedRank])
	{
		PortraitImage->SetBrushFromTexture(RankPortraitTextures[ClampedRank]);
	}
}
