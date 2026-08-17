// Copyright (c) Jared Taylor

#include "MobWaterDisturbanceComponent.h"

#include "MobWaterSubsystem.h"

UMobWaterDisturbanceComponent::UMobWaterDisturbanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

void UMobWaterDisturbanceComponent::OnRegister()
{
	Super::OnRegister();

	if (UMobWaterSubsystem* Subsystem = UMobWaterSubsystem::Get(this))
	{
		Subsystem->RegisterDisturber(this);
	}
}

void UMobWaterDisturbanceComponent::OnUnregister()
{
	if (UMobWaterSubsystem* Subsystem = UMobWaterSubsystem::Get(this))
	{
		Subsystem->UnregisterDisturber(this);
	}

	Super::OnUnregister();
}

bool UMobWaterDisturbanceComponent::ShouldStamp(float DeltaTime, FVector& OutLocation, float& OutStrength)
{
	OutLocation = GetComponentLocation();
	OutStrength = Strength;

	if (!IsActive() || FMath::IsNearlyZero(Strength))
	{
		return false;
	}

	if (bPersistent)
	{
		// Per second rather than per frame. The same push every frame is four times the disturbance at
		// 240 fps that it is at 60, and the field it lands in cannot tell the two apart.
		OutStrength = Strength * DeltaTime * 60.f;

		LastStampLocation = OutLocation;
		bHasStamped = true;
		return true;
	}

	const float Threshold = MoveThreshold > 0.f ? MoveThreshold : Radius * 0.5f;

	if (bHasStamped && FVector::DistSquared2D(OutLocation, LastStampLocation) < FMath::Square(Threshold))
	{
		return false;
	}

	LastStampLocation = OutLocation;
	bHasStamped = true;
	return true;
}
