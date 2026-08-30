#pragma once

#include "CoreMinimal.h"

struct FContactClothGpuSphere
{
	FVector3f Center = FVector3f::ZeroVector;
	float RadiusCm = 0.0f;
};

struct FContactClothGpuCapsule
{
	FVector3f Start = FVector3f::ZeroVector;
	FVector3f End = FVector3f(0.0f, 0.0f, 1.0f);
	float RadiusCm = 0.0f;
};

struct FContactClothGpuJacobiSettings
{
	int32 Columns = 16;
	int32 Rows = 12;
	float SpacingCm = 12.0f;
	int32 Iterations = 64;
	float Relaxation = 0.9f;
	FVector3f SphereCenter = FVector3f::ZeroVector;
	float SphereRadiusCm = 0.0f;
	FVector3f CapsuleStart = FVector3f::ZeroVector;
	FVector3f CapsuleEnd = FVector3f(0.0f, 0.0f, 1.0f);
	float CapsuleRadiusCm = 0.0f;
	float CollisionThicknessCm = 1.0f;
	TArray<FContactClothGpuSphere> CollisionSpheres;
	TArray<FContactClothGpuCapsule> CollisionCapsules;
};

struct FContactClothGpuJacobiResult
{
	bool bDispatched = false;
	bool bReadbackSucceeded = false;
	TArray<FVector3f> Positions;
	double InputMaximumEdgeErrorCm = 0.0;
	double OutputMaximumEdgeErrorCm = 0.0;
	double InputMaximumCapsulePenetrationCm = 0.0;
	double OutputMaximumCapsulePenetrationCm = 0.0;
	double BlockingWallMilliseconds = 0.0;
	FString FailureReason;
};

/**
 * Blocking validation dispatch for the first GPU gate. It intentionally stalls for readback so CPU/GPU error can be compared.
 * Runtime cloth will replace this with persistent buffers and asynchronous metrics once the kernel contract is proven.
 */
class PHYSICSRESEARCHSHADERS_API FContactClothGpuJacobi
{
public:
	static FContactClothGpuJacobiResult RunBlocking(
		TConstArrayView<FVector3f> InputPositions,
		const FContactClothGpuJacobiSettings& Settings);

	static double MeasureMaximumStructuralEdgeErrorCm(
		TConstArrayView<FVector3f> Positions,
		int32 Columns,
		int32 Rows,
		float RestSpacingCm);

	static double MeasureMaximumCapsulePenetrationCm(
		TConstArrayView<FVector3f> Positions,
		const FVector3f& CapsuleStart,
		const FVector3f& CapsuleEnd,
		float RadiusWithThicknessCm);
};

struct FContactClothGpuRealtimeSettings : public FContactClothGpuJacobiSettings
{
	float FixedDeltaSeconds = 1.0f / 60.0f;
	FVector3f GravityCmPerSecondSquared = FVector3f(0.0f, 0.0f, -980.0f);
	float VelocityDamping = 0.995f;
	int32 ReadbackIntervalSteps = 4;
	int32 ModalModeCount = 0;
	float ModalCorrectionStrength = 0.0f;
	TArray<float> ModalBasisValues;
	TArray<float> ModalInverseSystemValues;
	bool bEnableRankOneCollisionHessian = false;
	TArray<float> ModalSystemValues;
	TArray<float> ModalParticleOuterProductValues;
	float RankOneMaximumCorrectionCm = 4.0f;
	bool bEnableNonDistanceBarrier = false;
	float NdbBaseWeight = 1.0f;
	float NdbActivationMultiplier = 2.0f;
	float NdbMaximumWeight = 1048576.0f;
	int32 CollisionPrimitiveCapacity = 0;
	bool bEnableCompleteCcd = false;
	float CcdSafetyOffsetCm = 0.01f;
	bool bEnableResidualReuse = false;
	float ResidualForwardScale = 0.4f;
	float MaximumResidualCm = 2.0f;
	float MaximumResidualCcdCorrectionCm = 0.25f;
};

struct FContactClothGpuRealtimeStats
{
	bool bInitialized = false;
	bool bReadbackPending = false;
	int64 StepsEnqueued = 0;
	int64 StepsExecuted = 0;
	int64 ReadbacksCompleted = 0;
	int64 LatestReadbackStep = 0;
	int64 ModalCorrectionsExecuted = 0;
	int64 RankOneCollisionSystemsExecuted = 0;
	int64 NdbStateUpdatesExecuted = 0;
	int64 CompleteCcdPassesExecuted = 0;
	int64 ResidualReusePassesExecuted = 0;
	int64 ResidualFallbackEvents = 0;
	float MaximumForwardedResidualCm = 0.0f;
	int32 LatestActiveContactPairs = 0;
	int32 MaximumActiveContactPairs = 0;
	int32 LatestMaximumNdbActivationCount = 0;
	int32 MaximumNdbActivationCount = 0;
	double LatestRenderSubmissionMilliseconds = 0.0;
	double TotalRenderSubmissionMilliseconds = 0.0;
	double MaximumRenderSubmissionMilliseconds = 0.0;
};

struct FContactClothGpuModalContact
{
	int32 ParticleIndex = INDEX_NONE;
	FVector3f Normal = FVector3f(0.0f, 0.0f, 1.0f);
	float Weight = 0.0f;
};

struct FContactClothGpuRankOneAssemblyResult
{
	bool bDispatched = false;
	bool bReadbackSucceeded = false;
	int32 ModeCount = 0;
	TArray<float> Matrix;
	FString FailureReason;
};

/** Blocking D3D validation path for the full 3m x 3m rank-one collision matrix. */
class PHYSICSRESEARCHSHADERS_API FContactClothGpuRankOneAssembly
{
public:
	static FContactClothGpuRankOneAssemblyResult RunBlocking(
		int32 ParticleCount,
		int32 ModeCount,
		TConstArrayView<float> BaseReducedSystem,
		TConstArrayView<float> ParticleOuterProducts,
		TConstArrayView<FContactClothGpuModalContact> Contacts);
};

class FContactClothGpuRealtimeState;

/**
 * Persistent-buffer, non-blocking GPU cloth session. The runtime path never waits for the GPU.
 * Call PollReadback once per game frame and consume the latest completed snapshot when available.
 */
class PHYSICSRESEARCHSHADERS_API FContactClothGpuRealtimeSimulation
{
public:
	FContactClothGpuRealtimeSimulation();
	~FContactClothGpuRealtimeSimulation();

	bool Initialize(TConstArrayView<FVector3f> InitialPositions, const FContactClothGpuRealtimeSettings& Settings);
	void UpdateCollisionPrimitives(
		TConstArrayView<FContactClothGpuSphere> Spheres,
		TConstArrayView<FContactClothGpuCapsule> Capsules);
	bool EnqueueStep();
	void RequestReadback();
	void PollReadback();
	bool ConsumeLatestPositions(TArray<FVector3f>& OutPositions, int64& OutStepIndex);
	FContactClothGpuRealtimeStats GetStats() const;
	void FlushAndWaitForReadbackForValidation();
	void Shutdown();

private:
	TSharedPtr<FContactClothGpuRealtimeState, ESPMode::ThreadSafe> State;
};
