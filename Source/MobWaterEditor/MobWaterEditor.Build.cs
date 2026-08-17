// Copyright (c) Jared Taylor

using UnrealBuildTool;

public class MobWaterEditor : ModuleRules
{
	public MobWaterEditor(ReadOnlyTargetRules Target) : base(Target)
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
				"Slate",
				"SlateCore",
				"InputCore",
				"UnrealEd",
				"ComponentVisualizers",
				"PlacementMode",
				"LevelEditor",
				"AppFramework",
				"ContentBrowser",
				"PropertyEditor",
				"MaterialEditor",
				"AssetTools",
				"AssetRegistry",
				"ToolMenus",
				"Projects",
				"Settings",
				"SourceControl",
				"MeshDescription",
				"StaticMeshDescription",
				"PythonScriptPlugin",
				"RewindDebuggerInterface",
				"MobWater",
			}
			);
	}
}
