#include "EnemyProjectile.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "DeathMetalCatCharacter.h"

AEnemyProjectile::AEnemyProjectile()
{
	PrimaryActorTick.bCanEverTick = false; // ProjectileMovementComponent drives motion itself, no manual per-frame code needed

	CollisionComp = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComp"));
	RootComponent = CollisionComp;
	CollisionComp->InitSphereRadius(15.f);
	// Overlap-only, same convention as the player's own SwordHitbox -- this only ever needs to
	// report an overlap against the player, never physically block or be blocked by anything.
	CollisionComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionComp->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	CollisionComp->SetGenerateOverlapEvents(true);
	CollisionComp->OnComponentBeginOverlap.AddDynamic(this, &AEnemyProjectile::OnCollisionBeginOverlap);

	ProjectileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileMesh"));
	ProjectileMesh->SetupAttachment(CollisionComp);
	// Engine basic-shape Sphere is ~100x100x100 (radius 50); scale down to a small bright dot --
	// same "approximate on purpose, exact fit doesn't matter" placeholder philosophy as
	// ADeathMetalCatEnemyBase::PlaceholderMesh's own cylinder scale.
	ProjectileMesh->SetRelativeScale3D(FVector(0.24f, 0.24f, 0.24f));
	ProjectileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProjectileMesh->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMeshAsset.Succeeded())
	{
		ProjectileMesh->SetStaticMesh(SphereMeshAsset.Object);
	}

	// Reuses the same placeholder material every enemy's own PlaceholderMesh uses, rather than
	// inventing a new one -- it already exposes the "Color" vector parameter this class needs.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlaceholderMaterialAsset(TEXT("/Game/Characters/EnemyBase/M_EnemyPlaceholder.M_EnemyPlaceholder"));
	if (PlaceholderMaterialAsset.Succeeded())
	{
		ProjectileMaterial = PlaceholderMaterialAsset.Object;
	}

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->bRotationFollowsVelocity = false; // a plain sprite/sphere placeholder has no facing to speak of
	ProjectileMovement->ProjectileGravityScale = 0.f; // straight line, not an arc -- matches "not homing" and keeps aim exact
	ProjectileMovement->bShouldBounce = false;

	InitialLifeSpan = 10.f; // hard backstop only -- InitializeProjectile's own timer is the real lifetime, this just guarantees cleanup if Initialize is ever skipped
}

void AEnemyProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (ProjectileMaterial)
	{
		ProjectileMesh->SetMaterial(0, ProjectileMaterial);
	}
	if (UMaterialInstanceDynamic* DynamicMat = ProjectileMesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		DynamicMat->SetVectorParameterValue(TEXT("Color"), ProjectileColor);
	}
}

void AEnemyProjectile::InitializeProjectile(const FVector& Direction, float InSpeed, float InDamage, float InLifetime, AController* InstigatorController, AActor* DamageCauserActor)
{
	Damage = InDamage;
	DamageInstigatorController = InstigatorController;
	DamageCauser = DamageCauserActor;

	const FVector NormalizedDirection = Direction.GetSafeNormal();
	ProjectileMovement->Velocity = NormalizedDirection * InSpeed;
	ProjectileMovement->InitialSpeed = InSpeed;
	ProjectileMovement->MaxSpeed = InSpeed;

	GetWorldTimerManager().SetTimer(LifetimeTimerHandle, this, &AEnemyProjectile::DestroyProjectile, InLifetime, false);
}

void AEnemyProjectile::OnCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || !OtherActor->IsA<ADeathMetalCatCharacter>())
	{
		return;
	}

	UGameplayStatics::ApplyDamage(OtherActor, Damage, DamageInstigatorController, DamageCauser, UDamageType::StaticClass());

	DestroyProjectile();
}

void AEnemyProjectile::DestroyProjectile()
{
	GetWorldTimerManager().ClearTimer(LifetimeTimerHandle);
	Destroy();
}
