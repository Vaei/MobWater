// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MobWaterLookPreset.generated.h"

/**
 * What a body of water looks like, as one asset.
 *
 * The difference between stylized and realistic water is not one setting, it is a dozen agreeing
 * with each other - a hard shallow colour wants tight foam and no detail normal, and a soft
 * absorption wants the opposite. Set half of them and the result reads as neither.
 *
 * Every value here is per-body custom primitive data, so applying a preset costs no material
 * instance and a level can carry as many looks as it likes for one set of shaders.
 */
UCLASS(BlueprintType)
class MOBWATER_API UMobWaterLookPreset : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * The colour comes from a gradient ramp indexed by depth rather than from an absorption between
	 * two colours. A ramp can hold hard steps; an exponential cannot.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour")
	bool bGradientColor = false;

	/** Which palette in the atlas. GA_MobWater ships 0 Stylized, 1 Toon, 2 Tropical, 3 Swamp. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(EditCondition="bGradientColor", ClampMin="0"))
	int32 GradientRow = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(EditCondition="!bGradientColor"))
	FLinearColor ShallowColor = FLinearColor(0.18f, 0.42f, 0.42f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(EditCondition="!bGradientColor"))
	FLinearColor DeepColor = FLinearColor(0.01f, 0.06f, 0.11f, 1.f);

	/** The water column over which the colour reaches the deep one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(ClampMin="1.0", ForceUnits="cm"))
	float FadeDepth = 300.f;

	/** How opaque the water is regardless of how much of it there is. 1 hides the bed entirely. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinOpacity = 0.f;

	/** The water column over which the bed stops being visible at all. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(ClampMin="1.0", ForceUnits="cm"))
	float ClarityDepth = 500.f;

	/**
	 * How much of the water's colour is emitted rather than lit.
	 *
	 * 0 lets the scene light it, which is right for water that belongs to its environment. 1 emits it
	 * instead, so the colour on screen is the colour that was chosen - no ambient tint from whatever
	 * the skylight happened to capture, and no dark patches where a wave turns away from the sun.
	 * A green cast on blue water, or black blotches that drift with the surface, are both this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Unlit = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Roughness = 0.02f;

	/** How much of the scrolling detail normal reaches the surface. 0 is glass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0"))
	float DetailStrength = 1.f;

	/** How fast the detail drifts with no flow and no waves. 0 is genuinely still water. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0"))
	float DetailScrollSpeed = 1.f;

	/** How pronounced the slow swell across the whole body is. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0"))
	float MacroStrength = 2.2f;

	/** How far in from the bank the edge foam line reaches. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(ClampMin="0.0", ClampMax="1.0"))
	float EdgeFoamWidth = 0.12f;

	/** How much foam there is at all. 0 is none, 1 is all of it; scales the finished mask. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(ClampMin="0.0", ClampMax="1.0"))
	float FoamOpacity = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam")
	bool bFoam = true;

	/** How far up from the bed foam reaches. A line, not a region - see the component's note. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(ClampMin="0.0", ForceUnits="cm"))
	float ShoreFoamDepth = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(ClampMin="0.0", ClampMax="1.0"))
	float CrestFoamThreshold = 0.55f;

	/**
	 * How far the noise moves the foam edge, as a fraction of the band width.
	 *
	 * Clean by default. A wandering edge is the thing that reads as texture on a stylized surface,
	 * and it is a shoreline's own shape that should be doing that work.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(ClampMin="0.0", ClampMax="1.0"))
	float FoamNoiseAmount = 0.f;

	/** Foam carries a texture of its own, sampled across the shoreline rather than with the water. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam")
	bool bFoamTexture = false;

	/**
	 * How textured the foam is: its own texture when bFoamTexture is on, and how much of the water
	 * underneath shows through it when off. 0 is flat froth either way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(ClampMin="0.0", ClampMax="1.0"))
	float FoamTextureOpacity = 0.f;

	/** How hard the foam edge is. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(ClampMin="1.0"))
	float FoamSharpness = 3.f;

	/** Cuts the foam into this many steps. 0 leaves it smooth. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(ClampMin="0.0", ClampMax="8.0"))
	float FoamBands = 0.f;

	/**
	 * How much water shows between the foam bands.
	 *
	 * 0 leaves them touching, which reads as one band with steps in it rather than as separate rings.
	 * Raising it slices gaps into the ramp so the water shows through between them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(ClampMin="0.0", ClampMax="0.95"))
	float FoamBandSeparation = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="1.0"))
	float GlintGloss = 380.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0"))
	float GlintStrength = 2.5f;

	/**
	 * How many of the cut glints actually appear.
	 *
	 * Lower removes whole glints rather than dimming them all, so a sparser surface has the same
	 * marks in fewer places instead of an even wash of half-bright ones. Which places is decided by
	 * noise anchored to the world, so they scatter and stay put.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0", ClampMax="1.0"))
	float GlintDensity = 1.f;

	/**
	 * Pushes a glint past what the light could account for, so it blooms.
	 *
	 * Above 1 it is no longer a reflection of anything, which is the point.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0"))
	float GlintEmissive = 1.f;

	/** Cuts the sun lobe into separate glints. 0 leaves it a continuous sheen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0", ClampMax="1.0"))
	float GlintThreshold = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Caustics")
	bool bCaustics = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Caustics", meta=(ClampMin="0.0"))
	float CausticStrength = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Caustics", meta=(ClampMin="1.0", ForceUnits="cm"))
	float CausticDepth = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Refraction")
	bool bRefraction = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Refraction", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RefractionStrength = 0.3f;

	/** How much sky this body reflects. 0 is none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reflection", meta=(ClampMin="0.0"))
	float ReflectionStrength = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ripples")
	bool bRipples = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ripples", meta=(ClampMin="0.0"))
	float RippleStrength = 1.f;

	/** The waves this look expects. Unset leaves whatever the body already had. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Waves")
	TObjectPtr<class UMobWaterWavePreset> Waves;
};
