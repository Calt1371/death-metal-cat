using UnrealBuildTool;
using System.Collections.Generic;

public class PythonTestEditorTarget : TargetRules
{
	public PythonTestEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.AddRange(new string[] { "PythonTest" });
	}
}
