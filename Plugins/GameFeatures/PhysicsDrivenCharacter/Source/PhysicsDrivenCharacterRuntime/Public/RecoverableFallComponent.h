#pragma once

#include "Components/ActorComponent.h"
#include "RecoverableFallState.h"
#include "RecoverableFallComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRecoverableFallStateChanged, ERecoverableFallState, OldState, ERecoverableFallState, NewState);

UCLASS(BlueprintType, Blueprintable, ClassGroup=(PhysicsResearch), meta=(BlueprintSpawnableComponent))
class PHYSICSDRIVENCHARACTERRUNTIME_API URecoverableFallComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    URecoverableFallComponent();
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION(BlueprintPure, Category="Physics Research|Recoverable Fall")
    ERecoverableFallState GetFallState() const { return FallState; }

    UFUNCTION(BlueprintPure, Category="Physics Research|Recoverable Fall")
    EPhysicalGetUpBranch GetRecoveryBranch() const { return RecoveryBranch; }

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Physics Research|Recoverable Fall")
    bool BeginRecoverableFall();

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Physics Research|Recoverable Fall")
    void UpdateServerObservation(bool bGrounded, double FacingUpDot, double LinearSpeedCmPerSecond, double AngularSpeedRadPerSecond, double StableSeconds);

    UFUNCTION(Server, Reliable, BlueprintCallable, Category="Physics Research|Recoverable Fall")
    void ServerRequestRecovery();

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Physics Research|Recoverable Fall")
    bool FinishRecovery();

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Physics Research|Recoverable Fall")
    void FailRecovery();

    UPROPERTY(BlueprintAssignable)
    FRecoverableFallStateChanged OnFallStateChanged;

private:
    UFUNCTION()
    void OnRep_FallState(ERecoverableFallState OldState);

    void SetFallState(ERecoverableFallState NewState);
    void ApplyStateToLyra(ERecoverableFallState OldState, ERecoverableFallState NewState);
    bool IsDeathActive() const;
    void ScheduleNetworkProbe();

    UPROPERTY(ReplicatedUsing=OnRep_FallState)
    ERecoverableFallState FallState = ERecoverableFallState::Balanced;

    UPROPERTY(Replicated)
    EPhysicalGetUpBranch RecoveryBranch = EPhysicalGetUpBranch::Pending;

    bool bRecoveryRequested = false;
};
