// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Components/SplineComponent.h"
#include "MobWaterSplineComponent.generated.h"

/**
 * The outline of a body of water, or the course of one.
 *
 * Closed, the spline is a shoreline and the water is everything inside it - a lake with whatever
 * shape you draw. Open, it is the middle of a river and the width says how far the banks are.
 *
 * One component for both because they are the same question asked twice: how far is this point from
 * the edge of the water. A closed spline answers it with a signed distance to the loop, an open one
 * with the distance across the ribbon, and everything downstream - the mesh, the wave attenuation,
 * the CPU query - only ever asks that.
 */
UCLASS(ClassGroup=Rendering, meta=(BlueprintSpawnableComponent, DisplayName="Mob Water Spline"))
class MOBWATER_API UMobWaterSplineComponent : public USplineComponent
{
	GENERATED_BODY()

public:
	UMobWaterSplineComponent();

	/**
	 * How far the banks are from the middle, per spline point, in world units.
	 *
	 * Only read on an open spline. A closed one is its own bank, so a width would be asking where the
	 * shore is when the shore is the thing that was drawn.
	 *
	 * Short lists are padded with the last entry rather than treated as an error - a spline gains
	 * points by being dragged, and refusing to build until someone tops the array up would mean the
	 * water vanishes mid-edit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water", meta=(ForceUnits="cm"))
	TArray<float> Widths;

	/** How finely a closed body is tessellated across its bounds. The waves have nowhere else to go. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water", meta=(ClampMin="4", ClampMax="256"))
	int32 GridResolution = 64;

	/** How many segments a river is cut into along its length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water", meta=(ClampMin="2", ClampMax="512"))
	int32 SegmentsAlong = 64;

	/** How many across. Two is a flat ribbon; more lets a wave cross the river rather than only run it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water", meta=(ClampMin="2", ClampMax="64"))
	int32 SegmentsAcross = 6;

	/** The width at a distance along the spline, padded and interpolated from Widths. */
	float GetWidthAtDistance(float Distance) const;

	/**
	 * How far a world point is inside the water, in world units. Negative outside it.
	 *
	 * The one question everything else is built on. Closed splines answer with a signed distance to
	 * the loop; open ones with how far the point is from the nearer bank.
	 */
	float GetDistanceInside(const FVector& WorldLocation) const;

	/** The spline's points projected to the horizontal plane, in world space. */
	void GetShorelinePoints(TArray<FVector2D>& OutPoints, int32 SamplesPerSegment = 8) const;
};
