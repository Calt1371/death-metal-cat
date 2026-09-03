#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PauseMenuWidget.generated.h"

class UCanvasPanel;
class UImage;
class UTextBlock;
class UBorder;
class UProgressBar;
class UScrollBox;
class AGameplayPlayerController;

/** Which of the pause screen's three pages is currently showing -- see UPauseMenuWidget class comment. */
UENUM()
enum class EPausePage : uint8
{
	Main,
	Options,
	Controls,
};

/**
 * The pause screen itself: one background image (Pause_screen_DMC.png, which leaves the whole panel
 * below the PAUSED title/divider blank -- no menu content baked into the art at all) shared by all
 * three pages, with each page's own content drawn into that same empty panel region. Built entirely
 * in Initialize()
 * (WidgetTree->ConstructWidget), same approach as every other hand-built widget in this project
 * (UGnarlyRankHUDWidget, UFullscreenVideoWidgetBase) and for the same reason: Initialize() runs
 * before UMG builds the underlying Slate tree, NativeConstruct() would be too late.
 *
 * Everything is positioned with fixed pixel coordinates matching the background art's native
 * 1672x941 resolution, inside a USizeBox(1672x941) wrapped in a UScaleBox(ScaleToFit) -- the same
 * "fixed-size canvas scaled as a whole" technique UFullscreenVideoWidgetBase uses for its video
 * image, just via a SizeBox instead of a brush's desired-size override since there's no single
 * texture whose size to hook here.
 *
 * This widget owns ONLY the pages' visual state (CurrentPage, which row is selected/highlighted,
 * the live volume/brightness bars, the generated control list text) and exposes it through a small
 * navigation API (NavigateUp/Down/Left/Right, Confirm, GoBack) that AGameplayPlayerController calls
 * in response to its own raw key bindings -- see that class for why input is handled there rather
 * than here (this widget is never given Slate focus; input mode stays GameOnly throughout, exactly
 * like the title/intro screens, so nothing here does any hit-testing or click handling). Confirm()
 * on the main page's Resume/Quit To Title entries calls back into the owning controller
 * (SetOwningController) to actually perform those -- this widget has no authority to pause/unpause
 * or change level on its own, same push/pull separation already used for
 * UIntroCinematicWidget/AIntroCinematicGameMode.
 */
UCLASS()
class PYTHONTEST_API UPauseMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;

	/** Set once, right after CreateWidget -- lets Confirm() call back into the controller for Resume/Quit To Title, and lets the Controls page reach the possessed character's MoveMappingContext. Same pattern as UGnarlyRankHUDWidget::SetOwningCharacter/UIntroCinematicWidget::SetOwningGameMode. */
	void SetOwningController(AGameplayPlayerController* InController);

	/** Resets to the main page with RESUME selected and refreshes the highlight -- called every time the pause menu is (re)opened, so it never remembers a stale sub-page/selection from last time. */
	void ShowMainPage();

	// -- Navigation, called by AGameplayPlayerController's raw key handlers. All are no-ops for
	// whichever direction/page combination doesn't mean anything (e.g. NavigateLeft on the Controls
	// page) -- see the .cpp for exactly which page each one affects.
	void NavigateUp();
	void NavigateDown();
	void NavigateLeft();
	void NavigateRight();

	/** Activates whatever's currently selected -- only meaningful on the main page (Resume/Options/Controls/Quit To Title); no-op on the Options/Controls sub-pages, which have nothing to "confirm" (their rows are live sliders, not selectable actions). */
	void Confirm();

	/** Returns to the main page from Options/Controls; no-op if already on the main page. Called by the controller when the pause-toggle key is pressed while on a sub-page -- see that class for why the toggle key doubles as Back rather than this widget having its own dedicated cancel input. */
	void GoBack();

	EPausePage GetCurrentPage() const { return CurrentPage; }

private:
	// -- Construction helpers, called once each from Initialize() --
	void BuildMainPageWidgets(UCanvasPanel* ArtCanvas);
	void BuildOptionsPageWidgets(UCanvasPanel* ArtCanvas);
	void BuildControlsPageWidgets(UCanvasPanel* ArtCanvas);

	/** Shows/hides each page's widget group to match CurrentPage. Called after every page change. */
	void RefreshPageVisibility();

	/** Repositions MainHighlightBox/the flanking arrows over MainSelectedIndex's row. */
	void RefreshMainHighlight();

	/** Refreshes both bars/value texts from the live GameInstance settings and repositions OptionsHighlightBox over OptionsSelectedIndex's row. */
	void RefreshOptionsDisplay();

	/** Rebuilds ControlsText from the possessed character's MoveMappingContext's CURRENT mappings -- called every time the Controls page is opened (not cached), so it always reflects whatever the bindings actually are right now. See class comment on why this must be live rather than hardcoded. */
	void RefreshControlsList();

	/** Nudges master volume or brightness (whichever OptionsSelectedIndex points at) by Delta via the GameInstance, then refreshes the display. No-op off the Options page. */
	void AdjustSelectedOptionsValue(float Delta);

	/** "IA_SwordAttack" -> "Sword Attack" -- purely cosmetic readability pass over an input action asset's own object name, used by RefreshControlsList since UInputAction has no separate display-name property in this project. */
	static FString HumanizeActionName(const FString& RawAssetName);

	UPROPERTY()
	TObjectPtr<AGameplayPlayerController> OwningController;

	EPausePage CurrentPage = EPausePage::Main;

	/** 0=Resume, 1=Options, 2=Controls, 3=Quit To Title. */
	int32 MainSelectedIndex = 0;

	/** 0=Sound, 1=Brightness. */
	int32 OptionsSelectedIndex = 0;

	// -- Shared --
	UPROPERTY()
	TObjectPtr<UImage> BackgroundImage;

	// -- Main page --
	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> MainLabels;

	UPROPERTY()
	TObjectPtr<UBorder> MainHighlightBox;

	UPROPERTY()
	TObjectPtr<UTextBlock> MainArrowLeft;

	UPROPERTY()
	TObjectPtr<UTextBlock> MainArrowRight;

	// -- Options page --
	UPROPERTY()
	TObjectPtr<UTextBlock> SoundLabel;

	UPROPERTY()
	TObjectPtr<UProgressBar> SoundBar;

	UPROPERTY()
	TObjectPtr<UTextBlock> SoundValueText;

	UPROPERTY()
	TObjectPtr<UTextBlock> BrightnessLabel;

	UPROPERTY()
	TObjectPtr<UProgressBar> BrightnessBar;

	UPROPERTY()
	TObjectPtr<UTextBlock> BrightnessValueText;

	UPROPERTY()
	TObjectPtr<UBorder> OptionsHighlightBox;

	// -- Controls page --
	UPROPERTY()
	TObjectPtr<UScrollBox> ControlsScrollBox;

	UPROPERTY()
	TObjectPtr<UTextBlock> ControlsText;
};
