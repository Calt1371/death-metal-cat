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
#include "DrawDebugHelpers.h"

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

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->SetPlaneConstraintOrigin(GetActorLocation());
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
	}
}

void ADeathMetalCatCharacter::HandleMoveRight(const FInputActionValue& Value)
{
	const float AxisValue = Value.Get<float>();
	AddMovementInput(FVector(1.f, 0.f, 0.f), AxisValue);
}

void ADeathMetalCatCharacter::HandleJump(const FInputActionValue& Value)
{
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

	// bXYOverride = true: replace X/Y velocity outright for a consistent burst regardless of
	// current speed. bZOverride = false: don't stomp vertical velocity, so dodging mid-jump/fall
	// doesn't cancel gravity's effect on Z.
	LaunchCharacter(FVector(FacingSign * DodgeImpulseStrength, 0.f, 0.f), true, false);

	// Real dedicated dodge art now exists (was a JumpFlipbook placeholder) -- played once,
	// same treatment as SwordAttackFlipbook, rather than the old placeholder's looping playback.
	if (SpriteComp && DodgeFlipbook)
	{
		SpriteComp->SetFlipbook(DodgeFlipbook);
		SpriteComp->SetLooping(false);
		SpriteComp->PlayFromStart();
		CurrentFlipbook = DodgeFlipbook;
	}

	bIsDodging = true;
	bIsInvincible = true;

	GetWorldTimerManager().SetTimer(DodgeTimerHandle, this, &ADeathMetalCatCharacter::ClearDodgeState, DodgeDuration, false);
	GetWorldTimerManager().SetTimer(IFrameTimerHandle, this, &ADeathMetalCatCharacter::ClearInvincibility, IFrameDuration, false);
}

void ADeathMetalCatCharacter::ClearDodgeState()
{
	bIsDodging = false;
}

void ADeathMetalCatCharacter::ClearInvincibility()
{
	bIsInvincible = false;
}

bool ADeathMetalCatCharacter::CanTakeDamage() const
{
	return !bIsInvincible;
}

void ADeathMetalCatCharacter::HandleSwordAttack(const FInputActionValue& Value)
{
	if (bIsAttacking)
	{
		// Ignore re-triggers while already mid-swing rather than restarting/stacking timers.
		return;
	}

	// TEMP diagnostic: lets EnableSwordHitbox log real elapsed delay vs the configured HitboxActiveDelay.
	SwordAttackStartTime = GetWorld()->GetTimeSeconds();

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
		// FacingSign matches Scale.X's sign directly: unflipped (Scale.X >= 0, facing right)
		// puts the hitbox at positive X (screen-right, in front); flipped (Scale.X < 0, facing
		// left) puts it at negative X (screen-left, in front). Confirmed via logged data across
		// both facings and both moving/idle -- see git history for the debugging trail.
		if (SwordHitbox)
		{
			const float CurrentScaleX = SpriteComp->GetRelativeScale3D().X;
			const float FacingSign = (CurrentScaleX < 0.f) ? -1.f : 1.f;
			FVector Loc = SwordHitbox->GetRelativeLocation();
			Loc.X = FacingSign * FMath::Abs(Loc.X);
			SwordHitbox->SetRelativeLocation(Loc);

			UE_LOG(LogTemp, Warning, TEXT("[SWING START] Velocity.X=%f  Scale.X(read)=%f  FacingSign=%f  Hitbox.RelativeLocation=%s"),
				GetVelocity().X, CurrentScaleX, FacingSign, *SwordHitbox->GetRelativeLocation().ToString());
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

		// TEMP diagnostic: real elapsed delay (should be ~= HitboxActiveDelay) and Scale.X read
		// again here, to compare against the [SWING START] log -- if these Scale.X values differ,
		// something changed facing between swing start and hitbox-enable.
		const float Elapsed = GetWorld()->GetTimeSeconds() - SwordAttackStartTime;
		const float ScaleXNow = GetSprite() ? GetSprite()->GetRelativeScale3D().X : 0.f;
		UE_LOG(LogTemp, Warning, TEXT("[HITBOX ENABLE] Elapsed=%f (configured HitboxActiveDelay=%f)  Scale.X(read)=%f  Hitbox.RelativeLocation=%s  Hitbox.WorldLocation=%s"),
			Elapsed, HitboxActiveDelay, ScaleXNow, *SwordHitbox->GetRelativeLocation().ToString(), *SwordHitbox->GetComponentLocation().ToString());

		// TEMP visualization only -- 2s lifetime has nothing to do with the real HitboxActiveDuration
		// collision window, it's just long enough to actually see. Drawn here (not at swing start)
		// so what you see is the box at its real activation moment, not its position at click-time.
		DrawDebugBox(GetWorld(), SwordHitbox->GetComponentLocation(), SwordHitbox->GetScaledBoxExtent(),
			SwordHitbox->GetComponentQuat(), FColor::Green, false, 2.0f, 0, 3.0f);
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

	UE_LOG(LogTemp, Warning, TEXT("Sword hitbox overlapped actor: %s (component: %s)"), *OtherActor->GetName(), OtherComp ? *OtherComp->GetName() : TEXT("<none>"));

	// TODO: once a damage system exists, this is where it gets called -- e.g.
	// UGameplayStatics::ApplyDamage(OtherActor, DamageAmount, GetController(), this, DamageType);
}

void ADeathMetalCatCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateAnimation();
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

	if (bIsAttacking)
	{
		// SwordAttackFlipbook is started once (non-looping) in HandleSwordAttack itself, so there's
		// nothing to switch here -- just make sure nothing else stomps it while attacking.
	}
	else if (bIsDodging)
	{
		// DodgeFlipbook is started once (non-looping) in HandleDodge itself, so there's nothing
		// to switch here -- just make sure nothing else stomps it while dodging.
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
		const float Speed = FMath::Abs(Velocity.X);
		if (Speed > KINDA_SMALL_NUMBER)
		{
			DesiredFlipbook = (Speed >= WalkSpeedThreshold) ? RunFlipbook : WalkFlipbook;
		}

		if (DesiredFlipbook && DesiredFlipbook != CurrentFlipbook)
		{
			SpriteComp->SetFlipbook(DesiredFlipbook);
			SpriteComp->SetLooping(true);
			SpriteComp->Play();
			CurrentFlipbook = DesiredFlipbook;
		}
	}

	if (FMath::Abs(Velocity.X) > KINDA_SMALL_NUMBER)
	{
		FVector Scale = SpriteComp->GetRelativeScale3D();
		Scale.X = (Velocity.X < 0.f) ? -FMath::Abs(Scale.X) : FMath::Abs(Scale.X);
		SpriteComp->SetRelativeScale3D(Scale);
	}
}
