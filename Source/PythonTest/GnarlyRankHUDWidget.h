#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GnarlyRankHUDWidget.generated.h"

class UTextBlock;
class UCanvasPanel;
class UImage;
class UTexture2D;
class ADeathMetalCatCharacter;

/**
 * Persistent HUD element showing a static "GNARLY RANK" logo, the player's current GnarlyRank
 * (letter grade D/C/B/A/S) with hit-count progress toward the next rank, an escalating face
 * portrait, and a passive Level/XP readout ("LVL 5 (120/250 XP)"). Added to the viewport once
 * (from ADeathMetalCatCharacter::NotifyControllerChanged) and polled every tick thereafter --
 * unlike ADamageNumberActor's floating numbers, this is never spawned/destroyed per event.
 *
 * Built entirely in Initialize() (WidgetTree->ConstructWidget), applying the lesson learned from
 * UDamageNumberWidget's invisible-widget bug: Initialize() runs before UMG builds this
 * UUserWidget's underlying Slate tree from WidgetTree->RootWidget, while NativeConstruct() runs
 * after and would be too late (that's exactly what left DamageNumberWidget's TextBlock valid but
 * unmeasured/unrendered). RootWidget here is a UCanvasPanel; the logo and rank text are explicit
 * AddChildToCanvas() children of it, and the portrait Image is nested inside a UBorder (for the
 * frame) which is itself an AddChildToCanvas() child -- both attachment patterns that bug report
 * called out (root widget, or child of a panel) are demonstrated and verified here.
 */
UCLASS()
class PYTHONTEST_API UGnarlyRankHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Set once, right after CreateWidget -- this widget polls the character's GnarlyRank/GnarlyHitCount every tick rather than the character pushing updates via a delegate. */
	void SetOwningCharacter(ADeathMetalCatCharacter* InCharacter);

private:
	/**
	 * Polls the owning character and refreshes whichever sub-elements have actually changed since
	 * the last call: Gnarly rank text + portrait (gated on GnarlyRank/GnarlyHitCount), and the
	 * Level/XP text (gated independently on CurrentLevel/CurrentXP) -- one single polling function
	 * for the whole widget (called from NativeTick/SetOwningCharacter/Initialize alike), not a
	 * separate/parallel update mechanism per sub-element, even though each piece's own gate is
	 * independent so unrelated changes don't force unnecessary reformatting of the other.
	 */
	void RefreshDisplay();

	/** Static "GNARLY RANK" logo graphic (T_GnarlyRank_Logo), shown above RankText -- set once in Initialize() and never reassigned, unlike PortraitImage. */
	UPROPERTY()
	TObjectPtr<UImage> LogoImage;

	UPROPERTY()
	TObjectPtr<UTextBlock> RankText;

	UPROPERTY()
	TObjectPtr<UImage> PortraitImage;

	/** T_GnarlyRank_0..4, loaded once in Initialize() -- direct 1:1 index-to-rank mapping, one image per rank. */
	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> RankPortraitTextures;

	/** Level/XP readout, e.g. "LVL 5 (120/250 XP)" -- purely passive/read-only, same as everything else on this HUD. */
	UPROPERTY()
	TObjectPtr<UTextBlock> LevelText;

	UPROPERTY()
	TObjectPtr<ADeathMetalCatCharacter> OwningCharacter;

	int32 LastSeenRank = -1;
	int32 LastSeenHitCount = -1;

	int32 LastSeenLevel = -1;
	float LastSeenXP = -1.f;
};
