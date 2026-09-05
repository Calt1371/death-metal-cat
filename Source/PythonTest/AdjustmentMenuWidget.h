#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AdjustmentMenuWidget.generated.h"

class UCanvasPanel;
class UImage;
class UTextBlock;
class UBorder;
class UProgressBar;
class USoundBase;

/**
 * The very first screen of the game (L_AdjustmentMenu, this project's GameDefaultMap): three
 * horizontal "sliders" (Brightness/SFX Volume/Music Volume, left-to-right, matching
 * Adjustment_Menu.png's baked-in icon/label art) that read and write the exact same
 * UDMCGameInstance settings the Pause Screen's Options page does (UPauseMenuWidget), so a change
 * on either screen is reflected on the other -- see UDMCGameInstance's class comment.
 *
 * Built entirely in Initialize() (WidgetTree->ConstructWidget), same convention as every other
 * hand-built widget in this project (UPauseMenuWidget, UGnarlyRankHUDWidget) -- Initialize() runs
 * before UMG builds the underlying Slate tree, NativeConstruct() would be too late. Positioned with
 * fixed pixel coordinates matching Adjustment_Menu.png's native 1672x941 resolution, inside a
 * USizeBox(1672x941) wrapped in a UScaleBox(ScaleToFit) -- the same "fixed-size canvas scaled as a
 * whole" technique UPauseMenuWidget uses for Pause_screen_DMC.png.
 *
 * Navigation axes are SWAPPED relative to the Pause Screen's Options page (UPauseMenuWidget::
 * NavigateUp/Down/Left/Right): here, Left/Right moves the highlight between the three sliders and
 * Up/Down adjusts the selected one's value -- matching this screen's horizontal layout instead of
 * keeping the Pause Menu's vertical-list mapping. Confirmed live (2026-09-04) that the "consistent
 * axes" version actively misled a real player: with a horizontal row of sliders on screen, Left/
 * Right reads as "move between them" on sight, and a player who presses it expecting navigation but
 * gets a silent brightness nudge instead concludes the screen is stuck ("no clear way to move
 * between settings"). This is also the very first screen a player ever sees, before they've
 * touched the Pause Menu at all, so there is no existing muscle memory to preserve here in the
 * first place -- discoverability wins over cross-screen axis consistency.
 *
 * Live preview, per the brief: moving the SFX slider plays GunFirePreviewSound (the same raw
 * gunshot SoundWave asset Cayde's gun uses, /Game/Audio/SFX/gun_fire -- loaded directly here, NOT
 * through ADeathMetalCatCharacter's gameplay firing path, since there is no pawn/character on this
 * screen) as a one-shot via PlaySound2D every time the value changes, at the volume the just-applied
 * SetSFXVolume call leaves in effect (SC_SFX's live class-mix override applies to this one-shot the
 * same way it applies to any other SC_SFX-classed sound). Moving the Music slider doesn't need any
 * special handling here at all -- AAdjustmentMenuGameMode already has DMC_Music playing on loop the
 * moment this screen appears, and SetMusicVolume's SC_Music class-mix override live-updates that
 * already-playing AudioComponent's audible volume with no further code needed, the same mechanism
 * that makes cross-screen consistency work in the first place.
 *
 * This widget has no authority to change level at all -- unlike UPauseMenuWidget (whose Confirm()
 * calls back into its owning controller for Resume/Quit To Title), "continue to the title screen"
 * is the ONLY action this screen has, so AAdjustmentMenuGameMode's own Confirm key handler just
 * opens L_TitleScreen directly rather than routing back through this widget for a decision it isn't
 * making.
 */
UCLASS()
class PYTHONTEST_API UAdjustmentMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;

	/** Refreshes all three bars/value texts from the live GameInstance settings. Called once right after construction and again after every value change. */
	void RefreshDisplay();

	// -- Navigation, called by AAdjustmentMenuGameMode's raw key handlers (forwarded from
	// AAdjustmentMenuPlayerController). Up/Down adjusts the selected slider's value; Left/Right
	// moves the selection between sliders -- see class comment for why these axes are swapped
	// relative to the Pause Menu's Options page.
	void NavigateUp();
	void NavigateDown();
	void NavigateLeft();
	void NavigateRight();

private:
	/** Nudges whichever of Brightness/SFX/Music SelectedIndex points at by Delta via the GameInstance, plays the SFX preview if that's the one being adjusted, then refreshes the display. Called by NavigateUp/Down. */
	void AdjustSelectedValue(float Delta);

	/** 0=Brightness, 1=SFX Volume, 2=Music Volume -- left-to-right order, matching Adjustment_Menu.png. */
	int32 SelectedIndex = 0;

	UPROPERTY()
	TObjectPtr<UImage> BackgroundImage;

	UPROPERTY()
	TObjectPtr<UBorder> HighlightBox;

	UPROPERTY()
	TObjectPtr<UProgressBar> BrightnessBar;

	UPROPERTY()
	TObjectPtr<UTextBlock> BrightnessValueText;

	UPROPERTY()
	TObjectPtr<UProgressBar> SFXBar;

	UPROPERTY()
	TObjectPtr<UTextBlock> SFXValueText;

	UPROPERTY()
	TObjectPtr<UProgressBar> MusicBar;

	UPROPERTY()
	TObjectPtr<UTextBlock> MusicValueText;

	UPROPERTY()
	TObjectPtr<UTextBlock> ContinueHintText;

	/** /Game/Audio/SFX/gun_fire -- see class comment. Loaded once in Initialize(). */
	UPROPERTY()
	TObjectPtr<USoundBase> GunFirePreviewSound;
};
