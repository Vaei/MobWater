// Copyright (c) Jared Taylor

#include "MobWaterBodyActor.h"

#include "MobWaterComponent.h"
#include "MobWaterModule.h"
#include "MobWaterSplineComponent.h"
#include "MobWaterSplineMesh.h"
#include "MobWaterTypes.h"
#include "Components/BillboardComponent.h"
#include "Engine/StaticMesh.h"

AMobWaterBody::AMobWaterBody()
{
	PrimaryActorTick.bCanEverTick = false;

	Spline = CreateDefaultSubobject<UMobWaterSplineComponent>(TEXT("Spline"));
	RootComponent = Spline;

	Water = CreateDefaultSubobject<UMobWaterComponent>(TEXT("Water"));
	Water->SetupAttachment(Spline);
	Water->Shape = EMobWaterShape::Spline;

#if WITH_EDITORONLY_DATA
	// A lake is a spline of thin curves and a surface with no thickness. Neither is easy to hit, and
	// clicking the surface selects the mesh rather than the actor that owns the shape.
	Sprite = CreateDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Sprite)
	{
		Sprite->SetupAttachment(Spline);
		Sprite->bIsScreenSizeScaled = true;
		Sprite->SetHiddenInGame(true);
		Sprite->SetUsingAbsoluteScale(true);
		MobWaterSprite::Apply(Sprite);
	}
#endif
}

void AMobWaterBody::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	// The mesh is transient, so it is gone every time the level is loaded and has to be made again.
	// Saving it instead would put a megabyte of triangles in the map for something that is a hundred
	// bytes of spline.
	RebuildSurface();
}

#if WITH_EDITOR
void AMobWaterBody::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	RebuildSurface();
}

void AMobWaterBody::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	// Only when the drag ends. A spline point being dragged fires this every frame, and remeshing a
	// sixty-four square grid per frame turns the handle to treacle.
	if (bFinished)
	{
		RebuildSurface();
	}
}
#endif

void AMobWaterBody::RebuildSurface()
{
	if (!Spline || !Water)
	{
		return;
	}

	if (!GeneratedMesh)
	{
		GeneratedMesh = NewObject<UStaticMesh>(this, NAME_None, RF_Transient);
	}

	if (!FMobWaterSplineMesh::Build(*Spline, *GeneratedMesh))
	{
		// Left alone rather than emptied. A spline mid-edit briefly has one point, and blinking the
		// water out every time someone adds one is worse than a frame of stale geometry.
		return;
	}

	Water->SetShoreSpline(Spline);
	Water->SetStaticMesh(GeneratedMesh);

	// The generated mesh is already in world-scale component space, so the component must not also
	// scale it the way a unit-sized pool mesh is scaled.
	Water->SetRelativeScale3D(FVector::OneVector);

	Water->ApplySurface();
	Water->MarkRenderStateDirty();
}
