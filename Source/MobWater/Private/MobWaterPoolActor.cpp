// Copyright (c) Jared Taylor

#include "MobWaterPoolActor.h"

#include "MobWaterComponent.h"
#include "Components/BillboardComponent.h"

AMobWaterPool::AMobWaterPool()
{
	PrimaryActorTick.bCanEverTick = false;

	Water = CreateDefaultSubobject<UMobWaterComponent>(TEXT("Water"));
	RootComponent = Water;

#if WITH_EDITORONLY_DATA
	// A still pool seen edge on is a line, and a line is not something you can click. The sprite is
	// what makes an empty basin selectable before it has been given an extent.
	Sprite = CreateDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Sprite)
	{
		Sprite->SetupAttachment(Water);
		Sprite->bIsScreenSizeScaled = true;
		Sprite->SetHiddenInGame(true);
		// The sprite rides the surface, which is scaled to the body's extent, so it would otherwise
		// grow with the pool and swallow the level.
		Sprite->SetUsingAbsoluteScale(true);
		MobWaterSprite::Apply(Sprite);
	}
#endif
}
