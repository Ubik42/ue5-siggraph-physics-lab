#include "PhysicsDrivenCharacterAddComponentsAction.h"

#include "Character/LyraCharacter.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "RecoverableFallComponent.h"

void UPhysicsDrivenCharacterAddComponentsAction::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
    FContextHandles& Handles = ContextHandles.FindOrAdd(Context);
    Handles.GameInstanceStartHandle = FWorldDelegates::OnStartGameInstance.AddUObject(this, &ThisClass::HandleGameInstanceStart, FGameFeatureStateChangeContext(Context));
    ensure(Handles.ComponentRequestHandles.Num() == 0);
    for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
    {
        if (Context.ShouldApplyToWorldContext(WorldContext)) AddToWorld(WorldContext, Handles);
    }
}

void UPhysicsDrivenCharacterAddComponentsAction::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
    FContextHandles& Handles = ContextHandles.FindOrAdd(Context);
    FWorldDelegates::OnStartGameInstance.Remove(Handles.GameInstanceStartHandle);
    Handles.ComponentRequestHandles.Empty();
}

void UPhysicsDrivenCharacterAddComponentsAction::AddToWorld(const FWorldContext& WorldContext, FContextHandles& Handles)
{
    UWorld* World = WorldContext.World();
    UGameInstance* GameInstance = WorldContext.OwningGameInstance;
    if (!GameInstance || !World || !World->IsGameWorld()) return;
    if (UGameFrameworkComponentManager* Manager = UGameInstance::GetSubsystem<UGameFrameworkComponentManager>(GameInstance))
    {
        Handles.ComponentRequestHandles.Add(Manager->AddComponentRequest(ALyraCharacter::StaticClass(), URecoverableFallComponent::StaticClass(), EGameFrameworkAddComponentFlags::None));
    }
}

void UPhysicsDrivenCharacterAddComponentsAction::HandleGameInstanceStart(UGameInstance* GameInstance, FGameFeatureStateChangeContext ChangeContext)
{
    if (FWorldContext* WorldContext = GameInstance->GetWorldContext())
    {
        if (ChangeContext.ShouldApplyToWorldContext(*WorldContext))
        {
            if (FContextHandles* Handles = ContextHandles.Find(ChangeContext)) AddToWorld(*WorldContext, *Handles);
        }
    }
}
