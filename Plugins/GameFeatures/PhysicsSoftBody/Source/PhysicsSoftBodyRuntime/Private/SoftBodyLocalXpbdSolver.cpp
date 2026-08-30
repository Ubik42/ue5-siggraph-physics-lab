#include "SoftBodyLocalXpbdSolver.h"

namespace
{
struct FSoftBodyEdgeKey
{
	int32 A = INDEX_NONE;
	int32 B = INDEX_NONE;

	FSoftBodyEdgeKey(int32 InA, int32 InB)
		: A(FMath::Min(InA, InB))
		, B(FMath::Max(InA, InB))
	{
	}

	bool operator==(const FSoftBodyEdgeKey& Other) const
	{
		return A == Other.A && B == Other.B;
	}

	friend uint32 GetTypeHash(const FSoftBodyEdgeKey& Key)
	{
		return HashCombine(GetTypeHash(Key.A), GetTypeHash(Key.B));
	}
};

double SignedVolumeCm3(
	const FVector3d& A,
	const FVector3d& B,
	const FVector3d& C,
	const FVector3d& D)
{
	return FVector3d::DotProduct(B - A, FVector3d::CrossProduct(C - A, D - A)) / 6.0;
}

void SetXpbdFailure(FString* OutFailureReason, const FString& FailureReason)
{
	if (OutFailureReason)
	{
		*OutFailureReason = FailureReason;
	}
}
}

bool FSoftBodyLocalXpbdSolver::Initialize(
	const FSoftBodyTetMesh& InMesh,
	const FSoftBodyLocalXpbdSettings& InSettings,
	FString* OutFailureReason)
{
	bInitialized = false;
	Mesh = InMesh;
	Settings = InSettings;
	if (!Mesh.IsValid(OutFailureReason) || !ValidateSettings(OutFailureReason))
	{
		return false;
	}

	TMap<FSoftBodyEdgeKey, uint8> EdgeMaterials;
	for (int32 TetIndex = 0; TetIndex < Mesh.Tetrahedra.Num(); ++TetIndex)
	{
		const FSoftBodyTetrahedron& Tet = Mesh.Tetrahedra[TetIndex];
		const int32 Vertices[4] = { Tet.A, Tet.B, Tet.C, Tet.D };
		for (int32 I = 0; I < 4; ++I)
		{
			for (int32 J = I + 1; J < 4; ++J)
			{
				uint8& Material = EdgeMaterials.FindOrAdd(FSoftBodyEdgeKey(Vertices[I], Vertices[J]));
				Material = FMath::Max(Material, Mesh.MaterialIds[TetIndex]);
			}
		}
	}
	TArray<FSoftBodyEdgeKey> EdgeKeys;
	EdgeMaterials.GenerateKeyArray(EdgeKeys);
	EdgeKeys.Sort([](const FSoftBodyEdgeKey& Left, const FSoftBodyEdgeKey& Right)
	{
		return Left.A != Right.A ? Left.A < Right.A : Left.B < Right.B;
	});
	DistanceConstraints.Reserve(EdgeKeys.Num());
	for (const FSoftBodyEdgeKey& Key : EdgeKeys)
	{
		const double RestLength = FVector3d(Mesh.RestPositions[Key.B] - Mesh.RestPositions[Key.A]).Length();
		if (!FMath::IsFinite(RestLength) || RestLength <= UE_DOUBLE_SMALL_NUMBER)
		{
			SetXpbdFailure(OutFailureReason, TEXT("四面体网格包含零长度结构边"));
			return false;
		}
		DistanceConstraints.Add(FDistanceConstraint{ Key.A, Key.B, RestLength, EdgeMaterials[Key] });
	}

	Positions.Reserve(Mesh.RestPositions.Num());
	for (const FVector3f& Position : Mesh.RestPositions)
	{
		Positions.Add(FVector3d(Position));
	}
	PreviousPositions = Positions;
	DistanceLambdas.SetNumZeroed(DistanceConstraints.Num());
	VolumeLambdas.SetNumZeroed(Mesh.Tetrahedra.Num());
	KinematicMask.Init(false, Mesh.RestPositions.Num());
	KinematicTargets = Positions;
	BuildLowFrequencyBasis();
	StepsExecuted = 0;
	LatestStats = FSoftBodyLocalXpbdStats();
	bInitialized = true;
	return true;
}

bool FSoftBodyLocalXpbdSolver::ValidateSettings(FString* OutFailureReason) const
{
	const bool bFinite = FMath::IsFinite(Settings.FixedDeltaSeconds)
		&& FMath::IsFinite(Settings.VelocityDamping)
		&& FMath::IsFinite(Settings.JacobiRelaxation)
		&& FMath::IsFinite(Settings.MaximumVertexCorrectionCm)
		&& FMath::IsFinite(Settings.SoftEdgeCompliance)
		&& FMath::IsFinite(Settings.StiffEdgeCompliance)
		&& FMath::IsFinite(Settings.SoftVolumeCompliance)
		&& FMath::IsFinite(Settings.StiffVolumeCompliance)
		&& FMath::IsFinite(Settings.LowFrequencyCorrectionStrength)
		&& FMath::IsFinite(Settings.GroundPlaneZCm)
		&& FMath::IsFinite(Settings.GroundContactStiffness)
		&& !Settings.GravityCmPerSecondSquared.ContainsNaN();
	if (!bFinite || Settings.FixedDeltaSeconds <= 0.0 || Settings.SolverIterations <= 0
		|| Settings.VelocityDamping < 0.0 || Settings.VelocityDamping > 1.0
		|| Settings.JacobiRelaxation <= 0.0 || Settings.JacobiRelaxation > 1.0
		|| Settings.MaximumVertexCorrectionCm <= 0.0
		|| Settings.SoftEdgeCompliance < 0.0 || Settings.StiffEdgeCompliance < 0.0
		|| Settings.SoftVolumeCompliance < 0.0 || Settings.StiffVolumeCompliance < 0.0
		|| Settings.LowFrequencyCorrectionStrength < 0.0 || Settings.LowFrequencyCorrectionStrength > 1.0
		|| Settings.LowFrequencyCorrectionInterval <= 0
		|| Settings.GroundContactStiffness < 0.0 || Settings.GroundContactStiffness > 1.0)
	{
		SetXpbdFailure(OutFailureReason, TEXT("局部 XPBD 设置必须为有限、正时间步和合法阻尼/松弛/柔度"));
		return false;
	}
	return true;
}

void FSoftBodyLocalXpbdSolver::Reset()
{
	if (!bInitialized)
	{
		return;
	}
	Positions.Reset(Mesh.RestPositions.Num());
	for (const FVector3f& Position : Mesh.RestPositions)
	{
		Positions.Add(FVector3d(Position));
	}
	PreviousPositions = Positions;
	DistanceLambdas.SetNumZeroed(DistanceConstraints.Num());
	VolumeLambdas.SetNumZeroed(Mesh.Tetrahedra.Num());
	StepsExecuted = 0;
	LatestStats = FSoftBodyLocalXpbdStats();
}

bool FSoftBodyLocalXpbdSolver::SetKinematicTarget(
	int32 VertexIndex,
	const FVector3d& TargetPosition,
	FString* OutFailureReason)
{
	if (!bInitialized || !Positions.IsValidIndex(VertexIndex) || TargetPosition.ContainsNaN())
	{
		SetXpbdFailure(OutFailureReason, TEXT("运动学目标要求已初始化求解器、有效顶点和有限位置"));
		return false;
	}
	KinematicMask[VertexIndex] = true;
	KinematicTargets[VertexIndex] = TargetPosition;
	return true;
}

void FSoftBodyLocalXpbdSolver::ClearKinematicTargets()
{
	KinematicMask.Init(false, Mesh.RestPositions.Num());
	KinematicTargets.Reset(Mesh.RestPositions.Num());
	for (const FVector3f& RestPosition : Mesh.RestPositions)
	{
		KinematicTargets.Add(FVector3d(RestPosition));
	}
}

double FSoftBodyLocalXpbdSolver::EffectiveInverseMass(int32 VertexIndex) const
{
	return KinematicMask.IsValidIndex(VertexIndex) && KinematicMask[VertexIndex]
		? 0.0
		: static_cast<double>(Mesh.InverseMasses[VertexIndex]);
}

FVector3d FSoftBodyLocalXpbdSolver::EffectiveFixedTarget(int32 VertexIndex) const
{
	return KinematicMask.IsValidIndex(VertexIndex) && KinematicMask[VertexIndex]
		? KinematicTargets[VertexIndex]
		: FVector3d(Mesh.RestPositions[VertexIndex]);
}

double FSoftBodyLocalXpbdSolver::EdgeCompliance(uint8 MaterialId) const
{
	return MaterialId == 0 ? Settings.SoftEdgeCompliance : Settings.StiffEdgeCompliance;
}

double FSoftBodyLocalXpbdSolver::VolumeCompliance(uint8 MaterialId) const
{
	return MaterialId == 0 ? Settings.SoftVolumeCompliance : Settings.StiffVolumeCompliance;
}

void FSoftBodyLocalXpbdSolver::BuildLowFrequencyBasis()
{
	LowFrequencyBasis.SetNumZeroed(Mesh.RestPositions.Num());
	if (Mesh.RestPositions.IsEmpty())
	{
		return;
	}
	FVector3d Minimum(Mesh.RestPositions[0]);
	FVector3d Maximum = Minimum;
	for (const FVector3f& RestPositionFloat : Mesh.RestPositions)
	{
		const FVector3d RestPosition(RestPositionFloat);
		Minimum.X = FMath::Min(Minimum.X, RestPosition.X);
		Minimum.Y = FMath::Min(Minimum.Y, RestPosition.Y);
		Minimum.Z = FMath::Min(Minimum.Z, RestPosition.Z);
		Maximum.X = FMath::Max(Maximum.X, RestPosition.X);
		Maximum.Y = FMath::Max(Maximum.Y, RestPosition.Y);
		Maximum.Z = FMath::Max(Maximum.Z, RestPosition.Z);
	}
	const FVector3d Extent = Maximum - Minimum;
	const int32 Axis = Extent.X >= Extent.Y && Extent.X >= Extent.Z ? 0 : (Extent.Y >= Extent.Z ? 1 : 2);
	const double AxisMinimum = Axis == 0 ? Minimum.X : (Axis == 1 ? Minimum.Y : Minimum.Z);
	const double AxisExtent = Axis == 0 ? Extent.X : (Axis == 1 ? Extent.Y : Extent.Z);
	if (AxisExtent <= UE_DOUBLE_SMALL_NUMBER)
	{
		return;
	}
	for (int32 Vertex = 0; Vertex < Mesh.RestPositions.Num(); ++Vertex)
	{
		const FVector3d RestPosition(Mesh.RestPositions[Vertex]);
		const double Coordinate = Axis == 0 ? RestPosition.X : (Axis == 1 ? RestPosition.Y : RestPosition.Z);
		const double T = FMath::Clamp((Coordinate - AxisMinimum) / AxisExtent, 0.0, 1.0);
		LowFrequencyBasis[Vertex] = FVector2d(T, T * T);
	}
}

bool FSoftBodyLocalXpbdSolver::ProjectLowFrequencyCorrection(
	const TArray<FVector3d>& LocalCorrections,
	FVector3d& OutLinearCoefficient,
	FVector3d& OutQuadraticCoefficient) const
{
	double S00 = 0.0;
	double S01 = 0.0;
	double S11 = 0.0;
	FVector3d R0 = FVector3d::ZeroVector;
	FVector3d R1 = FVector3d::ZeroVector;
	for (int32 Vertex = 0; Vertex < LocalCorrections.Num(); ++Vertex)
	{
		if (EffectiveInverseMass(Vertex) <= 0.0)
		{
			continue;
		}
		const FVector2d Phi = LowFrequencyBasis[Vertex];
		S00 += Phi.X * Phi.X;
		S01 += Phi.X * Phi.Y;
		S11 += Phi.Y * Phi.Y;
		R0 += Phi.X * LocalCorrections[Vertex];
		R1 += Phi.Y * LocalCorrections[Vertex];
	}
	const double Determinant = S00 * S11 - S01 * S01;
	if (Determinant <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}
	OutLinearCoefficient = (S11 * R0 - S01 * R1) / Determinant;
	OutQuadraticCoefficient = (S00 * R1 - S01 * R0) / Determinant;
	return !OutLinearCoefficient.ContainsNaN() && !OutQuadraticCoefficient.ContainsNaN();
}

bool FSoftBodyLocalXpbdSolver::Step(FSoftBodyLocalXpbdStats& OutStats, FString* OutFailureReason)
{
	if (!bInitialized || !ValidateSettings(OutFailureReason))
	{
		SetXpbdFailure(OutFailureReason, TEXT("局部 XPBD 求解器尚未有效初始化"));
		return false;
	}
	const double Dt = Settings.FixedDeltaSeconds;
	const double DtSquared = Dt * Dt;
	for (int32 Vertex = 0; Vertex < Positions.Num(); ++Vertex)
	{
		if (EffectiveInverseMass(Vertex) <= 0.0)
		{
			Positions[Vertex] = EffectiveFixedTarget(Vertex);
			PreviousPositions[Vertex] = Positions[Vertex];
			continue;
		}
		const FVector3d Current = Positions[Vertex];
		const FVector3d VelocityStep = (Current - PreviousPositions[Vertex]) * Settings.VelocityDamping;
		Positions[Vertex] = Current + VelocityStep + Settings.GravityCmPerSecondSquared * DtSquared;
		PreviousPositions[Vertex] = Current;
	}
	DistanceLambdas.SetNumZeroed(DistanceConstraints.Num());
	VolumeLambdas.SetNumZeroed(Mesh.Tetrahedra.Num());

	TArray<FVector3d> Corrections;
	Corrections.SetNumZeroed(Positions.Num());
	TArray<int32> ContributionCounts;
	ContributionCounts.SetNumZeroed(Positions.Num());
	TArray<FVector3d> LocalCorrections;
	LocalCorrections.SetNumZeroed(Positions.Num());
	OutStats = FSoftBodyLocalXpbdStats();
	OutStats.IterationResidualRms.Reserve(Settings.SolverIterations);
	for (int32 Iteration = 0; Iteration < Settings.SolverIterations; ++Iteration)
	{
		for (FVector3d& Correction : Corrections) Correction = FVector3d::ZeroVector;
		for (int32& Count : ContributionCounts) Count = 0;

		for (int32 ConstraintIndex = 0; ConstraintIndex < DistanceConstraints.Num(); ++ConstraintIndex)
		{
			const FDistanceConstraint& Constraint = DistanceConstraints[ConstraintIndex];
			const FVector3d Delta = Positions[Constraint.B] - Positions[Constraint.A];
			const double Length = Delta.Length();
			if (Length <= UE_DOUBLE_SMALL_NUMBER) continue;
			const FVector3d Gradient = Delta / Length;
			const double WA = EffectiveInverseMass(Constraint.A);
			const double WB = EffectiveInverseMass(Constraint.B);
			const double Alpha = EdgeCompliance(Constraint.MaterialId) / DtSquared;
			const double Denominator = WA + WB + Alpha;
			if (Denominator <= UE_DOUBLE_SMALL_NUMBER) continue;
			const double C = Length - Constraint.RestLengthCm;
			const double DeltaLambda = (-C - Alpha * DistanceLambdas[ConstraintIndex]) / Denominator;
			DistanceLambdas[ConstraintIndex] += DeltaLambda;
			if (WA > 0.0)
			{
				Corrections[Constraint.A] -= WA * DeltaLambda * Gradient;
				++ContributionCounts[Constraint.A];
			}
			if (WB > 0.0)
			{
				Corrections[Constraint.B] += WB * DeltaLambda * Gradient;
				++ContributionCounts[Constraint.B];
			}
		}

		for (int32 TetIndex = 0; TetIndex < Mesh.Tetrahedra.Num(); ++TetIndex)
		{
			const FSoftBodyTetrahedron& Tet = Mesh.Tetrahedra[TetIndex];
			const FVector3d& X0 = Positions[Tet.A];
			const FVector3d& X1 = Positions[Tet.B];
			const FVector3d& X2 = Positions[Tet.C];
			const FVector3d& X3 = Positions[Tet.D];
			const FVector3d G1 = FVector3d::CrossProduct(X2 - X0, X3 - X0) / 6.0;
			const FVector3d G2 = FVector3d::CrossProduct(X3 - X0, X1 - X0) / 6.0;
			const FVector3d G3 = FVector3d::CrossProduct(X1 - X0, X2 - X0) / 6.0;
			const FVector3d G0 = -(G1 + G2 + G3);
			const FVector3d Gradients[4] = { G0, G1, G2, G3 };
			const int32 Vertices[4] = { Tet.A, Tet.B, Tet.C, Tet.D };
			double Denominator = VolumeCompliance(Mesh.MaterialIds[TetIndex]) / DtSquared;
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				Denominator += EffectiveInverseMass(Vertices[Corner]) * Gradients[Corner].SquaredLength();
			}
			if (Denominator <= UE_DOUBLE_SMALL_NUMBER) continue;
			const double Alpha = VolumeCompliance(Mesh.MaterialIds[TetIndex]) / DtSquared;
			const double C = SignedVolumeCm3(X0, X1, X2, X3) - Mesh.RestVolumesCm3[TetIndex];
			const double DeltaLambda = (-C - Alpha * VolumeLambdas[TetIndex]) / Denominator;
			VolumeLambdas[TetIndex] += DeltaLambda;
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const double W = EffectiveInverseMass(Vertices[Corner]);
				if (W > 0.0)
				{
					Corrections[Vertices[Corner]] += W * DeltaLambda * Gradients[Corner];
					++ContributionCounts[Vertices[Corner]];
				}
			}
		}

		for (int32 Vertex = 0; Vertex < Positions.Num(); ++Vertex)
		{
			LocalCorrections[Vertex] = ContributionCounts[Vertex] > 0
				? Corrections[Vertex] / static_cast<double>(ContributionCounts[Vertex])
				: FVector3d::ZeroVector;
		}
		const bool bApplyLowFrequencyCorrection = Settings.bEnableLowFrequencyCorrection
			&& Iteration % Settings.LowFrequencyCorrectionInterval == 0;
		FVector3d LinearCoefficient = FVector3d::ZeroVector;
		FVector3d QuadraticCoefficient = FVector3d::ZeroVector;
		const bool bProjectionValid = bApplyLowFrequencyCorrection && ProjectLowFrequencyCorrection(
			LocalCorrections, LinearCoefficient, QuadraticCoefficient);
		if (bProjectionValid)
		{
			++OutStats.LowFrequencyCorrectionPasses;
			OutStats.LowFrequencyModeCount = 2;
		}

		for (int32 Vertex = 0; Vertex < Positions.Num(); ++Vertex)
		{
			if (EffectiveInverseMass(Vertex) <= 0.0)
			{
				Positions[Vertex] = EffectiveFixedTarget(Vertex);
				continue;
			}
			if (ContributionCounts[Vertex] > 0)
			{
				FVector3d Correction = LocalCorrections[Vertex];
				if (bProjectionValid)
				{
					const FVector2d Phi = LowFrequencyBasis[Vertex];
					const FVector3d GlobalCorrection = Settings.LowFrequencyCorrectionStrength
						* (Phi.X * LinearCoefficient + Phi.Y * QuadraticCoefficient);
					OutStats.MaximumLowFrequencyCorrectionCm = FMath::Max(
						OutStats.MaximumLowFrequencyCorrectionCm, GlobalCorrection.Length());
					Correction += GlobalCorrection;
				}
				Correction = Correction.GetClampedToMaxSize(Settings.MaximumVertexCorrectionCm);
				Positions[Vertex] += Settings.JacobiRelaxation * Correction;
			}
		}
		OutStats.IterationResidualRms.Add(CalculateResidualRms());
	}

	if (Settings.bEnableGroundContact)
	{
		for (int32 Vertex = 0; Vertex < Positions.Num(); ++Vertex)
		{
			if (EffectiveInverseMass(Vertex) <= 0.0)
			{
				Positions[Vertex] = EffectiveFixedTarget(Vertex);
				continue;
			}
			const double PenetrationCm = FMath::Max(0.0, Settings.GroundPlaneZCm - Positions[Vertex].Z);
			Positions[Vertex].Z += Settings.GroundContactStiffness * PenetrationCm;
		}
		OutStats.GroundContactPassesExecuted = 1;
	}

	for (const FVector3d& Position : Positions)
	{
		if (Position.ContainsNaN())
		{
			SetXpbdFailure(OutFailureReason, TEXT("局部 XPBD 产生了非有限位置"));
			return false;
		}
	}
	++StepsExecuted;
	Measure(OutStats);
	OutStats.StepsExecuted = StepsExecuted;
	OutStats.IterationsExecuted = Settings.SolverIterations;
	OutStats.DistanceConstraintCount = DistanceConstraints.Num();
	OutStats.VolumeConstraintCount = Mesh.Tetrahedra.Num();
	LatestStats = OutStats;
	return true;
}

double FSoftBodyLocalXpbdSolver::CalculateResidualRms() const
{
	double SumSquared = 0.0;
	int32 Count = 0;
	for (const FDistanceConstraint& Constraint : DistanceConstraints)
	{
		const double Relative = ((Positions[Constraint.B] - Positions[Constraint.A]).Length() - Constraint.RestLengthCm)
			/ Constraint.RestLengthCm;
		SumSquared += Relative * Relative;
		++Count;
	}
	for (int32 TetIndex = 0; TetIndex < Mesh.Tetrahedra.Num(); ++TetIndex)
	{
		const FSoftBodyTetrahedron& Tet = Mesh.Tetrahedra[TetIndex];
		const double Relative = (SignedVolumeCm3(Positions[Tet.A], Positions[Tet.B], Positions[Tet.C], Positions[Tet.D])
			- Mesh.RestVolumesCm3[TetIndex]) / Mesh.RestVolumesCm3[TetIndex];
		SumSquared += Relative * Relative;
		++Count;
	}
	return Count > 0 ? FMath::Sqrt(SumSquared / Count) : 0.0;
}

void FSoftBodyLocalXpbdSolver::Measure(FSoftBodyLocalXpbdStats& OutStats) const
{
	OutStats.ResidualRms = CalculateResidualRms();
	if (Settings.bEnableGroundContact)
	{
		for (int32 Vertex = 0; Vertex < Positions.Num(); ++Vertex)
		{
			if (EffectiveInverseMass(Vertex) <= 0.0) continue;
			OutStats.MaximumGroundPenetrationCm = FMath::Max(
				OutStats.MaximumGroundPenetrationCm, Settings.GroundPlaneZCm - Positions[Vertex].Z);
			if (FMath::Abs(Positions[Vertex].Z - Settings.GroundPlaneZCm) < 0.01)
			{
				++OutStats.ActiveGroundContactVertices;
			}
		}
		OutStats.MaximumGroundPenetrationCm = FMath::Max(0.0, OutStats.MaximumGroundPenetrationCm);
	}
	double VolumeErrorSum = 0.0;
	double MaterialVolumeErrorSums[2] = { 0.0, 0.0 };
	int32 MaterialVolumeCounts[2] = { 0, 0 };
	double CurrentTotalVolume = 0.0;
	for (const FDistanceConstraint& Constraint : DistanceConstraints)
	{
		const double RelativeError = FMath::Abs((Positions[Constraint.B] - Positions[Constraint.A]).Length()
			- Constraint.RestLengthCm) / Constraint.RestLengthCm;
		OutStats.MaximumEdgeRelativeError = FMath::Max(OutStats.MaximumEdgeRelativeError, RelativeError);
	}
	for (int32 TetIndex = 0; TetIndex < Mesh.Tetrahedra.Num(); ++TetIndex)
	{
		const FSoftBodyTetrahedron& Tet = Mesh.Tetrahedra[TetIndex];
		const double CurrentVolume = SignedVolumeCm3(Positions[Tet.A], Positions[Tet.B], Positions[Tet.C], Positions[Tet.D]);
		const double RelativeError = FMath::Abs(CurrentVolume - Mesh.RestVolumesCm3[TetIndex]) / Mesh.RestVolumesCm3[TetIndex];
		VolumeErrorSum += RelativeError;
		const int32 MaterialIndex = FMath::Clamp<int32>(Mesh.MaterialIds[TetIndex], 0, 1);
		MaterialVolumeErrorSums[MaterialIndex] += RelativeError;
		++MaterialVolumeCounts[MaterialIndex];
		OutStats.MaximumVolumeRelativeError = FMath::Max(OutStats.MaximumVolumeRelativeError, RelativeError);
		CurrentTotalVolume += CurrentVolume;
	}
	OutStats.MeanVolumeRelativeError = VolumeErrorSum / Mesh.Tetrahedra.Num();
	OutStats.MeanSoftMaterialVolumeRelativeError = MaterialVolumeCounts[0] > 0
		? MaterialVolumeErrorSums[0] / MaterialVolumeCounts[0] : 0.0;
	OutStats.MeanStiffMaterialVolumeRelativeError = MaterialVolumeCounts[1] > 0
		? MaterialVolumeErrorSums[1] / MaterialVolumeCounts[1] : 0.0;
	OutStats.TotalVolumeRatio = CurrentTotalVolume / Mesh.CalculateTotalRestVolumeCm3();
	double TipDisplacementSum = 0.0;
	int32 TipVertexCount = 0;
	double MaximumRestX = -TNumericLimits<double>::Max();
	for (const FVector3f& RestPosition : Mesh.RestPositions)
	{
		MaximumRestX = FMath::Max(MaximumRestX, static_cast<double>(RestPosition.X));
	}
	for (int32 Vertex = 0; Vertex < Positions.Num(); ++Vertex)
	{
		const FVector3d RestPosition(Mesh.RestPositions[Vertex]);
		if (Mesh.InverseMasses[Vertex] <= 0.0f)
		{
			OutStats.FixedVertexDriftCm = FMath::Max(OutStats.FixedVertexDriftCm, (Positions[Vertex] - RestPosition).Length());
		}
		if (KinematicMask.IsValidIndex(Vertex) && KinematicMask[Vertex])
		{
			OutStats.MaximumKinematicTargetErrorCm = FMath::Max(
				OutStats.MaximumKinematicTargetErrorCm,
				(Positions[Vertex] - KinematicTargets[Vertex]).Length());
		}
		if (FMath::IsNearlyEqual(RestPosition.X, MaximumRestX))
		{
			TipDisplacementSum += RestPosition.Z - Positions[Vertex].Z;
			++TipVertexCount;
		}
	}
	OutStats.TipDisplacementCm = TipVertexCount > 0 ? TipDisplacementSum / TipVertexCount : 0.0;
}
