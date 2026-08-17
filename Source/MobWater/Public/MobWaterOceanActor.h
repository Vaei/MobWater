// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MobWaterOceanActor.generated.h"

class UMobWaterComponent;
class UBillboardComponent;

/**
 * Water with no edge.
 *
 * A disc kept centred on whatever is looking at it, big enough to reach the horizon. It has no bank,
 * so nothing attenuates its waves and its extent is only how far it is drawn - move far enough and
 * the ocean has come with you.
 *
 * The surface itself is a function of world position, so following the camera moves the window and
 * not the water: sail a hundred metres and the swell you were looking at is a hundred metres behind
 * you, because it was never attached to the mesh in the first place.
 */
UCLASS(ClassGroup=Rendering, hidecategories=(Input, Collision, Replication, Physics, HLOD, Cooking, DataLayers))
class MOBWATER_API AMobWaterOcean : public AActor
{
	GENERATED_BODY()

public:
	AMobWaterOcean();

	//~ Begin AActor Interface
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostRegisterAllComponents() override;
	//~ End AActor Interface

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Water")
	TObjectPtr<UMobWaterComponent> Water;

	UMobWaterComponent* GetWaterComponent() const { return Water; }

	/** Whether the ocean follows the view. Off, it stays where it was placed and has an edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ocean")
	bool bFollowView = true;

	/**
	 * How far the ocean moves at a time, in world units.
	 *
	 * Snapped rather than continuous, and the snap is what stops it swimming. Following the camera
	 * exactly means the mesh's vertices slide underneath the wave function every frame, and a wave
	 * evaluated at a moving vertex crawls even though the water itself is standing still.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ocean", meta=(ClampMin="1.0", ForceUnits="cm"))
	float FollowSnap = 500.f;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> Sprite;
#endif

protected:
	/** The height the ocean sits at, kept as the actor is dragged around by its own following. */
	double SurfaceZ = 0.0;
	bool bHasSurfaceZ = false;
};
