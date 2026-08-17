// Copyright (c) Jared Taylor

#include "MobWaterSettings.h"

#include "MobWaterComponent.h"
#include "MobWaterSpectrum.h"
#include "MobWaterModule.h"
#include "MobWaterUnderwaterComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "UObject/UObjectIterator.h"

namespace
{
	/**
	 * Shapes already complained about.
	 *
	 * A material that is missing is missing on every frame a body of water is visible, so the warning
	 * has to be said once and then stopped, or it is the only thing in the log.
	 */
	TSet<EMobWaterShape> GReportedMissing;
}

UMobWaterSettings::UMobWaterSettings()
{
	// Defaults so a fresh clone renders something before anyone has opened the settings. Every one of
	// these is redirectable, which is the point of them being settings at all.
	SurfaceMeshes.Add(EMobWaterShape::Box, TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/MobWater/Meshes/SM_MobWaterPlane.SM_MobWaterPlane"))));
	SurfaceMeshes.Add(EMobWaterShape::Disc, TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/MobWater/Meshes/SM_MobWaterDisc.SM_MobWaterDisc"))));
	SurfaceMeshes.Add(EMobWaterShape::Ocean, TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/MobWater/Meshes/SM_MobWaterOceanRing.SM_MobWaterOceanRing"))));

	ParameterCollection = TSoftObjectPtr<UMaterialParameterCollection>(FSoftObjectPath(TEXT("/MobWater/Materials/MPC_MobWater.MPC_MobWater")));

	// A pond that arrives carrying an ocean's swell has to be corrected before it can be judged, and
	// the correction is the same every time.
	const TCHAR* Pond = TEXT("/MobWater/Waves/WP_MobWater_Pond.WP_MobWater_Pond");
	const TCHAR* Lake = TEXT("/MobWater/Waves/WP_MobWater_Lake.WP_MobWater_Lake");
	const TCHAR* Ocean = TEXT("/MobWater/Waves/WP_MobWater_Ocean.WP_MobWater_Ocean");

	DefaultWavePresets.Add(EMobWaterShape::Box, TSoftObjectPtr<UMobWaterWavePreset>(FSoftObjectPath(Pond)));
	DefaultWavePresets.Add(EMobWaterShape::Disc, TSoftObjectPtr<UMobWaterWavePreset>(FSoftObjectPath(Pond)));
	DefaultWavePresets.Add(EMobWaterShape::Spline, TSoftObjectPtr<UMobWaterWavePreset>(FSoftObjectPath(Lake)));
	DefaultWavePresets.Add(EMobWaterShape::Ocean, TSoftObjectPtr<UMobWaterWavePreset>(FSoftObjectPath(Ocean)));

	DefaultSpectrum = TSoftObjectPtr<UMobWaterSpectrum>(FSoftObjectPath(TEXT("/MobWater/Spectra/SP_MobWater_Ocean.SP_MobWater_Ocean")));

	ReflectionTexture = TSoftObjectPtr<UTexture>(FSoftObjectPath(TEXT("/MobWater/Textures/T_MobWaterSky.T_MobWaterSky")));

	UnderwaterMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/MobWater/Materials/M_MobWaterUnderwater.M_MobWaterUnderwater")));
	UnderwaterComponent = UMobWaterUnderwaterComponent::StaticClass();

	RippleTarget = TSoftObjectPtr<UTextureRenderTarget2D>(FSoftObjectPath(TEXT("/MobWater/Textures/RT_MobWaterRipple.RT_MobWaterRipple")));
	RippleHistory = TSoftObjectPtr<UTextureRenderTarget2D>(FSoftObjectPath(TEXT("/MobWater/Textures/RT_MobWaterRippleHistory.RT_MobWaterRippleHistory")));
	RippleStepMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/MobWater/Materials/M_MobWaterRippleStep.M_MobWaterRippleStep")));
	RippleCopyMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/MobWater/Materials/M_MobWaterRippleCopy.M_MobWaterRippleCopy")));
	RippleStampMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/MobWater/Materials/M_MobWaterRippleStamp.M_MobWaterRippleStamp")));
}

#if WITH_EDITOR
void UMobWaterSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Whatever was missing may not be missing any more, so let the warning be said again if it is.
	GReportedMissing.Empty();

	RefreshPlacedWater();
}

void UMobWaterSettings::RefreshPlacedWater()
{
	GReportedMissing.Empty();

	for (TObjectIterator<UMobWaterComponent> It; It; ++It)
	{
		UMobWaterComponent* Water = *It;
		if (IsValid(Water) && Water->GetWorld())
		{
			Water->ApplySurface();
			Water->MarkRenderStateDirty();
		}
	}
}
#endif

UStaticMesh* UMobWaterSettings::GetSurfaceMesh(EMobWaterShape Shape)
{
	const UMobWaterSettings* Settings = GetDefault<UMobWaterSettings>();
	if (const TSoftObjectPtr<UStaticMesh>* Found = Settings->SurfaceMeshes.Find(Shape))
	{
		return Found->LoadSynchronous();
	}

	return nullptr;
}

UMobWaterWavePreset* UMobWaterSettings::GetDefaultWavePreset(EMobWaterShape Shape)
{
	const UMobWaterSettings* Settings = GetDefault<UMobWaterSettings>();
	if (const TSoftObjectPtr<UMobWaterWavePreset>* Found = Settings->DefaultWavePresets.Find(Shape))
	{
		return Found->LoadSynchronous();
	}

	return nullptr;
}

UMobWaterSpectrum* UMobWaterSettings::GetDefaultSpectrum()
{
	return GetDefault<UMobWaterSettings>()->DefaultSpectrum.LoadSynchronous();
}

UMaterialInterface* UMobWaterSettings::GetMaterial(EMobWaterShape Shape, int32 Variant)
{
	const UMobWaterSettings* Settings = GetDefault<UMobWaterSettings>();

	const FMobWaterMaterialSet* Set = Settings->Materials.Find(Shape);
	if (!Set)
	{
		if (!GReportedMissing.Contains(Shape))
		{
			GReportedMissing.Add(Shape);
			UE_LOG(LogMobWater, Warning, TEXT("No materials for %s water. Run Water > Generate Materials."), MobWaterShapeName(Shape));
#if WITH_EDITOR
			OnMobWaterMaterialMissing.Broadcast(Shape);
#endif
		}
		return nullptr;
	}

	// Drop one feature at a time rather than falling straight to the plain material, so a project that
	// has generated most of the set gets the nearest thing to what it asked for. The foam's own
	// texture goes first because losing it leaves foam that is still foam, then refraction, which is
	// the one a platform may have refused to compile at all, then the gradient, which costs the body
	// its palette and leaves it water, then foam, then ripples.
	static constexpr int32 DropOrder[] = { MobWaterVariant::FoamTexture, MobWaterVariant::Refraction,
		MobWaterVariant::Gradient, MobWaterVariant::Foam, MobWaterVariant::Ripples };
	static constexpr int32 NumDrops = static_cast<int32>(UE_ARRAY_COUNT(DropOrder));

	int32 Wanted = Variant;
	for (int32 Attempt = 0; Attempt <= NumDrops; ++Attempt)
	{
		if (Set->Variants.IsValidIndex(Wanted))
		{
			if (UMaterialInterface* Material = Set->Variants[Wanted].LoadSynchronous())
			{
				return Material;
			}
		}

		if (Attempt < NumDrops)
		{
			Wanted &= ~DropOrder[Attempt];
		}
	}

	if (!GReportedMissing.Contains(Shape))
	{
		GReportedMissing.Add(Shape);
		UE_LOG(LogMobWater, Warning, TEXT("No material for %s water, variant %d. Run Water > Generate Materials."),
			MobWaterShapeName(Shape), Variant);
#if WITH_EDITOR
		OnMobWaterMaterialMissing.Broadcast(Shape);
#endif
	}

	return nullptr;
}
