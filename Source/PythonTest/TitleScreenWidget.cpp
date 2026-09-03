#include "TitleScreenWidget.h"

#include "MediaPlayer.h"
#include "MediaSource.h"

namespace
{
	// Created by AgentScripts/ue_create_title_screen_assets.py.
	const TCHAR* TitleMediaPlayerPath = TEXT("/Game/UI/TitleScreen/MP_TitleVideo.MP_TitleVideo");
	const TCHAR* TitleMediaSourcePath = TEXT("/Game/UI/TitleScreen/MS_TitleVideo.MS_TitleVideo");
	const TCHAR* TitleMediaTexturePath = TEXT("/Game/UI/TitleScreen/MT_TitleVideo.MT_TitleVideo");

	// How long the title sits frozen on the final frame before the video replays. The brief asked
	// for "about 20 seconds".
	constexpr float FreezeHoldSeconds = 20.f;

	// How far before the true end of the stream playback is paused.
	//
	// Measured, not guessed: at 0.08s this lost the race to EOF on every single cycle, because
	// GetTime() advances in decoded-frame steps rather than continuously, and the game ticks on its
	// own unrelated cadence -- so the last value observed before EOF can sit a couple of frames
	// short of the margin and the check never fires. 0.25s is ~6 source frames of headroom, which
	// comfortably absorbs that aliasing while still being far inside the static title card at the
	// end of the clip, so the held image is the intended one.
	constexpr float FreezeMarginSeconds = 0.25f;

	// How long the Restarting state waits for an issued Seek to actually rewind the clock before
	// giving up and reopening the source outright.
	constexpr float RestartTimeoutSeconds = 2.f;
}

const TCHAR* UTitleScreenWidget::GetMediaPlayerAssetPath() const
{
	return TitleMediaPlayerPath;
}

const TCHAR* UTitleScreenWidget::GetMediaSourceAssetPath() const
{
	return TitleMediaSourcePath;
}

const TCHAR* UTitleScreenWidget::GetMediaTextureAssetPath() const
{
	return TitleMediaTexturePath;
}

FString UTitleScreenWidget::GetHintTextString() const
{
	return TEXT("PRESS ANY BUTTON TO START");
}

void UTitleScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Reset each construct, same as the base's own bVideoDimensionsApplied -- covers the (currently
	// theoretical) case of this widget being reused rather than recreated.
	VideoState = EVideoState::Opening;
}

void UTitleScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UpdateVideoCycle();
}

void UTitleScreenWidget::UpdateVideoCycle()
{
	if (IsFadeTriggered() || !MediaPlayer)
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

			// Logged so the cycle's timing is verifiable from a log file rather than only by
			// watching it -- pair this with the "resuming" line in RestartVideo.
			UE_LOG(LogTemp, Log, TEXT("[TITLE] Froze on final frame at %.3fs of %.3fs -- holding %.0fs."),
				MediaPlayer->GetTime().GetTotalSeconds(), Duration.GetTotalSeconds(), FreezeHoldSeconds);
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

	case EVideoState::Restarting:
	{
		const FTimespan Duration = MediaPlayer->GetDuration();
		if (Duration <= FTimespan::Zero())
		{
			break;
		}

		// Only hand back to Playing once the clock has genuinely rewound clear of the freeze point.
		// Resuming any earlier just re-triggers the freeze check on the same near-the-end timestamp.
		const FTimespan FreezeAt = Duration - FTimespan::FromSeconds(FreezeMarginSeconds);
		if (MediaPlayer->GetTime() < FreezeAt)
		{
			VideoState = EVideoState::Playing;
			break;
		}

		if (World->GetTimeSeconds() - RestartRequestTime >= RestartTimeoutSeconds)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TITLE] Seek did not rewind within %.0fs -- reopening the source."), RestartTimeoutSeconds);
			if (MediaSource)
			{
				MediaPlayer->OpenSource(MediaSource);
			}
			VideoState = EVideoState::Opening;
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

	UE_LOG(LogTemp, Log, TEXT("[TITLE] Hold finished -- replaying from the start."));

	// Not straight to Playing: the seek is asynchronous, so wait for the clock to actually rewind.
	// See the Restarting enumerator's comment.
	if (const UWorld* World = GetWorld())
	{
		RestartRequestTime = World->GetTimeSeconds();
	}
	VideoState = EVideoState::Restarting;
}

void UTitleScreenWidget::HandleMediaEndReached()
{
	if (IsFadeTriggered())
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
