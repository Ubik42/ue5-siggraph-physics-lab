#pragma once

#include "CoreMinimal.h"
#include "FlowMapGpuClosedLoop.h"
#include "GameFramework/Actor.h"
#include "ParticleFlowMap2D.h"
#include "FlowMapRealtimeActor.generated.h"

class UCameraComponent;
class UDirectionalLightComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPhysicsExperimentComponent;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EFlowMapRuntimeBackend : uint8
{
	ShortMapBaseline UMETA(DisplayName = "Short Map 数值基线"),
	ParticleFlowMapResearch UMETA(DisplayName = "Particle Flow Map 研究后端")
};

UENUM(BlueprintType)
enum class EFlowMapBudgetProfile : uint8
{
	Low UMETA(DisplayName = "低预算"),
	Medium UMETA(DisplayName = "中预算"),
	High UMETA(DisplayName = "高预算")
};

/** Case03：Live Chaos 运动球体驱动持久 GPU Flow Map 边界与粒子展示。 */
UCLASS(DisplayName = "Flow Map + Chaos 运动障碍运行实验")
class PHYSICSFLOWMAPRUNTIME_API AFlowMapRealtimeActor : public AActor
{
	GENERATED_BODY()

public:
	AFlowMapRealtimeActor();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UPhysicsExperimentComponent* GetExperimentComponent() const { return ExperimentComponent; }
	FFlowMapGpuClosedLoopStats GetGpuStats() const { return GpuSession.GetStats(); }
	int64 GetLatestSnapshotStep() const { return LatestSnapshotStep; }
	int64 GetBoundaryUpdatesSubmitted() const { return BoundaryUpdatesSubmitted; }
	int64 GetChaosPoseSamples() const { return ChaosPoseSamples; }
	int64 GetRejectedSnapshots() const { return RejectedSnapshots; }
	int32 GetPauseToggleCount() const { return PauseToggleCount; }
	int32 GetSingleStepCount() const { return SingleStepCount; }
	int32 GetResetCount() const { return ResetCount; }
	int32 GetDebugToggleCount() const { return DebugToggleCount; }
	int32 GetManualExportCount() const { return ManualExportCount; }
	bool IsDebugDrawEnabled() const { return bDebugDrawEnabled; }
	bool IsVelocityBudgetAblation() const { return bVelocityBudgetAblationRequested; }
	int32 GetParticleInstanceCount() const;
	int32 GetVelocityArrowCount() const;
	float GetLatestMaximumVelocityCmPerSecond() const { return LatestMaximumVelocityCmPerSecond; }
	int32 GetLatestVelocityLimitedFaceCount() const { return LatestVelocityLimitedFaceCount; }
	float GetLatestMinimumPressurePotential() const { return LatestMinimumPressurePotential; }
	float GetLatestMaximumPressurePotential() const { return LatestMaximumPressurePotential; }
	int32 GetPositivePressureCellCount() const { return PositivePressureCellCount; }
	int32 GetNegativePressureCellCount() const { return NegativePressureCellCount; }
	EFlowMapRuntimeBackend GetRuntimeBackend() const { return RuntimeBackend; }
	EFlowMapBudgetProfile GetBudgetProfile() const { return BudgetProfile; }
	FString GetRuntimeBackendDisplayName() const;
	FString GetBudgetProfileDisplayName() const;
	int32 GetBackendToggleCount() const { return BackendToggleCount; }
	int32 GetBudgetToggleCount() const { return BudgetToggleCount; }
	const FString& GetStatusText() const { return StatusText; }
	bool IsInitialized() const { return bInitialized; }

protected:
	UFUNCTION()
	void HandleExperimentStep(double FixedDeltaSeconds, int64 StepIndex);

private:
	bool InitializeFlowMap(FString& OutFailureReason);
	bool SubmitChaosBoundary(double FixedDeltaSeconds, FString& OutFailureReason);
	void ProcessLatestSnapshot();
	void UpdateObstacleDrive();
	void HandleInteractiveInput();
	void TogglePauseResume();
	void ExecuteSingleStep();
	bool ResetInteractiveRun();
	void ToggleDebugDraw();
	void ToggleRuntimeBackend();
	void CycleBudgetProfile();
	void ConfigureRuntimeProfile();
	void ExportManualMetrics();
	void ExportLongSequenceEvidence();
	void DrawFlowDebug() const;
	void AdvanceInteractionScript();
	void AdvanceBackendBudgetScript();
	void SetupVisualMaterials();
	void UpdateVelocityInstances(const TArray<float>& FaceU,const TArray<float>& FaceV);
	void RequestPortfolioScreenshot();
	void UpdatePressureHeatmap(const TArray<float>& PressurePotential);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPhysicsExperimentComponent> ExperimentComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> ChaosObstacle;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> ParticleInstances;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> VelocityInstances;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> NegativePressureInstances;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> NeutralPressureInstances;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> PositivePressureInstances;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> DomainPlane;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> DirectionalLight;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> PointLight;

	FFlowMapGridSettings2D GridSettings;
	FFlowMapGpuClosedLoopSession GpuSession;
	FTransform PreviousChaosTransform;
	float ObstacleRadiusCm = 30.0f;
	float ObstacleDriveSpeedCmPerSecond = 45.0f;
	int32 ObstacleDriveDirection = 1;
	int64 LatestSnapshotStep = 0;
	int64 BoundaryUpdatesSubmitted = 0;
	int64 ChaosPoseSamples = 0;
	int64 RejectedSnapshots = 0;
	int32 PauseToggleCount = 0;
	int32 SingleStepCount = 0;
	int32 ResetCount = 0;
	int32 DebugToggleCount = 0;
	int32 ManualExportCount = 0;
	int32 BackendToggleCount = 0;
	int32 BudgetToggleCount = 0;
	bool bInitialized = false;
	bool bDebugDrawEnabled = false;
	bool bInteractionScriptRequested = false;
	bool bPortfolioCaptureRequested = false;
	bool bBackendBudgetScriptRequested = false;
	bool bLongSequenceCaptureRequested = false;
	bool bPortfolioCaptureIssued = false;
	bool bCameraViewAssigned = false;
	bool bVelocityBudgetAblationRequested = false;
	EFlowMapRuntimeBackend RuntimeBackend = EFlowMapRuntimeBackend::ParticleFlowMapResearch;
	EFlowMapBudgetProfile BudgetProfile = EFlowMapBudgetProfile::Medium;
	int32 InteractionScriptPhase = 0;
	int32 BackendBudgetScriptPhase = 0;
	int32 AutoExitSteps = 0;
	int32 PortfolioCaptureStep = 300;
	float LatestMaximumVelocityCmPerSecond = 0.0f;
	int32 LatestVelocityLimitedFaceCount = 0;
	float RuntimeMaximumFaceVelocityCmPerSecond = 120.0f;
	int32 RuntimePressureIterations = 0;
	int32 RuntimeMapResetSteps = 0;
	bool bRuntimeUseGradient = false;
	float LatestMinimumPressurePotential = 0.0f;
	float LatestMaximumPressurePotential = 0.0f;
	int32 PositivePressureCellCount = 0;
	int32 NegativePressureCellCount = 0;
	FString StatusText = TEXT("正在初始化 Flow Map 与 Chaos 障碍");
};
