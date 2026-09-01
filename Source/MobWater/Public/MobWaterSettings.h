// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "MobWaterHooks.h"
#include "MobWaterTypes.h"
#include "Engine/DeveloperSettings.h"
#include "MobWaterSettings.generated.h"

class UMaterialInterface;
class UMaterialParameterCollection;
class UMobWaterWavePreset;
class UStaticMesh;

/**
 * Which mesh, which material and which waves each shape of water uses.
 *
 * Settings rather than constants, so a project can point a shape at its own material without
 * subclassing the component.
 */
UCLASS(Config=Engine, DefaultConfig, BlueprintType, meta=(DisplayName="Mob Water"))
class MOBWATER_API UMobWaterSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMobWaterSettings();

	/** The surface each shape rasterises. Unit sized; the component scales it to the extent. */
	UPROPERTY(EditAnywhere, Config, Category="Surface")
	TMap<EMobWaterShape, TSoftObjectPtr<UStaticMesh>> SurfaceMeshes;

	UPROPERTY(EditAnywhere, Config, Category="Surface")
	TMap<EMobWaterShape, FMobWaterMaterialSet> Materials;

	/**
	 * The waves a newly placed body of each shape starts with.
	 *
	 * A pond that arrives carrying an ocean's swell has to be corrected before it can be judged, and
	 * the correction is the same every time, so it belongs here rather than in a habit.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Waves")
	TMap<EMobWaterShape, TSoftObjectPtr<UMobWaterWavePreset>> DefaultWavePresets;

	/** The waves a body of this shape falls back to when it carries none of its own. */
	static class UMobWaterWavePreset* GetDefaultWavePreset(EMobWaterShape Shape);

	/**
	 * Where the wave set and the clock every water material shares are read from.
	 *
	 * The waves are a property of the world's weather rather than of any one body, so they arrive as a
	 * collection. The alternative is a dynamic material instance per body, which is the cost this
	 * whole plugin is arranged to avoid.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Waves")
	TSoftObjectPtr<UMaterialParameterCollection> ParameterCollection;

	/**
	 * How long water time runs before it folds back on itself, in seconds.
	 *
	 * Every wave is periodic, so folding is free of any visible seam as long as the period is a whole
	 * number of turns for each of them - which it is not, in general. Five minutes is short enough that
	 * a float still has fractions to work with after a long session and long enough that nothing
	 * repeats visibly, and it is what a baked spectrum's loop is expected to divide into.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Waves", meta=(UIMin="10", UIMax="3600", ClampMin="1", ForceUnits="s"))
	float TimeLoopPeriod = 300.f;

	/**
	 * The sky every body of water reflects, as a long-latitude image.
	 *
	 * Point this at whatever the level's backdrop uses and Set Up Water writes it into every generated
	 * instance. It is a material parameter rather than a collection one because a collection cannot
	 * hold a texture, which is also why switching it at runtime is not something this offers - the
	 * intensity and rotation are on the collection and can be changed whenever.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Reflection")
	TSoftObjectPtr<class UTexture> ReflectionTexture;

	/**
	 * What a camera under the surface looks through, indexed by MobWaterUnderwaterVariant.
	 *
	 * A set rather than one material, because caustics and Snell's window are both texture reads on
	 * a quad that covers the screen, and a compiler cannot fold away per-instance data however small
	 * it is. A plane asked for a combination that was never generated drops one feature at a time
	 * rather than falling straight to the plain one.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Underwater")
	FMobWaterMaterialSet UnderwaterMaterials;

	/**
	 * What a waterfall is drawn with, indexed by MobWaterFallVariant.
	 *
	 * Its own set rather than a fifth shape in Materials, because a fall is not a shape of the
	 * surface: it shares the clock, the textures and the sky with a body of water and not one line of
	 * its maths. A fall asked for a combination that was never generated drops one feature at a time
	 * rather than falling straight to the plain one.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Waterfall")
	FMobWaterMaterialSet FallMaterials;

	/** Variant is a mask of MobWaterFallVariant flags. */
	static UMaterialInterface* GetFallMaterial(int32 Variant);

	/**
	 * Where a scene capture writes the world above for Snell's window to read.
	 *
	 * One target, so one eye at a time gets a captured window. Split screen shares it, and the
	 * second view would be reading the first view's sky - which is why the sky source is the default
	 * and this one is asked for.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Underwater")
	TSoftObjectPtr<class UTextureRenderTarget2D> SnellTarget;

	/**
	 * Whether the local player's camera is given an underwater view of its own.
	 *
	 * Off, the component is something a project attaches itself. That is what a game with its own
	 * camera rig wants, and it is not something anyone should have to find out by walking into a lake
	 * and seeing the surface from below with nothing in front of it.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Underwater")
	bool bAutoUnderwater = true;

	/** What gets attached. Subclass to change what being under water looks like. */
	UPROPERTY(EditAnywhere, Config, Category="Underwater", meta=(EditCondition="bAutoUnderwater"))
	TSoftClassPtr<class UMobWaterUnderwaterComponent> UnderwaterComponent;

	/**
	 * Where baked mesh outlines are drawn, so the surface can read them back.
	 *
	 * A target of its own rather than the ripple field's spare channel, which is not writable: a UI
	 * domain material reaches its target through emissive, and emissive is three channels.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Exclusion")
	TSoftObjectPtr<class UTextureRenderTarget2D> ExclusionTarget;

	/** What draws them. */
	UPROPERTY(EditAnywhere, Config, Category="Exclusion")
	TSoftObjectPtr<UMaterialInterface> ExclusionFieldMaterial;

	/** Whether the ripple field is drawn at all. Off costs nothing and leaves the field flat. */
	UPROPERTY(EditAnywhere, Config, Category="Ripples")
	bool bRipplesEnabled = true;

	/**
	 * Where the field is drawn, and where it is read back from.
	 *
	 * Two, because a wave equation needs the frame before it while it is writing this one, and a field
	 * that scrolls with the view has to be resampled as it goes.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Ripples")
	TSoftObjectPtr<class UTextureRenderTarget2D> RippleTarget;

	UPROPERTY(EditAnywhere, Config, Category="Ripples")
	TSoftObjectPtr<class UTextureRenderTarget2D> RippleHistory;

	/** Advances the wave equation by one step and scrolls it to follow the view. */
	UPROPERTY(EditAnywhere, Config, Category="Ripples")
	TSoftObjectPtr<UMaterialInterface> RippleStepMaterial;

	/** Copies the stepped field back into the history. */
	UPROPERTY(EditAnywhere, Config, Category="Ripples")
	TSoftObjectPtr<UMaterialInterface> RippleCopyMaterial;

	/**
	 * Copies the stepped field back into the history and adds whatever is standing in the water.
	 *
	 * A material rather than a canvas draw, and it has to stay one: canvas stamping and
	 * DrawMaterialToRenderTarget are two paths to the same target that do not reliably land in the
	 * order they were asked for, and it is the stamp that loses - so the field receives nothing while
	 * everything along the way reports having pushed it.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Ripples")
	TSoftObjectPtr<UMaterialInterface> RippleStampMaterial;

	/**
	 * How wide the field is, in world units. It reaches half of this from its centre, and the outer
	 * seventh of that is faded out so its border is not a square drawn on the water.
	 *
	 * The whole cost of the field is fixed by its resolution, so this trades reach against how fine a
	 * ripple can be. Two thousand across a 256 target is about eight centimetres a texel, which puts
	 * eighteen of them across a character's ripple - enough for it to read as round. Twice the width
	 * is half of that, and a stamp nine texels wide is a block, not a ripple.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Ripples", meta=(UIMin="1000", UIMax="20000", ClampMin="100", ForceUnits="cm"))
	float RippleExtent = 2000.f;

	/**
	 * How fast a ripple travels, as the square of texels per step.
	 *
	 * Squared because that is the term the wave equation actually takes, and writing it as the speed
	 * would mean squaring it here to hide where the stability limit is. Above 0.5 the equation goes
	 * unstable and the field fills with noise that never settles, so this is clamped below it rather
	 * than left to be discovered.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Ripples", meta=(UIMin="0.05", UIMax="0.45", ClampMin="0.01", ClampMax="0.45"))
	float RippleSpeed = 0.28f;

	/** How much of a ripple survives each step. Below 1 it dies out; at 1 the pond never settles. */
	UPROPERTY(EditAnywhere, Config, Category="Ripples", meta=(UIMin="0.9", UIMax="1.0", ClampMin="0.5", ClampMax="1.0"))
	float RippleDamping = 0.985f;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/**
	 * Re-applies every body of water in every loaded world.
	 *
	 * A component picks its mesh and material up when it registers, so pointing a shape at a material
	 * after the level was opened reaches nothing that is already standing in it - the water stays
	 * blank and looks like the material is broken rather than unread.
	 */
	static void RefreshPlacedWater();
#endif

	static UStaticMesh* GetSurfaceMesh(EMobWaterShape Shape);

	/** Variant is a mask of MobWaterVariant flags. */
	static UMaterialInterface* GetMaterial(EMobWaterShape Shape, int32 Variant);

	/** Variant is a mask of MobWaterUnderwaterVariant flags. */
	static UMaterialInterface* GetUnderwaterMaterial(int32 Variant);

	/**
	 * Project material functions spliced into the masters as they are generated.
	 *
	 * The point of them is that a generated material cannot be hand-edited and keep the edit: the
	 * next authoring run empties the graph and rebuilds it. Put the project's own maths in a
	 * function, name it here, and every regenerate wires it back in.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Hooks")
	TArray<FMobWaterHook> Hooks;
};
