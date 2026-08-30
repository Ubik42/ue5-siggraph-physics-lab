using UnrealBuildTool;

public class PhysicsResearch : ModuleRules
{
	public PhysicsResearch(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"DeveloperSettings",
				"PhysicsCore",
				"Json",
				"InputCore"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Chaos",
				"ChaosCore",
				"ChaosSolverEngine",
				"Projects"
			});
	}
}
