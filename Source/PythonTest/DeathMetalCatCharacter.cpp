#include "DeathMetalCatCharacter.h"

#include "PaperFlipbookComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "TimerManager.h"
#include "Components/BoxComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "DamageNumberActor.h"
#include "GnarlyRankHUDWidget.h"
#include "Blueprint/UserWidget.h"
#include "QuipLibrary.h"
#include "RoomProgressionManager.h"

ADeathMetalCatCharacter::ADeathMetalCatCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Death Metal Cat is a 2D side-scroller: lock movement to the X-Z plane so the
	// character can never drift along Y (the sprite's billboard/depth axis).
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = false;
		MoveComp->bConstrainToPlane = true;
		MoveComp->SetPlaneConstraintNormal(FVector(0.f, 1.f, 0.f));
		MoveComp->bSnapToPlaneAtStart = true;
		MoveComp->MaxWalkSpeed = MaxMoveSpeed;
		MoveComp->JumpZVelocity = JumpZVelocity;
		MoveComp->AirControl = AirControl;
	}

	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	// Side-scroller camera: pull the camera back along the depth (Y) axis so it views the
	// X-Z movement plane face-on, rather than the default floating first-person-ish view
	// APaperCharacter has with no camera at all.
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 800.f;
	// Yaw -90 (not +90) so screen-right lines up with world +X -- the same axis
	// HandleMoveRight drives -- otherwise pressing "right" would move the character
	// left on screen. Pitch/Roll stay at 0 for a level, undistorted side view.
	CameraBoom->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = false;
	CameraBoom->bInheritRoll = false;
	// No occluders expected along the fixed side-view axis; collision test would otherwise
	// punch the boom in/out unpredictably.
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bEnableCameraRotationLag = false;

	SideViewCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("SideViewCamera"));
	SideViewCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	SideViewCamera->bUsePawnControlRotation = false;

	// Sword swing hitbox. Placeholder extent/offset roughly covering the swing area in front of
	// the character; positioned/mirrored per-attack in HandleSwordAttack based on facing.
	// QueryOnly + NoCollision-by-default: this only ever needs to report overlaps, never
	// physically block anything, and must not affect anything outside the active swing window.
	SwordHitbox = CreateDefaultSubobject<UBoxComponent>(TEXT("SwordHitbox"));
	SwordHitbox->SetupAttachment(RootComponent);
	SwordHitbox->SetBoxExtent(FVector(60.f, 50.f, 70.f));
	SwordHitbox->SetRelativeLocation(FVector(80.f, 0.f, 0.f));
	SwordHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SwordHitbox->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	SwordHitbox->SetGenerateOverlapEvents(true);
	SwordHitbox->OnComponentBeginOverlap.AddDynamic(this, &ADeathMetalCatCharacter::OnSwordHitboxBeginOverlap);
}

void ADeathMetalCatCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Where HandleRespawn puts the character back after dying -- unlike the enemy's own version of
	// this cache, there's no plane-snap correction to interleave with here, so this can just be
	// read straight off wherever the engine's default GameMode/PlayerStart flow actually placed us.
	InitialSpawnTransform = GetActorTransform();

	Health = MaxHealth;

	RecalculateXPToNextLevel();
	ApplyAttributeEffects();

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->SetPlaneConstraintOrigin(GetActorLocation());

		// Cached once, before ApplyAirFireFloat (the only thing that ever touches GravityScale)
		// has a chance to run -- the single source of truth both for reducing gravity during the
		// air-fire float window and for restoring it afterward.
		DefaultGravityScale = MoveComp->GravityScale;
	}

	if (IdleFlipbook && GetSprite())
	{
		GetSprite()->SetFlipbook(IdleFlipbook);
		CurrentFlipbook = IdleFlipbook;
	}
}

void ADeathMetalCatCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
				ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (MoveMappingContext)
			{
				Subsystem->AddMappingContext(MoveMappingContext, 0);
			}
		}

		// Gnarly Rank HUD: a persistent element, created once (the first time this character is
		// possessed by a player controller) and left in the viewport for the rest of the session --
		// NotifyControllerChanged is the same proven hook already used above for the input mapping
		// context, for the same reason (GetController() isn't guaranteed valid yet in BeginPlay).
		if (!GnarlyRankHUDWidgetInstance)
		{
			GnarlyRankHUDWidgetInstance = CreateWidget<UGnarlyRankHUDWidget>(PC, UGnarlyRankHUDWidget::StaticClass());
			if (GnarlyRankHUDWidgetInstance)
			{
				GnarlyRankHUDWidgetInstance->SetOwningCharacter(this);
				GnarlyRankHUDWidgetInstance->AddToViewport();
			}
		}
	}
}

void ADeathMetalCatCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveRightAction)
		{
			EnhancedInput->BindAction(MoveRightAction, ETriggerEvent::Triggered, this, &ADeathMetalCatCharacter::HandleMoveRight);
			EnhancedInput->BindAction(MoveRightAction, ETriggerEvent::Completed, this, &ADeathMetalCatCharacter::HandleMoveRightReleased);
			EnhancedInput->BindAction(MoveRightAction, ETriggerEvent::Canceled, this, &ADeathMetalCatCharacter::HandleMoveRightReleased);
		}

		if (JumpAction)
		{
			EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &ADeathMetalCatCharacter::HandleJump);
		}

		if (DodgeAction)
		{
			EnhancedInput->BindAction(DodgeAction, ETriggerEvent::Started, this, &ADeathMetalCatCharacter::HandleDodge);
		}

		if (SwordAttackAction)
		{
			EnhancedInput->BindAction(SwordAttackAction, ETriggerEvent::Started, this, &ADeathMetalCatCharacter::HandleSwordAttack);
		}

		if (ShootAction)
		{
			// Triggered (not Ongoing) is what actually fires every tick while a simple digital
			// action is held -- Ongoing only applies to triggers with hold/delay evaluation logic,
			// which the default trigger doesn't have, so binding Ongoing would silently never fire.
			// This mirrors HandleMoveRight's existing Triggered binding above.
			EnhancedInput->BindAction(ShootAction, ETriggerEvent::Started, this, &ADeathMetalCatCharacter::HandleShootStarted);
			EnhancedInput->BindAction(ShootAction, ETriggerEvent::Triggered, this, &ADeathMetalCatCharacter::HandleShootHeld);
			EnhancedInput->BindAction(ShootAction, ETriggerEvent::Completed, this, &ADeathMetalCatCharacter::HandleShootReleased);
			EnhancedInput->BindAction(ShootAction, ETriggerEvent::Canceled, this, &ADeathMetalCatCharacter::HandleShootReleased);
		}
	}
}

void ADeathMetalCatCharacter::HandleMoveRight(const FInputActionValue& Value)
{
	// Cached unconditionally, before any of the guards below -- DetectWallForSlide needs to know
	// which direction the player is holding toward even on ticks where that input doesn't
	// actually get to move the character (e.g. during the wall-jump input lockout just below).
	LastMoveRightAxisValue = Value.Get<float>();

	// Grounded + holding Shoot, or grounded + mid sword-swing: plant and ignore movement input --
	// you can't realistically run while firing a held weapon or swinging for power. Airborne is
	// exempted from both: jump momentum should carry through normally even mid-action, since
	// that's a different, acceptable case (no realistic "planting" while falling anyway).
	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	const bool bAirborne = MoveComp && MoveComp->IsFalling();
	if ((bIsHoldingShootButton || bIsAttacking) && !bAirborne)
	{
		return;
	}

	// Brief post-wall-jump window: input still gets through (so the player can genuinely redirect
	// mid-air and chain into a second wall/platform), just at reduced authority rather than full
	// lockout -- see WallJumpCommitmentDuration/WallJumpCommitmentInputScale.
	const float InputScale = bInWallJumpCommitmentWindow ? WallJumpCommitmentInputScale : 1.f;
	AddMovementInput(FVector(1.f, 0.f, 0.f), LastMoveRightAxisValue * InputScale);
}

void ADeathMetalCatCharacter::HandleMoveRightReleased(const FInputActionValue& Value)
{
	LastMoveRightAxisValue = 0.f;
}

void ADeathMetalCatCharacter::HandleJump(const FInputActionValue& Value)
{
	if (bIsWallSliding)
	{
		// Launch up and away from the wall -- away from WallSlideFacingSign (the direction being
		// held INTO the wall), not toward it. Same LaunchCharacter mechanism HandleDodge's own
		// impulse uses, but with bZOverride true (not false like Dodge): a wall jump provides an
		// explicit new vertical component too, replacing whatever fall velocity had built up,
		// rather than preserving it the way Dodge preserves Z to not fight jump/fall arcs.
		LaunchCharacter(FVector(-WallSlideFacingSign * WallJumpForceHorizontal, 0.f, WallJumpForceVertical), true, true);

		bIsWallSliding = false;
		bInWallJumpCommitmentWindow = true;
		GetWorldTimerManager().SetTimer(WallJumpCommitmentTimerHandle, this, &ADeathMetalCatCharacter::ClearWallJumpCommitmentWindow, WallJumpCommitmentDuration, false);
		return;
	}

	Jump();
}

void ADeathMetalCatCharacter::HandleDodge(const FInputActionValue& Value)
{
	if (bIsDodging)
	{
		// Ignore re-triggers while already mid-dodge rather than restarting/stacking timers.
		return;
	}

	UPaperFlipbookComponent* SpriteComp = GetSprite();
	const float FacingSign = (SpriteComp && SpriteComp->GetRelativeScale3D().X < 0.f) ? -1.f : 1.f;

	// Lock facing to whatever it was the instant the dodge started -- UpdateAnimation checks
	// bIsDodging and holds the sprite at this sign for the whole dodge, overriding the normal
	// velocity-based flip that would otherwise re-face the character toward the (backward) dodge
	// movement direction. Also doubles as the stored launch direction: AdvanceDodgeFrame reads
	// this back when it fires the deferred LaunchCharacter call on the frame-0-to-1 transition.
	DodgeFacingSignAtStart = FacingSign;

	// Frame 0 (standing neutral) plays as a brief static beat with zero velocity -- no impulse
	// yet, and any pre-existing horizontal momentum (e.g. dodging while already running) is
	// explicitly zeroed so it doesn't visibly slide during this beat. Z is left untouched so a
	// mid-air dodge doesn't lose its vertical motion. The actual backward impulse (LaunchCharacter)
	// fires later, from AdvanceDodgeFrame, exactly as frame 1 (the wind-up pose) begins -- see
	// there for why this is split instead of firing immediately here.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		FVector V = MoveComp->Velocity;
		V.X = 0.f;
		MoveComp->Velocity = V;
	}

	// 5-frame back-handspring sequence, played once over DodgeDuration. Frame 1's hold time is
	// deliberately shorter than the rest -- see GetDodgeFrameHoldDuration/DodgeWindUpFrameDuration
	// -- so this is a chain of one-shot timers (each re-armed with the current frame's own hold
	// duration), not a single flat-interval repeating timer.
	DodgeCurrentFrame = 0;
	SetDodgeFrame(0, TEXT("frame_0"));
	ScheduleNextDodgeFrame();

	bIsDodging = true;
	bIsInvincible = true;

	GetWorldTimerManager().SetTimer(DodgeTimerHandle, this, &ADeathMetalCatCharacter::ClearDodgeState, DodgeDuration, false);
	GetWorldTimerManager().SetTimer(IFrameTimerHandle, this, &ADeathMetalCatCharacter::ClearInvincibility, IFrameDuration, false);
}

void ADeathMetalCatCharacter::AdvanceDodgeFrame()
{
	++DodgeCurrentFrame;

	if (DodgeCurrentFrame == 1)
	{
		// Deferred from HandleDodge: the backward impulse fires here, synchronized to the exact
		// timer tick that transitions into frame 1 (the crouch/wind-up pose), so the visual
		// wind-up and the actual movement start together rather than movement preceding any
		// visible motion. Retreat, not a dash toward the threat: launch AWAY from
		// DodgeFacingSignAtStart, not toward it. bXYOverride = true: replace X/Y velocity outright
		// for a consistent burst regardless of current speed (which is exactly zero right now,
		// per HandleDodge's frame-0 reset, but this matches the convention used everywhere else
		// LaunchCharacter is called in this class). bZOverride = false: don't stomp vertical
		// velocity, so dodging mid-jump/fall doesn't cancel gravity's effect on Z.
		LaunchCharacter(FVector(-DodgeFacingSignAtStart * DodgeImpulseStrength, 0.f, 0.f), true, false);
	}

	SetDodgeFrame(DodgeCurrentFrame, TEXT("advance"));
	ScheduleNextDodgeFrame();
}

void ADeathMetalCatCharacter::ScheduleNextDodgeFrame()
{
	if (DodgeCurrentFrame >= 4)
	{
		// Frame 4 (standing) is the last frame -- nothing left to advance to. ClearDodgeState
		// (armed for DodgeDuration in HandleDodge) handles ending bIsDodging itself.
		return;
	}

	GetWorldTimerManager().SetTimer(DodgeFrameTimerHandle, this, &ADeathMetalCatCharacter::AdvanceDodgeFrame,
		GetDodgeFrameHoldDuration(DodgeCurrentFrame), false);
}

float ADeathMetalCatCharacter::GetDodgeFrameHoldDuration(int32 FrameIndex) const
{
	return (FrameIndex == 1) ? DodgeWindUpFrameDuration : (DodgeDuration / 5.f);
}

void ADeathMetalCatCharacter::SetDodgeFrame(int32 FrameIndex, const TCHAR* Reason)
{
	UPaperFlipbookComponent* SpriteComp = GetSprite();
	if (!SpriteComp || !DodgeFlipbook)
	{
		return;
	}

	if (CurrentFlipbook != DodgeFlipbook)
	{
		SpriteComp->SetFlipbook(DodgeFlipbook);
		SpriteComp->Stop();
		CurrentFlipbook = DodgeFlipbook;
	}

	UE_LOG(LogTemp, Verbose, TEXT("[DODGE FRAME] t=%f  frame=%d  reason=%s"), GetWorld()->GetTimeSeconds(), FrameIndex, Reason);

	if (SpriteComp->GetPlaybackPositionInFrames() != FrameIndex)
	{
		SpriteComp->SetPlaybackPositionInFrames(FrameIndex, false);
	}
}

void ADeathMetalCatCharacter::ClearDodgeState()
{
	GetWorldTimerManager().ClearTimer(DodgeFrameTimerHandle);
	bIsDodging = false;
}

void ADeathMetalCatCharacter::ClearInvincibility()
{
	bIsInvincible = false;
}

void ADeathMetalCatCharacter::ClearWallJumpCommitmentWindow()
{
	bInWallJumpCommitmentWindow = false;
}

bool ADeathMetalCatCharacter::DetectWallForSlide(float& OutWallSign) const
{
	if (FMath::Abs(LastMoveRightAxisValue) < KINDA_SMALL_NUMBER)
	{
		// Not holding toward anything -- nothing to slide on regardless of what's nearby.
		return false;
	}

	const float CheckSign = FMath::Sign(LastMoveRightAxisValue);
	const FVector Start = GetActorLocation();
	const FVector End = Start + FVector(CheckSign * WallCheckDistance, 0.f, 0.f);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	// Same channel FireShotTrace's own hitscan already sweeps against (ECC_Visibility) and the
	// same small-sphere-not-a-line tolerance reasoning as GunTraceRadius -- reusing a
	// already-proven-working channel/shape combination rather than introducing a new one.
	FHitResult Hit;
	const bool bHit = GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeSphere(WallCheckRadius), QueryParams);

	if (!bHit)
	{
		return false;
	}

	OutWallSign = CheckSign;
	return true;
}

void ADeathMetalCatCharacter::UpdateWallSlide()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	const bool bAirborne = MoveComp && MoveComp->IsFalling();

	float WallSign = 0.f;
	const bool bWallDetected = bAirborne && DetectWallForSlide(WallSign);

	if (!bWallDetected)
	{
		bIsWallSliding = false;
		return;
	}

	bIsWallSliding = true;
	WallSlideFacingSign = WallSign;

	if (MoveComp->Velocity.Z < -WallSlideSpeed)
	{
		FVector Velocity = MoveComp->Velocity;
		Velocity.Z = -WallSlideSpeed;
		MoveComp->Velocity = Velocity;
	}
}

bool ADeathMetalCatCharacter::CanTakeDamage() const
{
	return !bIsInvincible;
}

float ADeathMetalCatCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!CanTakeDamage())
	{
		// Mid-dodge i-frames -- ignore entirely, no health change, no Hurt animation.
		return 0.f;
	}

	float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (ActualDamage <= 0.f)
	{
		return ActualDamage;
	}

	// Defense attribute: percentage damage resistance, clamped so it can never reach 100% (which
	// would read as literal invincibility once Defense grows large enough over many levels).
	const float DefenseResistance = FMath::Clamp(Defense * DefenseResistPerPoint, 0.f, 0.9f);
	ActualDamage *= (1.f - DefenseResistance);

	Health = FMath::Max(0.f, Health - ActualDamage);

	// Any real damage (past the i-frame check above) fully resets GnarlyRank -- deliberate
	// high-risk/high-reward design per the GDD, not a bug.
	ResetGnarlyRank();

	TriggerQuip(EQuipTriggerType::Damage);

	if (UPaperFlipbookComponent* SpriteComp = GetSprite())
	{
		if (HurtFlipbook)
		{
			SpriteComp->SetFlipbook(HurtFlipbook);
			SpriteComp->SetLooping(false);
			SpriteComp->PlayFromStart();
			CurrentFlipbook = HurtFlipbook;
			bIsHurt = true;
			GetWorldTimerManager().SetTimer(HurtTimerHandle, this, &ADeathMetalCatCharacter::ClearHurtState, HurtDuration, false);
		}
	}

	if (Health <= 0.f && !bIsDead)
	{
		bIsDead = true;
		UE_LOG(LogTemp, Error, TEXT("PLAYER DIED"));
		HandleDeath();
	}

	return ActualDamage;
}

void ADeathMetalCatCharacter::ClearHurtState()
{
	bIsHurt = false;
}

void ADeathMetalCatCharacter::HandleDeath()
{
	// DisableInput blocks all bound Enhanced Input actions (movement, jump, dodge, sword, shoot)
	// for this pawn without needing to add a bIsDead guard to every handler. No hide/disable-
	// collision step like the enemy's version of this -- the player stays visible, playing out the
	// Hurt pose TakeDamage already started above, which reads fine as a placeholder death beat.
	DisableInput(Cast<APlayerController>(GetController()));

	GetWorldTimerManager().SetTimer(RespawnTimerHandle, this, &ADeathMetalCatCharacter::HandleRespawn, RespawnDelay, false);
}

void ADeathMetalCatCharacter::HandleRespawn()
{
	// No SetActorTransform here anymore -- the teleport now happens inside
	// ARoomProgressionManager::BeginRoomTransition (called via ResetToStartingRoom below), timed
	// to land during the fade's black pause rather than instantly/visibly right now. This also
	// resolves the old fragility this line used to have: InitialSpawnTransform only worked because
	// PlayerStart happened to be co-located with RoomShell_ROOM1; the new teleport instead reads
	// the starting room's actual live RoomShell position every time, so it can never drift out of
	// sync with wherever that shell really is.

	// Stop any leftover fall/knockback velocity now (independent of the teleport's own timing) --
	// without this, dying mid-air (or mid-dodge-launch) would have the character immediately
	// resume falling/sliding once actually teleported, using whatever velocity they died with.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
	}

	// Death fully restarts the biome run, not just the player's own position -- rooms deactivate
	// as the player advances, so without this a death in (say) Room6 would respawn the player at
	// Room1 while Room2-6 stayed hidden/collision-disabled from the original pass, leaving them
	// stuck. ResetToStartingRoom now routes through BeginRoomTransition (fade to black, re-activate
	// Room1 + re-arm every exit trigger + teleport during the pause, fade back in), rather than
	// switching instantly, so the death restart gets the same treatment as any other room change.
	if (ARoomProgressionManager* Manager = Cast<ARoomProgressionManager>(UGameplayStatics::GetActorOfClass(this, ARoomProgressionManager::StaticClass())))
	{
		Manager->ResetToStartingRoom();
	}

	Health = MaxHealth;
	bIsDead = false;

	// No EnableInput here anymore -- BeginRoomTransition re-enables input itself once the fade-in
	// completes, so the player can't act while the screen is still black/fading.

	// Brief invincibility so respawning doesn't immediately re-die standing in the same spot --
	// reuses the exact same mechanism as Dodge's i-frames (bIsInvincible/IFrameTimerHandle/
	// ClearInvincibility), just armed for PostRespawnInvulnDuration instead of IFrameDuration. Safe
	// to arm now (before input is even back) since it only ever gates incoming damage, never input.
	bIsInvincible = true;
	GetWorldTimerManager().SetTimer(IFrameTimerHandle, this, &ADeathMetalCatCharacter::ClearInvincibility, PostRespawnInvulnDuration, false);
}

float ADeathMetalCatCharacter::RollDamage(float BaseDamage, EDamageTier& OutTier) const
{
	const float Roll = FMath::FRand();
	if (Roll < CriticalChance)
	{
		OutTier = EDamageTier::Critical;
		return BaseDamage * CriticalMultiplier;
	}
	if (Roll < CriticalChance + WeaknessChance)
	{
		OutTier = EDamageTier::Weakness;
		return BaseDamage * WeaknessMultiplier;
	}
	OutTier = EDamageTier::Normal;
	return BaseDamage;
}

void ADeathMetalCatCharacter::SpawnDamageNumber(const FVector& Location, float DamageAmount, EDamageTier Tier)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ADamageNumberActor* Number = World->SpawnActor<ADamageNumberActor>(ADamageNumberActor::StaticClass(), FTransform(Location));
	if (Number)
	{
		Number->InitDamageNumber(DamageAmount, Tier);
	}
}

void ADeathMetalCatCharacter::RegisterGnarlyHit()
{
	++GnarlyHitCount;

	const int32 PreviousRank = GnarlyRank;
	while (GnarlyRank < GnarlyRankThresholds.Num() && GnarlyHitCount >= GnarlyRankThresholds[GnarlyRank])
	{
		++GnarlyRank;
	}

	if (GnarlyRank != PreviousRank)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GNARLY RANK] Rank up: %d -> %d (GnarlyHitCount=%d)"), PreviousRank, GnarlyRank, GnarlyHitCount);
	}
}

void ADeathMetalCatCharacter::ResetGnarlyRank()
{
	if (GnarlyHitCount > 0 || GnarlyRank > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GNARLY RANK] Reset (was rank %d, GnarlyHitCount %d) -- took damage"), GnarlyRank, GnarlyHitCount);
	}

	GnarlyHitCount = 0;
	GnarlyRank = 0;
}

void ADeathMetalCatCharacter::RecalculateXPToNextLevel()
{
	XPToNextLevel = CurrentLevel * XPPerLevelBase;
}

void ADeathMetalCatCharacter::ApplyAttributeEffects()
{
	// Speed is the only attribute applied as a cached/derived value rather than inline at its
	// point of use -- Strength/Dexterity/Defense are read fresh each time in
	// OnSwordHitboxBeginOverlap/FireShotTrace/TakeDamage instead.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		const float SpeedMultiplier = 1.f + (Speed * SpeedMultiplierPerPoint);
		MoveComp->MaxWalkSpeed = MaxMoveSpeed * SpeedMultiplier;
	}
}

void ADeathMetalCatCharacter::AddXP(float Amount)
{
	if (CurrentLevel >= MaxLevel)
	{
		// Capped -- no further XP gain or leveling past MaxLevel, per design.
		return;
	}

	CurrentXP += Amount;

	// While loop (not a single if): handles a single large XP award crossing more than one
	// threshold at once, correctly applying every level-up's attribute/skill-point gains rather
	// than just the first.
	while (CurrentLevel < MaxLevel && CurrentXP >= XPToNextLevel)
	{
		CurrentXP -= XPToNextLevel;
		++CurrentLevel;

		Speed += AttributeGainPerLevel;
		Strength += AttributeGainPerLevel;
		Dexterity += AttributeGainPerLevel;
		Defense += AttributeGainPerLevel;
		SkillPoints += SkillPointsPerLevel;

		RecalculateXPToNextLevel();
		ApplyAttributeEffects();

		UE_LOG(LogTemp, Warning, TEXT("[LEVEL UP] Level %d  Speed=%.1f Strength=%.1f Dexterity=%.1f Defense=%.1f SkillPoints=%d"),
			CurrentLevel, Speed, Strength, Dexterity, Defense, SkillPoints);
	}
}

void ADeathMetalCatCharacter::TriggerQuip(EQuipTriggerType TriggerType)
{
	if (!QuipDataTable || !GnarlyRankHUDWidgetInstance)
	{
		return;
	}

	float* PerTypeLastTime = nullptr;
	float PerTypeCooldown = 0.f;
	switch (TriggerType)
	{
	case EQuipTriggerType::Kill:
		PerTypeLastTime = &LastKillQuipTime;
		PerTypeCooldown = QuipCooldown_Kill;
		break;
	case EQuipTriggerType::Damage:
		PerTypeLastTime = &LastDamageQuipTime;
		PerTypeCooldown = QuipCooldown_Damage;
		break;
	case EQuipTriggerType::Environment:
		PerTypeLastTime = &LastEnvironmentQuipTime;
		PerTypeCooldown = QuipCooldown_Environment;
		break;
	}

	if (!PerTypeLastTime)
	{
		return;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();

	// Both cooldown layers must pass -- the short GlobalQuipDebounce (shared across every trigger
	// type) AND this type's own independent per-type cooldown. Neither is touched below unless a
	// quip actually fires: a suppressed attempt must never consume/reset either timer.
	if (CurrentTime - LastQuipShownTime < GlobalQuipDebounce)
	{
		return;
	}

	if (CurrentTime - *PerTypeLastTime < PerTypeCooldown)
	{
		return;
	}

	FString Line;
	FString SoundTag;
	if (!UQuipLibrary::GetRandomQuip(QuipDataTable, TriggerType, Line, SoundTag))
	{
		return;
	}

	*PerTypeLastTime = CurrentTime;
	LastQuipShownTime = CurrentTime;
	GnarlyRankHUDWidgetInstance->ShowQuip(Line, QuipDisplayDuration);
}

void ADeathMetalCatCharacter::TestQuip(const FString& TriggerTypeName)
{
	EQuipTriggerType TriggerType;
	if (TriggerTypeName.Equals(TEXT("Kill"), ESearchCase::IgnoreCase))
	{
		TriggerType = EQuipTriggerType::Kill;
	}
	else if (TriggerTypeName.Equals(TEXT("Damage"), ESearchCase::IgnoreCase))
	{
		TriggerType = EQuipTriggerType::Damage;
	}
	else if (TriggerTypeName.Equals(TEXT("Environment"), ESearchCase::IgnoreCase))
	{
		TriggerType = EQuipTriggerType::Environment;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[QUIP] TestQuip: unrecognized trigger type '%s' -- expected Kill/Damage/Environment"), *TriggerTypeName);
		return;
	}

	TriggerQuip(TriggerType);
}

void ADeathMetalCatCharacter::TestJumpDistance(const FString& JumpTypeName)
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp)
	{
		return;
	}

	const bool bRunning = JumpTypeName.Equals(TEXT("Running"), ESearchCase::IgnoreCase);
	const bool bStanding = JumpTypeName.Equals(TEXT("Standing"), ESearchCase::IgnoreCase);
	if (!bRunning && !bStanding)
	{
		UE_LOG(LogTemp, Warning, TEXT("[JUMP DISTANCE TEST] Unrecognized type '%s' -- expected Running or Standing"), *JumpTypeName);
		return;
	}

	if (!MoveComp->IsMovingOnGround())
	{
		UE_LOG(LogTemp, Warning, TEXT("[JUMP DISTANCE TEST] Must be grounded to start a test -- not currently on ground"));
		return;
	}

	JumpDistanceTestLabel = JumpTypeName;
	JumpDistanceTestStartX = GetActorLocation().X;

	// Running: current effective MaxWalkSpeed (post attribute scaling), matching a real player at
	// full run-up speed the instant they leave the ground. Standing: zero -- the whole arc's
	// horizontal distance comes purely from AirControl-driven acceleration during flight (see
	// UpdateJumpDistanceTest, which holds forward input every tick regardless of which type this is).
	FVector Velocity = MoveComp->Velocity;
	Velocity.X = bRunning ? MoveComp->MaxWalkSpeed : 0.f;
	Velocity.Z = 0.f;
	MoveComp->Velocity = Velocity;

	Jump();
	bJumpDistanceTestActive = true;
	bJumpDistanceTestHasLeftGround = false;

	UE_LOG(LogTemp, Warning, TEXT("[JUMP DISTANCE TEST] Started '%s' jump test at X=%.2f, initial VelocityX=%.1f (MaxWalkSpeed=%.1f, JumpZVelocity=%.1f, AirControl=%.2f)"),
		*JumpDistanceTestLabel, JumpDistanceTestStartX, Velocity.X, MoveComp->MaxWalkSpeed, MoveComp->JumpZVelocity, MoveComp->AirControl);
}

void ADeathMetalCatCharacter::UpdateJumpDistanceTest()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp)
	{
		bJumpDistanceTestActive = false;
		return;
	}

	// Held for the entire arc, matching how a player actually clears a gap -- not just a takeoff
	// impulse.
	AddMovementInput(FVector(1.f, 0.f, 0.f), 1.f);

	const bool bFalling = MoveComp->IsFalling();
	if (bFalling)
	{
		bJumpDistanceTestHasLeftGround = true;
	}

	// Only counts as "landed" after having actually been observed falling first -- guards against
	// a false-positive on the very first tick, before CharacterMovementComponent has processed the
	// jump (and transitioned out of Walking) yet.
	if (bJumpDistanceTestHasLeftGround && !bFalling)
	{
		const float EndX = GetActorLocation().X;
		const float Distance = EndX - JumpDistanceTestStartX;
		UE_LOG(LogTemp, Warning, TEXT("[JUMP DISTANCE TEST] '%s' jump LANDED: StartX=%.2f EndX=%.2f Distance=%.2f"),
			*JumpDistanceTestLabel, JumpDistanceTestStartX, EndX, Distance);
		bJumpDistanceTestActive = false;
		bJumpDistanceTestHasLeftGround = false;
	}
}

void ADeathMetalCatCharacter::HandleSwordAttack(const FInputActionValue& Value)
{
	if (bIsAttacking)
	{
		// Ignore re-triggers while already mid-swing rather than restarting/stacking timers.
		return;
	}

	bIsAttacking = true;

	if (UPaperFlipbookComponent* SpriteComp = GetSprite())
	{
		if (SwordAttackFlipbook)
		{
			SpriteComp->SetFlipbook(SwordAttackFlipbook);
			SpriteComp->SetLooping(false);
			SpriteComp->PlayFromStart();
			CurrentFlipbook = SwordAttackFlipbook;
		}

		// Mirror the hitbox to whichever side the character is currently facing. Only the sign
		// of the offset changes -- the box's extent (shape/size) is symmetric either way.
		// FacingSign matches Scale.X's sign directly (Scale.X >= 0 -> facing right,
		// FacingSign=+1; Scale.X < 0 -> facing left, FacingSign=-1) -- confirmed correct via a
		// direct visual cross-check (a debug arrow drawn as a pure world-space offset, verified
		// to point the same way the sprite visibly faces on screen).
		//
		// BUT: SwordHitbox is parented to RootComponent, and its offset is applied via
		// SetRelativeLocation -- a ROOT-LOCAL offset, not a world-space one. That's only
		// equivalent to a world-space offset if the root/actor's own rotation is identity. The
		// old verification of this line only ever checked the logged RelativeLocation/WorldLocation
		// numbers against the EXPECTED sign convention (self-consistent, since the log and the
		// convention comment were written by the same assumption) -- it never cross-checked
		// against an independently-confirmed visual reference. The arrow test did exactly that
		// and showed the hitbox landing on the opposite side from the visibly-correct facing,
		// which the root-local-vs-world-space distinction fully explains: this character's
		// placed/spawned actor rotation isn't identity, so root-local +X isn't world +X here.
		// Sign is inverted below to compensate. This is correct for THIS character's current
		// spawn rotation specifically, not a universal geometric law -- if the spawn rotation
		// ever changes, this may need re-flipping (or switching SwordHitbox to a world-space
		// SetWorldLocation offset, immune to root rotation, would remove this fragility entirely).
		if (SwordHitbox)
		{
			const float CurrentScaleX = SpriteComp->GetRelativeScale3D().X;
			const float FacingSign = (CurrentScaleX < 0.f) ? -1.f : 1.f;
			FVector Loc = SwordHitbox->GetRelativeLocation();
			Loc.X = -FacingSign * FMath::Abs(Loc.X);
			SwordHitbox->SetRelativeLocation(Loc);
		}
	}

	// Hitbox turns on partway through the swing (skipping wind-up) and off again before the
	// swing fully ends (skipping recovery) -- see HitboxActiveDelay/HitboxActiveDuration comments.
	GetWorldTimerManager().SetTimer(SwordHitboxEnableTimerHandle, this, &ADeathMetalCatCharacter::EnableSwordHitbox, HitboxActiveDelay, false);
	GetWorldTimerManager().SetTimer(SwordAttackEndTimerHandle, this, &ADeathMetalCatCharacter::ClearAttackState, AttackDuration, false);
}

void ADeathMetalCatCharacter::EnableSwordHitbox()
{
	if (SwordHitbox)
	{
		SwordHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}

	GetWorldTimerManager().SetTimer(SwordHitboxDisableTimerHandle, this, &ADeathMetalCatCharacter::DisableSwordHitbox, HitboxActiveDuration, false);
}

void ADeathMetalCatCharacter::DisableSwordHitbox()
{
	if (SwordHitbox)
	{
		SwordHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void ADeathMetalCatCharacter::ClearAttackState()
{
	bIsAttacking = false;
}

void ADeathMetalCatCharacter::OnSwordHitboxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Guard against self-overlap (components of this same actor never fire against each other in
	// practice, but this is the correctness check a real damage system will also need) and a
	// null actor, so this only ever logs/forwards genuine external hits.
	if (!OtherActor || OtherActor == this)
	{
		return;
	}

	EDamageTier Tier;
	float RolledDamage = RollDamage(SwordBaseDamage, Tier);

	// GnarlyRank and Strength both grant melee-ONLY damage bonuses, applied on top of the tier
	// roll above (not replacing it). They MULTIPLY together (not add) -- keeps both systems
	// meaningfully impactful rather than one diluting the other. Gun damage deliberately does NOT
	// get either of these, see FireShotTrace.
	const float GnarlyMultiplier = 1.f + (GnarlyRank * GnarlyRankMeleeDamageBonusPerRank);
	const float StrengthMultiplier = 1.f + (Strength * StrengthMultiplierPerPoint);
	RolledDamage *= GnarlyMultiplier * StrengthMultiplier;

	const float DamageApplied = UGameplayStatics::ApplyDamage(OtherActor, RolledDamage, GetController(), this, UDamageType::StaticClass());

	UE_LOG(LogTemp, Warning, TEXT("Sword hitbox overlapped actor: %s (component: %s), dealt %.1f damage (tier=%d, GnarlyRank=%d, GnarlyMultiplier=%.2f, Strength=%.1f, StrengthMultiplier=%.2f)"),
		*OtherActor->GetName(), OtherComp ? *OtherComp->GetName() : TEXT("<none>"), DamageApplied, (int32)Tier, GnarlyRank, GnarlyMultiplier, Strength, StrengthMultiplier);

	if (DamageApplied > 0.f)
	{
		SpawnDamageNumber(OtherActor->GetActorLocation() + FVector(0.f, 0.f, DamageNumberSpawnHeight), DamageApplied, Tier);
		RegisterGnarlyHit();
	}
}

namespace
{
	// FB_DeathMetalCat_Shoot frame roles (only the draw pose is still used from this row -- the
	// held-fire loop now comes from the dedicated FB_DeathMetalCat_HoldFire flipbook instead;
	// see HoldFireFlipbook's doc comment for why):
	//   [2] arms bent/compact, gun pulled in close to the body -- the one frame visually distinct
	//       from the other three (which are all full-extension variants); read as the character
	//       mid-raise, not yet at full extension, so used as the "draw" pose
	constexpr int32 ShootFrame_Draw = 2;
}

void ADeathMetalCatCharacter::HandleShootStarted(const FInputActionValue& Value)
{
	bIsHoldingShootButton = true;

	// TEMP diagnostic: is Started re-firing mid-hold (it shouldn't -- EnhancedInput's Started only
	// fires once per press), which would explain BeginDraw() (and its draw frame) recurring.
	UE_LOG(LogTemp, Warning, TEXT("[SHOOT STATE] t=%f  HandleShootStarted  ShootAnimPhase(before)=%d"),
		GetWorld()->GetTimeSeconds(), (int32)ShootAnimPhase);

	// Defensive: don't trust that HandleShootReleased always cleaned up properly. Rapid
	// press/release timing (or a release racing a movement input) is exactly where that
	// assumption broke before -- ShootAnimPhase could be left stuck non-None from a prior press,
	// silently blocking every future BeginDraw() forever. Force a clean slate before proceeding
	// whenever we find state left over, rather than only handling the case we expect.
	if (ShootAnimPhase != EShootPhase::None)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SHOOT STATE] HandleShootStarted found stale ShootAnimPhase=%d, forcing reset"), (int32)ShootAnimPhase);
		ResetShootState();
	}

	BeginDraw();
}

void ADeathMetalCatCharacter::HandleShootHeld(const FInputActionValue& Value)
{
	// TEMP diagnostic: prints every Triggered tick's guard inputs, to see exactly why it does or
	// doesn't proceed to FireShotTrace() each time.
	UE_LOG(LogTemp, Warning, TEXT("[SHOOT STATE] t=%f  HandleShootHeld  bIsHoldingShootButton=%d  ShootAnimPhase=%d  bIsShooting=%d"),
		GetWorld()->GetTimeSeconds(), bIsHoldingShootButton, (int32)ShootAnimPhase, bIsShooting);

	// Triggered fires every tick while held, including the same tick as Started. Only attempt a
	// repeat shot once the hold-fire loop has taken over from the initial draw -- this both paces
	// repeated fire correctly and skips the redundant same-tick call from Started, since
	// ShootAnimPhase is Drawing (not HoldFiring) at that point. The first shot of a hold fires
	// from BeginHoldFireLoop() itself, not from here.
	if (!bIsHoldingShootButton || ShootAnimPhase != EShootPhase::HoldFiring || bIsShooting)
	{
		return;
	}

	FireShotTrace();
}

void ADeathMetalCatCharacter::HandleShootReleased(const FInputActionValue& Value)
{
	// TEMP diagnostic: is this firing spuriously mid-hold (Completed/Canceled should only fire
	// once, on actual release) -- if so, that would explain ShootAnimPhase getting reset to None
	// while still held, causing HandleShootStarted to treat the next Triggered/Started as a fresh
	// press and restart the draw, which would produce exactly the reported draw/flash bounce.
	UE_LOG(LogTemp, Warning, TEXT("[SHOOT STATE] t=%f  HandleShootReleased  ShootAnimPhase(before)=%d"),
		GetWorld()->GetTimeSeconds(), (int32)ShootAnimPhase);

	bIsHoldingShootButton = false;

	// Unconditional cleanup regardless of which phase we were in when released. The previous
	// version only reset ShootAnimPhase for the (now-removed) Aiming case, on the assumption an
	// in-flight draw sequence would always finish naturally and clean up after itself -- but if
	// that sequence's own fire call bailed out early on its bIsShooting guard (which can happen
	// on rapid taps, since a prior shot's cooldown may still be active), nothing was ever left to
	// un-stick ShootAnimPhase, and it stayed stuck at Drawing indefinitely -- the actual freeze
	// bug, confirmed via the diagnostic logs.
	ResetShootState();
}

void ADeathMetalCatCharacter::ResetShootState()
{
	GetWorldTimerManager().ClearTimer(ShootDrawTimerHandle);
	GetWorldTimerManager().ClearTimer(ShootTimerHandle);
	ShootAnimPhase = EShootPhase::None;
	bIsShooting = false;
}

void ADeathMetalCatCharacter::BeginDraw()
{
	ShootAnimPhase = EShootPhase::Drawing;
	SetShootFrame(ShootFrame_Draw, TEXT("draw"));
	GetWorldTimerManager().SetTimer(ShootDrawTimerHandle, this, &ADeathMetalCatCharacter::BeginHoldFireLoop, DrawDuration, false);
}

void ADeathMetalCatCharacter::BeginHoldFireLoop()
{
	ShootAnimPhase = EShootPhase::HoldFiring;

	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	UpdateHoldFireFlipbook(MoveComp && MoveComp->IsFalling());

	UE_LOG(LogTemp, Warning, TEXT("[SHOOT STATE] t=%f  BeginHoldFireLoop"), GetWorld()->GetTimeSeconds());

	FireShotTrace();
}

void ADeathMetalCatCharacter::UpdateHoldFireFlipbook(bool bAirborne)
{
	UPaperFlipbookComponent* SpriteComp = GetSprite();
	if (!SpriteComp)
	{
		return;
	}

	// Engine-driven looping playback, not manual SetPlaybackPositionInFrames jumping: both rows'
	// frames already cycle through their own variations (muzzle-flash / down-shot) while holding
	// a consistent pose, so there's no per-frame role for code to pick between anymore -- just
	// let whichever one applies play.
	UPaperFlipbook* DesiredFlipbook = bAirborne ? AirDownShotFlipbook : HoldFireFlipbook;
	if (DesiredFlipbook && CurrentFlipbook != DesiredFlipbook)
	{
		SpriteComp->SetFlipbook(DesiredFlipbook);
		SpriteComp->SetLooping(true);
		SpriteComp->PlayFromStart();
		CurrentFlipbook = DesiredFlipbook;
	}
}

void ADeathMetalCatCharacter::ApplyAirFireFloat()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp)
	{
		return;
	}

	if (!bAirFireFloatActive)
	{
		MoveComp->GravityScale = DefaultGravityScale * AirFireGravityScaleMultiplier;
		bAirFireFloatActive = true;
	}

	// (Re-)arms the same timer on every airborne shot -- continuous fire extends the float window
	// rather than stacking the multiplier again on top of an already-reduced gravity scale.
	GetWorldTimerManager().SetTimer(AirFireFloatTimerHandle, this, &ADeathMetalCatCharacter::ClearAirFireFloat, AirFireFloatDuration, false);
}

void ADeathMetalCatCharacter::ClearAirFireFloat()
{
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->GravityScale = DefaultGravityScale;
	}
	bAirFireFloatActive = false;
}

void ADeathMetalCatCharacter::FireShotTrace()
{
	if (bIsShooting)
	{
		// Fire-rate cooldown still active -- this can happen if HandleShootHeld's own guard is
		// bypassed by the timer-driven call from BeginHoldFireLoop() racing a very short
		// FireCooldown; bail out rather than fire early. The next Triggered tick will retry once
		// eligible.
		return;
	}

	// Same facing convention as the sword hitbox (confirmed correct via logged data): unflipped
	// (Scale.X >= 0, facing right) fires toward +X; flipped (Scale.X < 0, facing left) fires -X.
	// Read fresh each shot (not cached from when the button was first pressed) so a direction
	// change mid-hold correctly affects the next shot.
	UPaperFlipbookComponent* SpriteComp = GetSprite();
	const float FacingSign = (SpriteComp && SpriteComp->GetRelativeScale3D().X < 0.f) ? -1.f : 1.f;

	// Airborne re-checked fresh per shot (not cached from the start of the hold) so jumping or
	// landing mid-hold correctly changes the very next shot, same as the facing check above.
	// AirDownShotFlipbook is kept in sync with this same check via UpdateHoldFireFlipbook.
	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	const bool bAirborneShot = MoveComp && MoveComp->IsFalling();
	UpdateHoldFireFlipbook(bAirborneShot);

	if (bAirborneShot)
	{
		// Independent of FireDirection/animation below -- this only ever touches GravityScale, so
		// it can't fight with the fixed 45-degree trajectory or the Air Down Shot flipbook.
		ApplyAirFireFloat();
	}

	// Airborne: fixed 45 degrees downward, horizontal component still respecting facing (an
	// aim-down-forward shot, not a pure vertical drop) -- grounded: unchanged pure horizontal.
	const FVector FireDirection = bAirborneShot
		? FVector(FacingSign, 0.f, -1.f).GetSafeNormal()
		: FVector(FacingSign, 0.f, 0.f);

	const FVector Start = GetActorLocation();
	const FVector End = Start + FireDirection * MaxTraceRange;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	// Swept as a small sphere, not a zero-thickness line -- confirmed root cause of gun fire
	// whiffing against a correctly-aimed, correctly-collision-configured enemy: a raw line trace
	// has zero tolerance for the target's Y-position exactly matching the trace's fixed Y-line,
	// while a swept sphere (standard practice for hitscan weapons) tolerates a small margin. See
	// GunTraceRadius's doc comment for more.
	FHitResult Hit;
	const bool bHit = GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeSphere(GunTraceRadius), QueryParams);

	if (bHit && Hit.GetActor())
	{
		EDamageTier Tier;
		// No GnarlyRank multiplier here -- deliberately melee-only per the GDD (see OnSwordHitboxBeginOverlap).
		// Dexterity applies the same multiplicative way Strength does on the sword -- gun has no
		// second (Gnarly-style) multiplier to stack against right now, but this keeps the pattern
		// consistent for if that changes later.
		float RolledDamage = RollDamage(GunBaseDamage, Tier);
		const float DexterityMultiplier = 1.f + (Dexterity * DexterityMultiplierPerPoint);
		RolledDamage *= DexterityMultiplier;

		const float DamageApplied = UGameplayStatics::ApplyDamage(Hit.GetActor(), RolledDamage, GetController(), this, UDamageType::StaticClass());

		UE_LOG(LogTemp, Warning, TEXT("Gun fire hit actor: %s at location %s, dealt %.1f damage (tier=%d, Dexterity=%.1f, DexterityMultiplier=%.2f)"),
			*Hit.GetActor()->GetName(), *Hit.Location.ToString(), DamageApplied, (int32)Tier, Dexterity, DexterityMultiplier);

		if (DamageApplied > 0.f)
		{
			SpawnDamageNumber(Hit.Location + FVector(0.f, 0.f, DamageNumberSpawnHeight), DamageApplied, Tier);
			// Gun hits still charge GnarlyHitCount toward the next rank -- only the melee damage
			// bonus itself is sword-exclusive, not rank progression.
			RegisterGnarlyHit();
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Gun fire hit nothing (traced full range %f)"), MaxTraceRange);
	}

	bIsShooting = true;
	GetWorldTimerManager().SetTimer(ShootTimerHandle, this, &ADeathMetalCatCharacter::ClearShootingState, FireCooldown, false);
}

void ADeathMetalCatCharacter::SetShootFrame(int32 FrameIndex, const TCHAR* Reason)
{
	UPaperFlipbookComponent* SpriteComp = GetSprite();
	if (!SpriteComp || !ShootFlipbook)
	{
		return;
	}

	if (CurrentFlipbook != ShootFlipbook)
	{
		SpriteComp->SetFlipbook(ShootFlipbook);
		SpriteComp->Stop();
		CurrentFlipbook = ShootFlipbook;
	}

	// TEMP diagnostic: every actual frame-index write, with why and when, to see the real
	// state-machine sequence during a hold instead of guessing at it.
	UE_LOG(LogTemp, Warning, TEXT("[SHOOT FRAME] t=%f  frame=%d  reason=%s  (was %d)"),
		GetWorld()->GetTimeSeconds(), FrameIndex, Reason, SpriteComp->GetPlaybackPositionInFrames());

	if (SpriteComp->GetPlaybackPositionInFrames() != FrameIndex)
	{
		SpriteComp->SetPlaybackPositionInFrames(FrameIndex, false);
	}
}

void ADeathMetalCatCharacter::ClearShootingState()
{
	bIsShooting = false;
}

void ADeathMetalCatCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateWallSlide();
	UpdateAnimation();

	if (bJumpDistanceTestActive)
	{
		UpdateJumpDistanceTest();
	}
}

void ADeathMetalCatCharacter::UpdateAnimation()
{
	UPaperFlipbookComponent* SpriteComp = GetSprite();
	if (!SpriteComp)
	{
		return;
	}

	const FVector Velocity = GetVelocity();
	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	const bool bAirborne = MoveComp && MoveComp->IsFalling();

	if (bIsHurt)
	{
		// Checked first (highest visual priority): HurtFlipbook is started once (non-looping) in
		// TakeDamage itself, so there's nothing to switch here -- just make sure a hit reaction
		// always visually shows even if it lands mid-swing/mid-shot. Note this deliberately does
		// NOT cancel bIsAttacking/ShootAnimPhase/their timers -- those keep running in the
		// background and resume controlling the sprite once bIsHurt clears, rather than risking
		// subtler bugs from force-cancelling unrelated state machines mid-flight.
	}
	else if (bIsAttacking)
	{
		// SwordAttackFlipbook is started once (non-looping) in HandleSwordAttack itself, so there's
		// nothing to switch here -- just make sure nothing else stomps it while attacking.
	}
	else if (ShootAnimPhase != EShootPhase::None)
	{
		// Draw (event-driven, via SetShootFrame) and HoldFiring (engine-driven looping playback,
		// started once in BeginHoldFireLoop) both manage their own flipbook/frame state -- nothing
		// to do here except make sure nothing else stomps it while a shoot sequence is in
		// progress. Note this checks ShootAnimPhase, not bIsShooting: bIsShooting is only the
		// short per-shot fire-rate gate now, not the (much longer) whole-hold animation window.
	}
	else if (bIsDodging)
	{
		// The 5-frame handspring sequence is fully event-driven (HandleDodge / AdvanceDodgeFrame
		// via SetDodgeFrame), not tick-based -- nothing to do here except make sure nothing else
		// stomps it while a dodge is in progress.
	}
	else if (bIsWallSliding)
	{
		if (WallSlideFlipbook && CurrentFlipbook != WallSlideFlipbook)
		{
			SpriteComp->SetFlipbook(WallSlideFlipbook);
			SpriteComp->SetLooping(true);
			SpriteComp->Play();
			CurrentFlipbook = WallSlideFlipbook;
		}
	}
	else if (bAirborne)
	{
		// FB_DeathMetalCat_Jump's two frames (rising pose, landing pose) are picked explicitly
		// by velocity direction instead of left to play on the flipbook's own timer: how long
		// the rising pose should hold depends on jump height/gravity, which varies per jump --
		// a fixed per-keyframe hold on the asset can't express that, so this has to be code-driven.
		if (JumpFlipbook && CurrentFlipbook != JumpFlipbook)
		{
			SpriteComp->SetFlipbook(JumpFlipbook);
			SpriteComp->Stop();
			CurrentFlipbook = JumpFlipbook;
		}
		if (JumpFlipbook)
		{
			const int32 DesiredFrame = (Velocity.Z >= 0.f) ? 0 : 1; // 0 = rising/peak, 1 = falling
			if (SpriteComp->GetPlaybackPositionInFrames() != DesiredFrame)
			{
				SpriteComp->SetPlaybackPositionInFrames(DesiredFrame, false);
			}
		}
	}
	else
	{
		UPaperFlipbook* DesiredFlipbook = IdleFlipbook;
		const float CurrentMoveSpeed = FMath::Abs(Velocity.X);
		if (CurrentMoveSpeed > KINDA_SMALL_NUMBER)
		{
			DesiredFlipbook = (CurrentMoveSpeed >= WalkSpeedThreshold) ? RunFlipbook : WalkFlipbook;
		}

		if (DesiredFlipbook && DesiredFlipbook != CurrentFlipbook)
		{
			SpriteComp->SetFlipbook(DesiredFlipbook);
			SpriteComp->SetLooping(true);
			SpriteComp->Play();
			CurrentFlipbook = DesiredFlipbook;
		}
	}

	if (bIsDodging)
	{
		// Facing is locked for the whole dodge, not driven by velocity: a dodge's velocity points
		// backward (away from facing, by design -- see HandleDodge), so the normal velocity-based
		// flip below would incorrectly re-face the character toward the dodge's movement direction
		// if allowed to run here. Hold at whatever facing was captured the instant the dodge started.
		FVector Scale = SpriteComp->GetRelativeScale3D();
		Scale.X = DodgeFacingSignAtStart * FMath::Abs(Scale.X);
		SpriteComp->SetRelativeScale3D(Scale);
	}
	else if (bIsWallSliding)
	{
		// Face INTO the wall (WallSlideFacingSign), not by velocity: Velocity.X is normally ~0
		// while pressed against a wall (blocked), so the velocity-based flip below would have
		// nothing reliable to go on here.
		FVector Scale = SpriteComp->GetRelativeScale3D();
		Scale.X = WallSlideFacingSign * FMath::Abs(Scale.X);
		SpriteComp->SetRelativeScale3D(Scale);
	}
	else if (FMath::Abs(Velocity.X) > KINDA_SMALL_NUMBER)
	{
		FVector Scale = SpriteComp->GetRelativeScale3D();
		Scale.X = (Velocity.X < 0.f) ? -FMath::Abs(Scale.X) : FMath::Abs(Scale.X);
		SpriteComp->SetRelativeScale3D(Scale);
	}
}
