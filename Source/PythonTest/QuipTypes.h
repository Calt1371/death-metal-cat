#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "QuipTypes.generated.h"

/**
 * Trigger types for the Quip Generator agent (GDD Pillar 1). Enumerator names (Kill/Damage/
 * Environment) match the lowercase trigger_type values Tools/quip_generator.py accepts and keys
 * its JSON output by -- AgentScripts/ue_import_quip_datatable.py capitalizes each JSON key
 * ("kill" -> "Kill") when writing DataTable rows, so if these enumerator names ever change, that
 * importer's capitalization mapping needs to change too.
 */
UENUM(BlueprintType)
enum class EQuipTriggerType : uint8
{
	Kill,
	Damage,
	Environment
};

/**
 * DataTable row structure for imported Quip Generator output -- one row per curated quip.
 * Populated entirely offline: AgentScripts/ue_import_quip_datatable.py reads
 * Tools/quip_batch_curation.json (the hand-curated output of Tools/quip_generator.py, which
 * calls the Claude API directly) and fills a DataTable of this row type via Unreal's built-in
 * JSON DataTable importer. No live API dependency at runtime -- UQuipLibrary::GetRandomQuip only
 * ever reads from this already-imported DataTable, matching the GDD's "agents run offline,
 * pre-build" principle.
 */
USTRUCT(BlueprintType)
struct FQuipDataTableRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Which trigger this quip belongs to -- filtered on by UQuipLibrary::GetRandomQuip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quip")
	EQuipTriggerType TriggerType = EQuipTriggerType::Kill;

	/** The quip's text, <=40 words per the Quip Generator's own output spec. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quip")
	FString Line;

	/** Invented placeholder sound-bite tag name (see quip_generator.py's sound_tag naming convention) -- doesn't map to a real audio asset yet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quip")
	FString SoundTag;
};
