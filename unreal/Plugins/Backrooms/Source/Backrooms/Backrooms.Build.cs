using System.IO;
using UnrealBuildTool;

// One module holds core, the sim and the Unreal classes that use them. Core has
// no export annotations, and editor builds link each module as a shared library
// with hidden symbols, so a second module could not call into core.
//
// core, sim and port are compiled from the repository's src/, the same files the
// raylib build compiles: each Private/Shared/*.cpp includes one of them. Those
// translation units see no Unreal header, which is why the module has no PCH.
public class Backrooms : ModuleRules
{
	public Backrooms(ReadOnlyTargetRules Target) : base(Target)
	{
		string Repo = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "..", ".."));
		PublicIncludePaths.Add(Path.Combine(Repo, "src"));
		PrivateIncludePaths.Add(Path.Combine(Repo, "tools"));

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "DeveloperSettings", "InputCore", "EnhancedInput", "ProceduralMeshComponent" });
		PrivateDependencyModuleNames.Add("AssetRegistry");

		// A shared PCH is force-included into every file, core's included, and
		// core must see nothing but itself and the standard library.
		PCHUsage = PCHUsageMode.NoPCHs;
		// Core has file-static helpers that could collide in a unity file.
		bUseUnity = false;
		bEnableExceptions = false;
		bUseRTTI = false;
		// No fused multiply-adds and no fast math: a contracted multiply-add can
		// flip a noise threshold and generate a different maze
		// (docs/migration.md, "Floating point"). Core and sim also say so in
		// source (src/core/fp_strict.h).
		FPSemantics = FPSemanticsMode.Precise;
	}
}
