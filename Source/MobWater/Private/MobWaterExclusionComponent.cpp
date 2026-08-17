// Copyright (c) Jared Taylor

#include "MobWaterExclusionComponent.h"

#include "MobWaterModule.h"
#include "MobWaterSubsystem.h"

UMobWaterExclusionComponent::UMobWaterExclusionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

void UMobWaterExclusionComponent::OnRegister()
{
	Super::OnRegister();

	if (Shape == EMobWaterExclusionShape::Mesh)
	{
		// Said once, where it can be acted on. A mesh silhouette needs the field's spare channel and
		// a top-down capture to fill it, and neither exists yet - so the shape is accepted, behaves
		// as its bounding rectangle, and says so rather than silently doing something else.
		UE_LOG(LogMobWater, Warning,
			TEXT("MobWater: %s uses Mesh exclusion, which is not implemented. It behaves as a Rect of "
				 "its extent."), *GetPathName());
	}

	if (UMobWaterSubsystem* Subsystem = UMobWaterSubsystem::Get(this))
	{
		Subsystem->RegisterExclusion(this);
	}
}

void UMobWaterExclusionComponent::OnUnregister()
{
	if (UMobWaterSubsystem* Subsystem = UMobWaterSubsystem::Get(this))
	{
		Subsystem->UnregisterExclusion(this);
	}

	Super::OnUnregister();
}

void UMobWaterExclusionComponent::PackForShader(FLinearColor& OutA, FLinearColor& OutB) const
{
	const FVector Location = GetComponentLocation();
	const float Yaw = FMath::DegreesToRadians(GetComponentRotation().Yaw);

	const bool bRadial = Shape == EMobWaterExclusionShape::Disc || Shape == EMobWaterExclusionShape::Sphere;

	OutA = FLinearColor(
		static_cast<float>(Location.X),
		static_cast<float>(Location.Y),
		FMath::Max(static_cast<float>(Extent.X), 1.f),
		FMath::Max(static_cast<float>(Extent.Y), 1.f));

	// Cosine and sine rather than the angle, so the shader compares without calling sincos on every
	// pixel for a value that is the same across the whole volume.
	OutB = FLinearColor(
		FMath::Cos(Yaw),
		FMath::Sin(Yaw),
		bRadial ? 0.f : 1.f,
		Strength * (IsActive() ? 1.f : 0.f));
}

float UMobWaterExclusionComponent::GetExclusionAt(const FVector& Location) const
{
	if (!IsActive() || Strength <= 0.f)
	{
		return 0.f;
	}

	const FVector Centre = GetComponentLocation();
	const float Yaw = FMath::DegreesToRadians(GetComponentRotation().Yaw);

	const FVector2D Offset(Location.X - Centre.X, Location.Y - Centre.Y);

	// Into the volume's own frame, which is the same rotation the shader applies.
	const float CosYaw = FMath::Cos(Yaw);
	const float SinYaw = FMath::Sin(Yaw);
	const FVector2D Local(
		Offset.X * CosYaw + Offset.Y * SinYaw,
		-Offset.X * SinYaw + Offset.Y * CosYaw);

	const float HalfX = FMath::Max(static_cast<float>(Extent.X), 1.f);
	const float HalfY = FMath::Max(static_cast<float>(Extent.Y), 1.f);

	float Inside;
	if (Shape == EMobWaterExclusionShape::Disc || Shape == EMobWaterExclusionShape::Sphere)
	{
		Inside = HalfX - static_cast<float>(Local.Size());
	}
	else
	{
		Inside = FMath::Min(
			HalfX - FMath::Abs(static_cast<float>(Local.X)),
			HalfY - FMath::Abs(static_cast<float>(Local.Y)));
	}

	if (Inside <= 0.f)
	{
		return 0.f;
	}

	const float Softness = FMath::Max(EdgeSoftness, 1.f);
	return FMath::Clamp(Inside / Softness, 0.f, 1.f) * Strength;
}
