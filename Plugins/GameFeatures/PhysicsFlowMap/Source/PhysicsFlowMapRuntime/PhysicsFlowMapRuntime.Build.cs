using UnrealBuildTool;

public class PhysicsFlowMapRuntime : ModuleRules
{
	public PhysicsFlowMapRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "PhysicsResearch", "PhysicsResearchShaders" });
		PrivateDependencyModuleNames.AddRange(new string[] { "RHI", "Json" });
	}
}
