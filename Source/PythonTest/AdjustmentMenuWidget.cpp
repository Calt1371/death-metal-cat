#include "AdjustmentMenuWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Sound/SoundBase.h"
#include "DMCGameInstance.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	// Imported directly (no processing), same loose-file-at-Content-root convention as
	// Pause_screen_DMC.png / Coming_Soon.png.
	const TCHAR* BackgroundPath = TEXT("/Game/UI/AdjustmentMenu/T_AdjustmentMenu.T_AdjustmentMenu");
	const TCHAR* GunFirePreviewPath = TEXT("/Game/Audio/SFX/gun_fire.gun_fire");

	// The art's native resolution -- same technique as UPauseMenuWidget, and this background
	// (Adjustment_Menu.png) happens to share Pause_screen_DMC.png's exact 1672x941 dimensions.
	constexpr float NativeWidth = 1672.f;
	constexpr float NativeHeight = 941.f;

	// -- Column X positions, measured directly off Adjustment_Menu.png's baked-in icon/label art --
	constexpr float BrightnessColumnX = 385.f;
	constexpr float SFXColumnX = 830.f;
	constexpr float MusicColumnX = 1280.f;
	const float ColumnX[3] = { BrightnessColumnX, SFXColumnX, MusicColumnX };

	// Bar sits just under each column's baked-in label; value readout just under the bar; both
	// clear of the art's own "ADJUST TO YOUR PREFERENCE" caption lower still.
	constexpr float BarY = 815.f;
	constexpr float BarWidth = 300.f;
	constexpr float BarHeight = 26.f;
	constexpr float ValueTextY = 852.f;

	// Highlight box spans from just above each column's icon down through its value readout.
	constexpr float HighlightWidth = 340.f;
	constexpr float HighlightHeight = 210.f;
	constexpr float HighlightCenterY = 770.f;

	constexpr float ContinueHintY = 918.f;

	// How much one Up/Down press changes the selected slider's value -- 5% per press, 21 discrete
	// stops. Matches UPauseMenuWidget's OptionsAdjustStep.
	constexpr float AdjustStep = 0.05f;

	constexpr int32 ValueFontSize = 24;
	constexpr int32 HintFontSize = 22;

	// Same highlight colors as UPauseMenuWidget, for visual consistency between the two settings
	// screens.
	const FLinearColor HighlightFillColor(0.32f, 0.05f, 0.06f, 0.6f);
	const FLinearColor HighlightOutlineColor(0.92f, 0.26f, 0.16f, 1.f);
	constexpr float HighlightOutlineWidth = 3.f;
	constexpr float HighlightCornerRadius = 6.f;

	const FLinearColor BarFillColor(0.85f, 0.18f, 0.16f, 1.f);

	FSlateFontInfo MakeOutlinedFont(const FSlateFontInfo& BaseFont, int32 Size)
	{
		FSlateFontInfo Font = BaseFont;
		Font.Size = Size;
		Font.OutlineSettings.OutlineSize = 2;
		Font.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 1.f);
		return Font;
	}
}

bool UAdjustmentMenuWidget::Initialize()
{
	const bool bSuperResult = Super::Initialize();
	if (!bSuperResult)
	{
		return false;
	}

	if (!BackgroundImage)
	{
		UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
		WidgetTree->RootWidget = RootCanvas;

		UScaleBox* OuterScaleBox = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("OuterScaleBox"));
		OuterScaleBox->SetStretch(EStretch::ScaleToFit);
		if (UCanvasPanelSlot* ScaleBoxSlot = RootCanvas->AddChildToCanvas(OuterScaleBox))
		{
			ScaleBoxSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			ScaleBoxSlot->SetOffsets(FMargin(0.f));
		}

		USizeBox* NativeSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("NativeSizeBox"));
		NativeSizeBox->SetWidthOverride(NativeWidth);
		NativeSizeBox->SetHeightOverride(NativeHeight);
		OuterScaleBox->AddChild(NativeSizeBox);

		UCanvasPanel* ArtCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ArtCanvas"));
		NativeSizeBox->AddChild(ArtCanvas);

		// -- Background, filling the whole native canvas, added first so everything else paints on
		// top of it. --
		BackgroundImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("AdjustmentBackgroundImage"));
		if (UTexture2D* BgTexture = LoadObject<UTexture2D>(nullptr, BackgroundPath))
		{
			BackgroundImage->SetBrushFromTexture(BgTexture);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[ADJUSTMENT MENU] Failed to load background texture: %s"), BackgroundPath);
		}
		if (UCanvasPanelSlot* BgSlot = ArtCanvas->AddChildToCanvas(BackgroundImage))
		{
			BgSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			BgSlot->SetOffsets(FMargin(0.f));
		}

		// -- Highlight box, added before the bars/text so it paints under them. --
		HighlightBox = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HighlightBox"));
		HighlightBox->SetBrush(FSlateRoundedBoxBrush(HighlightFillColor, HighlightCornerRadius, HighlightOutlineColor, HighlightOutlineWidth));
		if (UCanvasPanelSlot* ChildSlot = ArtCanvas->AddChildToCanvas(HighlightBox))
		{
			ChildSlot->SetAnchors(FAnchors(0.f, 0.f));
			ChildSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			ChildSlot->SetSize(FVector2D(HighlightWidth, HighlightHeight));
			ChildSlot->SetPosition(FVector2D(ColumnX[0], HighlightCenterY));
			ChildSlot->SetAutoSize(false);
		}

		auto MakeBar = [this, ArtCanvas](const TCHAR* Name, float ColumnCenterX) -> UProgressBar*
		{
			UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), Name);
			Bar->SetFillColorAndOpacity(BarFillColor);
			if (UCanvasPanelSlot* ChildSlot = ArtCanvas->AddChildToCanvas(Bar))
			{
				ChildSlot->SetAnchors(FAnchors(0.f, 0.f));
				ChildSlot->SetAlignment(FVector2D(0.5f, 0.5f));
				ChildSlot->SetPosition(FVector2D(ColumnCenterX, BarY));
				ChildSlot->SetSize(FVector2D(BarWidth, BarHeight));
				ChildSlot->SetAutoSize(false);
			}
			return Bar;
		};

		auto MakeValueText = [this, ArtCanvas](const TCHAR* Name, float ColumnCenterX) -> UTextBlock*
		{
			UTextBlock* ValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
			ValueText->SetJustification(ETextJustify::Center);
			ValueText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			ValueText->SetFont(MakeOutlinedFont(ValueText->GetFont(), ValueFontSize));
			if (UCanvasPanelSlot* ChildSlot = ArtCanvas->AddChildToCanvas(ValueText))
			{
				ChildSlot->SetAnchors(FAnchors(0.f, 0.f));
				ChildSlot->SetAlignment(FVector2D(0.5f, 0.5f));
				ChildSlot->SetPosition(FVector2D(ColumnCenterX, ValueTextY));
				ChildSlot->SetAutoSize(true);
			}
			return ValueText;
		};

		BrightnessBar = MakeBar(TEXT("BrightnessBar"), BrightnessColumnX);
		BrightnessValueText = MakeValueText(TEXT("BrightnessValueText"), BrightnessColumnX);

		SFXBar = MakeBar(TEXT("SFXBar"), SFXColumnX);
		SFXValueText = MakeValueText(TEXT("SFXValueText"), SFXColumnX);

		MusicBar = MakeBar(TEXT("MusicBar"), MusicColumnX);
		MusicValueText = MakeValueText(TEXT("MusicValueText"), MusicColumnX);

		ContinueHintText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ContinueHintText"));
		ContinueHintText->SetText(FText::FromString(TEXT("PRESS ENTER / A TO CONTINUE")));
		ContinueHintText->SetJustification(ETextJustify::Center);
		ContinueHintText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ContinueHintText->SetFont(MakeOutlinedFont(ContinueHintText->GetFont(), HintFontSize));
		if (UCanvasPanelSlot* ChildSlot = ArtCanvas->AddChildToCanvas(ContinueHintText))
		{
			ChildSlot->SetAnchors(FAnchors(0.f, 0.f));
			ChildSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			ChildSlot->SetPosition(FVector2D(NativeWidth / 2.f, ContinueHintY));
			ChildSlot->SetAutoSize(true);
		}

		GunFirePreviewSound = LoadObject<USoundBase>(nullptr, GunFirePreviewPath);
		if (!GunFirePreviewSound)
		{
			UE_LOG(LogTemp, Error, TEXT("[ADJUSTMENT MENU] Failed to load SFX preview sound: %s"), GunFirePreviewPath);
		}
	}

	RefreshDisplay();

	return true;
}

void UAdjustmentMenuWidget::RefreshDisplay()
{
	const UWorld* World = GetWorld();
	const UDMCGameInstance* GameInstance = World ? Cast<UDMCGameInstance>(World->GetGameInstance()) : nullptr;
	if (!GameInstance)
	{
		return;
	}

	const float Brightness = GameInstance->GetBrightness();
	const float SFXVolume = GameInstance->GetSFXVolume();
	const float MusicVolume = GameInstance->GetMusicVolume();

	if (BrightnessBar) BrightnessBar->SetPercent(Brightness);
	if (BrightnessValueText) BrightnessValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Brightness * 100.f))));

	if (SFXBar) SFXBar->SetPercent(SFXVolume);
	if (SFXValueText) SFXValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(SFXVolume * 100.f))));

	if (MusicBar) MusicBar->SetPercent(MusicVolume);
	if (MusicValueText) MusicValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(MusicVolume * 100.f))));

	if (HighlightBox)
	{
		if (UCanvasPanelSlot* ChildSlot = Cast<UCanvasPanelSlot>(HighlightBox->Slot))
		{
			ChildSlot->SetPosition(FVector2D(ColumnX[SelectedIndex], HighlightCenterY));
		}
	}
}

void UAdjustmentMenuWidget::NavigateUp()
{
	AdjustSelectedValue(AdjustStep);
}

void UAdjustmentMenuWidget::NavigateDown()
{
	AdjustSelectedValue(-AdjustStep);
}

void UAdjustmentMenuWidget::NavigateLeft()
{
	SelectedIndex = (SelectedIndex + 2) % 3;
	RefreshDisplay();
}

void UAdjustmentMenuWidget::NavigateRight()
{
	SelectedIndex = (SelectedIndex + 1) % 3;
	RefreshDisplay();
}

void UAdjustmentMenuWidget::AdjustSelectedValue(float Delta)
{
	UWorld* World = GetWorld();
	UDMCGameInstance* GameInstance = World ? Cast<UDMCGameInstance>(World->GetGameInstance()) : nullptr;
	if (!GameInstance)
	{
		return;
	}

	switch (SelectedIndex)
	{
	case 0:
		GameInstance->SetBrightness(GameInstance->GetBrightness() + Delta);
		break;
	case 1:
		GameInstance->SetSFXVolume(this, GameInstance->GetSFXVolume() + Delta);
		// Live preview -- see class comment. A one-shot, so no orphaned-sound risk (that only
		// applies to looping sounds with no owning AudioComponent).
		if (GunFirePreviewSound)
		{
			UGameplayStatics::PlaySound2D(this, GunFirePreviewSound);
		}
		break;
	case 2:
		GameInstance->SetMusicVolume(this, GameInstance->GetMusicVolume() + Delta);
		break;
	default:
		break;
	}

	RefreshDisplay();
}
