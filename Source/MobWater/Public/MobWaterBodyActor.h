// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MobWaterBodyActor.generated.h"

class UBillboardComponent;
class UMobWaterComponent;
class UMobWaterSplineComponent;
class UStaticMesh;

/**
 * A body of water with a shape you draw.
 *
 * Closed, the spline is a shoreline and this is a lake. Open, it is the middle of a river and the
 * width says where the banks are. The two are the same actor because they are the same question -
 * how far is a point from the edge of the water - and everything downstream only asks that.
 */
UCLASS(ClassGroup=Rendering, hidecategories=(Input, Collision, Replication, Physics, HLOD, Cooking, DataLayers))
class MOBWATER_API AMobWaterBody : public AActor
{
	GENERATED_BODY()

public:
	AMobWaterBody();

	//~ Begin AActor Interface
	virtual void PostRegisterAllComponents() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;
#endif
	//~ End AActor Interface

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Water")
	TObjectPtr<UMobWaterSplineComponent> Spline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Water")
	TObjectPtr<UMobWaterComponent> Water;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> Sprite;
#endif

	UMobWaterComponent* GetWaterComponent() const { return Water; }
	UMobWaterSplineComponent* GetSplineComponent() const { return Spline; }

	/** Rebuilds the surface from the spline. Cheap enough to call whenever the spline is touched. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Water")
	void RebuildSurface();

protected:
	/**
	 * The generated surface.
	 *
	 * Owned by this actor rather than saved as an asset, because it is this body's shape and nothing
	 * else's - a mesh asset per lake would fill a project with single-use assets nobody can name.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> GeneratedMesh;
};
