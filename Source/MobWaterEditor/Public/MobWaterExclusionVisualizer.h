// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "ComponentVisualizer.h"

/**
 * Draws the footprint an exclusion volume clears.
 *
 * A volume that removes water is invisible by construction - what it does is make something not be
 * drawn - so without this it can only be placed by moving it and watching the water elsewhere.
 */
class FMobWaterExclusionVisualizer : public FComponentVisualizer
{
public:
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View,
		FPrimitiveDrawInterface* PDI) override;
};
