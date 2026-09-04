#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MediaAudioActor.generated.h"

class UMediaSoundComponent;
class UMediaPlayer;

/**
 * Minimal actor whose only job is hosting a UMediaSoundComponent -- the piece
 * UFullscreenVideoWidgetBase's video playback (UMediaPlayer + UMediaTexture) never provided on its
 * own: those two only ever decode/display VIDEO. A UMediaPlayer's audio only actually reaches the
 * speakers through a UMediaSoundComponent pointed at it, and since UMediaSoundComponent is an
 * ActorComponent, a UUserWidget can't own one directly -- this actor is that component's home,
 * spawned and destroyed by UFullscreenVideoWidgetBase alongside its own playback lifetime, exactly
 * the same "widget owns a small dedicated actor" shape as ADamageNumberActor/AHitImpactEffectActor
 * (WidgetComp/SoundComponent set directly as RootComponent in the constructor, no separate empty
 * root needed, since both are already USceneComponent subclasses).
 */
UCLASS()
class PYTHONTEST_API AMediaAudioActor : public AActor
{
	GENERATED_BODY()

public:
	AMediaAudioActor();

	/** Points this actor's sound component at Player, so whatever audio track that player decodes actually gets heard, and starts it. Safe to call again with a different player (or the same one) at any time. */
	void SetMediaPlayer(UMediaPlayer* Player);

private:
	UPROPERTY(VisibleAnywhere, Category = "Media Audio")
	TObjectPtr<UMediaSoundComponent> SoundComponent;
};
