#include "TitleScreenWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "MediaPlayer.h"
#include "MediaSource.h"
#include "MediaTexture.h"

namespace
{
	// Created by AgentScripts/ue_create_title_screen_assets.py.
	const TCHAR* TitleMediaPlayerPath = TEXT("/Game/UI/TitleScreen/MP_TitleVideo.MP_TitleVideo");
	const TCHAR* TitleMediaSourcePath = TEXT("/Game/UI/TitleScreen/MS_TitleVideo.MS_TitleVideo");
	const TCHAR* TitleMediaTexturePath = TEXT("/Game/UI/TitleScreen/MT_TitleVideo.MT_TitleVideo");

	// Engine-provided 1x1 white texture -- tinted via SetColorAndOpacity to get a plain solid fill
	// without a dedicated asset, exactly as UGnarlyRankHUDWidget does for its tint/fade overlays.
	const TCHAR* WhiteSquarePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");

	// How long the title sits frozen on the final frame before the video replays. The brief asked
	// for "about 20 seconds".
	constexpr float FreezeHoldSeconds = 20.f;

	// How far before the true end of the stream playback is paused. ~2.5 frames at 30fps -- long
	// enough to reliably beat the decoder to EOF (which would drop the player into the stopped
	// state this whole approach exists to avoid, see the class comment), short enough that the
	// held frame is visually the last one. Raise this if the freeze ever flickers black.
	constexpr float FreezeMarginSeconds = 0.08f;

	// Prompt pulse. The floor is deliberately well above zero: the brief asks for the prompt to be
	// visible throughout, so this reads as a slow breath rather than a blink.
	constexpr float PromptPulseSpeed = 2.2f;
	constexpr float PromptMinOpacity = 0.55f;
	constexpr float PromptMaxOpacity = 1.f;

	constexpr int32 PromptFontSize = 34;

	// Distance from the bottom edge of the screen to the baseline of the prompt.
	constexpr float PromptBottomMargin = 90.f;
}

bool UTitleScreenWidget::Initialize()
{
	const bool bSuperResult = Super::Initialize();
	if (!bSuperResult)
	{
		return false;
	}

	// Built here, not in NativeConstruct -- see this class's header comment for why.
	if (!VideoImage)
	{
		UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
		WidgetTree->RootWidget = RootCanvas;

		UTexture2D* WhiteTexture = LoadObject<UTexture2D>(nullptr, WhiteSquarePath);

		// -- Black backdrop, added FIRST so everything else paints on top of it. This is what fills
		// the letterbox bars the ScaleBox leaves when the screen's aspect ratio differs from the
		// video's.
		BackdropImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BackdropImage"));
		if (WhiteTexture)
		{
			BackdropImage->SetBrushFromTexture(WhiteTexture);
		}
		BackdropImage->SetColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 1.f));
		if (UCanvasPanelSlot* BackdropSlot = RootCanvas->AddChildToCanvas(BackdropImage))
		{
			BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			BackdropSlot->SetOffsets(FMargin(0.f));
		}

		// -- The video itself, letterboxed inside a ScaleToFit box (see class comment).
		VideoImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("VideoImage"));

		MediaTexture = LoadObject<UMediaTexture>(nullptr, TitleMediaTexturePath);
		if (MediaTexture)
		{
			// UMediaTexture derives from UTexture, so Slate can draw it as a brush resource with no
			// intermediate material asset.
			VideoImage->SetBrushResourceObject(MediaTexture);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[TITLE] Failed to load media texture: %s"), TitleMediaTexturePath);
		}

		VideoScaleBox = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("VideoScaleBox"));
		VideoScaleBox->SetStretch(EStretch::ScaleToFit);
		VideoScaleBox->SetContent(VideoImage);

		if (UCanvasPanelSlot* VideoSlot = RootCanvas->AddChildToCanvas(VideoScaleBox))
		{
			VideoSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			VideoSlot->SetOffsets(FMargin(0.f));
		}

		// -- "PRESS ANY BUTTON TO START", bottom-centre, heavily outlined so it stays legible over
		// whatever the video happens to be showing behind it.
		PromptText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PromptText"));
		PromptText->SetText(FText::FromString(TEXT("PRESS ANY BUTTON TO START")));
		PromptText->SetJustification(ETextJustify::Center);
		PromptText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		{
			FSlateFontInfo PromptFont = PromptText->GetFont();
			PromptFont.Size = PromptFontSize;
			PromptFont.OutlineSettings.OutlineSize = 3;
			PromptFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 1.f);
			PromptText->SetFont(PromptFont);
		}
		if (UCanvasPanelSlot* PromptSlot = RootCanvas->AddChildToCanvas(PromptText))
		{
			// Anchored to the bottom-centre of the screen and aligned by its own bottom-centre, so it
			// stays put at any resolution rather than drifting with a fixed top-left offset.
			PromptSlot->SetAnchors(FAnchors(0.5f, 1.f));
			PromptSlot->SetAlignment(FVector2D(0.5f, 1.f));
			PromptSlot->SetPosition(FVector2D(0.f, -PromptBottomMargin));
			PromptSlot->SetAutoSize(true);
		}

		// -- Full-screen black, added LAST so it covers the video and the prompt alike. Starts fully
		// transparent; only BeginStartTransition ever drives it.
		BlackOverlayImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BlackOverlayImage"));
		if (WhiteTexture)
		{
			BlackOverlayImage->SetBrushFromTexture(WhiteTexture);
		}
		BlackOverlayImage->SetColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 1.f));
		BlackOverlayImage->SetRenderOpacity(0.f);
		BlackOverlayImage->SetVisibility(ESlateVisibility::Hidden);
		if (UCanvasPanelSlot* OverlaySlot = RootCanvas->AddChildToCanvas(BlackOverlayImage))
		{
			OverlaySlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			OverlaySlot->SetOffsets(FMargin(0.f));
		}
	}

	MediaPlayer = LoadObject<UMediaPlayer>(nullptr, TitleMediaPlayerPath);
	if (!MediaPlayer)
	{
		UE_LOG(LogTemp, Error, TEXT("[TITLE] Failed to load media player: %s"), TitleMediaPlayerPath);
	}

	MediaSource = LoadObject<UMediaSource>(nullptr, TitleMediaSourcePath);
	if (!MediaSource)
	{
		UE_LOG(LogTemp, Error, TEXT("[TITLE] Failed to load media source: %s"), TitleMediaSourcePath);
	}

	return true;
}

void UTitleScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (!MediaPlayer || !MediaSource)
	{
		return;
	}

	// The MediaTexture is the asset's own, and the asset may already point at this player; re-assert
	// it anyway so the pairing is guaranteed regardless of how the assets were last saved.
	if (MediaTexture && MediaTexture->GetMediaPlayer() != MediaPlayer)
	{
		MediaTexture->SetMediaPlayer(MediaPlayer);
		MediaTexture->UpdateResource();
	}

	// Looping is driven by hand in UpdateVideoCycle (play -> freeze -> hold -> replay), so the
	// player's own loop must be off or it would wrap straight back to the start with no hold.
	MediaPlayer->SetLooping(false);
	MediaPlayer->PlayOnOpen = true;

	MediaPlayer->OnEndReached.AddDynamic(this, &UTitleScreenWidget::HandleMediaEndReached);

	VideoState = EVideoState::Opening;
	bVideoDimensionsApplied = false;

	if (!MediaPlayer->OpenSource(MediaSource))
	{
		UE_LOG(LogTemp, Error, TEXT("[TITLE] OpenSource failed for %s -- is the .mp4 present at Content/Movies and is a media player backend (WmfMedia) enabled?"), *MediaSource->GetName());
	}
}

void UTitleScreenWidget::NativeDestruct()
{
	if (MediaPlayer)
	{
		MediaPlayer->OnEndReached.RemoveDynamic(this, &UTitleScreenWidget::HandleMediaEndReached);
		MediaPlayer->Close();
	}

	Super::NativeDestruct();
}

void UTitleScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	ApplyVideoDimensions();
	UpdateVideoCycle();
	UpdatePromptPulse(InDeltaTime);
	UpdateStartTransition();
}

void UTitleScreenWidget::ApplyVideoDimensions()
{
	if (bVideoDimensionsApplied || !MediaTexture || !VideoImage)
	{
		return;
	}

	// Zero until the first decoded frame lands in the texture; the ScaleBox has nothing meaningful
	// to fit against before then, so just wait for it.
	const float Width = MediaTexture->GetSurfaceWidth();
	const float Height = MediaTexture->GetSurfaceHeight();
	if (Width <= 0.f || Height <= 0.f)
	{
		return;
	}

	// A UImage's desired size comes from its brush image size, and that desired size is exactly what
	// the enclosing ScaleBox fits -- so this is what gives the letterboxing a correct aspect ratio.
	VideoImage->SetDesiredSizeOverride(FVector2D(Width, Height));
	bVideoDimensionsApplied = true;

	UE_LOG(LogTemp, Log, TEXT("[TITLE] Video dimensions resolved: %.0fx%.0f"), Width, Height);
}

void UTitleScreenWidget::UpdateVideoCycle()
{
	if (bStartTriggered || !MediaPlayer)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	switch (VideoState)
	{
	case EVideoState::Opening:
	{
		// GetDuration stays zero until the source is actually open and its tracks are known.
		if (MediaPlayer->GetDuration() > FTimespan::Zero())
		{
			VideoState = EVideoState::Playing;
		}
		break;
	}

	case EVideoState::Playing:
	{
		const FTimespan Duration = MediaPlayer->GetDuration();
		if (Duration <= FTimespan::Zero())
		{
			break;
		}

		const FTimespan FreezeAt = Duration - FTimespan::FromSeconds(FreezeMarginSeconds);
		if (MediaPlayer->GetTime() >= FreezeAt)
		{
			// Pause rather than stop -- this is the whole point, see the class comment.
			MediaPlayer->SetRate(0.f);
			FreezeStartTime = World->GetTimeSeconds();
			VideoState = EVideoState::Frozen;
		}
		break;
	}

	case EVideoState::Frozen:
	{
		if (World->GetTimeSeconds() - FreezeStartTime >= FreezeHoldSeconds)
		{
			RestartVideo();
		}
		break;
	}
	}
}

void UTitleScreenWidget::RestartVideo()
{
	if (!MediaPlayer)
	{
		return;
	}

	// Because the player was paused rather than stopped, it is still open and seekable, so this is
	// the path that should always be taken.
	const bool bSeeked = MediaPlayer->Seek(FTimespan::Zero());
	const bool bResumed = MediaPlayer->SetRate(1.f);

	if (!bSeeked || !bResumed)
	{
		// Fell out of a seekable state anyway -- reopen from scratch. PlayOnOpen restarts playback.
		UE_LOG(LogTemp, Warning, TEXT("[TITLE] Seek/resume refused (seek=%d resume=%d) -- reopening the source."), bSeeked ? 1 : 0, bResumed ? 1 : 0);
		if (MediaSource)
		{
			MediaPlayer->OpenSource(MediaSource);
		}
		VideoState = EVideoState::Opening;
		return;
	}

	VideoState = EVideoState::Playing;
}

void UTitleScreenWidget::HandleMediaEndReached()
{
	if (bStartTriggered)
	{
		return;
	}

	// Safety net: UpdateVideoCycle's margin should normally pause playback before this can fire. If
	// a hitch let it through, honour the hold from here so the cycle still has the right shape --
	// the held frame may be whatever the player left resident rather than a guaranteed final frame.
	if (VideoState != EVideoState::Frozen)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TITLE] Playback hit EOF before the freeze margin -- consider raising FreezeMarginSeconds."));
		MediaPlayer->SetRate(0.f);
		if (const UWorld* World = GetWorld())
		{
			FreezeStartTime = World->GetTimeSeconds();
		}
		VideoState = EVideoState::Frozen;
	}
}

void UTitleScreenWidget::UpdatePromptPulse(float DeltaTime)
{
	if (!PromptText || bStartTriggered)
	{
		return;
	}

	PromptPulseTime += DeltaTime;

	// Map sine's [-1,1] onto [PromptMinOpacity, PromptMaxOpacity] -- never reaching zero, so the
	// prompt is legible at every point in the cycle.
	const float Wave = 0.5f * (FMath::Sin(PromptPulseTime * PromptPulseSpeed) + 1.f);
	PromptText->SetRenderOpacity(FMath::Lerp(PromptMinOpacity, PromptMaxOpacity, Wave));
}

void UTitleScreenWidget::BeginStartTransition(float Duration)
{
	if (bStartTriggered)
	{
		return;
	}

	bStartTriggered = true;
	bTransitionActive = true;
	TransitionDuration = FMath::Max(Duration, KINDA_SMALL_NUMBER);

	if (const UWorld* World = GetWorld())
	{
		TransitionStartTime = World->GetTimeSeconds();
	}

	if (PromptText)
	{
		PromptText->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (BlackOverlayImage)
	{
		BlackOverlayImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		BlackOverlayImage->SetRenderOpacity(0.f);
	}
}

void UTitleScreenWidget::UpdateStartTransition()
{
	if (!bTransitionActive || !BlackOverlayImage)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Elapsed = World->GetTimeSeconds() - TransitionStartTime;
	const float Alpha = FMath::Clamp(Elapsed / TransitionDuration, 0.f, 1.f);
	BlackOverlayImage->SetRenderOpacity(Alpha);

	if (Alpha >= 1.f)
	{
		bTransitionActive = false;

		// Fully black now, so nothing is left to show -- stop decoding.
		if (MediaPlayer)
		{
			MediaPlayer->Close();
		}
	}
}
