#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DamageTypes.h"
#include "HitImpactWidget.generated.h"

class UImage;

/**
 * The visual content of AHitImpactEffectActor's screen-space burst: a handful of thin "spark"
 * rectangles arranged radially around the widget's center, all sharing the same scale-up/fade-out
 * animation but each fixed at its own rotation -- reads as a quick impact flash without needing any
 * dedicated impact-VFX art, the same "no art yet, build it from primitives" approach this project
 * already used for the Rage ultimate's beam and the Fancy Attack laser (see
 * ADeathMetalCatCharacter::SpawnRageBeamEffect), just via UMG/Slate instead of a 3D mesh+material
 * since this is a 2D screen-space overlay, not a world-space effect.
 *
 * Built entirely in Initialize() (WidgetTree->ConstructWidget), same idiom and same reason as every
 * other hand-built widget in this project (UDamageNumberWidget, UGnarlyRankHUDWidget): Initialize()
 * runs before UMG builds this UUserWidget's underlying Slate tree from WidgetTree->RootWidget, while
 * NativeConstruct() would be too late.
 */
UCLASS()
class PYTHONTEST_API UHitImpactWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;

	/** Sets the spark tint for DamageAmount's Tier (same three-color convention as UDamageNumberWidget) and resets the burst to its starting (small, fully opaque) state. Call immediately after spawning, before the first UpdateImpactAlpha. */
	void InitHitImpact(EDamageTier Tier);

	/**
	 * Drives the burst's scale-up/fade-out: Alpha runs 0 (just spawned: small, fully opaque) to 1
	 * (end of life: fully grown, fully transparent). Called every Tick by the owning actor -- this
	 * widget has no timer/animation of its own, exactly like UDamageNumberWidget's externally-driven
	 * SetRenderOpacity.
	 */
	void UpdateImpactAlpha(float Alpha);

private:
	/** Six spark rectangles, each constructed once in Initialize() at its own fixed rotation (60 degrees apart) -- only their shared scale/opacity move per UpdateImpactAlpha call, rotation never changes after construction. */
	UPROPERTY()
	TArray<TObjectPtr<UImage>> Sparks;
};
