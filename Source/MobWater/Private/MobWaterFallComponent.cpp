// Copyright (c) Jared Taylor

#include "MobWaterFallComponent.h"

#include "MobWaterFallSplineComponent.h"
#include "MobWaterInfo.h"
#include "MobWaterLookPreset.h"
#include "MobWaterSettings.h"
#include "MobWaterStatics.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "SceneTypes.h"

static_assert(MobWaterFallData::Num <= FCustomPrimitiveData::NumCustomPrimitiveDataFloats,
	"MobWaterFallData does not fit in the engine's custom primitive data. Pack two values into one "
	"slot rather than adding another - anything past the end is silently discarded.");

UMobWaterFallComponent::UMobWaterFallComponent()
{
	// The lip join is the only reason this ticks, and it has to happen in the viewport as well: a
	// fall is placed against a river that is already moving, and one that only joined up in play
	// would be authored against a seam that is not there at runtime.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
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
}

void UMobWaterFallComponent::OnRegister()
{
	Super::OnRegister();

	ApplySurface();
}

#if WITH_EDITOR
void UMobWaterFallComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	static const FName LookPresetName = GET_MEMBER_NAME_CHECKED(UMobWaterFallComponent, LookPreset);

	if (PropertyChangedEvent.GetPropertyName() == LookPresetName)
	{
		ApplyLookPreset();
		return;
	}

	ApplySurface();
}
#endif

void UMobWaterFallComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	TickLipJoin();
}

void UMobWaterFallComponent::UpdateLipJoin()
{
	TickLipJoin();
}

void UMobWaterFallComponent::SetLip(UMobWaterFallSplineComponent* InLip)
{
	Lip = InLip;
}

int32 UMobWaterFallComponent::WantedVariant() const
{
	int32 Variant = 0;

	if (bFoam)
	{
		Variant |= MobWaterFallVariant::Foam;
	}
	if (bRefraction)
	{
		Variant |= MobWaterFallVariant::Refraction;
	}
	if (bGradientColor)
	{
		Variant |= MobWaterFallVariant::Gradient;
	}

	return Variant;
}

void UMobWaterFallComponent::ApplyLookPreset()
{
	if (!LookPreset)
	{
		return;
	}

	// The parts of a look a fall has. A shoreline, a caustic, a ripple and a wave band are read and
	// ignored rather than approximated, because there is nothing here for any of them to happen on -
	// and a fall that invented an approximation of a shoreline would stop matching the river it came
	// out of, which is the entire reason it takes the preset at all.
	bGradientColor = LookPreset->bGradientColor;
	GradientRow = LookPreset->GradientRow;
	ThinColor = LookPreset->ShallowColor;
	ThickColor = LookPreset->DeepColor;
	Unlit = LookPreset->Unlit;
	Roughness = LookPreset->Roughness;
	DetailStrength = LookPreset->DetailStrength;

	bFoam = LookPreset->bFoam;
	FoamAmount = LookPreset->FoamOpacity;
	FoamSharpness = LookPreset->FoamSharpness;

	GlintGloss = LookPreset->GlintGloss;
	GlintStrength = LookPreset->GlintStrength;

	bRefraction = LookPreset->bRefraction;
	RefractionStrength = LookPreset->RefractionStrength;
	ReflectionStrength = LookPreset->ReflectionStrength;

	ApplySurface();
	MarkRenderStateDirty();
}

void UMobWaterFallComponent::ApplySurface()
{
	if (UMaterialInterface* Material = UMobWaterSettings::GetFallMaterial(WantedVariant()))
	{
		if (GetMaterial(0) != Material)
		{
			SetMaterial(0, Material);
		}
	}

	const UMobWaterFallSplineComponent* Course = Lip.Get();

	// The sheet's own size, which the mesh cannot say: the vertices carry a unit UV so the material
	// can address them, and how many world units that UV covers is not in the geometry.
	const float Width = Course ? FMath::Max(Course->GetSplineLength(), 1.f) : 100.f;
	const float Height = Course ? Course->GetMaxDrop() : 100.f;

	if (bGradientColor)
	{
		WriteFallData(MobWaterFallData::GradientRow, static_cast<float>(GradientRow));
	}
	else
	{
		WriteFallData3(MobWaterFallData::ThinColor, ThinColor);
	}
	WriteFallData3(MobWaterFallData::ThickColor, ThickColor);

	WriteFallData(MobWaterFallData::Thickness, Thickness);
	WriteFallData(MobWaterFallData::ClarityDepth, ClarityDepth);
	WriteFallData(MobWaterFallData::MinOpacity, MinOpacity);
	WriteFallData(MobWaterFallData::Unlit, Unlit);
	WriteFallData(MobWaterFallData::Roughness, Roughness);
	WriteFallData(MobWaterFallData::DetailStrength, DetailStrength);
	WriteFallData2(MobWaterFallData::Size, FVector2D(Width, Height));
	WriteFallData(MobWaterFallData::LipSpeed, LipSpeed);
	WriteFallData(MobWaterFallData::Gravity, Gravity);
	WriteFallData2(MobWaterFallData::StreakSize, FVector2D(StreakLength, StreakWidth));
	WriteFallData(MobWaterFallData::ThinAmount, ThinAmount);
	WriteFallData(MobWaterFallData::Breakup, Breakup);
	WriteFallData(MobWaterFallData::EdgeFade, EdgeFade);

	// Amount and sharpness share a slot: hundredths of the amount in the whole part, the sharpness
	// over its own range in the fraction. Packed for the same reason the surface packs its pairs.
	const float Amount = FMath::RoundToFloat(FMath::Clamp(bFoam ? FoamAmount : 0.f, 0.f, 1.f) * 100.f);
	const float Hardness = FMath::Min(
		FMath::Clamp(FoamSharpness, 1.f, MobWaterFallFoam::SharpnessRange) / MobWaterFallFoam::SharpnessRange,
		0.9995f);

	WriteFallData(MobWaterFallData::FoamAmount, Amount + Hardness);
	WriteFallData(MobWaterFallData::LipFoam, bFoam ? LipFoam : 0.f);
	WriteFallData(MobWaterFallData::BaseFoam, bFoam ? BaseFoam : 0.f);

	WriteFallData(MobWaterFallData::GlintGloss, GlintGloss);
	WriteFallData(MobWaterFallData::GlintStrength, GlintStrength);
	WriteFallData(MobWaterFallData::ReflectionStrength, ReflectionStrength);
	WriteFallData(MobWaterFallData::LipFade, LipFade);
	WriteFallData(MobWaterFallData::RefractionStrength, bRefraction ? RefractionStrength : 0.f);

	// Cleared here rather than left to the tick, which is about to be turned off and would never run
	// to clear it. A join switched off has to put the lip back where the mesh has it.
	if (!bJoinToWater)
	{
		LipOffsets = FVector2D::ZeroVector;
	}

	// Written here as well as on tick, so a fall dropped into a level and never played is not drawn
	// with whatever the last one left behind.
	WriteFallData2(MobWaterFallData::LipOffset, LipOffsets);

	// The join is the only reason this ticks, so a fall that is not joined to anything stops ticking
	// rather than reaching the early return every frame. A cliff face of them costs nothing.
	PrimaryComponentTick.SetTickFunctionEnable(bJoinToWater);
}

void UMobWaterFallComponent::TickLipJoin()
{
	const UMobWaterFallSplineComponent* Course = Lip.Get();

	if (!bJoinToWater || !Course || Course->GetNumberOfSplinePoints() < 2)
	{
		if (!LipOffsets.IsNearlyZero())
		{
			LipOffsets = FVector2D::ZeroVector;
			WriteFallData2(MobWaterFallData::LipOffset, LipOffsets);
		}
		return;
	}

	const float Length = Course->GetSplineLength();

	// The two ends, and only the two ends. A lip is a line and a swell reaches one end of it before
	// the other, so one offset would hold the join in the middle and open it at both sides. Two is a
	// straight fit across the lip, which is exact wherever the lip is shorter than the wave crossing
	// it - and a lip longer than that is a fall the size of a coastline.
	auto OffsetAt = [this](const FVector& At) -> float
	{
		FMobWaterInfo Info;
		if (!UMobWaterStatics::GetWaterInfoAtLocation(this, At, Info) || !Info.bValid)
		{
			return 0.f;
		}

		const float Offset = Info.SurfaceZ - static_cast<float>(At.Z);

		// Refused rather than clamped when it is further than the lip may move. A query finds the
		// nearest surface under the point when there is none over it, so a fall on a ledge answers
		// with whatever is at the bottom of the cliff, and clamping that drags the sheet by the full
		// limit for water it is not coming out of at all.
		return FMath::Abs(Offset) <= MaxLipOffset ? Offset : 0.f;
	};

	const FVector Start = Course->GetLocationAtDistanceAlongSpline(0.f, ESplineCoordinateSpace::World);
	const FVector End = Course->GetLocationAtDistanceAlongSpline(Length, ESplineCoordinateSpace::World);

	const FVector2D Wanted(OffsetAt(Start), OffsetAt(End));

	// Only when it has actually moved. Primitive data is a render command, and a still river writing
	// the same two zeroes every frame would be issuing one for nothing.
	if (!LipOffsets.Equals(Wanted, 0.01))
	{
		LipOffsets = Wanted;
		WriteFallData2(MobWaterFallData::LipOffset, LipOffsets);
	}
}

bool UMobWaterFallComponent::GetPlunge(FVector& OutLocation, float& OutRadius) const
{
	const UMobWaterFallSplineComponent* Course = Lip.Get();
	if (!Course || Course->GetNumberOfSplinePoints() < 2)
	{
		return false;
	}

	const float Length = Course->GetSplineLength();
	const float Middle = Length * 0.5f;

	const FVector Top = Course->GetLocationAtDistanceAlongSpline(Middle, ESplineCoordinateSpace::World);
	const FVector Downstream = Course->GetDownstreamAtDistance(Middle);

	OutLocation = Top
		- FVector::UpVector * Course->GetDropAtDistance(Middle)
		+ Downstream * Course->Overhang;

	// One push rather than a line of them. Stamps add where they overlap, so a row of disturbers
	// along the base builds a mound instead of a churn - the field's own documentation says so - and
	// a fall wanting a line of separate impacts is a fall with more than one lip.
	OutRadius = FMath::Clamp(Length * 0.5f, 60.f, 500.f);

	return true;
}

void UMobWaterFallComponent::WriteFallData(int32 Index, float Value)
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

void UMobWaterFallComponent::WriteFallData2(int32 Index, const FVector2D& Value)
{
	WriteFallData(Index, static_cast<float>(Value.X));
	WriteFallData(Index + 1, static_cast<float>(Value.Y));
}

void UMobWaterFallComponent::WriteFallData3(int32 Index, const FLinearColor& Value)
{
	WriteFallData(Index, Value.R);
	WriteFallData(Index + 1, Value.G);
	WriteFallData(Index + 2, Value.B);
}
