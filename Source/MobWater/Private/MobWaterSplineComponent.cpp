// Copyright (c) Jared Taylor

#include "MobWaterSplineComponent.h"

UMobWaterSplineComponent::UMobWaterSplineComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	bDrawDebug = true;

	// A shoreline is a shape, not a path. Curved points make a lake read as a lake rather than as a
	// polygon, and a river bend that is a corner is a canal.
	SetClosedLoop(true);
}

float UMobWaterSplineComponent::GetWidthAtDistance(float Distance) const
{
	if (Widths.Num() == 0)
	{
		return 300.f;
	}

	const float Length = GetSplineLength();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return Widths[0];
	}

	const int32 PointCount = GetNumberOfSplinePoints();
	if (PointCount < 2)
	{
		return Widths[0];
	}

	// Where along the points this distance falls, so the width follows the spline's own parameter
	// rather than a straight fraction of its length - the two differ wherever the curve is uneven.
	const float Key = GetInputKeyValueAtDistanceAlongSpline(Distance);

	const int32 Lower = FMath::Clamp(FMath::FloorToInt(Key), 0, PointCount - 1);
	const int32 Upper = FMath::Clamp(Lower + 1, 0, PointCount - 1);

	// Padded with the last entry rather than refused. A spline gains points by being dragged, and
	// water that vanishes until someone tops up an array is water nobody can edit.
	const float A = Widths[FMath::Min(Lower, Widths.Num() - 1)];
	const float B = Widths[FMath::Min(Upper, Widths.Num() - 1)];

	return FMath::Lerp(A, B, FMath::Frac(Key));
}

void UMobWaterSplineComponent::GetShorelinePoints(TArray<FVector2D>& OutPoints, int32 SamplesPerSegment) const
{
	OutPoints.Reset();

	const int32 PointCount = GetNumberOfSplinePoints();
	if (PointCount < 2)
	{
		return;
	}

	// Sampled rather than the control points themselves. A curved shoreline between two points is
	// most of its shape, and a polygon of the control points alone cuts every bay off.
	const int32 Segments = IsClosedLoop() ? PointCount : PointCount - 1;
	const int32 PerSegment = FMath::Max(SamplesPerSegment, 1);

	OutPoints.Reserve(Segments * PerSegment);

	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		for (int32 Step = 0; Step < PerSegment; ++Step)
		{
			const float Key = Segment + static_cast<float>(Step) / static_cast<float>(PerSegment);
			const FVector World = GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World);

			OutPoints.Add(FVector2D(World.X, World.Y));
		}
	}
}

float UMobWaterSplineComponent::GetDistanceInside(const FVector& WorldLocation) const
{
	const FVector2D Point(WorldLocation.X, WorldLocation.Y);

	if (!IsClosedLoop())
	{
		// A river: how far from the nearer bank. The spline is the middle, so the distance across is
		// the half width less how far off the middle the point is.
		const float Key = FindInputKeyClosestToWorldLocation(WorldLocation);
		const FVector OnSpline = GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World);
		const float Distance = GetDistanceAlongSplineAtSplineInputKey(Key);

		const float HalfWidth = GetWidthAtDistance(Distance) * 0.5f;
		const float Across = static_cast<float>(FVector2D::Distance(Point, FVector2D(OnSpline.X, OnSpline.Y)));

		return HalfWidth - Across;
	}

	TArray<FVector2D> Shore;
	GetShorelinePoints(Shore);

	if (Shore.Num() < 3)
	{
		return -1.f;
	}

	// Crossing count for inside or out, nearest edge for how far. Two passes over the same list
	// rather than one clever one, because the winding test and the distance test disagree about
	// what to do at a vertex and interleaving them is how that becomes a bug nobody can reproduce.
	bool bInside = false;
	float NearestSquared = TNumericLimits<float>::Max();

	for (int32 Index = 0, Previous = Shore.Num() - 1; Index < Shore.Num(); Previous = Index++)
	{
		const FVector2D& A = Shore[Index];
		const FVector2D& B = Shore[Previous];

		if ((A.Y > Point.Y) != (B.Y > Point.Y))
		{
			const double Cross = (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X;
			if (Point.X < Cross)
			{
				bInside = !bInside;
			}
		}

		const FVector2D Segment = B - A;
		const double LengthSquared = Segment.SizeSquared();

		const double T = LengthSquared > SMALL_NUMBER
			? FMath::Clamp(FVector2D::DotProduct(Point - A, Segment) / LengthSquared, 0.0, 1.0)
			: 0.0;

		const float Squared = static_cast<float>(FVector2D::DistSquared(Point, A + Segment * T));
		NearestSquared = FMath::Min(NearestSquared, Squared);
	}

	const float Nearest = FMath::Sqrt(NearestSquared);
	return bInside ? Nearest : -Nearest;
}
