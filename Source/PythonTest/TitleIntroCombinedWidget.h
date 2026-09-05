#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "FullscreenVideoWidgetBase.h"
#include "TitleIntroCombinedWidget.generated.h"

/**
 * Drives the ENTIRE pre-gameplay video experience -- title loop, then intro cinematic -- as ONE
 * continuous playback of a single merged .mp4 (DMC_TitleIntro_Combined.mp4, muxed from the
 * original separate title/intro clips via ffmpeg), through ONE UMediaPlayer/UMediaSource/
 * UMediaTexture for the whole game boot sequence.
 *
 * This replaces the earlier two-widget (UTitleScreenWidget then UIntroCinematicWidget) design.
 * Confirmed live (2026-09-04), across FOUR separate mitigations (reusing the same player instance,
 * deferring OpenSource, stopping the audio component early, disabling NativeAudioOut) and TWO
 * different backends (WmfMedia, Electra): whichever UMediaPlayer opens its source SECOND in this
 * process never actually surfaces real playback state through GetTime()/the texture again, even
 * though the underlying native player (confirmed via Electra's own internal log reporting a real,
 * advancing play position) is genuinely decoding just fine -- the break is in Unreal's own
 * MediaAssets/MediaUtils layer between the native player and UMediaPlayer/UMediaTexture, not
 * anything reachable from game code. The only reliable fix is to never open a second source at
 * all -- so this widget is the ONLY thing in the whole title/intro/gameplay flow that ever calls
 * MediaPlayer->OpenSource(), for the merged file's whole ~71s duration.
 *
 * Playback cycle:
 *   Loop [0, TitleSegmentDuration) with a freeze-hold on the final title frame (same
 *   play/freeze/hold/replay shape UTitleScreenWidget used to drive alone -- see UpdateLoopCycle),
 *   until the first "any input" -- then just let the SAME already-open, already-playing player keep
 *   rolling forward past TitleSegmentDuration into the intro portion (no seek, no reopen, nothing
 *   that could touch the broken second-source path), skippable by a second "any input" or ending
 *   naturally at the file's true end. Both hand off identically -- see NotifyAnyInput/
 *   HandleMediaEndReached and SetOnReadyForGameplayDelegate.
 */
UCLASS()
class PYTHONTEST_API UTitleIntroCombinedWidget : public UFullscreenVideoWidgetBase
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/**
	 * Called on every "any input" press for as long as this widget is up. The FIRST call leaves the
	 * title loop and lets the intro portion play through (updating the hint line to match); every
	 * call after that fires OnReadyForGameplayDelegate instead, exactly as HandleMediaEndReached does
	 * if the player never presses a second time and the file just ends on its own.
	 */
	void NotifyAnyInput();

	/** Set once, right after CreateWidget -- fired (at most once) when the intro portion ends, whether skipped by a second input or reached naturally. */
	void SetOnReadyForGameplayDelegate(FSimpleDelegate InDelegate);

protected:
	virtual const TCHAR* GetMediaPlayerAssetPath() const override;
	virtual const TCHAR* GetMediaSourceAssetPath() const override;
	virtual const TCHAR* GetMediaTextureAssetPath() const override;
	virtual FString GetHintTextString() const override;
	virtual FName GetDesiredMediaPlayerName() const override { return FName(TEXT("WmfMedia")); }
	virtual void HandleMediaEndReached() override;

private:
	/**
	 * Where the title loop currently is -- mirrors UTitleScreenWidget's old EVideoState, just bounded
	 * to [0, TitleSegmentDuration) instead of the whole file. Moves to Committed for good the first
	 * time NotifyAnyInput is called.
	 */
	enum class ELoopState : uint8
	{
		Opening,
		Playing,
		Frozen,
		Restarting,
		/**
		 * NotifyAnyInput's first press landed while Frozen (paused via SetRate(0.f)) -- waiting for a
		 * Seek(Zero)+SetRate(1.f) resume (issued the instant this state is entered) to genuinely take
		 * hold before moving on to Committed. Needed because resuming a PAUSED player only reliably
		 * works via that exact pairing -- confirmed live (2026-09-04), in this exact scenario, that
		 * SetRate(1.f) alone, and separately SetRate(1.f) paired with a Seek() to the same or a
		 * nearby-but-different timestamp, all leave GetTime() frozen indefinitely (IsPlaying/IsPaused
		 * both report playing-shaped values, no error, just no actual new frames) -- the identical
		 * "reports healthy, never advances" signature as the second-OpenSource bug this class's
		 * comment describes, just triggered here by resuming a paused player rather than a second
		 * Open. Seek(Zero) is the one pairing already proven reliable in this exact file (RestartLoop
		 * uses it every loop cycle), so resuming out of a freeze reuses it verbatim rather than
		 * guessing at another seek target. This means a successful commit-while-frozen briefly
		 * replays the tail of the title segment (from 0 back up to the freeze point) before rolling
		 * into the intro -- a minor, accepted UX cost for actually working reliably.
		 */
		PendingCommit,
		/** Left the loop for good -- the intro portion is playing through, no more loop-cycle logic runs. */
		Committed,
	};

	/** Per-tick state machine for the title loop -- see the class comment. No-ops once Committed or IsFadeTriggered(). */
	void UpdateLoopCycle();

	/** Seek back to 0 and resume, same as UTitleScreenWidget::RestartVideo used to. */
	void RestartLoop();

	ELoopState LoopState = ELoopState::Opening;

	/** GetWorld()->GetTimeSeconds() at the moment the loop was paused on its final title frame. */
	float FreezeStartTime = 0.f;

	/** GetWorld()->GetTimeSeconds() at the moment RestartLoop issued its Seek. */
	float RestartRequestTime = 0.f;

	/** GetWorld()->GetTimeSeconds() at the moment NotifyAnyInput's PendingCommit resume issued its Seek -- see PendingCommit's comment. */
	float PendingCommitRequestTime = 0.f;

	FSimpleDelegate OnReadyForGameplayDelegate;
};
