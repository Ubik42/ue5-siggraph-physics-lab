#pragma once

#include "ContactClothGpuJacobi.h"
#include "CoreMinimal.h"

struct FContactClothGpuExplicitTopologySettings
{
	int32 Iterations = 128;
	float Relaxation = 0.9f;
};

struct FContactClothGpuExplicitTopologyResult
{
	bool bDispatched = false;
	bool bReadbackSucceeded = false;
	int32 ParticleCount = 0;
	int32 EdgeCount = 0;
	TArray<FVector3f> Positions;
	double InputMaximumEdgeErrorCm = 0.0;
	double OutputMaximumEdgeErrorCm = 0.0;
	double MaximumFixedParticleDriftCm = 0.0;
	double BlockingWallMilliseconds = 0.0;
	FString FailureReason;
};

struct FContactClothGpuExplicitTopologyRealtimeSettings : public FContactClothGpuExplicitTopologySettings
{
	float FixedDeltaSeconds = 1.0f / 60.0f;
	FVector3f GravityCmPerSecondSquared = FVector3f(0.0f, 0.0f, -980.0f);
	float VelocityDamping = 0.995f;
	int32 ReadbackIntervalSteps = 10;
	int32 GpuTimingWarmupSteps = 16;
	int32 ModalModeCount = 0;
	float ModalCorrectionStrength = 0.0f;
	TArray<float> ModalBasisValues;
	TArray<float> ModalInverseSystemValues;
	bool bEnableRankOneCollisionHessian = false;
	TArray<float> ModalSystemValues;
	TArray<float> ModalParticleOuterProductValues;
	float RankOneMaximumCorrectionCm = 4.0f;
	float CollisionThicknessCm = 1.0f;
	bool bEnableNonDistanceBarrier = false;
	float NdbBaseWeight = 0.05f;
	float NdbActivationMultiplier = 2.0f;
	float NdbMaximumWeight = 1024.0f;
	int32 CollisionPrimitiveCapacity = 32;
	bool bEnableCompleteCcd = false;
	float CcdSafetyOffsetCm = 0.01f;
};

struct FContactClothGpuExplicitTopologyRealtimeStats
{
	bool bInitialized = false;
	bool bReadbackPending = false;
	int64 StepsEnqueued = 0;
	int64 StepsExecuted = 0;
	int64 ReadbacksCompleted = 0;
	int64 LatestReadbackStep = 0;
	int64 KinematicTargetUpdatesAccepted = 0;
	int64 KinematicTargetApplicationsExecuted = 0;
	int64 LatestKinematicTargetVersion = 0;
	int64 CollisionPrimitiveUpdatesAccepted = 0;
	int64 CollisionPrimitiveApplicationsExecuted = 0;
	int64 LatestCollisionPrimitiveVersion = 0;
	int64 ModalCorrectionsExecuted = 0;
	int64 RankOneCollisionSystemsExecuted = 0;
	int64 CompleteCcdPassesExecuted = 0;
	int64 NdbStateUpdatesExecuted = 0;
	int32 LatestActiveContactPairs = 0;
	int32 MaximumActiveContactPairs = 0;
	int32 LatestMaximumNdbActivationCount = 0;
	int32 MaximumNdbActivationCount = 0;
	int32 LatestCollisionSphereCount = 0;
	int32 LatestCollisionCapsuleCount = 0;
	double LatestRenderSubmissionMilliseconds = 0.0;
	double TotalRenderSubmissionMilliseconds = 0.0;
	double MaximumRenderSubmissionMilliseconds = 0.0;
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

/**
 * Blocking correctness gate for solver-neutral cloth topology. The GPU gathers a CPU-built CSR
 * adjacency list, so arbitrary Chaos physical meshes do not depend on rectangular-grid indexing.
 */
class PHYSICSRESEARCHSHADERS_API FContactClothGpuExplicitTopology
{
public:
	static FContactClothGpuExplicitTopologyResult RunBlocking(
		TConstArrayView<FVector3f> InputPositions,
		TConstArrayView<float> InverseMasses,
		TConstArrayView<FIntPoint> StructuralEdges,
		TConstArrayView<float> StructuralRestLengthsCm,
		const FContactClothGpuExplicitTopologySettings& Settings);

	static double MeasureMaximumEdgeErrorCm(
		TConstArrayView<FVector3f> Positions,
		TConstArrayView<FIntPoint> StructuralEdges,
		TConstArrayView<float> StructuralRestLengthsCm);

	static double MeasureMaximumCollisionPenetrationCm(
		TConstArrayView<FVector3f> Positions,
		TConstArrayView<float> InverseMasses,
		TConstArrayView<FContactClothGpuSphere> Spheres,
		TConstArrayView<FContactClothGpuCapsule> Capsules,
		float CollisionThicknessCm);
};

class FContactClothGpuExplicitTopologyRealtimeState;

/** Persistent arbitrary-topology dynamics session. Runtime methods enqueue work without waiting for the GPU. */
class PHYSICSRESEARCHSHADERS_API FContactClothGpuExplicitTopologySimulation
{
public:
	FContactClothGpuExplicitTopologySimulation();
	~FContactClothGpuExplicitTopologySimulation();

	bool Initialize(
		TConstArrayView<FVector3f> InitialPositions,
		TConstArrayView<float> InverseMasses,
		TConstArrayView<FIntPoint> StructuralEdges,
		TConstArrayView<float> StructuralRestLengthsCm,
		const FContactClothGpuExplicitTopologyRealtimeSettings& Settings);
	bool UpdateKinematicTargets(TConstArrayView<FVector3f> TargetPositions);
	bool UpdateCollisionPrimitives(
		TConstArrayView<FContactClothGpuSphere> Spheres,
		TConstArrayView<FContactClothGpuCapsule> Capsules);
	bool EnqueueStep();
	void RequestReadback();
	void PollReadback();
	bool ConsumeLatestPositions(TArray<FVector3f>& OutPositions, int64& OutStepIndex);
	FContactClothGpuExplicitTopologyRealtimeStats GetStats() const;
	void GetGpuTimingSamples(TArray<int64>& OutSteps, TArray<double>& OutMilliseconds) const;
	void FlushAndWaitForReadbackForValidation();
	void Shutdown();

private:
	TSharedPtr<FContactClothGpuExplicitTopologyRealtimeState, ESPMode::ThreadSafe> State;
};
