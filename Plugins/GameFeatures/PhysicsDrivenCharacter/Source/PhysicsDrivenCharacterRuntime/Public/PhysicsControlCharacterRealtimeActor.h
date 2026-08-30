#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PhysicsControlCharacterRealtimeActor.generated.h"

class UCameraComponent;
class UMaterialInterface;
class UPointLightComponent;
class UMeasuredPhysicsControlComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

USTRUCT()
struct FDrivenCharacterRealtimeSample
{
    GENERATED_BODY()

    double TimeSeconds = 0.0;
    double BaselinePoseErrorDegrees = 0.0;
    double ResearchPoseErrorDegrees = 0.0;
    double BaselinePelvisDisplacementCentimeters = 0.0;
    double ResearchPelvisDisplacementCentimeters = 0.0;
    double BaselinePelvisHeightCentimeters = 0.0;
    double ResearchPelvisHeightCentimeters = 0.0;
    double BaselinePelvisVerticalSpeedCentimetersPerSecond = 0.0;
    double ResearchPelvisVerticalSpeedCentimetersPerSecond = 0.0;
    double BaselineHipErrorDegrees = 0.0;
    double BaselineKneeErrorDegrees = 0.0;
    double BaselineTorsoErrorDegrees = 0.0;
    double ResearchHipErrorDegrees = 0.0;
    double ResearchKneeErrorDegrees = 0.0;
    double ResearchTorsoErrorDegrees = 0.0;
    double FrameWallMilliseconds = 0.0;
    double PhysicsControlUpdateMilliseconds = 0.0;
};

UCLASS()
class PHYSICSDRIVENCHARACTERRUNTIME_API APhysicsControlCharacterRealtimeActor : public AActor
{
    GENERATED_BODY()

public:
    APhysicsControlCharacterRealtimeActor();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    FString GetStatusLine() const;
    int32 GetPhysicsBodyCount() const { return PhysicsBodyCount; }
    int32 GetResearchControlCount() const { return ResearchControlCount; }
    double GetBaselinePoseErrorDegrees() const { return BaselinePoseErrorDegrees; }
    double GetResearchPoseErrorDegrees() const { return ResearchPoseErrorDegrees; }
    double GetBaselinePelvisDisplacementCentimeters() const { return BaselinePelvisDisplacementCentimeters; }
    double GetResearchPelvisDisplacementCentimeters() const { return ResearchPelvisDisplacementCentimeters; }
    bool IsImpulseApplied() const { return bImpulseApplied; }
    bool IsRunComplete() const { return bRunComplete; }
    bool IsPortfolioMode() const { return bPortfolioMode; }

private:
    void ConfigureMesh(USkeletalMeshComponent* Mesh, const TCHAR* CollisionProfile);
    void InitializePhysicsControl();
    void InitializePortfolioScene();
    void ApplySharedImpulse();
    double ComputeMeanPoseErrorDegrees(USkeletalMeshComponent* Mesh) const;
    double ComputeRelativeJointErrorDegrees(USkeletalMeshComponent* Mesh, FName ChildBone, FName ParentBone) const;
    void CaptureSample();
    void ExportRuntimeEvidence();

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Ground;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USkeletalMeshComponent> BaselineMesh;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USkeletalMeshComponent> ResearchMesh;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UMeasuredPhysicsControlComponent> ResearchPhysicsControl;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCameraComponent> Camera;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UPointLightComponent> FillLight;

    TMap<FName, FTransform> InitialBaselineBoneTransforms;
    TMap<FName, FTransform> InitialResearchBoneTransforms;
    TMap<FName, FQuat> InitialBaselineRelativeJointRotations;
    TMap<FName, FQuat> InitialResearchRelativeJointRotations;
    FVector InitialBaselinePelvisLocation = FVector::ZeroVector;
    FVector InitialResearchPelvisLocation = FVector::ZeroVector;
    FName ResearchPelvisControlName = NAME_None;
    TArray<FDrivenCharacterRealtimeSample> Samples;
    double ElapsedSeconds = 0.0;
    double BaselinePoseErrorDegrees = 0.0;
    double ResearchPoseErrorDegrees = 0.0;
    double BaselinePelvisDisplacementCentimeters = 0.0;
    double ResearchPelvisDisplacementCentimeters = 0.0;
    double MaximumBaselinePoseErrorDegrees = 0.0;
    double MaximumResearchPoseErrorDegrees = 0.0;
    double PeakBaselineDownwardSpeedCentimetersPerSecond = 0.0;
    double PeakResearchDownwardSpeedCentimetersPerSecond = 0.0;
    double MinimumBaselinePelvisHeightCentimeters = 0.0;
    double MinimumResearchPelvisHeightCentimeters = 0.0;
    FVector PeakBaselineJointErrorsDegrees = FVector::ZeroVector;
    FVector PeakResearchJointErrorsDegrees = FVector::ZeroVector;
    int32 PhysicsBodyCount = 0;
    int32 ResearchControlCount = 0;
    bool bImpulseApplied = false;
    bool bRunComplete = false;
    bool bEvidenceExported = false;
    bool bAutoRun = false;
    bool bDropMappingMode = false;
    bool bPerformanceMode = false;
    bool bPortfolioMode = false;
    double PreviousCaptureWallSeconds = 0.0;
    double AutoExitCountdown = -1.0;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> PortfolioMeshes;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UPointLightComponent>> PortfolioLights;
};
