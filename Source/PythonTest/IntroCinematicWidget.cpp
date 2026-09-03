#include "IntroCinematicWidget.h"

#include "IntroCinematicGameMode.h"

namespace
{
	// Created by AgentScripts/ue_create_intro_cinematic_assets.py.
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

void UIntroCinematicWidget::SetOwningGameMode(AIntroCinematicGameMode* InGameMode)
{
	OwningGameMode = InGameMode;
}

void UIntroCinematicWidget::HandleMediaEndReached()
{
	if (OwningGameMode)
	{
		OwningGameMode->HandleCinematicEnd();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[INTRO] Cinematic finished but OwningGameMode was never set -- SetOwningGameMode must be called right after CreateWidget."));
	}
}
