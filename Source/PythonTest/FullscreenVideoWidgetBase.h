#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FullscreenVideoWidgetBase.generated.h"

class UCanvasPanel;
class UImage;
class UScaleBox;
class UTextBlock;
class UMediaPlayer;
class UMediaSource;
class UMediaTexture;
class AMediaAudioActor;

/**
 * Shared scaffolding for a full-screen video-backed UMG screen: black backdrop + letterboxed video
 * image + an optional pulsing hint line + a full-screen black fade-out + the video's own audio (via
 * a spawned AMediaAudioActor -- a UUserWidget can't own an ActorComponent itself, see that class's
 * comment), all built once here so the two screens in this game that play an .mp4 through Media
 * Framework -- the title screen and the intro cinematic -- don't each reimplement the same
 * UMediaPlayer wiring, aspect-ratio handling, audio, and fade-to-black.
 *
 * Built entirely in Initialize() (WidgetTree->ConstructWidget), not NativeConstruct -- same
 * approach and reason as UGnarlyRankHUDWidget: Initialize() runs before UMG builds the underlying
 * Slate tree from WidgetTree->RootWidget, while NativeConstruct() would be too late.
 *
 * A subclass supplies which assets to load (GetMediaPlayerAssetPath/GetMediaSourceAssetPath/
 * GetMediaTextureAssetPath) and what its hint line says, if anything at all
 * (GetHintTextString -- an empty string, the default, means no hint line is built). Everything
 * about ONE screen's playback shape -- the title screen's loop/freeze/hold cycle, the cinematic's
 * play-once-then-hand-off -- stays in that subclass; this base only owns what both share.
 */
UCLASS(Abstract)
class PYTHONTEST_API UFullscreenVideoWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/**
	 * Ramps BlackOverlayImage's opacity from its current value to fully opaque over Duration
	 * seconds (driven from NativeTick), hides the hint line immediately, and closes MediaPlayer
	 * once the ramp completes. Idempotent -- a second call while already fading/faded is a no-op.
	 * Public: both ATitleScreenGameMode (leaving on any input) and AIntroCinematicGameMode
	 * (skipped or finished) call this directly on their widget to drive the exact same fade.
	 */
	void BeginFadeToBlack(float Duration);

protected:
	// -- subclass configuration hooks --

	/** Path to the UMediaPlayer asset to load, e.g. "/Game/UI/TitleScreen/MP_TitleVideo.MP_TitleVideo". */
	virtual const TCHAR* GetMediaPlayerAssetPath() const PURE_VIRTUAL(UFullscreenVideoWidgetBase::GetMediaPlayerAssetPath, return TEXT(""););

	/** Path to the UMediaSource (FileMediaSource) asset to load. */
	virtual const TCHAR* GetMediaSourceAssetPath() const PURE_VIRTUAL(UFullscreenVideoWidgetBase::GetMediaSourceAssetPath, return TEXT(""););

	/** Path to the UMediaTexture asset to load and draw as the video image's brush. */
	virtual const TCHAR* GetMediaTextureAssetPath() const PURE_VIRTUAL(UFullscreenVideoWidgetBase::GetMediaTextureAssetPath, return TEXT(""););

	/** Text for the pulsing hint line (e.g. "PRESS ANY BUTTON TO START"). Return an empty string (the default) for no hint line at all. */
	virtual FString GetHintTextString() const { return FString(); }

	/**
	 * Which native media player backend this screen's MediaSource should be forced onto -- "WmfMedia"
	 * or "ElectraPlayer" (the two player factory names registered in this project; see
	 * Initialize()'s comment for how this gets applied). Deliberately NOT left as NAME_None/"auto"
	 * for either screen: confirmed live (2026-09-04) that once the Electra plugin is enabled at all,
	 * "auto" resolves to Electra even for a screen that never asked for it, silently overriding what
	 * used to auto-resolve to WmfMedia. Every screen must say explicitly which one it wants.
	 *
	 * WmfMedia can't actually decode a frame for whichever UMediaPlayer opens second in this process
	 * (confirmed live 2026-09-04) -- Electra fixes that, but Electra's own H.264 parser rejects the
	 * title screen's specific encoding ("Invalid value for max_dec_frame_buffering in SPS"). So the
	 * title screen (first, and already reliable under WmfMedia) stays on WmfMedia, and only the intro
	 * cinematic (second, the one that needs the fix) switches to Electra -- see each subclass's
	 * override.
	 */
	virtual FName GetDesiredMediaPlayerName() const PURE_VIRTUAL(UFullscreenVideoWidgetBase::GetDesiredMediaPlayerName, return NAME_None;);

	/** True from the first BeginFadeToBlack call onward -- lets a subclass's own per-tick logic (e.g. the title screen's loop) stop itself once the hand-off has begun. */
	bool IsFadeTriggered() const { return bFadeTriggered; }

	/**
	 * Pushes the video's true pixel dimensions into the video image's brush once they're known, so
	 * the enclosing ScaleBox has a real aspect ratio to fit against rather than a placeholder. Reads
	 * the media player's video track dimensions rather than the texture's surface size, because the
	 * texture reports a 2x2 placeholder before its first real frame arrives, and latching that
	 * squashes the video into a square until the fallback below finally catches up. Cheap no-op once
	 * applied.
	 */
	void ApplyVideoDimensions();

	/** Called every Tick; wires AudioActor to MediaPlayer (starting its audio) the first time the player reports a real duration -- see the .cpp for why this can't just happen once, unconditionally, in NativeConstruct. No-ops once done for this construct. */
	void StartAudioOnceReady();

	/** Safety/completion hook for UMediaPlayer::OnEndReached. Default no-op -- the title screen uses this as a safety net for its freeze margin, the cinematic uses it to detect natural (unskipped) completion. */
	UFUNCTION()
	virtual void HandleMediaEndReached();

private:
	/**
	 * Bound to UMediaPlayer::OnMediaOpened/OnMediaOpenFailed -- these fire asynchronously once the
	 * open this widget requested in NativeConstruct actually finishes (or fails), unlike
	 * OpenSource()'s own return value, which only means the request was accepted, not that it
	 * completed. Pure diagnostic logging, added to get direct evidence of what OpenSource's outcome
	 * actually was rather than inferring it from GetDuration()/IsReady() afterwards.
	 */
	UFUNCTION()
	void HandleMediaOpened(FString OpenedUrl);

	UFUNCTION()
	void HandleMediaOpenFailed(FString FailedUrl);

protected:

	/** Full-screen black behind the letterboxed video, so the pillar/letterbox bars are black rather than showing whatever is behind the widget. */
	UPROPERTY()
	TObjectPtr<UImage> BackdropImage;

	/** Draws MediaTexture directly as its brush resource -- UMediaTexture derives from UTexture, so Slate can draw it with no intermediate material asset. */
	UPROPERTY()
	TObjectPtr<UImage> VideoImage;

	/** ScaleToFit wrapper around VideoImage, so the video letterboxes rather than stretching or cropping on aspect ratios other than its own. */
	UPROPERTY()
	TObjectPtr<UScaleBox> VideoScaleBox;

	/** Bottom-centre pulsing hint line. Only constructed if GetHintTextString() is non-empty. */
	UPROPERTY()
	TObjectPtr<UTextBlock> HintText;

	/** Full-screen black overlay, added LAST in Initialize() so it paints over the video and the hint line alike. Starts fully transparent; only BeginFadeToBlack ever drives it. */
	UPROPERTY()
	TObjectPtr<UImage> BlackOverlayImage;

	/** The subclass's MediaPlayer asset (not a transient instance) -- loaded by GetMediaPlayerAssetPath() in Initialize(). */
	UPROPERTY()
	TObjectPtr<UMediaPlayer> MediaPlayer;

	/** The subclass's FileMediaSource asset. Kept as a member so a subclass's own restart/seek logic can re-open it if a plain seek ever fails. */
	UPROPERTY()
	TObjectPtr<UMediaSource> MediaSource;

	/** The render target MediaPlayer decodes into, and the brush resource VideoImage draws. */
	UPROPERTY()
	TObjectPtr<UMediaTexture> MediaTexture;

	/** Hosts the UMediaSoundComponent that actually outputs MediaPlayer's audio track -- see AMediaAudioActor's own comment for why this needs a separate actor. Spawned in NativeConstruct, destroyed alongside everything else in NativeDestruct. */
	UPROPERTY()
	TObjectPtr<AMediaAudioActor> AudioActor;

private:
	/** Gentle sine pulse on the hint line's opacity. Deliberately never reaches zero -- the hint is meant to be readable at all times, the pulse only marks it as interactive. No-ops once IsFadeTriggered(). */
	void UpdateHintPulse(float DeltaTime);

	/** Per-tick opacity ramp for BlackOverlayImage -- see BeginFadeToBlack. No-ops while bFadeActive is false. */
	void UpdateFadeToBlack();

	/** Periodically logs MediaPlayer's actual playback state (see the .cpp) so a failed-to-render video's cause shows up in the log instead of just as a black screen. Temporary diagnostic. */
	void LogMediaDiagnostics(float DeltaTime);

	float DiagLogAccumulator = 0.f;

	bool bVideoDimensionsApplied = false;

	/** True once StartAudioOnceReady has wired AudioActor up for this construct -- see that method. */
	bool bAudioStarted = false;

	/** Accumulates every tick; drives UpdateHintPulse's sine. */
	float HintPulseTime = 0.f;

	/** True from the first BeginFadeToBlack call onward -- see IsFadeTriggered. */
	bool bFadeTriggered = false;

	/** True only while the fade ramp itself is in progress -- turns false once it reaches full opacity. */
	bool bFadeActive = false;

	float FadeStartTime = 0.f;
	float FadeDuration = 0.5f;
};
