#pragma once

#include "CoreMinimal.h"

struct FFlowMapGpuPressureInput
{
	FIntPoint CellCounts = FIntPoint(32, 16);
	float CellSizeCm = 5.0f;
	int32 JacobiIterations = 512;
	TArray<float> FaceU;
	TArray<float> FaceV;
};

struct FFlowMapGpuPressureResult
{
	bool bDispatched = false;
	bool bReadbackSucceeded = false;
	TArray<float> FaceU;
	TArray<float> FaceV;
	int32 DivergencePasses = 0;
	int32 JacobiPasses = 0;
	int32 ApplyPasses = 0;
	FString FailureReason;
};

/** Blocking CPU/GPU truth gate only; runtime migration will use a persistent non-blocking session. */
PHYSICSRESEARCHSHADERS_API FFlowMapGpuPressureResult RunFlowMapGpuPressureForValidation(
	const FFlowMapGpuPressureInput& Input);

struct FFlowMapGpuPressureSessionSettings
{
	int32 JacobiIterationsPerStep = 128;
	int32 ReadbackIntervalSteps = 4;
};

struct FFlowMapGpuPressureSessionStats
{
	bool bInitialized = false;
	bool bReadbackPending = false;
	int64 StepsEnqueued = 0;
	int64 StepsExecuted = 0;
	int64 ReadbacksCompleted = 0;
	int64 LatestReadbackStep = 0;
	int64 DivergencePassesExecuted = 0;
	int64 JacobiPassesExecuted = 0;
	int64 ApplyPassesExecuted = 0;
};

class FFlowMapGpuPressureSessionState;

/** 持久 GPU 压力投影会话；运行方法只排队，快照通过双 Buffer Readback 异步消费。 */
class PHYSICSRESEARCHSHADERS_API FFlowMapGpuPressureSession
{
public:
	FFlowMapGpuPressureSession();
	~FFlowMapGpuPressureSession();

	bool Initialize(const FFlowMapGpuPressureInput& Input, const FFlowMapGpuPressureSessionSettings& Settings);
	bool EnqueueProjectionStep();
	void RequestReadback();
	void PollReadback();
	bool ConsumeLatestVelocity(TArray<float>& OutFaceU, TArray<float>& OutFaceV, int64& OutStepIndex);
	FFlowMapGpuPressureSessionStats GetStats() const;
	void FlushAndWaitForReadbackForValidation();
	void Shutdown();

private:
	TSharedPtr<FFlowMapGpuPressureSessionState, ESPMode::ThreadSafe> State;
};
