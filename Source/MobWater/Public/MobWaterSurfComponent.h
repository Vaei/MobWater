// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "MobWaterSurfComponent.generated.h"

/** What the wave that arrived here was doing when it got here. */
USTRUCT(BlueprintType)
struct MOBWATER_API FMobWaterSurfImpact
{
	GENERATED_BODY()

	/** Where it happened: this component's column, at the height the wave put the surface. */
	UPROPERTY(BlueprintReadOnly, Category="Surf")
	FVector Location = FVector::ZeroVector;

	/** The surface normal there, which is the face the water arrived on. */
	UPROPERTY(BlueprintReadOnly, Category="Surf")
	FVector Normal = FVector::UpVector;

	/** Which way the water is thrown. This component's forward, so it is aimed rather than derived. */
	UPROPERTY(BlueprintReadOnly, Category="Surf")
	FVector Direction = FVector::ForwardVector;

	/** How hard, 0 at the threshold that fired it and 1 at the hardest the surface can fold. */
	UPROPERTY(BlueprintReadOnly, Category="Surf")
	float Strength = 0.f;

	/**
	 * How far the surface stood over this point when it fired, in world units.
	 *
	 * Where to throw the spray from, rather than how hard: put the component at the foot of the rock
	 * and this is how far up it the water reached.
	 */
	UPROPERTY(BlueprintReadOnly, Category="Surf")
	float Rise = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMobWaterSurfImpactSignature, const FMobWaterSurfImpact&, Impact);

/**
 * One place waves break, and an event for when one does.
 *
 * Spray is the one part of surf that is not a surface. What leaves the water when a wave meets a
 * rock is a plume with a life of its own, and no heightfield holds one - a Gerstner wave pinches to
 * a cusp and stops, and a height cannot be two values over the same point however hard it is pushed.
 * So this plugin does not draw the burst. It says when and where one is due, and a project throws
 * whatever it throws.
 *
 * It runs entirely on the CPU, out of the analytic wave model, which is what makes it worth having
 * rather than reading back a render target. Two machines given the same instant fire the same
 * impacts, so a project may broadcast one, replicate one, or let each machine find its own.
 *
 * Nothing here is on by default beyond the component existing. It costs one branch with nothing
 * bound to it, one distance check past MaxDistance, and one water query otherwise - and the
 * conditions a project actually wants are its own, so bind OnSurfImpact and gate it there.
 *
 * Deactivate turns one off; mob.Water.Surf turns all of them off.
 */
UCLASS(ClassGroup=Rendering, meta=(BlueprintSpawnableComponent, DisplayName="Mob Water Surf"))
class MOBWATER_API UMobWaterSurfComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMobWaterSurfComponent();

	//~ Begin UActorComponent Interface
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

	/**
	 * A wave has arrived and is folding harder than Threshold.
	 *
	 * Fires once a wave and rearms as the surface flattens behind it, so a swell running past leaves
	 * one impact per crest rather than one a frame for as long as the crest is over the point.
	 */
	UPROPERTY(BlueprintAssignable, Category="Surf")
	FMobWaterSurfImpactSignature OnSurfImpact;

	/** Stops this one without removing it, for a project that turns surf off with its own conditions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surf")
	bool bEnabled = true;

	/**
	 * How hard the surface has to fold here before a wave counts as breaking.
	 *
	 * The same number crest foam is cut at, so an impact lands on a crest that is already white. A
	 * point inside an exclusion volume's shoal reaches it sooner, because a shoaling wave leans as
	 * it rises - which is the whole reason to put one of these against a cliff rather than out at sea.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surf", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Threshold = 0.5f;

	/**
	 * How far the fold has to fall back before this can fire again, as a fraction of Threshold.
	 *
	 * Hysteresis rather than a timer, so it paces itself off the sea instead of off a number nobody
	 * can set correctly for two swells at once. A long slow ocean fires slowly and a choppy harbour
	 * fires often, from the same value.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surf", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Rearm = 0.6f;

	/**
	 * How far from a view this still fires, in world units. 0 is everywhere.
	 *
	 * Spray nobody is near enough to see is a particle system nobody is near enough to see, and a
	 * coastline is a great many of these. Checked before the water is queried, so a point out of
	 * range costs a distance and nothing else.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surf", meta=(ClampMin="0.0", ForceUnits="cm"))
	float MaxDistance = 8000.f;

	/** Whether a wave is currently over this point, which is what the rearm is tracking. */
	UFUNCTION(BlueprintPure, Category="Surf")
	bool IsBreaking() const { return bBreaking; }

	/** The fold the last query found here, whether or not it fired. */
	UFUNCTION(BlueprintPure, Category="Surf")
	float GetFold() const { return LastFold; }

protected:
	/** True between a crest crossing Threshold and the surface falling back past the rearm. */
	bool bBreaking = false;

	/** What the last query answered, so a project can drive something continuous off the same tick. */
	float LastFold = 0.f;
};
