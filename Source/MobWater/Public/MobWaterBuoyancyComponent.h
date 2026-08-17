// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "MobWaterBuoyancyComponent.generated.h"

class UPrimitiveComponent;

/**
 * One point that holds a body up.
 *
 * A sphere rather than a shape, because what buoyancy needs from geometry is how much of it is under
 * the surface, and a sphere answers that from one height difference. A hull is several of these.
 */
USTRUCT(BlueprintType)
struct MOBWATER_API FMobWaterPontoon
{
	GENERATED_BODY()

	/** Where it sits on the body, in the component's own space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pontoon")
	FVector Offset = FVector::ZeroVector;

	/**
	 * How far above and below its centre it reaches.
	 *
	 * This is the whole of the smoothing. A pontoon is fully under at Radius below the surface and
	 * fully out at Radius above it, so a small radius on a choppy sea is a body that is either
	 * held up completely or not at all, and it hammers.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pontoon", meta=(ClampMin="1.0", ForceUnits="cm"))
	float Radius = 50.f;

	/** How much of it is under, 0 clear of the water and 1 fully in. Filled in every tick. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Pontoon")
	float Submersion = 0.f;

	/** Where the surface was over it, last time it was asked. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Pontoon")
	float SurfaceZ = 0.f;

	/** Whether there was any water over it at all. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Pontoon")
	bool bInWater = false;
};

/**
 * Makes a physics body float.
 *
 * The one thing GetWaterInfoAtLocations was shaped for, and the reason the whole wave model is a
 * pure function of position and time: the surface a boat is held up by has to be the surface the
 * client draws, and a dedicated server has no ripple field, no foam and no GPU to hold them in. What
 * both machines can have is the analytic wave set, evaluated from the same clock.
 *
 * That is a claim about the water and not about the physics. Two machines given the same water and
 * the same forces still diverge, because a physics solver is not deterministic across builds or
 * frame rates - this component removes water as a cause of that, and nothing else.
 *
 * Ripples, wakes and stamped foam are not in the answer and cannot be. A boat does not bob on its
 * own wake here.
 */
UCLASS(ClassGroup=(Mob), meta=(BlueprintSpawnableComponent, DisplayName="Mob Water Buoyancy"))
class MOBWATER_API UMobWaterBuoyancyComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMobWaterBuoyancyComponent();

	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

	/**
	 * Where this body is held up, in this component's space.
	 *
	 * Four in a rectangle is a raft. One is a buoy, and it will not stay upright, because a single
	 * point applies no torque - which is correct and is usually not what was wanted.
	 *
	 * Keep them level with the centre of mass or above it. Lift applied below the centre of mass has
	 * an arm that tips the body further the moment it tilts, and on anything as tall as it is wide
	 * that beats the righting couple the four of them make between them. A hull that rolls over on
	 * calm water is this and nothing else.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buoyancy")
	TArray<FMobWaterPontoon> Pontoons;

	/**
	 * Fills Pontoons with four corners of the body's own bounds, if it is empty when play begins.
	 *
	 * A default rather than a feature. A component dropped on a crate with nothing else set should
	 * float the crate, not sink it while the details panel says buoyancy is enabled.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buoyancy")
	bool bAutoPontoons = true;

	/** Builds the four corner pontoons now, from whatever the body currently measures. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Buoyancy")
	void BuildPontoonsFromBounds();

	/**
	 * How hard the water pushes back, as a multiple of the body's own weight.
	 *
	 * Deliberately a multiple of weight rather than a density and a displaced volume. A sphere's
	 * volume is not the volume of the hull it stands for, so a physical model would have to be
	 * retuned every time a pontoon moved - and it answers a question nobody asks. This one answers
	 * the question everybody asks, because the equilibrium falls straight out of it: at rest the
	 * pontoons settle at 1/Coefficient submerged, so 2 puts their centres exactly on the waterline
	 * and 4 leaves them a quarter under.
	 *
	 * Below 1 the body cannot hold itself up at all and sinks however deep the water is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buoyancy", meta=(ClampMin="0.0"))
	float BuoyancyCoefficient = 2.f;

	/**
	 * How hard the water resists a pontoon moving up or down through it.
	 *
	 * Without this a float is a spring with nothing damping it, and a body dropped in oscillates
	 * forever at whatever frequency its mass and this coefficient make. It is scaled by how much of
	 * the pontoon is actually in the water, so a body in the air is not being damped by nothing.
	 *
	 * It is a rate, per second, and reads as one: at full submersion this is how many times a second
	 * the vertical speed is taken away, so 6 settles in about a sixth of a second and 1 wallows. A
	 * body that leaves the water entirely is not damped at all while it is out, which is why a crate
	 * dropped from a height bounces for longer than this number suggests.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buoyancy", meta=(ClampMin="0.0"))
	float VerticalDamping = 6.f;

	/** How hard the water drags a pontoon sideways, per second. What stops a raft sliding forever. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buoyancy", meta=(ClampMin="0.0"))
	float LateralDamping = 1.f;

	/** How hard the water resists the body turning, per second. What stops a raft spinning forever. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buoyancy", meta=(ClampMin="0.0"))
	float AngularDamping = 1.f;

	/**
	 * How much of the water's flow the body is carried by.
	 *
	 * 1 is a body that ends up travelling at the current. 0 is one moored to the world, which is what
	 * a jetty wants and what a barrel does not.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buoyancy", meta=(ClampMin="0.0"))
	float FlowStrength = 1.f;

	/**
	 * Whether the body is tipped by the slope of the water as well as lifted by its height.
	 *
	 * Height alone already tips anything with more than one pontoon, because the pontoons are at
	 * different heights on a wave. This adds the surface's own normal on top, which is what makes a
	 * small boat lie along a swell instead of straddling it - and on a body far larger than the waves
	 * it is wrong, so it is a switch.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buoyancy")
	bool bAlignToSurface = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buoyancy",
		meta=(EditCondition="bAlignToSurface", ClampMin="0.0"))
	float AlignStrength = 1.f;

	/**
	 * What is being floated.
	 *
	 * Unset, the owner's root is used when it simulates physics. A component on an actor whose root
	 * does not simulate says so once and then does nothing, because silently floating nothing is the
	 * failure that reads as the water being broken.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buoyancy")
	TObjectPtr<UPrimitiveComponent> FloatedBody;

	/** How much of the body is in the water, averaged over its pontoons. 0 is clear of it. */
	UFUNCTION(BlueprintPure, Category="Buoyancy")
	float GetSubmersion() const { return Submersion; }

	/** Whether any pontoon found water at all on the last tick. */
	UFUNCTION(BlueprintPure, Category="Buoyancy")
	bool IsInWater() const { return bInWater; }

protected:
	/** The primitive the forces go on, resolved once. */
	UPrimitiveComponent* ResolveBody();

	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> Body;

	float Submersion = 0.f;
	bool bInWater = false;

	/** Said once. A body that cannot be floated cannot be floated on every frame, not only the first. */
	bool bWarnedNoBody = false;
};
