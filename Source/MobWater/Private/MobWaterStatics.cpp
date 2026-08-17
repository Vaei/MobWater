// Copyright (c) Jared Taylor

#include "MobWaterStatics.h"

#include "MobWaterComponent.h"
#include "MobWaterSpectrum.h"
#include "MobWaterSubsystem.h"
#include "MobWaterWavePreset.h"

float UMobWaterStatics::GetWaterTime(const UObject* WorldContextObject)
{
	const UMobWaterSubsystem* Subsystem = UMobWaterSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetWaterTime() : 0.f;
}

bool UMobWaterStatics::GetWaterInfoAtLocation(const UObject* Querier, const FVector& Location,
	FMobWaterInfo& OutInfo)
{
	const UMobWaterSubsystem* Subsystem = UMobWaterSubsystem::Get(Querier);
	if (!Subsystem)
	{
		OutInfo = FMobWaterInfo();
		return false;
	}

	const UMobWaterComponent* Body = Subsystem->FindBodyAt(Location);
	if (!Body)
	{
		OutInfo = FMobWaterInfo();
		return false;
	}

	OutInfo = Body->GetWaterInfoAtLocation(Location);
	return OutInfo.bValid;
}

bool UMobWaterStatics::GetWaterSurfaceZ(const UObject* Querier, const FVector& Location,
	float& OutSurfaceZ)
{
	FMobWaterInfo Info;
	if (GetWaterInfoAtLocation(Querier, Location, Info))
	{
		OutSurfaceZ = Info.SurfaceZ;
		return true;
	}

	OutSurfaceZ = 0.f;
	return false;
}

void UMobWaterStatics::GetWaterInfoAtLocations(const UObject* Querier,
	const TArray<FVector>& Locations, TArray<FMobWaterInfo>& OutInfos)
{
	OutInfos.Reset(Locations.Num());

	const UMobWaterSubsystem* Subsystem = UMobWaterSubsystem::Get(Querier);
	if (!Subsystem)
	{
		OutInfos.AddDefaulted(Locations.Num());
		return;
	}

	// Cached across the loop rather than looked up per point. A pontoon array is a handful of points
	// a metre apart, and they are in the same body every time but the one frame a hull crosses an
	// edge - so the lookup is repeated work on all but that frame.
	const UMobWaterComponent* Cached = nullptr;

	for (const FVector& Location : Locations)
	{
		if (!Cached || !Cached->ContainsLocation(Location))
		{
			Cached = Subsystem->FindBodyAt(Location);
		}

		OutInfos.Add(Cached ? Cached->GetWaterInfoAtLocation(Location) : FMobWaterInfo());
	}
}

void UMobWaterStatics::UnpackBodyWaveScales(float Packed, float& Amplitude, float& Speed)
{
	MobWaterBodyScales::Unpack(Packed, Amplitude, Speed);
}

float UMobWaterStatics::PackBodyWaveScales(float Amplitude, float Speed)
{
	return MobWaterBodyScales::Pack(Amplitude, Speed);
}

void UMobWaterStatics::EvaluateWavePreset(const UMobWaterWavePreset* Preset, FVector2D SampleXY, float Time,
	FVector& Displacement, FVector& Normal, float& Fold)
{
	if (!Preset)
	{
		Displacement = FVector::ZeroVector;
		Normal = FVector::UpVector;
		Fold = 0.f;
		return;
	}

	const FMobWaterSample Sample = FMobWaterWaves::Evaluate(Preset->Waves, FVector2f(SampleXY), Time);

	Displacement = FVector(Sample.Displacement);
	Normal = FVector(Sample.Normal);
	Fold = Sample.Fold;
}

void UMobWaterStatics::EvaluateWavePresetSurface(const UMobWaterWavePreset* Preset, FVector2D WorldXY, float Time,
	float& SurfaceOffsetZ, FVector& Normal, float& Fold)
{
	if (!Preset)
	{
		SurfaceOffsetZ = 0.f;
		Normal = FVector::UpVector;
		Fold = 0.f;
		return;
	}

	const FMobWaterSample Sample = FMobWaterWaves::Surface(Preset->Waves, FVector2f(WorldXY), Time);

	SurfaceOffsetZ = Sample.Displacement.Z;
	Normal = FVector(Sample.Normal);
	Fold = Sample.Fold;
}

void UMobWaterStatics::EvaluateSpectrum(const UMobWaterSpectrum* Spectrum, FVector2D WorldXY, float Time,
	FVector& Displacement, float& Fold)
{
	Displacement = FVector::ZeroVector;
	Fold = 0.f;

	if (!Spectrum || !Spectrum->IsUsable())
	{
		return;
	}

	float Folding = 0.f;
	Displacement = FVector(Spectrum->SampleDisplacement(FVector2f(WorldXY), Time, Folding));
	Fold = Folding;
}

FMobWaterInfo UMobWaterStatics::EvaluateWaterAt(const UObject* WorldContextObject, FVector Location,
	float StillSurfaceZ, float WaterDepth, float ShoreFade)
{
	const UMobWaterSubsystem* Subsystem = UMobWaterSubsystem::Get(WorldContextObject);
	if (!Subsystem)
	{
		return FMobWaterInfo();
	}

	return EvaluateWaterAtNative(Subsystem->GetDefaultWaves(), Location, StillSurfaceZ, WaterDepth,
		ShoreFade, Subsystem->GetWaterTime());
}

FMobWaterInfo UMobWaterStatics::EvaluateWaterAtNative(const FMobWaterWaveParams& Params, const FVector& Location,
	float StillSurfaceZ, float WaterDepth, float ShoreFade, float Time, const UMobWaterSpectrum* Spectrum)
{
	const FVector2f WorldXY = FVector2f(static_cast<float>(Location.X), static_cast<float>(Location.Y));

	const FMobWaterSample Sample = MobWaterCombined::Surface(Params, Spectrum, WorldXY, Time);

	// Waves lie down towards the bank, and the query has to agree with the vertex about that or
	// buoyancy floats a crate above a shoreline the surface has already flattened.
	const float Attenuation = FMath::Clamp(ShoreFade, 0.f, 1.f);

	FMobWaterInfo Out;
	Out.bValid = true;
	Out.SurfaceZ = StillSurfaceZ + Sample.Displacement.Z * Attenuation;
	Out.Normal = FVector(FMath::Lerp(FVector3f(0.f, 0.f, 1.f), Sample.Normal, Attenuation).GetSafeNormal());
	Out.ImmersionDepth = Out.SurfaceZ - static_cast<float>(Location.Z);
	Out.Depth = WaterDepth;
	Out.Fold = Sample.Fold * Attenuation;

	return Out;
}
