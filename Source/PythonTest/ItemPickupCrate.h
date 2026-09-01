#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ItemTypes.h"
#include "ItemPickupCrate.generated.h"

class UPaperSpriteComponent;
class UBoxComponent;
class ADeathMetalCatCharacter;

/** One weighted entry in a crate's drop table -- see AItemPickupCrate::DropTable. */
USTRUCT(BlueprintType)
struct FCrateDropEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Table")
	EPickupResultType Type = EPickupResultType::Scraps;

	/** This entry's share of a pickup is Weight / (sum of every entry's Weight) -- doesn't need to sum to any particular total. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Table", meta = (ClampMin = "0"))
	float Weight = 1.f;
};

/**
 * World pickup crate placed at a room's PickupSpawn markers (see AEncounterSpawnMarker/
 * EEncounterMarkerType and Tools/spawn_encounter_actors.py -- population=="pickup" markers now
 * spawn this class instead of being skipped). A single static sprite (skull-question-item-crate),
 * not animated -- there's no separate "opened" art, so touching it resolves and destroys
 * immediately rather than playing an open animation first.
 *
 * On overlap with the player, rolls DropTable (weighted random) and either adds Scraps directly or
 * hands off to ADeathMetalCatCharacter::ResolvePickup for one of the special items, then destroys
 * itself. DropTable's default values are a documented placeholder weighting -- NineLifeStim
 * intentionally the rarest per the design ask ("harder to find"), Scraps the common baseline drop.
 */
UCLASS()
class PYTHONTEST_API AItemPickupCrate : public AActor
{
	GENERATED_BODY()

public:
	AItemPickupCrate();

protected:
	/** Re-applies SpriteTintColor here rather than trusting the constructor's own SetSpriteColor call alone -- a Blueprint's Class Defaults edit to an EditDefaultsOnly property only takes effect AFTER the native constructor has already run, so a construction-time-only SetSpriteColor call can silently go stale the moment someone tunes SpriteTintColor in the editor (the same pitfall ADeathMetalCatCharacter::BaseSpriteTintColor hit and was fixed the same way). */
	virtual void BeginPlay() override;

	/** Bound to OverlapVolume's OnComponentBeginOverlap -- resolves and destroys on the first overlapping ADeathMetalCatCharacter, ignores everything else (enemies, projectiles, etc.). */
	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Rolls DropTable (weighted random over Weight/(sum of Weights)), grants the result to Player (AddScraps directly for Scraps, ResolvePickup otherwise), then destroys this crate. */
	void ResolveAndDestroy(ADeathMetalCatCharacter* Player);

	/** Static single-sprite visual (skull-question-item-crate) -- a UPaperSpriteComponent, not a UPaperFlipbookComponent, since the source art is one static image with no frames. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPaperSpriteComponent> SpriteComponent;

	/** Root component; overlap-only (no blocking), so the player can walk through it while it resolves rather than needing a separate trigger volume alongside solid collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> OverlapVolume;

	/**
	 * Weighted drop table. Populated with a full documented placeholder weighting in the
	 * constructor (rather than requiring every placed/spawned crate to author it from scratch) --
	 * see the class doc for the design intent (NineLifeStim rarest, Scraps commonest). Still fully
	 * tunable per-Blueprint/per-instance in the Class Defaults or Details panel.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Drop Table")
	TArray<FCrateDropEntry> DropTable;

	/** Scraps granted when DropTable resolves to EPickupResultType::Scraps. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Drop Table", meta = (ClampMin = "0"))
	int32 ScrapsPerDrop = 10;

	/**
	 * Tint applied to SpriteComponent in the constructor -- same fix and same root cause as
	 * ADeathMetalCatCharacter::BaseSpriteTintColor (Unlit/Emissive-driven Paper2D sprite material
	 * blooming out badly at full-bright SpriteColor once combined with this level's dynamic
	 * auto-exposure), applied here to the item crate sprite for visual consistency with Cayde's own
	 * dimmed tint. Placeholder value, tune freely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual")
	FLinearColor SpriteTintColor = FLinearColor(0.025f, 0.025f, 0.025f, 1.f);
};
