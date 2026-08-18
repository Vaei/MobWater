// Copyright (c) Jared Taylor

#include "MobWaterSurfComponent.h"

#include "MobWaterStatics.h"
#include "MobWaterSubsystem.h"

namespace MobWaterSurfCVars
{
	static int32 Enabled = 1;
	static FAutoConsoleVariableRef CVarEnabled(
		TEXT("mob.Water.Surf"),
		Enabled,
		TEXT("Whether surf points broadcast their impacts. 0 stops every one of them at once."),
		ECVF_Default);
}

UMobWaterSurfComponent::UMobWaterSurfComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bAutoActivate = true;
}

void UMobWaterSurfComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Nothing is listening, so there is nothing to work out. This is the check that lets a coastline
	// carry a hundred of these against the day a project binds one.
	if (!bEnabled || !MobWaterSurfCVars::Enabled || !OnSurfImpact.IsBound())
	{
		bBreaking = false;
		LastFold = 0.f;
		return;
	}

	const UMobWaterSubsystem* Subsystem = UMobWaterSubsystem::Get(this);
	if (!Subsystem)
	{
		return;
	}

	const FVector Location = GetComponentLocation();

	if (MaxDistance > 0.f)
	{
		FVector ViewLocation;
		if (Subsystem->GetViewLocation(ViewLocation)
			&& FVector::DistSquared(ViewLocation, Location) > FMath::Square(MaxDistance))
		{
			// Left as it was rather than reset, so walking back into range does not fire an impact
			// for a wave that broke while nobody was there.
			return;
		}
	}

	FMobWaterInfo Info;
	if (!UMobWaterStatics::GetWaterInfoAtLocation(this, Location, Info))
	{
		bBreaking = false;
		LastFold = 0.f;
		return;
	}

	LastFold = Info.Fold;

	if (bBreaking)
	{
		bBreaking = Info.Fold > Threshold * FMath::Clamp(Rearm, 0.f, 1.f);
		return;
	}

	if (Info.Fold <= Threshold)
	{
		return;
	}

	bBreaking = true;

	FMobWaterSurfImpact Impact;
	Impact.Location = FVector(Location.X, Location.Y, Info.SurfaceZ);
	Impact.Normal = Info.Normal;
	Impact.Direction = GetForwardVector();
	Impact.Rise = Info.ImmersionDepth;
	Impact.Strength = FMath::Clamp((Info.Fold - Threshold) / FMath::Max(1.f - Threshold, KINDA_SMALL_NUMBER), 0.f, 1.f);

	OnSurfImpact.Broadcast(Impact);
}
