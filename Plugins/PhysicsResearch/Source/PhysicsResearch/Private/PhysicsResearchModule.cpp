#include "PhysicsResearchModule.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPhysicsResearch, Log, All);

void FPhysicsResearchModule::StartupModule()
{
	UE_LOG(LogPhysicsResearch, Display, TEXT("PhysicsResearch runtime module started."));
}

void FPhysicsResearchModule::ShutdownModule()
{
	UE_LOG(LogPhysicsResearch, Display, TEXT("PhysicsResearch runtime module stopped."));
}

IMPLEMENT_MODULE(FPhysicsResearchModule, PhysicsResearch)
