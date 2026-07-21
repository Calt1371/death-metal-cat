#include "QuipLibrary.h"

#include "Engine/DataTable.h"

bool UQuipLibrary::GetRandomQuip(const UDataTable* QuipTable, EQuipTriggerType TriggerType, FString& OutLine, FString& OutSoundTag)
{
	if (!QuipTable)
	{
		return false;
	}

	TArray<FQuipDataTableRow*> AllRows;
	QuipTable->GetAllRows(TEXT("UQuipLibrary::GetRandomQuip"), AllRows);

	TArray<FQuipDataTableRow*> Matching;
	Matching.Reserve(AllRows.Num());
	for (FQuipDataTableRow* Row : AllRows)
	{
		if (Row && Row->TriggerType == TriggerType)
		{
			Matching.Add(Row);
		}
	}

	if (Matching.Num() == 0)
	{
		return false;
	}

	const FQuipDataTableRow* Chosen = Matching[FMath::RandRange(0, Matching.Num() - 1)];
	OutLine = Chosen->Line;
	OutSoundTag = Chosen->SoundTag;
	return true;
}
