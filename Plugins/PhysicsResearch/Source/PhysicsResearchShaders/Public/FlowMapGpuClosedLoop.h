#pragma once

#include "CoreMinimal.h"
#include "FlowMapGpuImpulseTransfer.h"

struct FFlowMapGpuClosedLoopInput
{
	FIntPoint CellCounts = FIntPoint(16, 16);
	float CellSizeCm = 5.0f;
	FVector2f OriginCm = FVector2f::ZeroVector;
	float FixedDeltaSeconds = 1.0f / 240.0f;
	int32 PressureIterations = 64;
	int32 LongMapResetSteps = 20;
	int32 ReadbackIntervalSteps = 2;
	int32 GpuTimingWarmupSteps = 8;
	/** 0 表示使用 0.9*CellSize/FixedDelta 的默认 CFL 上限；作品运行可给出更严格预算。 */
	float MaximumFaceVelocityCmPerSecond = 0.0f;
	bool bUseGradient = true;
	TArray<float> InitialFaceU;
	TArray<float> InitialFaceV;
	TArray<FFlowMapGpuImpulseParticle> Particles;
};

struct FFlowMapGpuSolidBoundaryUpdate
{
	struct FSolidCircle
	{
		FVector2f CenterCm = FVector2f::ZeroVector;
		float RadiusCm = 10.0f;
		float SafetyOffsetCm = 0.05f;
	};

	TArray<uint32> SolidCellMask;
	TArray<uint32> SolidFaceUMask;
	TArray<uint32> SolidFaceVMask;
	TArray<float> SolidFaceVelocityU;
	TArray<float> SolidFaceVelocityV;
	TArray<FSolidCircle> ParticleCollisionCircles;
};

struct FFlowMapGpuClosedLoopStats
{
	bool bInitialized = false;
	bool bReadbackPending = false;
	int64 StepsEnqueued = 0;
	int64 StepsExecuted = 0;
	int64 ParticleAdvectionPasses = 0;
	int64 ImpulseGatherPasses = 0;
	int64 DivergencePasses = 0;
	int64 PressureJacobiPasses = 0;
	int64 PressureApplyPasses = 0;
	int64 MapResetPasses = 0;
	int64 MapResets = 0;
	int64 SolidBoundaryUpdatesAccepted = 0;
	int64 SolidBoundaryApplicationsExecuted = 0;
	int64 LatestSolidBoundaryVersion = 0;
	int32 LatestSolidCellCount = 0;
	int32 LatestSolidFaceUCount = 0;
	int32 LatestSolidFaceVCount = 0;
	int32 LatestSolidCircleCount = 0;
	int64 ParticleSolidProjectionPasses = 0;
	int64 ReadbacksCompleted = 0;
	int64 LatestReadbackStep = 0;
	bool bGpuTimingSupported = false;
	int64 GpuTimingSamplesCompleted = 0;
	int64 GpuTimingSamplesSkipped = 0;
	int64 LatestGpuTimingStep = 0;
	double LatestGpuStepMilliseconds = 0.0;
	double MeanGpuStepMilliseconds = 0.0;
	double P95GpuStepMilliseconds = 0.0;
	double P99GpuStepMilliseconds = 0.0;
	double MaximumGpuStepMilliseconds = 0.0;
	int64 SteadyStateGpuTimingSamples = 0;
	double MeanSteadyStateGpuStepMilliseconds = 0.0;
	double P95SteadyStateGpuStepMilliseconds = 0.0;
	double P99SteadyStateGpuStepMilliseconds = 0.0;
	double MaximumSteadyStateGpuStepMilliseconds = 0.0;
};

struct FFlowMapGpuClosedLoopSnapshot
{
	int64 StepIndex = 0;
	TArray<float> FaceU;
	TArray<float> FaceV;
	/** 最终 Jacobi 压力势；单位沿当前速度投影离散式，不冒充 Pa。 */
	TArray<float> PressurePotential;
	TArray<FVector2f> ParticlePositionsCm;
	TArray<FVector4f> BackwardMapJacobians;
};

class FFlowMapGpuClosedLoopState;

/** 持久 GPU Flow Map 闭环：粒子平流 → impulse/梯度 P2G → 压力投影 → 下一步速度。 */
class PHYSICSRESEARCHSHADERS_API FFlowMapGpuClosedLoopSession
{
public:
	FFlowMapGpuClosedLoopSession();
	~FFlowMapGpuClosedLoopSession();

	bool Initialize(const FFlowMapGpuClosedLoopInput& Input);
	bool UpdateSolidBoundary(const FFlowMapGpuSolidBoundaryUpdate& Boundary);
	bool EnqueueStep();
	void RequestReadback();
	void PollReadback();
	bool ConsumeLatestSnapshot(FFlowMapGpuClosedLoopSnapshot& OutSnapshot);
	FFlowMapGpuClosedLoopStats GetStats() const;
	void GetGpuTimingSamples(TArray<int64>& OutSteps, TArray<double>& OutMilliseconds) const;
	void FlushAndWaitForReadbackForValidation();
	void Shutdown();

private:
	TSharedPtr<FFlowMapGpuClosedLoopState, ESPMode::ThreadSafe> State;
};
