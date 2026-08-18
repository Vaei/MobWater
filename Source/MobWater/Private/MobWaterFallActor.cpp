// Copyright (c) Jared Taylor

#include "MobWaterFallActor.h"

#include "MobWaterDisturbanceComponent.h"
#include "MobWaterFallComponent.h"
#include "MobWaterFallMesh.h"
#include "MobWaterFallSplineComponent.h"
#include "MobWaterTypes.h"
#include "Components/BillboardComponent.h"
#include "Engine/StaticMesh.h"

AMobWaterFall::AMobWaterFall()
{
	PrimaryActorTick.bCanEverTick = false;

	Lip = CreateDefaultSubobject<UMobWaterFallSplineComponent>(TEXT("Lip"));
	RootComponent = Lip;

	Fall = CreateDefaultSubobject<UMobWaterFallComponent>(TEXT("Fall"));
	Fall->SetupAttachment(Lip);

	Plunge = CreateDefaultSubobject<UMobWaterDisturbanceComponent>(TEXT("Plunge"));
	Plunge->SetupAttachment(Lip);

	// A fall never stops arriving, so the push it makes never stops either. This is the case the
	// disturbance component's persistent flag exists for.
	Plunge->bPersistent = true;
	Plunge->Strength = 0.3f;
	Plunge->Foam = 0.35f;

#if WITH_EDITORONLY_DATA
	// A lip is a thin curve and a sheet has no thickness. Neither is easy to hit, and clicking the
	// water selects the mesh rather than the actor that owns the shape.
	Sprite = CreateDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Sprite)
	{
		Sprite->SetupAttachment(Lip);
		Sprite->bIsScreenSizeScaled = true;
		Sprite->SetHiddenInGame(true);
		Sprite->SetUsingAbsoluteScale(true);
		MobWaterSprite::Apply(Sprite);
	}
#endif
}

void AMobWaterFall::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	// The mesh is transient, so it is gone every time the level is loaded and has to be made again.
	RebuildSurface();
}

#if WITH_EDITOR
void AMobWaterFall::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	RebuildSurface();
}

void AMobWaterFall::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	// Only when the drag ends, exactly as a spline body remeshes. A point being dragged fires this
	// every frame and the handle turns to treacle.
	if (bFinished)
	{
		RebuildSurface();
	}
}
#endif

void AMobWaterFall::RebuildSurface()
{
	if (!Lip || !Fall)
	{
		return;
	}

	if (!GeneratedMesh)
	{
		GeneratedMesh = NewObject<UStaticMesh>(this, NAME_None, RF_Transient);
	}

	// Set before the mesh is built, because the sheet's size reaches the material as data and the
	// component has to be able to ask the lip for it the first time it applies.
	Fall->SetLip(Lip);

	if (!FMobWaterFallMesh::Build(*Lip, *GeneratedMesh))
	{
		return;
	}

	Fall->SetStaticMesh(GeneratedMesh);

	// The generated mesh is already in world-scale component space, so the component must not also
	// scale it the way a unit-sized pool mesh is scaled.
	Fall->SetRelativeScale3D(FVector::OneVector);

	Fall->ApplySurface();
	Fall->MarkRenderStateDirty();

	if (!Plunge)
	{
		return;
	}

	FVector Landing;
	float Radius;

	if (bPlunge && Fall->GetPlunge(Landing, Radius))
	{
		Plunge->SetWorldLocation(Landing);
		Plunge->Radius = Radius;
		Plunge->SetActive(true);
	}
	else
	{
		Plunge->SetActive(false);
	}
}
