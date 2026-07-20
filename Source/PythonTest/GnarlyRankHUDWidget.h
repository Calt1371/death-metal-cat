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
 * (letter grade D/C/B/A/S) with hit-count progress toward the next rank, and an escalating face
 * portrait. Added to the viewport once (from ADeathMetalCatCharacter::NotifyControllerChanged) and
 * polled every tick thereafter -- unlike ADamageNumberActor's floating numbers, this is never
 * spawned/destroyed per event.
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
	 * Rebuilds the displayed text AND the portrait image, but only when GnarlyRank/GnarlyHitCount
	 * actually changed since the last call -- the single update path driving both the rank
	 * text/meter and the portrait, per design (no separate/parallel update mechanism for the
	 * portrait). Avoids reformatting/reassigning anything every single tick for no reason.
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

	UPROPERTY()
	TObjectPtr<ADeathMetalCatCharacter> OwningCharacter;

	int32 LastSeenRank = -1;
	int32 LastSeenHitCount = -1;
};
