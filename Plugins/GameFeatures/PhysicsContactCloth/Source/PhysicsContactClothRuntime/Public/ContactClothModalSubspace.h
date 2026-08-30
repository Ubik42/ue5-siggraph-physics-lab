#pragma once

#include "CoreMinimal.h"

struct FContactClothModalBasis
{
	int32 ParticleCount = 0;
	int32 ModeCount = 0;
	TArray<double> Values;

	bool IsValid() const
	{
		return ParticleCount > 0 && ModeCount > 0 && Values.Num() == ParticleCount * ModeCount;
	}

	double GetValue(int32 ParticleIndex, int32 ModeIndex) const
	{
		return Values[ModeIndex * ParticleCount + ParticleIndex];
	}
};

struct FContactClothReducedSystem
{
	int32 ModeCount = 0;
	TArray<double> Matrix;

	bool IsValid() const
	{
		return ModeCount > 0 && Matrix.Num() == ModeCount * ModeCount;
	}
};

struct FContactClothRankOneProjectionCache
{
	int32 ParticleCount = 0;
	int32 ModeCount = 0;
	TArray<double> Values;

	bool IsValid() const { return ParticleCount > 0 && ModeCount > 0 && Values.Num() == ParticleCount * ModeCount * ModeCount; }
	double GetValue(int32 ParticleIndex, int32 ModeA, int32 ModeB) const
	{
		return Values[(ParticleIndex * ModeCount + ModeA) * ModeCount + ModeB];
	}
};

struct FContactClothModalContact
{
	int32 ParticleIndex = INDEX_NONE;
	FVector3d Normal = FVector3d::UnitZ();
	double Weight = 0.0;
};

struct FContactClothVectorReducedSystem
{
	int32 ModeCount = 0;
	TArray<double> Matrix;
	int32 GetDofCount() const { return ModeCount * 3; }
	bool IsValid() const { return ModeCount > 0 && Matrix.Num() == GetDofCount() * GetDofCount(); }
};

/** CPU reference for the rest-shape low-frequency subspace used by the reduced cloth solve. */
class PHYSICSCONTACTCLOTHRUNTIME_API FContactClothModalSubspace
{
public:
	static bool BuildPinnedGridBasis(
		int32 Columns,
		int32 Rows,
		int32 RequestedModeCount,
		FContactClothModalBasis& OutBasis);

	/** Deterministic low-frequency graph basis with zero support on inverse-mass-fixed particles. */
	static bool BuildExplicitTopologyBasis(
		TConstArrayView<FVector3f> RestPositions,
		TConstArrayView<float> InverseMasses,
		TConstArrayView<FIntPoint> StructuralEdges,
		int32 RequestedModeCount,
		FContactClothModalBasis& OutBasis);

	static bool Project(
		const FContactClothModalBasis& Basis,
		TConstArrayView<double> ParticleValues,
		TArray<double>& OutModalCoordinates);

	static bool Reconstruct(
		const FContactClothModalBasis& Basis,
		TConstArrayView<double> ModalCoordinates,
		TArray<double>& OutParticleValues);

	static double MeasureRelativeRetainedEnergy(
		TConstArrayView<double> InputValues,
		TConstArrayView<double> ReconstructedValues);

	static bool BuildReducedGridSystem(
		const FContactClothModalBasis& Basis,
		int32 Columns,
		int32 Rows,
		double MassCoefficient,
		double StructuralStiffnessCoefficient,
		FContactClothReducedSystem& OutSystem);

	/** U^T (mass I + stiffness L) U for an arbitrary structural-edge graph. */
	static bool BuildReducedExplicitTopologySystem(
		const FContactClothModalBasis& Basis,
		TConstArrayView<float> InverseMasses,
		TConstArrayView<FIntPoint> StructuralEdges,
		double MassCoefficient,
		double StructuralStiffnessCoefficient,
		FContactClothReducedSystem& OutSystem);

	static bool SolveReducedSystem(
		const FContactClothModalBasis& Basis,
		const FContactClothReducedSystem& System,
		TConstArrayView<double> ParticleRhs,
		TArray<double>& OutModalCoordinates,
		TArray<double>& OutParticleSolution);

	static bool InvertReducedSystem(
		const FContactClothReducedSystem& System,
		TArray<double>& OutInverseMatrix);

	static bool BuildRankOneProjectionCache(
		const FContactClothModalBasis& Basis,
		FContactClothRankOneProjectionCache& OutCache);

	static bool AssembleVectorCollisionSystem(
		const FContactClothReducedSystem& BaseSystem,
		const FContactClothRankOneProjectionCache& Cache,
		TConstArrayView<FContactClothModalContact> Contacts,
		FContactClothVectorReducedSystem& OutSystem);

	static bool SolveVectorReducedSystem(
		const FContactClothVectorReducedSystem& System,
		TConstArrayView<double> Rhs,
		TArray<double>& OutSolution);
};
