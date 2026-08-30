#pragma once

#include "CoreMinimal.h"
#include "SoftBodyTetrahedralMesh.h"

struct FSoftBodyLocalXpbdSettings
{
	double FixedDeltaSeconds = 1.0 / 60.0;
	int32 SolverIterations = 64;
	FVector3d GravityCmPerSecondSquared = FVector3d(0.0, 0.0, -980.0);
	double VelocityDamping = 0.98;
	double JacobiRelaxation = 0.85;
	double MaximumVertexCorrectionCm = 2.0;
	double SoftEdgeCompliance = 2.e-8;
	double StiffEdgeCompliance = 2.e-9;
	double SoftVolumeCompliance = 2.e-10;
	double StiffVolumeCompliance = 2.e-11;
	bool bEnableLowFrequencyCorrection = false;
	double LowFrequencyCorrectionStrength = 0.35;
	int32 LowFrequencyCorrectionInterval = 1;
	bool bEnableGroundContact = false;
	double GroundPlaneZCm = 0.0;
	double GroundContactStiffness = 1.0;
};

struct FSoftBodyLocalXpbdStats
{
	int64 StepsExecuted = 0;
	int32 IterationsExecuted = 0;
	int32 DistanceConstraintCount = 0;
	int32 VolumeConstraintCount = 0;
	double ResidualRms = 0.0;
	double MaximumEdgeRelativeError = 0.0;
	double MeanVolumeRelativeError = 0.0;
	double MaximumVolumeRelativeError = 0.0;
	double MeanSoftMaterialVolumeRelativeError = 0.0;
	double MeanStiffMaterialVolumeRelativeError = 0.0;
	double TotalVolumeRatio = 1.0;
	double TipDisplacementCm = 0.0;
	double FixedVertexDriftCm = 0.0;
	double MaximumKinematicTargetErrorCm = 0.0;
	int32 LowFrequencyCorrectionPasses = 0;
	int32 LowFrequencyModeCount = 0;
	double MaximumLowFrequencyCorrectionCm = 0.0;
	int32 GroundContactPassesExecuted = 0;
	double MaximumGroundPenetrationCm = 0.0;
	int32 ActiveGroundContactVertices = 0;
	TArray<double> IterationResidualRms;
};

class PHYSICSSOFTBODYRUNTIME_API FSoftBodyLocalXpbdSolver
{
public:
	bool Initialize(
		const FSoftBodyTetMesh& InMesh,
		const FSoftBodyLocalXpbdSettings& InSettings,
		FString* OutFailureReason = nullptr);
	bool Step(FSoftBodyLocalXpbdStats& OutStats, FString* OutFailureReason = nullptr);
	void Reset();
	bool SetKinematicTarget(int32 VertexIndex, const FVector3d& TargetPosition, FString* OutFailureReason = nullptr);
	void ClearKinematicTargets();

	bool IsInitialized() const { return bInitialized; }
	const TArray<FVector3d>& GetPositions() const { return Positions; }
	const FSoftBodyLocalXpbdStats& GetLatestStats() const { return LatestStats; }

private:
	struct FDistanceConstraint
	{
		int32 A = INDEX_NONE;
		int32 B = INDEX_NONE;
		double RestLengthCm = 0.0;
		uint8 MaterialId = 0;
	};

	bool ValidateSettings(FString* OutFailureReason) const;
	void Measure(FSoftBodyLocalXpbdStats& OutStats) const;
	double CalculateResidualRms() const;
	double EdgeCompliance(uint8 MaterialId) const;
	double VolumeCompliance(uint8 MaterialId) const;
	double EffectiveInverseMass(int32 VertexIndex) const;
	FVector3d EffectiveFixedTarget(int32 VertexIndex) const;
	void BuildLowFrequencyBasis();
	bool ProjectLowFrequencyCorrection(
		const TArray<FVector3d>& LocalCorrections,
		FVector3d& OutLinearCoefficient,
		FVector3d& OutQuadraticCoefficient) const;

	FSoftBodyTetMesh Mesh;
	FSoftBodyLocalXpbdSettings Settings;
	TArray<FVector3d> Positions;
	TArray<FVector3d> PreviousPositions;
	TArray<FDistanceConstraint> DistanceConstraints;
	TArray<double> DistanceLambdas;
	TArray<double> VolumeLambdas;
	TBitArray<> KinematicMask;
	TArray<FVector3d> KinematicTargets;
	TArray<FVector2d> LowFrequencyBasis;
	FSoftBodyLocalXpbdStats LatestStats;
	int64 StepsExecuted = 0;
	bool bInitialized = false;
};
