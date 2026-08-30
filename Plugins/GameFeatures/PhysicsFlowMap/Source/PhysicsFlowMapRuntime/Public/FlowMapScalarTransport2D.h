#pragma once

#include "CoreMinimal.h"
#include "ParticleFlowMap2D.h"

enum class EFlowMapScalarTransportMode2D : uint8
{
	ShortPic,
	LongMapPic,
	LongMapGradient
};

struct FFlowMapScalarTransportSettings2D
{
	FFlowMapGridSettings2D Grid;
	int32 ParticlesPerCellAxis = 2;
	FVector2f UniformVelocityCmPerSecond = FVector2f(20.0f, 10.0f);
	float FixedDeltaSeconds = 1.0f / 60.0f;
	int32 LongMapResetSteps = 20;
	int32 FieldCyclesX = 2;
	int32 FieldCyclesY = 2;
};

struct FFlowMapScalarTransportMetrics2D
{
	int32 StepsExecuted = 0;
	int32 ParticleCount = 0;
	int32 MapResets = 0;
	double RmsError = 0.0;
	double RelativeRmsError = 0.0;
	float MaximumAbsoluteError = 0.0f;
	float ScalarAmplitude = 0.0f;
	float ExactAmplitude = 0.0f;
	double ScalarEnergyProxy = 0.0;
	double ExactEnergyProxy = 0.0;
};

/**
 * Periodic passive-scalar truth slice. Each scalar component follows the same
 * transfer mathematics as a particle impulse component in the PFM paper.
 */
class PHYSICSFLOWMAPRUNTIME_API FFlowMapScalarTransportSimulation2D
{
public:
	bool Initialize(
		const FFlowMapScalarTransportSettings2D& InSettings,
		EFlowMapScalarTransportMode2D InMode,
		FString* OutFailureReason = nullptr);
	bool Step(FString* OutFailureReason = nullptr);

	const FFlowMapScalarTransportMetrics2D& GetMetrics() const { return Metrics; }
	const TArray<float>& GetCellScalar() const { return CellScalar; }
	const TArray<FFlowMapScalarParticle2D>& GetParticles() const { return Particles; }

private:
	float EvaluateExact(const FVector2f& PositionCm, float TimeSeconds) const;
	FVector2f EvaluateExactGradient(const FVector2f& PositionCm, float TimeSeconds) const;
	float SamplePeriodicScalar(const FVector2f& PositionCm) const;
	FVector2f SamplePeriodicGradient(const FVector2f& PositionCm) const;
	void UpdateMetrics();
	void ResampleParticlesFromGrid();

	FFlowMapScalarTransportSettings2D Settings;
	EFlowMapScalarTransportMode2D Mode = EFlowMapScalarTransportMode2D::ShortPic;
	TArray<FFlowMapScalarParticle2D> Particles;
	TArray<float> CellScalar;
	FFlowMapScalarTransportMetrics2D Metrics;
};
