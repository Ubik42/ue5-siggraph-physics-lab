#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PhysicalGetUpDecision.h"
#include "PhysicalGetUpRealtimeActor.generated.h"

class UAnimSequence;
class UCameraComponent;
class UPhysicsControlComponent;
class UPointLightComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

enum class EPhysicalGetUpLanePhase : uint8
{
    Ragdoll,
    Stabilizing,
    Recovering,
    Recovered,
    Rejected,
    Failed
};

struct FPhysicalGetUpLaneRuntime
{
    USkeletalMeshComponent* Mesh = nullptr;
    UPhysicsControlComponent* Control = nullptr;
    FString Label;
    EPhysicalGetUpLanePhase Phase = EPhysicalGetUpLanePhase::Ragdoll;
    EPhysicalGetUpBranch Branch = EPhysicalGetUpBranch::Pending;
    FQuat UprightPelvisWorldRotation = FQuat::Identity;
    FTransform UprightComponentTransform = FTransform::Identity;
    double FacingUpDot = 0.0;
    double LinearSpeed = 0.0;
    double AngularSpeed = 0.0;
    double StableSeconds = 0.0;
    double RecoveryStartSeconds = -1.0;
    double RecoveryCompleteSeconds = -1.0;
    int32 ControlCount = 0;
    bool bGrounded = false;
};

UCLASS()
class PHYSICSDRIVENCHARACTERRUNTIME_API APhysicalGetUpRealtimeActor : public AActor
{
    GENERATED_BODY()

public:
    APhysicalGetUpRealtimeActor();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    FString GetStatusLine() const;
    FString GetLaneLine(int32 LaneIndex) const;
    bool IsRunComplete() const { return bRunComplete; }
    bool IsInteractiveMode() const { return bInteractiveMode; }
    FString GetInteractionLine() const;

private:
    void ConfigureMesh(USkeletalMeshComponent* Mesh, const FVector& Location);
    void ApplyFallImpulses();
    void UpdateLane(FPhysicalGetUpLaneRuntime& Lane, float DeltaSeconds, bool bCanRecover);
    FPhysicalGetUpDecisionInput MeasureDecisionInput(FPhysicalGetUpLaneRuntime& Lane) const;
    void BeginRecovery(FPhysicalGetUpLaneRuntime& Lane);
    void CreateRecoveryControls(FPhysicalGetUpLaneRuntime& Lane);
    void CompleteRecovery(FPhysicalGetUpLaneRuntime& Lane);
    void ResetLaneToProne(FPhysicalGetUpLaneRuntime& Lane, EPhysicalGetUpBranch PoseBranch);
    void SelectFrontPose();
    void SelectBackPose();
    void ApplyPlayerPush();
    void RequestPlayerRecovery();
    void ResetInteractiveLane();
    void UpdateAutomatedInteractionScript();
    void ExportEvidence();
    void ExportInteractionEvidence();
    static const TCHAR* PhaseToString(EPhysicalGetUpLanePhase Phase);

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Ground;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USkeletalMeshComponent> LaneAMesh;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USkeletalMeshComponent> LaneBMesh;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USkeletalMeshComponent> RejectMesh;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UPhysicsControlComponent> LaneAControl;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UPhysicsControlComponent> LaneBControl;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCameraComponent> Camera;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UPointLightComponent> FillLight;

    UPROPERTY()
    TObjectPtr<UAnimSequence> FrontGetUp;

    UPROPERTY()
    TObjectPtr<UAnimSequence> BackGetUp;

    FPhysicalGetUpLaneRuntime LaneA;
    FPhysicalGetUpLaneRuntime LaneB;
    FPhysicalGetUpLaneRuntime RejectLane;
    double ElapsedSeconds = 0.0;
    double AutoExitCountdown = -1.0;
    bool bFallImpulsesApplied = false;
    bool bCanonicalLanesSettled = false;
    bool bRejectAgitationApplied = false;
    bool bRunComplete = false;
    bool bEvidenceExported = false;
    bool bAutoRun = false;
    bool bInteractiveMode = false;
    bool bInteractionAuto = false;
    bool bPlayerRecoveryRequested = false;
    bool bInteractionEvidenceExported = false;
    int32 SelectedPoseCount = 0;
    int32 PushCount = 0;
    int32 RecoveryRequestCount = 0;
    int32 ResetCount = 0;
    int32 InteractionScriptStep = 0;
    double InteractionStepStartSeconds = 0.0;
    EPhysicalGetUpBranch SelectedPoseBranch = EPhysicalGetUpBranch::Front;
};
