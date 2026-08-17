// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "MobWaterTypes.generated.h"

/**
 * What a body of water is, which decides both the mesh it rasterises and the waves it defaults to.
 *
 * Ordered smallest first on purpose. A level is mostly the first two, and an ocean is one more shape
 * rather than the thing the plugin is about.
 */
UENUM(BlueprintType)
enum class EMobWaterShape : uint8
{
	/** An axis aligned rectangle. Baths, troughs, flooded rooms, rice paddies. */
	Box			UMETA(DisplayName = "Box"),

	/** A circle. Puddles, basins, wells, ponds. */
	Disc		UMETA(DisplayName = "Disc"),

	/** A spline. Closed, it is a lake with whatever shoreline you draw; open, it is a river. */
	Spline		UMETA(DisplayName = "Spline"),

	/** Unbounded, and the only shape that reads a baked spectrum. */
	Ocean		UMETA(DisplayName = "Ocean"),
};

/** The shape's name, as it appears in asset names, icon names and actor labels. */
MOBWATER_API const TCHAR* MobWaterShapeName(EMobWaterShape Shape);

/** Which way an authored direction points. */
UENUM(BlueprintType)
enum class EMobWaterSpace : uint8
{
	/** The direction is where it says on the level's compass, whatever the actor is doing. */
	World		UMETA(DisplayName = "World"),

	/** The direction is relative to the actor, so rotating the actor carries it round. */
	Local		UMETA(DisplayName = "Local"),
};

#if WITH_EDITORONLY_DATA
namespace MobWaterSprite
{
	/**
	 * Puts the water icon on an actor's billboard.
	 *
	 * Call it from the actor's constructor. The finder underneath may only run while a class default
	 * object is being built, and every water actor wants the same icon.
	 */
	MOBWATER_API void Apply(class UBillboardComponent* Sprite);
}
#endif

/**
 * What an exclusion volume is shaped like.
 *
 * The first four are evaluated, so they carve a clean edge and cost the surface a little arithmetic.
 * Mesh is stamped as a silhouette instead, which is what lets an arbitrary hull carve its own outline
 * without anyone authoring a shape to approximate it.
 */
UENUM(BlueprintType)
enum class EMobWaterExclusionShape : uint8
{
	Disc		UMETA(DisplayName = "Disc"),
	Sphere		UMETA(DisplayName = "Sphere"),
	Box			UMETA(DisplayName = "Box"),
	Rect		UMETA(DisplayName = "Rect"),
	Mesh		UMETA(DisplayName = "Mesh"),
};

/**
 * Which of a shape's materials to use. Every one of these is a compile time choice, so each
 * combination is a shader of its own.
 *
 * They are permutations rather than amounts of zero because the values reach the shader as per
 * instance data, which the compiler cannot fold away however small they are.
 */
namespace MobWaterVariant
{
	/** Reads the ripple field. Off, the surface never samples it and a still pool costs no tap. */
	static constexpr int32 Ripples = 1 << 0;

	/** Shoreline and crest foam. */
	static constexpr int32 Foam = 1 << 1;

	/** Bends what is behind the surface. The only feature that reads scene colour. */
	static constexpr int32 Refraction = 1 << 2;

	/**
	 * Foam carries a texture of its own, in the shoreline's frame.
	 *
	 * A permutation rather than an amount, because it is a sixth texture read and its coordinates are
	 * their own arithmetic - a body with plain foam should pay for neither. Only meaningful with Foam,
	 * so the generator never builds the four combinations without it.
	 */
	static constexpr int32 FoamTexture = 1 << 3;

	/**
	 * The water's colour comes from a gradient ramp indexed by depth rather than from an absorption
	 * between two colours.
	 *
	 * A permutation rather than a blend between the two, because the ramp is a texture read: a body
	 * grading by absorption would carry the tap and the coordinate for a palette it never samples,
	 * and per instance data is not something the compiler can fold away.
	 */
	static constexpr int32 Gradient = 1 << 4;

	static constexpr int32 Num = 32;

	/** The name suffix the generator gives this combination. */
	MOBWATER_API FString Suffix(int32 Variant);
}

/**
 * The materials one shape can render with, indexed by MobWaterVariant.
 *
 * An array rather than eight named members: the set only ever grows by another axis, and naming each
 * corner of a cube stops being readable at the third one.
 */
USTRUCT(BlueprintType)
struct FMobWaterMaterialSet
{
	GENERATED_BODY()

	/** Indexed by the MobWaterVariant flags: ripples 1, foam 2, refraction 4. */
	UPROPERTY(EditAnywhere, Category="Materials")
	TArray<TSoftObjectPtr<class UMaterialInterface>> Variants;
};

/**
 * Custom primitive data layout, shared by the component that writes it and the master material that
 * reads it.
 *
 * The material's parameters carry the same indices; mob_water_verify asserts the two agree.
 *
 * There are only thirty six of these, and that is the engine's number rather than a choice -
 * FCustomPrimitiveData holds nine float4s. A write past the end is not an error, it is a no-op, and
 * the material reads zero for the rest of the project's life with nothing said. MobWaterComponent.cpp
 * carries a static_assert against the engine's constant so the next parameter that does not fit
 * fails the build instead of failing silently.
 *
 * Being full is what the packing here is for: FoamBands carries its separation in its fraction
 * because the pair is one concept, not because the slots were spare.
 */
namespace MobWaterData
{
	/** Linear RGB of the water at its shallowest. Occupies 0, 1 and 2. */
	static constexpr int32 ShallowColor = 0;

	/**
	 * Which row of the colour atlas a gradient-graded body reads.
	 *
	 * The same float as ShallowColor's red, because the gradient fork replaces both colours outright:
	 * only one of the two is ever compiled, so the six floats they would take are free whenever this
	 * one is wanted. The component writes whichever the body is actually using.
	 */
	static constexpr int32 GradientRow = 0;

	/** Linear RGB the water grades to at FadeDepth. Occupies 3, 4 and 5. */
	static constexpr int32 DeepColor = 3;

	/** World units of water column over which the colour reaches DeepColor. */
	static constexpr int32 FadeDepth = 6;

	/** World units of water column over which the bed stops being visible at all. */
	static constexpr int32 ClarityDepth = 7;

	/** How far up from the bed foam reaches, in world units. 0 is no shoreline foam. */
	static constexpr int32 ShoreFoamDepth = 8;

	/** Wave steepness at which crest foam starts. 1 is never. */
	static constexpr int32 CrestFoamThreshold = 9;

	/** Scales every wave's amplitude for this body, after its own depth attenuation. */
	static constexpr int32 WaveAmplitude = 10;

	/** World units in from the body's edge over which waves are flattened to nothing. */
	static constexpr int32 ShoreFadeDistance = 11;

	/** How far the surface bends what is behind it, in world units at one metre. */
	static constexpr int32 RefractionStrength = 12;

	/** How much of the ripple field's height reaches the surface. 0 is a still body. */
	static constexpr int32 RippleStrength = 13;

	/** Surface roughness where the water is calm, which is what the specular reads. */
	static constexpr int32 Roughness = 14;

	/** Flow across the surface in world units per second. Occupies 15 and 16. */
	static constexpr int32 FlowVelocity = 15;

	/**
	 * Half the body's size on X and Y, in world units. Occupies 17 and 18.
	 *
	 * The vertex shader needs this to know how far a vertex is from the bank, which is what the wave
	 * attenuation is weighted by. It cannot get there from the depth buffer - a vertex shader cannot
	 * read one - and the mesh is unit sized and scaled by the component, so the size is not in the
	 * geometry either.
	 */
	static constexpr int32 HalfExtent = 17;

	/**
	 * How much of the detail normal reaches the surface.
	 *
	 * Per body rather than per material instance, because it is most of the difference between a
	 * stylized surface and a realistic one, and a look preset has to be able to set it without
	 * needing an instance of its own.
	 */
	static constexpr int32 DetailStrength = 19;

	/** How bright the caustic thrown onto the bed is. 0 is none. */
	static constexpr int32 CausticStrength = 20;

	/** The water column over which the caustic is lost, in world units. */
	static constexpr int32 CausticDepth = 21;

	/** How far the noise moves a foam edge, as a fraction of the band's width. */
	static constexpr int32 FoamNoiseAmount = 22;

	/** How hard a foam edge is. */
	static constexpr int32 FoamSharpness = 23;

	/**
	 * How many steps the foam is cut into, with the gap between them in the fraction.
	 *
	 * Two values in one slot, which is what MobWaterFoamBands unpacks: the whole part is the band
	 * count and 0 leaves the foam smooth, the fraction is how much of each band is cut away so water
	 * shows between them. Packed because the last slot went and these two are one control.
	 */
	static constexpr int32 FoamBands = 24;

	/** How tight the sun's lobe on the surface is. */
	static constexpr int32 GlintGloss = 25;

	/** How bright the sun is on the surface. */
	static constexpr int32 GlintStrength = 26;

	/** Cuts the sun lobe into separate glints. 0 leaves it a continuous sheen. */
	static constexpr int32 GlintThreshold = 27;

	/** How fast the surface detail drifts with no flow and no waves. 0 is genuinely still. */
	static constexpr int32 DetailScrollSpeed = 28;

	/** How pronounced the slow swell across the whole body is. */
	static constexpr int32 MacroStrength = 29;

	/** How far in from the bank the edge foam line reaches, as a fraction of the shore fade. */
	static constexpr int32 EdgeFoamWidth = 30;

	/** How opaque the water is regardless of how much of it there is. */
	static constexpr int32 MinOpacity = 31;

	/** How much sky this body reflects. 0 is none. */
	static constexpr int32 ReflectionStrength = 32;

	/** How many of the cut glints actually appear. Lower removes whole glints rather than dimming. */
	static constexpr int32 GlintDensity = 33;

	/** Pushes a glint past what the light could account for, so it blooms. */
	static constexpr int32 GlintEmissive = 34;

	/** How much of the water's colour is emitted rather than lit. 1 is a flat colour. */
	static constexpr int32 Unlit = 35;

	static constexpr int32 Num = 36;
}
