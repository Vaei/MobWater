// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"

class UMobWaterFallSplineComponent;
class UStaticMesh;

/**
 * Builds the sheet a waterfall is drawn on.
 *
 * A ribbon hung down from the lip: U runs along the edge, V runs down the drop. That is the whole
 * geometry, and it is the reason a fall cannot be a shape of the surface - the surface's UVs are a
 * plan view of a body of water, and every meaning attached to them is a distance across the ground.
 *
 * Tessellated on the way down as well as along, because the lip join moves the top rows to meet the
 * river above and leaves the bottom where it was placed. With too few rows that blend is a crease
 * rather than a curve, and the crease moves with the swell.
 */
struct MOBWATER_API FMobWaterFallMesh
{
	/**
	 * Rebuilds a mesh from a lip. The mesh is owned by the caller and rebuilt in place.
	 *
	 * Returns false when the lip has too little to build from, leaving the mesh untouched rather than
	 * emptied - a spline mid-edit briefly has one point, and blinking the fall out every time someone
	 * adds one is worse than a frame of stale geometry.
	 */
	static bool Build(const UMobWaterFallSplineComponent& Lip, UStaticMesh& OutMesh);
};
