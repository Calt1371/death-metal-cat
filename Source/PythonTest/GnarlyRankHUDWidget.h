#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GnarlyRankHUDWidget.generated.h"

class UTextBlock;
class UCanvasPanel;
class UImage;
class UProgressBar;
class UBorder;
class UTexture2D;
class ADeathMetalCatCharacter;

/**
 * Persistent HUD element showing a full-screen low-health tint, a static "GNARLY RANK" logo, the
 * player's current GnarlyRank (letter grade D/C/B/A/S) with hit-count progress toward the next
 * rank, an escalating face portrait, a passive Level/XP readout ("LVL 5 (120/250 XP)"), and a
 * Health bar with numeric readout. Added to the viewport once (from
 * ADeathMetalCatCharacter::NotifyControllerChanged) and polled every tick thereafter -- unlike
 * ADamageNumberActor's floating numbers, this is never spawned/destroyed per event.
 *
 * Built entirely in Initialize() (WidgetTree->ConstructWidget), applying the lesson learned from
 * UDamageNumberWidget's invisible-widget bug: Initialize() runs before UMG builds this
 * UUserWidget's underlying Slate tree from WidgetTree->RootWidget, while NativeConstruct() runs
 * after and would be too late (that's exactly what left DamageNumberWidget's TextBlock valid but
 * unmeasured/unrendered). RootWidget here is a UCanvasPanel; the logo and rank text are explicit
 * AddChildToCanvas() children of it, and the portrait Image is nested inside a UBorder (for the
 * frame) which is itself an AddChildToCanvas() child -- both attachment patterns that bug report
 * called out (root widget, or child of a panel) are demonstrated and verified here. The low-health
 * tint is added as the FIRST canvas child (before the logo) so every other element here paints on
 * top of it rather than being obscured.
 *
 * Also shows a JRPG-style dialogue box (portrait + name + quip line) for Cayde's Quip Generator
 * lines (see QuipTypes.h / QuipLibrary.h). Unlike every other element on this HUD, the dialogue
 * box is PUSH-driven rather than polled: ShowQuip() is called directly by
 * ADeathMetalCatCharacter::TriggerQuip whenever a quip actually fires, since quips are one-shot
 * random events, not continuously-readable character state the way Health/GnarlyRank/Level are.
 * There is only ever one dialogue box instance (never spawned/destroyed per quip, consistent with
 * this whole widget's philosophy), so retriggering while one is already showing just resets its
 * timer and replaces the text -- it can never stack multiple boxes.
 *
 * Also owns the full-screen room-transition fade (RoomFadeImage), added LAST in Initialize() so
 * it paints over every other element here (including the low-health tint and quip box) --
 * unlike LowHealthTintImage's health-driven opacity, this is driven entirely by
 * ARoomProgressionManager calling StartRoomFadeOut/StartRoomFadeIn, same push-driven idea as
 * ShowQuip. See RoomProgressionManager::BeginRoomTransition for the orchestration.
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

	/**
	 * Displays Line in the JRPG-style dialogue box for DisplayDuration seconds before fading out
	 * over QuipFadeDuration. Called directly (push, not polled) by
	 * ADeathMetalCatCharacter::TriggerQuip. Calling this again while a quip is already showing
	 * just resets the timer and swaps the text -- see this class's header comment for why that
	 * can never stack multiple boxes.
	 */
	void ShowQuip(const FString& Line, float DisplayDuration);

	/** Begins smoothly ramping RoomFadeImage's opacity from its current value to fully opaque (1.0) over Duration seconds, driven from NativeTick. Called by ARoomProgressionManager::BeginRoomTransition at the start of a room transition. */
	void StartRoomFadeOut(float Duration);

	/** Begins smoothly ramping RoomFadeImage's opacity from its current value down to fully transparent (0.0) over Duration seconds, driven from NativeTick. Called by ARoomProgressionManager::BeginRoomTransition once the black pause is over and the new room is ready. */
	void StartRoomFadeIn(float Duration);

	/** Immediate, non-animated opacity set -- used to snap RoomFadeImage to a known state rather than animate to it (e.g. forcing fully black with no fade, if ever needed). */
	void SetRoomFadeOpacity(float Alpha);

	/**
	 * Shows the full-screen death overlay (dim backdrop + you_died image + "YOU DIED" in blood
	 * red), Dark-Souls-style: the backdrop eases in first, then -- after a short delay -- the
	 * image and text ease in together on top of it, arriving visibly after the screen rather than
	 * everything popping in at once. See UpdateDeathScreenFade for the actual ramp. Push-driven the
	 * same way as ShowQuip -- called directly by ADeathMetalCatCharacter::HandleDeath the moment
	 * Health reaches 0.
	 */
	void ShowDeathScreen();

	/** Hides the death overlay -- called by ADeathMetalCatCharacter::HandleRespawn right before ResetToStartingRoom kicks off the room-transition fade-out, so it disappears cleanly instead of lingering behind (and briefly showing through during fade-in of) the reset room. */
	void HideDeathScreen();

	/** True from ShowDeathScreen until UpdateDeathScreenFade's ramps both finish -- ADeathMetalCatCharacter::HandleDeathContinuePressed checks this before allowing a button press to respawn, so mashing a button the instant the player dies can't skip or cut the fade-in short. */
	bool IsDeathScreenFadeInProgress() const { return bDeathScreenActive; }

	/**
	 * TEMPORARY (tonight's rough draft only): shows the full-screen "Coming Soon" placeholder
	 * (opaque black backdrop + Coming_Soon.png, letterboxed to fill the screen, + a
	 * "PRESS ANY BUTTON TO RETURN TO TITLE" hint line) -- see ADeathMetalCatCharacter::
	 * ShowComingSoonScreen / ARoomProgressionManager::HandleRoomTransitionFadeOutComplete. Unlike
	 * ShowDeathScreen this is a simple immediate show, no eased fade-in and no matching Hide: this
	 * draft has nowhere further to send the player, so the screen just stays up until the player
	 * presses any button and the level changes out from under it entirely.
	 */
	void ShowComingSoonScreen();

private:
	/**
	 * Polls the owning character and refreshes whichever sub-elements have actually changed since
	 * the last call: Gnarly rank text + portrait (gated on GnarlyRank/GnarlyHitCount), the
	 * Level/XP text (gated independently on CurrentLevel/CurrentXP), and the Health bar/text/
	 * low-health tint (gated independently again, on Health/MaxHealth, since all three of those
	 * derive from the same two source values) -- one single polling function for the whole widget
	 * (called from NativeTick/SetOwningCharacter/Initialize alike), not a separate/parallel update
	 * mechanism per sub-element, even though each piece's own gate is independent so unrelated
	 * changes don't force unnecessary reformatting of the others.
	 */
	void RefreshDisplay();

	/** Per-tick fade timing for the quip dialogue box -- see ShowQuip. No-ops entirely while bQuipShowing is false. */
	void UpdateQuipFade();

	/** Per-tick fade timing for RoomFadeImage -- see StartRoomFadeOut/StartRoomFadeIn. No-ops entirely while bRoomFadeActive is false. */
	void UpdateRoomFade();

	/** Per-tick staged ease-in for the death screen -- see ShowDeathScreen. No-ops entirely while bDeathScreenActive is false; clears that flag itself once both the backdrop and image/text ramps have fully completed. */
	void UpdateDeathScreenFade();

	/** Applies Alpha as the render opacity of every quip dialogue box element in one call -- QuipPortraitImage is deliberately excluded since it's nested inside QuipPortraitFrame via SetContent, and Slate render opacity already cascades to children. */
	void SetQuipVisualsOpacity(float Alpha);

	/** Shared visibility toggle for DeathScreenBackdrop/DeathScreenImage/DeathScreenText -- same Collapsed-when-hidden approach as SetQuipVisualsOpacity (a Collapsed widget isn't painted or hit-tested at all), used by both ShowDeathScreen/HideDeathScreen instead of each duplicating the three-element toggle. */
	void SetDeathScreenVisible(bool bVisible);

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

	/** Full-screen tint, a plain solid-color UImage (engine's WhiteSquareTexture tinted via SetColorAndOpacity) stretched to fill the whole canvas. Added as the first canvas child so everything else paints on top of it. Opacity ramps with how far below LowHealthThreshold Health is -- see RefreshDisplay. */
	UPROPERTY()
	TObjectPtr<UImage> LowHealthTintImage;

	/** Health fill bar, below the Level/XP text. Percent and fill color (green -> yellow -> red) both driven by Health/MaxHealth in RefreshDisplay. */
	UPROPERTY()
	TObjectPtr<UProgressBar> HealthBar;

	/** Numeric readout alongside HealthBar, e.g. "72/100". */
	UPROPERTY()
	TObjectPtr<UTextBlock> HealthText;

	/** Rage fill bar, directly below HealthBar. Percent is polled/set in RefreshDisplay same as Health; fill COLOR is instead hue-cycled every tick in UpdateRageBarVisuals regardless of fill amount ("rainbow-colored... not a static color" -- unlike Health's fixed color-by-percent lerp). */
	UPROPERTY()
	TObjectPtr<UProgressBar> RageBar;

	/** Static "RAGE" word label alongside RageBar -- unlike Health (which has no word label, just the numeric readout), Rage was explicitly asked to be labeled. */
	UPROPERTY()
	TObjectPtr<UTextBlock> RageLabelText;

	/** Scraps currency icon (T_ScrapsCurrency), directly below RageBar -- set once in Initialize() and never reassigned. */
	UPROPERTY()
	TObjectPtr<UImage> ScrapsImage;

	/** Numeric Scraps count, alongside ScrapsImage. */
	UPROPERTY()
	TObjectPtr<UTextBlock> ScrapsText;

	/** Change-gated poll for ScrapsText, same pattern as LastSeenHealth/LastSeenRage. */
	int32 LastSeenScraps = -1;

	/** Accumulates every tick (NativeTick's InDeltaTime) -- drives both the hue-cycle and the full-bar pulsate/shake sine waves in UpdateRageBarVisuals. */
	float RageAnimTime = 0.f;

	/** Per-tick hue-cycle for RageBar's fill color, plus a small scale-pulse + positional jitter on the whole widget while OwningCharacter->IsRageFull() -- see class doc. Runs every tick regardless of whether Rage/RageMax actually changed (unlike RefreshDisplay's change-gated polling), since the hue must keep advancing even while the numeric value sits still at 0 or at max. */
	void UpdateRageBarVisuals(float DeltaTime);

	/** Shared gate for RageBar's percent -- polled the same change-gated way as HealthBar/HealthText above. */
	float LastSeenRage = -1.f;
	float LastSeenRageMax = -1.f;

	UPROPERTY()
	TObjectPtr<ADeathMetalCatCharacter> OwningCharacter;

	int32 LastSeenRank = -1;
	int32 LastSeenHitCount = -1;

	int32 LastSeenLevel = -1;
	float LastSeenXP = -1.f;

	/** Shared gate for HealthBar/HealthText/LowHealthTintImage -- all three derive from these same two values. */
	float LastSeenHealth = -1.f;
	float LastSeenMaxHealth = -1.f;

	// -- Quip dialogue box --

	/** Wide rounded dark panel behind the whole dialogue box (portrait + text together), so the two read as one connected element rather than two separate floating boxes. */
	UPROPERTY()
	TObjectPtr<UBorder> QuipBoxBackground;

	/** Smaller rounded frame around QuipPortraitImage, nested via SetContent -- same "child of a content panel" attachment pattern as the Gnarly rank PortraitImage/PortraitBorder above. */
	UPROPERTY()
	TObjectPtr<UBorder> QuipPortraitFrame;

	/** Cayde's dialogue portrait (T_CaydeDialoguePortrait) -- set once in Initialize() and never reassigned, there's only the one portrait. */
	UPROPERTY()
	TObjectPtr<UImage> QuipPortraitImage;

	/** Small accent-colored "CAYDE" name label, above QuipLineText. */
	UPROPERTY()
	TObjectPtr<UTextBlock> QuipNameText;

	/** The quip's own text -- larger, readable body text, word-wrapped within the box. */
	UPROPERTY()
	TObjectPtr<UTextBlock> QuipLineText;

	/** True from ShowQuip until the fade-out finishes; UpdateQuipFade no-ops entirely while false. */
	bool bQuipShowing = false;

	/** GetWorld()->GetTimeSeconds() at the moment ShowQuip was last called. */
	float QuipShowStartTime = 0.f;

	/** How long (seconds) the current quip stays fully visible before UpdateQuipFade starts fading it out -- the DisplayDuration passed into the most recent ShowQuip call. */
	float QuipDisplayDuration = 3.f;

	// -- Room transition fade --

	/** Full-screen black overlay for room transitions, added LAST in Initialize() so it paints over every other element on this HUD. Plain solid-color UImage (engine WhiteSquareTexture tinted black), same construction technique as LowHealthTintImage. Starts fully transparent. */
	UPROPERTY()
	TObjectPtr<UImage> RoomFadeImage;

	/** True while StartRoomFadeOut/StartRoomFadeIn's animated ramp is in progress; UpdateRoomFade no-ops entirely while false. */
	bool bRoomFadeActive = false;

	/** GetWorld()->GetTimeSeconds() at the moment the current fade (in or out) started. */
	float RoomFadeStartTime = 0.f;

	/** How long (seconds) the current fade animation takes -- the Duration passed into the most recent StartRoomFadeOut/StartRoomFadeIn call. */
	float RoomFadeDuration = 0.35f;

	/** Opacity StartRoomFadeOut/StartRoomFadeIn began animating from -- lets UpdateRoomFade lerp correctly even if a fade is (re)started mid-animation rather than always assuming a 0/1 starting point. */
	float RoomFadeStartOpacity = 0.f;

	/** Opacity the current fade animation is ramping TOWARD -- 1.0 for a fade-out (to black), 0.0 for a fade-in (back to the scene). */
	float RoomFadeTargetOpacity = 0.f;

	// -- Death screen --

	/** Full-screen dark dim behind DeathScreenImage/DeathScreenText, so both stay legible over whatever's on screen at the moment of death -- same flat-solid-color construction as LowHealthTintImage/RoomFadeImage, just a fixed (non-animated) opacity rather than one driven by Health or a fade ramp. Added LAST in Initialize(), after even RoomFadeImage, so the death screen paints on top of the room-transition fade too. */
	UPROPERTY()
	TObjectPtr<UImage> DeathScreenBackdrop;

	/** you_died_death_metal_cat.png, imported as /Game/UI/DeathScreen/T_YouDied -- see AgentScripts/ue_import_death_screen.py. Centered, above DeathScreenText. */
	UPROPERTY()
	TObjectPtr<UImage> DeathScreenImage;

	/** "YOU DIED" in blood red, centered below DeathScreenImage. */
	UPROPERTY()
	TObjectPtr<UTextBlock> DeathScreenText;

	/** True from ShowDeathScreen until UpdateDeathScreenFade's ramps both finish; UpdateDeathScreenFade no-ops entirely while false. Cleared early (without finishing the ramp) by HideDeathScreen if death is interrupted by a respawn before the fade-in completes. */
	bool bDeathScreenActive = false;

	/** GetWorld()->GetTimeSeconds() at the moment the most recent ShowDeathScreen call fired -- both the backdrop and image/text ramps below measure their own elapsed time from this same instant, just with the image/text ramp additionally offset by DeathContentFadeInDelay. */
	float DeathScreenShowStartTime = 0.f;

	// -- Coming Soon screen (temporary, tonight's rough draft only) --

	/** Full-screen opaque black behind ComingSoonImage -- same full-screen-stretch construction as RoomFadeImage/DeathScreenBackdrop, just fully opaque and never animated. Added LAST in Initialize(), after even DeathScreenBackdrop, so this paints over the whole HUD. Hidden by default. */
	UPROPERTY()
	TObjectPtr<UImage> ComingSoonBackdrop;

	/** Coming_Soon.png (imported as /Game/UI/ComingSoon/T_ComingSoon -- see AgentScripts/ue_import_coming_soon.py), letterboxed to fill the whole screen via UImage's default ScaleToFit stretch inside a full-screen (anchors 0,0-1,1) canvas slot. Hidden by default. */
	UPROPERTY()
	TObjectPtr<UImage> ComingSoonImage;

	/** "PRESS ANY BUTTON TO RETURN TO TITLE", bottom-centre, same styling as UFullscreenVideoWidgetBase's hint line (white text, black outline) for visual consistency with the title/intro screens. Hidden by default. */
	UPROPERTY()
	TObjectPtr<UTextBlock> ComingSoonHintText;
};
