#include "DeathMetalCatEnemyBase.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "DeathMetalCatCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "PaperFlipbookComponent.h"
#include "PaperFlipbook.h"
#include "EnemyProjectile.h"

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

	// Default projectile class -- works out of the box (a small placeholder-colored sphere) with
	// zero Blueprint setup required; override ProjectileClass per-subclass if a Blueprint variant
	// with real art is made later.
	ProjectileClass = AEnemyProjectile::StaticClass();
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

	// Opt-in flight mode (see bFliesFreely's own comment) -- switches off the normal
	// MOVE_Walking/full-gravity ground behavior so this actor isn't pulled back down to the
	// floor plane; Tick's chase block separately adds the actual Z-axis movement toward the
	// player once this is set.
	if (bFliesFreely)
	{
		if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
		{
			MoveComp->SetMovementMode(MOVE_Flying);
			MoveComp->GravityScale = 0.f;
		}
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
		// MOVE_Flying reads MaxFlySpeed, a completely separate cap from MaxWalkSpeed -- without
		// this, bFliesFreely subclasses moved at the engine's untouched MaxFlySpeed default
		// (600) regardless of MoveSpeed. Harmless to set unconditionally on ground enemies too,
		// since they never enter MOVE_Flying and this cap just sits unused.
		MoveComp->MaxFlySpeed = MoveSpeed;
	}

	// Finds whatever PaperFlipbookComponent exists on this actor regardless of whether it's
	// native or Blueprint-added (see CachedFlipbookComponent's own comment); stays null (and every
	// flipbook-touching block below no-ops) for subclasses with no such component.
	if (!CachedFlipbookComponent)
	{
		CachedFlipbookComponent = FindComponentByClass<UPaperFlipbookComponent>();
	}

	const FVector MyLocation = GetActorLocation();
	const FVector PlayerLocation = CachedPlayerCharacter->GetActorLocation();
	const float DistanceToPlayer = FVector::Dist(MyLocation, PlayerLocation);
	const float XDistance = PlayerLocation.X - MyLocation.X;
	// Only actually used for movement when bFliesFreely -- ground enemies never leave their
	// spawn-snapped Z, so this is always ~0 for them and harmless to compute unconditionally.
	const float ZDistance = PlayerLocation.Z - MyLocation.Z;

	// Facing: whenever the player is within DetectionRadius at all (any band), face toward them --
	// same Scale.X-sign convention ADeathMetalCatCharacter's own UpdateAnimation uses for its own
	// sprite (>= 0 faces +X/right, < 0 faces -X/left) -- except where bSpriteFacesReversed opts a
	// subclass out because its own art was authored facing the opposite default way (see that
	// property's own comment). Deliberately not gated on bIsAdvancing -- the enemy should keep
	// facing the player even while stopped for melee/ranged attacks.
	if (CachedFlipbookComponent && DistanceToPlayer <= DetectionRadius && FMath::Abs(XDistance) > KINDA_SMALL_NUMBER)
	{
		FVector Scale = CachedFlipbookComponent->GetRelativeScale3D();
		const float TowardPlayerSign = (XDistance < 0.f) ? -1.f : 1.f;
		const float DesiredSign = bSpriteFacesReversed ? -TowardPlayerSign : TowardPlayerSign;
		if (!FMath::IsNearlyEqual(FMath::Sign(Scale.X), DesiredSign))
		{
			Scale.X = DesiredSign * FMath::Abs(Scale.X);
			CachedFlipbookComponent->SetRelativeScale3D(Scale);
		}
	}

	// Three concentric bands -- see the class comment. A burst already in progress (Drawing/
	// Firing) runs to completion regardless of the player's exact position once started, so all of
	// this is skipped entirely while ShootPhase != None.
	bool bIsAdvancing = false;
	const bool bIsInMeleeRange = DistanceToPlayer <= MeleeRange;
	const bool bIsInShootBand = !bIsInMeleeRange && DistanceToPlayer <= ShootRange;

	if (ShootPhase == EEnemyShootPhase::None)
	{
		if (bIsInMeleeRange)
		{
			const float CurrentTime = GetWorld()->GetTimeSeconds();
			if (CurrentTime - LastContactDamageTime >= ContactDamageCooldown)
			{
				UGameplayStatics::ApplyDamage(CachedPlayerCharacter, ContactDamage, GetController(), this, UDamageType::StaticClass());
				LastContactDamageTime = CurrentTime;
			}
		}
		else if (bIsInShootBand)
		{
			const float CurrentTime = GetWorld()->GetTimeSeconds();
			if (CurrentTime - LastBurstEndTime >= ShootBurstCooldown)
			{
				BeginRangedAttack();
			}
		}
		else if (DistanceToPlayer <= DetectionRadius)
		{
			// Ground enemies: unchanged, X-only unit vector. Flying: plain direct-approach
			// toward the player's actual position on both axes (normalized so diagonal
			// movement isn't faster than axis-aligned movement) -- still no
			// pathfinding/obstacle avoidance, matching the GDD's existing convention for this
			// enemy system, just extended to two axes instead of one.
			const FVector MoveDirection = bFliesFreely
				? FVector(XDistance, 0.f, ZDistance).GetSafeNormal()
				: FVector(FMath::Sign(XDistance), 0.f, 0.f);
			AddMovementInput(MoveDirection, 1.f);
			bIsAdvancing = true;
		}
	}

	// Idle/Walk flipbook selection -- skipped entirely while a burst owns the flipbook
	// (BeginRangedAttack/BeginBurstLoop set ShootDrawFlipbook/ShootLoopFlipbook themselves for the
	// duration of Drawing/Firing).
	if (CachedFlipbookComponent && ShootPhase == EEnemyShootPhase::None)
	{
		UPaperFlipbook* DesiredFlipbook = CachedFlipbookComponent->GetFlipbook();
		if (bIsInMeleeRange && AttackFlipbook)
		{
			DesiredFlipbook = AttackFlipbook;
		}
		else if (bIsAdvancing && WalkFlipbook)
		{
			DesiredFlipbook = WalkFlipbook;
		}
		else if (IdleFlipbook)
		{
			DesiredFlipbook = IdleFlipbook;
		}

		// TEMPORARY debug logging for the "always left leg" walk-animation investigation --
		// remove once resolved.
		static int32 DebugLogThrottle = 0;
		if (DesiredFlipbook != CachedFlipbookComponent->GetFlipbook())
		{
			UE_LOG(LogTemp, Warning, TEXT("[WALK ANIM DEBUG] t=%f TRANSITION bIsAdvancing=%d -> new flipbook=%s (was frame %d)"),
				GetWorld()->GetTimeSeconds(), bIsAdvancing,
				DesiredFlipbook ? *DesiredFlipbook->GetName() : TEXT("null"),
				CachedFlipbookComponent->GetPlaybackPositionInFrames());
			CachedFlipbookComponent->SetFlipbook(DesiredFlipbook);
		}
		else if (bIsAdvancing && (++DebugLogThrottle % 6 == 0))
		{
			UE_LOG(LogTemp, Warning, TEXT("[WALK ANIM DEBUG] t=%f steady bIsAdvancing=1 flipbook=%s frame=%d playing=%d looping=%d ScaleX=%.2f"),
				GetWorld()->GetTimeSeconds(),
				*CachedFlipbookComponent->GetFlipbook()->GetName(),
				CachedFlipbookComponent->GetPlaybackPositionInFrames(),
				CachedFlipbookComponent->IsPlaying(), CachedFlipbookComponent->IsLooping(),
				CachedFlipbookComponent->GetRelativeScale3D().X);
		}
	}
}

void ADeathMetalCatEnemyBase::BeginRangedAttack()
{
	ShootPhase = EEnemyShootPhase::Drawing;

	if (CachedFlipbookComponent && ShootDrawFlipbook)
	{
		CachedFlipbookComponent->SetFlipbook(ShootDrawFlipbook);
		CachedFlipbookComponent->SetLooping(false);
		CachedFlipbookComponent->PlayFromStart();
		GetWorldTimerManager().SetTimer(ShootDrawTimerHandle, this, &ADeathMetalCatEnemyBase::BeginBurstLoop, ShootDrawDuration, false);
	}
	else
	{
		// No windup art configured for this subclass -- skip straight to the firing loop.
		BeginBurstLoop();
	}
}

void ADeathMetalCatEnemyBase::BeginBurstLoop()
{
	ShootPhase = EEnemyShootPhase::Firing;
	ShotsFiredInBurst = 0;

	if (CachedFlipbookComponent && ShootLoopFlipbook)
	{
		CachedFlipbookComponent->SetFlipbook(ShootLoopFlipbook);
		CachedFlipbookComponent->SetLooping(true);
		CachedFlipbookComponent->PlayFromStart();
	}

	// Per-shot interval matches the loop flipbook's own frame rate, so each shot lines up with a
	// flash frame actually being shown on screen -- falls back to a reasonable default if this
	// subclass has no loop flipbook configured.
	const float FrameRate = ShootLoopFlipbook ? ShootLoopFlipbook->GetFramesPerSecond() : 8.f;
	const float ShotInterval = (FrameRate > 0.f) ? (1.f / FrameRate) : 0.125f;

	FireOneShot();
	GetWorldTimerManager().SetTimer(ShootBurstTimerHandle, this, &ADeathMetalCatEnemyBase::FireOneShot, ShotInterval, true);
}

void ADeathMetalCatEnemyBase::FireOneShot()
{
	if (CachedPlayerCharacter && ProjectileClass)
	{
		const FVector Direction = (CachedPlayerCharacter->GetActorLocation() - GetActorLocation()).GetSafeNormal();
		const FTransform SpawnTransform(Direction.Rotation(), GetActorLocation());

		FActorSpawnParameters SpawnParams;
		// The enemy's own capsule likely overlaps the spawn point at this exact location -- always
		// spawn regardless rather than silently failing.
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		if (AEnemyProjectile* Projectile = GetWorld()->SpawnActor<AEnemyProjectile>(ProjectileClass, SpawnTransform, SpawnParams))
		{
			Projectile->InitializeProjectile(Direction, ProjectileSpeed, ProjectileDamage, ProjectileLifetime, GetController(), this);
		}
	}

	++ShotsFiredInBurst;

	// Burst length matches the loop flipbook's own frame count (one shot per frame shown) --
	// falls back to a single shot if this subclass has no loop flipbook configured.
	const int32 BurstShotCount = (ShootLoopFlipbook && ShootLoopFlipbook->GetNumFrames() > 0) ? ShootLoopFlipbook->GetNumFrames() : 1;
	if (ShotsFiredInBurst >= BurstShotCount)
	{
		GetWorldTimerManager().ClearTimer(ShootBurstTimerHandle);
		ShootPhase = EEnemyShootPhase::None;
		LastBurstEndTime = GetWorld()->GetTimeSeconds();
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
			KillerCharacter->TriggerQuip(EQuipTriggerType::Kill);
		}

		HandleDeath();
	}

	return ActualDamage;
}

void ADeathMetalCatEnemyBase::HandleDeath()
{
	// Collision disabled immediately -- a testing-convenience respawn cycle so the same enemy can
	// be repeatedly killed without manually re-placing one in the level each time. Covers the
	// capsule (sword overlap, gun hitscan trace) in one call. Visibility is NOT hidden outright
	// here anymore -- it blinks first via TickDeathBlink, then hides for good once the blink
	// sequence finishes.
	SetActorEnableCollision(false);

	// Reset to the same far-negative sentinel LastContactDamageTime/LastBurstEndTime start at, so a
	// respawned enemy's first contact/burst is immediately available rather than silently
	// inheriting whatever cooldown window was still in progress from its previous life. Also
	// unconditionally cancels any in-progress ranged-attack burst -- a mid-burst death would
	// otherwise leave ShootBurstTimerHandle armed, still calling FireOneShot on a hidden/dead actor.
	LastContactDamageTime = -1000.f;
	LastBurstEndTime = -1000.f;
	GetWorldTimerManager().ClearTimer(ShootDrawTimerHandle);
	GetWorldTimerManager().ClearTimer(ShootBurstTimerHandle);
	ShootPhase = EEnemyShootPhase::None;

	// Force the hit-flash color back to resting immediately, and cancel its own pending
	// clear-timer, rather than leaving a stale flash mid-transition into the death-blink sequence
	// -- this is exactly what would otherwise leave the material "stuck mid-flash" on respawn.
	GetWorldTimerManager().ClearTimer(HitFlashTimerHandle);
	if (DynamicMaterial)
	{
		DynamicMaterial->SetVectorParameterValue(TEXT("Color"), BaseColor);
	}

	RemainingDeathBlinks = DeathBlinkCount;
	GetWorldTimerManager().SetTimer(DeathBlinkTimerHandle, this, &ADeathMetalCatEnemyBase::TickDeathBlink, DeathBlinkInterval, true);
}

void ADeathMetalCatEnemyBase::TickDeathBlink()
{
	SetActorHiddenInGame(!IsHidden());
	--RemainingDeathBlinks;

	if (RemainingDeathBlinks <= 0)
	{
		GetWorldTimerManager().ClearTimer(DeathBlinkTimerHandle);
		SetActorHiddenInGame(true);
		GetWorldTimerManager().SetTimer(RespawnTimerHandle, this, &ADeathMetalCatEnemyBase::HandleRespawn, RespawnDelay, false);
	}
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
