// Copyright (c) Jared Taylor

using UnrealBuildTool;

public class MobWater : ModuleRules
{
	public MobWater(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"DeveloperSettings",
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Projects",
				"RenderCore",
				// The Rewind Debugger track. Recording is behind a trace channel that is off by
				// default; the dependency is on the tracing macros, not on anything editor side.
				"TraceLog",
				// A spline body's surface is generated from the shape that was drawn, at runtime as
				// well as in the editor, because the mesh is transient and remade on every load.
				"MeshDescription",
				"StaticMeshDescription",
			}
			);
	}
}
