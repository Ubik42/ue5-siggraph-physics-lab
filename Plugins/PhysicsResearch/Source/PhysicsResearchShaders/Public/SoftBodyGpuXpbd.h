#pragma once

#include "CoreMinimal.h"

struct FSoftBodyGpuXpbdInput
{
	TArray<FVector3f> RestPositions;
	TArray<float> InverseMasses;
	TArray<FIntPoint> EdgeVertices;
	TArray<float> EdgeRestLengthsCm;
	TArray<uint32> EdgeMaterialIds;
	TArray<uint32> VertexEdgeOffsets;
	TArray<uint32> VertexEdgeConstraintIndices;
	TArray<uint32> VertexEdgeCorners;
	TArray<FIntVector4> TetrahedronVertices;
	TArray<float> TetrahedronRestVolumesCm3;
	TArray<uint32> TetrahedronMaterialIds;
	TArray<uint32> VertexTetrahedronOffsets;
	TArray<uint32> VertexTetrahedronConstraintIndices;
	TArray<uint32> VertexTetrahedronCorners;
};

struct FSoftBodyGpuXpbdSettings
{
	float FixedDeltaSeconds = 1.0f / 60.0f;
	int32 SolverIterations = 32;
	FVector3f GravityCmPerSecondSquared = FVector3f(0.0f, 0.0f, -980.0f);
	float VelocityDamping = 0.98f;
	float JacobiRelaxation = 0.85f;
	float MaximumVertexCorrectionCm = 2.0f;
	float SoftEdgeCompliance = 2.e-8f;
	float StiffEdgeCompliance = 2.e-9f;
	float SoftVolumeCompliance = 2.e-10f;
	float StiffVolumeCompliance = 2.e-11f;
	int32 ReadbackIntervalSteps = 10;
	int32 GpuTimingWarmupSteps = 4;
	bool bEnableLowFrequencyCorrection = false;
	float LowFrequencyCorrectionStrength = 0.35f;
	int32 LowFrequencyCorrectionInterval = 1;
	bool bEnableGroundContact = false;
	float GroundPlaneZCm = 0.0f;
	/** 0..1 的单步接触投影强度；1 为硬投影，失败消融只修改此项。 */
	float GroundContactStiffness = 1.0f;
};

struct FSoftBodyGpuXpbdStats
{
	bool bInitialized = false;
	bool bReadbackPending = false;
	int64 StepsEnqueued = 0;
	int64 StepsExecuted = 0;
	int64 ReadbacksCompleted = 0;
	int64 LatestReadbackStep = 0;
	int64 DistancePassesExecuted = 0;
	int64 VolumePassesExecuted = 0;
	int64 GatherPassesExecuted = 0;
	int64 LowFrequencyProjectionPassesExecuted = 0;
	int64 LowFrequencyApplyPassesExecuted = 0;
	int64 LowFrequencyReductionDispatchesExecuted = 0;
	int32 LowFrequencyModeCount = 0;
	int64 GroundContactPassesExecuted = 0;
	bool bGpuTimingSupported = false;
	int64 GpuTimingSamplesCompleted = 0;
	int64 GpuTimingSamplesSkipped = 0;
	double MeanGpuStepMilliseconds = 0.0;
	double MedianGpuStepMilliseconds = 0.0;
	double P95GpuStepMilliseconds = 0.0;
	double P99GpuStepMilliseconds = 0.0;
	double MaximumGpuStepMilliseconds = 0.0;
	int64 SteadyStateGpuTimingSamples = 0;
	double MeanSteadyStateGpuStepMilliseconds = 0.0;
	double MedianSteadyStateGpuStepMilliseconds = 0.0;
	double P95SteadyStateGpuStepMilliseconds = 0.0;
	double P99SteadyStateGpuStepMilliseconds = 0.0;
	double MaximumSteadyStateGpuStepMilliseconds = 0.0;
};

class FSoftBodyGpuXpbdState;

/** 持久 GPU 四面体 XPBD 会话；运行方法只排队，不等待 GPU。 */
class PHYSICSRESEARCHSHADERS_API FSoftBodyGpuXpbdSimulation
{
public:
	FSoftBodyGpuXpbdSimulation();
	~FSoftBodyGpuXpbdSimulation();

	bool Initialize(const FSoftBodyGpuXpbdInput& Input, const FSoftBodyGpuXpbdSettings& Settings);
	bool EnqueueStep();
	void RequestReadback();
	void PollReadback();
	bool ConsumeLatestPositions(TArray<FVector3f>& OutPositions, int64& OutStepIndex);
	FSoftBodyGpuXpbdStats GetStats() const;
	void GetGpuTimingSamples(TArray<int64>& OutSteps, TArray<double>& OutMilliseconds) const;
	void FlushAndWaitForReadbackForValidation();
	void Shutdown();

private:
	TSharedPtr<FSoftBodyGpuXpbdState, ESPMode::ThreadSafe> State;
};
