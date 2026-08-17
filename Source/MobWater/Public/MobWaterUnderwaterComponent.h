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
	 * completely different images every frame it bobs.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Underwater", meta=(ClampMin="0.1", ForceUnits="cm"))
	float CrossFadeDepth = 12.f;

	UPROPERTY(BlueprintAssignable, Category="Underwater")
	FMobWaterSubmerged OnSubmergedChanged;

	UFUNCTION(BlueprintPure, Category="Underwater")
	bool IsSubmerged() const { return bSubmerged; }

	/** 0 above the surface, 1 fully under it. */
	UFUNCTION(BlueprintPure, Category="Underwater")
	float GetSubmersion() const { return Submersion; }

protected:
	void ApplyPlacement();

	bool bSubmerged = false;
	float Submersion = 0.f;
};
