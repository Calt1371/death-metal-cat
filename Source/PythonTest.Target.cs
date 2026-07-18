using UnrealBuildTool;
using System.Collections.Generic;

public class PythonTestTarget : TargetRules
{
	public PythonTestTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.AddRange(new string[] { "PythonTest" });
	}
}
