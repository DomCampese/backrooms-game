using UnrealBuildTool;

public class BackroomsGameEditorTarget : TargetRules
{
	public BackroomsGameEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("BackroomsGame");
	}
}
