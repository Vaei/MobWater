// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "MobWaterInfo.h"
#include "MobWaterWaves.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MobWaterStatics.generated.h"

class UMobWaterSpectrum;
class UMobWaterWavePreset;

/**
 * The water, as anything outside the plugin asks about it.
 */
UCLASS()
class MOBWATER_API UMobWaterStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The instant the waves are being evaluated at, folded into the loop period.
	 *
	 * Anything that wants to reproduce a surface later - a replay, a server checking a client's claim -
	 * needs this rather than the world's own time.
	 */
	UFUNCTION(BlueprintPure, Category="Water", meta=(WorldContext="WorldContextObject"))
	static float GetWaterTime(const UObject* WorldContextObject);

	/**
	 * The water at a point.
	 *
	 * Ripples, wakes and stamped foam are not in this answer and cannot be: the field they live in is
	 * a render target, and a dedicated server has no GPU to hold one. What is here is a pure function
	 * of position and time, so two machines given the same instant agree exactly - which is the whole
	 * reason anything is allowed to build physics on it.
	 *
	 * Querier is who is asking, and it is an explicit argument rather than a world context that
	 * happens to be whatever was convenient. When a ship and a client disagree about where the water
	 * is, the first thing worth knowing is almost never that the water disagreed - it is that the two
	 * machines asked about different places, because the ship was somewhere different. Separating
	 * those needs the query attributed to something, and attributing it to whichever object supplied
	 * the world would be right by luck and wrong from a Blueprint library.
	 *
	 * It also has to be an object in the world, because it is what the subsystem is found through.
	 */
	UFUNCTION(BlueprintCallable, Category="Water", meta=(DefaultToSelf="Querier"))
	static bool GetWaterInfoAtLocation(const UObject* Querier, const FVector& Location,
		FMobWaterInfo& OutInfo);

	/** The surface height above a point, and nothing else. The cheap case, and the common one. */
	UFUNCTION(BlueprintPure, Category="Water", meta=(DefaultToSelf="Querier"))
	static bool GetWaterSurfaceZ(const UObject* Querier, const FVector& Location,
		float& OutSurfaceZ);

	/**
	 * Many points at once.
	 *
	 * A pontoon array asks about four to eight points that are almost always in the same body, so
	 * this looks the body up once instead of once per point.
	 */
	UFUNCTION(BlueprintCallable, Category="Water", meta=(DefaultToSelf="Querier"))
	static void GetWaterInfoAtLocations(const UObject* Querier, const TArray<FVector>& Locations,
		TArray<FMobWaterInfo>& OutInfos);

	/**
	 * A body's own wave amplitude and speed, split out of the one data slot they share.
	 *
	 * Exposed because the parity test drives it: the shader unpacks the same float in four lines of
	 * its own, and nothing but a test keeps the two readings of it equal.
	 */
	UFUNCTION(BlueprintPure, Category="Water|Waves")
	static void UnpackBodyWaveScales(float Packed, float& Amplitude, float& Speed);

	/** The reverse, which is what the component writes into the slot. */
	UFUNCTION(BlueprintPure, Category="Water|Waves")
	static float PackBodyWaveScales(float Amplitude, float Speed);

	/**
	 * The surface above a point, from a wave set given explicitly.
	 *
	 * Explicit rather than looked up because this is the function the CPU and GPU parity test drives:
	 * it has to be able to ask for a named preset at a named instant, with no world state in the way.
	 */
	UFUNCTION(BlueprintCallable, Category="Water|Waves")
	static void EvaluateWavePreset(const UMobWaterWavePreset* Preset, FVector2D SampleXY, float Time,
		FVector& Displacement, FVector& Normal, float& Fold);

	/**
	 * Where the baked sea moved a point, from the table rather than from the texture.
	 *
	 * Explicit rather than looked up because this is the function the spectrum's parity test drives,
	 * and it has to be able to ask a named asset about a named instant with no world state in the way.
	 */
	UFUNCTION(BlueprintCallable, Category="Water|Waves")
	static void EvaluateSpectrum(const UMobWaterSpectrum* Spectrum, FVector2D WorldXY, float Time,
		FVector& Displacement, float& Fold);

	/**
	 * The surface above a world XY, walked back so the answer is over the column asked about rather
	 * than over wherever that column's water drifted to.
	 */
	UFUNCTION(BlueprintCallable, Category="Water|Waves")
	static void EvaluateWavePresetSurface(const UMobWaterWavePreset* Preset, FVector2D WorldXY, float Time,
		float& SurfaceOffsetZ, FVector& Normal, float& Fold);

	/**
	 * The water at a point, given where its still surface sits and how deep it is.
	 *
	 * ShoreFade is how much of the wave survives here, 0 at the bank and 1 out in open water. It
	 * arrives already computed rather than being worked out inside, because the body knows its own
	 * shape and the vertex shader computes the same number from the same arithmetic - handing both the
	 * finished value is what stops them drifting.
	 *
	 * Bodies of water answer all of this for themselves from Phase 3 on, at which point this becomes
	 * the inner half of GetWaterInfoAtLocation rather than the way anyone calls it.
	 */
	UFUNCTION(BlueprintCallable, Category="Water", meta=(WorldContext="WorldContextObject"))
	static FMobWaterInfo EvaluateWaterAt(const UObject* WorldContextObject, FVector Location,
		float StillSurfaceZ, float WaterDepth, float ShoreFade = 1.f);

	/**
	 * Native access to the same evaluation, without a preset asset in the way.
	 *
	 * Spectrum is added to the Gerstner set rather than replacing it, so an ocean whose sea state has
	 * not been baked yet still has waves instead of turning to glass.
	 */
	static FMobWaterInfo EvaluateWaterAtNative(const FMobWaterWaveParams& Params, const FVector& Location,
		float StillSurfaceZ, float WaterDepth, float ShoreFade, float Time,
		const UMobWaterSpectrum* Spectrum = nullptr, const struct FMobWaterShoalField* Shoal = nullptr);
};
