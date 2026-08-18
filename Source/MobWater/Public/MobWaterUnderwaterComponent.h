// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "MobWaterTypes.h"
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
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
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

	/**
	 * Snell's window: the world above, compressed into the cone the surface lets through.
	 *
	 * Water bends light, so everything above the surface reaches a submerged eye inside a disc of
	 * about forty-nine degrees, and beyond that disc the surface is a mirror. It is the one cue that
	 * says the eye is under water rather than behind a coloured pane, because it is a change of
	 * geometry and nothing done to the colour can stand in for it.
	 *
	 * A compiled permutation rather than a strength, and the disc itself costs one texture read -
	 * what it is filled from is the expensive question, and that is Window Source.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Snell Window")
	bool bSnellWindow = true;

	/**
	 * Where the world seen through the window is read from.
	 *
	 * Sky is one tap and knows nothing of the level. Scene Capture is a second view of the world and
	 * costs what a second view costs, running only while the eye is under the surface.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Snell Window", meta=(EditCondition="bSnellWindow"))
	EMobWaterWindowSource WindowSource = EMobWaterWindowSource::Sky;

	/**
	 * The water's refractive index, which is the only number the size of the window comes from.
	 *
	 * 1.333 is fresh water and puts the edge of the disc at 48.6 degrees. Raising it closes the
	 * window and is what a denser or dirtier medium does; below 1 there is no critical angle and no
	 * window at all, which is why it clamps there.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Snell Window", meta=(EditCondition="bSnellWindow", ClampMin="1.0", ClampMax="2.5"))
	float RefractionIndex = 1.333f;

	/**
	 * How much of the window's edge is softened, as a fraction of its radius.
	 *
	 * The edge is drawn by the surface refusing to transmit past the critical angle, so this is not
	 * what makes it: it only stops the last ring of pixels aliasing into a stair. Large values eat
	 * the bright rim, which is the part of the effect people recognise.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Snell Window", meta=(EditCondition="bSnellWindow", ClampMin="0.0", ClampMax="0.5"))
	float WindowFeather = 0.06f;

	/** How bright the world above is through the window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Snell Window", meta=(EditCondition="bSnellWindow", ClampMin="0.0"))
	float WindowBrightness = 1.f;

	/**
	 * How bright the ring at the edge of the window is.
	 *
	 * Everything from the horizon up to a few degrees above it arrives inside that ring, so it
	 * carries more light than anywhere else in the view. 0 leaves the disc with a plain edge.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Snell Window", meta=(EditCondition="bSnellWindow", ClampMin="0.0", ClampMax="2.0"))
	float RimStrength = 0.5f;

	/**
	 * How much thicker the water reads outside the window.
	 *
	 * Beyond the critical angle the surface mirrors the water below, and what that mirror shows is
	 * already behind this quad. So nothing is reflected here; the water is thickened where a mirror
	 * would be, which is what makes the disc read as a disc. 0 leaves the water outside it alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Snell Window", meta=(EditCondition="bSnellWindow", ClampMin="0.0", ClampMax="1.0"))
	float MirrorStrength = 0.35f;

	/**
	 * How wide the capture of the world above sees, in degrees.
	 *
	 * It has to hold the whole window, which is the sky from straight up to the horizon, and no
	 * perspective capture can reach the horizon at all. Wider covers more of the disc and spends its
	 * resolution on the periphery; narrower is sharper overhead and smears sooner at the rim.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Snell Window", meta=(EditCondition="bSnellWindow && WindowSource == EMobWaterWindowSource::SceneCapture", ClampMin="60.0", ClampMax="170.0", ForceUnits="deg"))
	float CaptureFov = 150.f;

	/**
	 * The eye crossing the surface, either way.
	 *
	 * This is the camera's own crossing and nothing to do with any character's. A swimmer whose head
	 * is under is in water; a camera three metres behind them is not, and the effects that belong to
	 * each are different ones. Bind UMobWaterSubsystem::OnViewSubmergedChanged instead where the
	 * plane is one the plugin attached, because that one is rebuilt whenever the view changes hands.
	 */
	UPROPERTY(BlueprintAssignable, Category="Underwater")
	FMobWaterSubmerged OnSubmergedChanged;

	UFUNCTION(BlueprintPure, Category="Underwater")
	bool IsSubmerged() const { return bSubmerged; }

	/** 0 above the surface, 1 fully under it. */
	UFUNCTION(BlueprintPure, Category="Underwater")
	float GetSubmersion() const { return Submersion; }

	/** How far the eye is below the surface, and zero above it. */
	UFUNCTION(BlueprintPure, Category="Underwater")
	float GetImmersionDepth() const { return ImmersionDepth; }

	/**
	 * The deepest the eye reached without coming up, kept after it has.
	 *
	 * What a lens effect wants at the moment of surfacing: depth is nothing by then, and how wet the
	 * lens should be is a question about where it has just been. Cleared on going back under.
	 */
	UFUNCTION(BlueprintPure, Category="Underwater")
	float GetDeepestImmersion() const { return DeepestImmersion; }

	/** Which of the plane's materials this one wants, as a mask of MobWaterUnderwaterVariant flags. */
	UFUNCTION(BlueprintPure, Category="Underwater")
	int32 GetWantedVariant() const;

protected:
	void ApplyPlacement();

	/** Picks the permutation carrying the features this plane asked for. */
	void ApplyMaterial();

	/**
	 * Keeps the view of the world above alive while the eye is under, and gone the rest of the time.
	 *
	 * SurfaceZ is where the water is over the eye, which is where the capture sits: refraction
	 * happens at the surface rather than at the eye, and a capture held down at the eye would be
	 * looking up through the water it is meant to be seeing past.
	 */
	void UpdateCapture(bool bWanted, const FVector& Eye, float SurfaceZ);

	/** Leaves every registered surface out of the capture, so the water is not its own ceiling. */
	void HideWaterFromCapture();

	bool bSubmerged = false;
	float Submersion = 0.f;
	float ImmersionDepth = 0.f;
	float DeepestImmersion = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<class USceneCaptureComponent2D> Capture;

	/** What the capture was told to leave out, so it is only rebuilt when a body comes or goes. */
	int32 HiddenBodies = -1;

	/** What is on the plane now, so a permutation is looked up when it changes and not every frame. */
	int32 AppliedVariant = INDEX_NONE;
};
