#pragma once

#include "CoreMinimal.h"
#include "DifferentiableSpringExperiment.h"
#include "GameFramework/Actor.h"
#include "InverseSpringRealtimeActor.generated.h"

class UCameraComponent;
class UDirectionalLightComponent;
class UPhysicsConstraintComponent;
class UPhysicsExperimentComponent;
class UPointLightComponent;
class UStaticMeshComponent;

UCLASS()
class PHYSICSINVERSEDESIGNRUNTIME_API AInverseSpringRealtimeActor : public AActor
{
    GENERATED_BODY()

public:
    AInverseSpringRealtimeActor();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UPhysicsExperimentComponent* GetExperimentComponent() const { return ExperimentComponent; }
    FString GetStatusText() const { return StatusText; }
    int32 GetCompletedSteps() const { return CompletedSteps; }
    int32 GetTrajectorySteps() const { return TrajectorySteps; }
    double GetInitialLoss() const { return Optimization.InitialLoss; }
    double GetFittedLoss() const { return Optimization.FinalLoss; }
    double GetRecoveredDrive() const { return Optimization.Parameters.DriveForce; }
    double GetRecoveredDamping() const { return Optimization.Parameters.Damping; }
    double GetGradientRelativeError() const { return MaximumGradientRelativeError; }
    double GetChaosTrajectoryRmsCm() const;
    double GetChaosFinalTargetErrorCm() const { return ChaosFinalTargetErrorCm; }
    double GetResearchFinalTargetErrorCm() const { return ResearchFinalTargetErrorCm; }
    int32 GetChaosSamples() const { return ChaosSamples.Num(); }
    bool IsRunComplete() const { return bRunComplete; }
    bool IsIdentifiabilityFailure() const { return bIdentifiabilityFailure; }
    double GetParameterL1Error() const { return ParameterL1Error; }
    int32 GetObservationStep() const { return ObservationStep; }
    int32 GetPauseToggleCount() const { return PauseToggleCount; }
    int32 GetSingleStepCount() const { return SingleStepCount; }
    int32 GetResetCount() const { return ResetCount; }
    int32 GetReoptimizeCount() const { return ReoptimizeCount; }
    int32 GetDebugToggleCount() const { return DebugToggleCount; }
    int32 GetManualExportCount() const { return ManualExportCount; }
    int32 GetFailureToggleCount() const { return FailureToggleCount; }
    int32 GetInteractionScriptPhase() const { return InteractionScriptPhase; }

private:
    UFUNCTION()
    void HandleExperimentStep(double FixedDeltaSeconds, int64 StepIndex);
    void BuildExperiment();
    void ConfigureChaosReplay();
    void SetupVisualMaterials();
    void UpdateVisualState(int32 StepIndex);
    void ExportEvidence() const;
    void RequestPortfolioScreenshot();
    void HandleInteractiveInput();
    void AdvanceInteractionScript();
    void TogglePauseResume();
    void RequestSingleStep();
    void ResetRuntime(bool bCountReset);
    void ReoptimizeRuntime();
    void ToggleFailureMode();
    void ToggleDebugDisplay();

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> Root;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UPhysicsExperimentComponent> ExperimentComponent;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> TargetBody;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> InitialBody;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> ResearchBody;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> ChaosBody;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> ChaosAnchor;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> TargetGate;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> TrackFloor;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UPhysicsConstraintComponent> ChaosConstraint;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UDirectionalLightComponent> DirectionalLight;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UPointLightComponent> PointLight;

    FSpringDesignParameters TruthParameters{34.0, 14.0, 2.8};
    FSpringDesignParameters InitialParameters{12.0, 14.0, 0.4};
    FSpringDesignOptimizationResult Optimization;
    TArray<FSpringDesignState> TargetTrajectory;
    TArray<FSpringDesignState> InitialTrajectory;
    TArray<FSpringDesignState> ResearchTrajectory;
    TArray<double> ChaosSamples;
    FString StatusText = TEXT("等待初始化");
    double MaximumGradientRelativeError = 0.0;
    double ChaosSquaredErrorSum = 0.0;
    double ChaosFinalTargetErrorCm = 0.0;
    double ResearchFinalTargetErrorCm = 0.0;
    double ParameterL1Error = 0.0;
    FVector SavedChaosVelocity = FVector::ZeroVector;
    int32 CompletedSteps = 0;
    int32 TrajectorySteps = 120;
    int32 AutoExitFramesAfterCompletion = 30;
    int32 FramesAfterCompletion = 0;
    int32 ObservationStep = 120;
    int32 PauseToggleCount = 0;
    int32 SingleStepCount = 0;
    int32 ResetCount = 0;
    int32 ReoptimizeCount = 0;
    int32 DebugToggleCount = 0;
    int32 ManualExportCount = 0;
    int32 FailureToggleCount = 0;
    int32 InteractionScriptPhase = 0;
    int32 InteractionHoldFrames = 0;
    double VisualizationScaleCm = 100.0;
    bool bInitialized = false;
    bool bRunComplete = false;
    bool bCameraViewAssigned = false;
    bool bCaptureRequested = false;
    bool bCaptureIssued = false;
    bool bIdentifiabilityFailure = false;
    bool bSingleStepPending = false;
    bool bDebugDisplay = true;
    bool bInteractionScriptRequested = false;
};
