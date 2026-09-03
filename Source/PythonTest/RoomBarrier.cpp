#include "RoomBarrier.h"

#include "Components/BoxComponent.h"
#include "PaperFlipbookComponent.h"
#include "PaperFlipbook.h"
#include "RoomShell.h"

ARoomBarrier::ARoomBarrier()
{
	PrimaryActorTick.bCanEverTick = true;

	// Same box-volume-at-the-doorway sizing as ARoomExitTrigger's own TriggerVolume (50,200,150) --
	// this barrier needs to physically span the same opening, just with real blocking collision
	// instead of overlap-only. Tune per-placement if a doorway needs a different size.
	BlockingVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("BlockingVolume"));
	RootComponent = BlockingVolume;
	BlockingVolume->SetBoxExtent(FVector(50.f, 200.f, 150.f));
	BlockingVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BlockingVolume->SetCollisionProfileName(TEXT("BlockAll"));

	FlipbookComponent = CreateDefaultSubobject<UPaperFlipbookComponent>(TEXT("FlipbookComponent"));
	FlipbookComponent->SetupAttachment(RootComponent);
	FlipbookComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ARoomBarrier::BeginPlay()
{
	Super::BeginPlay();

	OwningRoomShell = Cast<ARoomShell>(GetAttachParentActor());
	if (!OwningRoomShell)
	{
		UE_LOG(LogTemp, Error, TEXT("[ROOM BARRIER] %s is not attached to a RoomShell -- it will never block anything. Attach it to its room's RoomShell in the World Outliner."), *GetName());
	}

	FlipbookComponent->SetSpriteColor(BarrierTintColor);

	// Placed instances of this Blueprint have twice now been found with BarrierFlipbook reading
	// null in the level despite the Blueprint's own class defaults being correctly set (confirmed
	// via a direct CDO query both times) -- an editor-session-specific staleness this project has
	// hit more than once tonight (the same class of issue also bit BarrierFlipbook's original
	// import). Rather than leave a silently blank/invisible barrier (collision would still block
	// the player with nothing visible to explain why -- confirmed exactly this symptom in Room2),
	// fall back to loading the one real barrier flipbook directly by path so this can't silently
	// recur in whichever room this is placed in next.
	if (!BarrierFlipbook)
	{
		UE_LOG(LogTemp, Error, TEXT("[ROOM BARRIER] %s has a null BarrierFlipbook -- falling back to loading FB_Trap_RoomBarrier directly. Re-dragging a fresh instance from BP_RoomBarrier is the real fix; this is a safety net, not a substitute for that."), *GetName());
		BarrierFlipbook = LoadObject<UPaperFlipbook>(nullptr, TEXT("/Game/Environments/CityBiome/Traps/Flipbooks/FB_Trap_RoomBarrier.FB_Trap_RoomBarrier"));
	}

	if (BarrierFlipbook)
	{
		FlipbookComponent->SetFlipbook(BarrierFlipbook);
		FlipbookComponent->SetLooping(true);
		FlipbookComponent->Play();
	}

	// Evaluated immediately rather than waiting for the first Tick -- a closed barrier shouldn't
	// render visible-but-uncollided (or vice versa) for even one frame after spawning.
	RefreshBlockingState();
}

void ARoomBarrier::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	RefreshBlockingState();
}

void ARoomBarrier::RefreshBlockingState()
{
	const bool bShouldBlock = ARoomShell::IsRoomBarrierGateEnabled() && OwningRoomShell && !OwningRoomShell->IsRoomCleared();

	BlockingVolume->SetCollisionEnabled(bShouldBlock ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

	// No distinct "opening" animation exists in the source sheet (see class doc) -- hide/show
	// outright rather than trying to animate toward a frame that isn't there.
	SetActorHiddenInGame(!bShouldBlock);
}
