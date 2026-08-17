// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

/**
 * Puts a plugin's own categories above everything its base class brings with it.
 *
 * A water component inherits Rendering, LOD, Materials, Tags, Activation and a dozen more from
 * UStaticMeshComponent, all of which sort alphabetically above the categories anyone opened the
 * panel for. Without this, Water and Waves sit below Physics on a component that has no physics.
 */
class FMobWaterDetails : public IDetailCustomization
{
public:
	/** Categories in the order they are worked in, which is the order they are shown in. */
	static TSharedRef<IDetailCustomization> Make(TArray<FName> Categories);

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TArray<FName> Ordered;
};
