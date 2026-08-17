// Copyright (c) Jared Taylor

#include "MobWaterModule.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(LogMobWater);

#if WITH_EDITOR
FMobWaterMissingMaterial OnMobWaterMaterialMissing;
#endif

void FMobWaterModule::StartupModule()
{
	// The water master reaches its maths through Custom nodes that include /MobWater/Public/*.ush,
	// so the virtual directory has to exist before anything compiles a material. Hence PostConfigInit
	// rather than Default: an include that resolves to nothing is stripped from the cached data, which
	// surfaces as an undeclared identifier rather than a missing file.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MobWater"));
	if (Plugin.IsValid())
	{
		const FString PluginRoot = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
		AddShaderSourceDirectoryMapping(TEXT("/MobWater"), FPaths::Combine(PluginRoot, TEXT("Shaders")));
	}
}

IMPLEMENT_MODULE(FMobWaterModule, MobWater)
