#pragma once

#include "CoreMinimal.h"
#include "ParticleFlowMap2D.h"

enum class EFlowMapImpulseMode2D : uint8
{
	ShortPic,
	LongMapPic,
	LongMapGradient
};

struct FFlowMapImpulseVortexSettings2D
{
	FFlowMapGridSettings2D Grid;
	int32 ParticlesPerCellAxis = 2;
	float FixedDeltaSeconds = 1.0f / 240.0f;
	int32 LongMapResetSteps = 20;
	int32 PressureIterations = 128;
	float VortexAmplitudeCmPerSecond = 20.0f;
};

struct FFlowMapImpulseVortexMetrics2D
{
	int32 StepsExecuted = 0;
	int32 ParticleCount = 0;
	int32 MapResets = 0;
	int32 PressureProjections = 0;
	double VelocityRmsErrorCmPerSecond = 0.0;
	double RelativeVelocityRmsError = 0.0;
	double DivergenceRms = 0.0;
	float MaximumAbsoluteDivergence = 0.0f;
	double KineticEnergyProxy = 0.0;
	double InitialKineticEnergyProxy = 0.0;
	float MaximumJacobianDeterminantError = 0.0f;
};

/** Particle impulse + backward-map Jacobian + MAC pressure projection CPU truth loop. */
class PHYSICSFLOWMAPRUNTIME_API FFlowMapImpulseVortexSimulation2D
{
public:
	bool Initialize(
		const FFlowMapImpulseVortexSettings2D& InSettings,
		EFlowMapImpulseMode2D InMode,
		FString* OutFailureReason = nullptr);
	bool Step(FString* OutFailureReason = nullptr);

	const FFlowMapImpulseVortexMetrics2D& GetMetrics() const { return Metrics; }
	const FFlowMapVelocityGrid2D& GetVelocityGrid() const { return VelocityGrid; }

private:
	struct FImpulseParticle
	{
		FVector2f PositionCm = FVector2f::ZeroVector;
		FVector2f ReferenceImpulseCmPerSecond = FVector2f::ZeroVector;
		FFlowMapMatrix2f ReferenceImpulseGradientPerSecond;
		FFlowMapMatrix2f BackwardMapJacobian;
	};

	FVector2f EvaluateTaylorGreenVelocity(const FVector2f& PositionCm) const;
	FFlowMapMatrix2f EvaluateTaylorGreenVelocityGradient(const FVector2f& PositionCm) const;
	FVector2f SamplePeriodicVelocity(const FVector2f& PositionCm) const;
	FFlowMapMatrix2f SamplePeriodicVelocityGradient(const FVector2f& PositionCm) const;
	bool DepositCurrentImpulse(FString* OutFailureReason);
	void ResetParticleMaps();
	void UpdateMetrics();

	FFlowMapImpulseVortexSettings2D Settings;
	EFlowMapImpulseMode2D Mode = EFlowMapImpulseMode2D::ShortPic;
	FFlowMapVelocityGrid2D VelocityGrid;
	TArray<FImpulseParticle> Particles;
	FFlowMapImpulseVortexMetrics2D Metrics;
};
