#pragma once

#include "CoreMinimal.h"
#include "DamageTypes.generated.h"

/**
 * Which tier a damage roll landed on -- drives both the damage multiplier applied (see
 * ADeathMetalCatCharacter::RollDamage) and the floating damage number's color (see
 * ADamageNumberActor::InitDamageNumber). BlueprintType since the eventual floating-number
 * Blueprint/UMG work may want to branch on it directly.
 */
UENUM(BlueprintType)
enum class EDamageTier : uint8
{
	Normal,
	Weakness,
	Critical,
};
