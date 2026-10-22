using UnrealBuildTool;

// Maps the plugin's Shaders folder to /BackroomsShaders, where the world and
// post materials' Custom nodes include the Web build's shaders from. Its own
// module because a shader folder must be mapped before shaders compile, in the
// PostConfigInit loading phase, which is too early for the Backrooms module's
// UObjects.
public class BackroomsShaders : ModuleRules
{
	public BackroomsShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "Projects", "RenderCore" });
	}
}
