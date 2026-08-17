// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "MobWaterUnderwaterComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMobWaterSubmerged, bool, bSubmerged);

/**
 * What being under the surface looks like.
 *
 * Attach it to a camera. It is a plane held just in front of the near clip, drawn only while the
 * camera is under water, that reads the depth buffer and absorbs by how far the light travelled to
 * reach the eye - so the world goes green and then gone with distance, rather than being tinted
 * evenly the way a flat overlay would.
 *
 * A plane rather than a post-process pass, for the same reason the lights in MobLights are meshes:
 * this renderer has no spare full-screen pass, and a material on a quad is one the renderer already
 * knows how to draw.
 */
UCLASS(ClassGroup=(Mob), meta=(BlueprintSpawnableComponent, DisplayName="Mob Water Underwater"),
	hidecategories=(Collision, Physics, Navigation, HLOD, VirtualTexture, RayTracing))
class MOBWATER_API UMobWaterUnderwaterComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UMobWaterUnderwaterComponent();

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

	/**
	 * How far in front of the camera the plane sits, in world units.
	 *
	 * Has to clear the near clip plane and nothing more. Further out and something can get between
	 * the camera and it, which shows as a hole in the water.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Underwater", meta=(ClampMin="1.0", ForceUnits="cm"))
	float Distance = 15.f;

	/** How wide the plane is at that distance. Wide enough to cover the widest field of view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Underwater", meta=(ClampMin="1.0", ForceUnits="cm"))
	float Size = 200.f;

	/**
	 * How far light travels through the water before it is gone, in world units.
	 *
	 * This is the whole read of how clean the water is. A few metres is a silty river; tens of metres
	 * is open sea.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Underwater", meta=(ClampMin="1.0", ForceUnits="cm"))
	float Clarity = 1200.f;

	/** What the water absorbs down to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Underwater")
	FLinearColor AbsorbColor = FLinearColor(0.02f, 0.09f, 0.13f);

	/**
	 * How far below the surface before it is fully underwater, in world units.
	 *
	 * Crossing the surface instantly is what makes a camera at the waterline flicker between two
	 * completely different images every frame it bobs. The waterline itself is drawn geometrically,
	 * so this is only the short fade that brings the plane in at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Underwater", meta=(ClampMin="0.1", ForceUnits="cm"))
	float CrossFadeDepth = 12.f;

	/**
	 * How tall the band of water clinging to the lens is, in world units.
	 *
	 * The waterline is where the surface plane crosses the quad, so it is a real line across the
	 * view that tilts as a swell passes rather than a fade over the whole screen. This is the only
	 * part of it that is not geometry: how far either side of that line the water is neither clearly
	 * above nor clearly below.
	 *
	 * Without it the two halves meet at a hard cut, which reads as a rendering seam rather than as a
	 * surface - the one thing everybody recognises about a camera at the waterline is the bead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Meniscus", meta=(ClampMin="0.1", ForceUnits="cm"))
	float MeniscusThickness = 2.5f;

	/** How much denser and brighter the bead is than the water behind it. 0 leaves a plain cut. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Meniscus", meta=(ClampMin="0.0", ClampMax="2.0"))
	float MeniscusStrength = 1.f;

	/**
	 * Light dappling down through the water, seen from under it.
	 *
	 * A compiled permutation rather than a strength of zero, because it is two texture reads on a
	 * quad that covers the screen - which is the most expensive place in this renderer to carry
	 * something nobody asked for.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Caustics")
	bool bCaustics = true;

	/** How bright the dappling is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Caustics", meta=(EditCondition="bCaustics", ClampMin="0.0"))
	float CausticStrength = 0.6f;

	/** The world size the caustic web tiles over, in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Caustics", meta=(EditCondition="bCaustics", ClampMin="1.0", ForceUnits="cm"))
	float CausticScale = 400.f;

	/**
	 * How far down the dappling is lost, in world units.
	 *
	 * Caustics are focused light and focus is lost with depth, so this is what stops a trench at
	 * thirty metres being as dappled as a metre of shallows.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Caustics", meta=(EditCondition="bCaustics", ClampMin="1.0", ForceUnits="cm"))
	float CausticDepth = 800.f;

	UPROPERTY(BlueprintAssignable, Category="Underwater")
	FMobWaterSubmerged OnSubmergedChanged;

	UFUNCTION(BlueprintPure, Category="Underwater")
	bool IsSubmerged() const { return bSubmerged; }

	/** 0 above the surface, 1 fully under it. */
	UFUNCTION(BlueprintPure, Category="Underwater")
	float GetSubmersion() const { return Submersion; }

protected:
	void ApplyPlacement();

	/** Picks the plain plane or the one that carries caustics, which are separate permutations. */
	void ApplyMaterial();

	bool bSubmerged = false;
	float Submersion = 0.f;
};
