#pragma once

#include "CoreMinimal.h"
#include "AnyInputPlayerControllerBase.h"
#include "IntroCinematicPlayerController.generated.h"

/** Intro-cinematic specialization of AAnyInputPlayerControllerBase -- see that class's comment for what it catches and why. Any input notifies AIntroCinematicGameMode that the player wants to skip. */
UCLASS()
class PYTHONTEST_API AIntroCinematicPlayerController : public AAnyInputPlayerControllerBase
{
	GENERATED_BODY()

protected:
	virtual void OnAnyInputDetected() override;
	virtual const TCHAR* GetLogTag() const override { return TEXT("[INTRO]"); }
};
