// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "MobWaterInfo.h"
#include "MobWaterTypes.h"
#include "MobWaterWaves.h"
#include "Components/StaticMeshComponent.h"
#include "MobWaterComponent.generated.h"

class UMobWaterWavePreset;

/**
 * A body of water.
 *
 * A static mesh component with everything the renderer would care about switched off, drawing a
 * translucent material that recovers the bed from the depth buffer. The renderer never learns water
 * exists; it is a plane with an expensive material on it, and that is the whole trick.
 *
 * Everything a body differs by reaches the material as custom primitive data, so a level full of
 * pools is one material and one set of shader permutations rather than a dynamic material instance
 * each.
 */
UCLASS(ClassGroup=(Mob), meta=(BlueprintSpawnableComponent, DisplayName="Mob Water"),
	hidecategories=(Collision, Physics, Navigation, HLOD, VirtualTexture, RayTracing))
class MOBWATER_API UMobWaterComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UMobWaterComponent();

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	//~ End UActorComponent Interface

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Which shape this body is, which decides its mesh, its material and how its waves reach the bank. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Water")
	EMobWaterShape Shape = EMobWaterShape::Box;

	/**
	 * A look to start from.
	 *
	 * Applied when it is set, then forgotten: the values below are what the body actually uses, so a
	 * preset is a starting point that can be adjusted rather than a link that overrides the
	 * adjustment every time the level loads.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Water")
	TObjectPtr<class UMobWaterLookPreset> LookPreset;

	/** Copies a look onto this body. Everything it sets is per-body data, so it costs no material. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Water")
	void ApplyLookPreset();

	/** Half the body's size on X and Y, in world units. The mesh is unit sized and scaled to this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Water", meta=(ForceUnits="cm"))
	FVector2D Extent = FVector2D(500.0, 500.0);

	/**
	 * How deep the water is, in world units.
	 *
	 * Answered rather than measured: the bed is only knowable per pixel, from the depth buffer, and
	 * the CPU has no access to that. Gameplay asking how deep it is here gets this number.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Water", meta=(ClampMin="0.0", ForceUnits="cm"))
	float Depth = 200.f;

	/** How far in from the edge the waves are flattened to nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Water", meta=(ClampMin="0.0", ForceUnits="cm"))
	float ShoreFadeDistance = 200.f;

	/** The waves this body carries. Unset means the world's own set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Waves")
	TObjectPtr<UMobWaterWavePreset> WavePreset;

	/** Scales this body's waves on top of the set it shares with everything else. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Waves", meta=(ClampMin="0.0"))
	float WaveAmplitude = 1.f;

	/**
	 * The colour comes from a gradient ramp indexed by depth rather than from an absorption between
	 * two colours.
	 *
	 * A ramp holds a whole palette and can hold hard steps in it, which is the difference between
	 * water that grades and water that is painted: absorption is an exponential and can only ever be
	 * smooth, so a toon surface built out of it is a surface fighting its own maths.
	 *
	 * A compiled variant: a body grading by absorption carries no ramp tap at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour")
	bool bGradientColor = false;

	/**
	 * Which palette in the atlas this body is graded by.
	 *
	 * The rows GA_MobWater ships are 0 Stylized, 1 Toon, 2 Tropical, 3 Swamp. A project bringing more
	 * points the material instance's Color Gradient at an atlas of its own; the row is per body, so
	 * one instance still covers a jade pool and a swamp.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(EditCondition="bGradientColor", ClampMin="0"))
	int32 GradientRow = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(EditCondition="!bGradientColor"))
	FLinearColor ShallowColor = FLinearColor(0.18f, 0.42f, 0.42f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(EditCondition="!bGradientColor"))
	FLinearColor DeepColor = FLinearColor(0.01f, 0.06f, 0.11f, 1.f);

	/** The water column over which the colour reaches the deep one, or the far end of the ramp. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(ClampMin="1.0", ForceUnits="cm"))
	float FadeDepth = 300.f;

	/** The water column over which the bed stops being visible at all. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(ClampMin="1.0", ForceUnits="cm"))
	float ClarityDepth = 500.f;

	/**
	 * How opaque the water is regardless of how much of it there is.
	 *
	 * 0 lets depth decide, which is physically right and is the wrong model for water meant to read
	 * as a flat colour: a shallow pool has almost no water in it, so it stays nearly transparent
	 * however low the clarity is set, and what shows through is the ground - which looks like the
	 * water being textured rather than like it being clear. 1 hides the bed entirely.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Colour", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinOpacity = 0.f;

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

	/** Calm water is nearly a mirror, which is what the specular reads. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Roughness = 0.02f;

	/** How fast the surface slides across the ground. Drifts the detail; it carries nothing yet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ForceUnits="cm/s"))
	FVector2D FlowVelocity = FVector2D::ZeroVector;

	/**
	 * Whether the flow is a compass direction or the actor's own.
	 *
	 * Local is what a river wants: the water runs down its own length, and rotating the actor to lay
	 * the spline out should carry the current round with it rather than leave it pointing north. It
	 * is one direction for the whole body either way - a spline that doubles back has water running
	 * the wrong way along the return, and the fix for that is per point flow, which is not here yet.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface")
	EMobWaterSpace FlowSpace = EMobWaterSpace::World;

	/** The flow as the material and the query see it, with the actor's rotation already in it. */
	UFUNCTION(BlueprintPure, Category="Water")
	FVector2D GetWorldFlowVelocity() const;

	/**
	 * Shoreline and crest foam.
	 *
	 * A compiled variant rather than an amount of zero, so a body without it does not carry the two
	 * foam samples or their maths at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam")
	bool bFoam = true;

	/**
	 * How far up from the bed foam reaches. 0 is no shoreline foam.
	 *
	 * A line, not a region. Set anywhere near how deep the water actually is and the foam covers all
	 * of it, which reads as a broken texture rather than as a value being too large.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(EditCondition="bFoam", ClampMin="0.0", ForceUnits="cm"))
	float ShoreFoamDepth = 8.f;

	/**
	 * How far in from the bank the edge foam line reaches, as a fraction of the shore fade.
	 *
	 * The line every body gets whether or not there is anything under it to be shallow against. On a
	 * shallow pool this does most of the work, because "where the water is thin" is everywhere.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(EditCondition="bFoam", ClampMin="0.0", ClampMax="1.0"))
	float EdgeFoamWidth = 0.12f;

	/**
	 * How much foam there is at all, whatever it is made of.
	 *
	 * Scales the finished mask, after the bands and the edges have been worked out, so turning it
	 * down thins every band evenly instead of eating them from the shore outwards. 0 is a body with
	 * no foam on it while every foam setting keeps its value; 1 is all of it.
	 *
	 * Quantised to a hundredth: it shares a data slot with EdgeFoamWidth, the primitive data being
	 * full at thirty six.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(EditCondition="bFoam", ClampMin="0.0", ClampMax="1.0"))
	float FoamOpacity = 1.f;

	/** How hard the surface has to fold before it breaks white. 1 is never. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(EditCondition="bFoam", ClampMin="0.0", ClampMax="1.0"))
	float CrestFoamThreshold = 0.55f;

	/**
	 * How far the noise moves the foam edge, as a fraction of the band width.
	 *
	 * The noise moves where the foam ends, not how bright it is. 0 is a clean contour; 1 is ragged.
	 *
	 * Clean by default. A wandering edge is the thing that reads as texture on a stylized surface,
	 * and it is a shoreline's own shape that should be doing that work.
	 *
	 * Quantised to a hundredth: it shares a data slot with FoamTextureOpacity, which the primitive
	 * data being full at thirty six is the reason for.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(EditCondition="bFoam", ClampMin="0.0", ClampMax="1.0"))
	float FoamNoiseAmount = 0.f;

	/**
	 * Foam carries a texture of its own instead of the water's.
	 *
	 * Off, foam is froth and nothing below it shows through, and FoamTextureOpacity instead decides
	 * how much of the water's own shading is let back through it. On, the foam is textured by
	 * FoamTexture in the shoreline's frame - streaks running away from the bank rather than along the
	 * current, which is the direction water actually leaves when it runs back down a shore.
	 *
	 * A compiled permutation, so a body with plain foam pays for neither the sample nor its
	 * coordinates.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(EditCondition="bFoam"))
	bool bFoamTexture = false;

	/**
	 * The pattern the foam carries, when it has one of its own.
	 *
	 * Unset, the material's own texture is used and this body draws with the shared instance like
	 * every other. Setting it is the one thing on this component that cannot be per-instance data -
	 * a texture is not a number - so the body takes a dynamic material instance of its own, and stops
	 * batching with the bodies that did not. Worth it for a hero pool, not for a hundred puddles.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(EditCondition="bFoam && bFoamTexture"))
	TObjectPtr<UTexture2D> FoamTexture;

	/**
	 * How textured the foam is, and which texture that means depends on bFoamTexture.
	 *
	 * With the foam's own texture off, this is how much of the water underneath shows through the
	 * foam: 0 is flat froth that hides the surface completely, 1 lets the wave normal, the sun and
	 * the sky read straight through it the way they did before foam was made opaque.
	 *
	 * With it on, this is how strongly FoamTexture marks the foam instead.
	 *
	 * One control for both because they are the same question - how much pattern is in the foam - and
	 * the two can never be answered at once, the switch having chosen which texture there is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(EditCondition="bFoam", ClampMin="0.0", ClampMax="1.0"))
	float FoamTextureOpacity = 0.f;

	/** How hard the foam edge is. Low is a wet fade up a beach; high is a stylized cut line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(EditCondition="bFoam", ClampMin="1.0"))
	float FoamSharpness = 3.f;

	/** Cuts the foam into this many steps. 0 leaves it smooth; a few gives painted rings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(EditCondition="bFoam", ClampMin="0.0", ClampMax="8.0"))
	float FoamBands = 0.f;

	/**
	 * How much water shows between the foam bands.
	 *
	 * 0 leaves them touching, which reads as one band with steps in it rather than as separate rings.
	 * Raising it slices gaps into the ramp so the water shows through between them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Foam", meta=(ClampMin="0.0", ClampMax="0.95"))
	float FoamBandSeparation = 0.f;

	/** How tight the sun's lobe is. Low is a wide sheen; high is hard sparkle over open water. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="1.0"))
	float GlintGloss = 380.f;

	/**
	 * How bright the sun is on the surface.
	 *
	 * This renderer has its local lights off and no sky atmosphere, so a lit translucent surface has
	 * almost nothing to reflect. The glint is most of what puts the light back.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0"))
	float GlintStrength = 2.5f;

	/**
	 * Cuts the sun lobe into separate glints.
	 *
	 * 0 leaves a continuous sheen, which is what real water shows. Raising it scatters the surface
	 * with distinct bright marks instead, which is what a stylized one shows.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0", ClampMax="1.0"))
	float GlintThreshold = 0.f;

	/**
	 * How many of the cut glints actually appear.
	 *
	 * Lower removes whole glints rather than dimming them all, so a sparser surface has the same
	 * marks in fewer places instead of an even wash of half-bright ones. Which places is decided by
	 * noise anchored to the world, so they scatter and stay put rather than crawling with the camera.
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

	/**
	 * Bends what is behind the surface.
	 *
	 * The one feature that reads scene colour, and the one a platform may refuse outright. Confirm it
	 * on the target before a level depends on the look.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Refraction")
	bool bRefraction = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Refraction", meta=(EditCondition="bRefraction", ClampMin="0.0", ClampMax="1.0"))
	float RefractionStrength = 0.3f;

	/**
	 * How much sky this body reflects. 0 is none.
	 *
	 * A reflection is a gradient, and a gradient is the thing toon shading is trying not to have.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reflection", meta=(ClampMin="0.0"))
	float ReflectionStrength = 1.f;

	/**
	 * Whether this body reads the interactive ripple field.
	 *
	 * The field is drawn once for the whole level, so a second body reading it is not a second cost -
	 * but the five samples that read it are per pixel, so a body nothing can reach is better without.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ripples")
	bool bRipples = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ripples", meta=(EditCondition="bRipples", ClampMin="0.0"))
	float RippleStrength = 1.f;

	/** How much of the scrolling detail normal reaches the surface. 0 is glass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0"))
	float DetailStrength = 1.f;

	/**
	 * How fast the surface detail drifts on its own, with no flow and no waves.
	 *
	 * Water with a still surface still moves, so the detail layers drift regardless. 0 turns that off
	 * and gives genuinely still water - which no combination of flow and wave amplitude could do,
	 * because this is not driven by either.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0"))
	float DetailScrollSpeed = 1.f;

	/** How pronounced the slow swell across the whole body is. Large scale, not texture. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface", meta=(ClampMin="0.0"))
	float MacroStrength = 2.2f;

	/**
	 * Whether a caustic is thrown onto whatever the water is sitting on.
	 *
	 * Drawn by the water itself, so it costs two samples rather than a pass. Off, those samples and
	 * the bed reconstruction leave the shader entirely.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Caustics")
	bool bCaustics = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Caustics", meta=(EditCondition="bCaustics", ClampMin="0.0"))
	float CausticStrength = 0.35f;

	/** The water column over which the caustic is lost. Focused light stops focusing as it deepens. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Caustics", meta=(EditCondition="bCaustics", ClampMin="1.0", ForceUnits="cm"))
	float CausticDepth = 400.f;

	/**
	 * Whether this body answers CPU queries.
	 *
	 * Off, it is scenery: it draws and nothing can ask where its surface is. A decorative ocean on the
	 * horizon has no business paying for a wave evaluation nobody reads, and that is the "full GPU, or
	 * output what a ship needs" split.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Query")
	bool bCpuQueries = true;

	/** The water at a point, or an invalid result when the point is outside this body. */
	UFUNCTION(BlueprintCallable, Category="Water")
	FMobWaterInfo GetWaterInfoAtLocation(const FVector& Location) const;

	/** Whether a world XY is over this body at all. */
	UFUNCTION(BlueprintPure, Category="Water")
	bool ContainsLocation(const FVector& Location) const;

	/**
	 * How much of a wave survives at a world XY, 0 at the bank and 1 out in open water.
	 *
	 * The same arithmetic MobWaterSurface.ush does from the mesh UV. Written twice for the same reason
	 * the wave maths is: the vertex shader cannot call this, and a query that disagreed with the
	 * vertices would float things above a surface that had already flattened.
	 */
	float GetShoreFade(const FVector& Location) const;

	/** Pushes every property into custom primitive data and picks the mesh and material. */
	void ApplySurface();

	/**
	 * The spline this body takes its shape from, when it has one.
	 *
	 * Set by AMobWaterBody. With it, containment and shore fade are asked of the spline instead of
	 * being worked out from an extent, because a lake's shoreline is whatever was drawn and no pair
	 * of numbers describes it.
	 */
	void SetShoreSpline(class UMobWaterSplineComponent* InSpline);

	const FMobWaterWaveParams& GetWaveParams() const;

protected:
	/**
	 * Writes one float of custom primitive data.
	 *
	 * Twice outside a game world, deliberately. The default array is what the level saves and what an
	 * unplayed viewport draws from; the runtime array is what the renderer actually reads. Writing
	 * only the first leaves the viewport correct and the game wrong, and writing only the second
	 * leaves a body of water that resets every time the level loads.
	 */
	void WriteWaterData(int32 Index, float Value);
	void WriteWaterData2(int32 Index, const FVector2D& Value);
	void WriteWaterData3(int32 Index, const FLinearColor& Value);

	/** Which material variant this body's settings ask for, as a mask of MobWaterVariant flags. */
	int32 WantedVariant() const;

	/** Puts a per-body foam texture on, or takes the material it needed back off again. */
	void ApplyFoamTexture(UMaterialInterface* Shared);

	/** Only exists while a body carries a foam texture of its own. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FoamMaterial;

	TWeakObjectPtr<class UMobWaterSplineComponent> ShoreSpline;
};
