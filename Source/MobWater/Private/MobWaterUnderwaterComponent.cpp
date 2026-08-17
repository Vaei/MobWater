// Copyright (c) Jared Taylor

#include "MobWaterUnderwaterComponent.h"

#include "MobWaterModule.h"
#include "MobWaterSettings.h"
#include "MobWaterStatics.h"
#include "MobWaterTypes.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

namespace
{
	/**
	 * Custom primitive data the underwater plane carries.
	 *
	 * Its own small layout rather than MobWaterData: the plane is not a body of water and shares
	 * almost nothing with one, and giving it the surface's twenty-one slots to use three of them
	 * would make both layouts harder to read.
	 */
	namespace MobUnderwaterData
	{
		/** Linear RGB the water absorbs down to. Occupies 0, 1 and 2. */
		static constexpr int32 AbsorbColor = 0;

		/** How far light travels before it is gone. */
		static constexpr int32 Clarity = 3;

		/** 0 above the surface, 1 fully under. */
		static constexpr int32 Submersion = 4;
	}
}

UMobWaterUnderwaterComponent::UMobWaterUnderwaterComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	Mobility = EComponentMobility::Movable;

	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	CastShadow = false;
	bCastDynamicShadow = false;
	bAffectDistanceFieldLighting = false;
	bAffectDynamicIndirectLighting = false;
	bUseAsOccluder = false;
	bVisibleInRayTracing = false;
	bReceivesDecals = false;

	// Starts hidden. A camera that has never been in water should never have drawn this.
	SetHiddenInGame(true);
	SetVisibility(false);
}

void UMobWaterUnderwaterComponent::OnRegister()
{
	Super::OnRegister();

	if (UStaticMesh* Mesh = UMobWaterSettings::GetSurfaceMesh(EMobWaterShape::Box))
	{
		if (GetStaticMesh() != Mesh)
		{
			SetStaticMesh(Mesh);
		}
	}

	if (UMaterialInterface* Material = GetDefault<UMobWaterSettings>()->UnderwaterMaterial.LoadSynchronous())
	{
		if (GetMaterial(0) != Material)
		{
			SetMaterial(0, Material);
		}
	}

	ApplyPlacement();
}

void UMobWaterUnderwaterComponent::ApplyPlacement()
{
	// The plane's own mesh lies in XY facing up, and a camera looks down its own +X. Pitching it
	// ninety degrees stands it up across the view; without this it is edge on and invisible, which
	// looks exactly like the material having failed.
	SetRelativeLocationAndRotation(FVector(Distance, 0.f, 0.f), FRotator(90.f, 0.f, 0.f));
	SetRelativeScale3D(FVector(Size, Size, 1.f));
}

void UMobWaterUnderwaterComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// The parent's location, not the plane's: the plane is held in front of the camera, so asking
	// where the plane is asks about a point already fifteen centimetres into the scene.
	const USceneComponent* Parent = GetAttachParent();
	const FVector Eye = Parent ? Parent->GetComponentLocation() : GetComponentLocation();

	FMobWaterInfo Info;
	const bool bFound = UMobWaterStatics::GetWaterInfoAtLocation(this, Eye, Info);

	const float Wanted = bFound
		? FMath::Clamp(Info.ImmersionDepth / FMath::Max(CrossFadeDepth, 0.1f), 0.f, 1.f)
		: 0.f;

	Submersion = Wanted;

	const bool bNowSubmerged = Submersion > 0.f;
	if (bNowSubmerged != bSubmerged)
	{
		bSubmerged = bNowSubmerged;

		// Hidden rather than faded to nothing when dry. A transparent full-screen quad is still a
		// full-screen quad, and this renderer is fill bound before it is anything else.
		SetHiddenInGame(!bSubmerged);
		SetVisibility(bSubmerged);

		OnSubmergedChanged.Broadcast(bSubmerged);
	}

	if (!bSubmerged)
	{
		return;
	}

	SetCustomPrimitiveDataFloat(MobUnderwaterData::AbsorbColor, AbsorbColor.R);
	SetCustomPrimitiveDataFloat(MobUnderwaterData::AbsorbColor + 1, AbsorbColor.G);
	SetCustomPrimitiveDataFloat(MobUnderwaterData::AbsorbColor + 2, AbsorbColor.B);
	SetCustomPrimitiveDataFloat(MobUnderwaterData::Clarity, Clarity);
	SetCustomPrimitiveDataFloat(MobUnderwaterData::Submersion, Submersion);
}
