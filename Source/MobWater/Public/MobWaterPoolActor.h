// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MobWaterPoolActor.generated.h"

class UMobWaterComponent;
class UBillboardComponent;

/**
 * A bounded body of water: a puddle, a basin, a bath, a pond.
 *
 * The shape most levels actually want, and the one the whole material is arranged around - a surface
 * with an edge close enough to see, where the waves have to lie down before they reach it.
 */
UCLASS(ClassGroup=Rendering, hidecategories=(Input, Collision, Replication, Physics, HLOD, Cooking, DataLayers))
class MOBWATER_API AMobWaterPool : public AActor
{
	GENERATED_BODY()

public:
	AMobWaterPool();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Water")
	TObjectPtr<UMobWaterComponent> Water;

	UMobWaterComponent* GetWaterComponent() const { return Water; }

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> Sprite;
#endif
};
