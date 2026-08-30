#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SoftBodyGpuTopology.h"
#include "SoftBodyGpuXpbd.h"
#include "SoftBodyLocalXpbdSolver.h"
#include "SoftBodySurfaceBinding.h"
#include "SoftBodyTetrahedralMesh.h"
#include "SoftBodyDragonRealtimeActor.generated.h"

class UCameraComponent;
class UDirectionalLightComponent;
class UDynamicMeshComponent;
class UPhysicsExperimentComponent;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

/** Case 02：持久 GPU 四面体 Cage 通过确定性权重驱动 Dragon 动态表面。 */
UCLASS(DisplayName = "GPU 多尺度软体 Dragon 逐帧运行")
class PHYSICSSOFTBODYRUNTIME_API ASoftBodyDragonRealtimeActor : public AActor
{
	GENERATED_BODY()

public:
	ASoftBodyDragonRealtimeActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UPhysicsExperimentComponent* GetExperimentComponent() const { return ExperimentComponent; }
	FSoftBodyGpuXpbdStats GetGpuStats() const { return GpuSimulation.GetStats(); }
	FSoftBodyLocalXpbdStats GetCpuStats() const { return CpuLatestStats; }
	int32 GetSurfaceVertexCount() const { return SurfaceBinding.RestSurfacePositions.Num(); }
	int32 GetSurfaceTriangleCount() const { return SurfaceTriangleCount; }
	int32 GetCageVertexCount() const { return VolumeCage.RestPositions.Num(); }
	int32 GetCageTetrahedronCount() const { return VolumeCage.Tetrahedra.Num(); }
	int32 GetFixedCageVertexCount() const { return FixedCageVertexCount; }
	int64 GetLatestReadbackStep() const { return LatestReadbackStep; }
	int64 GetSurfaceUpdateCount() const { return SurfaceUpdateCount; }
	int32 GetPauseToggleCount() const { return PauseToggleCount; }
	int32 GetSingleStepCount() const { return SingleStepCount; }
	int32 GetResetCount() const { return ResetCount; }
	int32 GetDebugToggleCount() const { return DebugToggleCount; }
	int32 GetManualExportCount() const { return ManualExportCount; }
	bool IsDebugDrawEnabled() const { return bEnableDebugDraw; }
	bool IsInteractionScriptEnabled() const { return bInteractionScriptRequested; }
	bool IsBackendToggleScriptEnabled() const { return bBackendToggleScriptRequested; }
	double GetMaximumSurfaceDisplacementCm() const { return MaximumSurfaceDisplacementCm; }
	double GetMaximumRestReconstructionErrorCm() const { return MaximumRestReconstructionErrorCm; }
	double GetMaximumFixedFaceDriftCm() const { return MaximumFixedFaceDriftCm; }
	double GetMaximumGroundPenetrationCm() const { return MaximumGroundPenetrationCm; }
	int32 GetActiveGroundContactVertices() const { return ActiveGroundContactVertices; }
	float GetGroundContactStiffness() const { return bCpuReferenceRequested ? static_cast<float>(CpuSettings.GroundContactStiffness) : GpuSettings.GroundContactStiffness; }
	int32 GetContactProfileToggleCount() const { return ContactProfileToggleCount; }
	int32 GetBackendToggleCount() const { return BackendToggleCount; }
	bool IsWeakGroundContactProfile() const { return bWeakGroundContactRequested; }
	bool IsCpuReferenceBackend() const { return bCpuReferenceRequested; }
	int64 GetDisplayedSolverSteps() const;
	int64 GetDisplayedLowFrequencyPasses() const;
	int64 GetDisplayedGroundContactPasses() const;
	double GetMeanCpuStepMilliseconds() const;
	double GetMaximumCpuStepMilliseconds() const { return MaximumCpuStepMilliseconds; }
	const FString& GetStatusText() const { return StatusText; }
	bool DidRunPass() const { return bRunComplete && bRunPassed; }

protected:
	UFUNCTION()
	void HandleExperimentStep(double FixedDeltaSeconds, int64 StepIndex);

private:
	bool BuildRuntimeAsset(FString& OutFailureReason);
	bool BuildVolumeCageAndBinding(FString& OutFailureReason);
	bool InitializeGpuSimulation(FString& OutFailureReason);
	bool InitializeCpuSimulation(FString& OutFailureReason);
	bool InitializeActiveSimulation(FString& OutFailureReason);
	bool ResetInteractiveRun(bool bStartAfterReset);
	void ResetRunMeasurements();
	void HandleInteractiveInput();
	void AdvanceInteractionScript();
	void TogglePauseResume();
	void ExecuteSingleStep();
	void ToggleDebugDraw();
	void ToggleGroundContactProfile();
	void ToggleSolverBackend();
	void ExportManualMetrics();
	void ProcessCpuStep(int64 StepIndex);
	void ProcessLatestReadback();
	void UpdateDynamicSurface(TConstArrayView<FVector3f> Positions);
	void DrawCageDebug() const;
	void CompleteRun();
	void WriteRunReport() const;
	FString GetReportStem() const;
	void CaptureScreenshotAndScheduleExit();
	void RequestOwnedProcessExit();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPhysicsExperimentComponent> ExperimentComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDynamicMeshComponent> DragonSurface;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> GroundPlane;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> DirectionalLight;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> PointLight;

	FSoftBodyTetGridSettings GridSettings;
	FSoftBodyTetMesh VolumeCage;
	FSoftBodyGpuTopology GpuTopology;
	FSoftBodySurfaceBinding SurfaceBinding;
	FSoftBodyGpuXpbdSettings GpuSettings;
	FSoftBodyGpuXpbdSimulation GpuSimulation;
	FSoftBodyLocalXpbdSettings CpuSettings;
	FSoftBodyLocalXpbdSolver CpuSimulation;
	FSoftBodyLocalXpbdStats CpuLatestStats;
	TArray<FVector3f> LatestCagePositions;
	TArray<FVector3f> LatestSurfacePositions;
	FTimerHandle CaptureTimerHandle;
	FTimerHandle ExitTimerHandle;
	FString StatusText = TEXT("正在建立 Dragon 表面与体积 Cage");
	int32 SurfaceTriangleCount = 0;
	int32 FixedCageVertexCount = 0;
	int64 LatestReadbackStep = 0;
	int64 SurfaceUpdateCount = 0;
	int32 PauseToggleCount = 0;
	int32 SingleStepCount = 0;
	int32 ResetCount = 0;
	int32 DebugToggleCount = 0;
	int32 ManualExportCount = 0;
	int32 ContactProfileToggleCount = 0;
	int32 BackendToggleCount = 0;
	int32 InteractionScriptPhase = 0;
	double MaximumSurfaceDisplacementCm = 0.0;
	double MaximumRestReconstructionErrorCm = 0.0;
	double MaximumFixedFaceDriftCm = 0.0;
	double MaximumGroundPenetrationCm = 0.0;
	int32 ActiveGroundContactVertices = 0;
	int64 CpuLowFrequencyPassesExecuted = 0;
	int64 CpuGroundContactPassesExecuted = 0;
	double TotalCpuStepMilliseconds = 0.0;
	double MaximumCpuStepMilliseconds = 0.0;
	bool bAwaitingFinalReadback = false;
	bool bRunComplete = false;
	bool bRunPassed = false;
	bool bCaptureRequested = false;
	bool bAutoQuitRequested = false;
	bool bInteractionScriptRequested = false;
	bool bBackendToggleScriptRequested = false;
	bool bWeakGroundContactRequested = false;
	bool bCpuReferenceRequested = false;
	bool bEnableDebugDraw = true;
	static constexpr int64 StepsToRun = 300;
};
