// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialFunction.h"
#include "MobWaterHooks.generated.h"

/**
 * Where in a generated master a project's own material function is spliced in.
 *
 * The masters are generated, so anything hand-wired into one is lost the next time they are
 * authored. A hook is how a project keeps its own maths in a generated material: name a function,
 * name a point, and the generator wires it in every time.
 *
 * A hook function is wired by name. Every input it declares is looked up in the signals live at
 * that point and connected if one matches; an input naming nothing available is left on its own
 * default. Every output it declares replaces the signal of the same name, and a signal the
 * function has no output for passes through untouched. Generating logs the pool at each point.
 */
UENUM(BlueprintType)
enum class EMobWaterHookPoint : uint8
{
	/**
	 * Last, with every pin settled and nothing after it but the material outputs.
	 *
	 * Replaceable: Color, Opacity, Roughness, Normal, Refraction, Emissive. Readable alongside
	 * them: Foam, Column, ShoreFade, Caustics, Glint, Reflection and Fold, which is what the
	 * surface was built from.
	 *
	 * The only point the water master offers, and it does not need another: the terms a mid-chain
	 * hook would want to see are all still in scope here, so a function can restyle the surface
	 * knowing how deep the water is and where the foam went.
	 */
	Output,

	/**
	 * Added to world position offset, so a project's own displacement stacks with the waves rather
	 * than replacing them.
	 *
	 * Declare an output named WorldPositionOffset. Nothing else here is replaceable.
	 */
	WorldPositionOffset,
};

/** One project material function, and where it goes. */
USTRUCT(BlueprintType)
struct MOBWATER_API FMobWaterHook
{
	GENERATED_BODY()

	/**
	 * The function to splice in. Its inputs and outputs are matched to the master's signals by name.
	 *
	 * A material function, not an instance of one: the generator reads the declared pin names off
	 * the asset's own graph.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category="Hook")
	TSoftObjectPtr<UMaterialFunction> Function;

	/** Where in the master it goes. Which signals it can read and replace follows from this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category="Hook")
	EMobWaterHookPoint Point = EMobWaterHookPoint::Output;

	/**
	 * Static bool parameter gating the whole hook. Leave empty and it is always in.
	 *
	 * Worth naming for anything most instances do not want: a static switch takes its dead branch
	 * with it, so an instance with the switch off compiles none of the function.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category="Hook")
	FName SwitchName = NAME_None;

	/** What that switch defaults to on the master. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category="Hook")
	bool bSwitchDefault = false;

	/** Parameter group the switch is filed under in an instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category="Hook")
	FName SwitchGroup = TEXT("Project");

	/**
	 * Which masters it goes into, by asset name. Empty is every one the generator authors.
	 *
	 * The surface and the waterfall are different materials with different pools, so a hook meant
	 * for one usually names it. M_MobWater and M_MobWaterFall are the two.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category="Hook")
	TArray<FName> Masters;
};
