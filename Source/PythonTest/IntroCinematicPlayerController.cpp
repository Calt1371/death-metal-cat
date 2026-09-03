#include "IntroCinematicPlayerController.h"

#include "IntroCinematicGameMode.h"
#include "Kismet/GameplayStatics.h"

void AIntroCinematicPlayerController::OnAnyInputDetected()
{
	if (AIntroCinematicGameMode* IntroGameMode = Cast<AIntroCinematicGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		IntroGameMode->HandleCinematicEnd();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[INTRO] Skip pressed but the game mode is not an AIntroCinematicGameMode -- check the map's World Settings."));
	}
}
