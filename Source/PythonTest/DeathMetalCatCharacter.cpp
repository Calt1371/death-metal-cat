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

	const UPaperFlipbookComponent* SpriteComp = GetSprite();
	const float FacingSign = (SpriteComp && SpriteComp->GetRelativeScale3D().X < 0.f) ? -1.f : 1.f;

	// bXYOverride = true: replace X/Y velocity outright for a consistent burst regardless of
	// current speed. bZOverride = false: don't stomp vertical velocity, so dodging mid-jump/fall
	// doesn't cancel gravity's effect on Z.
	LaunchCharacter(FVector(FacingSign * DodgeImpulseStrength, 0.f, 0.f), true, false);

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

	if (bIsDodging)
	{
		// No dedicated dodge art yet -- FB_DeathMetalCat_Jump is reused as a placeholder so
		// there's at least a distinct "something is happening" visual during the dodge window.
		// Played normally (looping), unlike the airborne case below which pins specific frames.
		if (JumpFlipbook && CurrentFlipbook != JumpFlipbook)
		{
			SpriteComp->SetFlipbook(JumpFlipbook);
			SpriteComp->SetLooping(true);
			SpriteComp->Play();
			CurrentFlipbook = JumpFlipbook;
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
