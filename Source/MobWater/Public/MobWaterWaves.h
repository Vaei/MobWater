// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "MobWaterWaves.generated.h"

/**
 * The constants the wave maths is written against.
 *
 * Duplicated in Shaders/Public/MobWaterWaves.ush, which is the whole reason mob_water_verify exists:
 * this header and that file are one algorithm written twice, and nothing but a test keeps them equal.
 */
namespace MobWaterWaveConstants
{
	/** Centimetres per second squared, because a wave's speed comes from gravity and UE is in cm. */
	static constexpr float Gravity = 980.f;

	/**
	 * How many waves a body may carry.
	 *
	 * The shader unrolls to this count, so raising it is a shader change and not only a data one.
	 */
	static constexpr int32 MaxWaves = 8;

	/**
	 * How many times the surface query walks back towards the point it was asked about.
	 *
	 * A Gerstner wave moves a vertex sideways as well as up, so the height above a world XY is not the
	 * height of the vertex that started there. Four steps closes the gap to well under a millimetre at
	 * the steepness the clamp allows, and being a fixed count it costs the same on every machine -
	 * which a convergence test would not.
	 */
	static constexpr int32 SurfaceIterations = 4;

	/**
	 * The most a single wave may pinch.
	 *
	 * Past this a Gerstner wave crosses itself and the surface turns inside out. Clamping here rather
	 * than trusting the data means a query and a vertex cannot disagree about whether it happened.
	 */
	static constexpr float MaxSteepness = 1.f;

	/**
	 * The widest a body's own wave speed may be, which is what its packed fraction is measured in.
	 *
	 * A range rather than the value itself, because amplitude and speed share one data slot and only
	 * a bounded number fits in a fraction. Five is well past anything a sea does and leaves the
	 * fraction about three ten thousandths of resolution, which is finer than the control's own step.
	 */
	static constexpr float SpeedRange = 5.f;
}

/**
 * A body's own wave amplitude and wave speed, in one float.
 *
 * The custom primitive data is full at the engine's thirty six, so a new value has to share. The
 * whole part is the amplitude in hundredths and the fraction is the speed over SpeedRange, which is
 * the same idiom the foam slots use.
 *
 * The pack is here rather than in the component because the CPU query has to read back what it
 * wrote: the shader sees only the packed float, so a query that used the raw properties would answer
 * a fractionally different surface from the one being drawn.
 */
namespace MobWaterBodyScales
{
	static FORCEINLINE float Pack(float Amplitude, float Speed)
	{
		const float Whole = FMath::RoundToFloat(FMath::Clamp(Amplitude, 0.f, 10.f) * 100.f);

		// Held a whisker under one so the amplitude still floors out of it.
		const float Fraction = FMath::Min(
			FMath::Clamp(Speed, 0.f, MobWaterWaveConstants::SpeedRange) / MobWaterWaveConstants::SpeedRange,
			0.9995f);

		return Whole + Fraction;
	}

	static FORCEINLINE void Unpack(float Packed, float& OutAmplitude, float& OutSpeed)
	{
		const float Whole = FMath::FloorToFloat(Packed);

		OutAmplitude = Whole * 0.01f;
		OutSpeed = (Packed - Whole) * MobWaterWaveConstants::SpeedRange;
	}
}

/**
 * One sine wave, displaced along its own direction as well as up.
 *
 * Wavelength drives the speed rather than sitting beside it: in deep water a wave's phase speed is
 * fixed by its length, so a set with an authored speed per wave can be given values no sea produces,
 * and it reads as wrong before anyone can say why.
 */
USTRUCT(BlueprintType)
struct MOBWATER_API FMobGerstnerWave
{
	GENERATED_BODY()

	/** Which way it travels, on the horizontal plane. Normalised on use, so it need not be here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave")
	FVector2f Direction = FVector2f(1.f, 0.f);

	/** Crest to crest, in world units. Also what decides how fast it travels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave", meta=(ClampMin="1.0", ForceUnits="cm"))
	float Wavelength = 800.f;

	/** Still surface to crest, in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave", meta=(ClampMin="0.0", ForceUnits="cm"))
	float Amplitude = 20.f;

	/** How far the crest leans forward. 0 is a plain sine; 1 is as sharp as it can get without folding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Steepness = 0.5f;

	/** Where in its cycle it starts, in radians. Two waves that share a direction need this to differ. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave", meta=(ClampMin="0.0", ClampMax="6.2831853"))
	float PhaseOffset = 0.f;
};

/**
 * The waves one body of water carries, and the three scalars that let one preset serve several bodies.
 *
 * A preset is shared and the scalars are not, so a pond and a harbour can be the same authored set at
 * a tenth the amplitude and half the speed, rather than two sets that drift apart as one is tuned.
 */
USTRUCT(BlueprintType)
struct MOBWATER_API FMobWaterWaveParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Waves")
	TArray<FMobGerstnerWave> Waves;

	/** Scales every wave's height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Waves", meta=(ClampMin="0.0"))
	float AmplitudeScale = 1.f;

	/** Scales how fast every wave travels. 0 freezes the surface without flattening it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Waves", meta=(ClampMin="0.0"))
	float SpeedScale = 1.f;

	/** Scales how far crests lean. 0 leaves sines, which is what a small still body wants. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Waves", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ChoppinessScale = 1.f;
};

/**
 * Everything one point on the surface is, from one pass over the wave set.
 *
 * Returned together rather than through three entry points because all three come out of the same
 * sines, and a caller that wanted height and normal would otherwise pay for them twice.
 */
struct FMobWaterSample
{
	/** Where the point moved to, relative to where it started. */
	FVector3f Displacement = FVector3f::ZeroVector;

	/** The surface normal there, already normalised. */
	FVector3f Normal = FVector3f(0.f, 0.f, 1.f);

	/** How far the surface is folding, 0 flat and 1 about to cross itself. */
	float Fold = 0.f;
};

/**
 * The wave field, evaluated.
 *
 * Every function here is a pure function of position and time. That is not tidiness, it is the whole
 * networking story: a dedicated server has no GPU and therefore no simulation to read, so the only
 * surface two machines can agree on is one they can both compute from the same two inputs.
 *
 * Written against float rather than double throughout, because the shader has no choice and a query
 * that answered more precisely than the pixels would put physics slightly off the water it draws.
 */
struct MOBWATER_API FMobWaterWaves
{
	/** Radians per world unit. */
	static FORCEINLINE float WaveNumber(const FMobGerstnerWave& Wave)
	{
		return 2.f * UE_PI / FMath::Max(Wave.Wavelength, 1.f);
	}

	/** Radians per second, from the deep water dispersion relation. */
	static FORCEINLINE float AngularFrequency(float WaveNumberK)
	{
		return FMath::Sqrt(MobWaterWaveConstants::Gravity * WaveNumberK);
	}

	/** X - Period * floor(X / Period), which is a modulo that stays positive for negative X. */
	static FORCEINLINE float Repeat(float X, float Period)
	{
		return X - Period * FMath::FloorToFloat(X / Period);
	}

	/**
	 * Where a wave is in its cycle at a point and an instant, always within one turn of zero.
	 *
	 * Both terms are folded before they meet, and that is not an optimisation. A world position out at
	 * a kilometre times a wave number gives a phase in the thousands of radians, and a float carries
	 * about seven digits, so the sine of it is quantised into visible steps - the surface reads as
	 * faceted far from the origin and smooth near it. Folding the distance by the wavelength and the
	 * time term by a full turn is exact, because both are periods of the wave itself.
	 */
	static FORCEINLINE float Phase(const FMobGerstnerWave& Wave, const FVector2f& Dir, const FVector2f& SampleXY, float Time, float SpeedScale)
	{
		const float K = WaveNumber(Wave);

		// Folded by the same clamped wavelength the wave number was built from. Folding by the raw
		// value would part from the shader for any wave under a centimetre long.
		const float Along = Repeat(Dir.X * SampleXY.X + Dir.Y * SampleXY.Y, FMath::Max(Wave.Wavelength, 1.f));
		const float Turns = Repeat(AngularFrequency(K) * SpeedScale * Time, 2.f * UE_PI);

		return K * Along - Turns + Wave.PhaseOffset;
	}

	/**
	 * The surface at a point that started at SampleXY.
	 *
	 * This is the forward map, and the one thing the vertex shader and this file both implement.
	 * Everything else here is built out of it, so parity between CPU and GPU is a test of this alone.
	 */
	static FMobWaterSample Evaluate(const FMobWaterWaveParams& Params, const FVector2f& SampleXY, float Time)
	{
		FMobWaterSample Out;

		const int32 Count = FMath::Min(Params.Waves.Num(), MobWaterWaveConstants::MaxWaves);
		if (Count <= 0)
		{
			return Out;
		}

		FVector3f Slope = FVector3f(0.f, 0.f, 1.f);

		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FMobGerstnerWave& Wave = Params.Waves[Index];

			const FVector2f Dir = Wave.Direction.GetSafeNormal();
			const float K = WaveNumber(Wave);
			const float Amplitude = Wave.Amplitude * Params.AmplitudeScale;

			const float WavePhase = Phase(Wave, Dir, SampleXY, Time, Params.SpeedScale);
			const float SinPhase = FMath::Sin(WavePhase);
			const float CosPhase = FMath::Cos(WavePhase);

			// Steepness is shared out across the set rather than applied per wave, so adding a ninth
			// wave to a calm sea does not fold it. The alternative is a set that has to be retuned
			// every time one is added to it.
			const float Steep = FMath::Min(Wave.Steepness * Params.ChoppinessScale, MobWaterWaveConstants::MaxSteepness);
			const float KA = K * Amplitude;
			const float Q = KA > UE_SMALL_NUMBER ? Steep / (KA * static_cast<float>(Count)) : 0.f;

			Out.Displacement.X += Q * Amplitude * Dir.X * CosPhase;
			Out.Displacement.Y += Q * Amplitude * Dir.Y * CosPhase;
			Out.Displacement.Z += Amplitude * SinPhase;

			Slope.X -= Dir.X * KA * CosPhase;
			Slope.Y -= Dir.Y * KA * CosPhase;
			Slope.Z -= Q * KA * SinPhase;

			Out.Fold += Q * KA * SinPhase;
		}

		Out.Normal = Slope.GetSafeNormal();
		Out.Fold = FMath::Clamp(Out.Fold, 0.f, 1.f);

		return Out;
	}

	/**
	 * The surface directly above a world XY.
	 *
	 * Evaluate answers where a point went; this answers what is over a point, which is the question
	 * buoyancy and immersion actually ask. It walks back towards the asked-for column a fixed number
	 * of times rather than iterating to a tolerance, so two machines take the same path to the same
	 * answer.
	 */
	static FMobWaterSample Surface(const FMobWaterWaveParams& Params, const FVector2f& WorldXY, float Time)
	{
		FVector2f Guess = WorldXY;
		for (int32 Step = 0; Step < MobWaterWaveConstants::SurfaceIterations; ++Step)
		{
			const FMobWaterSample Walk = Evaluate(Params, Guess, Time);
			Guess = WorldXY - FVector2f(Walk.Displacement.X, Walk.Displacement.Y);
		}

		FMobWaterSample Out = Evaluate(Params, Guess, Time);

		// The horizontal part has done its job getting here and would otherwise be reported as an
		// offset from the column that was asked about, which it is not.
		Out.Displacement.X = 0.f;
		Out.Displacement.Y = 0.f;

		return Out;
	}

	/**
	 * How much of a wave survives this close to the edge of its body, 0 at the bank and 1 out in it.
	 *
	 * Without this a lake's waves keep their full height as the bank comes up and stand above dry
	 * ground at the rim. It is one multiply, and it is the difference between an edge that reads as a
	 * shoreline and one that reads as a plane intersecting terrain.
	 *
	 * Distance to the body's own edge, and not the depth of the water, however much the second sounds
	 * like the better measure. The displacement happens in the vertex shader, and a vertex shader
	 * cannot read the depth buffer - the bed is only recoverable per pixel, by which point the vertex
	 * has already moved. The edge of a box, a disc or a spline is arithmetic, so the vertex shader and
	 * this function can both have it, which is the requirement that actually binds.
	 *
	 * Scene depth still does the work it can do: colour, clarity and shoreline foam are all per pixel.
	 */
	static FORCEINLINE float ShoreAttenuation(float EdgeDistance, float FadeDistance)
	{
		if (FadeDistance <= UE_SMALL_NUMBER)
		{
			return 1.f;
		}

		const float T = FMath::Clamp(EdgeDistance / FadeDistance, 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}
};
