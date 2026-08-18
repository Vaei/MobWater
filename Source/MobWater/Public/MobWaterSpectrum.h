// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MobWaterWaves.h"
#include "MobWaterSpectrum.generated.h"

class UTexture2D;

/**
 * The constants the baked sea state is addressed with.
 *
 * Duplicated in Shaders/Public/MobWaterSpectrum.ush for the same reason the wave constants are, and
 * asserted equal by mob_water_verify.
 */
namespace MobWaterSpectrumConstants
{
	/**
	 * Texels of its own opposite edge copied around every frame in the atlas.
	 *
	 * The frames sit side by side, so the hardware filter reaching past one cell would read the next
	 * frame. The field wraps, so what it should have read is that frame's own opposite edge, and
	 * putting it there is what lets a tiling field be addressed with one bilinear tap out of a texture
	 * that does not tile.
	 *
	 * The table the query reads has no gutter in it - nothing filters across a cell boundary on the
	 * CPU, because there are no cells there.
	 */
	static constexpr int32 Gutter = 1;
}

/**
 * One sea state, solved offline and baked.
 *
 * A Gerstner set is a handful of sines, so it can only ever look like a handful of sines: real open
 * water is a continuum, and the crossing patterns that make it read as an ocean rather than as a
 * corrugated sheet come from hundreds of components at once. Those cannot be summed per vertex on
 * this renderer, so they are summed once at author time and sampled instead.
 *
 * It loops exactly. Every component's angular frequency is rounded down to a multiple of one turn
 * over the loop period before the transform runs, so the last frame is the frame before the first and
 * there is no crossfade anywhere - a seam in time on an ocean is more obvious than a seam in space.
 *
 * The CPU reads the same bytes the GPU samples, out of Samples rather than out of the texture. A
 * dedicated server has no texture: its platform data is a GPU upload that may never have been made,
 * and a query that answered from a texture would answer differently on the machine that decides.
 */
UCLASS(BlueprintType)
class MOBWATER_API UMobWaterSpectrum : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Displacement and folding, one frame of the loop to a cell of the atlas.
	 *
	 * RGB is where a point moved to, biased into the positive half and scaled by HorizontalScale and
	 * VerticalScale. A is how hard the surface is folding there.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spectrum")
	TObjectPtr<UTexture2D> DisplacementTexture;

	/**
	 * The surface normal, in the same layout.
	 *
	 * Sampled per pixel rather than interpolated from the vertices, because an ocean's mesh is a ring
	 * of a few thousand triangles reaching to the horizon and a normal carried across one of those is
	 * a normal for something the size of a house.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spectrum")
	TObjectPtr<UTexture2D> NormalTexture;

	/** How wide one tile of the field is in the world. The field repeats every one of these. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spectrum", meta=(ClampMin="1.0", ForceUnits="cm"))
	float TileSize = 6144.f;

	/** How long the field takes to come back to where it started. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spectrum", meta=(ClampMin="0.1", ForceUnits="s"))
	float LoopPeriod = 16.f;

	/** Texels across one tile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spectrum", meta=(ClampMin="4"))
	int32 Resolution = 64;

	/** Frames in the loop. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spectrum", meta=(ClampMin="2"))
	int32 Frames = 128;

	/**
	 * How many frames sit across the atlas.
	 *
	 * The layout is the material's business and not the query's, which reads a table with no atlas in
	 * it at all. It lives here because the material has no other way to be told.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spectrum", meta=(ClampMin="1"))
	int32 AtlasColumns = 16;

	/** What a full swing of the displacement's red and green channels is worth, in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spectrum", meta=(ClampMin="0.0", ForceUnits="cm"))
	float HorizontalScale = 100.f;

	/** What a full swing of the displacement's blue channel is worth, in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spectrum", meta=(ClampMin="0.0", ForceUnits="cm"))
	float VerticalScale = 100.f;

	/** What a full swing of the normal's red and green channels is worth, as a slope. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spectrum", meta=(ClampMin="0.0"))
	float NormalScale = 1.f;

	/** The wind this was solved for. Kept because a sea state is not readable back out of the bytes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Provenance", meta=(ForceUnits="cm/s"))
	float WindSpeed = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Provenance", meta=(ForceUnits="deg"))
	float WindDirection = 0.f;

	/** How far the transform was allowed to pull points towards a crest. 0 leaves rounded swell. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Provenance")
	float Choppiness = 1.f;

	/** Root mean square of the baked height. Significant wave height is about four of these. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Provenance", meta=(ForceUnits="cm"))
	float RmsHeight = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Provenance")
	int32 Seed = 0;

	/**
	 * The displacement texture's texels, without its gutter, as the query reads them.
	 *
	 * Four bytes a texel in Resolution x Resolution x Frames order, which is the same encoding the
	 * texture carries - not a second solve of the same spectrum. Two implementations of a Gerstner
	 * sum can be asserted equal; two solves of a Fourier transform in different languages cannot, and
	 * the whole networking story needs the answer to be the same rather than nearly the same.
	 */
	UPROPERTY()
	TArray<uint8> Samples;

	/** Whether there is anything here to read. An unbaked asset answers a flat sea rather than a NaN. */
	bool IsUsable() const;

	/**
	 * What the query table weighs.
	 *
	 * A function rather than reading Samples, because the cost report is the only thing that asks and
	 * reading the array from Python would marshal two million entries to count them.
	 */
	UFUNCTION(BlueprintPure, Category="Water|Spectrum")
	int32 GetTableBytes() const { return Samples.Num(); }

	/**
	 * Where a point on the still surface moved to, and how hard the surface is folding there.
	 *
	 * The same bilinear across the tile and linear across the two frames the shader does, on the same
	 * bytes. What is left between them is the hardware's own subtexel weight precision, which
	 * Verify Contract measures rather than assumes.
	 */
	FVector3f SampleDisplacement(const FVector2f& WorldXY, float Time, float& OutFold) const;

	/**
	 * The surface at a point, in the shape the wave evaluator answers.
	 *
	 * The normal is the height field's own gradient, taken across one texel, rather than the baked
	 * normal texture: that one is a shading normal for a pixel, and what a raft rides is the slope of
	 * the water it is sitting on. They are the same field measured at two scales.
	 */
	FMobWaterSample Evaluate(const FVector2f& WorldXY, float Time) const;

#if WITH_EDITOR
	/**
	 * Fills Samples from a file the bake wrote beside the textures.
	 *
	 * A file rather than an array handed over from Python: the table is two million entries, and
	 * marshalling that one Python integer at a time costs minutes and a gigabyte to move bytes that
	 * are already on disk.
	 */
	UFUNCTION(BlueprintCallable, Category="Water|Spectrum")
	bool LoadSamplesFromFile(const FString& FilePath);

	/**
	 * Records what the bake was asked for.
	 *
	 * A function rather than five properties the bake writes, because the properties are a record and
	 * a record that can be edited is not one - a sea state whose stated wind is not the wind it was
	 * solved at is worse than one that states nothing.
	 */
	UFUNCTION(BlueprintCallable, Category="Water|Spectrum")
	void RecordBake(float InWindSpeed, float InWindDirection, float InChoppiness, float InRmsHeight,
		int32 InSeed);
#endif
};

/**
 * The wave field a body of water actually has, which is a Gerstner set and possibly a baked one on
 * top of it.
 *
 * Separate from FMobWaterWaves because that file is pure arithmetic with no UObject in it, and this
 * needs an asset. The composition is addition: the spectrum carries the sea and the Gerstner set
 * carries whatever a level wants on top of it, so an ocean whose spectrum has not been assigned yet
 * still has waves rather than turning to glass.
 */
namespace MobWaterCombined
{
	/**
	 * Where a point that started at SampleXY went, from both sources at once.
	 *
	 * Only the authored waves shoal. A baked spectrum is a deep water sea state - Phillips is
	 * defined for one - so Green's law has nothing to say about it, and the surface it draws is
	 * killed into an obstacle rather than raised against it. The swell that rises is the Gerstner
	 * tier layered over it, which is where a body's surf is authored anyway.
	 */
	MOBWATER_API FMobWaterSample Evaluate(const FMobWaterWaveParams& Params,
		const UMobWaterSpectrum* Spectrum, const FVector2f& SampleXY, float Time,
		const FMobWaterShoalField* Shoal = nullptr);

	/**
	 * The surface directly above a world XY.
	 *
	 * Walks back over the sum rather than over the Gerstner set alone. The spectrum moves points
	 * sideways as hard as a Gerstner wave does, so a walk that ignored it would answer about a column
	 * up to a metre from the one that was asked about.
	 */
	MOBWATER_API FMobWaterSample Surface(const FMobWaterWaveParams& Params,
		const UMobWaterSpectrum* Spectrum, const FVector2f& WorldXY, float Time,
		const FMobWaterShoalField* Shoal = nullptr);
}
