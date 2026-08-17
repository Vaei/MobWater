// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MobWaterEditorLibrary.generated.h"

/**
 * The few editor-side things the generators and the checks need and Python has no binding for.
 */
UCLASS()
class MOBWATEREDITOR_API UMobWaterEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Blocks until everything still being built has finished being built.
	 *
	 * A texture loaded a moment ago is not a texture that can be sampled. Its platform data is built
	 * on worker threads and finalised on the game thread, and until that has happened it reports
	 * itself as a placeholder a few texels square and a shader handed it samples the engine's default
	 * instead - which reads as the atlas being wrong rather than as it not being there yet.
	 *
	 * A script has no frames between loading an asset and drawing with it, so it has to ask. This is
	 * what stands between the spectrum check comparing the baked sea against black and reporting that
	 * the maths has parted, and between the cost report quoting a sea state at thirty two texels
	 * square.
	 */
	UFUNCTION(BlueprintCallable, Category="MobWater")
	static void FinishAssetCompilation();
};
