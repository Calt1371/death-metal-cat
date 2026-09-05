#include "TitleIntroCombinedWidget.h"

#include "Components/TextBlock.h"
#include "MediaPlayer.h"
#include "MediaSource.h"

namespace
{
	// Reuses the title screen's original assets -- only MS_TitleVideo's own FileMediaSource file
	// path changed (repointed at the merged clip by AgentScripts, not a new asset).
	const TCHAR* CombinedMediaPlayerPath = TEXT("/Game/UI/TitleScreen/MP_TitleVideo.MP_TitleVideo");
	const TCHAR* CombinedMediaSourcePath = TEXT("/Game/UI/TitleScreen/MS_TitleVideo.MS_TitleVideo");
	const TCHAR* CombinedMediaTexturePath = TEXT("/Game/UI/TitleScreen/MT_TitleVideo.MT_TitleVideo");

	// Where the title portion ends and the intro portion begins in the merged file. The original
	// title clip is 5.041667s; the merge re-encoded both clips to a constant 30fps, which lands the
	// actual cut somewhere between frame 151 (5.0333s) and frame 152 (5.0667s) -- 5.05s sits safely
	// in that gap. Precision beyond this doesn't matter: CombinedFreezeMarginSeconds below already
	// exists to absorb exactly this kind of slack.
	constexpr float TitleSegmentDuration = 5.05f;

	// Same shape as UTitleScreenWidget's original loop constants, just renamed -- UE compiles the
	// module as a unity build (every .cpp concatenated into one translation unit for a full/fresh
	// build, though an adaptive/incremental build can keep files like this one and
	// TitleScreenWidget.cpp separate and hide the collision), so identically-named anonymous-
	// namespace constants in different files collide. UTitleScreenWidget is orphaned but still
	// compiled (see its own class comment), so this file's copies need distinct names rather than
	// deleting the other side.
	constexpr float CombinedFreezeHoldSeconds = 20.f;
	constexpr float CombinedFreezeMarginSeconds = 0.25f;
	constexpr float CombinedRestartTimeoutSeconds = 2.f;
}

const TCHAR* UTitleIntroCombinedWidget::GetMediaPlayerAssetPath() const
{
	return CombinedMediaPlayerPath;
}

const TCHAR* UTitleIntroCombinedWidget::GetMediaSourceAssetPath() const
{
	return CombinedMediaSourcePath;
}

const TCHAR* UTitleIntroCombinedWidget::GetMediaTextureAssetPath() const
{
	return CombinedMediaTexturePath;
}

FString UTitleIntroCombinedWidget::GetHintTextString() const
{
	return TEXT("PRESS ANY BUTTON TO START");
}

void UTitleIntroCombinedWidget::NativeConstruct()
{
	Super::NativeConstruct();

	LoopState = ELoopState::Opening;
}

void UTitleIntroCombinedWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UpdateLoopCycle();
}

void UTitleIntroCombinedWidget::UpdateLoopCycle()
{
	if (IsFadeTriggered() || LoopState == ELoopState::Committed || !MediaPlayer)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	switch (LoopState)
	{
	case ELoopState::Opening:
	{
		if (MediaPlayer->GetDuration() > FTimespan::Zero())
		{
			LoopState = ELoopState::Playing;
		}
		break;
	}

	case ELoopState::Playing:
	{
		const FTimespan FreezeAt = FTimespan::FromSeconds(TitleSegmentDuration - CombinedFreezeMarginSeconds);
		if (MediaPlayer->GetTime() >= FreezeAt)
		{
			// Pause rather than stop -- keeps the player open and seekable, and keeps the last
			// decoded frame resident in the texture. Same rationale UTitleScreenWidget's freeze used.
			MediaPlayer->SetRate(0.f);
			FreezeStartTime = World->GetTimeSeconds();
			LoopState = ELoopState::Frozen;

			UE_LOG(LogTemp, Log, TEXT("[TITLE] Froze on final title frame at %.3fs -- holding %.0fs."),
				MediaPlayer->GetTime().GetTotalSeconds(), CombinedFreezeHoldSeconds);
		}
		break;
	}

	case ELoopState::Frozen:
	{
		if (World->GetTimeSeconds() - FreezeStartTime >= CombinedFreezeHoldSeconds)
		{
			RestartLoop();
		}
		break;
	}

	case ELoopState::Restarting:
	{
		const FTimespan FreezeAt = FTimespan::FromSeconds(TitleSegmentDuration - CombinedFreezeMarginSeconds);
		if (MediaPlayer->GetTime() < FreezeAt)
		{
			LoopState = ELoopState::Playing;
			break;
		}

		if (World->GetTimeSeconds() - RestartRequestTime >= CombinedRestartTimeoutSeconds)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TITLE] Seek did not rewind within %.0fs -- reopening the source."), CombinedRestartTimeoutSeconds);
			if (MediaSource)
			{
				MediaPlayer->OpenSource(MediaSource);
			}
			LoopState = ELoopState::Opening;
		}
		break;
	}

	case ELoopState::PendingCommit:
	{
		// Confirms the Seek(Zero)+SetRate(1.f) NotifyAnyInput issued on entry to this state has
		// genuinely taken hold -- same confirmation check Restarting uses, just landing on Committed
		// (never re-freezes) instead of Playing (which would). See PendingCommit's own comment.
		const FTimespan FreezeAt = FTimespan::FromSeconds(TitleSegmentDuration - CombinedFreezeMarginSeconds);
		if (MediaPlayer->GetTime() < FreezeAt)
		{
			LoopState = ELoopState::Committed;
			break;
		}

		if (World->GetTimeSeconds() - PendingCommitRequestTime >= CombinedRestartTimeoutSeconds)
		{
			UE_LOG(LogTemp, Warning, TEXT("[INTRO] Resume-from-freeze seek did not rewind within %.0fs -- reopening the source."), CombinedRestartTimeoutSeconds);
			if (MediaSource)
			{
				MediaPlayer->OpenSource(MediaSource);
			}
			// Commit regardless -- the player already accepted "any input" and told the player so
			// (hint text already changed); there's no going back to the title loop from here even if
			// the reopen itself has the same reliability problem this whole class exists to avoid.
			LoopState = ELoopState::Committed;
		}
		break;
	}

	case ELoopState::Committed:
		break;
	}
}

void UTitleIntroCombinedWidget::RestartLoop()
{
	if (!MediaPlayer)
	{
		return;
	}

	const bool bSeeked = MediaPlayer->Seek(FTimespan::Zero());
	const bool bResumed = MediaPlayer->SetRate(1.f);

	if (!bSeeked || !bResumed)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TITLE] Seek/resume refused (seek=%d resume=%d) -- reopening the source."), bSeeked ? 1 : 0, bResumed ? 1 : 0);
		if (MediaSource)
		{
			MediaPlayer->OpenSource(MediaSource);
		}
		LoopState = ELoopState::Opening;
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[TITLE] Hold finished -- replaying from the start."));

	if (const UWorld* World = GetWorld())
	{
		RestartRequestTime = World->GetTimeSeconds();
	}
	LoopState = ELoopState::Restarting;
}

void UTitleIntroCombinedWidget::NotifyAnyInput()
{
	if (LoopState != ELoopState::Committed && LoopState != ELoopState::PendingCommit)
	{
		// First press: leave the loop for good. If we were mid-loop-playback (never froze), playback
		// is already at rate 1 and there's nothing to do beyond marking it Committed -- it just keeps
		// rolling forward into the intro portion with no OpenSource, nothing touching the broken
		// second-source path.
		//
		// If we WERE frozen (paused via SetRate(0.f)), SetRate(1.f) alone does not resume real
		// decoding -- confirmed live (2026-09-04): IsPlaying/IsPaused both flip back to
		// playing-shaped values with no error, but GetTime() then sits dead at the exact frozen
		// timestamp indefinitely. Pairing SetRate(1.f) with a Seek() to the same timestamp, and
		// separately with a Seek() to a nearby-but-different timestamp, both failed the same way --
		// confirmed live immediately after each. The only pairing proven to actually resume forward
		// playback anywhere in this file is RestartLoop()'s Seek(Zero) + SetRate(1.f), so resuming out
		// of a freeze reuses that exact call via PendingCommit (see its enum comment) rather than
		// guessing at another seek target -- UpdateLoopCycle confirms it actually took hold before
		// moving on to Committed.
		const bool bWasFrozen = (LoopState == ELoopState::Frozen);
		if (bWasFrozen)
		{
			MediaPlayer->Seek(FTimespan::Zero());
			MediaPlayer->SetRate(1.f);
			if (const UWorld* World = GetWorld())
			{
				PendingCommitRequestTime = World->GetTimeSeconds();
			}
			LoopState = ELoopState::PendingCommit;
		}
		else
		{
			LoopState = ELoopState::Committed;
		}

		if (HintText)
		{
			HintText->SetText(FText::FromString(TEXT("PRESS ANY BUTTON TO SKIP")));
		}

		UE_LOG(LogTemp, Log, TEXT("[INTRO] Committed to playing the intro portion through (was frozen: %d)."), bWasFrozen ? 1 : 0);
		return;
	}

	// Second press: skip straight to gameplay, same hand-off HandleMediaEndReached uses for a
	// natural (unskipped) finish. Also covers the (practically unreachable -- PendingCommit resolves
	// within a tick or two) edge of a second press landing before PendingCommit has resolved.
	UE_LOG(LogTemp, Log, TEXT("[INTRO] Skip pressed."));
	OnReadyForGameplayDelegate.ExecuteIfBound();
}

void UTitleIntroCombinedWidget::SetOnReadyForGameplayDelegate(FSimpleDelegate InDelegate)
{
	OnReadyForGameplayDelegate = InDelegate;
}

void UTitleIntroCombinedWidget::HandleMediaEndReached()
{
	if (IsFadeTriggered())
	{
		return;
	}

	if (LoopState != ELoopState::Committed && LoopState != ELoopState::PendingCommit)
	{
		// Reached true EOF before ever leaving the loop -- shouldn't normally happen (the loop's own
		// freeze margin should always catch it first), but if a hitch let it through, treat it the
		// same as a natural intro completion rather than getting stuck.
		UE_LOG(LogTemp, Warning, TEXT("[TITLE] Playback hit EOF before the freeze margin -- consider raising FreezeMarginSeconds."));
	}
	LoopState = ELoopState::Committed;

	UE_LOG(LogTemp, Log, TEXT("[INTRO] Reached the end of the file naturally."));
	OnReadyForGameplayDelegate.ExecuteIfBound();
}
