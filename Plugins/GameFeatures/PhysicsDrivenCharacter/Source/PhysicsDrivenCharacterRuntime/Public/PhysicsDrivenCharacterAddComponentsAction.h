#pragma once

#include "GameFeatureAction.h"
#include "GameFeaturesSubsystem.h"
#include "PhysicsDrivenCharacterAddComponentsAction.generated.h"

class UGameInstance;
struct FComponentRequestHandle;
struct FWorldContext;

UCLASS(BlueprintType, meta=(DisplayName="Add Lyra Recoverable Fall Component"))
class PHYSICSDRIVENCHARACTERRUNTIME_API UPhysicsDrivenCharacterAddComponentsAction final : public UGameFeatureAction
{
    GENERATED_BODY()

public:
    virtual void OnGameFeatureActivating(FGameFeatureActivatingContext& Context) override;
    virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;

private:
    struct FContextHandles
    {
        FDelegateHandle GameInstanceStartHandle;
        TArray<TSharedPtr<FComponentRequestHandle>> ComponentRequestHandles;
    };

    void AddToWorld(const FWorldContext& WorldContext, FContextHandles& Handles);
    void HandleGameInstanceStart(UGameInstance* GameInstance, FGameFeatureStateChangeContext ChangeContext);
    TMap<FGameFeatureStateChangeContext, FContextHandles> ContextHandles;
};
