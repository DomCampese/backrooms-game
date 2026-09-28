using UnrealBuildTool;

public class BackroomsGameTarget : TargetRules
{
	public BackroomsGameTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("BackroomsGame");
	}
}
