using UnrealBuildTool;

public class PhysicsDrivenCharacterRuntime : ModuleRules
{
    public PhysicsDrivenCharacterRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "GameplayAbilities",
            "GameFeatures",
            "GameplayTags",
            "InputCore",
            "ModularGameplay",
            "PhysicsControl",
            "PhysicsCore",
            "PhysicsResearch"
        });
        PrivateDependencyModuleNames.AddRange(new[] { "Json", "LyraGame", "MovieSceneCapture", "ImageWrapper", "Slate", "SlateCore", "RenderCore", "RHI" });
    }
}
