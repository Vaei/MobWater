// Copyright (c) Jared Taylor

#include "MobWaterFallSplineComponent.h"

UMobWaterFallSplineComponent::UMobWaterFallSplineComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	bDrawDebug = true;

	SetClosedLoop(false);
}

float UMobWaterFallSplineComponent::GetDropAtDistance(float Distance) const
{
	if (DropOverrides.Num() == 0)
	{
		return Drop;
	}

	const int32 PointCount = GetNumberOfSplinePoints();
	if (PointCount < 2 || GetSplineLength() <= KINDA_SMALL_NUMBER)
	{
		return DropOverrides[0];
	}

	// Along the spline's own parameter rather than a straight fraction of its length. The two differ
	// wherever the curve is uneven, and a drop that slid along the lip as a point was dragged would
	// be a fall changing height somewhere nobody touched.
	const float Key = GetInputKeyValueAtDistanceAlongSpline(Distance);

	const int32 Lower = FMath::Clamp(FMath::FloorToInt(Key), 0, PointCount - 1);
	const int32 Upper = FMath::Clamp(Lower + 1, 0, PointCount - 1);

	const float A = DropOverrides[FMath::Min(Lower, DropOverrides.Num() - 1)];
	const float B = DropOverrides[FMath::Min(Upper, DropOverrides.Num() - 1)];

	return FMath::Lerp(A, B, FMath::Frac(Key));
}

FVector UMobWaterFallSplineComponent::GetDownstreamAtDistance(float Distance) const
{
	const FVector Tangent = GetDirectionAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);

	// Flattened before the cross product. A lip drawn down a slope would otherwise send the water out
	// along the slope as well as down it, and the sheet would lean by however much the lip did.
	const FVector Flat = FVector(Tangent.X, Tangent.Y, 0.0).GetSafeNormal();
	if (Flat.IsNearlyZero())
	{
		return FVector::ForwardVector;
	}

	const FVector Side = FVector::CrossProduct(Flat, FVector::UpVector).GetSafeNormal();
	return bMirror ? -Side : Side;
}

float UMobWaterFallSplineComponent::GetMaxDrop() const
{
	if (DropOverrides.Num() == 0)
	{
		return FMath::Max(Drop, 1.f);
	}

	float Deepest = 0.f;
	for (const float Override : DropOverrides)
	{
		Deepest = FMath::Max(Deepest, Override);
	}

	return FMath::Max(Deepest, 1.f);
}
