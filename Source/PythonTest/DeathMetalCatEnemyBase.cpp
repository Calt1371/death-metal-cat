#include "DeathMetalCatEnemyBase.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ADeathMetalCatEnemyBase::ADeathMetalCatEnemyBase()
{
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
		// Simple death for now -- no death animation/ragdoll/loot, just remove the actor. A real
		// death flow (matching the player's future respawn work) is a separate future task.
		Destroy();
	}

	return ActualDamage;
}

void ADeathMetalCatEnemyBase::ClearHitFlash()
{
	if (DynamicMaterial)
	{
		DynamicMaterial->SetVectorParameterValue(TEXT("Color"), BaseColor);
	}
}
