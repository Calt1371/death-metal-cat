#include "GnarlyRankHUDWidget.h"

#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "Brushes/SlateRoundedBoxBrush.h"
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

	// Cayde's dialogue portrait, imported by AgentScripts/ue_import_cayde_portrait.py.
	const TCHAR* CaydePortraitPath = TEXT("/Game/UI/CaydeDialogue/T_CaydeDialoguePortrait.T_CaydeDialoguePortrait");

	// you_died_death_metal_cat.png, imported by AgentScripts/ue_import_death_screen.py.
	const TCHAR* YouDiedTexturePath = TEXT("/Game/UI/DeathScreen/T_YouDied.T_YouDied");

	// Blood red for the "YOU DIED" text -- deliberately dark/desaturated rather than a bright pure
	// red, so it reads as "blood" rather than a UI-warning red (same reasoning as the low-health
	// tint's dark red base color above it).
	const FLinearColor BloodRed(0.55f, 0.02f, 0.02f, 1.f);

	// Dark-Souls-style staged death-screen fade-in: the backdrop eases in on its own from t=0;
	// the image and text are held at zero opacity for DeathContentFadeInDelay before easing in
	// TOGETHER over their own duration, so they visibly arrive after the backdrop rather than
	// everything popping in at once. DeathScreenBackdrop's own brush color already bakes in its
	// max 0.75 alpha (see Initialize()), so ramping RenderOpacity 0->1 here naturally tops out at
	// that dim rather than going fully opaque.
	constexpr float DeathBackdropFadeInDuration = 1.75f;
	constexpr float DeathContentFadeInDelay = 0.5f;
	constexpr float DeathContentFadeInDuration = 1.75f;

	// Exponent for FMath::InterpEaseInOut -- 2.0 is a standard smooth ease (accelerate out of 0,
	// decelerate into 1), used instead of a linear lerp specifically so the fade reads as smooth
	// rather than mechanical.
	constexpr float DeathFadeEaseExp = 2.f;

	// How long (seconds) the quip dialogue box takes to fade out once QuipDisplayDuration has
	// elapsed -- a widget-visual-styling constant like the font sizes/positions above, not a
	// gameplay tunable, so it lives here rather than as a UPROPERTY (this widget has no Blueprint
	// asset to edit defaults on anyway -- see this class's header comment).
	constexpr float QuipFadeDuration = 0.5f;

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

	// -- Rage bar --
	//
	// Widget-visual-styling constants, same reasoning as QuipFadeDuration above (no Blueprint asset
	// to expose these as editable defaults on, so they live here rather than as UPROPERTYs).

	/** Degrees/second the Rage bar's fill color cycles through the hue wheel -- independent of fill percent, unlike Health's color-by-percent lerp. */
	constexpr float RageBarHueCycleSpeed = 120.f;

	/** Sine-wave speed (radians/second) for the full-bar "ready" pulsate. */
	constexpr float RageFullPulseSpeed = 8.f;

	/** Peak scale the pulsate reaches (1.0 = no scale change) -- small and subtle per spec ("small scale-pulse"), not a dramatic zoom. */
	constexpr float RageFullPulseMaxScale = 1.08f;

	/** Max positional jitter (Slate units) applied on top of the pulsate while full -- small per spec ("slight jitter"). */
	constexpr float RageFullJitterAmount = 3.f;
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

		// Rage bar, directly below HealthBar -- same position/size pattern, offset down by the bar's
		// own height plus a small gap.
		RageBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("RageBar"));
		RageBar->SetPercent(0.f);

		if (UCanvasPanelSlot* RageBarSlot = RootCanvas->AddChildToCanvas(RageBar))
		{
			RageBarSlot->SetAnchors(FAnchors(0.f, 0.f));
			RageBarSlot->SetAlignment(FVector2D(0.f, 0.f));
			RageBarSlot->SetPosition(FVector2D(30.f, 304.f));
			RageBarSlot->SetSize(FVector2D(220.f, 24.f));
			RageBarSlot->SetAutoSize(false);
		}

		// "RAGE" word label, same position pattern as HealthText -- Health has no word label (just
		// its number), but Rage was explicitly asked to be labeled.
		RageLabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RageLabelText"));
		FSlateFontInfo RageFont = RageLabelText->GetFont();
		RageFont.Size = 20;
		RageLabelText->SetFont(RageFont);
		RageLabelText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		RageLabelText->SetText(FText::FromString(TEXT("RAGE")));

		if (UCanvasPanelSlot* RageLabelSlot = RootCanvas->AddChildToCanvas(RageLabelText))
		{
			RageLabelSlot->SetAnchors(FAnchors(0.f, 0.f));
			RageLabelSlot->SetAlignment(FVector2D(0.f, 0.f));
			RageLabelSlot->SetPosition(FVector2D(260.f, 304.f));
			RageLabelSlot->SetAutoSize(true);
		}

		// -- Cayde's JRPG-style dialogue box (portrait + name + quip line) -- the one element on
		// this whole HUD anchored to the BOTTOM of the canvas: Anchors(0,1) is a bottom-left
		// point-anchor (everything above uses a top-left point-anchor, Anchors(0,0)), so Position
		// here is an offset up-and-right from the screen's bottom-left corner, not down-and-right
		// from its top-left corner -- a negative Y moves an element UP off the bottom edge.
		const float QuipBoxLeft = 40.f;
		const float QuipBoxBottom = 40.f;
		const float QuipBoxWidth = 760.f;
		const float QuipBoxHeight = 160.f;
		const float QuipPortraitSize = 140.f;
		const float QuipPortraitPadding = 10.f;
		const FLinearColor QuipAccentColor(0.95f, 0.75f, 0.2f, 1.f);

		// One wide rounded panel behind BOTH the portrait and the text, rather than two separate
		// bordered boxes placed side by side, so the two read as one connected element. Built with
		// FSlateRoundedBoxBrush (a genuine rounded rect + accent-colored outline, no texture
		// needed) rather than a background image asset, since no dedicated one exists for this.
		QuipBoxBackground = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("QuipBoxBackground"));
		QuipBoxBackground->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.05f, 0.05f, 0.05f, 0.92f), 14.0f, QuipAccentColor, 2.0f));

		if (UCanvasPanelSlot* QuipBoxSlot = RootCanvas->AddChildToCanvas(QuipBoxBackground))
		{
			QuipBoxSlot->SetAnchors(FAnchors(0.f, 1.f));
			QuipBoxSlot->SetAlignment(FVector2D(0.f, 1.f));
			QuipBoxSlot->SetPosition(FVector2D(QuipBoxLeft, -QuipBoxBottom));
			QuipBoxSlot->SetSize(FVector2D(QuipBoxWidth, QuipBoxHeight));
			QuipBoxSlot->SetAutoSize(false);
		}

		// Portrait, framed the same "child of a content panel" way as the Gnarly rank portrait
		// above (SetContent into a Border), just with a rounded brush instead of a plain color tint.
		QuipPortraitFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("QuipPortraitFrame"));
		QuipPortraitFrame->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.05f, 0.05f, 0.05f, 1.f), 10.0f, QuipAccentColor, 2.0f));
		QuipPortraitFrame->SetPadding(FMargin(4.f));

		QuipPortraitImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("QuipPortraitImage"));
		if (UTexture2D* PortraitTexture = LoadObject<UTexture2D>(nullptr, CaydePortraitPath))
		{
			QuipPortraitImage->SetBrushFromTexture(PortraitTexture);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[QUIP] Failed to load Cayde portrait texture: %s"), CaydePortraitPath);
		}
		QuipPortraitFrame->SetContent(QuipPortraitImage);

		if (UCanvasPanelSlot* QuipPortraitSlot = RootCanvas->AddChildToCanvas(QuipPortraitFrame))
		{
			QuipPortraitSlot->SetAnchors(FAnchors(0.f, 1.f));
			QuipPortraitSlot->SetAlignment(FVector2D(0.f, 1.f));
			QuipPortraitSlot->SetPosition(FVector2D(QuipBoxLeft + QuipPortraitPadding, -(QuipBoxBottom + QuipPortraitPadding)));
			QuipPortraitSlot->SetSize(FVector2D(QuipPortraitSize, QuipPortraitSize));
			QuipPortraitSlot->SetAutoSize(false);
		}

		// Text column starts right of the portrait: box left (40) + portrait padding (10) +
		// portrait width (140) + a 15px gap = 205. Y positions are measured up from the box's own
		// bottom edge (40): the name sits 15px below the box's top edge (200), the line just below
		// the name.
		QuipNameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("QuipNameText"));
		QuipNameText->SetText(FText::FromString(TEXT("CAYDE")));
		FSlateFontInfo QuipNameFont = QuipNameText->GetFont();
		QuipNameFont.Size = 16;
		QuipNameText->SetFont(QuipNameFont);
		QuipNameText->SetColorAndOpacity(FSlateColor(QuipAccentColor));

		if (UCanvasPanelSlot* QuipNameSlot = RootCanvas->AddChildToCanvas(QuipNameText))
		{
			QuipNameSlot->SetAnchors(FAnchors(0.f, 1.f));
			QuipNameSlot->SetAlignment(FVector2D(0.f, 0.f));
			QuipNameSlot->SetPosition(FVector2D(205.f, -185.f));
			QuipNameSlot->SetAutoSize(true);
		}

		// Larger, readable body text -- explicit Size + AutoWrapText (not AutoSize) so a long quip
		// wraps within the box instead of overflowing it.
		QuipLineText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("QuipLineText"));
		FSlateFontInfo QuipLineFont = QuipLineText->GetFont();
		QuipLineFont.Size = 20;
		QuipLineText->SetFont(QuipLineFont);
		QuipLineText->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.92f, 0.9f)));
		QuipLineText->SetAutoWrapText(true);

		if (UCanvasPanelSlot* QuipLineSlot = RootCanvas->AddChildToCanvas(QuipLineText))
		{
			QuipLineSlot->SetAnchors(FAnchors(0.f, 1.f));
			QuipLineSlot->SetAlignment(FVector2D(0.f, 0.f));
			QuipLineSlot->SetPosition(FVector2D(205.f, -155.f));
			QuipLineSlot->SetSize(FVector2D(575.f, 90.f));
			QuipLineSlot->SetAutoSize(false);
		}

		// Hidden until the first ShowQuip call -- no quip has fired yet at HUD construction time.
		SetQuipVisualsOpacity(0.f);

		// Room transition fade: added LAST, after every other child above, so it paints over the
		// whole HUD (including the low-health tint and quip box) rather than being obscured by
		// them -- canvas children later in add-order render on top of earlier ones, the exact
		// opposite reasoning from why LowHealthTintImage was added FIRST. Same full-screen-stretch
		// construction technique as that tint (WhiteSquareTexture tinted via SetColorAndOpacity),
		// just black instead of red and starting fully transparent.
		RoomFadeImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("RoomFadeImage"));
		if (UTexture2D* WhiteTexture = LoadObject<UTexture2D>(nullptr, WhiteSquarePath))
		{
			RoomFadeImage->SetBrushFromTexture(WhiteTexture);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[ROOM FADE] Failed to load fade texture: %s"), WhiteSquarePath);
		}
		RoomFadeImage->SetColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.f));

		if (UCanvasPanelSlot* FadeSlot = RootCanvas->AddChildToCanvas(RoomFadeImage))
		{
			FadeSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			FadeSlot->SetOffsets(FMargin(0.f));
		}

		// Death screen: added LAST, after even RoomFadeImage, so "YOU DIED" always paints on top of
		// everything else on this HUD -- including the room-transition fade -- rather than being
		// obscured by it. Same reasoning as why RoomFadeImage itself was added after everything else
		// above it. Hidden by default (SetDeathScreenVisible(false) below); shown/hidden via the
		// push-driven ShowDeathScreen/HideDeathScreen, called by
		// ADeathMetalCatCharacter::HandleDeath/HandleRespawn.
		DeathScreenBackdrop = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("DeathScreenBackdrop"));
		if (UTexture2D* WhiteTextureForDeathScreen = LoadObject<UTexture2D>(nullptr, WhiteSquarePath))
		{
			DeathScreenBackdrop->SetBrushFromTexture(WhiteTextureForDeathScreen);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[DEATH SCREEN] Failed to load backdrop texture: %s"), WhiteSquarePath);
		}
		DeathScreenBackdrop->SetColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.75f));

		if (UCanvasPanelSlot* DeathBackdropSlot = RootCanvas->AddChildToCanvas(DeathScreenBackdrop))
		{
			DeathBackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			DeathBackdropSlot->SetOffsets(FMargin(0.f));
		}

		// Centered, offset up to leave room for DeathScreenText below it -- UImage's default Stretch
		// (ScaleToFit) preserves the source PNG's own aspect ratio inside this fixed box, same as
		// LogoImage above.
		DeathScreenImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("DeathScreenImage"));
		if (UTexture2D* YouDiedTexture = LoadObject<UTexture2D>(nullptr, YouDiedTexturePath))
		{
			DeathScreenImage->SetBrushFromTexture(YouDiedTexture);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[DEATH SCREEN] Failed to load you_died texture: %s"), YouDiedTexturePath);
		}

		if (UCanvasPanelSlot* DeathImageSlot = RootCanvas->AddChildToCanvas(DeathScreenImage))
		{
			DeathImageSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			DeathImageSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			DeathImageSlot->SetPosition(FVector2D(0.f, -60.f));
			DeathImageSlot->SetSize(FVector2D(420.f, 420.f));
			DeathImageSlot->SetAutoSize(false);
		}

		// "YOU DIED" in blood red, centered below the image.
		DeathScreenText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DeathScreenText"));
		DeathScreenText->SetText(FText::FromString(TEXT("YOU DIED")));
		FSlateFontInfo DeathFont = DeathScreenText->GetFont();
		DeathFont.Size = 64;
		DeathScreenText->SetFont(DeathFont);
		DeathScreenText->SetColorAndOpacity(FSlateColor(BloodRed));

		if (UCanvasPanelSlot* DeathTextSlot = RootCanvas->AddChildToCanvas(DeathScreenText))
		{
			DeathTextSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			DeathTextSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			DeathTextSlot->SetPosition(FVector2D(0.f, 180.f));
			DeathTextSlot->SetAutoSize(true);
		}

		// Hidden until the first ShowDeathScreen call -- the player hasn't died yet at HUD
		// construction time (same reasoning as SetQuipVisualsOpacity(0.f) above).
		SetDeathScreenVisible(false);
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
	UpdateQuipFade();
	UpdateRoomFade();
	UpdateDeathScreenFade();
	UpdateRageBarVisuals(InDeltaTime);
}

void UGnarlyRankHUDWidget::ShowQuip(const FString& Line, float DisplayDuration)
{
	if (!QuipLineText)
	{
		return;
	}

	QuipLineText->SetText(FText::FromString(Line));
	QuipDisplayDuration = DisplayDuration;
	QuipShowStartTime = GetWorld()->GetTimeSeconds();
	bQuipShowing = true;
	SetQuipVisualsOpacity(1.f);
}

void UGnarlyRankHUDWidget::UpdateQuipFade()
{
	if (!bQuipShowing)
	{
		return;
	}

	const float Elapsed = GetWorld()->GetTimeSeconds() - QuipShowStartTime;
	if (Elapsed >= QuipDisplayDuration + QuipFadeDuration)
	{
		bQuipShowing = false;
		SetQuipVisualsOpacity(0.f);
	}
	else if (Elapsed >= QuipDisplayDuration)
	{
		const float FadeAlpha = 1.f - (Elapsed - QuipDisplayDuration) / QuipFadeDuration;
		SetQuipVisualsOpacity(FMath::Clamp(FadeAlpha, 0.f, 1.f));
	}
}

void UGnarlyRankHUDWidget::SetQuipVisualsOpacity(float Alpha)
{
	// RenderOpacity alone left a lingering artifact: QuipBoxBackground/QuipPortraitFrame's
	// FSlateRoundedBoxBrush outline stayed faintly visible even at RenderOpacity 0 (a Slate
	// rounded-box-outline quirk, not a logic bug -- this function was already applying opacity to
	// the panels themselves, not just their text/image contents). Collapsing at Alpha == 0
	// sidesteps that entirely: a Collapsed widget isn't painted or hit-tested at all, so there's
	// nothing left to draw regardless of how the brush's outline handles opacity. RenderOpacity
	// still drives the smooth fade itself while Alpha is between 0 and 1; Collapse is only the
	// final, exact-zero state once the fade (or Initialize()'s initial hide) is complete.
	const ESlateVisibility NewQuipVisibility = (Alpha > 0.f) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;

	// QuipPortraitImage is deliberately excluded -- it's nested inside QuipPortraitFrame via
	// SetContent, and both render opacity and visibility already cascade to children.
	if (QuipBoxBackground)
	{
		QuipBoxBackground->SetRenderOpacity(Alpha);
		QuipBoxBackground->SetVisibility(NewQuipVisibility);
	}
	if (QuipPortraitFrame)
	{
		QuipPortraitFrame->SetRenderOpacity(Alpha);
		QuipPortraitFrame->SetVisibility(NewQuipVisibility);
	}
	if (QuipNameText)
	{
		QuipNameText->SetRenderOpacity(Alpha);
		QuipNameText->SetVisibility(NewQuipVisibility);
	}
	if (QuipLineText)
	{
		QuipLineText->SetRenderOpacity(Alpha);
		QuipLineText->SetVisibility(NewQuipVisibility);
	}
}

void UGnarlyRankHUDWidget::ShowDeathScreen()
{
	if (!DeathScreenBackdrop || !DeathScreenImage || !DeathScreenText)
	{
		return;
	}

	bDeathScreenActive = true;
	DeathScreenShowStartTime = GetWorld()->GetTimeSeconds();

	// Visible (so the opacity ramp below is actually painted) but starting fully transparent --
	// UpdateDeathScreenFade eases these up from here every tick, rather than snapping straight to
	// visible the way the old immediate-toggle version did.
	DeathScreenBackdrop->SetVisibility(ESlateVisibility::HitTestInvisible);
	DeathScreenImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	DeathScreenText->SetVisibility(ESlateVisibility::HitTestInvisible);

	DeathScreenBackdrop->SetRenderOpacity(0.f);
	DeathScreenImage->SetRenderOpacity(0.f);
	DeathScreenText->SetRenderOpacity(0.f);
}

void UGnarlyRankHUDWidget::HideDeathScreen()
{
	// Stops the fade-in ramp immediately if HideDeathScreen fires before it's finished (e.g. an
	// unusually short RespawnDelay) -- UpdateDeathScreenFade no-ops the instant this is false.
	bDeathScreenActive = false;
	SetDeathScreenVisible(false);
}

void UGnarlyRankHUDWidget::SetDeathScreenVisible(bool bVisible)
{
	// Collapsed rather than just zero opacity while hidden -- same reasoning as
	// SetQuipVisualsOpacity: a Collapsed widget isn't painted or hit-tested at all, so there's
	// nothing left over once hidden. No fade on the way out (unlike the quip box) -- death is
	// meant to read as an abrupt stop when it's dismissed, only the way IN is staged/eased.
	const ESlateVisibility NewVisibility = bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;

	if (DeathScreenBackdrop)
	{
		DeathScreenBackdrop->SetVisibility(NewVisibility);
	}
	if (DeathScreenImage)
	{
		DeathScreenImage->SetVisibility(NewVisibility);
	}
	if (DeathScreenText)
	{
		DeathScreenText->SetVisibility(NewVisibility);
	}
}

void UGnarlyRankHUDWidget::UpdateDeathScreenFade()
{
	if (!bDeathScreenActive)
	{
		return;
	}

	const float Elapsed = GetWorld()->GetTimeSeconds() - DeathScreenShowStartTime;

	// Backdrop: eases in from t=0 on its own timeline.
	const float BackdropAlpha = FMath::Clamp(Elapsed / DeathBackdropFadeInDuration, 0.f, 1.f);
	if (DeathScreenBackdrop)
	{
		DeathScreenBackdrop->SetRenderOpacity(FMath::InterpEaseInOut(0.f, 1.f, BackdropAlpha, DeathFadeEaseExp));
	}

	// Image + text: held at 0 until DeathContentFadeInDelay has elapsed (FMath::Clamp floors the
	// ratio at 0 for the negative elapsed time before then), then ease in together over their own
	// duration -- both driven by this one ContentAlpha so they arrive in lockstep with each other,
	// just visibly after the backdrop.
	const float ContentAlpha = FMath::Clamp((Elapsed - DeathContentFadeInDelay) / DeathContentFadeInDuration, 0.f, 1.f);
	const float ContentOpacity = FMath::InterpEaseInOut(0.f, 1.f, ContentAlpha, DeathFadeEaseExp);
	if (DeathScreenImage)
	{
		DeathScreenImage->SetRenderOpacity(ContentOpacity);
	}
	if (DeathScreenText)
	{
		DeathScreenText->SetRenderOpacity(ContentOpacity);
	}

	// Both ramps fully complete -- nothing left to animate until the next ShowDeathScreen call.
	if (BackdropAlpha >= 1.f && ContentAlpha >= 1.f)
	{
		bDeathScreenActive = false;
	}
}

void UGnarlyRankHUDWidget::StartRoomFadeOut(float Duration)
{
	if (!RoomFadeImage)
	{
		return;
	}

	RoomFadeStartOpacity = RoomFadeImage->GetColorAndOpacity().A;
	RoomFadeTargetOpacity = 1.f;
	RoomFadeDuration = FMath::Max(Duration, KINDA_SMALL_NUMBER);
	RoomFadeStartTime = GetWorld()->GetTimeSeconds();
	bRoomFadeActive = true;
}

void UGnarlyRankHUDWidget::StartRoomFadeIn(float Duration)
{
	if (!RoomFadeImage)
	{
		return;
	}

	RoomFadeStartOpacity = RoomFadeImage->GetColorAndOpacity().A;
	RoomFadeTargetOpacity = 0.f;
	RoomFadeDuration = FMath::Max(Duration, KINDA_SMALL_NUMBER);
	RoomFadeStartTime = GetWorld()->GetTimeSeconds();
	bRoomFadeActive = true;
}

void UGnarlyRankHUDWidget::SetRoomFadeOpacity(float Alpha)
{
	if (!RoomFadeImage)
	{
		return;
	}

	bRoomFadeActive = false;
	const FLinearColor Current = RoomFadeImage->GetColorAndOpacity();
	RoomFadeImage->SetColorAndOpacity(FLinearColor(Current.R, Current.G, Current.B, FMath::Clamp(Alpha, 0.f, 1.f)));
}

void UGnarlyRankHUDWidget::UpdateRoomFade()
{
	if (!bRoomFadeActive || !RoomFadeImage)
	{
		return;
	}

	const float Elapsed = GetWorld()->GetTimeSeconds() - RoomFadeStartTime;
	const float Alpha = FMath::Clamp(Elapsed / RoomFadeDuration, 0.f, 1.f);
	const float NewOpacity = FMath::Lerp(RoomFadeStartOpacity, RoomFadeTargetOpacity, Alpha);

	const FLinearColor Current = RoomFadeImage->GetColorAndOpacity();
	RoomFadeImage->SetColorAndOpacity(FLinearColor(Current.R, Current.G, Current.B, NewOpacity));

	if (Alpha >= 1.f)
	{
		bRoomFadeActive = false;
	}
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

	// -- Rage bar percent -- fill COLOR is handled separately, every tick, in UpdateRageBarVisuals
	// (it hue-cycles regardless of whether the percent itself changed), so only the percent value
	// is gated here, same change-gated polling pattern as everything else on this widget.
	const float CurrentRage = OwningCharacter->RageMeter;
	const float CurrentRageMax = OwningCharacter->RageMax;

	if (RageBar && (!FMath::IsNearlyEqual(CurrentRage, LastSeenRage) || !FMath::IsNearlyEqual(CurrentRageMax, LastSeenRageMax)))
	{
		LastSeenRage = CurrentRage;
		LastSeenRageMax = CurrentRageMax;

		const float RagePercent = (CurrentRageMax > 0.f) ? FMath::Clamp(CurrentRage / CurrentRageMax, 0.f, 1.f) : 0.f;
		RageBar->SetPercent(RagePercent);
	}
}

void UGnarlyRankHUDWidget::UpdateRageBarVisuals(float DeltaTime)
{
	if (!RageBar || !OwningCharacter)
	{
		return;
	}

	RageAnimTime += DeltaTime;

	// Rainbow hue-cycle, independent of fill percent -- runs unconditionally (not gated on a
	// changed value) since the hue has to keep advancing even while Rage sits still at 0 or full.
	const float HueDegrees = FMath::Fmod(RageAnimTime * RageBarHueCycleSpeed, 360.f);
	const FLinearColor CycledColor = FLinearColor::MakeFromHSV8(static_cast<uint8>((HueDegrees / 360.f) * 255.f), 255, 255);
	RageBar->SetFillColorAndOpacity(CycledColor);

	if (OwningCharacter->IsRageFull())
	{
		// Small scale-pulse + slight jitter to signal "ready" -- see class doc / RageFullPulseMaxScale
		// and RageFullJitterAmount's own comments for why these stay subtle rather than dramatic.
		const float PulseAlpha = (FMath::Sin(RageAnimTime * RageFullPulseSpeed) + 1.f) * 0.5f;
		const float PulseScale = FMath::Lerp(1.f, RageFullPulseMaxScale, PulseAlpha);
		RageBar->SetRenderScale(FVector2D(PulseScale, PulseScale));
		RageBar->SetRenderTranslation(FVector2D(FMath::FRandRange(-RageFullJitterAmount, RageFullJitterAmount),
			FMath::FRandRange(-RageFullJitterAmount, RageFullJitterAmount)));
	}
	else
	{
		RageBar->SetRenderScale(FVector2D(1.f, 1.f));
		RageBar->SetRenderTranslation(FVector2D(0.f, 0.f));
	}
}
