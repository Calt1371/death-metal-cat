#include "DeathMetalCatEnemyBase.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "DeathMetalCatCharacter.h"
#include "Kismet/GameplayStatics.h"

ADeathMetalCatEnemyBase::ADeathMetalCatEnemyBase()
{
	// The default "Pawn" collision profile (left otherwise untouched -- still blocks Pawn/Camera,
	// still whatever else that profile does by default) ignores ECC_Visibility, which is the
	// specific channel ADeathMetalCatCharacter::FireShotTrace's LineTraceSingleByChannel uses for
	// the gun's hitscan -- confirmed directly (queried the capsule's actual collision response)
	// as why gun fire was tracing clean through this actor without ever registering a hit. Only
	// this one channel is changed to Block; the sword's hitbox uses a completely separate
	// overlap-only component (SwordHitbox, on the player) and isn't affected by anything here.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

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
	if (ACharacter* PlayerCharacter = UGameplayStatics::GetPlayerCharacter(this, 0))
	{
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
