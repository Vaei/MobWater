// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MobWaterMeshLibrary.generated.h"

class UStaticMesh;

/**
 * The surfaces a body of water rasterises.
 *
 * Generated rather than modelled, because they have to be exact. A plane has to carry UVs that run
 * cleanly 0 to 1 so the shader can measure distance to the bank from them, and it has to be evenly
 * tessellated enough for a Gerstner wave to have somewhere to go - both of those are arithmetic, not
 * modelling, and a mesh that is nearly right fails in ways that look like a shader bug.
 */
UCLASS()
class MOBWATEREDITOR_API UMobWaterMeshLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Builds every surface mesh the settings name. Generating materials does this for you. */
	UFUNCTION(BlueprintCallable, Category="Mob|Water")
	static void BuildSurfaceMeshes();

	/**
	 * A unit square in XY, centred on the origin, subdivided Segments times each way.
	 *
	 * Unit sized because the component scales it to the body's extent, which is what lets one mesh
	 * serve every pool in a level.
	 */
	static UStaticMesh* BuildPlane(const FString& PackagePath, const FString& Name, int32 Segments);

	/** A unit disc, as rings and sectors, with UVs that put the rim exactly on the unit circle. */
	static UStaticMesh* BuildDisc(const FString& PackagePath, const FString& Name, int32 Rings, int32 Sectors);

	/**
	 * A unit disc whose rings crowd towards the middle.
	 *
	 * An ocean is drawn centred on the camera and reaches to the horizon, so an evenly spaced disc
	 * spends almost all of its vertices on water that is kilometres away and a pixel high. Spacing
	 * the rings by a power puts them where the waves can actually be seen, which is the difference
	 * between an ocean that has a shape near you and one that is a flat plate with a moving texture.
	 */
	static UStaticMesh* BuildOceanRing(const FString& PackagePath, const FString& Name,
		int32 Rings, int32 Sectors, float Exponent, float WorldSize);

private:
	/** Both discs, differing in how the rings are spaced and whether they are built at world size. */
	static UStaticMesh* BuildDiscInternal(const FString& PackagePath, const FString& Name,
		int32 Rings, int32 Sectors, float Exponent, float WorldSize);
};
