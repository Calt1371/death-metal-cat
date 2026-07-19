#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "DeathMetalCatEnemyBase.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * Base enemy class: Character-derived (capsule collision + movement component, for whatever
 * future AI/pathfinding work needs them) but with no skeletal mesh or animation -- real enemy
 * art is a separate future task. Stands in for now with a plain colored placeholder mesh that
 * flashes color on hit and destroys the actor outright at 0 health (no death animation yet,
 * also a future task).
 */
UCLASS()
class PYTHONTEST_API ADeathMetalCatEnemyBase : public ACharacter
{
	GENERATED_BODY()

public:
	ADeathMetalCatEnemyBase();

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0"))
	float MaxHealth = 100.f;

	UPROPERTY(BlueprintReadOnly, Category = "Health")
	float Health = 100.f;

protected:
	virtual void BeginPlay() override;

	/** Timer callback: reverts the placeholder mesh's color from HitFlashColor back to BaseColor. */
	void ClearHitFlash();

	/** Placeholder visual (an engine basic-shape mesh) standing in for real enemy art. Scaled to roughly fill the default ACharacter capsule; retune if capsule size changes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> PlaceholderMesh;

	/** Material assigned to PlaceholderMesh; must expose a "Color" vector parameter (see M_EnemyPlaceholder) for the hit-flash and BaseColor to have any visible effect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<UMaterialInterface> PlaceholderMaterial;

	/** Resting (non-flashed) tint of the placeholder mesh. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual")
	FLinearColor BaseColor = FLinearColor(0.6f, 0.1f, 0.1f); // dull red, reads as "hostile" at rest

	/** Color the placeholder mesh flashes to for HitFlashDuration after taking damage. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual")
	FLinearColor HitFlashColor = FLinearColor::White;

	/** How long the hit-flash color lasts, in seconds. Placeholder value, tune freely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Visual", meta = (ClampMin = "0"))
	float HitFlashDuration = 0.15f;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

	FTimerHandle HitFlashTimerHandle;
};
