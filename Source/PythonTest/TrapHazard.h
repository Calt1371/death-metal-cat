#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TrapHazard.generated.h"

class UBoxComponent;
class UPaperFlipbookComponent;
class UPaperFlipbook;

/**
 * Reusable environmental hazard trap: plays a looping PaperFlipbook animation (spikes extending/
 * retracting, an electric arc cycling, a spinning saw blade, etc.) and deals damage to the player
 * ONLY while both (a) the player is overlapping this trap's damage box, AND (b) the flipbook's
 * CURRENT frame falls within [DangerousFrameStart, DangerousFrameEnd] -- e.g. a spike trap should
 * only hurt the player once the spikes are actually extended, not during its retracted/idle
 * frames. Frame indices are 0-based, matching UPaperFlipbookComponent::GetPlaybackPositionInFrames().
 *
 * Overlap-based, not distance-based like ADeathMetalCatEnemyBase's melee contact -- that class's
 * own header documents a real Block/Block mismatch between the enemy and player CAPSULES (both
 * use the stock "Pawn" profile, which can never actually generate an overlap against each other).
 * This box instead uses "OverlapAllDynamic" -- the same profile ARoomExitTrigger/
 * AEncounterSpawnMarker already use successfully -- so a real BeginOverlap/EndOverlap against the
 * player is reliable here; distance-based gating isn't needed to work around that mismatch.
 *
 * Damage applies via the same UGameplayStatics::ApplyDamage path everything else in this game
 * uses; the player's own TakeDamage handles CanTakeDamage()/i-frames entirely, this class never
 * checks or duplicates that.
 *
 * One base class, many trap types: each Blueprint child (BP_Trap_SpikeColumn, BP_Trap_Electric,
 * BP_Trap_Saw, BP_Trap_SpikeFloor) just assigns its own Flipbook + DangerousFrameStart/End +
 * DamageBoxExtent -- no new code needed per trap type.
 */
UCLASS()
class PYTHONTEST_API ATrapHazard : public AActor
{
	GENERATED_BODY()

public:
	ATrapHazard();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** The trap's visual animation -- also drives the danger-frame check below via GetPlaybackPositionInFrames(). Set per Blueprint child, e.g. FB_Trap_SpikeColumn. */
	UPROPERTY(EditAnywhere, Category = "Trap Hazard")
	TObjectPtr<UPaperFlipbook> Flipbook;

	/** Half-extents (X/Y/Z) of the damage-overlap box. Resize per trap type -- e.g. a spike column's box should roughly match where the spikes actually reach at full extension, not the whole flipbook canvas. */
	UPROPERTY(EditAnywhere, Category = "Trap Hazard")
	FVector DamageBoxExtent = FVector(100.f, 100.f, 100.f);

	/** First flipbook frame (0-indexed, inclusive) considered dangerous. Outside [DangerousFrameStart, DangerousFrameEnd], the player takes no damage even while overlapping -- e.g. Spike Column's retracted/extending frames 0-2 and retracting frames 6-7 stay safe, only 3-5 (0-indexed; frames 4-6 in the 1-indexed asset naming) actually hurt. */
	UPROPERTY(EditAnywhere, Category = "Trap Hazard")
	int32 DangerousFrameStart = 0;

	/** Last flipbook frame (0-indexed, inclusive) considered dangerous. */
	UPROPERTY(EditAnywhere, Category = "Trap Hazard")
	int32 DangerousFrameEnd = 0;

	/** Damage applied to the player, via the existing ApplyDamage/TakeDamage path, once per ContactDamageCooldown while overlapping AND the current frame is within the dangerous range. Placeholder value, tune freely. */
	UPROPERTY(EditAnywhere, Category = "Trap Hazard")
	float ContactDamage = 10.f;

	/** Minimum seconds between contact-damage applications while continuously overlapping during dangerous frames. Placeholder value, tune freely. */
	UPROPERTY(EditAnywhere, Category = "Trap Hazard")
	float ContactDamageCooldown = 1.0f;

private:
	UFUNCTION()
	void OnDamageBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnDamageBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	UPROPERTY(VisibleAnywhere, Category = "Trap Hazard")
	TObjectPtr<UBoxComponent> DamageBox;

	UPROPERTY(VisibleAnywhere, Category = "Trap Hazard")
	TObjectPtr<UPaperFlipbookComponent> FlipbookComponent;

	/** True while the player character is overlapping DamageBox. Overlap alone never damages -- Tick still gates on the current frame being in the dangerous range and the cooldown. */
	bool bPlayerOverlapping = false;

	/** World time (GetWorld()->GetTimeSeconds()) contact damage was last applied; compared against ContactDamageCooldown. Starts far enough negative that the first dangerous-frame contact always damages immediately. */
	float LastContactDamageTime = -1000.f;
};
