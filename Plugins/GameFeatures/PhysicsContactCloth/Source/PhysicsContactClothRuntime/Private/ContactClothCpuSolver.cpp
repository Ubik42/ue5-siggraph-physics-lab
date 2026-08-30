#include "ContactClothCpuSolver.h"

namespace ContactClothCpuSolver
{
constexpr double SmallLength = 1.e-9;
}

void FContactClothCpuSolver::InitializeGrid(const FContactClothCpuSettings& InSettings, const FVector3d& Origin)
{
	Settings = InSettings;
	Settings.Columns = FMath::Max(Settings.Columns, 2);
	Settings.Rows = FMath::Max(Settings.Rows, 2);
	Settings.SpacingCm = FMath::Max(Settings.SpacingCm, 0.01);
	Settings.ParticleMass = FMath::Max(Settings.ParticleMass, 1.e-6);
	Settings.LocalGlobalIterations = FMath::Max(Settings.LocalGlobalIterations, 1);
	Settings.GlobalGaussSeidelSweeps = FMath::Max(Settings.GlobalGaussSeidelSweeps, 1);

	Particles.Reset(Settings.Columns * Settings.Rows);
	Constraints.Reset();
	RestPositions.Reset(Settings.Columns * Settings.Rows);

	const double HalfWidth = 0.5 * static_cast<double>(Settings.Columns - 1) * Settings.SpacingCm;
	for (int32 Row = 0; Row < Settings.Rows; ++Row)
	{
		for (int32 Column = 0; Column < Settings.Columns; ++Column)
		{
			const FVector3d Position = Origin + FVector3d(
				static_cast<double>(Column) * Settings.SpacingCm - HalfWidth,
				0.0,
				-static_cast<double>(Row) * Settings.SpacingCm);
			FContactClothCpuParticle& Particle = Particles.AddDefaulted_GetRef();
			Particle.Position = Position;
			Particle.PreviousPosition = Position;
			Particle.PinnedPosition = Position;
			Particle.InverseMass = Settings.bPinTopEdge && Row == 0 ? 0.0 : 1.0 / Settings.ParticleMass;
			RestPositions.Add(Position);
		}
	}

	for (int32 Row = 0; Row < Settings.Rows; ++Row)
	{
		for (int32 Column = 0; Column < Settings.Columns; ++Column)
		{
			const int32 Index = GetParticleIndex(Column, Row);
			if (Column + 1 < Settings.Columns)
			{
				AddConstraint(Index, GetParticleIndex(Column + 1, Row), Settings.StretchWeight);
			}
			if (Row + 1 < Settings.Rows)
			{
				AddConstraint(Index, GetParticleIndex(Column, Row + 1), Settings.StretchWeight);
			}
			if (Column + 1 < Settings.Columns && Row + 1 < Settings.Rows)
			{
				AddConstraint(Index, GetParticleIndex(Column + 1, Row + 1), Settings.ShearWeight);
				AddConstraint(GetParticleIndex(Column + 1, Row), GetParticleIndex(Column, Row + 1), Settings.ShearWeight);
			}
			if (Column + 2 < Settings.Columns)
			{
				AddConstraint(Index, GetParticleIndex(Column + 2, Row), Settings.BendWeight);
			}
			if (Row + 2 < Settings.Rows)
			{
				AddConstraint(Index, GetParticleIndex(Column, Row + 2), Settings.BendWeight);
			}
		}
	}
	BuildConstraintGraph();
}

FContactClothCpuStepStats FContactClothCpuSolver::Step(double DeltaSeconds, TConstArrayView<FContactClothSphereCollider> SphereColliders)
{
	FContactClothCpuStepStats ProjectionStats;
	if (!IsInitialized() || DeltaSeconds <= 0.0)
	{
		return ProjectionStats;
	}

	const double MassTerm = Settings.ParticleMass / (DeltaSeconds * DeltaSeconds);
	TArray<FVector3d> PredictedPositions;
	TArray<FVector3d> Positions;
	PredictedPositions.SetNumUninitialized(Particles.Num());
	Positions.SetNumUninitialized(Particles.Num());

	for (int32 Index = 0; Index < Particles.Num(); ++Index)
	{
		FContactClothCpuParticle& Particle = Particles[Index];
		Particle.PreviousPosition = Particle.Position;
		if (Particle.IsPinned())
		{
			PredictedPositions[Index] = Particle.PinnedPosition;
			Positions[Index] = Particle.PinnedPosition;
		}
		else
		{
			PredictedPositions[Index] = Particle.Position + Particle.Velocity * DeltaSeconds + Settings.GravityCmPerSecondSquared * (DeltaSeconds * DeltaSeconds);
			Positions[Index] = PredictedPositions[Index];
		}
	}

	TArray<FVector3d> ProjectedEdges;
	ProjectedEdges.SetNumUninitialized(Constraints.Num());
	TArray<FVector3d> RightHandSide;
	RightHandSide.SetNumUninitialized(Particles.Num());

	for (int32 Iteration = 0; Iteration < Settings.LocalGlobalIterations; ++Iteration)
	{
		for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ++ConstraintIndex)
		{
			const FContactClothCpuConstraint& Constraint = Constraints[ConstraintIndex];
			const FVector3d Edge = Positions[Constraint.ParticleA] - Positions[Constraint.ParticleB];
			const double Length = Edge.Length();
			const FVector3d Direction = Length > ContactClothCpuSolver::SmallLength
				? Edge / Length
				: (RestPositions[Constraint.ParticleA] - RestPositions[Constraint.ParticleB]).GetSafeNormal();
			ProjectedEdges[ConstraintIndex] = Direction * Constraint.RestLengthCm;
		}

		for (int32 ParticleIndex = 0; ParticleIndex < Particles.Num(); ++ParticleIndex)
		{
			RightHandSide[ParticleIndex] = PredictedPositions[ParticleIndex] * MassTerm;
		}
		for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ++ConstraintIndex)
		{
			const FContactClothCpuConstraint& Constraint = Constraints[ConstraintIndex];
			const FVector3d WeightedProjection = ProjectedEdges[ConstraintIndex] * Constraint.Weight;
			RightHandSide[Constraint.ParticleA] += WeightedProjection;
			RightHandSide[Constraint.ParticleB] -= WeightedProjection;
		}

		for (int32 Sweep = 0; Sweep < Settings.GlobalGaussSeidelSweeps; ++Sweep)
		{
			for (int32 ParticleIndex = 0; ParticleIndex < Particles.Num(); ++ParticleIndex)
			{
				if (Particles[ParticleIndex].IsPinned())
				{
					Positions[ParticleIndex] = Particles[ParticleIndex].PinnedPosition;
					continue;
				}

				FVector3d Numerator = RightHandSide[ParticleIndex];
				double Diagonal = MassTerm;
				for (const int32 ConstraintIndex : IncidentConstraintIndices[ParticleIndex])
				{
					const FContactClothCpuConstraint& Constraint = Constraints[ConstraintIndex];
					const int32 OtherIndex = Constraint.ParticleA == ParticleIndex ? Constraint.ParticleB : Constraint.ParticleA;
					Numerator += Positions[OtherIndex] * Constraint.Weight;
					Diagonal += Constraint.Weight;
				}
				Positions[ParticleIndex] = Numerator / Diagonal;
			}
		}
		ProjectCollisions(Positions, SphereColliders, ProjectionStats);
	}

	for (int32 Index = 0; Index < Particles.Num(); ++Index)
	{
		FContactClothCpuParticle& Particle = Particles[Index];
		Particle.Position = Particle.IsPinned() ? Particle.PinnedPosition : Positions[Index];
		Particle.Velocity = Particle.IsPinned()
			? FVector3d::ZeroVector
			: (Particle.Position - Particle.PreviousPosition) / DeltaSeconds * Settings.VelocityDamping;
	}
	return MeasureFinalStats(SphereColliders, ProjectionStats);
}

void FContactClothCpuSolver::Reset()
{
	for (int32 Index = 0; Index < Particles.Num(); ++Index)
	{
		Particles[Index].Position = RestPositions[Index];
		Particles[Index].PreviousPosition = RestPositions[Index];
		Particles[Index].PinnedPosition = RestPositions[Index];
		Particles[Index].Velocity = FVector3d::ZeroVector;
	}
}

int32 FContactClothCpuSolver::GetParticleIndex(int32 Column, int32 Row) const
{
	check(Column >= 0 && Column < Settings.Columns);
	check(Row >= 0 && Row < Settings.Rows);
	return Row * Settings.Columns + Column;
}

void FContactClothCpuSolver::AddConstraint(int32 ParticleA, int32 ParticleB, double Weight)
{
	FContactClothCpuConstraint& Constraint = Constraints.AddDefaulted_GetRef();
	Constraint.ParticleA = ParticleA;
	Constraint.ParticleB = ParticleB;
	Constraint.RestLengthCm = FVector3d::Distance(RestPositions[ParticleA], RestPositions[ParticleB]);
	Constraint.Weight = FMath::Max(Weight, 0.0);
}

void FContactClothCpuSolver::BuildConstraintGraph()
{
	IncidentConstraintIndices.Reset();
	IncidentConstraintIndices.SetNum(Particles.Num());
	for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ++ConstraintIndex)
	{
		const FContactClothCpuConstraint& Constraint = Constraints[ConstraintIndex];
		IncidentConstraintIndices[Constraint.ParticleA].Add(ConstraintIndex);
		IncidentConstraintIndices[Constraint.ParticleB].Add(ConstraintIndex);
	}
}

void FContactClothCpuSolver::ProjectCollisions(TArray<FVector3d>& Positions, TConstArrayView<FContactClothSphereCollider> SphereColliders, FContactClothCpuStepStats& Stats) const
{
	for (int32 ParticleIndex = 0; ParticleIndex < Positions.Num(); ++ParticleIndex)
	{
		if (Particles[ParticleIndex].IsPinned())
		{
			continue;
		}
		for (const FContactClothSphereCollider& Collider : SphereColliders)
		{
			const double TargetRadius = FMath::Max(0.0, Collider.RadiusCm) + Settings.CollisionThicknessCm;
			const FVector3d Offset = Positions[ParticleIndex] - Collider.Center;
			const double Distance = Offset.Length();
			if (Distance < TargetRadius)
			{
				const FVector3d Normal = Distance > ContactClothCpuSolver::SmallLength ? Offset / Distance : FVector3d::UpVector;
				const double Correction = TargetRadius - Distance;
				Positions[ParticleIndex] += Normal * Correction;
				Stats.MaximumCollisionCorrectionCm = FMath::Max(Stats.MaximumCollisionCorrectionCm, Correction);
				++Stats.CollisionProjectionCount;
			}
		}
	}
}

FContactClothCpuStepStats FContactClothCpuSolver::MeasureFinalStats(TConstArrayView<FContactClothSphereCollider> SphereColliders, const FContactClothCpuStepStats& ProjectionStats) const
{
	FContactClothCpuStepStats Stats = ProjectionStats;
	double ErrorSum = 0.0;
	for (const FContactClothCpuConstraint& Constraint : Constraints)
	{
		const double Length = FVector3d::Distance(Particles[Constraint.ParticleA].Position, Particles[Constraint.ParticleB].Position);
		const double Error = Constraint.RestLengthCm > ContactClothCpuSolver::SmallLength ? FMath::Abs(Length / Constraint.RestLengthCm - 1.0) : 0.0;
		Stats.MaximumStretchRatioError = FMath::Max(Stats.MaximumStretchRatioError, Error);
		ErrorSum += Error;
	}
	Stats.MeanStretchRatioError = Constraints.IsEmpty() ? 0.0 : ErrorSum / static_cast<double>(Constraints.Num());

	for (const FContactClothCpuParticle& Particle : Particles)
	{
		if (Particle.IsPinned())
		{
			continue;
		}
		for (const FContactClothSphereCollider& Collider : SphereColliders)
		{
			const double TargetRadius = FMath::Max(0.0, Collider.RadiusCm) + Settings.CollisionThicknessCm;
			Stats.MaximumRemainingPenetrationCm = FMath::Max(Stats.MaximumRemainingPenetrationCm, TargetRadius - FVector3d::Distance(Particle.Position, Collider.Center));
		}
	}
	Stats.MaximumRemainingPenetrationCm = FMath::Max(Stats.MaximumRemainingPenetrationCm, 0.0);
	return Stats;
}
