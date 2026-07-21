#include "DeathMetalCatEnemyBase.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "DeathMetalCatCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"

ADeathMetalCatEnemyBase::ADeathMetalCatEnemyBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// Same 2D side-scroller plane lock as ADeathMetalCatCharacter's own constructor -- this enemy
	// now moves (straight-line chase toward the player), so it needs the same guarantee the player
	// has of never drifting off the locked Y/Z gameplay plane, on top of the one-time BeginPlay
	// snap onto that plane already in place below.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = false;
		MoveComp->bConstrainToPlane = true;
		MoveComp->SetPlaneConstraintNormal(FVector(0.f, 1.f, 0.f));
		MoveComp->bSnapToPlaneAtStart = true;
	}
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	// The default "Pawn" collision profile (left otherwise untouched -- still blocks Pawn/Camera,
	// still whatever else that profile does by default) ignores ECC_Visibility, which is the
	// specific channel ADeathMetalCatCharacter::FireShotTrace's LineTraceSingleByChannel uses for
	// the gun's hitscan -- confirmed directly (queried the capsule's actual collision response)
	// as why gun fire was tracing clean through this actor without ever registering a hit. Only
	// this one channel is changed to Block; the sword's hitbox uses a completely separate
	// overlap-only component (SwordHitbox, on the player) and isn't affected by anything here.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// Left enabled (not for this class's own use -- contact damage is gated by plain distance in
	// Tick, not overlap events) because the player's SwordHitbox overlap-checks THIS capsule from
	// its own side, and CanComponentsGenerateOverlap requires GetGenerateOverlapEvents() true on
	// both components for that to fire at all.
	GetCapsuleComponent()->SetGenerateOverlapEvents(true);

	PlaceholderMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderMesh"));
	PlaceholderMesh->SetupAttachment(GetCapsuleComponent());
	// Engine basic-shape Cylinder is ~100x100x100 (radius 50, height 100) centered on its own
	// origin; scale to roughly fill the default ACharacter capsule (radius 34, half-height 88).
	// Approximate on purpose -- this is a placeholder, exact fit doesn't matter.
	PlaceholderMesh->SetRelativeScale3D(FVector(0.68f, 0.68f, 1.76f));
	// Capsule already handles collision/overlap detection for both the sword hitbox and the gun's
	// hitscan trace -- the mesh is purely visual, so it shouldn't also generate its own hits.
	PlaceholderMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderMesh->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMeshAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMeshAsset.Succeeded())
	{
		PlaceholderMesh->SetStaticMesh(CylinderMeshAsset.Object);
	}

	// No skeleton/animation exists for this placeholder -- ACharacter's default SkeletalMeshComponent
	// (GetMesh()) would just render as an empty/invisible mesh anyway, but disable it explicitly
	// rather than leave stray collision/tick overhead from an unused component.
	if (USkeletalMeshComponent* SkeletalMesh = GetMesh())
	{
		SkeletalMesh->SetVisibility(false);
		SkeletalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void ADeathMetalCatEnemyBase::BeginPlay()
{
	Super::BeginPlay();

	Health = MaxHealth;

	// Snap onto the player's exact X-Z gameplay plane (Y and Z) before caching the spawn
	// transform below, rather than trusting however precisely this actor was dragged into the 3D
	// viewport. This is a real fix for large-scale placement drift (a 116-unit Y gap was measured
	// on the placed enemy that exposed the gun-fire miss bug), distinct from -- and in addition to
	// -- FireShotTrace's small-radius sphere-trace tolerance, which only covers minor
	// misalignment, not drift on this scale. The player's own Y is fixed for the whole session by
	// CharacterMovementComponent's plane constraint (see ADeathMetalCatCharacter's constructor),
	// so reading it here is safe regardless of whether the player's own BeginPlay has run yet --
	// placement itself isn't touched by that constraint, only future movement is.
	if (ADeathMetalCatCharacter* PlayerCharacter = Cast<ADeathMetalCatCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
	{
		CachedPlayerCharacter = PlayerCharacter;

		FVector Loc = GetActorLocation();
		const FVector PlayerLoc = PlayerCharacter->GetActorLocation();
		Loc.Y = PlayerLoc.Y;
		Loc.Z = PlayerLoc.Z;
		SetActorLocation(Loc);
	}

	InitialSpawnTransform = GetActorTransform();

	if (PlaceholderMesh)
	{
		if (PlaceholderMaterial)
		{
			PlaceholderMesh->SetMaterial(0, PlaceholderMaterial);
		}

		DynamicMaterial = PlaceholderMesh->CreateAndSetMaterialInstanceDynamic(0);
		if (DynamicMaterial)
		{
			DynamicMaterial->SetVectorParameterValue(TEXT("Color"), BaseColor);
		}
	}
}

void ADeathMetalCatEnemyBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bIsDead || !CachedPlayerCharacter)
	{
		return;
	}

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = MoveSpeed;
	}

	const FVector MyLocation = GetActorLocation();
	const FVector PlayerLocation = CachedPlayerCharacter->GetActorLocation();
	const float DistanceToPlayer = FVector::Dist(MyLocation, PlayerLocation);

	if (DistanceToPlayer <= DetectionRadius)
	{
		const float XDistance = PlayerLocation.X - MyLocation.X;
		if (FMath::Abs(XDistance) > MeleeRange)
		{
			AddMovementInput(FVector(FMath::Sign(XDistance), 0.f, 0.f), 1.f);
		}
	}

	if (DistanceToPlayer <= MeleeRange)
	{
		const float CurrentTime = GetWorld()->GetTimeSeconds();
		if (CurrentTime - LastContactDamageTime >= ContactDamageCooldown)
		{
			UGameplayStatics::ApplyDamage(CachedPlayerCharacter, ContactDamage, GetController(), this, UDamageType::StaticClass());
			LastContactDamageTime = CurrentTime;
		}
	}
}

float ADeathMetalCatEnemyBase::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (bIsDead)
	{
		// Hidden and collision-disabled, waiting to respawn -- this shouldn't normally be
		// reachable (nothing should be able to overlap/trace-hit a collision-disabled actor), but
		// guarded explicitly anyway so XP can never be double-awarded from some edge case that
		// bypasses collision (e.g. a direct ApplyDamage call).
		return 0.f;
	}

	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (ActualDamage <= 0.f)
	{
		return ActualDamage;
	}

	Health = FMath::Max(0.f, Health - ActualDamage);

	if (DynamicMaterial)
	{
		DynamicMaterial->SetVectorParameterValue(TEXT("Color"), HitFlashColor);
		GetWorldTimerManager().SetTimer(HitFlashTimerHandle, this, &ADeathMetalCatEnemyBase::ClearHitFlash, HitFlashDuration, false);
	}

	if (Health <= 0.f)
	{
		bIsDead = true;

		// XP goes to whoever actually landed the killing blow -- DamageCauser is exactly that
		// (ApplyDamage's call sites in ADeathMetalCatCharacter pass `this` as DamageCauser), so no
		// separate "find the player" lookup is needed. Runs exactly once, right here, regardless of
		// how many times this enemy subsequently respawns -- HandleDeath/HandleRespawn below are
		// purely a visual/state reset and don't touch XP at all.
		if (ADeathMetalCatCharacter* KillerCharacter = Cast<ADeathMetalCatCharacter>(DamageCauser))
		{
			KillerCharacter->AddXP(XPReward);
		}

		HandleDeath();
	}

	return ActualDamage;
}

void ADeathMetalCatEnemyBase::HandleDeath()
{
	// Hidden + collision disabled rather than destroyed outright -- a testing-convenience
	// respawn cycle so the same enemy can be repeatedly killed without manually re-placing one in
	// the level each time. SetActorEnableCollision(false) covers the capsule (sword overlap, gun
	// hitscan trace) in one call; SetActorHiddenInGame(true) covers rendering for every component.
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);

	// Reset to the same far-negative sentinel LastContactDamageTime starts at, so a respawned
	// enemy's first contact damages immediately rather than silently inheriting whatever cooldown
	// window was still in progress from its previous life.
	LastContactDamageTime = -1000.f;

	// Force the hit-flash color back to resting immediately, and cancel its own pending
	// clear-timer, rather than leaving a stale flash mid-transition into the hidden/respawn
	// window -- this is exactly what would otherwise leave the material "stuck mid-flash" on respawn.
	GetWorldTimerManager().ClearTimer(HitFlashTimerHandle);
	if (DynamicMaterial)
	{
		DynamicMaterial->SetVectorParameterValue(TEXT("Color"), BaseColor);
	}

	GetWorldTimerManager().SetTimer(RespawnTimerHandle, this, &ADeathMetalCatEnemyBase::HandleRespawn, RespawnDelay, false);
}

void ADeathMetalCatEnemyBase::HandleRespawn()
{
	SetActorTransform(InitialSpawnTransform);
	Health = MaxHealth;
	bIsDead = false;

	SetActorEnableCollision(true);
	SetActorHiddenInGame(false);

	// Redundant with HandleDeath's own reset (color was already forced back to BaseColor there),
	// but cheap and makes this function correct on its own even if that ever changes.
	if (DynamicMaterial)
	{
		DynamicMaterial->SetVectorParameterValue(TEXT("Color"), BaseColor);
	}
}

void ADeathMetalCatEnemyBase::ClearHitFlash()
{
	if (DynamicMaterial)
	{
		DynamicMaterial->SetVectorParameterValue(TEXT("Color"), BaseColor);
	}
}
