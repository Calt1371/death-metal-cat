#pragma once

#include "CoreMinimal.h"
#include "AnyInputPlayerControllerBase.h"
#include "TitleScreenPlayerController.generated.h"

/** Title-screen specialization of AAnyInputPlayerControllerBase -- see that class's comment for what it catches and why. Any input notifies ATitleScreenGameMode to leave the title screen. */
UCLASS()
class PYTHONTEST_API ATitleScreenPlayerController : public AAnyInputPlayerControllerBase
{
	GENERATED_BODY()

protected:
	virtual void OnAnyInputDetected() override;
	virtual const TCHAR* GetLogTag() const override { return TEXT("[TITLE]"); }
};
