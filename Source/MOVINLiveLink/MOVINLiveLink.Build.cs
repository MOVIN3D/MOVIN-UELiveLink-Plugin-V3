using UnrealBuildTool;

public class MOVINLiveLink : ModuleRules
{
	public MOVINLiveLink(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDefinitions.Add("MOVIN_STREAM_VALIDATION=" + (Target.bBuildEditor && System.Environment.GetEnvironmentVariable("MOVIN_STREAM_VALIDATION") == "1" ? "1" : "0"));

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicSystemLibraries.Add("bcrypt.lib");

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Networking",
				"Sockets",
				"LiveLinkInterface",
				"LiveLinkAnimationCore",
				"LiveLink",
				"LiveLinkComponents",
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
			}
		);

		PrecompileForTargets = PrecompileTargetsType.Any;
	}
}
