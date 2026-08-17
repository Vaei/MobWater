// Copyright (c) Jared Taylor

#include "MobWaterComponent.h"

#include "MobWaterLookPreset.h"
#include "MobWaterModule.h"
#include "MobWaterSplineComponent.h"
#include "MobWaterSplineMesh.h"
#include "MobWaterSettings.h"
#include "MobWaterStatics.h"
#include "MobWaterSubsystem.h"
#include "MobWaterWavePreset.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "SceneTypes.h"

// A write past the end of the primitive's data is not an error, it is a no-op, and the material then
// reads zero forever with nothing logged. Three parameters were dead that way before this existed.
static_assert(MobWaterData::Num <= FCustomPrimitiveData::NumCustomPrimitiveDataFloats,
	"MobWaterData does not fit in the engine's custom primitive data. Pack two values into one slot "
	"rather than adding another - anything past the end is silently discarded.");

UMobWaterComponent::UMobWaterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	Mobility = EComponentMobility::Movable;

	// Everything the renderer would otherwise do with this. It is a surface that shades itself and
	// takes part in nothing else: it casts no shadow, occludes nothing, and contributes to no
	// lighting build. A water plane that showed up in any of those would be a plane, not water.
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	CastShadow = false;
	bCastDynamicShadow = false;
	bAffectDistanceFieldLighting = false;
	bAffectDynamicIndirectLighting = false;
	bUseAsOccluder = false;
	bVisibleInRayTracing = false;
	bReceivesDecals = false;
}

void UMobWaterComponent::OnRegister()
{
	Super::OnRegister();

	if (UMobWaterSubsystem* Subsystem = UMobWaterSubsystem::Get(this))
	{
		Subsystem->RegisterBody(this);
	}

	ApplySurface();
}

void UMobWaterComponent::OnUnregister()
{
	if (UMobWaterSubsystem* Subsystem = UMobWaterSubsystem::Get(this))
	{
		Subsystem->UnregisterBody(this);
	}

	Super::OnUnregister();
}

#if WITH_EDITOR
void UMobWaterComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Choosing a preset applies it there and then, so the details panel fills in with what was
	// chosen. Applying it on every change instead would undo each edit as it was made.
	static const FName LookPresetName = GET_MEMBER_NAME_CHECKED(UMobWaterComponent, LookPreset);

	if (PropertyChangedEvent.GetPropertyName() == LookPresetName)
	{
		ApplyLookPreset();
		return;
	}

	ApplySurface();
}
#endif

int32 UMobWaterComponent::WantedVariant() const
{
	int32 Variant = 0;

	if (bFoam)
	{
		Variant |= MobWaterVariant::Foam;

		if (bFoamTexture)
		{
			Variant |= MobWaterVariant::FoamTexture;
		}
	}
	if (bRefraction)
	{
		Variant |= MobWaterVariant::Refraction;
	}
	if (bRipples)
	{
		Variant |= MobWaterVariant::Ripples;
	}

	return Variant;
}

void UMobWaterComponent::SetShoreSpline(UMobWaterSplineComponent* InSpline)
{
	ShoreSpline = InSpline;
}

void UMobWaterComponent::ApplyLookPreset()
{
	if (!LookPreset)
	{
		return;
	}

	ShallowColor = LookPreset->ShallowColor;
	DeepColor = LookPreset->DeepColor;
	FadeDepth = LookPreset->FadeDepth;
	ClarityDepth = LookPreset->ClarityDepth;
	MinOpacity = LookPreset->MinOpacity;
	Unlit = LookPreset->Unlit;
	Roughness = LookPreset->Roughness;
	DetailStrength = LookPreset->DetailStrength;
	DetailScrollSpeed = LookPreset->DetailScrollSpeed;
	MacroStrength = LookPreset->MacroStrength;
	EdgeFoamWidth = LookPreset->EdgeFoamWidth;

	bFoam = LookPreset->bFoam;
	ShoreFoamDepth = LookPreset->ShoreFoamDepth;
	CrestFoamThreshold = LookPreset->CrestFoamThreshold;
	FoamNoiseAmount = LookPreset->FoamNoiseAmount;
	FoamOpacity = LookPreset->FoamOpacity;
	bFoamTexture = LookPreset->bFoamTexture;
	FoamTextureOpacity = LookPreset->FoamTextureOpacity;
	FoamSharpness = LookPreset->FoamSharpness;
	FoamBands = LookPreset->FoamBands;
	FoamBandSeparation = LookPreset->FoamBandSeparation;

	GlintGloss = LookPreset->GlintGloss;
	GlintStrength = LookPreset->GlintStrength;
	GlintThreshold = LookPreset->GlintThreshold;
	GlintDensity = LookPreset->GlintDensity;
	GlintEmissive = LookPreset->GlintEmissive;

	bCaustics = LookPreset->bCaustics;
	CausticStrength = LookPreset->CausticStrength;
	CausticDepth = LookPreset->CausticDepth;

	bRefraction = LookPreset->bRefraction;
	RefractionStrength = LookPreset->RefractionStrength;
	ReflectionStrength = LookPreset->ReflectionStrength;

	bRipples = LookPreset->bRipples;
	RippleStrength = LookPreset->RippleStrength;

	if (LookPreset->Waves)
	{
		WavePreset = LookPreset->Waves;
	}

	ApplySurface();
	MarkRenderStateDirty();
}

void UMobWaterComponent::ApplySurface()
{
	// A spline body brings its own mesh, generated from the shape that was drawn, and its own scale
	// with it. Taking the settings' unit plane here would replace the lake with a square.
	if (Shape != EMobWaterShape::Spline)
	{
		if (UStaticMesh* Mesh = UMobWaterSettings::GetSurfaceMesh(Shape))
		{
			if (GetStaticMesh() != Mesh)
			{
				SetStaticMesh(Mesh);
			}
		}
	}

	if (UMaterialInterface* Material = UMobWaterSettings::GetMaterial(Shape, WantedVariant()))
	{
		if (GetMaterial(0) != Material)
		{
			SetMaterial(0, Material);
		}

		ApplyFoamTexture(Material);
	}

	// A spline body generates its mesh at world size, and an ocean's is built at world size too -
	// scaling either would be scaling something that is already the right size.
	if (Shape != EMobWaterShape::Spline && Shape != EMobWaterShape::Ocean)
	{
		// The mesh is a unit square centred on the origin, so the component's own scale is the body's
		// size. An artist sets an extent and never touches a transform.
		const FVector Scale(FMath::Max(Extent.X, 1.0) * 2.0, FMath::Max(Extent.Y, 1.0) * 2.0, 1.0);
		if (!GetRelativeScale3D().Equals(Scale))
		{
			SetRelativeScale3D(Scale);
		}
	}

	WriteWaterData3(MobWaterData::ShallowColor, ShallowColor);
	WriteWaterData3(MobWaterData::DeepColor, DeepColor);
	WriteWaterData(MobWaterData::FadeDepth, FadeDepth);
	WriteWaterData(MobWaterData::ClarityDepth, ClarityDepth);
	WriteWaterData(MobWaterData::MinOpacity, MinOpacity);
	WriteWaterData(MobWaterData::Unlit, Unlit);
	WriteWaterData(MobWaterData::WaveAmplitude, WaveAmplitude);
	WriteWaterData(MobWaterData::ShoreFadeDistance, ShoreFadeDistance);
	WriteWaterData(MobWaterData::Roughness, Roughness);
	WriteWaterData2(MobWaterData::HalfExtent, Extent);
	WriteWaterData2(MobWaterData::FlowVelocity, GetWorldFlowVelocity());

	// A feature that is off writes zero rather than its authored value. An overridden material that
	// still carries the switch would otherwise render it at whatever the details panel last held.
	//
	// Not capped against the body's depth. That cap was added when foam appeared to cover whole
	// ponds, and the cause turned out to be elsewhere - all it does now is stop anyone deliberately
	// running a wide foam margin, which is exactly what banded stylized foam needs.
	WriteWaterData(MobWaterData::ShoreFoamDepth, bFoam ? ShoreFoamDepth : 0.f);
	// Edge line width and foam opacity share a slot: hundredths in the whole part, opacity in the
	// fraction. Held under one so the width still floors out of it.
	const float EdgeWidth = FMath::RoundToFloat(FMath::Clamp(EdgeFoamWidth, 0.f, 1.f) * 100.f);
	const float FoamShown = bFoam ? FMath::Clamp(FoamOpacity, 0.f, 0.999f) : 0.f;
	WriteWaterData(MobWaterData::EdgeFoamWidth, EdgeWidth + FoamShown);
	WriteWaterData(MobWaterData::CrestFoamThreshold, bFoam ? CrestFoamThreshold : 1.f);
	// Edge wander and foam texture share a slot: hundredths in the whole part, texture in the
	// fraction. Held under one so the wander still floors out of it.
	const float Wander = FMath::RoundToFloat(FMath::Clamp(FoamNoiseAmount, 0.f, 1.f) * 100.f);
	const float Textured = FMath::Clamp(FoamTextureOpacity, 0.f, 0.999f);
	WriteWaterData(MobWaterData::FoamNoiseAmount, Wander + Textured);
	WriteWaterData(MobWaterData::FoamSharpness, FoamSharpness);
	// Band count and band gap share a slot: whole part the count, fraction the gap. The fraction is
	// held under one so the count still floors out of it.
	const float BandCount = bFoam ? FMath::FloorToFloat(FMath::Max(FoamBands, 0.f)) : 0.f;
	const float BandGap = BandCount >= 1.f ? FMath::Clamp(FoamBandSeparation, 0.f, 0.95f) : 0.f;
	WriteWaterData(MobWaterData::FoamBands, BandCount + BandGap);
	WriteWaterData(MobWaterData::GlintGloss, GlintGloss);
	WriteWaterData(MobWaterData::GlintStrength, GlintStrength);
	WriteWaterData(MobWaterData::GlintThreshold, GlintThreshold);
	WriteWaterData(MobWaterData::GlintDensity, GlintDensity);
	WriteWaterData(MobWaterData::GlintEmissive, GlintEmissive);
	WriteWaterData(MobWaterData::RefractionStrength, bRefraction ? RefractionStrength : 0.f);
	WriteWaterData(MobWaterData::ReflectionStrength, ReflectionStrength);
	WriteWaterData(MobWaterData::RippleStrength, bRipples ? RippleStrength : 0.f);
	WriteWaterData(MobWaterData::DetailStrength, DetailStrength);
	WriteWaterData(MobWaterData::DetailScrollSpeed, DetailScrollSpeed);
	WriteWaterData(MobWaterData::MacroStrength, MacroStrength);
	WriteWaterData(MobWaterData::CausticStrength, bCaustics ? CausticStrength : 0.f);
	WriteWaterData(MobWaterData::CausticDepth, CausticDepth);
}

void UMobWaterComponent::ApplyFoamTexture(UMaterialInterface* Shared)
{
	// The one setting on this component that cannot travel as primitive data, so it is the one that
	// costs a material. The instance is made only when a texture is actually set, and dropped again
	// when it is cleared, so a body that leaves it alone keeps batching with every other body.
	const bool bWanted = bFoam && bFoamTexture && FoamTexture != nullptr;

	if (!bWanted)
	{
		if (FoamMaterial)
		{
			FoamMaterial = nullptr;
			SetMaterial(0, Shared);
		}
		return;
	}

	if (!FoamMaterial || FoamMaterial->Parent != Shared)
	{
		FoamMaterial = CreateDynamicMaterialInstance(0, Shared);
	}

	if (FoamMaterial)
	{
		FoamMaterial->SetTextureParameterValue(TEXT("FoamTexture"), FoamTexture);
	}
}

void UMobWaterComponent::WriteWaterData(int32 Index, float Value)
{
	const UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		SetCustomPrimitiveDataFloat(Index, Value);
		return;
	}

	SetDefaultCustomPrimitiveDataFloat(Index, Value);
	SetCustomPrimitiveDataFloat(Index, Value);
}

void UMobWaterComponent::WriteWaterData2(int32 Index, const FVector2D& Value)
{
	WriteWaterData(Index, static_cast<float>(Value.X));
	WriteWaterData(Index + 1, static_cast<float>(Value.Y));
}

void UMobWaterComponent::WriteWaterData3(int32 Index, const FLinearColor& Value)
{
	WriteWaterData(Index, Value.R);
	WriteWaterData(Index + 1, Value.G);
	WriteWaterData(Index + 2, Value.B);
}

const FMobWaterWaveParams& UMobWaterComponent::GetWaveParams() const
{
	if (WavePreset)
	{
		return WavePreset->Waves;
	}

	static const FMobWaterWaveParams Empty;

	const UMobWaterSubsystem* Subsystem = UMobWaterSubsystem::Get(this);
	return Subsystem ? Subsystem->GetDefaultWaves() : Empty;
}

bool UMobWaterComponent::ContainsLocation(const FVector& Location) const
{
	if (Shape == EMobWaterShape::Spline)
	{
		const UMobWaterSplineComponent* Spline = ShoreSpline.Get();
		return Spline && Spline->GetDistanceInside(Location) > 0.f;
	}

	const FVector Local = GetComponentTransform().InverseTransformPosition(Location);

	// Against the unscaled extent rather than the component's bounds, because the bounds grow with
	// whatever the waves are doing and a point would drift in and out of the body as one passed.
	const FVector2D Offset(Local.X * Extent.X * 2.0, Local.Y * Extent.Y * 2.0);

	if (Shape == EMobWaterShape::Disc)
	{
		const double Radius = FMath::Min(Extent.X, Extent.Y);
		return Offset.SizeSquared() <= Radius * Radius;
	}

	return FMath::Abs(Offset.X) <= Extent.X && FMath::Abs(Offset.Y) <= Extent.Y;
}

float UMobWaterComponent::GetShoreFade(const FVector& Location) const
{
	if (Shape == EMobWaterShape::Spline)
	{
		const UMobWaterSplineComponent* Spline = ShoreSpline.Get();
		if (!Spline)
		{
			return 1.f;
		}

		// Clamped to the same reference the vertex colour is stored against, so the query and the
		// vertices agree even where the shader has run out of range to describe.
		const float Inside = FMath::Min(Spline->GetDistanceInside(Location), MOB_WATER_SHORE_REFERENCE);
		return FMobWaterWaves::ShoreAttenuation(FMath::Max(Inside, 0.f), ShoreFadeDistance);
	}

	const FVector Local = GetComponentTransform().InverseTransformPosition(Location);
	const FVector2D Offset(Local.X * Extent.X * 2.0, Local.Y * Extent.Y * 2.0);

	float EdgeDistance;
	if (Shape == EMobWaterShape::Disc)
	{
		const double Radius = FMath::Min(Extent.X, Extent.Y);
		EdgeDistance = static_cast<float>(Radius - Offset.Size());
	}
	else
	{
		EdgeDistance = static_cast<float>(FMath::Min(
			Extent.X - FMath::Abs(Offset.X),
			Extent.Y - FMath::Abs(Offset.Y)));
	}

	return FMobWaterWaves::ShoreAttenuation(FMath::Max(EdgeDistance, 0.f), ShoreFadeDistance);
}

FMobWaterInfo UMobWaterComponent::GetWaterInfoAtLocation(const FVector& Location) const
{
	if (!bCpuQueries || !ContainsLocation(Location))
	{
		return FMobWaterInfo();
	}

	const UMobWaterSubsystem* Subsystem = UMobWaterSubsystem::Get(this);
	if (!Subsystem)
	{
		return FMobWaterInfo();
	}

	FMobWaterWaveParams Params = GetWaveParams();

	// The per-body amplitude is applied here and in the shader's custom primitive data, and it has to
	// be the same multiply in both or the query answers a different surface from the one drawn.
	Params.AmplitudeScale *= WaveAmplitude;

	FMobWaterInfo Info = UMobWaterStatics::EvaluateWaterAtNative(
		Params, Location,
		static_cast<float>(GetComponentLocation().Z),
		Depth,
		GetShoreFade(Location),
		Subsystem->GetWaterTime());

	Info.FlowVelocity = GetWorldFlowVelocity();

	return Info;
}

FVector2D UMobWaterComponent::GetWorldFlowVelocity() const
{
	if (FlowSpace == EMobWaterSpace::World)
	{
		return FlowVelocity;
	}

	const FVector Rotated = GetComponentTransform().TransformVectorNoScale(
		FVector(FlowVelocity.X, FlowVelocity.Y, 0.0));

	return FVector2D(Rotated.X, Rotated.Y);
}
