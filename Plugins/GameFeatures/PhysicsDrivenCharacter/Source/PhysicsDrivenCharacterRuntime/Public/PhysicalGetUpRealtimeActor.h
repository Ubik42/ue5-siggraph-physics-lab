#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PhysicalGetUpDecision.h"
#include "PhysicalGetUpRealtimeActor.generated.h"

class UAnimSequence;
class ACharacter;
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
    FVector FilteredLinearVelocity = FVector::ZeroVector;
    FVector FilteredAngularVelocity = FVector::ZeroVector;
    FTransform PreviousPelvisTransform = FTransform::Identity;
    bool bHasPreviousPelvisTransform = false;
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
    bool IsContinuousMode() const { return bContinuousMode; }

private:
    void BeginContinuousDemo();
    void TickContinuousDemo(float DeltaSeconds);
    void PushContinuousCharacter();
    void MoveContinuousCharacter(float Value);
    void ExportContinuousEvidence();
    void AlignContinuousRecovery();
    void RestoreContinuousMovement();
    UPROPERTY()
    TObjectPtr<ACharacter> ContinuousCharacter;
    UPROPERTY()
    TObjectPtr<UAnimSequence> ContinuousIdle;
    UPROPERTY()
    TObjectPtr<UAnimSequence> ContinuousWalk;
    bool bContinuousMode = false;
    bool bContinuousAuto = false;
    bool bContinuousWalking = false;
    bool bContinuousPushed = false;
    bool bContinuousRequested = false;
    bool bContinuousStandingTarget = false;
    int32 ContinuousRejectedRequests = 0;
    double ContinuousRecoveredAt = -1.0;
    double ContinuousUprightStableSeconds = 0.0;
    FVector ContinuousRecoveryPosition = FVector::ZeroVector;
    double ContinuousMovedAfterRecovery = 0.0;
    FString ContinuousTrace = TEXT("time_s,phase,branch,grounded,face_dot,speed_cm_s,angular_rad_s,stable_s,controls,simulating,movement_mode,x_cm,y_cm,z_cm,raw_speed_cm_s,raw_angular_rad_s,pelvis_x,pelvis_y,pelvis_z\n");
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
