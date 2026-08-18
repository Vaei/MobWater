// Copyright (c) Jared Taylor

#include "MobWaterSpectrum.h"

#include "MobWaterModule.h"
#include "Engine/Texture2D.h"

#if WITH_EDITOR
#include "Misc/FileHelper.h"
#endif

namespace
{
	/** Bytes a texel, which is what the atlas holds and therefore what the table holds. */
	constexpr int32 SpectrumChannels = 4;

	/** X - Period * floor(X / Period), matching FMobWaterWaves::Repeat and the shader's own. */
	FORCEINLINE float Repeat(float X, float Period)
	{
		return X - Period * FMath::FloorToFloat(X / Period);
	}

	/** Positive modulo on a whole number, because a tile to the west of the origin is index -1. */
	FORCEINLINE int32 Wrap(int32 Value, int32 Period)
	{
		const int32 Rem = Value % Period;
		return Rem < 0 ? Rem + Period : Rem;
	}
}

bool UMobWaterSpectrum::IsUsable() const
{
	if (Resolution < 4 || Frames < 2 || TileSize <= 1.f || LoopPeriod <= 0.f)
	{
		return false;
	}

	return Samples.Num() == Resolution * Resolution * Frames * SpectrumChannels;
}

FVector3f UMobWaterSpectrum::SampleDisplacement(const FVector2f& WorldXY, float Time, float& OutFold) const
{
	OutFold = 0.f;

	if (!IsUsable())
	{
		return FVector3f::ZeroVector;
	}

	const float N = static_cast<float>(Resolution);
	const float F = static_cast<float>(Frames);

	// Texel coordinates inside one tile, which is where the shader's own bilinear lands once the half
	// texel it added to reach a centre is taken back off again.
	const float Px = Repeat(WorldXY.X / TileSize, 1.f) * N;
	const float Py = Repeat(WorldXY.Y / TileSize, 1.f) * N;

	const int32 X0 = Wrap(FMath::FloorToInt32(Px), Resolution);
	const int32 Y0 = Wrap(FMath::FloorToInt32(Py), Resolution);
	const int32 X1 = Wrap(X0 + 1, Resolution);
	const int32 Y1 = Wrap(Y0 + 1, Resolution);

	const float Fx = Px - FMath::FloorToFloat(Px);
	const float Fy = Py - FMath::FloorToFloat(Py);

	const float Ticks = Repeat(Time, LoopPeriod) / LoopPeriod * F;
	const float Whole = FMath::FloorToFloat(Ticks);
	const float Blend = Ticks - Whole;

	const int32 Frame0 = Wrap(static_cast<int32>(Whole), Frames);
	const int32 Frame1 = Wrap(Frame0 + 1, Frames);

	const uint8* Data = Samples.GetData();

	// Encoded, then blended, then decoded once. Decoding is affine, so the order costs nothing and
	// this is the order the shader takes - it lerps two texture reads and decodes the result.
	float Encoded[SpectrumChannels] = { 0.f, 0.f, 0.f, 0.f };

	const int32 FrameStride = Resolution * Resolution * SpectrumChannels;

	for (int32 Step = 0; Step < 2; ++Step)
	{
		const int32 Frame = Step == 0 ? Frame0 : Frame1;
		const float FrameWeight = Step == 0 ? 1.f - Blend : Blend;

		const int32 Base = Frame * FrameStride;

		const int32 Corners[4] =
		{
			Base + (Y0 * Resolution + X0) * SpectrumChannels,
			Base + (Y0 * Resolution + X1) * SpectrumChannels,
			Base + (Y1 * Resolution + X0) * SpectrumChannels,
			Base + (Y1 * Resolution + X1) * SpectrumChannels,
		};

		const float Weights[4] =
		{
			(1.f - Fx) * (1.f - Fy),
			Fx * (1.f - Fy),
			(1.f - Fx) * Fy,
			Fx * Fy,
		};

		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const float Weight = FrameWeight * Weights[Corner];
			for (int32 Channel = 0; Channel < SpectrumChannels; ++Channel)
			{
				Encoded[Channel] += Weight * (static_cast<float>(Data[Corners[Corner] + Channel]) / 255.f);
			}
		}
	}

	OutFold = Encoded[3];

	return FVector3f(
		(Encoded[0] * 2.f - 1.f) * HorizontalScale,
		(Encoded[1] * 2.f - 1.f) * HorizontalScale,
		(Encoded[2] * 2.f - 1.f) * VerticalScale);
}

FMobWaterSample UMobWaterSpectrum::Evaluate(const FVector2f& WorldXY, float Time) const
{
	FMobWaterSample Out;

	if (!IsUsable())
	{
		return Out;
	}

	float Fold = 0.f;
	Out.Displacement = SampleDisplacement(WorldXY, Time, Fold);
	Out.Fold = FMath::Clamp(Fold, 0.f, 1.f);

	const float Step = TileSize / static_cast<float>(Resolution);

	float Ignored = 0.f;
	const float HeightEast = SampleDisplacement(WorldXY + FVector2f(Step, 0.f), Time, Ignored).Z;
	const float HeightWest = SampleDisplacement(WorldXY - FVector2f(Step, 0.f), Time, Ignored).Z;
	const float HeightNorth = SampleDisplacement(WorldXY + FVector2f(0.f, Step), Time, Ignored).Z;
	const float HeightSouth = SampleDisplacement(WorldXY - FVector2f(0.f, Step), Time, Ignored).Z;

	Out.Normal = FVector3f(
		-(HeightEast - HeightWest) / (2.f * Step),
		-(HeightNorth - HeightSouth) / (2.f * Step),
		1.f).GetSafeNormal();

	return Out;
}

#if WITH_EDITOR
bool UMobWaterSpectrum::LoadSamplesFromFile(const FString& FilePath)
{
	TArray<uint8> Loaded;
	if (!FFileHelper::LoadFileToArray(Loaded, *FilePath))
	{
		UE_LOG(LogMobWater, Error, TEXT("Spectrum table %s could not be read."), *FilePath);
		return false;
	}

	const int32 Wanted = Resolution * Resolution * Frames * SpectrumChannels;
	if (Loaded.Num() != Wanted)
	{
		UE_LOG(LogMobWater, Error,
			TEXT("Spectrum table %s is %d bytes and %dx%d over %d frames wants %d. Set the geometry ")
			TEXT("on the asset before loading the table, or the query reads the field at the wrong scale."),
			*FilePath, Loaded.Num(), Resolution, Resolution, Frames, Wanted);
		return false;
	}

	Samples = MoveTemp(Loaded);
	MarkPackageDirty();

	return true;
}

void UMobWaterSpectrum::RecordBake(float InWindSpeed, float InWindDirection, float InChoppiness,
	float InRmsHeight, int32 InSeed)
{
	WindSpeed = InWindSpeed;
	WindDirection = InWindDirection;
	Choppiness = InChoppiness;
	RmsHeight = InRmsHeight;
	Seed = InSeed;

	MarkPackageDirty();
}
#endif

namespace MobWaterCombined
{
	FMobWaterSample Evaluate(const FMobWaterWaveParams& Params, const UMobWaterSpectrum* Spectrum,
		const FVector2f& SampleXY, float Time, const FMobWaterShoalField* Shoal)
	{
		FMobWaterSample Out = FMobWaterWaves::Evaluate(Params, SampleXY, Time, Shoal);

		if (!Spectrum || !Spectrum->IsUsable())
		{
			return Out;
		}

		const FMobWaterSample Baked = Spectrum->Evaluate(SampleXY, Time);

		Out.Displacement += Baked.Displacement;

		// The same blend MobWaterBlendNormals does, written out. Two normals cannot be added, and the
		// two files having the same answer matters more here than either answer being the better one.
		Out.Normal = FVector3f(
			Out.Normal.X + Baked.Normal.X,
			Out.Normal.Y + Baked.Normal.Y,
			FMath::Max(Out.Normal.Z * Baked.Normal.Z, 1e-4f)).GetSafeNormal();
		// The larger of the two, not their sum. Both measure the same thing, and adding them would move
		// what Crest Foam Threshold means the moment a body was given a sea state.
		Out.Fold = FMath::Clamp(FMath::Max(Out.Fold, Baked.Fold), 0.f, 1.f);

		return Out;
	}

	FMobWaterSample Surface(const FMobWaterWaveParams& Params, const UMobWaterSpectrum* Spectrum,
		const FVector2f& WorldXY, float Time, const FMobWaterShoalField* Shoal)
	{
		if (!Spectrum || !Spectrum->IsUsable())
		{
			return FMobWaterWaves::Surface(Params, WorldXY, Time, Shoal);
		}

		FVector2f Guess = WorldXY;
		for (int32 Step = 0; Step < MobWaterWaveConstants::SurfaceIterations; ++Step)
		{
			const FMobWaterSample Walk = Evaluate(Params, Spectrum, Guess, Time, Shoal);
			Guess = WorldXY - FVector2f(Walk.Displacement.X, Walk.Displacement.Y);
		}

		FMobWaterSample Out = Evaluate(Params, Spectrum, Guess, Time, Shoal);

		Out.Displacement.X = 0.f;
		Out.Displacement.Y = 0.f;

		return Out;
	}
}
