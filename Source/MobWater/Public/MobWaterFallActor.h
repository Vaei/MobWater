// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MobWaterFallActor.generated.h"

class UBillboardComponent;
class UMobWaterDisturbanceComponent;
class UMobWaterFallComponent;
class UMobWaterFallSplineComponent;
class UStaticMesh;

/**
 * A waterfall: a sheet of water hung down a drop, and the churn where it lands.
 *
 * The spline is the lip, and every point on it carries how far the water falls from there, so an
 * uneven ledge over a sloped plunge pool is one actor rather than several.
 *
 * It is not a body of water. Nothing floats on a waterfall, and a query that found one would answer
 * with a surface standing on end - so this registers nothing, and a character walking into the
 * plunge is in whatever pool is underneath it.
 */
UCLASS(ClassGroup=Rendering, hidecategories=(Input, Collision, Replication, Physics, HLOD, Cooking, DataLayers))
class MOBWATER_API AMobWaterFall : public AActor
{
	GENERATED_BODY()

public:
	AMobWaterFall();

	//~ Begin AActor Interface
	virtual void PostRegisterAllComponents() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;
#endif
	//~ End AActor Interface

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fall")
	TObjectPtr<UMobWaterFallSplineComponent> Lip;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fall")
	TObjectPtr<UMobWaterFallComponent> Fall;

	/**
	 * What the fall does to the water it lands in.
	 *
	 * A persistent disturbance placed at the base, which is the thing that sells a plunge: the pool
	 * below never settles, and the churn spreads out of it and reflects off whatever it meets,
	 * because the ripple field is a wave equation rather than a decal.
	 *
	 * One rather than a row of them. Stamps add where they overlap, so a line of disturbers along the
	 * base builds a mound instead of a churn.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plunge")
	TObjectPtr<UMobWaterDisturbanceComponent> Plunge;

	/**
	 * Whether the fall pushes the water it lands in at all.
	 *
	 * Off for a fall landing on rock, or on nothing, where a churn in a pool that is not there is a
	 * ripple in mid air.
	 *
	 * ScriptName because Python drops a bool's leading b, and this would otherwise be a second
	 * plunge on the class beside the component - which it warns about on every editor boot.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Plunge", meta=(ScriptName="EnablePlunge"))
	bool bPlunge = true;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> Sprite;
#endif

	UMobWaterFallComponent* GetFallComponent() const { return Fall; }
	UMobWaterFallSplineComponent* GetLipComponent() const { return Lip; }

	/** Rebuilds the sheet from the lip, and moves the plunge to where the water now lands. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Fall")
	void RebuildSurface();

protected:
	/**
	 * The generated sheet.
	 *
	 * Owned by this actor rather than saved as an asset, because it is this fall's shape and nothing
	 * else's - a mesh asset per waterfall would fill a project with single-use assets nobody can name.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> GeneratedMesh;
};
