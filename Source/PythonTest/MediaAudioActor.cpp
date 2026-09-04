#include "MediaAudioActor.h"

#include "MediaSoundComponent.h"

AMediaAudioActor::AMediaAudioActor()
{
	SoundComponent = CreateDefaultSubobject<UMediaSoundComponent>(TEXT("SoundComponent"));
	RootComponent = SoundComponent;

	// This is a full-screen video's own soundtrack/dialogue (title/intro screens), not positional
	// 3D audio from something at a world location -- non-spatialized is correct here, and is also
	// this component's own default, so nothing further to set for that.
	SoundComponent->bAutoActivate = true;
}

void AMediaAudioActor::SetMediaPlayer(UMediaPlayer* Player)
{
	if (SoundComponent)
	{
		SoundComponent->SetMediaPlayer(Player);
		SoundComponent->Start();
	}
}
