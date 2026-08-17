// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "MobWaterInfo.generated.h"

/**
 * The water at a point, as the CPU can know it.
 *
 * What is deliberately absent: ripples, wakes and stamped foam. Those live in a render target, a
 * dedicated server has no GPU to hold one, and a value only the client can see is the wrong thing to
 * build physics on. Everything here is a pure function of position and time, so two machines given
 * the same instant fill this struct identically.
 */
USTRUCT(BlueprintType)
struct MOBWATER_API FMobWaterInfo
{
	GENERATED_BODY()

	/** False when the point is outside every body of water, in which case nothing else is meaningful. */
	UPROPERTY(BlueprintReadOnly, Category="Water")
	bool bValid = false;

	/** World Z of the surface directly above the point asked about. */
	UPROPERTY(BlueprintReadOnly, Category="Water")
	float SurfaceZ = 0.f;

	/** The surface normal there. Straight up on a still body. */
	UPROPERTY(BlueprintReadOnly, Category="Water")
	FVector Normal = FVector::UpVector;

	/**
	 * How far the point is below the surface, in world units. Negative above it.
	 *
	 * This is what wading and swimming read, and the sign is the whole state machine: below zero is
	 * dry, past a threshold is swimming, and between the two is how much drag to apply.
	 */
	UPROPERTY(BlueprintReadOnly, Category="Water")
	float ImmersionDepth = 0.f;

	/** How deep the water itself is here - surface to bed, not surface to the point. */
	UPROPERTY(BlueprintReadOnly, Category="Water")
	float Depth = 0.f;

	/** How fast the surface is moving across the ground, in world units per second. */
	UPROPERTY(BlueprintReadOnly, Category="Water")
	FVector2D FlowVelocity = FVector2D::ZeroVector;

	/**
	 * How hard the surface is folding here, 0 flat and 1 about to break.
	 *
	 * The same number crest foam is drawn from, so a splash spawned where this is high lands on a
	 * crest that actually looks like it is breaking.
	 */
	UPROPERTY(BlueprintReadOnly, Category="Water")
	float Fold = 0.f;

	/**
	 * How much water an exclusion volume keeps out of here, 0 none and 1 all of it.
	 *
	 * Already applied to ImmersionDepth, so nothing has to multiply by it. It is reported because a
	 * hull that wants to know it is standing in its own well cannot tell that from a body that has
	 * gone shallow.
	 */
	UPROPERTY(BlueprintReadOnly, Category="Water")
	float Exclusion = 0.f;
};
