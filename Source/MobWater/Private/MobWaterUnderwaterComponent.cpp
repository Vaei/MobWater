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

		/**
		 * The surface normal at the eye. Occupies 5, 6 and 7.
		 *
		 * With the depth below it, this is the plane the waterline is drawn as. A normal rather than
		 * a height alone because the surface tilts, and a horizontal line across the view while a
		 * swell is passing is the tell that the waterline is a screen effect and not a surface.
		 */
		static constexpr int32 SurfaceNormal = 5;

		/** How far the eye is below that plane, in world units. */
		static constexpr int32 ImmersionDepth = 8;

		/** How tall the bead of water at the waterline is, in world units. */
		static constexpr int32 MeniscusThickness = 9;

		/** How much denser and brighter the bead is than the water behind it. */
		static constexpr int32 MeniscusStrength = 10;

		/** How bright the dappling coming down through the water is. */
		static constexpr int32 CausticStrength = 11;

		/** The world size the caustic web tiles over. */
		static constexpr int32 CausticScale = 12;

		/** How far down the dappling is lost, in world units. */
		static constexpr int32 CausticDepth = 13;
	}
}

UMobWaterUnderwaterComponent::UMobWaterUnderwaterComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// An editor world ticks nothing that has not asked for it, and an editor viewport flown under the
	// surface is the view this is most often looked at from.
	bTickInEditor = true;

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

	ApplyMaterial();
	ApplyPlacement();
}

void UMobWaterUnderwaterComponent::ApplyMaterial()
{
	const UMobWaterSettings* Settings = GetDefault<UMobWaterSettings>();

	UMaterialInterface* Material = bCaustics
		? Settings->UnderwaterCausticMaterial.LoadSynchronous()
		: nullptr;

	if (!Material)
	{
		Material = Settings->UnderwaterMaterial.LoadSynchronous();
	}

	if (Material && GetMaterial(0) != Material)
	{
		SetMaterial(0, Material);
	}
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

	// Brought in from a little above the surface rather than from it, so the bead is already on
	// screen as the eye goes under. Any further above and the quad's lower half would be absorbing
	// a view that is travelling through air to get there.
	const float Wanted = bFound
		? FMath::Clamp((Info.ImmersionDepth + MeniscusThickness) / FMath::Max(CrossFadeDepth, 0.1f), 0.f, 1.f)
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

	// The surface as a plane through the eye's own column. Exact to first order, which over a quad
	// two metres across at arm's length is exact enough that nothing could tell - and it costs a dot
	// product where evaluating the waves again here would cost the whole set a second time.
	const FVector Normal = Info.Normal.GetSafeNormal();

	SetCustomPrimitiveDataFloat(MobUnderwaterData::SurfaceNormal, static_cast<float>(Normal.X));
	SetCustomPrimitiveDataFloat(MobUnderwaterData::SurfaceNormal + 1, static_cast<float>(Normal.Y));
	SetCustomPrimitiveDataFloat(MobUnderwaterData::SurfaceNormal + 2, static_cast<float>(Normal.Z));
	SetCustomPrimitiveDataFloat(MobUnderwaterData::ImmersionDepth, Info.ImmersionDepth);

	SetCustomPrimitiveDataFloat(MobUnderwaterData::MeniscusThickness, MeniscusThickness);
	SetCustomPrimitiveDataFloat(MobUnderwaterData::MeniscusStrength, MeniscusStrength);

	SetCustomPrimitiveDataFloat(MobUnderwaterData::CausticStrength, bCaustics ? CausticStrength : 0.f);
	SetCustomPrimitiveDataFloat(MobUnderwaterData::CausticScale, CausticScale);
	SetCustomPrimitiveDataFloat(MobUnderwaterData::CausticDepth, CausticDepth);
}
