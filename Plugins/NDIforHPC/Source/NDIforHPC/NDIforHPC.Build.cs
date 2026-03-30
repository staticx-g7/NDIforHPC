using UnrealBuildTool;

public class NDIforHPC : ModuleRules
{
	public NDIforHPC(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Media",
			"MediaUtils",
			"MediaAssets",
			"AudioMixer",
			"MediaIOCore",
			"CinematicCamera",
			"NDIMedia"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"RenderCore",
			"RHI",
			"Niagara",
			"MoviePlayer",
			"LevelSequence",
			"DeveloperSettings"
		});

		if (Target.bBuildEditor == true)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
					"AssetRegistry"
				}
			);
		}
	}
}
