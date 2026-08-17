// Copyright (c) Jared Taylor

using UnrealBuildTool;

public class MobWaterRewind : ModuleRules
{
	public MobWaterRewind(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				// The Rewind Debugger itself, and the curve widget its own tracks are drawn with.
				// Both come from GameplayInsights, which the .uplugin names as a dependency.
				"GameplayInsights",
				"RewindDebuggerInterface",
				"TraceAnalysis",
				"TraceServices",
				"MobWater",
			}
			);
	}
}
