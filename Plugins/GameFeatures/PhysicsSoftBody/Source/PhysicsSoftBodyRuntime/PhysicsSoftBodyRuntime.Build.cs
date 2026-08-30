using UnrealBuildTool;

public class PhysicsSoftBodyRuntime : ModuleRules
{
	public PhysicsSoftBodyRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"GeometryCore",
				"GeometryFramework",
				"InputCore",
				"PhysicsResearch",
				"PhysicsResearchShaders"
			});

		PrivateDependencyModuleNames.AddRange(new string[] { "RHI", "RenderCore" });
	}
}
