#pragma once

#include "ContactClothChaosTopology.h"
#include "ContactClothGpuExplicitTopology.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ContactClothExplicitTopologyActor.generated.h"

class UCameraComponent;
class UDirectionalLightComponent;
class UPhysicsExperimentComponent;
class UPointLightComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

struct FContactClothBackendComparisonInput
{
	TArray<FVector3f> ChaosPositions;
	TArray<FContactClothGpuSphere> CollisionSpheres;
	TArray<FContactClothGpuCapsule> CollisionCapsules;
	double ChaosMaximumEdgeErrorCm = 0.0;
	double ChaosMaximumPenetrationCm = 0.0;
};

struct FContactClothBackendComparisonSample
{
	int64 Step = 0;
	double ChaosMeanDynamicDisplacementCm = 0.0;
	double ResearchMeanDynamicDisplacementCm = 0.0;
	double MeanBackendDistanceCm = 0.0;
	double MaximumBackendDistanceCm = 0.0;
	double ChaosMaximumEdgeErrorCm = 0.0;
	double ResearchMaximumEdgeErrorCm = 0.0;
	double ChaosMaximumPenetrationCm = 0.0;
	double ResearchMaximumPenetrationCm = 0.0;
};

enum class EContactClothInteractiveDisplayMode : uint8
{
	Both,
	ChaosOnly,
	ResearchOnly
};

UCLASS(DisplayName = "Assassin 显式拓扑 GPU 布料")
class PHYSICSCONTACTCLOTHRUNTIME_API AContactClothExplicitTopologyActor : public AActor
{
	GENERATED_BODY()

public:
	AContactClothExplicitTopologyActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UPhysicsExperimentComponent* GetExperimentComponent() const { return ExperimentComponent; }
	FContactClothGpuExplicitTopologyRealtimeStats GetGpuStats() const { return GpuSimulation.GetStats(); }
	int32 GetParticleCount() const { return Topology.RestPositions.Num(); }
	int32 GetEdgeCount() const { return Topology.StructuralEdges.Num(); }
	int32 GetFixedVertexCount() const { return Topology.FixedVertexCount; }
	int32 GetDynamicVertexCount() const { return Topology.DynamicVertexCount; }
	int32 GetCollisionSphereCount() const { return CollisionSpheres.Num(); }
	int32 GetCollisionCapsuleCount() const { return CollisionCapsules.Num(); }
	int32 GetModalModeCount() const { return Settings.ModalModeCount; }
	bool IsRankOneEnabled() const { return Settings.bEnableRankOneCollisionHessian; }
	int64 GetLatestReadbackStep() const { return LatestReadbackStep; }
	double GetAnimationTargetMotionCm() const { return AnimationTargetMotionCm; }
	double GetFixedTargetErrorCm() const { return FixedTargetErrorCm; }
	double GetAverageDynamicDisplacementCm() const { return AverageDynamicDisplacementCm; }
	double GetMaximumEdgeErrorCm() const { return MaximumEdgeErrorCm; }
	double GetMaximumCollisionPenetrationCm() const { return MaximumCollisionPenetrationCm; }
	int64 GetChaosSnapshotCount() const { return ChaosSnapshotCount; }
	int32 GetComparisonSampleCount() const { return ComparisonSamples.Num(); }
	const TArray<FContactClothBackendComparisonSample>& GetComparisonSamples() const { return ComparisonSamples; }
	void GetGpuTimingSamples(TArray<int64>& OutSteps, TArray<double>& OutMilliseconds) const
	{
		GpuSimulation.GetGpuTimingSamples(OutSteps, OutMilliseconds);
	}
	int64 GetLatestComparedStep() const { return LatestComparedStep; }
	double GetLatestBackendMeanDistanceCm() const { return LatestBackendMeanDistanceCm; }
	double GetMaximumBackendDistanceCm() const { return MaximumBackendDistanceCm; }
	double GetLatestChaosPenetrationCm() const { return LatestChaosPenetrationCm; }
	bool IsFixedFrameContractActive() const { return bFixedFrameContractActive; }
	bool IsReplayMode() const { return bReplayRequested; }
	bool IsWeakBarrierFailureProfile() const { return bWeakBarrierFailureProfile; }
	bool WasExpectedFailureObserved() const { return bExpectedFailureObserved; }
	bool IsDebugDrawEnabled() const { return bEnableDebugDraw; }
	bool IsInteractionScriptEnabled() const { return bInteractionScriptRequested; }
	FString GetDisplayModeText() const;
	FString GetFailureProfileText() const;
	int32 GetPauseToggleCount() const { return PauseToggleCount; }
	int32 GetSingleStepCount() const { return SingleStepCount; }
	int32 GetResetCount() const { return ResetCount; }
	int32 GetBackendSwitchCount() const { return BackendSwitchCount; }
	int32 GetDebugToggleCount() const { return DebugToggleCount; }
	int32 GetDriverFreezeCount() const { return DriverFreezeCount; }
	int32 GetDriverResumeCount() const { return DriverResumeCount; }
	int32 GetManualDriverStepCount() const { return ManualDriverStepCount; }
	FString GetReplayInputHashText() const { return FString::Printf(TEXT("%016llx"), ReplayInputHash); }
	const FString& GetStatusText() const { return StatusText; }
	bool DidRunPass() const { return bRunComplete && bRunPassed; }

protected:
	UFUNCTION()
	void HandleExperimentStep(double FixedDeltaSeconds, int64 StepIndex);

private:
	bool InitializeAssassinTopology();
	bool ConfigureModalSubspace();
	bool ConfigureChaosBaseline();
	bool UpdateAnimatedInputs(int64 StepIndex);
	bool ResetInteractiveRun(bool bStartAfterReset);
	void ResetRunMeasurements();
	void ApplyFailureProfileSettings();
	void HandleInteractiveInput();
	void AdvanceInteractionScript();
	void TogglePauseResume();
	void ExecuteSingleStep();
	void SetDriverPaused(bool bPaused);
	void CycleDisplayBackend();
	void ToggleDebugDraw();
	void ExportManualMetrics();
	bool CaptureChaosSnapshot(int64 StepIndex);
	void CompareBackendSnapshot(TConstArrayView<FVector3f> ResearchPositions, int64 StepIndex);
	void ProcessLatestReadback();
	void DrawTopologyDebug() const;
	void CompleteRun();
	void WriteRunReport() const;
	void WriteComparisonTimeSeries() const;
	void WriteGpuTimingSeries() const;
	void AccumulateReplayInputHash(int64 StepIndex);
	FString GetReportStem() const;
	void CaptureScreenshotAndScheduleExit();
	void RequestOwnedProcessExit();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPhysicsExperimentComponent> ExperimentComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> AssassinMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> FloorMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> DirectionalLight;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> PointLight;

	FContactClothExplicitTopology Topology;
	FContactClothGpuExplicitTopologySimulation GpuSimulation;
	FContactClothGpuExplicitTopologyRealtimeSettings Settings;
	TArray<FVector3f> InitialTargets;
	TArray<FVector3f> LatestSubmittedTargets;
	TArray<FVector3f> LatestPositions;
	TArray<FContactClothGpuSphere> CollisionSpheres;
	TArray<FContactClothGpuCapsule> CollisionCapsules;
	TArray<FVector3f> LatestChaosPositions;
	TMap<int64, FContactClothBackendComparisonInput> PendingComparisonInputs;
	TArray<FContactClothBackendComparisonSample> ComparisonSamples;
	FTimerHandle CaptureTimerHandle;
	FTimerHandle ExitTimerHandle;
	FString StatusText = TEXT("初始化 Assassin Clothing 显式拓扑");
	FString ExpectedReplayInputHash;
	uint64 ReplayInputHash = 1469598103934665603ull;
	int64 LatestReadbackStep = 0;
	double AnimationTargetMotionCm = 0.0;
	double FixedTargetErrorCm = 0.0;
	double AverageDynamicDisplacementCm = 0.0;
	double MaximumEdgeErrorCm = 0.0;
	double MaximumCollisionPenetrationCm = 0.0;
	double LatestBackendMeanDistanceCm = 0.0;
	double MaximumBackendDistanceCm = 0.0;
	double LatestChaosPenetrationCm = 0.0;
	double FixedFrameDeltaSeconds = 0.0;
	int64 ChaosSnapshotCount = 0;
	int64 LatestComparedStep = 0;
	int32 PreviousChaosTimeStepSmoothing = INDEX_NONE;
	bool bFixedFrameContractActive = false;
	bool bChaosTimeStepSmoothingOverridden = false;
	bool bAwaitingFinalReadback = false;
	bool bRunComplete = false;
	bool bRunPassed = false;
	bool bCaptureRequested = false;
	bool bAutoQuitRequested = false;
	bool bReplayRequested = false;
	bool bReplayHashMatched = true;
	bool bWeakBarrierFailureProfile = false;
	bool bExpectedFailureObserved = false;
	bool bInteractionScriptRequested = false;
	bool bEnableDebugDraw = true;
	bool bInteractionResetDone = false;
	bool bPauseAfterNextStep = false;
	int32 InteractionScriptPhase = 0;
	int32 PauseToggleCount = 0;
	int32 SingleStepCount = 0;
	int32 ResetCount = 0;
	int32 BackendSwitchCount = 0;
	int32 DebugToggleCount = 0;
	int32 DriverFreezeCount = 0;
	int32 DriverResumeCount = 0;
	int32 ManualDriverStepCount = 0;
	EContactClothInteractiveDisplayMode DisplayMode = EContactClothInteractiveDisplayMode::Both;
	static constexpr int64 StepsToRun = 300;
};
