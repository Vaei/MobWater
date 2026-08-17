// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MobWaterExclusionActor.generated.h"

class UMobWaterExclusionComponent;
class UBillboardComponent;

/**
 * An area water is kept out of.
 *
 * Attach one to a boat and the hull is dry inside while it moves, because the volume is evaluated
 * where it stands rather than stamped where it was.
 */
UCLASS(ClassGroup=Rendering, hidecategories=(Input, Collision, Replication, Physics, HLOD, Cooking, DataLayers))
class MOBWATER_API AMobWaterExclusion : public AActor
{
	GENERATED_BODY()

public:
	AMobWaterExclusion();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Exclusion")
	TObjectPtr<UMobWaterExclusionComponent> Exclusion;

	UMobWaterExclusionComponent* GetExclusionComponent() const { return Exclusion; }

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> Sprite;
#endif
};
