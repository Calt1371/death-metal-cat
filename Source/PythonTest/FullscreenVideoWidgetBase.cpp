#include "FullscreenVideoWidgetBase.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "MediaPlayer.h"
#include "MediaSource.h"
#include "BaseMediaSource.h"
#include "MediaTexture.h"
#include "MediaAudioActor.h"

namespace
{
	// Engine-provided 1x1 white texture -- tinted via SetColorAndOpacity to get a plain solid fill
	// without a dedicated asset, exactly as UGnarlyRankHUDWidget does for its tint/fade overlays.
	// Name-prefixed rather than plain "WhiteSquarePath" because UE compiles the module as a unity
	// build: every .cpp is concatenated into one translation unit, so anonymous-namespace names
	// still collide across files, and GnarlyRankHUDWidget.cpp already defines that one.
	const TCHAR* FullscreenVideoWhiteSquarePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");

	// Smallest believable video dimension. UMediaTexture reports a 2x2 placeholder before its first
	// real frame arrives, and latching that leaves the ScaleBox fitting a 1:1 box -- which silently
	// squashes a real 16:9 video into a square.
	constexpr int32 MinPlausibleVideoDimension = 16;

	// Hint-line pulse. The floor is deliberately well above zero: both screens that use this hint
	// need it visible throughout, so this reads as a slow breath rather than a blink.
	constexpr float HintPulseSpeed = 2.2f;
	constexpr float HintMinOpacity = 0.55f;
	constexpr float HintMaxOpacity = 1.f;

	constexpr int32 HintFontSize = 34;

	// Distance from the bottom edge of the screen to the baseline of the hint line.
	constexpr float HintBottomMargin = 90.f;
}

bool UFullscreenVideoWidgetBase::Initialize()
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

		UTexture2D* WhiteTexture = LoadObject<UTexture2D>(nullptr, FullscreenVideoWhiteSquarePath);

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

		MediaTexture = LoadObject<UMediaTexture>(nullptr, GetMediaTextureAssetPath());
		if (MediaTexture)
		{
			VideoImage->SetBrushResourceObject(MediaTexture);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[VIDEO] Failed to load media texture: %s"), GetMediaTextureAssetPath());
		}

		VideoScaleBox = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("VideoScaleBox"));
		VideoScaleBox->SetStretch(EStretch::ScaleToFit);
		VideoScaleBox->SetContent(VideoImage);

		if (UCanvasPanelSlot* VideoSlot = RootCanvas->AddChildToCanvas(VideoScaleBox))
		{
			VideoSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			VideoSlot->SetOffsets(FMargin(0.f));
		}

		// -- Optional pulsing hint line, bottom-centre, heavily outlined so it stays legible over
		// whatever the video happens to be showing behind it.
		const FString HintString = GetHintTextString();
		if (!HintString.IsEmpty())
		{
			HintText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HintText"));
			HintText->SetText(FText::FromString(HintString));
			HintText->SetJustification(ETextJustify::Center);
			HintText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			{
				FSlateFontInfo HintFont = HintText->GetFont();
				HintFont.Size = HintFontSize;
				HintFont.OutlineSettings.OutlineSize = 3;
				HintFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 1.f);
				HintText->SetFont(HintFont);
			}
			if (UCanvasPanelSlot* HintSlot = RootCanvas->AddChildToCanvas(HintText))
			{
				// Anchored to the bottom-centre of the screen and aligned by its own bottom-centre, so
				// it stays put at any resolution rather than drifting with a fixed top-left offset.
				HintSlot->SetAnchors(FAnchors(0.5f, 1.f));
				HintSlot->SetAlignment(FVector2D(0.5f, 1.f));
				HintSlot->SetPosition(FVector2D(0.f, -HintBottomMargin));
				HintSlot->SetAutoSize(true);
			}
		}

		// -- Full-screen black, added LAST so it covers the video and the hint line alike. Starts
		// fully transparent; only BeginFadeToBlack ever drives it.
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

	MediaPlayer = LoadObject<UMediaPlayer>(nullptr, GetMediaPlayerAssetPath());
	if (!MediaPlayer)
	{
		UE_LOG(LogTemp, Error, TEXT("[VIDEO] Failed to load media player: %s"), GetMediaPlayerAssetPath());
	}

	MediaSource = LoadObject<UMediaSource>(nullptr, GetMediaSourceAssetPath());
	if (!MediaSource)
	{
		UE_LOG(LogTemp, Error, TEXT("[VIDEO] Failed to load media source: %s"), GetMediaSourceAssetPath());
	}

#if WITH_EDITORONLY_DATA
	// See GetDesiredMediaPlayerName's comment for the full story -- every screen explicitly names its
	// backend rather than leaving either on "auto". This only applies to editor-class builds
	// (including a Development Editor's -game launch, which is what every test so far has used) -- a
	// packaged build would need the equivalent runtime PlayerName set instead if this pans out.
	if (UBaseMediaSource* BaseSource = Cast<UBaseMediaSource>(MediaSource))
	{
		BaseSource->PlatformPlayerNames.Add(TEXT("Windows"), GetDesiredMediaPlayerName());
	}
#endif

	return true;
}

void UFullscreenVideoWidgetBase::NativeConstruct()
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

	// Neither screen that uses this base wants the player's own looping: the title screen drives its
	// own loop by hand so it can hold the frozen frame, and the cinematic must play exactly once.
	MediaPlayer->SetLooping(false);
	MediaPlayer->PlayOnOpen = true;

	// Audio must come ONLY from AudioActor's MediaSoundComponent below, never from the player's own
	// OS-mixer output -- Epic's own Media Framework forum documents NativeAudioOut and a
	// MediaSoundComponent both active at once as a known cause of exactly this class of bug (the video
	// frame freezing/staying black while the player otherwise reports healthy). Whatever an asset
	// happened to save this as, force it off here rather than trusting per-asset state.
	MediaPlayer->NativeAudioOut = false;

	MediaPlayer->OnEndReached.AddDynamic(this, &UFullscreenVideoWidgetBase::HandleMediaEndReached);
	MediaPlayer->OnMediaOpened.AddDynamic(this, &UFullscreenVideoWidgetBase::HandleMediaOpened);
	MediaPlayer->OnMediaOpenFailed.AddDynamic(this, &UFullscreenVideoWidgetBase::HandleMediaOpenFailed);

	// A UUserWidget can't own an ActorComponent itself, so MediaPlayer's audio track is routed
	// through a small dedicated actor instead -- see AMediaAudioActor's own comment. Only spawned
	// here; NOT wired to MediaPlayer yet -- see StartAudioOnceReady/NativeTick for why that's
	// deferred. Guarded in case NativeConstruct somehow runs more than once for the same widget
	// instance, same defensive pattern as the rest of this class.
	if (!AudioActor)
	{
		if (UWorld* World = GetWorld())
		{
			AudioActor = World->SpawnActor<AMediaAudioActor>();
		}
	}

	bVideoDimensionsApplied = false;
	bAudioStarted = false;

	// Logged on BOTH branches, not just failure -- the earlier version of this only logged failure,
	// which left "was OpenSource even called, and what did it actually return" resting on inference
	// (GetDuration()/IsReady() reporting sane-looking values afterwards) rather than direct evidence.
	const bool bOpenedOk = MediaPlayer->OpenSource(MediaSource);
	if (bOpenedOk)
	{
		UE_LOG(LogTemp, Log, TEXT("[VIDEO] OpenSource(%s) returned true for player %s."), *MediaSource->GetName(), *MediaPlayer->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[VIDEO] OpenSource failed for %s -- is the .mp4 present at Content/Movies and is a media player backend (WmfMedia) enabled?"), *MediaSource->GetName());
	}
}

void UFullscreenVideoWidgetBase::NativeDestruct()
{
	if (MediaPlayer)
	{
		MediaPlayer->OnEndReached.RemoveDynamic(this, &UFullscreenVideoWidgetBase::HandleMediaEndReached);
		MediaPlayer->OnMediaOpened.RemoveDynamic(this, &UFullscreenVideoWidgetBase::HandleMediaOpened);
		MediaPlayer->OnMediaOpenFailed.RemoveDynamic(this, &UFullscreenVideoWidgetBase::HandleMediaOpenFailed);
		MediaPlayer->Close();
	}

	if (AudioActor)
	{
		AudioActor->Stop();
		AudioActor->Destroy();
		AudioActor = nullptr;
	}

	Super::NativeDestruct();
}

void UFullscreenVideoWidgetBase::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	ApplyVideoDimensions();
	StartAudioOnceReady();
	UpdateHintPulse(InDeltaTime);
	UpdateFadeToBlack();
	LogMediaDiagnostics(InDeltaTime);
}

void UFullscreenVideoWidgetBase::LogMediaDiagnostics(float DeltaTime)
{
	if (bFadeTriggered || !MediaPlayer)
	{
		return;
	}

	DiagLogAccumulator += DeltaTime;
	if (DiagLogAccumulator < 1.f)
	{
		return;
	}
	DiagLogAccumulator = 0.f;

	const int32 SurfaceW = MediaTexture ? FMath::RoundToInt(MediaTexture->GetSurfaceWidth()) : -1;
	const int32 SurfaceH = MediaTexture ? FMath::RoundToInt(MediaTexture->GetSurfaceHeight()) : -1;

	UE_LOG(LogTemp, Log,
		TEXT("[VIDEO] Diagnostics for %s: IsPlaying=%d IsPaused=%d IsPreparing=%d IsBuffering=%d IsConnecting=%d IsClosed=%d HasError=%d IsReady=%d Duration=%.3f Time=%.3f TextureSurface=%dx%d"),
		*MediaPlayer->GetName(),
		MediaPlayer->IsPlaying() ? 1 : 0,
		MediaPlayer->IsPaused() ? 1 : 0,
		MediaPlayer->IsPreparing() ? 1 : 0,
		MediaPlayer->IsBuffering() ? 1 : 0,
		MediaPlayer->IsConnecting() ? 1 : 0,
		MediaPlayer->IsClosed() ? 1 : 0,
		MediaPlayer->HasError() ? 1 : 0,
		MediaPlayer->IsReady() ? 1 : 0,
		MediaPlayer->GetDuration().GetTotalSeconds(),
		MediaPlayer->GetTime().GetTotalSeconds(),
		SurfaceW, SurfaceH);
}

void UFullscreenVideoWidgetBase::StartAudioOnceReady()
{
	if (bAudioStarted || !AudioActor || !MediaPlayer)
	{
		return;
	}

	// Waits for the exact same "the player has genuinely opened and knows its own duration" signal
	// ApplyVideoDimensions already relies on, rather than wiring the audio component up unconditionally
	// in NativeConstruct (which this used to do). Confirmed live (2026-09-03): starting it immediately
	// in NativeConstruct -- before OpenSource has had any chance to actually take effect -- worked for
	// the intro cinematic (the SECOND screen loaded in a process, with an already-warm Media Framework/
	// audio pipeline) but produced no audio at all for the title screen (the FIRST screen loaded, cold).
	// Waiting for this proven-reliable readiness check fixes both cases the same way, rather than
	// depending on how warm the pipeline happens to be when this runs.
	if (MediaPlayer->GetDuration() > FTimespan::Zero())
	{
		AudioActor->SetMediaPlayer(MediaPlayer);
		bAudioStarted = true;
	}
}

void UFullscreenVideoWidgetBase::ApplyVideoDimensions()
{
	if (bVideoDimensionsApplied || !VideoImage)
	{
		return;
	}

	// Ask the player for the selected video track's real dimensions (INDEX_NONE means "whichever
	// track/format is currently selected") rather than the texture's surface size -- see this
	// method's header comment for why.
	FIntPoint Dimensions(0, 0);
	if (MediaPlayer)
	{
		Dimensions = MediaPlayer->GetVideoTrackDimensions(INDEX_NONE, INDEX_NONE);
	}

	// Fall back to the texture once a real frame has arrived, in case a backend does not report
	// track dimensions.
	if ((Dimensions.X < MinPlausibleVideoDimension || Dimensions.Y < MinPlausibleVideoDimension) && MediaTexture)
	{
		Dimensions.X = FMath::RoundToInt(MediaTexture->GetSurfaceWidth());
		Dimensions.Y = FMath::RoundToInt(MediaTexture->GetSurfaceHeight());
	}

	// Still a placeholder -- keep waiting rather than latching a wrong aspect ratio for good.
	if (Dimensions.X < MinPlausibleVideoDimension || Dimensions.Y < MinPlausibleVideoDimension)
	{
		return;
	}

	// A UImage's desired size comes from its brush image size, and that desired size is exactly what
	// the enclosing ScaleBox fits -- so this is what gives the letterboxing a correct aspect ratio.
	VideoImage->SetDesiredSizeOverride(FVector2D(Dimensions.X, Dimensions.Y));
	bVideoDimensionsApplied = true;

	UE_LOG(LogTemp, Log, TEXT("[VIDEO] Video dimensions resolved: %dx%d"), Dimensions.X, Dimensions.Y);
}

void UFullscreenVideoWidgetBase::HandleMediaOpened(FString OpenedUrl)
{
	UE_LOG(LogTemp, Log, TEXT("[VIDEO] OnMediaOpened fired for %s -- the open genuinely completed, not just accepted."), *OpenedUrl);
}

void UFullscreenVideoWidgetBase::HandleMediaOpenFailed(FString FailedUrl)
{
	UE_LOG(LogTemp, Error, TEXT("[VIDEO] OnMediaOpenFailed fired for %s."), *FailedUrl);
}

void UFullscreenVideoWidgetBase::HandleMediaEndReached()
{
	// No-op by default -- see subclass overrides (UTitleScreenWidget's freeze safety net,
	// UIntroCinematicWidget's natural-completion hand-off).
}

void UFullscreenVideoWidgetBase::UpdateHintPulse(float DeltaTime)
{
	if (!HintText || bFadeTriggered)
	{
		return;
	}

	HintPulseTime += DeltaTime;

	// Map sine's [-1,1] onto [HintMinOpacity, HintMaxOpacity] -- never reaching zero, so the hint is
	// legible at every point in the cycle.
	const float Wave = 0.5f * (FMath::Sin(HintPulseTime * HintPulseSpeed) + 1.f);
	HintText->SetRenderOpacity(FMath::Lerp(HintMinOpacity, HintMaxOpacity, Wave));
}

void UFullscreenVideoWidgetBase::BeginFadeToBlack(float Duration)
{
	if (bFadeTriggered)
	{
		return;
	}

	bFadeTriggered = true;
	bFadeActive = true;
	FadeDuration = FMath::Max(Duration, KINDA_SMALL_NUMBER);

	if (const UWorld* World = GetWorld())
	{
		FadeStartTime = World->GetTimeSeconds();
	}

	if (HintText)
	{
		HintText->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (BlackOverlayImage)
	{
		BlackOverlayImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		BlackOverlayImage->SetRenderOpacity(0.f);
	}

	// Both stopped here, at the START of the fade, rather than only once it completes (video) or only
	// in NativeDestruct (audio) -- UMediaPlayer::Close() only REQUESTS a shutdown; the underlying WMF
	// session actually tears down over subsequent ticks, not instantly, and the same is true of the
	// MediaSoundComponent's own audio render thread teardown. The GameMode that called this
	// (ATitleScreenGameMode/AIntroCinematicGameMode) opens the next level on its own timer of this same
	// Duration, so tearing down only once the fade finishes (or only in NativeDestruct, whenever the
	// engine gets around to it during the transition) left this player's still-unwinding session racing
	// the very next screen's OpenSource() on essentially the same frame. Stopping the audio component
	// FIRST, then closing the player, gives both the full fade-plus-level-load window to actually
	// release before anything tries to reopen a source on the way in.
	if (AudioActor)
	{
		AudioActor->Stop();
	}

	if (MediaPlayer)
	{
		MediaPlayer->Close();
	}
}

void UFullscreenVideoWidgetBase::UpdateFadeToBlack()
{
	if (!bFadeActive || !BlackOverlayImage)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Elapsed = World->GetTimeSeconds() - FadeStartTime;
	const float Alpha = FMath::Clamp(Elapsed / FadeDuration, 0.f, 1.f);
	BlackOverlayImage->SetRenderOpacity(Alpha);

	if (Alpha >= 1.f)
	{
		bFadeActive = false;
	}
}
