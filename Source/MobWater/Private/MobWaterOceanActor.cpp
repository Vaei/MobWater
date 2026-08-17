// Copyright (c) Jared Taylor

#include "MobWaterOceanActor.h"

#include "MobWaterComponent.h"
#include "MobWaterSubsystem.h"
#include "Components/BillboardComponent.h"

AMobWaterOcean::AMobWaterOcean()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Water = CreateDefaultSubobject<UMobWaterComponent>(TEXT("Water"));
	RootComponent = Water;

	Water->Shape = EMobWaterShape::Ocean;

	// Two kilometres across, and no bank. The extent is how far it is drawn rather than where it
	// stops being water, so the shore fade never engages and the waves keep their full height to the
	// horizon.
	Water->Extent = FVector2D(100000.0, 100000.0);
	Water->ShoreFadeDistance = 0.f;
	Water->Depth = 5000.f;

#if WITH_EDITORONLY_DATA
	Sprite = CreateDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Sprite)
	{
		Sprite->SetupAttachment(Water);
		Sprite->bIsScreenSizeScaled = true;
		Sprite->SetHiddenInGame(true);
		Sprite->SetUsingAbsoluteScale(true);
		MobWaterSprite::Apply(Sprite);
	}
#endif
}

void AMobWaterOcean::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	if (!bHasSurfaceZ)
	{
		SurfaceZ = GetActorLocation().Z;
		bHasSurfaceZ = true;
	}
}

void AMobWaterOcean::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bFollowView)
	{
		return;
	}

	const UMobWaterSubsystem* Subsystem = UMobWaterSubsystem::Get(this);
	if (!Subsystem)
	{
		return;
	}

	FVector ViewLocation;
	if (!Subsystem->GetViewLocation(ViewLocation))
	{
		return;
	}

	const double Snap = FMath::Max(FollowSnap, 1.f);

	// Snapped, and never in Z. The height of the sea is a level's decision; only the window moves.
	const FVector Wanted(
		FMath::GridSnap(ViewLocation.X, Snap),
		FMath::GridSnap(ViewLocation.Y, Snap),
		SurfaceZ);

	if (!GetActorLocation().Equals(Wanted, 0.5))
	{
		SetActorLocation(Wanted);

		// The surface's own Z is per-body data and the query reads it, so moving the actor without
		// this leaves buoyancy answering about wherever the ocean was when the level loaded.
		if (Water)
		{
			Water->ApplySurface();
		}
	}
}
