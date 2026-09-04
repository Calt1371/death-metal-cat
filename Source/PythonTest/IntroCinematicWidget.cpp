#include "IntroCinematicWidget.h"

namespace
{
	// Created by AgentScripts/ue_create_intro_cinematic_assets.py. Deliberately intro's OWN player,
	// not the title screen's -- see ATitleScreenGameMode's class comment for why: reopening a source
	// on ANY UMediaPlayer, same instance or a fresh one, right after UGameplayStatics::OpenLevel left
	// it permanently stuck (IsPlaying=true/IsReady=true/HasError=false forever, GetTime() never
	// leaving 0, texture never leaving its 2x2 placeholder) -- confirmed live (2026-09-04) across four
	// separate mitigations. The actual fix was eliminating that OpenLevel entirely, not which player
	// object this is.
	const TCHAR* IntroMediaPlayerPath = TEXT("/Game/UI/IntroCinematic/MP_IntroCinematic.MP_IntroCinematic");
	const TCHAR* IntroMediaSourcePath = TEXT("/Game/UI/IntroCinematic/MS_IntroCinematic.MS_IntroCinematic");
	const TCHAR* IntroMediaTexturePath = TEXT("/Game/UI/IntroCinematic/MT_IntroCinematic.MT_IntroCinematic");
}

const TCHAR* UIntroCinematicWidget::GetMediaPlayerAssetPath() const
{
	return IntroMediaPlayerPath;
}

const TCHAR* UIntroCinematicWidget::GetMediaSourceAssetPath() const
{
	return IntroMediaSourcePath;
}

const TCHAR* UIntroCinematicWidget::GetMediaTextureAssetPath() const
{
	return IntroMediaTexturePath;
}

FString UIntroCinematicWidget::GetHintTextString() const
{
	return TEXT("PRESS ANY BUTTON TO SKIP");
}

void UIntroCinematicWidget::SetOnCinematicEndDelegate(FSimpleDelegate InDelegate)
{
	OnCinematicEndDelegate = InDelegate;
}

void UIntroCinematicWidget::HandleMediaEndReached()
{
	if (OnCinematicEndDelegate.IsBound())
	{
		OnCinematicEndDelegate.Execute();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[INTRO] Cinematic finished but OnCinematicEndDelegate was never bound -- SetOnCinematicEndDelegate must be called right after CreateWidget."));
	}
}
