// Copyright (c) Jared Taylor

#include "MobWaterActorFactory.h"

#include "MobWaterBodyActor.h"
#include "MobWaterComponent.h"
#include "MobWaterExclusionActor.h"
#include "MobWaterFallActor.h"
#include "MobWaterFallComponent.h"
#include "MobWaterFallSplineComponent.h"
#include "MobWaterLookPreset.h"
#include "MobWaterOceanActor.h"
#include "MobWaterSplineComponent.h"
#include "MobWaterSpectrum.h"
#include "MobWaterWavePreset.h"
#include "MobWaterPoolActor.h"

#define LOCTEXT_NAMESPACE "MobWaterActorFactory"

namespace
{
	/**
	 * How wide the water is when it is first dropped, in world units.
	 *
	 * One number for a river and a waterfall both, because the usual reason to place a fall is that
	 * a river runs off something - and two defaults that did not match would have to be reconciled
	 * by hand on the first drop, every time.
	 */
	constexpr float MobWaterDefaultWidth = 300.f;
}

UMobWaterPoolFactory::UMobWaterPoolFactory()
{
	NewActorClass = AMobWaterPool::StaticClass();
	bUseSurfaceOrientation = false;
}

void UMobWaterPoolFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	if (AMobWaterPool* Pool = Cast<AMobWaterPool>(NewActor))
	{
		if (UMobWaterComponent* Water = Pool->GetWaterComponent())
		{
			Water->Shape = Shape;

			// Dropped on a look rather than on the class defaults. The defaults are a legal body of
			// water and not a good looking one, and a first impression made by unconfigured values is
			// one nobody comes back from.
			if (UMobWaterLookPreset* Look = LoadObject<UMobWaterLookPreset>(
				nullptr, TEXT("/MobWater/Looks/WL_MobWater_Realistic")))
			{
				Water->LookPreset = Look;
				Water->ApplyLookPreset();
			}
			else
			{
				Water->ApplySurface();
			}
		}
	}
}

UMobWaterPoolFactory_Box::UMobWaterPoolFactory_Box()
{
	Shape = EMobWaterShape::Box;
	DisplayName = LOCTEXT("BoxWater", "Water Pool");
}

UMobWaterPoolFactory_Disc::UMobWaterPoolFactory_Disc()
{
	Shape = EMobWaterShape::Disc;
	DisplayName = LOCTEXT("DiscWater", "Water Disc");
}

UMobWaterBodyFactory::UMobWaterBodyFactory()
{
	NewActorClass = AMobWaterBody::StaticClass();
	bUseSurfaceOrientation = false;
}

void UMobWaterBodyFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	AMobWaterBody* Body = Cast<AMobWaterBody>(NewActor);
	if (!Body)
	{
		return;
	}

	if (UMobWaterSplineComponent* Spline = Body->GetSplineComponent())
	{
		Spline->SetClosedLoop(bClosed);

		// A starting shape, because an actor whose spline is two points on top of each other looks
		// broken rather than new. A square for a lake, a straight run for a river.
		Spline->ClearSplinePoints(false);

		if (bClosed)
		{
			Spline->AddSplinePoint(FVector(-500.f, -500.f, 0.f), ESplineCoordinateSpace::Local, false);
			Spline->AddSplinePoint(FVector(500.f, -500.f, 0.f), ESplineCoordinateSpace::Local, false);
			Spline->AddSplinePoint(FVector(500.f, 500.f, 0.f), ESplineCoordinateSpace::Local, false);
			Spline->AddSplinePoint(FVector(-500.f, 500.f, 0.f), ESplineCoordinateSpace::Local, false);
		}
		else
		{
			Spline->AddSplinePoint(FVector(-800.f, 0.f, 0.f), ESplineCoordinateSpace::Local, false);
			Spline->AddSplinePoint(FVector(0.f, 0.f, 0.f), ESplineCoordinateSpace::Local, false);
			Spline->AddSplinePoint(FVector(800.f, 0.f, 0.f), ESplineCoordinateSpace::Local, false);

			Spline->Widths = { MobWaterDefaultWidth, MobWaterDefaultWidth, MobWaterDefaultWidth };
		}

		Spline->UpdateSpline();
	}

	if (UMobWaterComponent* Water = Body->GetWaterComponent())
	{
		if (UMobWaterLookPreset* Look = LoadObject<UMobWaterLookPreset>(
			nullptr, TEXT("/MobWater/Looks/WL_MobWater_Realistic")))
		{
			Water->LookPreset = Look;
			Water->ApplyLookPreset();
		}
	}

	Body->RebuildSurface();
}

UMobWaterBodyFactory_Lake::UMobWaterBodyFactory_Lake()
{
	bClosed = true;
	DisplayName = LOCTEXT("WaterLake", "Water Lake");
}

UMobWaterBodyFactory_River::UMobWaterBodyFactory_River()
{
	bClosed = false;
	DisplayName = LOCTEXT("WaterRiver", "Water River");
}

UMobWaterOceanFactory::UMobWaterOceanFactory()
{
	NewActorClass = AMobWaterOcean::StaticClass();
	bUseSurfaceOrientation = false;
	DisplayName = LOCTEXT("WaterOcean", "Water Ocean");
}

void UMobWaterOceanFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	const AMobWaterOcean* Ocean = Cast<AMobWaterOcean>(NewActor);
	if (!Ocean)
	{
		return;
	}

	if (UMobWaterComponent* Water = Ocean->GetWaterComponent())
	{
		// The ocean's own wave preset rather than the shared look's, which is a lake's. A sea with a
		// pond's ripples on it is the one thing that cannot be fixed by tuning anything else.
		if (UMobWaterLookPreset* Look = LoadObject<UMobWaterLookPreset>(
			nullptr, TEXT("/MobWater/Looks/WL_MobWater_Realistic")))
		{
			Water->LookPreset = Look;
			Water->ApplyLookPreset();
		}

		if (UMobWaterWavePreset* Waves = LoadObject<UMobWaterWavePreset>(
			nullptr, TEXT("/MobWater/Waves/WP_MobWater_Ocean")))
		{
			Water->WavePreset = Waves;
		}

		// The baked sea, which is what makes an ocean read as one rather than as a large pond. Absent
		// until someone runs the bake, and the preset above still moves the surface without it.
		if (UMobWaterSpectrum* Sea = LoadObject<UMobWaterSpectrum>(
			nullptr, TEXT("/MobWater/Spectra/SP_MobWater_Ocean")))
		{
			Water->Spectrum = Sea;
		}

		Water->ApplySurface();
	}
}

UMobWaterFallFactory::UMobWaterFallFactory()
{
	NewActorClass = AMobWaterFall::StaticClass();
	bUseSurfaceOrientation = false;

	DisplayName = LOCTEXT("Waterfall", "Waterfall");
}

void UMobWaterFallFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	AMobWaterFall* Waterfall = Cast<AMobWaterFall>(NewActor);
	if (!Waterfall)
	{
		return;
	}

	if (UMobWaterFallSplineComponent* Lip = Waterfall->GetLipComponent())
	{
		// A straight lip across the drop, because an actor whose spline is two points on top of each
		// other looks broken rather than new, and as wide as a river arrives, because that is what it
		// will be running off.
		const float Half = MobWaterDefaultWidth * 0.5f;

		Lip->ClearSplinePoints(false);
		Lip->AddSplinePoint(FVector(0.f, -Half, 0.f), ESplineCoordinateSpace::Local, false);
		Lip->AddSplinePoint(FVector(0.f, Half, 0.f), ESplineCoordinateSpace::Local, false);
		Lip->SetClosedLoop(false, false);

		Lip->UpdateSpline();
	}

	if (UMobWaterFallComponent* Sheet = Waterfall->GetFallComponent())
	{
		// Dropped on a look for the same reason a pool is: the class defaults are a legal fall and
		// not a good looking one, and they would not match the water it is falling out of either.
		if (UMobWaterLookPreset* Look = LoadObject<UMobWaterLookPreset>(
			nullptr, TEXT("/MobWater/Looks/WL_MobWater_Realistic")))
		{
			Sheet->LookPreset = Look;
			Sheet->ApplyLookPreset();
		}
	}

	Waterfall->RebuildSurface();
}

UMobWaterExclusionFactory::UMobWaterExclusionFactory()
{
	NewActorClass = AMobWaterExclusion::StaticClass();
	bUseSurfaceOrientation = false;
	DisplayName = LOCTEXT("WaterExclusion", "Water Exclusion");
}

#undef LOCTEXT_NAMESPACE
