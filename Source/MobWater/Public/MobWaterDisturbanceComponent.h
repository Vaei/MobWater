// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "MobWaterDisturbanceComponent.generated.h"

/**
 * Something that makes ripples.
 *
 * Stamped into the one top-down field every body of water reads, so a hundred of them cost what one
 * does. What it writes is a push on the surface, not a shape: the field carries it outwards and
 * reflects it off whatever is standing in the way, because it is a wave equation rather than a
 * decal that fades.
 */
UCLASS(ClassGroup=Rendering, meta=(BlueprintSpawnableComponent, DisplayName="Mob Water Disturbance"))
class MOBWATER_API UMobWaterDisturbanceComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMobWaterDisturbanceComponent();

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	//~ End UActorComponent Interface

	/** How far across the surface it pushes, in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Disturbance", meta=(ClampMin="1.0", ForceUnits="cm"))
	float Radius = 60.f;

	/**
	 * How far it displaces the surface. Negative pulls it down, which is what an object entering does.
	 *
	 * The surface is displaced and let go, so this is how tall the push starts and not how hard it is
	 * thrown. Two is as far as the field can hold, and a ripple that reaches it is clipped flat across
	 * its crest.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Disturbance", meta=(UIMin="-2.0", UIMax="2.0"))
	float Strength = 1.f;

	/**
	 * How white its wake gets. 0 pushes the surface and marks nothing.
	 *
	 * This is a trail, not a wave: it stays where the thing was and dries in place, so it reads as
	 * churn behind something rather than as anything the ripple carries with it.
	 *
	 * A ceiling rather than an amount. Consecutive stamps overlap by most of their radius, so foam
	 * that accumulated would reach white within a few strides whatever this was set to, and solid
	 * white has no shape in it - it is a hard edged slab dragged along behind whatever made it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Disturbance", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Foam = 0.35f;

	/**
	 * Whether it disturbs the surface every frame, or only when it moves.
	 *
	 * Off is what a character wants: standing still in water leaves it to settle, which is what still
	 * water does. On is a fountain or an outflow, something that never stops.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Disturbance")
	bool bPersistent = false;

	/**
	 * How far it has to move before it stamps again, in world units. Zero uses half the radius.
	 *
	 * Stamps add where they overlap, so anything shorter than the radius pushes water it has already
	 * pushed, before that push has had a frame to move. A walking character covers a few centimetres
	 * a frame, and at that rate what should be a trail of ripples is a mound that grows for as long
	 * as they keep walking.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Disturbance", meta=(ClampMin="0.0", ForceUnits="cm"))
	float MoveThreshold = 0.f;

	/** Whether this is disturbing anything right now, where it last did, and how hard. */
	bool ShouldStamp(float DeltaTime, FVector& OutLocation, float& OutStrength);

protected:
	FVector LastStampLocation = FVector::ZeroVector;
	bool bHasStamped = false;
};
