using UnrealBuildTool;

public class PhysicsContactClothRuntime : ModuleRules
{
	public PhysicsContactClothRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"PhysicsResearch",
				"PhysicsResearchShaders",
				"ClothingSystemRuntimeInterface",
				"ClothingSystemRuntimeCommon"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"InputCore",
				"RHI"
			});
	}
}
