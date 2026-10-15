// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

MOBWATER_API DECLARE_LOG_CATEGORY_EXTERN(LogMobWater, Log, All);

#if WITH_EDITOR
enum class EMobWaterShape : uint8;

/** Raised when a shape has no material to render with. MobWaterEditor turns it into a notification. */
DECLARE_MULTICAST_DELEGATE_OneParam(FMobWaterMissingMaterial, EMobWaterShape /* Shape */);
extern MOBWATER_API FMobWaterMissingMaterial OnMobWaterMaterialMissing;
#endif

class FMobWaterModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
};
