// Copyright (c) Jared Taylor

#include "MobWaterUnderwaterComponent.h"

#include "MobWaterComponent.h"
#include "MobWaterModule.h"
#include "MobWaterSettings.h"
#include "MobWaterStatics.h"
#include "MobWaterSubsystem.h"
#include "MobWaterTypes.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
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

		/** The water's refractive index, which is the only number the window's size comes from. */
		static constexpr int32 SnellIndex = 14;

		/** How much of the window's edge is softened, as a fraction of its radius. */
		static constexpr int32 SnellFeather = 15;

		/** How bright the ring at the edge of the window is. */
		static constexpr int32 SnellRim = 16;

		/** How bright the world above is through the window. */
		static constexpr int32 SnellBrightness = 17;

		/**
		 * The tangent of half the capture's field of view.
		 *
		 * The tangent rather than the angle, because the shader wants only that: a capture that had
		 * to carry its view matrix here would want sixteen floats out of a contract that holds
		 * thirty six, and a capture locked to world up needs none of them.
		 */
		static constexpr int32 SnellCaptureTan = 18;

		/** How much thicker the water reads outside the window. */
		static constexpr int32 SnellMirror = 19;
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

int32 UMobWaterUnderwaterComponent::GetWantedVariant() const
{
	int32 Variant = 0;

	if (bCaustics)
	{
		Variant |= MobWaterUnderwaterVariant::Caustics;
	}

	if (bSnellWindow)
	{
		Variant |= MobWaterUnderwaterVariant::Window;

		if (WindowSource == EMobWaterWindowSource::SceneCapture)
		{
			Variant |= MobWaterUnderwaterVariant::Capture;
		}
	}

	return Variant;
}

void UMobWaterUnderwaterComponent::ApplyMaterial()
{
	const int32 Variant = GetWantedVariant();
	if (Variant == AppliedVariant)
	{
		return;
	}

	UMaterialInterface* Material = UMobWaterSettings::GetUnderwaterMaterial(Variant);
	if (!Material)
	{
		// Not recorded as applied, so a plane whose permutation has not been generated yet picks one
		// up the moment somebody generates it rather than staying blank for the level's life.
		return;
	}

	AppliedVariant = Variant;

	if (GetMaterial(0) != Material)
	{
		SetMaterial(0, Material);
	}
}

void UMobWaterUnderwaterComponent::HideWaterFromCapture()
{
	if (!Capture)
	{
		return;
	}

	Capture->HiddenComponents.Reset();

	// The plane itself. It is held in front of a camera that is somewhere else entirely, and a
	// capture that caught it edge on would have a bar of water across the sky.
	Capture->HiddenComponents.Add(this);

	HiddenBodies = 0;

	const UMobWaterSubsystem* Water = UMobWaterSubsystem::Get(this);
	if (!Water)
	{
		return;
	}

	// Every surface, not only the one the eye is in. The capture looks up from the waterline, so any
	// body of water at all above it is a ceiling over the view it exists to see past - and a wave
	// crest on the body it is standing in is above it by definition.
	for (const TWeakObjectPtr<UMobWaterComponent>& Body : Water->GetBodies())
	{
		if (UMobWaterComponent* Surface = Body.Get())
		{
			Capture->HiddenComponents.Add(Surface);
		}
	}

	HiddenBodies = Water->GetBodyCount();
}

void UMobWaterUnderwaterComponent::UpdateCapture(bool bWanted, const FVector& Eye, float SurfaceZ)
{
	if (!bWanted)
	{
		if (Capture)
		{
			Capture->DestroyComponent();
			Capture = nullptr;
			HiddenBodies = INDEX_NONE;
		}
		return;
	}

	UTextureRenderTarget2D* Target = GetDefault<UMobWaterSettings>()->SnellTarget.LoadSynchronous();
	if (!Target)
	{
		// Once. Without a target the window still draws, filled from whatever the sky slot holds, so
		// the only symptom is a backdrop where the level should be - which reads as the capture being
		// wrong rather than as there being nowhere to put it.
		static bool bSaid = false;
		if (!bSaid)
		{
			bSaid = true;
			UE_LOG(LogMobWater, Warning,
				TEXT("Snell's window asked for a scene capture and there is no Snell Target. Run Water > Generate Materials."));
		}
		return;
	}

	if (!Capture)
	{
		Capture = NewObject<USceneCaptureComponent2D>(this, TEXT("MobWaterSnellCapture"));
		Capture->SetupAttachment(this);
		Capture->bCaptureEveryFrame = true;
		Capture->bCaptureOnMovement = false;
		Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		Capture->ProjectionType = ECameraProjectionMode::Perspective;

		// Kept between frames, or the exposure starts from nothing every capture and the window
		// arrives black or blown depending on which way the adaptation was heading.
		Capture->bAlwaysPersistRenderingState = true;
		Capture->TextureTarget = Target;
		Capture->RegisterComponent();

		HiddenBodies = INDEX_NONE;
	}

	Capture->FOVAngle = FMath::Clamp(CaptureFov, 60.f, 170.f);

	// World up with no yaw, held to whatever the plane is attached to doing. That fixed orientation
	// is what lets a direction become a coordinate with two multiplies, instead of the shader being
	// handed a view matrix it has no room for.
	//
	// Just clear of the water rather than at the eye: the light bends where it leaves the surface,
	// and a capture down at the eye would be looking up through the water it exists to see past.
	Capture->SetWorldLocationAndRotation(
		FVector(Eye.X, Eye.Y, SurfaceZ + 5.f), FRotator(90.f, 0.f, 0.f));

	const UMobWaterSubsystem* Water = UMobWaterSubsystem::Get(this);
	if (HiddenBodies != (Water ? Water->GetBodyCount() : 0))
	{
		HideWaterFromCapture();
	}
}

#if WITH_EDITOR
void UMobWaterUnderwaterComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	ApplyMaterial();
	ApplyPlacement();
}
#endif

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
	ImmersionDepth = bFound ? FMath::Max(Info.ImmersionDepth, 0.f) : 0.f;

	const bool bNowSubmerged = Submersion > 0.f;
	if (bNowSubmerged != bSubmerged)
	{
		bSubmerged = bNowSubmerged;

		// Reset on the way in rather than on the way out, so it still reads as how deep the eye had
		// been for as long as anything cares to ask after it has surfaced.
		if (bSubmerged)
		{
			DeepestImmersion = 0.f;
		}

		// Hidden rather than faded to nothing when dry. A transparent full-screen quad is still a
		// full-screen quad, and this renderer is fill bound before it is anything else.
		SetHiddenInGame(!bSubmerged);
		SetVisibility(bSubmerged);

		OnSubmergedChanged.Broadcast(bSubmerged);
	}

	// A view of the world above is a second render of it, so it exists for exactly as long as
	// something is looking through the window and not one frame more.
	UpdateCapture(bSubmerged && bSnellWindow && WindowSource == EMobWaterWindowSource::SceneCapture,
		Eye, Eye.Z + ImmersionDepth);

	if (!bSubmerged)
	{
		return;
	}

	ApplyMaterial();

	DeepestImmersion = FMath::Max(DeepestImmersion, ImmersionDepth);

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

	SetCustomPrimitiveDataFloat(MobUnderwaterData::SnellIndex, FMath::Max(RefractionIndex, 1.f));
	SetCustomPrimitiveDataFloat(MobUnderwaterData::SnellFeather, WindowFeather);
	SetCustomPrimitiveDataFloat(MobUnderwaterData::SnellRim, RimStrength);
	SetCustomPrimitiveDataFloat(MobUnderwaterData::SnellBrightness, WindowBrightness);
	SetCustomPrimitiveDataFloat(MobUnderwaterData::SnellMirror, MirrorStrength);

	SetCustomPrimitiveDataFloat(MobUnderwaterData::SnellCaptureTan,
		FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(CaptureFov, 60.f, 170.f) * 0.5f)));
}
