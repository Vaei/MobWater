// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MobWaterWaves.h"
#include "MobWaterWavePreset.generated.h"

/**
 * A named set of waves, shared by every body that wants that sea.
 *
 * An asset rather than a struct on each body, because the three scalars beside it let one preset
 * serve a pond and a harbour at different amplitudes - so a project tunes one sea and every body of
 * water in it follows, instead of drifting apart the first time one is adjusted.
 */
UCLASS(BlueprintType)
class MOBWATER_API UMobWaterWavePreset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Waves", meta=(ShowOnlyInnerProperties))
	FMobWaterWaveParams Waves;
};
