#include "DeathMetalCatCharacter.h"

#include "PaperFlipbookComponent.h"
#include "PaperFlipbook.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Components/InputComponent.h"
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
#include "DeathMetalCatEnemyBase.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

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

	// Captured before any per-flipbook feet correction ever runs -- see ApplyFeetOffsetCorrection.
	if (GetSprite())
	{
		BaseSpriteRelativeLocation = GetSprite()->GetRelativeLocation();
	}

	PopulateFeetOffsetCorrections();
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

		if (AimDownAction)
		{
			EnhancedInput->BindAction(AimDownAction, ETriggerEvent::Started, this, &ADeathMetalCatCharacter::HandleAimDownStarted);
			EnhancedInput->BindAction(AimDownAction, ETriggerEvent::Completed, this, &ADeathMetalCatCharacter::HandleAimDownReleased);
			EnhancedInput->BindAction(AimDownAction, ETriggerEvent::Canceled, this, &ADeathMetalCatCharacter::HandleAimDownReleased);
		}

		if (BlockAction)
		{
			EnhancedInput->BindAction(BlockAction, ETriggerEvent::Started, this, &ADeathMetalCatCharacter::HandleBlockStarted);
			EnhancedInput->BindAction(BlockAction, ETriggerEvent::Completed, this, &ADeathMetalCatCharacter::HandleBlockReleased);
			EnhancedInput->BindAction(BlockAction, ETriggerEvent::Canceled, this, &ADeathMetalCatCharacter::HandleBlockReleased);
		}

		if (InvulnDashAction)
		{
			EnhancedInput->BindAction(InvulnDashAction, ETriggerEvent::Started, this, &ADeathMetalCatCharacter::HandleInvulnDash);
		}

		if (RageActivateAction)
		{
			EnhancedInput->BindAction(RageActivateAction, ETriggerEvent::Started, this, &ADeathMetalCatCharacter::HandleUltimateActivate);
		}
	}
}

void ADeathMetalCatCharacter::HandleMoveRight(const FInputActionValue& Value)
{
	// Cached unconditionally, before any of the guards below -- DetectWallForSlide needs to know
	// which direction the player is holding toward even on ticks where that input doesn't
	// actually get to move the character (e.g. during the wall-jump input lockout just below).
	LastMoveRightAxisValue = Value.Get<float>();

	// Standing block: no movement at all while Block is held, regardless of airborne state --
	// unlike the Shoot/Attack guard below, this is a deliberate full lockout, not just a
	// grounded-only plant.
	if (bIsBlocking)
	{
		return;
	}

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
	if (bIsDodging || bIsBlocking || bIsDashing || bIsTransformed)
	{
		// Ignore re-triggers while already mid-dodge, and don't stack with Block/Dash -- these
		// three are mutually exclusive movement states. Also disabled while riding Fancy Pants --
		// no Fancy-Cayde art exists for Dodge, so playing it would flash back to solo Cayde's sprite
		// mid-transformation.
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

void ADeathMetalCatCharacter::HandleAimDownStarted(const FInputActionValue& Value)
{
	bIsHoldingDownInput = true;
}

void ADeathMetalCatCharacter::HandleAimDownReleased(const FInputActionValue& Value)
{
	bIsHoldingDownInput = false;
}

void ADeathMetalCatCharacter::HandleBlockStarted(const FInputActionValue& Value)
{
	if (bIsDodging || bIsDashing || bIsTransformed)
	{
		// Don't stack with Dodge/Dash -- mutually exclusive movement states. Also disabled while
		// riding Fancy Pants -- see HandleDodge's comment on the same restriction.
		return;
	}

	bIsBlocking = true;

	if (UPaperFlipbookComponent* SpriteComp = GetSprite())
	{
		BlockFacingSignAtStart = (SpriteComp->GetRelativeScale3D().X < 0.f) ? -1.f : 1.f;

		if (BlockFlipbook)
		{
			SpriteComp->SetFlipbook(BlockFlipbook);
			SpriteComp->SetLooping(true);
			SpriteComp->Play();
			CurrentFlipbook = BlockFlipbook;
		}
	}
}

void ADeathMetalCatCharacter::HandleBlockReleased(const FInputActionValue& Value)
{
	// Immediate: no recovery lockout on release, per spec.
	bIsBlocking = false;
}

void ADeathMetalCatCharacter::HandleInvulnDash(const FInputActionValue& Value)
{
	if (bIsDodging || bIsBlocking || bIsDashing || bIsTransformed)
	{
		// Don't stack with Dodge/Block, and ignore a repress mid-dash. Also disabled while riding
		// Fancy Pants -- see HandleDodge's comment on the same restriction.
		return;
	}

	UPaperFlipbookComponent* SpriteComp = GetSprite();
	const float FacingSign = (SpriteComp && SpriteComp->GetRelativeScale3D().X < 0.f) ? -1.f : 1.f;
	DashFacingSignAtStart = FacingSign;

	if (SpriteComp && InvulnDashFlipbook)
	{
		SpriteComp->SetFlipbook(InvulnDashFlipbook);
		SpriteComp->SetLooping(false);
		SpriteComp->PlayFromStart();
		CurrentFlipbook = InvulnDashFlipbook;
	}

	// Forward burst (toward facing) -- the opposite direction from Dodge's backward retreat.
	// A CONSTANT velocity sustained for the whole duration (reasserted every tick by
	// UpdateInvulnDash), not a single decaying LaunchCharacter impulse -- see UpdateInvulnDash's
	// doc comment for why: a one-shot impulse covered wildly different distances grounded vs.
	// airborne (~129uu vs ~669uu for the same impulse, confirmed via direct measurement), which
	// doesn't match "fixed-distance, fixed-duration dash." Z is left untouched here (and every
	// tick after) so gravity still applies normally if the dash starts mid-air.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		FVector Velocity = MoveComp->Velocity;
		Velocity.X = FacingSign * InvulnDashImpulseStrength;
		MoveComp->Velocity = Velocity;
	}

	bIsDashing = true;
	bIsInvincible = true;

	// Never let the state end before the flipbook (eyes turning red -> dash -> after-image trail)
	// has actually finished playing, regardless of how InvulnDashDuration or the flipbook's own fps
	// get retuned later -- see InvulnDashDuration's doc comment.
	float EffectiveDashDuration = InvulnDashDuration;
	if (InvulnDashFlipbook)
	{
		EffectiveDashDuration = FMath::Max(EffectiveDashDuration, InvulnDashFlipbook->GetTotalDuration() + 0.05f);
	}

	GetWorldTimerManager().SetTimer(InvulnDashTimerHandle, this, &ADeathMetalCatCharacter::ClearInvulnDashState, EffectiveDashDuration, false);
	// Shared with Dodge/post-respawn invuln -- see their own comments on this same mechanism.
	GetWorldTimerManager().SetTimer(IFrameTimerHandle, this, &ADeathMetalCatCharacter::ClearInvincibility, EffectiveDashDuration, false);
}

void ADeathMetalCatCharacter::ClearInvulnDashState()
{
	bIsDashing = false;
}

void ADeathMetalCatCharacter::UpdateInvulnDash()
{
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		FVector Velocity = MoveComp->Velocity;
		Velocity.X = DashFacingSignAtStart * InvulnDashImpulseStrength;
		MoveComp->Velocity = Velocity;
	}
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
	return !bIsInvincible && !bIsBlocking;
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
	AddRage(ActualDamage, RageGainPerDamageTaken);

	TriggerQuip(EQuipTriggerType::Damage);

	// Hurt flipbook is skipped while bIsTransformed -- there's no Fancy-Cayde hurt art, and briefly
	// flashing back to solo Cayde's sprite mid-transformation would look like a bug rather than a
	// deliberate hit-reaction. Damage/GnarlyRank/Rage above all still apply normally either way --
	// per spec, no invulnerability during the ultimate, just no mismatched-art flash.
	if (UPaperFlipbookComponent* SpriteComp = GetSprite())
	{
		if (HurtFlipbook && !bIsTransformed)
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

	if (GnarlyRankHUDWidgetInstance)
	{
		GnarlyRankHUDWidgetInstance->ShowDeathScreen();
	}

	// No more auto-respawn timer -- push a separate InputComponent (bound only to EKeys::AnyKey)
	// onto the player controller's own input stack instead, so any button press advances past the
	// death screen. This is deliberately a SEPARATE component from PlayerInputComponent, which
	// DisableInput above just blocked -- PushInputComponent bypasses that block entirely, and
	// binding here rather than on the normal gameplay InputComponent means there's no risk of this
	// listener also re-enabling movement/combat input early. HandleDeathContinuePressed itself
	// gates on the death-screen fade-in having actually finished before calling HandleRespawn.
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		DeathContinueInputComponent = NewObject<UInputComponent>(this, TEXT("DeathContinueInputComponent"));
		DeathContinueInputComponent->RegisterComponent();
		DeathContinueInputComponent->BindKey(EKeys::AnyKey, IE_Pressed, this, &ADeathMetalCatCharacter::HandleDeathContinuePressed);
		PC->PushInputComponent(DeathContinueInputComponent);
	}
}

void ADeathMetalCatCharacter::HandleDeathContinuePressed()
{
	// Ignores the press entirely while the death-screen fade-in is still playing (both the
	// backdrop and image/text ramps -- see UGnarlyRankHUDWidget::UpdateDeathScreenFade), so
	// mashing a button the instant Health hits 0 can't skip or cut the animation short. Once the
	// fade has fully finished, the next press is what actually respawns.
	if (GnarlyRankHUDWidgetInstance && GnarlyRankHUDWidgetInstance->IsDeathScreenFadeInProgress())
	{
		return;
	}

	HandleRespawn();
}

void ADeathMetalCatCharacter::HandleRespawn()
{
	// Pop and destroy the continue-listener now that it's done its job -- it must not keep
	// listening (or keep existing at all) once the player has actually respawned.
	if (DeathContinueInputComponent)
	{
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			PC->PopInputComponent(DeathContinueInputComponent);
		}
		DeathContinueInputComponent->DestroyComponent();
		DeathContinueInputComponent = nullptr;
	}

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

	// Hidden here, before the room-transition fade-out even starts ramping below -- so "YOU DIED"
	// disappears cleanly rather than lingering (Collapsed widgets don't fade) behind the black pause
	// and then briefly showing through the reset room once BeginRoomTransition's fade-in completes.
	if (GnarlyRankHUDWidgetInstance)
	{
		GnarlyRankHUDWidgetInstance->HideDeathScreen();
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

void ADeathMetalCatCharacter::AddRage(float DamageAmount, float GainPerDamage)
{
	if (DamageAmount <= 0.f)
	{
		return;
	}
	RageMeter = FMath::Clamp(RageMeter + DamageAmount * GainPerDamage, 0.f, RageMax);
}

void ADeathMetalCatCharacter::HandleUltimateActivate(const FInputActionValue& Value)
{
	TryActivateUltimate();
}

bool ADeathMetalCatCharacter::TryActivateUltimate()
{
	if (!IsRageFull() || bIsTransformed || bIsFadingOutForUltimate)
	{
		// Only usable once Rage is full, and can't be retriggered mid-sequence or mid-ride.
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("[RAGE] Ultimate activated -- beginning transformation sequence"));

	SpawnRageBeamEffect();

	bIsFadingOutForUltimate = true;
	UltimateFadeOutStartTime = GetWorld()->GetTimeSeconds();
	return true;
}

void ADeathMetalCatCharacter::UpdateUltimateFadeOut(float DeltaSeconds)
{
	UPaperFlipbookComponent* SpriteComp = GetSprite();
	if (!SpriteComp)
	{
		return;
	}

	const float Elapsed = GetWorld()->GetTimeSeconds() - UltimateFadeOutStartTime;
	const float Alpha = (UltimateFadeOutDuration > 0.f) ? FMath::Clamp(Elapsed / UltimateFadeOutDuration, 0.f, 1.f) : 1.f;

	// Shrink toward 0 rather than an alpha fade -- see UltimateFadeOutDuration's doc comment for why
	// (MaskedUnlitSpriteMaterial is Masked, not Translucent, so alpha alone can't produce a smooth
	// fade). Sign of Scale.X is preserved so facing doesn't flip mid-shrink.
	const FVector CurrentScale = SpriteComp->GetRelativeScale3D();
	const float SignX = (CurrentScale.X < 0.f) ? -1.f : 1.f;
	const float Magnitude = FMath::Lerp(1.f, 0.f, Alpha);
	SpriteComp->SetRelativeScale3D(FVector(SignX * Magnitude, Magnitude, Magnitude));

	if (Alpha >= 1.f)
	{
		bIsFadingOutForUltimate = false;
		BeginUltimateTransformation();
	}
}

void ADeathMetalCatCharacter::BeginUltimateTransformation()
{
	bIsTransformed = true;

	if (UPaperFlipbookComponent* SpriteComp = GetSprite())
	{
		const float SignX = (SpriteComp->GetRelativeScale3D().X < 0.f) ? -1.f : 1.f;
		SpriteComp->SetRelativeScale3D(FVector(SignX, 1.f, 1.f));

		if (FancyIdleFlipbook)
		{
			SpriteComp->SetFlipbook(FancyIdleFlipbook);
			SpriteComp->SetLooping(true);
			SpriteComp->Play();
			CurrentFlipbook = FancyIdleFlipbook;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[RAGE] Transformed -- riding Fancy Pants for %.1fs"), UltimateDuration);
	GetWorldTimerManager().SetTimer(UltimateDurationTimerHandle, this, &ADeathMetalCatCharacter::EndUltimateTransformation, UltimateDuration, false);
}

void ADeathMetalCatCharacter::EndUltimateTransformation()
{
	// Instant swap back, not a mirrored fade -- explicitly allowed ("doesn't need to be elaborate")
	// and simpler: UpdateAnimation's normal Idle/Run selection takes over on the very next Tick
	// purely because bIsTransformed is now false, so there's nothing else to set here.
	bIsTransformed = false;
	RageMeter = 0.f;
	UE_LOG(LogTemp, Warning, TEXT("[RAGE] Ultimate ended -- reverted to normal Cayde, Rage reset to 0"));
}

void ADeathMetalCatCharacter::SpawnRageBeamEffect()
{
	if (!RageBeamMeshComponent)
	{
		RageBeamMeshComponent = NewObject<UStaticMeshComponent>(this, TEXT("RageBeamMesh"));
		RageBeamMeshComponent->SetupAttachment(RootComponent);
		RageBeamMeshComponent->RegisterComponent();
		RageBeamMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RageBeamMeshComponent->SetCastShadow(false);
		RageBeamMeshComponent->SetMobility(EComponentMobility::Movable);

		if (UStaticMesh* CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
		{
			RageBeamMeshComponent->SetStaticMesh(CylinderMesh);
		}

		if (UMaterialInterface* BeamMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Characters/DeathMetalCat/Materials/M_RainbowBeam.M_RainbowBeam")))
		{
			RageBeamMID = UMaterialInstanceDynamic::Create(BeamMaterial, this);
			RageBeamMeshComponent->SetMaterial(0, RageBeamMID);
		}
	}

	// Tall, thin cylinder starting well above Cayde -- UpdateRageBeamEffect drops it down and fades
	// it out over RageBeamLifetime, reading as "a beam of light drops from the sky" without needing
	// a full Niagara system (see class doc / summary report for why this simpler approach was used).
	RageBeamMeshComponent->SetRelativeLocation(FVector(0.f, 0.f, 800.f));
	RageBeamMeshComponent->SetRelativeScale3D(FVector(1.5f, 1.5f, 10.f));
	RageBeamMeshComponent->SetVisibility(true);

	bRageBeamActive = true;
	RageBeamStartTime = GetWorld()->GetTimeSeconds();
}

void ADeathMetalCatCharacter::UpdateRageBeamEffect(float DeltaSeconds)
{
	if (!bRageBeamActive || !RageBeamMeshComponent)
	{
		return;
	}

	const float Elapsed = GetWorld()->GetTimeSeconds() - RageBeamStartTime;
	const float Alpha = (RageBeamLifetime > 0.f) ? FMath::Clamp(Elapsed / RageBeamLifetime, 0.f, 1.f) : 1.f;

	if (RageBeamMID)
	{
		const float HueDegrees = FMath::Fmod(Elapsed * RageBeamHueCycleSpeed, 360.f);
		const FLinearColor CycledColor = FLinearColor::MakeFromHSV8(static_cast<uint8>((HueDegrees / 360.f) * 255.f), 255, 255);
		RageBeamMID->SetVectorParameterValue(TEXT("BeamColor"), CycledColor);
		RageBeamMID->SetScalarParameterValue(TEXT("BeamOpacity"), 1.f - Alpha * 0.5f);
	}

	constexpr float BeamStartHeight = 800.f;
	RageBeamMeshComponent->SetRelativeLocation(FVector(0.f, 0.f, FMath::Lerp(BeamStartHeight, 0.f, Alpha)));

	if (Alpha >= 1.f)
	{
		bRageBeamActive = false;
		RageBeamMeshComponent->SetVisibility(false);
	}
}

void ADeathMetalCatCharacter::SpawnFancyAttackBeam(const FVector& BeamStart, const FVector& BeamEnd)
{
	if (!FancyAttackBeamMeshComponent)
	{
		FancyAttackBeamMeshComponent = NewObject<UStaticMeshComponent>(this, TEXT("FancyAttackBeamMesh"));
		FancyAttackBeamMeshComponent->SetupAttachment(RootComponent);
		FancyAttackBeamMeshComponent->RegisterComponent();
		FancyAttackBeamMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FancyAttackBeamMeshComponent->SetCastShadow(false);
		FancyAttackBeamMeshComponent->SetMobility(EComponentMobility::Movable);

		if (UStaticMesh* CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
		{
			FancyAttackBeamMeshComponent->SetStaticMesh(CylinderMesh);
		}

		if (UMaterialInterface* BeamMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Characters/DeathMetalCat/Materials/M_RainbowBeam.M_RainbowBeam")))
		{
			FancyAttackBeamMID = UMaterialInstanceDynamic::Create(BeamMaterial, this);
			FancyAttackBeamMeshComponent->SetMaterial(0, FancyAttackBeamMID);
		}
	}

	// /Engine/BasicShapes/Cylinder spans +/-50uu on every axis at scale 1 -- stretch its own Z to
	// exactly span BeamStart..BeamEnd, then rotate that Z axis to point the right way. Reused (moved/
	// rescaled/re-oriented) every shot rather than respawned, same pattern as RageBeamMeshComponent.
	const FVector Delta = BeamEnd - BeamStart;
	const float Length = Delta.Size();
	const FVector Direction = (Length > KINDA_SMALL_NUMBER) ? Delta / Length : FVector::ForwardVector;
	const FVector Midpoint = (BeamStart + BeamEnd) * 0.5f;

	FancyAttackBeamMeshComponent->SetWorldLocation(Midpoint);
	FancyAttackBeamMeshComponent->SetWorldRotation(FRotationMatrix::MakeFromZ(Direction).Rotator());
	const float DiameterScale = (FancyAttackBeamThickness * 2.f) / 100.f;
	FancyAttackBeamMeshComponent->SetRelativeScale3D(FVector(DiameterScale, DiameterScale, Length / 100.f));
	FancyAttackBeamMeshComponent->SetVisibility(true);

	bFancyAttackBeamActive = true;
	FancyAttackBeamStartTime = GetWorld()->GetTimeSeconds();
}

void ADeathMetalCatCharacter::UpdateFancyAttackBeamEffect(float DeltaSeconds)
{
	if (!bFancyAttackBeamActive || !FancyAttackBeamMeshComponent)
	{
		return;
	}

	const float Elapsed = GetWorld()->GetTimeSeconds() - FancyAttackBeamStartTime;
	const float Alpha = (FancyAttackBeamLifetime > 0.f) ? FMath::Clamp(Elapsed / FancyAttackBeamLifetime, 0.f, 1.f) : 1.f;

	if (FancyAttackBeamMID)
	{
		const float HueDegrees = FMath::Fmod(Elapsed * FancyAttackBeamHueCycleSpeed, 360.f);
		const FLinearColor CycledColor = FLinearColor::MakeFromHSV8(static_cast<uint8>((HueDegrees / 360.f) * 255.f), 255, 255);
		FancyAttackBeamMID->SetVectorParameterValue(TEXT("BeamColor"), CycledColor);
		FancyAttackBeamMID->SetScalarParameterValue(TEXT("BeamOpacity"), 1.f - Alpha);
	}

	if (Alpha >= 1.f)
	{
		bFancyAttackBeamActive = false;
		FancyAttackBeamMeshComponent->SetVisibility(false);
	}
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

namespace
{
	// SwordHitbox's fixed vertical offset (root-local) while The Spinny Down Thing is active --
	// "a small area below Cayde", restored to 0 (forward positioning) in ClearAttackState.
	constexpr float SpinnyDownHitboxZOffset = -90.f;

	// Mirrors SwordHitbox to whichever side the character is currently facing -- shared by every
	// forward-facing sword variant (ground combo stages, Uppy, Double Whammy). Only the sign of the
	// offset changes -- the box's extent (shape/size) is symmetric either way. FacingSign matches
	// Scale.X's sign directly (Scale.X >= 0 -> facing right, FacingSign=+1; Scale.X < 0 -> facing
	// left, FacingSign=-1) -- confirmed correct via a direct visual cross-check (a debug arrow drawn
	// as a pure world-space offset, verified to point the same way the sprite visibly faces on
	// screen).
	//
	// BUT: SwordHitbox is parented to RootComponent, and its offset is applied via
	// SetRelativeLocation -- a ROOT-LOCAL offset, not a world-space one. That's only equivalent to a
	// world-space offset if the root/actor's own rotation is identity. The old verification of this
	// line only ever checked the logged RelativeLocation/WorldLocation numbers against the EXPECTED
	// sign convention (self-consistent, since the log and the convention comment were written by the
	// same assumption) -- it never cross-checked against an independently-confirmed visual
	// reference. The arrow test did exactly that and showed the hitbox landing on the opposite side
	// from the visibly-correct facing, which the root-local-vs-world-space distinction fully
	// explains: this character's placed/spawned actor rotation isn't identity, so root-local +X
	// isn't world +X here. Sign is inverted below to compensate. This is correct for THIS
	// character's current spawn rotation specifically, not a universal geometric law -- if the
	// spawn rotation ever changes, this may need re-flipping (or switching SwordHitbox to a
	// world-space SetWorldLocation offset, immune to root rotation, would remove this fragility
	// entirely).
	void PositionSwordHitboxForward(UBoxComponent* SwordHitbox, float FacingSign)
	{
		if (!SwordHitbox)
		{
			return;
		}
		FVector Loc = SwordHitbox->GetRelativeLocation();
		Loc.X = -FacingSign * FMath::Abs(Loc.X);
		Loc.Z = 0.f;
		SwordHitbox->SetRelativeLocation(Loc);
	}
}

void ADeathMetalCatCharacter::HandleSwordAttack(const FInputActionValue& Value)
{
	if (bIsDashing || bIsTransformed)
	{
		// Committed burst move -- don't interrupt it with an attack. Also disabled while riding
		// Fancy Pants -- no Fancy-Cayde sword art exists, and Gun Fire already covers the ultimate's
		// one attack (the laser) -- see HandleShootStarted.
		return;
	}

	if (bIsAttacking)
	{
		// Only ever buffers into the next ground-combo stage (see ClearAttackState) -- Uppy/Double
		// Whammy/Spinny Down are one-off moves, not chain participants, so a repress mid-swing on
		// those is simply ignored, same as the old single-swing behavior.
		if (bLastAttackWasComboEligible)
		{
			bComboInputBuffered = true;
		}
		return;
	}

	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	const bool bAirborne = MoveComp && MoveComp->IsFalling();

	if (bAirborne)
	{
		// Airborne branch only ever depends on whether Down is held -- "Down, or Forward+Down" per
		// spec, i.e. the horizontal axis doesn't matter, only Down's own held state.
		if (bIsHoldingDownInput)
		{
			StartSpinnyDown();
		}
		else
		{
			StartDoubleWhammy();
		}
		return;
	}

	UPaperFlipbookComponent* SpriteComp = GetSprite();
	const float FacingSign = (SpriteComp && SpriteComp->GetRelativeScale3D().X < 0.f) ? -1.f : 1.f;
	const bool bHoldingBack = FMath::Abs(LastMoveRightAxisValue) > KINDA_SMALL_NUMBER
		&& FMath::Sign(LastMoveRightAxisValue) != FacingSign;

	if (bHoldingBack)
	{
		StartUppy();
		return;
	}

	// Base ground combo: advance a stage if the buffered window is still open (a press that arrived
	// after the previous stage fully finished, within SwordComboBufferWindow -- see
	// ClearAttackState), otherwise a fresh press with no recent chain restarts at the base swing.
	GetWorldTimerManager().ClearTimer(SwordComboWindowTimerHandle);
	SwordComboIndex = bSwordComboWindowOpen ? (SwordComboIndex + 1) % 3 : 0;
	bSwordComboWindowOpen = false;
	StartSwordComboStage(SwordComboIndex);
}

void ADeathMetalCatCharacter::StartSwordComboStage(int32 StageIndex)
{
	bIsAttacking = true;
	bLastAttackWasComboEligible = true;
	bCurrentAttackLaunchesTarget = false;
	bIsSpinnyDownAttackActive = false;

	// 0=sword-v2 (base swing), 1=sword_2, 2=sword_3 -- no damage escalation per stage, see SwordBaseDamage.
	UPaperFlipbook* StageFlipbook = SwordAttackFlipbook;
	if (StageIndex == 1)
	{
		StageFlipbook = SwordCombo2Flipbook;
	}
	else if (StageIndex == 2)
	{
		StageFlipbook = SwordCombo3Flipbook;
	}

	if (UPaperFlipbookComponent* SpriteComp = GetSprite())
	{
		if (StageFlipbook)
		{
			SpriteComp->SetFlipbook(StageFlipbook);
			SpriteComp->SetLooping(false);
			SpriteComp->PlayFromStart();
			CurrentFlipbook = StageFlipbook;
		}

		const float FacingSign = (SpriteComp->GetRelativeScale3D().X < 0.f) ? -1.f : 1.f;
		PositionSwordHitboxForward(SwordHitbox, FacingSign);
	}

	// Same timing for all three stages -- the spec didn't call for per-stage tuning, and reusing
	// these keeps the combo's first pass low-risk; retune per-stage if playtesting calls for it.
	GetWorldTimerManager().SetTimer(SwordHitboxEnableTimerHandle, this, &ADeathMetalCatCharacter::EnableSwordHitbox, HitboxActiveDelay, false);
	GetWorldTimerManager().SetTimer(SwordAttackEndTimerHandle, this, &ADeathMetalCatCharacter::ClearAttackState, AttackDuration, false);
}

void ADeathMetalCatCharacter::StartUppy()
{
	bIsAttacking = true;
	bLastAttackWasComboEligible = false;
	bCurrentAttackLaunchesTarget = true;
	bIsSpinnyDownAttackActive = false;

	if (UPaperFlipbookComponent* SpriteComp = GetSprite())
	{
		if (UppyFlipbook)
		{
			SpriteComp->SetFlipbook(UppyFlipbook);
			SpriteComp->SetLooping(false);
			SpriteComp->PlayFromStart();
			CurrentFlipbook = UppyFlipbook;
		}

		const float FacingSign = (SpriteComp->GetRelativeScale3D().X < 0.f) ? -1.f : 1.f;
		PositionSwordHitboxForward(SwordHitbox, FacingSign);
	}

	GetWorldTimerManager().SetTimer(SwordHitboxEnableTimerHandle, this, &ADeathMetalCatCharacter::EnableSwordHitbox, HitboxActiveDelay, false);
	GetWorldTimerManager().SetTimer(SwordAttackEndTimerHandle, this, &ADeathMetalCatCharacter::ClearAttackState, AttackDuration, false);
}

void ADeathMetalCatCharacter::StartDoubleWhammy()
{
	bIsAttacking = true;
	bLastAttackWasComboEligible = false;
	bCurrentAttackLaunchesTarget = false;
	bIsSpinnyDownAttackActive = false;

	if (UPaperFlipbookComponent* SpriteComp = GetSprite())
	{
		if (DoubleWhammyFlipbook)
		{
			SpriteComp->SetFlipbook(DoubleWhammyFlipbook);
			SpriteComp->SetLooping(false);
			SpriteComp->PlayFromStart();
			CurrentFlipbook = DoubleWhammyFlipbook;
		}

		const float FacingSign = (SpriteComp->GetRelativeScale3D().X < 0.f) ? -1.f : 1.f;
		PositionSwordHitboxForward(SwordHitbox, FacingSign);
	}

	// Same base damage/hitbox timing as a grounded swing -- see SwordBaseDamage/HitboxActiveDelay/
	// HitboxActiveDuration/AttackDuration; nothing in testing so far suggested this needs its own
	// timing, but it's untested feel-wise (see summary report).
	GetWorldTimerManager().SetTimer(SwordHitboxEnableTimerHandle, this, &ADeathMetalCatCharacter::EnableSwordHitbox, HitboxActiveDelay, false);
	GetWorldTimerManager().SetTimer(SwordAttackEndTimerHandle, this, &ADeathMetalCatCharacter::ClearAttackState, AttackDuration, false);
}

void ADeathMetalCatCharacter::StartSpinnyDown()
{
	bIsAttacking = true;
	bLastAttackWasComboEligible = false;
	bCurrentAttackLaunchesTarget = false;
	bIsSpinnyDownAttackActive = true;

	if (UPaperFlipbookComponent* SpriteComp = GetSprite())
	{
		if (SpinnyDownFlipbook)
		{
			SpriteComp->SetFlipbook(SpinnyDownFlipbook);
			SpriteComp->SetLooping(false);
			SpriteComp->PlayFromStart();
			CurrentFlipbook = SpinnyDownFlipbook;
		}
	}

	if (SwordHitbox)
	{
		// Small area directly below Cayde, not mirrored by facing (below doesn't depend on facing).
		FVector Loc = SwordHitbox->GetRelativeLocation();
		Loc.X = 0.f;
		Loc.Z = SpinnyDownHitboxZOffset;
		SwordHitbox->SetRelativeLocation(Loc);
	}

	// Ground-pound-style fast forced descent for the duration of the attack -- see SpinnyDownFallSpeed.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		FVector Velocity = MoveComp->Velocity;
		Velocity.Z = -SpinnyDownFallSpeed;
		MoveComp->Velocity = Velocity;
	}

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

	if (bIsSpinnyDownAttackActive)
	{
		bIsSpinnyDownAttackActive = false;
		if (SwordHitbox)
		{
			FVector Loc = SwordHitbox->GetRelativeLocation();
			Loc.Z = 0.f;
			SwordHitbox->SetRelativeLocation(Loc);
		}
	}

	if (!bLastAttackWasComboEligible)
	{
		// Uppy / Double Whammy / Spinny Down are one-off moves -- never open the ground-combo window.
		return;
	}

	if (bComboInputBuffered)
	{
		// Pressed again during recovery frames -- chain immediately into the next stage rather than
		// waiting for the buffered window (which is for a press AFTER the attack fully finishes).
		bComboInputBuffered = false;
		SwordComboIndex = (SwordComboIndex + 1) % 3;
		StartSwordComboStage(SwordComboIndex);
		return;
	}

	bSwordComboWindowOpen = true;
	GetWorldTimerManager().SetTimer(SwordComboWindowTimerHandle, this, &ADeathMetalCatCharacter::ResetSwordComboWindow, SwordComboBufferWindow, false);
}

void ADeathMetalCatCharacter::ResetSwordComboWindow()
{
	bSwordComboWindowOpen = false;
	SwordComboIndex = 0;
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

	// Restrict melee damage to actual enemies -- without this, the hitbox (OverlapAllDynamic)
	// happily overlaps anything with collision enabled, including solid background geometry like
	// Structure_ROOM1_00_LeftStructure/RightStructure. AActor's default TakeDamage() just echoes
	// the damage amount back with no health system, so those would otherwise still show a floating
	// damage number and register a Gnarly hit on every swing that merely swept through them.
	if (!OtherActor->IsA<ADeathMetalCatEnemyBase>())
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
		AddRage(DamageApplied, RageGainPerDamageDealt);

		// Uppy-only: launches the hit enemy upward on top of normal damage -- see UppyLaunchVelocityZ.
		if (bCurrentAttackLaunchesTarget)
		{
			if (ACharacter* HitCharacter = Cast<ACharacter>(OtherActor))
			{
				HitCharacter->LaunchCharacter(FVector(0.f, 0.f, UppyLaunchVelocityZ), false, true);
			}
		}
	}
}

void ADeathMetalCatCharacter::HandleShootStarted(const FInputActionValue& Value)
{
	// Gun Fire is repurposed into the ultimate's continuous attack for the duration of the ride --
	// bIsTransformed is handled entirely inside BeginHoldFireLoop/UpdateHoldFireFlipbook/
	// FireShotTrace from here on, the exact same hold-to-fire path as the regular gun (no separate
	// single-press case anymore).
	bIsHoldingShootButton = true;

	// Defensive: don't trust that HandleShootReleased always cleaned up properly. Rapid
	// press/release timing (or a release racing a movement input) is exactly where that
	// assumption broke before -- ShootAnimPhase could be left stuck non-None from a prior press,
	// silently blocking every future BeginHoldFireLoop() forever. Force a clean slate before
	// proceeding whenever we find state left over, rather than only handling the case we expect.
	if (ShootAnimPhase != EShootPhase::None)
	{
		ResetShootState();
	}

	BeginHoldFireLoop();
}

void ADeathMetalCatCharacter::HandleShootHeld(const FInputActionValue& Value)
{
	// Triggered fires every tick while held, including the same tick as Started -- the bIsShooting
	// check (still on cooldown from the first shot BeginHoldFireLoop() just fired) is what skips
	// that redundant same-tick call, not ShootAnimPhase (which is already HoldFiring by this point
	// now that Started transitions straight into it with no separate draw phase). The first shot of
	// a hold fires from BeginHoldFireLoop() itself, not from here.
	if (!bIsHoldingShootButton || ShootAnimPhase != EShootPhase::HoldFiring || bIsShooting)
	{
		return;
	}

	FireShotTrace();
}

void ADeathMetalCatCharacter::HandleShootReleased(const FInputActionValue& Value)
{
	bIsHoldingShootButton = false;

	// Unconditional cleanup regardless of which phase we were in when released -- an earlier
	// version only reset ShootAnimPhase for a since-removed case, on the assumption the in-flight
	// sequence would always finish naturally and clean up after itself; a fire call that bailed
	// early on its own bIsShooting guard (which can happen on rapid taps, since a prior shot's
	// cooldown may still be active) left nothing to un-stick ShootAnimPhase otherwise, and it
	// stayed stuck non-None indefinitely -- a real freeze bug, confirmed via diagnostic logging at
	// the time (since removed along with the draw phase it was investigating).
	ResetShootState();
}

void ADeathMetalCatCharacter::ResetShootState()
{
	GetWorldTimerManager().ClearTimer(ShootTimerHandle);
	ShootAnimPhase = EShootPhase::None;
	bIsShooting = false;
}

void ADeathMetalCatCharacter::BeginHoldFireLoop()
{
	ShootAnimPhase = EShootPhase::HoldFiring;

	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	const bool bAirborne = MoveComp && MoveComp->IsFalling();
	UpdateHoldFireFlipbook(bAirborne, bAirborne && bIsHoldingDownInput);

	FireShotTrace();
}

void ADeathMetalCatCharacter::UpdateHoldFireFlipbook(bool bAirborne, bool bAngled)
{
	UPaperFlipbookComponent* SpriteComp = GetSprite();
	if (!SpriteComp)
	{
		return;
	}

	if (bIsTransformed)
	{
		// Riding Fancy Pants: the ultimate's attack always shows FancyAttackFlipbook regardless of
		// grounded/airborne/angled state -- those sub-variants have no Fancy-Cayde equivalent art.
		// Looping, same as the regular HoldFireFlipbook case below, since this is now a genuine
		// continuous hold-fire (see HandleShootHeld) rather than the old single-press one-shot.
		if (FancyAttackFlipbook && CurrentFlipbook != FancyAttackFlipbook)
		{
			SpriteComp->SetFlipbook(FancyAttackFlipbook);
			SpriteComp->SetLooping(true);
			SpriteComp->PlayFromStart();
			CurrentFlipbook = FancyAttackFlipbook;
		}
		return;
	}

	// Engine-driven looping playback, not manual SetPlaybackPositionInFrames jumping: all three
	// rows' frames already cycle through their own variations (muzzle-flash / down-shot) while
	// holding a consistent pose, so there's no per-frame role for code to pick between anymore --
	// just let whichever one applies play.
	UPaperFlipbook* DesiredFlipbook = !bAirborne ? HoldFireFlipbook : (bAngled ? AirShotAngledFlipbook : AirDownShotFlipbook);
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
	// AirDownShotFlipbook/AirShotAngledFlipbook are kept in sync with this same check via
	// UpdateHoldFireFlipbook.
	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	const bool bAirborneShot = MoveComp && MoveComp->IsFalling();
	const bool bAngledShot = bAirborneShot && bIsHoldingDownInput;
	UpdateHoldFireFlipbook(bAirborneShot, bAngledShot);

	if (bAirborneShot)
	{
		// Independent of FireDirection/animation below -- this only ever touches GravityScale, so
		// it can't fight with either airborne trajectory or flipbook choice.
		ApplyAirFireFloat();
	}

	// Airborne + holding Down (or Forward+Down): fixed 45 degrees downward, horizontal component
	// still respecting facing (an aim-down-forward shot, not a pure vertical drop). Airborne without
	// Down, same as grounded: pure horizontal -- previously EVERY airborne shot used the fixed
	// 45-degree angle; see AirShotAngledFlipbook's doc comment for why the default moved to
	// horizontal once this variant needed to be visually/mechanically distinct from it.
	const FVector FireDirection = bAngledShot
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

	// Restrict gun damage to actual enemies -- same missing-filter bug as OnSwordHitboxBeginOverlap
	// had: ECC_Visibility is blocked by solid background geometry (Structure_ROOM1_00_LeftStructure/
	// RightStructure), and AActor's default TakeDamage() echoes the damage amount back with no
	// health system, so those would otherwise still show a floating damage number on every shot
	// that swept into them (most visible on the airborne down-shot, which aims into the ground/
	// structures rather than level).
	if (bHit && Hit.GetActor() && Hit.GetActor()->IsA<ADeathMetalCatEnemyBase>())
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
			AddRage(DamageApplied, RageGainPerDamageDealt);
		}
	}
	else if (bHit && Hit.GetActor())
	{
		UE_LOG(LogTemp, Warning, TEXT("Gun fire hit non-enemy actor: %s -- no damage applied"), *Hit.GetActor()->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Gun fire hit nothing (traced full range %f)"), MaxTraceRange);
	}

	if (bIsTransformed)
	{
		// Reuses this exact same shot's Start/FacingSign/End/Hit -- the laser always visually
		// matches exactly where the shot really went, including landing short on an actual hit
		// rather than always reaching MaxTraceRange.
		const FVector BeamOrigin = Start + FVector(0.f, 0.f, FancyAttackBeamEyeHeight) + FVector(FacingSign * FancyAttackBeamForwardOffset, 0.f, 0.f);
		const FVector BeamTarget = bHit ? Hit.Location : End;
		SpawnFancyAttackBeam(BeamOrigin, BeamTarget);
	}

	bIsShooting = true;
	GetWorldTimerManager().SetTimer(ShootTimerHandle, this, &ADeathMetalCatCharacter::ClearShootingState, FireCooldown, false);
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

	if (bIsDashing)
	{
		UpdateInvulnDash();
	}

	if (bIsFadingOutForUltimate)
	{
		UpdateUltimateFadeOut(DeltaSeconds);
	}

	if (bRageBeamActive)
	{
		UpdateRageBeamEffect(DeltaSeconds);
	}

	if (bFancyAttackBeamActive)
	{
		UpdateFancyAttackBeamEffect(DeltaSeconds);
	}

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
		// HoldFiring manages its own flipbook state (engine-driven looping playback, started once
		// in BeginHoldFireLoop) -- nothing to do here except make sure nothing else stomps it while
		// a shoot sequence is in progress. Note this checks ShootAnimPhase, not bIsShooting:
		// bIsShooting is only the short per-shot fire-rate gate, not the (much longer) whole-hold
		// animation window.
	}
	else if (bIsDodging)
	{
		// The 5-frame handspring sequence is fully event-driven (HandleDodge / AdvanceDodgeFrame
		// via SetDodgeFrame), not tick-based -- nothing to do here except make sure nothing else
		// stomps it while a dodge is in progress.
	}
	else if (bIsDashing)
	{
		// InvulnDashFlipbook is started once (non-looping, after-image baked into its own frames) in
		// HandleInvulnDash itself -- nothing to switch here.
	}
	else if (bIsBlocking)
	{
		if (BlockFlipbook && CurrentFlipbook != BlockFlipbook)
		{
			SpriteComp->SetFlipbook(BlockFlipbook);
			SpriteComp->SetLooping(true);
			SpriteComp->Play();
			CurrentFlipbook = BlockFlipbook;
		}
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
	else if (bAirborne && !bIsTransformed)
	{
		// FB_DeathMetalCat_Jump's two frames (rising pose, landing pose) are picked explicitly
		// by velocity direction instead of left to play on the flipbook's own timer: how long
		// the rising pose should hold depends on jump height/gravity, which varies per jump --
		// a fixed per-keyframe hold on the asset can't express that, so this has to be code-driven.
		// Skipped entirely while bIsTransformed -- there's no Fancy-Cayde jump art, so airborne
		// while riding Fancy Pants falls through to the idle/gallop branch below instead (which
		// picks FancyIdleFlipbook), rather than flashing solo Cayde's jump pose mid-transformation.
		// Jumping itself is NOT disabled during the ultimate, only this specific visual.
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
		// Walk removed as a separate state -- it briefly flashed on-screen right at movement start
		// before Run took over, and served no purpose once Run covers slow-to-fast movement fine on
		// its own. Any nonzero horizontal speed goes straight to Run, no threshold. While
		// bIsTransformed, Idle/Run (and the airborne fallback above) are replaced by
		// FancyIdle/FancyGallop -- the "riding Fancy Pants" ultimate moveset.
		UPaperFlipbook* DesiredFlipbook = bIsTransformed ? FancyIdleFlipbook : IdleFlipbook;
		const float CurrentMoveSpeed = FMath::Abs(Velocity.X);
		if (CurrentMoveSpeed > KINDA_SMALL_NUMBER)
		{
			DesiredFlipbook = bIsTransformed ? FancyGallopFlipbook : RunFlipbook;
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
	else if (bIsDashing)
	{
		// Same reasoning as Dodge above -- the dash's velocity points toward facing (forward, not
		// backward), but locking it still avoids any mid-burst flip from a stray velocity blip.
		FVector Scale = SpriteComp->GetRelativeScale3D();
		Scale.X = DashFacingSignAtStart * FMath::Abs(Scale.X);
		SpriteComp->SetRelativeScale3D(Scale);
	}
	else if (bIsBlocking)
	{
		// Velocity.X is ~0 while blocking (movement is locked out in HandleMoveRight), so the
		// velocity-based flip below would have nothing reliable to go on -- hold whatever facing was
		// captured the instant Block was pressed, same pattern as Dodge/Dash/WallSlide.
		FVector Scale = SpriteComp->GetRelativeScale3D();
		Scale.X = BlockFacingSignAtStart * FMath::Abs(Scale.X);
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

	// CurrentFlipbook is authoritative for the frame by this point -- every branch above either set
	// it directly or deliberately left it alone (already showing the right thing).
	ApplyFeetOffsetCorrection();
}

void ADeathMetalCatCharacter::SetFeetOffsetCorrection(UPaperFlipbook* Flipbook, float ZOffsetCorrection)
{
	if (!Flipbook)
	{
		return;
	}
	FlipbookFeetOffsetCorrections.Add(Flipbook, ZOffsetCorrection);
}

void ADeathMetalCatCharacter::PopulateFeetOffsetCorrections()
{
	// Measured directly from each flipbook's own source art (lowest non-transparent pixel row of a
	// representative frame, converted to world units via that flipbook's own PixelsPerUnrealUnit,
	// then compared against Idle's own measurement as the baseline -- see FlipbookFeetOffsetCorrections'
	// own doc comment). Populated here in BeginPlay, keyed off the already-assigned flipbook
	// UPROPERTYs, rather than via SetFeetOffsetCorrection from editor/Python tooling: a Blueprint
	// CDO's TMap<UObject*, float> mutated through a UFUNCTION call from Python was confirmed (live,
	// this session) to NOT propagate to freshly spawned/PIE'd instances, even though the exact same
	// CDO mutation is immediately visible when queried back on that same CDO object -- a plain
	// EditDefaultsOnly float set the same way DOES propagate correctly, so the gap is specific to
	// this TMap. Rather than chase that further, every instance now builds its own table fresh in
	// BeginPlay from literal measured constants, matching this file's existing "Placeholder value,
	// tune freely" convention for every other tuned constant. SetFeetOffsetCorrection is left in
	// place as a BlueprintCallable override for ad-hoc live tuning on an already-spawned instance.
	if (RunFlipbook) FlipbookFeetOffsetCorrections.Add(RunFlipbook, 13.826f);
	if (JumpFlipbook) FlipbookFeetOffsetCorrections.Add(JumpFlipbook, -12.5f);
	if (DodgeFlipbook) FlipbookFeetOffsetCorrections.Add(DodgeFlipbook, 2.5f);
	if (HoldFireFlipbook) FlipbookFeetOffsetCorrections.Add(HoldFireFlipbook, 0.5f);
	if (AirDownShotFlipbook) FlipbookFeetOffsetCorrections.Add(AirDownShotFlipbook, -14.0f);
	if (AirShotAngledFlipbook) FlipbookFeetOffsetCorrections.Add(AirShotAngledFlipbook, -5.0f);
	if (SwordAttackFlipbook) FlipbookFeetOffsetCorrections.Add(SwordAttackFlipbook, 4.784f);
	if (SwordCombo2Flipbook) FlipbookFeetOffsetCorrections.Add(SwordCombo2Flipbook, -7.354f);
	if (SwordCombo3Flipbook) FlipbookFeetOffsetCorrections.Add(SwordCombo3Flipbook, -5.0f);
	if (UppyFlipbook) FlipbookFeetOffsetCorrections.Add(UppyFlipbook, -8.0f);
	if (DoubleWhammyFlipbook) FlipbookFeetOffsetCorrections.Add(DoubleWhammyFlipbook, -20.5f);
	if (SpinnyDownFlipbook) FlipbookFeetOffsetCorrections.Add(SpinnyDownFlipbook, -33.024f);
	if (BlockFlipbook) FlipbookFeetOffsetCorrections.Add(BlockFlipbook, 9.5f);
	if (InvulnDashFlipbook) FlipbookFeetOffsetCorrections.Add(InvulnDashFlipbook, 3.5f);
	if (WallSlideFlipbook) FlipbookFeetOffsetCorrections.Add(WallSlideFlipbook, -16.941f);
	if (HurtFlipbook) FlipbookFeetOffsetCorrections.Add(HurtFlipbook, -0.5f);

	// Fancy Pants (riding the mount) flipbooks -- an entirely separate sprite sheet/character from
	// solo Cayde, so these are measured against FancyIdleFlipbook as their own baseline (0, no
	// entry needed), not against IdleFlipbook above. Same lowest-non-transparent-pixel-row
	// methodology, alpha>=128 threshold (a faint anti-aliasing/shadow tail below the visually solid
	// hoof was confirmed on this art at the alpha>0 threshold used for solo Cayde).
	if (FancyGallopFlipbook) FlipbookFeetOffsetCorrections.Add(FancyGallopFlipbook, -9.0f);
	if (FancyAttackFlipbook) FlipbookFeetOffsetCorrections.Add(FancyAttackFlipbook, 16.0f);
}

void ADeathMetalCatCharacter::Debug_ForceFlipbookForFeetTest(UPaperFlipbook* Flipbook)
{
	if (!Flipbook)
	{
		return;
	}
	CurrentFlipbook = Flipbook;
	if (UPaperFlipbookComponent* SpriteComp = GetSprite())
	{
		SpriteComp->SetFlipbook(Flipbook);
	}
	ApplyFeetOffsetCorrection();
}

void ADeathMetalCatCharacter::Debug_SetHoldFireForTest(bool bStart)
{
	if (bStart)
	{
		bIsHoldingShootButton = true;
		if (ShootAnimPhase != EShootPhase::None)
		{
			ResetShootState();
		}
		BeginHoldFireLoop();
	}
	else
	{
		bIsHoldingShootButton = false;
		ResetShootState();
	}
}

void ADeathMetalCatCharacter::ApplyFeetOffsetCorrection()
{
	UPaperFlipbookComponent* SpriteComp = GetSprite();
	if (!SpriteComp)
	{
		return;
	}

	const float* Correction = FlipbookFeetOffsetCorrections.Find(CurrentFlipbook);
	const float TargetZ = BaseSpriteRelativeLocation.Z + (Correction ? *Correction : 0.f);

	FVector Loc = SpriteComp->GetRelativeLocation();
	if (!FMath::IsNearlyEqual(Loc.Z, TargetZ))
	{
		Loc.Z = TargetZ;
		SpriteComp->SetRelativeLocation(Loc);
	}
}
