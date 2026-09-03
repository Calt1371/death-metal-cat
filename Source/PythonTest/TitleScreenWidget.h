#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TitleScreenWidget.generated.h"

class UCanvasPanel;
class UImage;
class UScaleBox;
class UTextBlock;
class UMediaPlayer;
class UMediaSource;
class UMediaTexture;

/**
 * Drives the title screen's looping background video and its "PRESS ANY BUTTON TO START" prompt.
 *
 * Built entirely in Initialize() (WidgetTree->ConstructWidget), the same construction approach and
 * for the same reason as UGnarlyRankHUDWidget: Initialize() runs before UMG builds the underlying
 * Slate tree from WidgetTree->RootWidget, while NativeConstruct() would be too late.
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
 * Seek(0)+SetRate(1) can restart it cleanly. The margin is a couple of frames, and the source
 * video is already static for its final stretch (the title is fully on screen by then), so the
 * held image is visually the final frame. OnEndReached is still bound as a safety net in case a
 * hitch lets playback blow past the margin.
 *
 * The video is displayed by handing the UMediaTexture straight to a UImage brush
 * (SetBrushResourceObject) -- UMediaTexture derives from UTexture, so Slate can draw it without a
 * dedicated material asset in between. It sits inside a UScaleBox set to ScaleToFit so the video
 * letterboxes rather than stretching or cropping on aspect ratios other than its own; cropping
 * specifically must be avoided here because the game title is baked into the video frame and
 * could otherwise be cut off.
 */
UCLASS()
class PYTHONTEST_API UTitleScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/**
	 * Begins the hand-off to the intro cinematic: hides the prompt and ramps the full-screen black
	 * overlay to fully opaque over Duration seconds. Push-driven by ATitleScreenGameMode the moment
	 * any input is detected, the same way UGnarlyRankHUDWidget's fades are pushed by
	 * ARoomProgressionManager. Also latches the video cycle off, so the loop can't restart mid-fade.
	 */
	void BeginStartTransition(float Duration);

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
	};

	/** Per-tick state machine for the play/freeze/hold/replay cycle. No-ops once bStartTriggered. */
	void UpdateVideoCycle();

	/** Seek back to 0 and resume. Falls back to a full OpenSource if the player refuses the rate change (i.e. it fell out of a seekable state despite the pause-before-EOF strategy). */
	void RestartVideo();

	/** Per-tick opacity ramp for BlackOverlayImage -- see BeginStartTransition. No-ops while bTransitionActive is false. */
	void UpdateStartTransition();

	/** Gentle sine pulse on the prompt text's opacity. Deliberately never reaches zero -- the prompt is meant to be readable at all times, the pulse only marks it as interactive. */
	void UpdatePromptPulse(float DeltaTime);

	/** Pushes the media texture's true pixel dimensions into the video image's brush once they're known, so the UScaleBox has a real aspect ratio to fit against rather than a placeholder. Cheap no-op once applied. */
	void ApplyVideoDimensions();

	/** Safety net for the freeze: fires if playback somehow reaches EOF despite pausing early. */
	UFUNCTION()
	void HandleMediaEndReached();

	/** Full-screen black behind the letterboxed video, so the pillar/letterbox bars are black rather than showing whatever is behind the widget. */
	UPROPERTY()
	TObjectPtr<UImage> BackdropImage;

	/** Draws MediaTexture directly as its brush resource -- see class comment. */
	UPROPERTY()
	TObjectPtr<UImage> VideoImage;

	/** ScaleToFit wrapper around VideoImage -- see class comment for why fit rather than fill. */
	UPROPERTY()
	TObjectPtr<UScaleBox> VideoScaleBox;

	/** "PRESS ANY BUTTON TO START", bottom-centre. */
	UPROPERTY()
	TObjectPtr<UTextBlock> PromptText;

	/** Full-screen black overlay, added LAST in Initialize() so it paints over the video and the prompt alike. Starts fully transparent; only BeginStartTransition ever drives it. */
	UPROPERTY()
	TObjectPtr<UImage> BlackOverlayImage;

	/** The MP_TitleVideo asset (not a transient instance) -- loaded by path in Initialize(). */
	UPROPERTY()
	TObjectPtr<UMediaPlayer> MediaPlayer;

	/** MS_TitleVideo, the FileMediaSource wrapping Content/Movies/DMC_Game_Title.mp4. Kept as a member so RestartVideo can re-open it if a plain seek ever fails. */
	UPROPERTY()
	TObjectPtr<UMediaSource> MediaSource;

	/** MT_TitleVideo -- the render target MediaPlayer decodes into, and the brush resource VideoImage draws. */
	UPROPERTY()
	TObjectPtr<UMediaTexture> MediaTexture;

	EVideoState VideoState = EVideoState::Opening;

	/** GetWorld()->GetTimeSeconds() at the moment the video was paused on its final frame. */
	float FreezeStartTime = 0.f;

	/** True once ApplyVideoDimensions has pushed real dimensions into the brush. */
	bool bVideoDimensionsApplied = false;

	/** Accumulates every tick; drives UpdatePromptPulse's sine. */
	float PromptPulseTime = 0.f;

	/** True from BeginStartTransition until the black ramp completes. */
	bool bTransitionActive = false;

	/** Latched by BeginStartTransition -- permanently stops UpdateVideoCycle so the loop can't restart behind the fade. */
	bool bStartTriggered = false;

	float TransitionStartTime = 0.f;
	float TransitionDuration = 0.5f;
};
