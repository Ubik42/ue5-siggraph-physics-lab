#include "ContactClothModalSubspace.h"

namespace ContactClothModalSubspace
{
struct FFrequencyPair
{
	int32 Horizontal = 0;
	int32 Vertical = 0;
	double EigenvalueProxy = 0.0;
};
}

bool FContactClothModalSubspace::BuildPinnedGridBasis(
	int32 Columns,
	int32 Rows,
	int32 RequestedModeCount,
	FContactClothModalBasis& OutBasis)
{
	OutBasis = FContactClothModalBasis();
	if (Columns < 2 || Rows < 2 || RequestedModeCount < 1)
	{
		return false;
	}

	TArray<ContactClothModalSubspace::FFrequencyPair> Frequencies;
	for (int32 Vertical = 0; Vertical < Rows - 1; ++Vertical)
	{
		for (int32 Horizontal = 0; Horizontal < Columns; ++Horizontal)
		{
			const double HorizontalFrequency = static_cast<double>(Horizontal) / Columns;
			const double VerticalFrequency = (Vertical + 0.5) / Rows;
			Frequencies.Add({Horizontal, Vertical, FMath::Square(HorizontalFrequency) + FMath::Square(VerticalFrequency)});
		}
	}
	Frequencies.Sort([](const ContactClothModalSubspace::FFrequencyPair& A, const ContactClothModalSubspace::FFrequencyPair& B)
	{
		if (!FMath::IsNearlyEqual(A.EigenvalueProxy, B.EigenvalueProxy))
		{
			return A.EigenvalueProxy < B.EigenvalueProxy;
		}
		return A.Vertical == B.Vertical ? A.Horizontal < B.Horizontal : A.Vertical < B.Vertical;
	});

	const int32 ParticleCount = Columns * Rows;
	const int32 TargetModeCount = FMath::Min(RequestedModeCount, Frequencies.Num());
	OutBasis.ParticleCount = ParticleCount;
	OutBasis.Values.Reserve(ParticleCount * TargetModeCount);
	TArray<double> Candidate;
	Candidate.SetNumUninitialized(ParticleCount);
	for (const ContactClothModalSubspace::FFrequencyPair& Frequency : Frequencies)
	{
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			for (int32 Column = 0; Column < Columns; ++Column)
			{
				const int32 Index = Row * Columns + Column;
				const double HorizontalShape = FMath::Cos(PI * Frequency.Horizontal * (Column + 0.5) / Columns);
				const double VerticalShape = FMath::Sin(PI * (Frequency.Vertical + 0.5) * Row / Rows);
				Candidate[Index] = HorizontalShape * VerticalShape;
			}
		}

		for (int32 ExistingMode = 0; ExistingMode < OutBasis.ModeCount; ++ExistingMode)
		{
			double Dot = 0.0;
			for (int32 Particle = 0; Particle < ParticleCount; ++Particle)
			{
				Dot += Candidate[Particle] * OutBasis.Values[ExistingMode * ParticleCount + Particle];
			}
			for (int32 Particle = 0; Particle < ParticleCount; ++Particle)
			{
				Candidate[Particle] -= Dot * OutBasis.Values[ExistingMode * ParticleCount + Particle];
			}
		}

		double SquaredNorm = 0.0;
		for (double Value : Candidate)
		{
			SquaredNorm += Value * Value;
		}
		if (SquaredNorm <= UE_DOUBLE_SMALL_NUMBER)
		{
			continue;
		}
		const double InverseNorm = 1.0 / FMath::Sqrt(SquaredNorm);
		for (double Value : Candidate)
		{
			OutBasis.Values.Add(Value * InverseNorm);
		}
		OutBasis.ModeCount++;
		if (OutBasis.ModeCount == TargetModeCount)
		{
			break;
		}
	}
	return OutBasis.IsValid() && OutBasis.ModeCount == TargetModeCount;
}

bool FContactClothModalSubspace::BuildExplicitTopologyBasis(
	TConstArrayView<FVector3f> RestPositions,
	TConstArrayView<float> InverseMasses,
	TConstArrayView<FIntPoint> StructuralEdges,
	int32 RequestedModeCount,
	FContactClothModalBasis& OutBasis)
{
	OutBasis = FContactClothModalBasis();
	const int32 ParticleCount = RestPositions.Num();
	if (ParticleCount < 2 || InverseMasses.Num() != ParticleCount || StructuralEdges.IsEmpty() || RequestedModeCount < 1)
	{
		return false;
	}
	TArray<TArray<int32>> Neighbors;
	Neighbors.SetNum(ParticleCount);
	int32 DynamicCount = 0;
	for (int32 Particle = 0; Particle < ParticleCount; ++Particle)
	{
		if (!FMath::IsFinite(InverseMasses[Particle]) || InverseMasses[Particle] < 0.0f) return false;
		DynamicCount += InverseMasses[Particle] > 0.0f ? 1 : 0;
	}
	for (const FIntPoint& Edge : StructuralEdges)
	{
		if (Edge.X < 0 || Edge.Y < 0 || Edge.X >= ParticleCount || Edge.Y >= ParticleCount || Edge.X == Edge.Y) return false;
		Neighbors[Edge.X].Add(Edge.Y);
		Neighbors[Edge.Y].Add(Edge.X);
	}
	const int32 TargetModeCount = FMath::Min(RequestedModeCount, DynamicCount);
	if (TargetModeCount < 1) return false;

	FBox3f Bounds(EForceInit::ForceInit);
	for (const FVector3f& Position : RestPositions) Bounds += Position;
	const FVector3f Extent = Bounds.GetSize().ComponentMax(FVector3f(1.0f));
	TArray<FVector3d> Normalized;
	Normalized.Reserve(ParticleCount);
	for (const FVector3f& Position : RestPositions)
	{
		Normalized.Add(FVector3d((Position.X - Bounds.Min.X) / Extent.X, (Position.Y - Bounds.Min.Y) / Extent.Y,
			(Position.Z - Bounds.Min.Z) / Extent.Z));
	}

	OutBasis.ParticleCount = ParticleCount;
	TArray<double> Candidate;
	TArray<double> Smoothed;
	Candidate.SetNumZeroed(ParticleCount);
	Smoothed.SetNumZeroed(ParticleCount);
	for (int32 Seed = 0; Seed < TargetModeCount * 8 && OutBasis.ModeCount < TargetModeCount; ++Seed)
	{
		const int32 Frequency = Seed / 4 + 1;
		for (int32 Particle = 0; Particle < ParticleCount; ++Particle)
		{
			if (InverseMasses[Particle] <= 0.0f)
			{
				Candidate[Particle] = 0.0;
				continue;
			}
			const FVector3d& P = Normalized[Particle];
			const double Phase = Frequency * (0.754877666 * P.X + 0.569840296 * P.Y + 0.438289617 * P.Z)
				+ 0.618033989 * (Seed % 4) + 0.000001 * Particle;
			Candidate[Particle] = Seed == 0 ? 1.0 : FMath::Sin(2.0 * PI * Phase);
		}
		for (int32 Iteration = 0; Iteration < 48; ++Iteration)
		{
			for (int32 Particle = 0; Particle < ParticleCount; ++Particle)
			{
				if (InverseMasses[Particle] <= 0.0f || Neighbors[Particle].IsEmpty())
				{
					Smoothed[Particle] = 0.0;
					continue;
				}
				double NeighborAverage = 0.0;
				for (int32 Neighbor : Neighbors[Particle]) NeighborAverage += Candidate[Neighbor];
				NeighborAverage /= Neighbors[Particle].Num();
				Smoothed[Particle] = 0.2 * Candidate[Particle] + 0.8 * NeighborAverage;
			}
			Swap(Candidate, Smoothed);
			for (int32 ExistingMode = 0; ExistingMode < OutBasis.ModeCount; ++ExistingMode)
			{
				double Dot = 0.0;
				for (int32 Particle = 0; Particle < ParticleCount; ++Particle)
					Dot += Candidate[Particle] * OutBasis.GetValue(Particle, ExistingMode);
				for (int32 Particle = 0; Particle < ParticleCount; ++Particle)
					Candidate[Particle] -= Dot * OutBasis.GetValue(Particle, ExistingMode);
			}
			double SquaredNorm = 0.0;
			for (double Value : Candidate) SquaredNorm += Value * Value;
			if (SquaredNorm > UE_DOUBLE_SMALL_NUMBER)
			{
				const double InverseNorm = 1.0 / FMath::Sqrt(SquaredNorm);
				for (double& Value : Candidate) Value *= InverseNorm;
			}
		}
		double SquaredNorm = 0.0;
		for (double Value : Candidate) SquaredNorm += Value * Value;
		if (SquaredNorm < 1.e-12) continue;
		const double InverseNorm = 1.0 / FMath::Sqrt(SquaredNorm);
		for (double Value : Candidate) OutBasis.Values.Add(Value * InverseNorm);
		OutBasis.ModeCount++;
	}
	return OutBasis.IsValid() && OutBasis.ModeCount == TargetModeCount;
}

bool FContactClothModalSubspace::Project(
	const FContactClothModalBasis& Basis,
	TConstArrayView<double> ParticleValues,
	TArray<double>& OutModalCoordinates)
{
	if (!Basis.IsValid() || ParticleValues.Num() != Basis.ParticleCount)
	{
		return false;
	}
	OutModalCoordinates.SetNumZeroed(Basis.ModeCount);
	for (int32 Mode = 0; Mode < Basis.ModeCount; ++Mode)
	{
		for (int32 Particle = 0; Particle < Basis.ParticleCount; ++Particle)
		{
			OutModalCoordinates[Mode] += Basis.GetValue(Particle, Mode) * ParticleValues[Particle];
		}
	}
	return true;
}

bool FContactClothModalSubspace::Reconstruct(
	const FContactClothModalBasis& Basis,
	TConstArrayView<double> ModalCoordinates,
	TArray<double>& OutParticleValues)
{
	if (!Basis.IsValid() || ModalCoordinates.Num() != Basis.ModeCount)
	{
		return false;
	}
	OutParticleValues.SetNumZeroed(Basis.ParticleCount);
	for (int32 Mode = 0; Mode < Basis.ModeCount; ++Mode)
	{
		for (int32 Particle = 0; Particle < Basis.ParticleCount; ++Particle)
		{
			OutParticleValues[Particle] += Basis.GetValue(Particle, Mode) * ModalCoordinates[Mode];
		}
	}
	return true;
}

double FContactClothModalSubspace::MeasureRelativeRetainedEnergy(
	TConstArrayView<double> InputValues,
	TConstArrayView<double> ReconstructedValues)
{
	if (InputValues.Num() != ReconstructedValues.Num() || InputValues.IsEmpty())
	{
		return 0.0;
	}
	double InputEnergy = 0.0;
	double ReconstructedEnergy = 0.0;
	for (int32 Index = 0; Index < InputValues.Num(); ++Index)
	{
		InputEnergy += InputValues[Index] * InputValues[Index];
		ReconstructedEnergy += ReconstructedValues[Index] * ReconstructedValues[Index];
	}
	return InputEnergy > UE_DOUBLE_SMALL_NUMBER ? ReconstructedEnergy / InputEnergy : 0.0;
}

bool FContactClothModalSubspace::BuildReducedGridSystem(
	const FContactClothModalBasis& Basis,
	int32 Columns,
	int32 Rows,
	double MassCoefficient,
	double StructuralStiffnessCoefficient,
	FContactClothReducedSystem& OutSystem)
{
	OutSystem = FContactClothReducedSystem();
	if (!Basis.IsValid() || Columns * Rows != Basis.ParticleCount
		|| MassCoefficient <= 0.0 || StructuralStiffnessCoefficient < 0.0)
	{
		return false;
	}
	OutSystem.ModeCount = Basis.ModeCount;
	OutSystem.Matrix.SetNumZeroed(Basis.ModeCount * Basis.ModeCount);
	for (int32 ModeA = 0; ModeA < Basis.ModeCount; ++ModeA)
	{
		for (int32 ModeB = ModeA; ModeB < Basis.ModeCount; ++ModeB)
		{
			double Value = 0.0;
			for (int32 Particle = 0; Particle < Basis.ParticleCount; ++Particle)
			{
				Value += MassCoefficient * Basis.GetValue(Particle, ModeA) * Basis.GetValue(Particle, ModeB);
			}
			for (int32 Row = 0; Row < Rows; ++Row)
			{
				for (int32 Column = 0; Column < Columns; ++Column)
				{
					const int32 Index = Row * Columns + Column;
					if (Column + 1 < Columns)
					{
						const int32 Neighbor = Index + 1;
						Value += StructuralStiffnessCoefficient
							* (Basis.GetValue(Index, ModeA) - Basis.GetValue(Neighbor, ModeA))
							* (Basis.GetValue(Index, ModeB) - Basis.GetValue(Neighbor, ModeB));
					}
					if (Row + 1 < Rows)
					{
						const int32 Neighbor = Index + Columns;
						Value += StructuralStiffnessCoefficient
							* (Basis.GetValue(Index, ModeA) - Basis.GetValue(Neighbor, ModeA))
							* (Basis.GetValue(Index, ModeB) - Basis.GetValue(Neighbor, ModeB));
					}
				}
			}
			OutSystem.Matrix[ModeA * Basis.ModeCount + ModeB] = Value;
			OutSystem.Matrix[ModeB * Basis.ModeCount + ModeA] = Value;
		}
	}
	return OutSystem.IsValid();
}

bool FContactClothModalSubspace::BuildReducedExplicitTopologySystem(
	const FContactClothModalBasis& Basis,
	TConstArrayView<float> InverseMasses,
	TConstArrayView<FIntPoint> StructuralEdges,
	double MassCoefficient,
	double StructuralStiffnessCoefficient,
	FContactClothReducedSystem& OutSystem)
{
	OutSystem = FContactClothReducedSystem();
	if (!Basis.IsValid() || InverseMasses.Num() != Basis.ParticleCount || StructuralEdges.IsEmpty()
		|| !FMath::IsFinite(MassCoefficient) || MassCoefficient <= 0.0
		|| !FMath::IsFinite(StructuralStiffnessCoefficient) || StructuralStiffnessCoefficient < 0.0)
	{
		return false;
	}
	for (const FIntPoint& Edge : StructuralEdges)
	{
		if (Edge.X < 0 || Edge.Y < 0 || Edge.X >= Basis.ParticleCount || Edge.Y >= Basis.ParticleCount || Edge.X == Edge.Y) return false;
	}
	OutSystem.ModeCount = Basis.ModeCount;
	OutSystem.Matrix.SetNumZeroed(Basis.ModeCount * Basis.ModeCount);
	for (int32 ModeA = 0; ModeA < Basis.ModeCount; ++ModeA)
	{
		for (int32 ModeB = ModeA; ModeB < Basis.ModeCount; ++ModeB)
		{
			double Value = 0.0;
			for (int32 Particle = 0; Particle < Basis.ParticleCount; ++Particle)
			{
				if (InverseMasses[Particle] > 0.0f)
					Value += MassCoefficient * Basis.GetValue(Particle, ModeA) * Basis.GetValue(Particle, ModeB);
			}
			for (const FIntPoint& Edge : StructuralEdges)
			{
				const double DifferenceA = Basis.GetValue(Edge.X, ModeA) - Basis.GetValue(Edge.Y, ModeA);
				const double DifferenceB = Basis.GetValue(Edge.X, ModeB) - Basis.GetValue(Edge.Y, ModeB);
				Value += StructuralStiffnessCoefficient * DifferenceA * DifferenceB;
			}
			OutSystem.Matrix[ModeA * Basis.ModeCount + ModeB] = Value;
			OutSystem.Matrix[ModeB * Basis.ModeCount + ModeA] = Value;
		}
	}
	return OutSystem.IsValid();
}

bool FContactClothModalSubspace::SolveReducedSystem(
	const FContactClothModalBasis& Basis,
	const FContactClothReducedSystem& System,
	TConstArrayView<double> ParticleRhs,
	TArray<double>& OutModalCoordinates,
	TArray<double>& OutParticleSolution)
{
	if (!Basis.IsValid() || !System.IsValid() || System.ModeCount != Basis.ModeCount
		|| ParticleRhs.Num() != Basis.ParticleCount)
	{
		return false;
	}
	TArray<double> ReducedRhs;
	if (!Project(Basis, ParticleRhs, ReducedRhs))
	{
		return false;
	}

	const int32 N = System.ModeCount;
	TArray<double> Cholesky;
	Cholesky.SetNumZeroed(N * N);
	for (int32 Row = 0; Row < N; ++Row)
	{
		for (int32 Column = 0; Column <= Row; ++Column)
		{
			double Sum = System.Matrix[Row * N + Column];
			for (int32 Inner = 0; Inner < Column; ++Inner)
			{
				Sum -= Cholesky[Row * N + Inner] * Cholesky[Column * N + Inner];
			}
			if (Row == Column)
			{
				if (Sum <= UE_DOUBLE_SMALL_NUMBER)
				{
					return false;
				}
				Cholesky[Row * N + Column] = FMath::Sqrt(Sum);
			}
			else
			{
				Cholesky[Row * N + Column] = Sum / Cholesky[Column * N + Column];
			}
		}
	}

	TArray<double> Intermediate;
	Intermediate.SetNumZeroed(N);
	for (int32 Row = 0; Row < N; ++Row)
	{
		double Sum = ReducedRhs[Row];
		for (int32 Column = 0; Column < Row; ++Column)
		{
			Sum -= Cholesky[Row * N + Column] * Intermediate[Column];
		}
		Intermediate[Row] = Sum / Cholesky[Row * N + Row];
	}
	OutModalCoordinates.SetNumZeroed(N);
	for (int32 Row = N - 1; Row >= 0; --Row)
	{
		double Sum = Intermediate[Row];
		for (int32 Column = Row + 1; Column < N; ++Column)
		{
			Sum -= Cholesky[Column * N + Row] * OutModalCoordinates[Column];
		}
		OutModalCoordinates[Row] = Sum / Cholesky[Row * N + Row];
	}
	return Reconstruct(Basis, OutModalCoordinates, OutParticleSolution);
}

bool FContactClothModalSubspace::InvertReducedSystem(
	const FContactClothReducedSystem& System,
	TArray<double>& OutInverseMatrix)
{
	OutInverseMatrix.Reset();
	if (!System.IsValid())
	{
		return false;
	}
	const int32 N = System.ModeCount;
	TArray<double> Cholesky;
	Cholesky.SetNumZeroed(N * N);
	for (int32 Row = 0; Row < N; ++Row)
	{
		for (int32 Column = 0; Column <= Row; ++Column)
		{
			double Sum = System.Matrix[Row * N + Column];
			for (int32 Inner = 0; Inner < Column; ++Inner)
			{
				Sum -= Cholesky[Row * N + Inner] * Cholesky[Column * N + Inner];
			}
			if (Row == Column)
			{
				if (Sum <= UE_DOUBLE_SMALL_NUMBER)
				{
					return false;
				}
				Cholesky[Row * N + Column] = FMath::Sqrt(Sum);
			}
			else
			{
				Cholesky[Row * N + Column] = Sum / Cholesky[Column * N + Column];
			}
		}
	}

	OutInverseMatrix.SetNumZeroed(N * N);
	TArray<double> Intermediate;
	TArray<double> Solution;
	Intermediate.SetNumZeroed(N);
	Solution.SetNumZeroed(N);
	for (int32 InverseColumn = 0; InverseColumn < N; ++InverseColumn)
	{
		for (int32 Row = 0; Row < N; ++Row)
		{
			double Sum = Row == InverseColumn ? 1.0 : 0.0;
			for (int32 Column = 0; Column < Row; ++Column)
			{
				Sum -= Cholesky[Row * N + Column] * Intermediate[Column];
			}
			Intermediate[Row] = Sum / Cholesky[Row * N + Row];
		}
		for (int32 Row = N - 1; Row >= 0; --Row)
		{
			double Sum = Intermediate[Row];
			for (int32 Column = Row + 1; Column < N; ++Column)
			{
				Sum -= Cholesky[Column * N + Row] * Solution[Column];
			}
			Solution[Row] = Sum / Cholesky[Row * N + Row];
		}
		for (int32 Row = 0; Row < N; ++Row)
		{
			OutInverseMatrix[Row * N + InverseColumn] = Solution[Row];
		}
	}
	return true;
}

bool FContactClothModalSubspace::BuildRankOneProjectionCache(
	const FContactClothModalBasis& Basis,
	FContactClothRankOneProjectionCache& OutCache)
{
	OutCache = FContactClothRankOneProjectionCache();
	if (!Basis.IsValid())
	{
		return false;
	}
	OutCache.ParticleCount = Basis.ParticleCount;
	OutCache.ModeCount = Basis.ModeCount;
	OutCache.Values.SetNumUninitialized(Basis.ParticleCount * Basis.ModeCount * Basis.ModeCount);
	for (int32 Particle = 0; Particle < Basis.ParticleCount; ++Particle)
	{
		for (int32 ModeA = 0; ModeA < Basis.ModeCount; ++ModeA)
		{
			for (int32 ModeB = 0; ModeB < Basis.ModeCount; ++ModeB)
			{
				OutCache.Values[(Particle * Basis.ModeCount + ModeA) * Basis.ModeCount + ModeB]
					= Basis.GetValue(Particle, ModeA) * Basis.GetValue(Particle, ModeB);
			}
		}
	}
	return true;
}

bool FContactClothModalSubspace::AssembleVectorCollisionSystem(
	const FContactClothReducedSystem& BaseSystem,
	const FContactClothRankOneProjectionCache& Cache,
	TConstArrayView<FContactClothModalContact> Contacts,
	FContactClothVectorReducedSystem& OutSystem)
{
	OutSystem = FContactClothVectorReducedSystem();
	if (!BaseSystem.IsValid() || !Cache.IsValid() || BaseSystem.ModeCount != Cache.ModeCount)
	{
		return false;
	}
	const int32 M = BaseSystem.ModeCount;
	const int32 DofCount = M * 3;
	OutSystem.ModeCount = M;
	OutSystem.Matrix.SetNumZeroed(DofCount * DofCount);
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		for (int32 ModeA = 0; ModeA < M; ++ModeA)
		{
			for (int32 ModeB = 0; ModeB < M; ++ModeB)
			{
				OutSystem.Matrix[(Axis * M + ModeA) * DofCount + Axis * M + ModeB] = BaseSystem.Matrix[ModeA * M + ModeB];
			}
		}
	}
	for (const FContactClothModalContact& Contact : Contacts)
	{
		if (!Cache.Values.IsValidIndex(Contact.ParticleIndex * M * M) || !FMath::IsFinite(Contact.Weight) || Contact.Weight < 0.0
			|| !FMath::IsFinite(Contact.Normal.X) || !FMath::IsFinite(Contact.Normal.Y) || !FMath::IsFinite(Contact.Normal.Z)
			|| Contact.Normal.IsNearlyZero())
		{
			return false;
		}
		const FVector3d Normal = Contact.Normal.GetSafeNormal();
		for (int32 AxisA = 0; AxisA < 3; ++AxisA)
		{
			for (int32 AxisB = 0; AxisB < 3; ++AxisB)
			{
				const double NormalProduct = Normal[AxisA] * Normal[AxisB];
				for (int32 ModeA = 0; ModeA < M; ++ModeA)
				{
					for (int32 ModeB = 0; ModeB < M; ++ModeB)
					{
						OutSystem.Matrix[(AxisA * M + ModeA) * DofCount + AxisB * M + ModeB]
							+= Contact.Weight * NormalProduct * Cache.GetValue(Contact.ParticleIndex, ModeA, ModeB);
					}
				}
			}
		}
	}
	return true;
}

bool FContactClothModalSubspace::SolveVectorReducedSystem(
	const FContactClothVectorReducedSystem& System,
	TConstArrayView<double> Rhs,
	TArray<double>& OutSolution)
{
	OutSolution.Reset();
	if (!System.IsValid() || Rhs.Num() != System.GetDofCount())
	{
		return false;
	}
	const int32 N = System.GetDofCount();
	TArray<double> Cholesky;
	Cholesky.SetNumZeroed(N * N);
	for (int32 Row = 0; Row < N; ++Row)
	{
		for (int32 Column = 0; Column <= Row; ++Column)
		{
			double Sum = System.Matrix[Row * N + Column];
			for (int32 Inner = 0; Inner < Column; ++Inner)
			{
				Sum -= Cholesky[Row * N + Inner] * Cholesky[Column * N + Inner];
			}
			if (Row == Column)
			{
				if (Sum <= UE_DOUBLE_SMALL_NUMBER) { return false; }
				Cholesky[Row * N + Column] = FMath::Sqrt(Sum);
			}
			else
			{
				Cholesky[Row * N + Column] = Sum / Cholesky[Column * N + Column];
			}
		}
	}
	TArray<double> Intermediate;
	Intermediate.SetNumZeroed(N);
	for (int32 Row = 0; Row < N; ++Row)
	{
		double Sum = Rhs[Row];
		for (int32 Column = 0; Column < Row; ++Column) { Sum -= Cholesky[Row * N + Column] * Intermediate[Column]; }
		Intermediate[Row] = Sum / Cholesky[Row * N + Row];
	}
	OutSolution.SetNumZeroed(N);
	for (int32 Row = N - 1; Row >= 0; --Row)
	{
		double Sum = Intermediate[Row];
		for (int32 Column = Row + 1; Column < N; ++Column) { Sum -= Cholesky[Column * N + Row] * OutSolution[Column]; }
		OutSolution[Row] = Sum / Cholesky[Row * N + Row];
	}
	return true;
}
