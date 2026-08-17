// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "MobWaterTypes.h"
#include "Components/SceneComponent.h"
#include "MobWaterExclusionComponent.generated.h"

/** How many exclusion volumes the surface evaluates. Matches MOB_WATER_EXCLUSIONS in the shader. */
#define MOB_WATER_EXCLUSION_SLOTS 4

/**
 * An area water is kept out of.
 *
 * Evaluated by the surface rather than stamped into the ripple field, deliberately: a hull moves, and
 * a field that follows the camera and fades at its border is the wrong place for something whose edge
 * has to stay exactly where the geometry is. Evaluated, it is arithmetic that cannot smear.
 *
 * Only so many fit, so the ones nearest the view win. That is the same trade the fog blockers in
 * MobLights make, for the same reason: the alternative is a texture, and a texture cannot hold an
 * edge sharp enough for a boat.
 *
 * Being a plan view, it has no top and no bottom. Water is kept out of a footprint, not out of a
 * storey - a bridge over a lake does not hold water off its deck.
 */
UCLASS(ClassGroup=Rendering, meta=(BlueprintSpawnableComponent, DisplayName="Mob Water Exclusion"))
class MOBWATER_API UMobWaterExclusionComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMobWaterExclusionComponent();

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	//~ End UActorComponent Interface

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Exclusion")
	EMobWaterExclusionShape Shape = EMobWaterExclusionShape::Box;

	/** Half the volume's size, in world units. A disc or sphere uses X as its radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Exclusion", meta=(ForceUnits="cm"))
	FVector2D Extent = FVector2D(200.0, 400.0);

	/**
	 * How much water it removes. 1 takes it away entirely.
	 *
	 * Less than 1 thins it rather than clearing it, which is what a half-submerged grating wants.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Exclusion", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Strength = 1.f;

	/** How far in from the edge the water fades out rather than stopping, in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Exclusion", meta=(ClampMin="0.0", ForceUnits="cm"))
	float EdgeSoftness = 20.f;

	/** Whether this also stops things being counted as submerged inside it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Exclusion")
	bool bBlocksSubmersion = true;

	/** How much water this volume removes at a world point, 0 outside and 1 fully excluded. */
	float GetExclusionAt(const FVector& Location) const;

	/** The two vectors the shader reads this volume as. */
	void PackForShader(FLinearColor& OutA, FLinearColor& OutB) const;
};
