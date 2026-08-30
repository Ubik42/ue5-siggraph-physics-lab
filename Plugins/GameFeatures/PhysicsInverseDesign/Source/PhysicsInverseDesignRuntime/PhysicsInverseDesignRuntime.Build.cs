using UnrealBuildTool;

public class PhysicsInverseDesignRuntime : ModuleRules
{
    public PhysicsInverseDesignRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "PhysicsCore", "PhysicsResearch" });
        PrivateDependencyModuleNames.AddRange(new[] { "Json" });
    }
}
