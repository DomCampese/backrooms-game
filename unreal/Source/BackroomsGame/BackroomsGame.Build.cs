using UnrealBuildTool;

// The project's own module. Everything the game is lives in the Backrooms
// plugin; this module exists because a code project needs one.
public class BackroomsGame : ModuleRules
{
	public BackroomsGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Backrooms" });
	}
}
