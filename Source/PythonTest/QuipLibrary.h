#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "QuipTypes.h"
#include "QuipLibrary.generated.h"

class UDataTable;

/**
 * Runtime access to the offline-imported Quip DataTable (see QuipTypes.h and
 * AgentScripts/ue_import_quip_datatable.py). Zero live API dependency: GetRandomQuip only ever
 * reads rows already baked into QuipTable by the import script, never calls Claude or any other
 * service.
 */
UCLASS()
class PYTHONTEST_API UQuipLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Picks a uniformly random row from QuipTable whose TriggerType matches -- straight random
	 * pick every time, no "avoid repeating the last line" logic. Returns false (leaving
	 * OutLine/OutSoundTag untouched) if QuipTable is null or has no rows matching TriggerType.
	 */
	UFUNCTION(BlueprintCallable, Category = "Quip")
	static bool GetRandomQuip(const UDataTable* QuipTable, EQuipTriggerType TriggerType, FString& OutLine, FString& OutSoundTag);
};
