#include "PauseMenuWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "GameplayPlayerController.h"
#include "DeathMetalCatCharacter.h"
#include "DMCGameInstance.h"

namespace
{
	// Imported directly (no processing) by AgentScripts/ue_create_pause_screen_assets.py --
	// Pause_screen_DMC.png leaves the whole panel below the PAUSED title/divider blank. All three
	// pages share this same texture and draw their own content into that empty panel area.
	const TCHAR* PauseBackgroundPath = TEXT("/Game/UI/PauseMenu/T_PauseScreen.T_PauseScreen");

	// The art's native resolution -- every position/size constant below is in this coordinate space;
	// see class comment for the ScaleBox/SizeBox technique that maps it onto any actual screen size.
	constexpr float NativeWidth = 1672.f;
	constexpr float NativeHeight = 941.f;

	// -- Sub-pages (Options/Controls) and the main list both live inside this same blank panel --
	// measured directly from the current art (Pause_screen_DMC.png v2, which leaves the whole
	// panel below the PAUSED title/divider empty rather than baking any menu content into it, so
	// this project no longer erases/patches anything -- see the asset script). --
	constexpr float ContentLeft = 35.f;
	constexpr float ContentRight = 600.f;
	constexpr float ContentTop = 320.f;
	constexpr float ContentBottom = 900.f;
	constexpr float ContentWidth = ContentRight - ContentLeft;

	// -- Main page: a compact 4-row list near the top of that panel, not spread across its full
	// height -- matches the ORIGINAL mockup's proportions (a tight list, not a sparse one), the
	// panel is just taller now than the list needs to be. --
	constexpr float ListBoxLeft = 70.f;
	constexpr float ListBoxWidth = 480.f;
	constexpr float ListBoxHeight = 70.f;
	constexpr float ListBoxCenterX = ListBoxLeft + ListBoxWidth / 2.f;
	const float MainRowY[4] = { 400.f, 490.f, 580.f, 670.f }; // Resume, Options, Controls, Quit To Title
	const TCHAR* MainRowLabels[4] = { TEXT("RESUME"), TEXT("OPTIONS"), TEXT("CONTROLS"), TEXT("QUIT TO TITLE") };
	constexpr float ArrowOffsetFromBox = 27.f;

	constexpr float OptionsSoundRowY = 440.f;
	constexpr float OptionsBrightnessRowY = 590.f;
	constexpr float OptionsLabelX = 70.f;
	constexpr float OptionsBarX = 70.f;
	constexpr float OptionsBarWidth = 420.f;
	constexpr float OptionsBarHeight = 32.f;
	constexpr float OptionsValueX = 500.f;
	constexpr float OptionsValueWidth = 80.f;
	constexpr float OptionsHighlightHeight = 92.f;

	// How much one Left/Right press changes Sound/Brightness -- 5% per press, 21 discrete stops.
	constexpr float OptionsAdjustStep = 0.05f;

	// -- Shared text/box styling --
	constexpr int32 MainLabelFontSize = 30;
	constexpr int32 OptionsLabelFontSize = 26;
	constexpr int32 OptionsValueFontSize = 24;
	constexpr int32 ControlsFontSize = 21;
	constexpr int32 ArrowFontSize = 28;

	// Highlight box colors, approximating the source art's own RESUME highlight (sampled: fill
	// ~(24,8,10)-(41,12,15), border ~(219,56,44)) but a little more saturated/opaque than the raw
	// sample, since the fill needs to read clearly against the now-flat-black cleared panel rather
	// than blend with whatever detail used to be under it.
	const FLinearColor HighlightFillColor(0.32f, 0.05f, 0.06f, 0.6f);
	const FLinearColor HighlightOutlineColor(0.92f, 0.26f, 0.16f, 1.f);
	constexpr float HighlightOutlineWidth = 3.f;
	constexpr float HighlightCornerRadius = 6.f;

	const FLinearColor OptionsBarFillColor(0.85f, 0.18f, 0.16f, 1.f);

	FSlateFontInfo MakeOutlinedFont(const FSlateFontInfo& BaseFont, int32 Size)
	{
		FSlateFontInfo Font = BaseFont;
		Font.Size = Size;
		Font.OutlineSettings.OutlineSize = 2;
		Font.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 1.f);
		return Font;
	}
}

bool UPauseMenuWidget::Initialize()
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

		// Fixed-size (1672x941) content, scaled as a whole to fit any actual screen size -- same
		// "fixed native-resolution canvas inside a ScaleBox" technique UFullscreenVideoWidgetBase
		// uses for its video, via a SizeBox instead of a brush's desired-size override since there's
		// no single texture whose size to hook here.
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
		BackgroundImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PauseBackgroundImage"));
		if (UTexture2D* BgTexture = LoadObject<UTexture2D>(nullptr, PauseBackgroundPath))
		{
			BackgroundImage->SetBrushFromTexture(BgTexture);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[PAUSE] Failed to load background texture: %s"), PauseBackgroundPath);
		}
		if (UCanvasPanelSlot* BgSlot = ArtCanvas->AddChildToCanvas(BackgroundImage))
		{
			BgSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			BgSlot->SetOffsets(FMargin(0.f));
		}

		BuildMainPageWidgets(ArtCanvas);
		BuildOptionsPageWidgets(ArtCanvas);
		BuildControlsPageWidgets(ArtCanvas);
	}

	// Never given Slate focus/hit-testing -- see class comment. Hidden until ShowMainPage() opens it.
	SetVisibility(ESlateVisibility::Collapsed);

	return true;
}

void UPauseMenuWidget::BuildMainPageWidgets(UCanvasPanel* ArtCanvas)
{
	// Highlight box added BEFORE the labels so the labels paint on top of its fill, not under it.
	MainHighlightBox = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MainHighlightBox"));
	MainHighlightBox->SetBrush(FSlateRoundedBoxBrush(HighlightFillColor, HighlightCornerRadius, HighlightOutlineColor, HighlightOutlineWidth));
	if (UCanvasPanelSlot* ChildSlot = ArtCanvas->AddChildToCanvas(MainHighlightBox))
	{
		ChildSlot->SetAnchors(FAnchors(0.f, 0.f));
		ChildSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		ChildSlot->SetSize(FVector2D(ListBoxWidth, ListBoxHeight));
		ChildSlot->SetPosition(FVector2D(ListBoxCenterX, MainRowY[0]));
		ChildSlot->SetAutoSize(false);
	}

	for (int32 Index = 0; Index < 4; ++Index)
	{
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *FString::Printf(TEXT("MainLabel%d"), Index));
		Label->SetText(FText::FromString(MainRowLabels[Index]));
		Label->SetJustification(ETextJustify::Center);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Label->SetFont(MakeOutlinedFont(Label->GetFont(), MainLabelFontSize));
		if (UCanvasPanelSlot* ChildSlot = ArtCanvas->AddChildToCanvas(Label))
		{
			ChildSlot->SetAnchors(FAnchors(0.f, 0.f));
			ChildSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			ChildSlot->SetPosition(FVector2D(ListBoxCenterX, MainRowY[Index]));
			ChildSlot->SetAutoSize(true);
		}
		MainLabels.Add(Label);
	}

	MainArrowLeft = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MainArrowLeft"));
	MainArrowLeft->SetText(FText::FromString(TEXT("<")));
	MainArrowLeft->SetColorAndOpacity(FSlateColor(HighlightOutlineColor));
	MainArrowLeft->SetFont(MakeOutlinedFont(MainArrowLeft->GetFont(), ArrowFontSize));
	if (UCanvasPanelSlot* ChildSlot = ArtCanvas->AddChildToCanvas(MainArrowLeft))
	{
		ChildSlot->SetAnchors(FAnchors(0.f, 0.f));
		ChildSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		ChildSlot->SetPosition(FVector2D(ListBoxLeft - ArrowOffsetFromBox, MainRowY[0]));
		ChildSlot->SetAutoSize(true);
	}

	MainArrowRight = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MainArrowRight"));
	MainArrowRight->SetText(FText::FromString(TEXT(">")));
	MainArrowRight->SetColorAndOpacity(FSlateColor(HighlightOutlineColor));
	MainArrowRight->SetFont(MakeOutlinedFont(MainArrowRight->GetFont(), ArrowFontSize));
	if (UCanvasPanelSlot* ChildSlot = ArtCanvas->AddChildToCanvas(MainArrowRight))
	{
		ChildSlot->SetAnchors(FAnchors(0.f, 0.f));
		ChildSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		ChildSlot->SetPosition(FVector2D(ListBoxLeft + ListBoxWidth + ArrowOffsetFromBox, MainRowY[0]));
		ChildSlot->SetAutoSize(true);
	}
}

void UPauseMenuWidget::BuildOptionsPageWidgets(UCanvasPanel* ArtCanvas)
{
	OptionsHighlightBox = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("OptionsHighlightBox"));
	OptionsHighlightBox->SetBrush(FSlateRoundedBoxBrush(HighlightFillColor, HighlightCornerRadius, HighlightOutlineColor, HighlightOutlineWidth));
	if (UCanvasPanelSlot* ChildSlot = ArtCanvas->AddChildToCanvas(OptionsHighlightBox))
	{
		ChildSlot->SetAnchors(FAnchors(0.f, 0.f));
		ChildSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		ChildSlot->SetSize(FVector2D(ContentWidth, OptionsHighlightHeight));
		ChildSlot->SetPosition(FVector2D(ContentLeft + ContentWidth / 2.f, OptionsSoundRowY));
		ChildSlot->SetAutoSize(false);
	}

	auto MakeRowLabel = [this, ArtCanvas](const TCHAR* Name, const TCHAR* Text, float RowY) -> UTextBlock*
	{
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Label->SetText(FText::FromString(Text));
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Label->SetFont(MakeOutlinedFont(Label->GetFont(), OptionsLabelFontSize));
		if (UCanvasPanelSlot* ChildSlot = ArtCanvas->AddChildToCanvas(Label))
		{
			ChildSlot->SetAnchors(FAnchors(0.f, 0.f));
			ChildSlot->SetAlignment(FVector2D(0.f, 1.f));
			ChildSlot->SetPosition(FVector2D(OptionsLabelX, RowY - OptionsBarHeight / 2.f - 4.f));
			ChildSlot->SetAutoSize(true);
		}
		return Label;
	};

	auto MakeRowBar = [this, ArtCanvas](const TCHAR* Name, float RowY) -> UProgressBar*
	{
		UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), Name);
		Bar->SetFillColorAndOpacity(OptionsBarFillColor);
		if (UCanvasPanelSlot* ChildSlot = ArtCanvas->AddChildToCanvas(Bar))
		{
			ChildSlot->SetAnchors(FAnchors(0.f, 0.f));
			ChildSlot->SetAlignment(FVector2D(0.f, 0.5f));
			ChildSlot->SetPosition(FVector2D(OptionsBarX, RowY));
			ChildSlot->SetSize(FVector2D(OptionsBarWidth, OptionsBarHeight));
			ChildSlot->SetAutoSize(false);
		}
		return Bar;
	};

	auto MakeRowValueText = [this, ArtCanvas](const TCHAR* Name, float RowY) -> UTextBlock*
	{
		UTextBlock* ValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		ValueText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ValueText->SetFont(MakeOutlinedFont(ValueText->GetFont(), OptionsValueFontSize));
		if (UCanvasPanelSlot* ChildSlot = ArtCanvas->AddChildToCanvas(ValueText))
		{
			ChildSlot->SetAnchors(FAnchors(0.f, 0.f));
			ChildSlot->SetAlignment(FVector2D(0.f, 0.5f));
			ChildSlot->SetPosition(FVector2D(OptionsValueX, RowY));
			ChildSlot->SetAutoSize(true);
		}
		return ValueText;
	};

	SoundLabel = MakeRowLabel(TEXT("SoundLabel"), TEXT("SOUND"), OptionsSoundRowY);
	SoundBar = MakeRowBar(TEXT("SoundBar"), OptionsSoundRowY);
	SoundValueText = MakeRowValueText(TEXT("SoundValueText"), OptionsSoundRowY);

	BrightnessLabel = MakeRowLabel(TEXT("BrightnessLabel"), TEXT("BRIGHTNESS"), OptionsBrightnessRowY);
	BrightnessBar = MakeRowBar(TEXT("BrightnessBar"), OptionsBrightnessRowY);
	BrightnessValueText = MakeRowValueText(TEXT("BrightnessValueText"), OptionsBrightnessRowY);
}

void UPauseMenuWidget::BuildControlsPageWidgets(UCanvasPanel* ArtCanvas)
{
	ControlsScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ControlsScrollBox"));
	if (UCanvasPanelSlot* ChildSlot = ArtCanvas->AddChildToCanvas(ControlsScrollBox))
	{
		ChildSlot->SetAnchors(FAnchors(0.f, 0.f));
		ChildSlot->SetAlignment(FVector2D(0.f, 0.f));
		ChildSlot->SetPosition(FVector2D(ContentLeft + 5.f, ContentTop + 5.f));
		ChildSlot->SetSize(FVector2D(ContentWidth - 10.f, ContentBottom - ContentTop - 10.f));
		ChildSlot->SetAutoSize(false);
	}

	ControlsText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ControlsText"));
	ControlsText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	ControlsText->SetFont(MakeOutlinedFont(ControlsText->GetFont(), ControlsFontSize));
	ControlsText->SetAutoWrapText(true);
	ControlsScrollBox->AddChild(ControlsText);
}

void UPauseMenuWidget::SetOwningController(AGameplayPlayerController* InController)
{
	OwningController = InController;
}

void UPauseMenuWidget::ShowMainPage()
{
	CurrentPage = EPausePage::Main;
	MainSelectedIndex = 0;
	RefreshMainHighlight();
	RefreshPageVisibility();
}

void UPauseMenuWidget::RefreshPageVisibility()
{
	const ESlateVisibility MainVis = (CurrentPage == EPausePage::Main) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	const ESlateVisibility OptionsVis = (CurrentPage == EPausePage::Options) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	const ESlateVisibility ControlsVis = (CurrentPage == EPausePage::Controls) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;

	if (MainHighlightBox) MainHighlightBox->SetVisibility(MainVis);
	if (MainArrowLeft) MainArrowLeft->SetVisibility(MainVis);
	if (MainArrowRight) MainArrowRight->SetVisibility(MainVis);
	for (UTextBlock* Label : MainLabels)
	{
		if (Label) Label->SetVisibility(MainVis);
	}

	if (SoundLabel) SoundLabel->SetVisibility(OptionsVis);
	if (SoundBar) SoundBar->SetVisibility(OptionsVis);
	if (SoundValueText) SoundValueText->SetVisibility(OptionsVis);
	if (BrightnessLabel) BrightnessLabel->SetVisibility(OptionsVis);
	if (BrightnessBar) BrightnessBar->SetVisibility(OptionsVis);
	if (BrightnessValueText) BrightnessValueText->SetVisibility(OptionsVis);
	if (OptionsHighlightBox) OptionsHighlightBox->SetVisibility(OptionsVis);

	if (ControlsScrollBox) ControlsScrollBox->SetVisibility(ControlsVis);
}

void UPauseMenuWidget::RefreshMainHighlight()
{
	if (!MainHighlightBox || !MainArrowLeft || !MainArrowRight)
	{
		return;
	}

	const float RowY = MainRowY[MainSelectedIndex];

	if (UCanvasPanelSlot* ChildSlot = Cast<UCanvasPanelSlot>(MainHighlightBox->Slot))
	{
		ChildSlot->SetPosition(FVector2D(ListBoxCenterX, RowY));
	}
	if (UCanvasPanelSlot* ChildSlot = Cast<UCanvasPanelSlot>(MainArrowLeft->Slot))
	{
		ChildSlot->SetPosition(FVector2D(ListBoxLeft - ArrowOffsetFromBox, RowY));
	}
	if (UCanvasPanelSlot* ChildSlot = Cast<UCanvasPanelSlot>(MainArrowRight->Slot))
	{
		ChildSlot->SetPosition(FVector2D(ListBoxLeft + ListBoxWidth + ArrowOffsetFromBox, RowY));
	}
}

void UPauseMenuWidget::RefreshOptionsDisplay()
{
	const UWorld* World = GetWorld();
	const UDMCGameInstance* GameInstance = World ? Cast<UDMCGameInstance>(World->GetGameInstance()) : nullptr;
	if (!GameInstance)
	{
		return;
	}

	const float Volume = GameInstance->GetMasterVolume();
	const float Brightness = GameInstance->GetBrightness();

	if (SoundBar) SoundBar->SetPercent(Volume);
	if (SoundValueText) SoundValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Volume * 100.f))));

	if (BrightnessBar) BrightnessBar->SetPercent(Brightness);
	if (BrightnessValueText) BrightnessValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Brightness * 100.f))));

	if (OptionsHighlightBox)
	{
		const float RowY = (OptionsSelectedIndex == 0) ? OptionsSoundRowY : OptionsBrightnessRowY;
		if (UCanvasPanelSlot* ChildSlot = Cast<UCanvasPanelSlot>(OptionsHighlightBox->Slot))
		{
			ChildSlot->SetPosition(FVector2D(ContentLeft + ContentWidth / 2.f, RowY));
		}
	}
}

void UPauseMenuWidget::RefreshControlsList()
{
	if (!ControlsText)
	{
		return;
	}

	ADeathMetalCatCharacter* Character = OwningController ? OwningController->GetPawn<ADeathMetalCatCharacter>() : nullptr;
	if (!Character || !Character->MoveMappingContext)
	{
		ControlsText->SetText(FText::FromString(TEXT("No active control bindings found.")));
		return;
	}

	// Group mappings by action (an action can have several keys -- e.g. Move has a stick axis plus
	// two D-Pad keys), preserving first-seen order, then render one line per action. Pulled live
	// from the mapping context's CURRENT mappings every time this is called (not cached) -- see
	// class comment.
	TArray<const UInputAction*> OrderedActions;
	TMap<const UInputAction*, TArray<FString>> KeysByAction;

	for (const FEnhancedActionKeyMapping& Mapping : Character->MoveMappingContext->GetMappings())
	{
		const UInputAction* Action = Mapping.Action;
		if (!Action)
		{
			continue;
		}

		TArray<FString>* Keys = KeysByAction.Find(Action);
		if (!Keys)
		{
			OrderedActions.Add(Action);
			Keys = &KeysByAction.Add(Action);
		}

		const FString KeyDisplayName = Mapping.Key.GetDisplayName().ToString();
		if (!Keys->Contains(KeyDisplayName))
		{
			Keys->Add(KeyDisplayName);
		}
	}

	TArray<FString> Lines;
	for (const UInputAction* Action : OrderedActions)
	{
		const FString ActionName = HumanizeActionName(Action->GetName());
		const FString KeysJoined = FString::Join(KeysByAction[Action], TEXT(", "));
		Lines.Add(FString::Printf(TEXT("%s:  %s"), *ActionName, *KeysJoined));
	}

	ControlsText->SetText(FText::FromString(FString::Join(Lines, TEXT("\n"))));
}

FString UPauseMenuWidget::HumanizeActionName(const FString& RawAssetName)
{
	FString Name = RawAssetName;
	Name.RemoveFromStart(TEXT("IA_"));

	// Insert a space before each internal capital letter run's start ("SwordAttack" -> "Sword Attack"),
	// then uppercase the whole thing for a consistent look with the rest of this screen's labels.
	FString Spaced;
	Spaced.Reserve(Name.Len() * 2);
	for (int32 Index = 0; Index < Name.Len(); ++Index)
	{
		const TCHAR Ch = Name[Index];
		if (Index > 0 && FChar::IsUpper(Ch) && !FChar::IsUpper(Name[Index - 1]))
		{
			Spaced.AppendChar(TEXT(' '));
		}
		Spaced.AppendChar(Ch);
	}
	return Spaced.ToUpper();
}

void UPauseMenuWidget::NavigateUp()
{
	switch (CurrentPage)
	{
	case EPausePage::Main:
		MainSelectedIndex = (MainSelectedIndex + 3) % 4;
		RefreshMainHighlight();
		break;
	case EPausePage::Options:
		OptionsSelectedIndex = (OptionsSelectedIndex + 1) % 2;
		RefreshOptionsDisplay();
		break;
	case EPausePage::Controls:
		if (ControlsScrollBox)
		{
			ControlsScrollBox->SetScrollOffset(FMath::Max(0.f, ControlsScrollBox->GetScrollOffset() - 40.f));
		}
		break;
	}
}

void UPauseMenuWidget::NavigateDown()
{
	switch (CurrentPage)
	{
	case EPausePage::Main:
		MainSelectedIndex = (MainSelectedIndex + 1) % 4;
		RefreshMainHighlight();
		break;
	case EPausePage::Options:
		OptionsSelectedIndex = (OptionsSelectedIndex + 1) % 2;
		RefreshOptionsDisplay();
		break;
	case EPausePage::Controls:
		if (ControlsScrollBox)
		{
			ControlsScrollBox->SetScrollOffset(ControlsScrollBox->GetScrollOffset() + 40.f);
		}
		break;
	}
}

void UPauseMenuWidget::NavigateLeft()
{
	AdjustSelectedOptionsValue(-OptionsAdjustStep);
}

void UPauseMenuWidget::NavigateRight()
{
	AdjustSelectedOptionsValue(OptionsAdjustStep);
}

void UPauseMenuWidget::AdjustSelectedOptionsValue(float Delta)
{
	if (CurrentPage != EPausePage::Options)
	{
		return;
	}

	UWorld* World = GetWorld();
	UDMCGameInstance* GameInstance = World ? Cast<UDMCGameInstance>(World->GetGameInstance()) : nullptr;
	if (!GameInstance)
	{
		return;
	}

	if (OptionsSelectedIndex == 0)
	{
		GameInstance->SetMasterVolume(this, GameInstance->GetMasterVolume() + Delta);
	}
	else
	{
		GameInstance->SetBrightness(GameInstance->GetBrightness() + Delta);
	}

	RefreshOptionsDisplay();
}

void UPauseMenuWidget::Confirm()
{
	if (CurrentPage != EPausePage::Main || !OwningController)
	{
		return;
	}

	switch (MainSelectedIndex)
	{
	case 0: // Resume
		OwningController->RequestResume();
		break;
	case 1: // Options
		CurrentPage = EPausePage::Options;
		OptionsSelectedIndex = 0;
		RefreshOptionsDisplay();
		RefreshPageVisibility();
		break;
	case 2: // Controls
		CurrentPage = EPausePage::Controls;
		RefreshControlsList();
		if (ControlsScrollBox)
		{
			ControlsScrollBox->SetScrollOffset(0.f);
		}
		RefreshPageVisibility();
		break;
	case 3: // Quit To Title
		OwningController->RequestQuitToTitle();
		break;
	default:
		break;
	}
}

void UPauseMenuWidget::GoBack()
{
	if (CurrentPage == EPausePage::Main)
	{
		return;
	}

	CurrentPage = EPausePage::Main;
	RefreshMainHighlight();
	RefreshPageVisibility();
}
