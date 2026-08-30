#include "PhysicsResearchShadersModule.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

void FPhysicsResearchShadersModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PhysicsResearch"));
	check(Plugin.IsValid());
	AddShaderSourceDirectoryMapping(
		TEXT("/Plugin/PhysicsResearch"),
		FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
}

IMPLEMENT_MODULE(FPhysicsResearchShadersModule, PhysicsResearchShaders)
