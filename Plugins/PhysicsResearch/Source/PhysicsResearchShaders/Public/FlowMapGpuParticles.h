#pragma once

#include "CoreMinimal.h"

struct FFlowMapGpuParticleState
{
	FVector2f PositionCm = FVector2f::ZeroVector;
	FVector4f BackwardMapJacobian = FVector4f(1.0f, 0.0f, 0.0f, 1.0f);
};

struct FFlowMapGpuParticleInput
{
	FIntPoint CellCounts = FIntPoint(32, 32);
	float CellSizeCm = 5.0f;
	FVector2f OriginCm = FVector2f::ZeroVector;
	float FixedDeltaSeconds = 1.0f / 240.0f;
	TArray<float> FaceU;
	TArray<float> FaceV;
	TArray<FFlowMapGpuParticleState> Particles;
};

struct FFlowMapGpuParticleSettings
{
	int32 ReadbackIntervalSteps = 4;
};

struct FFlowMapGpuParticleStats
{
	bool bInitialized = false;
	bool bReadbackPending = false;
	int64 StepsEnqueued = 0;
	int64 StepsExecuted = 0;
	int64 AdvectionPassesExecuted = 0;
	int64 ReadbacksCompleted = 0;
	int64 LatestReadbackStep = 0;
};

class FFlowMapGpuParticleStateInternal;

/** 周期 MAC 场上的持久 GPU 粒子/Backward Map 会话；运行入口不等待 GPU。 */
class PHYSICSRESEARCHSHADERS_API FFlowMapGpuParticleSession
{
public:
	FFlowMapGpuParticleSession();
	~FFlowMapGpuParticleSession();

	bool Initialize(const FFlowMapGpuParticleInput& Input, const FFlowMapGpuParticleSettings& Settings);
	bool EnqueueAdvectionStep();
	void RequestReadback();
	void PollReadback();
	bool ConsumeLatestParticles(TArray<FFlowMapGpuParticleState>& OutParticles, int64& OutStepIndex);
	FFlowMapGpuParticleStats GetStats() const;
	void FlushAndWaitForReadbackForValidation();
	void Shutdown();

private:
	TSharedPtr<FFlowMapGpuParticleStateInternal, ESPMode::ThreadSafe> State;
};
