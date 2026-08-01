#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OneWayPlatform.generated.h"

class UBoxComponent;
class ADeathMetalCatCharacter;

/**
 * Classic Mario-style one-way platform: solid when landed on from above, passable when jumped up
 * into from below. Unreal has no built-in "one-way" collision mode, so this is implemented via
 * per-tick dynamic collision-response switching (SetCollisionResponseToChannel on ECC_Pawn) --
 * every tick, Block vs Ignore is decided purely from the player's CAPSULE BOTTOM (feet) position
 * relative to the platform's own top surface -- NOT velocity. An earlier version also required
 * upward velocity to engage pass-through, which caused a real bug: a short jump that only poked
 * the player's head/torso above the surface (feet still below it) would fall out of pass-through
 * the instant upward velocity decayed near the jump's apex, snapping the still-embedded capsule
 * up onto the surface even though the feet never actually cleared it. Feet-vs-surface alone has no
 * such failure mode, so velocity is no longer part of the decision at all.
 *
 * Scoped to the single player character (see the project's other actors -- RoomExitTrigger,
 * EncounterSpawnMarker -- which do the same); this is a single-player game with no jumping
 * enemies, so a platform-wide Block/Ignore toggle on the whole Pawn channel (rather than a
 * per-actor ignore list) is correct here. If a jumping enemy is ever added, this would need
 * revisiting since it would also fall through while the player is passing through nearby.
 *
 * Pure invisible collision box (no mesh/sprite) -- the visible platform look is separate 2D sprite
 * dressing placed on top by hand, same convention as the project's other placeholder floor cubes.
 */
UCLASS()
class PYTHONTEST_API AOneWayPlatform : public AActor
{
	GENERATED_BODY()

public:
	AOneWayPlatform();

	virtual void Tick(float DeltaSeconds) override;

	/** Half-extents (X/Y/Z) of the platform's collision box -- widen X for a wider platform, Z is thickness. Applied directly to the box, so this is the one knob to use instead of actor scale. */
	UPROPERTY(EditAnywhere, Category = "One-Way Platform")
	FVector PlatformExtent = FVector(200.f, 200.f, 20.f);

	/**
	 * Hysteresis buffer only -- purely absorbs floating-point jitter right at the boundary (e.g.
	 * while standing still on top) so Block/Ignore doesn't flicker for a frame or two. This is NOT
	 * a second trigger condition: blocking engages exactly when the feet are at-or-above
	 * (PlatformTopZ - SurfaceTolerance), nothing else factors in.
	 */
	UPROPERTY(EditAnywhere, Category = "One-Way Platform", AdvancedDisplay)
	float SurfaceTolerance = 5.f;

private:
	UPROPERTY(VisibleAnywhere, Category = "One-Way Platform")
	TObjectPtr<UBoxComponent> PlatformCollision;

	/** Re-resolved lazily in Tick if invalid (e.g. this actor ticks before the player is possessed) -- the player character itself persists for the whole session, so once found this never needs re-fetching. */
	TWeakObjectPtr<ADeathMetalCatCharacter> CachedPlayer;
};
