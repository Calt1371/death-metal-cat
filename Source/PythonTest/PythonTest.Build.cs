using UnrealBuildTool;

public class PythonTest : ModuleRules
{
	public PythonTest(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"Paper2D",
			"UMG",
			"Slate",
			"SlateCore",
			// Title screen video playback (UMediaPlayer/UMediaTexture/UMediaSource). MediaAssets is a
			// built-in engine module, not a plugin -- the plugin side of Media Framework is only the
			// per-platform playback backend (WmfMedia on Win64), which the .uproject pins explicitly.
			"MediaAssets"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
