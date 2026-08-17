// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "MobWaterTypes.h"
#include "ActorFactories/ActorFactory.h"
#include "MobWaterActorFactory.generated.h"

/**
 * Drops a body of water, already the right shape.
 *
 * One factory per shape rather than one that asks afterwards, because the shape decides the mesh and
 * the material as well as the maths - it is not something that can be chosen after the drop without
 * rebuilding what was dropped.
 */
UCLASS(Abstract)
class MOBWATEREDITOR_API UMobWaterPoolFactory : public UActorFactory
{
	GENERATED_BODY()

public:
	UMobWaterPoolFactory();

	virtual void PostSpawnActor(UObject* Asset, AActor* NewActor) override;

protected:
	EMobWaterShape Shape = EMobWaterShape::Box;
};

UCLASS()
class MOBWATEREDITOR_API UMobWaterPoolFactory_Box : public UMobWaterPoolFactory
{
	GENERATED_BODY()

public:
	UMobWaterPoolFactory_Box();
};

UCLASS()
class MOBWATEREDITOR_API UMobWaterPoolFactory_Disc : public UMobWaterPoolFactory
{
	GENERATED_BODY()

public:
	UMobWaterPoolFactory_Disc();
};

/**
 * Drops a spline body: a lake when the loop is closed, a river when it is open.
 *
 * One factory each, because the two are drawn differently from the first click and a body that has
 * to be converted afterwards is one nobody converts.
 */
UCLASS(Abstract)
class MOBWATEREDITOR_API UMobWaterBodyFactory : public UActorFactory
{
	GENERATED_BODY()

public:
	UMobWaterBodyFactory();

	virtual void PostSpawnActor(UObject* Asset, AActor* NewActor) override;

protected:
	bool bClosed = true;
};

UCLASS()
class MOBWATEREDITOR_API UMobWaterBodyFactory_Lake : public UMobWaterBodyFactory
{
	GENERATED_BODY()

public:
	UMobWaterBodyFactory_Lake();
};

UCLASS()
class MOBWATEREDITOR_API UMobWaterBodyFactory_River : public UMobWaterBodyFactory
{
	GENERATED_BODY()

public:
	UMobWaterBodyFactory_River();
};

/** Drops an ocean: water with no edge, kept centred on the view. */
UCLASS()
class MOBWATEREDITOR_API UMobWaterOceanFactory : public UActorFactory
{
	GENERATED_BODY()

public:
	UMobWaterOceanFactory();

	virtual void PostSpawnActor(UObject* Asset, AActor* NewActor) override;
};

/** Drops an area water is kept out of. */
UCLASS()
class MOBWATEREDITOR_API UMobWaterExclusionFactory : public UActorFactory
{
	GENERATED_BODY()

public:
	UMobWaterExclusionFactory();
};
