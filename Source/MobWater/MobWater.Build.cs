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
				// A spline body's surface is generated from the shape that was drawn, at runtime as
				// well as in the editor, because the mesh is transient and remade on every load.
				"MeshDescription",
				"StaticMeshDescription",
			}
			);
	}
}
