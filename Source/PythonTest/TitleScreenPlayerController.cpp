#include "TitleScreenPlayerController.h"

#include "TitleScreenGameMode.h"
#include "Kismet/GameplayStatics.h"

void ATitleScreenPlayerController::OnAnyInputDetected()
{
	if (ATitleScreenGameMode* TitleGameMode = Cast<ATitleScreenGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		TitleGameMode->HandleStartPressed();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[TITLE] Start pressed but the game mode is not an ATitleScreenGameMode -- check the map's World Settings."));
	}
}
