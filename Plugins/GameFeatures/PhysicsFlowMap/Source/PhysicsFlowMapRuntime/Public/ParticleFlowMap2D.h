#pragma once

#include "CoreMinimal.h"

struct FFlowMapMatrix2f
{
	float M00 = 1.0f;
	float M01 = 0.0f;
	float M10 = 0.0f;
	float M11 = 1.0f;

	float Determinant() const { return M00 * M11 - M01 * M10; }
};

struct FFlowMapGridSettings2D
{
	FIntPoint CellCounts = FIntPoint(32, 16);
	float CellSizeCm = 10.0f;
	FVector2f OriginCm = FVector2f::ZeroVector;
};

struct FFlowMapKinematicCircle2D
{
	FVector2f CenterCm = FVector2f::ZeroVector;
	float RadiusCm = 10.0f;
	FVector2f LinearVelocityCmPerSecond = FVector2f::ZeroVector;
	float AngularVelocityRadiansPerSecond = 0.0f;
};

struct FFlowMapSolidBoundaryRaster2D
{
	FFlowMapGridSettings2D Grid;
	TArray<uint32> SolidCellMask;
	TArray<uint32> SolidFaceUMask;
	TArray<uint32> SolidFaceVMask;
	TArray<float> SolidFaceVelocityU;
	TArray<float> SolidFaceVelocityV;

	int32 CountSolidCells() const;
};

/** Converts UE/Chaos rigid-body poses to a 2D kinematic primitive and rasterizes no-slip MAC data. */
class PHYSICSFLOWMAPRUNTIME_API FFlowMapSolidBoundaryRasterizer2D
{
public:
	static bool BuildCircleFromRigidBodyTransforms(
		const FTransform& PreviousTransform,
		const FTransform& CurrentTransform,
		float RadiusCm,
		float FixedDeltaSeconds,
		FFlowMapKinematicCircle2D& OutCircle,
		FString* OutFailureReason = nullptr);
	static bool RasterizeCircle(
		const FFlowMapGridSettings2D& Grid,
		const FFlowMapKinematicCircle2D& Circle,
		FFlowMapSolidBoundaryRaster2D& OutRaster,
		FString* OutFailureReason = nullptr);
};

/**
 * Deterministic 2D MAC velocity grid used by the Case 03 standards suite.
 * U lives on X faces and V on Y faces; this is solver data, not Niagara state.
 */
struct PHYSICSFLOWMAPRUNTIME_API FFlowMapVelocityGrid2D
{
	FFlowMapGridSettings2D Settings;
	TArray<float> FaceU;
	TArray<float> FaceV;

	bool Initialize(const FFlowMapGridSettings2D& InSettings, FString* OutFailureReason = nullptr);
	bool IsValid(FString* OutFailureReason = nullptr) const;
	FVector2f GetMinimumCm() const { return Settings.OriginCm; }
	FVector2f GetMaximumCm() const;
	FVector2f SampleVelocity(const FVector2f& PositionCm) const;
	FFlowMapMatrix2f SampleVelocityGradient(const FVector2f& PositionCm) const;
	float CalculateCellDivergence(int32 X, int32 Y) const;
	double CalculateDivergenceRms() const;
	float CalculateMaximumAbsoluteDivergence() const;
	double CalculateKineticEnergyProxy() const;
	void SetUniformVelocity(const FVector2f& VelocityCmPerSecond);
	void SetSolidBodyVortex(const FVector2f& CenterCm, float AngularSpeedRadiansPerSecond);
	void SetPeriodicCompressionWave(float SpeedAmplitudeCmPerSecond);
};

struct FParticleFlowMapState2D
{
	FVector2f PositionCm = FVector2f::ZeroVector;
	FVector2f VelocityCmPerSecond = FVector2f::ZeroVector;
	FVector2f LongMapOriginCm = FVector2f::ZeroVector;
	FVector2f ShortMapOriginCm = FVector2f::ZeroVector;
	FFlowMapMatrix2f BackwardMapJacobian;
};

struct FFlowMapPressureProjectionMetrics2D
{
	double DivergenceRmsBefore = 0.0;
	double DivergenceRmsAfter = 0.0;
	float MaximumDivergenceBefore = 0.0f;
	float MaximumDivergenceAfter = 0.0f;
	int32 Iterations = 0;
};

/** CPU truth path for the periodic MAC pressure projection used before the RDG port. */
class PHYSICSFLOWMAPRUNTIME_API FFlowMapPressureProjector2D
{
public:
	static bool ProjectPeriodic(
		FFlowMapVelocityGrid2D& InOutGrid,
		int32 Iterations,
		FFlowMapPressureProjectionMetrics2D& OutMetrics,
		FString* OutFailureReason = nullptr);
};

/** Normalized bilinear PIC transfer. The output uses the same MAC contract as the source solver. */
class PHYSICSFLOWMAPRUNTIME_API FFlowMapParticleGridTransfer2D
{
public:
	static bool TransferPicToGrid(
		const TArray<FParticleFlowMapState2D>& Particles,
		const FFlowMapGridSettings2D& GridSettings,
		FFlowMapVelocityGrid2D& OutGrid,
		FString* OutFailureReason = nullptr);
};

struct FFlowMapScalarParticle2D
{
	FVector2f PositionCm = FVector2f::ZeroVector;
	float Value = 0.0f;
	FVector2f GradientPerCm = FVector2f::ZeroVector;
};

/** Component-wise reference for the paper's gradient-enhanced particle-to-grid transfer. */
class PHYSICSFLOWMAPRUNTIME_API FFlowMapGradientTransfer2D
{
public:
	static bool TransferScalarToPeriodicCellGrid(
		const TArray<FFlowMapScalarParticle2D>& Particles,
		const FFlowMapGridSettings2D& GridSettings,
		bool bUseGradientTerm,
		TArray<float>& OutCellValues,
		FString* OutFailureReason = nullptr);
};

enum class EFlowMapBoundaryMode2D : uint8
{
	Clamp,
	Periodic
};

struct FParticleFlowMapSettings2D
{
	int32 ParticlesPerCellAxis = 2;
	int32 LongMapResetSteps = 12;
	int32 ShortMapResetSteps = 3;
	float MaximumMapDistortion = 0.35f;
	EFlowMapBoundaryMode2D BoundaryMode = EFlowMapBoundaryMode2D::Clamp;
};

struct FParticleFlowMapMetrics2D
{
	int32 StepsExecuted = 0;
	int32 ParticleCount = 0;
	int32 LostParticles = 0;
	int32 BoundaryWraps = 0;
	int32 LongMapResets = 0;
	int32 ShortMapResets = 0;
	float MaximumLongMapDistanceCm = 0.0f;
	float MaximumJacobianDeterminantError = 0.0f;
};

/** Core particle-map lifecycle for the first PFM vertical slice. */
class PHYSICSFLOWMAPRUNTIME_API FParticleFlowMapSimulation2D
{
public:
	bool Initialize(
		const FFlowMapVelocityGrid2D& InVelocityGrid,
		const FParticleFlowMapSettings2D& InSettings,
		FString* OutFailureReason = nullptr);
	bool Step(float DeltaSeconds, FString* OutFailureReason = nullptr);

	const TArray<FParticleFlowMapState2D>& GetParticles() const { return Particles; }
	const FParticleFlowMapMetrics2D& GetMetrics() const { return Metrics; }
	const FFlowMapVelocityGrid2D& GetVelocityGrid() const { return VelocityGrid; }

private:
	void ResetLongMap();
	void ResetShortMap();

	FFlowMapVelocityGrid2D VelocityGrid;
	FParticleFlowMapSettings2D Settings;
	TArray<FParticleFlowMapState2D> Particles;
	FParticleFlowMapMetrics2D Metrics;
};
