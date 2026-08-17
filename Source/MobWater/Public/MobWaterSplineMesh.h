// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"

class UMobWaterSplineComponent;
class UStaticMesh;

/**
 * How far from the shore a vertex colour of 1 means, in world units.
 *
 * Vertex colour is eight bits and runs 0 to 1, so the distance it carries has to be divided by
 * something before it is stored and multiplied by the same thing after. Ten metres, because the
 * waves have entirely stopped attenuating well before that and the precision is better spent near
 * the bank where the fade actually happens.
 *
 * Duplicated as MOB_WATER_SHORE_REFERENCE in MobWaterSurface.ush.
 */
#define MOB_WATER_SHORE_REFERENCE 1000.f

/**
 * Builds the surface a spline body of water is drawn on.
 *
 * Two shapes out of one idea. A closed spline gets a grid over its bounds with the signed distance
 * to the shoreline baked into vertex colour, so the water is transparent outside the loop and the
 * shoreline can be any shape at all without anything being triangulated. An open spline gets a
 * ribbon along its course, which tessellates evenly for free.
 *
 * The grid is deliberately not clipped to the shoreline. Clipping means constrained triangulation,
 * which means an edge made of whatever the triangulator decided; letting the opacity do it means the
 * edge is resolved per pixel and a bay two centimetres across still reads.
 */
struct MOBWATER_API FMobWaterSplineMesh
{
	/**
	 * Rebuilds a mesh from a spline. The mesh is owned by the caller and rebuilt in place.
	 *
	 * Returns false when the spline has too little to build from, leaving the mesh untouched rather
	 * than emptied - a spline mid-edit briefly has one point, and blinking the water out every time
	 * someone adds one is worse than a frame of stale geometry.
	 */
	static bool Build(const UMobWaterSplineComponent& Spline, UStaticMesh& OutMesh);
};
