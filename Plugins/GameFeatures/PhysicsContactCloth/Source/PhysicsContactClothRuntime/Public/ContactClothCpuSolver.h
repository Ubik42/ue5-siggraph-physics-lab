#pragma once

#include "CoreMinimal.h"

struct FContactClothCpuSettings
{
	int32 Columns = 12;
	int32 Rows = 9;
	double SpacingCm = 18.0;
	double ParticleMass = 1.0;
	FVector3d GravityCmPerSecondSquared = FVector3d(0.0, 0.0, -980.0);
	double VelocityDamping = 0.995;
	double StretchWeight = 60000.0;
	double ShearWeight = 30000.0;
	double BendWeight = 3500.0;
	int32 LocalGlobalIterations = 8;
	int32 GlobalGaussSeidelSweeps = 10;
	double CollisionThicknessCm = 1.0;
	bool bPinTopEdge = true;
};

struct FContactClothSphereCollider
{
	FVector3d Center = FVector3d::ZeroVector;
	double RadiusCm = 50.0;
};

struct FContactClothCpuParticle
{
	FVector3d Position = FVector3d::ZeroVector;
	FVector3d PreviousPosition = FVector3d::ZeroVector;
	FVector3d Velocity = FVector3d::ZeroVector;
	FVector3d PinnedPosition = FVector3d::ZeroVector;
	double InverseMass = 1.0;

	bool IsPinned() const { return InverseMass <= 0.0; }
};

struct FContactClothCpuConstraint
{
	int32 ParticleA = INDEX_NONE;
	int32 ParticleB = INDEX_NONE;
	double RestLengthCm = 0.0;
	double Weight = 0.0;
};

struct FContactClothCpuStepStats
{
	double MaximumStretchRatioError = 0.0;
	double MeanStretchRatioError = 0.0;
	double MaximumCollisionCorrectionCm = 0.0;
	double MaximumRemainingPenetrationCm = 0.0;
	int32 CollisionProjectionCount = 0;
};

/**
 * Small deterministic local-global Projective Dynamics reference.
 * It deliberately favors readable, testable CPU math over runtime scale; the GPU backend will own production-sized grids.
 */
class PHYSICSCONTACTCLOTHRUNTIME_API FContactClothCpuSolver
{
public:
	void InitializeGrid(const FContactClothCpuSettings& InSettings, const FVector3d& Origin);
	FContactClothCpuStepStats Step(double DeltaSeconds, TConstArrayView<FContactClothSphereCollider> SphereColliders);
	void Reset();

	const FContactClothCpuSettings& GetSettings() const { return Settings; }
	const TArray<FContactClothCpuParticle>& GetParticles() const { return Particles; }
	const TArray<FContactClothCpuConstraint>& GetConstraints() const { return Constraints; }
	int32 GetParticleIndex(int32 Column, int32 Row) const;
	bool IsInitialized() const { return Particles.Num() > 0; }

private:
	void AddConstraint(int32 ParticleA, int32 ParticleB, double Weight);
	void BuildConstraintGraph();
	void ProjectCollisions(TArray<FVector3d>& Positions, TConstArrayView<FContactClothSphereCollider> SphereColliders, FContactClothCpuStepStats& Stats) const;
	FContactClothCpuStepStats MeasureFinalStats(TConstArrayView<FContactClothSphereCollider> SphereColliders, const FContactClothCpuStepStats& ProjectionStats) const;

	FContactClothCpuSettings Settings;
	TArray<FContactClothCpuParticle> Particles;
	TArray<FContactClothCpuConstraint> Constraints;
	TArray<TArray<int32>> IncidentConstraintIndices;
	TArray<FVector3d> RestPositions;
};
