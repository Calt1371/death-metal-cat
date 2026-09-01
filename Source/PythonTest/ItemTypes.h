#pragma once

#include "CoreMinimal.h"
#include "ItemTypes.generated.h"

/**
 * What an item crate resolves to on pickup -- Scraps (currency) or one of the special items.
 * Shared between AItemPickupCrate (which rolls the weighted drop table) and
 * ADeathMetalCatCharacter::ResolvePickup (which actually applies the effect), so a crate only
 * ever needs to know this one enum plus that one entry point.
 */
UENUM(BlueprintType)
enum class EPickupResultType : uint8
{
	Scraps,
	ScratchPatch,
	NineLifeStim,
	DeathDefier,
	RazorFang,
	DeadshotRounds,
	SteelFur,
	FancyFeed,
	GnarlyAmp,
	MirrorClaw,
	CatNip,
};
