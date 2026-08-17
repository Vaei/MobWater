// Copyright (c) Jared Taylor

#include "MobWaterExclusionActor.h"

#include "MobWaterExclusionComponent.h"
#include "MobWaterTypes.h"
#include "Components/BillboardComponent.h"

AMobWaterExclusion::AMobWaterExclusion()
{
	PrimaryActorTick.bCanEverTick = false;

	Exclusion = CreateDefaultSubobject<UMobWaterExclusionComponent>(TEXT("Exclusion"));
	RootComponent = Exclusion;

#if WITH_EDITORONLY_DATA
	Sprite = CreateDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Sprite)
	{
		Sprite->SetupAttachment(Exclusion);
		Sprite->bIsScreenSizeScaled = true;
		Sprite->SetHiddenInGame(true);
		MobWaterSprite::Apply(Sprite);
	}
#endif
}
