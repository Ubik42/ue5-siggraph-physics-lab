using UnrealBuildTool;

public class PhysicsResearchShaders : ModuleRules
{
	public PhysicsResearchShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"RenderCore",
				"RHI",
				"Projects"
			});
	}
}
