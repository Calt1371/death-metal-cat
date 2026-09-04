#include "TitleScreenPlayerController.h"

#include "TitleScreenGameMode.h"
#include "Kismet/GameplayStatics.h"

void ATitleScreenPlayerController::OnAnyInputDetected()
{
	// Forwarded straight to the game mode's single combined widget, which decides what "any input"
	// currently means (leave the title loop, or skip the intro portion) -- see
	// ATitleScreenGameMode::HandleAnyInput / UTitleIntroCombinedWidget::NotifyAnyInput. This
	// controller gets re-armed after every press (see AAnyInputPlayerControllerBase::Rearm) since
	// one continuous screen can legitimately receive "any input" more than once.
	if (ATitleScreenGameMode* TitleGameMode = Cast<ATitleScreenGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		TitleGameMode->HandleAnyInput();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[TITLE] Input detected but the game mode is not an ATitleScreenGameMode -- check the map's World Settings."));
	}
}
