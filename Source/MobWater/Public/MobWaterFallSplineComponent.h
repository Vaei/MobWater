// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Components/SplineComponent.h"
#include "MobWaterFallSplineComponent.generated.h"

/**
 * The lip of a waterfall: the edge the water goes over.
 *
 * Its own component rather than the body spline, because the two answer opposite questions. A body
 * spline is asked how far a point is from the edge of the water and every answer it gives is a
 * distance across the ground. This one is asked where the edge is and how far the water falls from
 * it, which is a question about a vertical sheet, and nothing about a shoreline has an opinion on
 * that.
 *
 * Open, always. A closed loop would be a fall going over its own lip on both sides of the same
 * water, which is a fountain rather than a fall, and the mesh has no way to draw the inside of it.
 */
UCLASS(ClassGroup=Rendering, meta=(BlueprintSpawnableComponent, DisplayName="Mob Water Fall Spline"))
class MOBWATER_API UMobWaterFallSplineComponent : public USplineComponent
{
	GENERATED_BODY()

public:
	UMobWaterFallSplineComponent();

	/**
	 * How far the water falls, in world units, everywhere the lip has no drop of its own.
	 *
	 * A ledge is level far more often than it is not, and a level ledge answered per point is the
	 * same number typed once for every point on it, retyped every time one of them moves.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fall", meta=(ClampMin="0", ForceUnits="cm"))
	float Drop = 500.f;

	/**
	 * How far the water falls at each point along the lip, in world units, in place of Drop.
	 *
	 * Empty is a level ledge. Filled, it is a fall over an uneven one, reaching a plunge pool that is
	 * not level. Short lists are padded with the last entry rather than treated as an error, for the
	 * same reason a river's widths are: a spline gains points by being dragged, and refusing to build
	 * until someone tops the array up would mean the fall vanishes mid-edit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fall", meta=(ForceUnits="cm"))
	TArray<float> DropOverrides;

	/**
	 * How far the base of the sheet stands out from the lip, in world units.
	 *
	 * Zero hangs straight down, which is a fall off an overhang. Positive leans it out the way the
	 * water is going, which is a fall running down a slope - and a sheet lying against rock is what
	 * the depth column reads as thin, so a leaning fall is also the one that shows its bed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fall", meta=(ForceUnits="cm"))
	float Overhang = 0.f;

	/**
	 * Which side of the lip the water goes over.
	 *
	 * A spline drawn right to left hangs its sheet the other way, and there is nothing in a line to
	 * say which side the drop is on. This is the one control that cannot be inferred, so it is a
	 * checkbox rather than a guess that is wrong half the time.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fall")
	bool bMirror = false;

	/** How many segments the lip is cut into along its length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fall", meta=(ClampMin="1", ClampMax="256"))
	int32 SegmentsAlong = 24;

	/**
	 * How many the sheet is cut into on the way down.
	 *
	 * The lip join moves the top rows and leaves the bottom where it was placed, so this is what
	 * decides whether that blend is a curve or a crease. Below about six it creases.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fall", meta=(ClampMin="1", ClampMax="128"))
	int32 SegmentsDown = 12;

	/** The drop at a distance along the lip, padded and interpolated from DropOverrides, or Drop where there are none. */
	float GetDropAtDistance(float Distance) const;

	/** Which way the water goes over, at a distance along the lip. Flat, and unit length. */
	FVector GetDownstreamAtDistance(float Distance) const;

	/** How tall the fall is where it is deepest, which is what the sheet's own arithmetic is scaled by. */
	float GetMaxDrop() const;
};
