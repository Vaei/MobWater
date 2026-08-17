// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "MobWaterInfo.h"
#include "Components/ActorComponent.h"
#include "MobWaterInteractionComponent.generated.h"

class UMobWaterComponent;
class UMobWaterDisturbanceComponent;

/**
 * Broadcast when the owner enters or leaves water.
 *
 * Speed is how fast it was travelling downwards as it crossed, which is what decides whether this
 * was a step in or a fall in - and therefore how big the splash should be.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMobWaterCrossed, FVector, Location, float, Speed);

/** Broadcast for anything worth spawning a splash at. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMobWaterSplash, FVector, Location, float, Strength);

/**
 * What being in water does to an actor.
 *
 * Immersion, the events that come off crossing the surface, the ripples left behind, and a seam for
 * movement. It spawns no effects and plays no sounds itself: a plugin cannot know what a project's
 * splash looks like, so it says where and how hard and the project decides what that means.
 *
 * The movement half is deliberately thin and virtual. MobWater has no idea what movement component a
 * project uses - stock, a fork, something written from scratch - and guessing wrong would be worse
 * than doing nothing, so the default handles the stock CharacterMovementComponent and a subclass
 * handles anything else.
 */
UCLASS(Blueprintable, ClassGroup=(Mob), meta=(BlueprintSpawnableComponent, DisplayName="Mob Water Interaction"))
class MOBWATER_API UMobWaterInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMobWaterInteractionComponent();

	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

	/** Where on the owner the surface is measured against. Usually the feet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water", meta=(ForceUnits="cm"))
	FVector MeasureOffset = FVector(0.f, 0.f, -88.f);

	/** How deep the owner has to be before it is wading rather than walking through a puddle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(ClampMin="0.0", ForceUnits="cm"))
	float WadeDepth = 30.f;

	/** How deep before it is swimming. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(ClampMin="0.0", ForceUnits="cm"))
	float SwimDepth = 130.f;

	/**
	 * How much of its speed is left at the deepest wade.
	 *
	 * Water is heavy and this is most of what selling that costs. Below about a third it stops
	 * reading as effort and starts reading as the controls having broken.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(ClampMin="0.05", ClampMax="1.0"))
	float WadeSpeedScale = 0.45f;

	/** Whether this component drives movement at all, or only reports. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement")
	bool bDriveMovement = true;

	/** Whether the owner leaves ripples behind it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripples")
	bool bMakeRipples = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripples", meta=(EditCondition="bMakeRipples", ClampMin="1.0", ForceUnits="cm"))
	float RippleRadius = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripples", meta=(EditCondition="bMakeRipples"))
	float RippleStrength = 0.4f;

	/** How much wider a splash is than the trail the owner leaves walking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripples", meta=(ClampMin="0.1"))
	float SplashRadiusScale = 0.6f;

	/**
	 * How fast the owner has to cross the surface for the smallest and the largest splash, in cm/s.
	 *
	 * The whole difference between stepping into a pond and falling into one. Both ends matter: a
	 * range that starts above walking speed means walking in does nothing at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripples", meta=(ForceUnits="cm/s"))
	FVector2D SplashSpeedRange = FVector2D(0.0, 800.0);

	/** What those two speeds are worth. Reaches OnSplash as it is, and the field as a push. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripples")
	FVector2D SplashStrengthRange = FVector2D(0.35, 2.0);

	/**
	 * How far the owner walks through water between splashes, in world units. 0 never splashes.
	 *
	 * By stride rather than by time, so a splash lands where a foot did at any speed. A component
	 * cannot know when a foot actually touches down - that is what a footstep notify calling Splash
	 * is for, and this is what happens for anything that has no such notify.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripples", meta=(ClampMin="0.0", ForceUnits="cm"))
	float WadeSplashDistance = 80.f;

	UPROPERTY(BlueprintAssignable, Category="Water")
	FMobWaterCrossed OnEnteredWater;

	UPROPERTY(BlueprintAssignable, Category="Water")
	FMobWaterCrossed OnExitedWater;

	/** Somewhere worth a splash. Bind a Niagara spawn to this. */
	UPROPERTY(BlueprintAssignable, Category="Water")
	FMobWaterSplash OnSplash;

	/** Whether the owner is in water at all. */
	UFUNCTION(BlueprintPure, Category="Water")
	bool IsInWater() const { return bInWater; }

	/** Whether it is deep enough to swim. */
	UFUNCTION(BlueprintPure, Category="Water")
	bool IsSwimming() const { return bSwimming; }

	/** How far the measure point is below the surface. Negative above it. */
	UFUNCTION(BlueprintPure, Category="Water")
	float GetImmersionDepth() const { return LastInfo.ImmersionDepth; }

	/** The water as of the last tick. */
	UFUNCTION(BlueprintPure, Category="Water")
	const FMobWaterInfo& GetWaterInfo() const { return LastInfo; }

	/**
	 * Splashes here and pushes the surface.
	 *
	 * Called by the component when the owner crosses the surface, and meant to be called by whatever
	 * knows about footfalls - a footstep notify knows when a foot lands, and this component would
	 * have to guess.
	 */
	UFUNCTION(BlueprintCallable, Category="Water")
	void Splash(const FVector& Location, float Strength);

protected:
	/**
	 * Slows the owner down by how deep it is, 0 dry and 1 at the swim line.
	 *
	 * The default scales a stock CharacterMovementComponent's walk speed. Override for anything else,
	 * and call nothing in the parent.
	 */
	UFUNCTION(BlueprintNativeEvent, Category="Movement")
	void ApplyWade(float Immersion01);
	virtual void ApplyWade_Implementation(float Immersion01);

	/** Puts the owner into or out of a swimming state. Default sets the stock movement mode. */
	UFUNCTION(BlueprintNativeEvent, Category="Movement")
	void ApplySwimming(bool bShouldSwim);
	virtual void ApplySwimming_Implementation(bool bShouldSwim);

	/** Where the surface is measured. */
	FVector GetMeasureLocation() const;

	/** Throws a splash every stride the owner wades, so walking through water is not silent. */
	void TickWadeSplash(const FVector& Measure, const FMobWaterInfo& Info, bool bFound);

	UPROPERTY(Transient)
	FMobWaterInfo LastInfo;

	UPROPERTY(Transient)
	TObjectPtr<UMobWaterDisturbanceComponent> Disturbance;

	bool bInWater = false;
	bool bSwimming = false;

	/**
	 * The walk speed before any of this touched it.
	 *
	 * Captured once on the first wade rather than every tick, because reading it back after the scale
	 * has been applied and calling that the original is how a character ends up permanently slower
	 * every time it steps into a puddle.
	 */
	float OriginalMaxWalkSpeed = 0.f;
	bool bCapturedWalkSpeed = false;

	/** Where the last wading splash was thrown, so the next one is a stride away and not a frame. */
	FVector WadeMark = FVector::ZeroVector;
	bool bHasWadeMark = false;
};
