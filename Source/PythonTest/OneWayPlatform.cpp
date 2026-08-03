#include "OneWayPlatform.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "PaperSpriteComponent.h"
#include "DeathMetalCatCharacter.h"

AOneWayPlatform::AOneWayPlatform()
{
	PrimaryActorTick.bCanEverTick = true;

	PlatformCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("PlatformCollision"));
	RootComponent = PlatformCollision;
	PlatformCollision->SetBoxExtent(PlatformExtent);

	// Solid by default (matches the project's other placeholder floor geometry) -- Tick only ever
	// overrides the Pawn channel's response down to Ignore, so anything else (world geometry
	// queries, etc.) still sees this as a normal blocking volume at all times.
	PlatformCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PlatformCollision->SetCollisionObjectType(ECC_WorldStatic);
	PlatformCollision->SetCollisionResponseToAllChannels(ECR_Block);
	PlatformCollision->SetGenerateOverlapEvents(false);
	PlatformCollision->SetHiddenInGame(true);

	// Purely cosmetic, no sprite assigned by default -- collision stays solely on PlatformCollision
	// above regardless of whether (or what) sprite an instance sets here, so this never collides on
	// its own and never factors into the pass-through Tick logic.
	PlatformSprite = CreateDefaultSubobject<UPaperSpriteComponent>(TEXT("PlatformSprite"));
	PlatformSprite->SetupAttachment(PlatformCollision);
	PlatformSprite->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Absolute scale -- without this, PlatformSprite inherits PlatformCollision's (the root's)
	// scale, so shrinking an oversized sprite via the actor's Transform also shrinks the collision
	// box along with it. Location/rotation stay relative (still follows the box's placement); only
	// scale is decoupled -- resize the sprite via ITS OWN component Transform (select the actor,
	// expand "Platform Sprite" in the Details/Components panel), not the actor's root Transform.
	PlatformSprite->SetAbsolute(false, false, true);
}

void AOneWayPlatform::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!CachedPlayer.IsValid())
	{
		CachedPlayer = Cast<ADeathMetalCatCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
		if (!CachedPlayer.IsValid())
		{
			return;
		}
	}

	ADeathMetalCatCharacter* Player = CachedPlayer.Get();
	const UCharacterMovementComponent* MoveComp = Player->GetCharacterMovement();
	const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
	if (!MoveComp || !Capsule)
	{
		return;
	}

	const FVector BoxLocation = PlatformCollision->GetComponentLocation();
	const FVector BoxExtent = PlatformCollision->GetScaledBoxExtent();
	const float PlatformTopZ = BoxLocation.Z + BoxExtent.Z;

	const FVector PlayerLocation = Capsule->GetComponentLocation();
	// Feet position -- the actual bottom of the capsule, not its center -- so a jump that only
	// pokes the player's head/torso above the surface doesn't count as "landed" until the feet
	// themselves have crossed it.
	const float PlayerBottomZ = PlayerLocation.Z - Capsule->GetScaledCapsuleHalfHeight();
	const float PlayerRadius = Capsule->GetScaledCapsuleRadius();

	// Horizontal footprint check (capsule radius widens the box on both axes) -- without this, a
	// platform would ignore the player for every jump anywhere in the room, not just jumps that
	// are actually happening underneath this specific platform.
	const bool bWithinX = FMath::Abs(PlayerLocation.X - BoxLocation.X) <= (BoxExtent.X + PlayerRadius);
	const bool bWithinY = FMath::Abs(PlayerLocation.Y - BoxLocation.Y) <= (BoxExtent.Y + PlayerRadius);
	const bool bWithinFootprint = bWithinX && bWithinY;

	// Blocking engages ONLY once the feet are at-or-above the surface -- SurfaceTolerance is purely
	// a hysteresis buffer against boundary jitter, not a second trigger condition. Velocity is
	// deliberately NOT part of this decision: it used to gate pass-through, which let a short jump
	// (head above the surface, feet still below) fall out of pass-through the instant upward
	// velocity decayed near the apex, snapping the still-embedded capsule up onto the surface even
	// though the feet never actually cleared it.
	const bool bFeetAtOrAboveSurface = PlayerBottomZ >= (PlatformTopZ - SurfaceTolerance);
	const bool bShouldPassThrough = bWithinFootprint && !bFeetAtOrAboveSurface;

	PlatformCollision->SetCollisionResponseToChannel(ECC_Pawn, bShouldPassThrough ? ECR_Ignore : ECR_Block);
}
