#pragma once

#include "CoreMinimal.h"
#include "FullscreenVideoWidgetBase.h"
#include "TitleScreenWidget.generated.h"

/**
 * Drives the title screen's looping background video, on top of UFullscreenVideoWidgetBase's shared
 * playback/fade scaffolding (see that class's comment for what's shared vs. what lives here).
 *
 * Playback cycle (see UpdateVideoCycle):
 *   Play the video once -> freeze on its final frame -> hold FreezeHoldSeconds -> replay -> repeat.
 *
 * The freeze is deliberately implemented by PAUSING a hair before the true end of the stream
 * (FreezeMarginSeconds) rather than by letting playback run to EOF. Reaching EOF with looping
 * disabled drops UMediaPlayer into a stopped/closed state, where the MediaTexture is liable to be
 * cleared (losing the frozen title frame -- the whole point of the hold) and where a subsequent
 * Seek is not reliably honoured. Pausing early keeps the player in a plain paused state, which
 * both guarantees the last decoded frame stays resident in the MediaTexture and guarantees
 * Seek(0)+SetRate(1) can restart it cleanly. The margin is a few frames, and the source video is
 * already static for its final stretch (the title is fully on screen by then), so the held image
 * is visually the final frame. OnEndReached is still bound as a safety net in case a hitch lets
 * playback blow past the margin.
 */
UCLASS()
class PYTHONTEST_API UTitleScreenWidget : public UFullscreenVideoWidgetBase
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

protected:
	virtual const TCHAR* GetMediaPlayerAssetPath() const override;
	virtual const TCHAR* GetMediaSourceAssetPath() const override;
	virtual const TCHAR* GetMediaTextureAssetPath() const override;
	virtual FString GetHintTextString() const override;
	virtual void HandleMediaEndReached() override;

private:
	/** Where the play/freeze/hold/replay cycle currently is -- see UpdateVideoCycle. */
	enum class EVideoState : uint8
	{
		/** OpenSource has been issued but the player hasn't reported a duration yet. */
		Opening,
		/** Rolling forward toward the freeze point. */
		Playing,
		/** Paused on the final frame, counting down FreezeHoldSeconds. */
		Frozen,
		/**
		 * Seek(0) has been issued but the player's clock has not rewound yet. This state exists
		 * because Seek is asynchronous: for at least one tick afterwards GetTime() still reports the
		 * old near-the-end value, and going straight back to Playing meant the freeze check matched
		 * immediately and re-froze the video milliseconds after resuming it -- so from the second
		 * cycle onward the video never actually replayed.
		 */
		Restarting,
	};

	/** Per-tick state machine for the play/freeze/hold/replay cycle. No-ops once IsFadeTriggered(). */
	void UpdateVideoCycle();

	/** Seek back to 0 and resume. Falls back to a full OpenSource if the player refuses the rate change (i.e. it fell out of a seekable state despite the pause-before-EOF strategy). */
	void RestartVideo();

	EVideoState VideoState = EVideoState::Opening;

	/** GetWorld()->GetTimeSeconds() at the moment the video was paused on its final frame. */
	float FreezeStartTime = 0.f;

	/** GetWorld()->GetTimeSeconds() at the moment RestartVideo issued its Seek -- bounds how long the Restarting state waits for the clock to rewind before forcing a full reopen. */
	float RestartRequestTime = 0.f;
};
