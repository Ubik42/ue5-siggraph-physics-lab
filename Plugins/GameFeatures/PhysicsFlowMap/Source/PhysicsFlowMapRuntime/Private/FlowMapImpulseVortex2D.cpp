#include "FlowMapImpulseVortex2D.h"

namespace
{
	float WrapImpulseCoordinate(float Value, float Minimum, float Extent)
	{
		return Minimum + FMath::Fmod(FMath::Fmod(Value - Minimum, Extent) + Extent, Extent);
	}

	int32 WrapImpulseIndex(int32 Value, int32 Count)
	{
		return (Value % Count + Count) % Count;
	}

	float PeriodicBilinear(const TArray<float>& Values, int32 Width, int32 Height, float X, float Y)
	{
		const int32 X0 = FMath::FloorToInt(X);
		const int32 Y0 = FMath::FloorToInt(Y);
		const float Tx = X - X0;
		const float Ty = Y - Y0;
		const float A = FMath::Lerp(Values[WrapImpulseIndex(Y0, Height) * Width + WrapImpulseIndex(X0, Width)], Values[WrapImpulseIndex(Y0, Height) * Width + WrapImpulseIndex(X0 + 1, Width)], Tx);
		const float B = FMath::Lerp(Values[WrapImpulseIndex(Y0 + 1, Height) * Width + WrapImpulseIndex(X0, Width)], Values[WrapImpulseIndex(Y0 + 1, Height) * Width + WrapImpulseIndex(X0 + 1, Width)], Tx);
		return FMath::Lerp(A, B, Ty);
	}

	FFlowMapMatrix2f MultiplyImpulseMatrices(const FFlowMapMatrix2f& A, const FFlowMapMatrix2f& B)
	{
		FFlowMapMatrix2f Result;
		Result.M00 = A.M00 * B.M00 + A.M01 * B.M10;
		Result.M01 = A.M00 * B.M01 + A.M01 * B.M11;
		Result.M10 = A.M10 * B.M00 + A.M11 * B.M10;
		Result.M11 = A.M10 * B.M01 + A.M11 * B.M11;
		return Result;
	}
}

bool FFlowMapImpulseVortexSimulation2D::Initialize(const FFlowMapImpulseVortexSettings2D& InSettings, EFlowMapImpulseMode2D InMode, FString* OutFailureReason)
{
	if (InSettings.Grid.CellCounts.X < 8 || InSettings.Grid.CellCounts.Y < 8 || InSettings.Grid.CellCounts.X != InSettings.Grid.CellCounts.Y ||
		InSettings.Grid.CellSizeCm <= 0.0f || InSettings.ParticlesPerCellAxis < 1 || InSettings.FixedDeltaSeconds <= 0.0f ||
		InSettings.LongMapResetSteps < 1 || InSettings.PressureIterations < 1 || InSettings.VortexAmplitudeCmPerSecond <= 0.0f)
	{
		if (OutFailureReason) *OutFailureReason = TEXT("Impulse vortex 需要正方形周期网格、正固定步、粒子密度、重置周期、压力预算和速度幅值。 ");
		return false;
	}
	Settings = InSettings;
	Mode = InMode;
	Metrics = FFlowMapImpulseVortexMetrics2D();
	if (!VelocityGrid.Initialize(Settings.Grid, OutFailureReason)) return false;
	const int32 Nx = Settings.Grid.CellCounts.X;
	const int32 Ny = Settings.Grid.CellCounts.Y;
	const float H = Settings.Grid.CellSizeCm;
	for (int32 Y = 0; Y < Ny; ++Y)
		for (int32 X = 0; X <= Nx; ++X)
		{
			const FVector2f P = Settings.Grid.OriginCm + FVector2f(X * H, (Y + 0.5f) * H);
			VelocityGrid.FaceU[Y * (Nx + 1) + X] = EvaluateTaylorGreenVelocity(P).X;
		}
	for (int32 Y = 0; Y <= Ny; ++Y)
		for (int32 X = 0; X < Nx; ++X)
		{
			const FVector2f P = Settings.Grid.OriginCm + FVector2f((X + 0.5f) * H, Y * H);
			VelocityGrid.FaceV[Y * Nx + X] = EvaluateTaylorGreenVelocity(P).Y;
		}
	Metrics.InitialKineticEnergyProxy = VelocityGrid.CalculateKineticEnergyProxy();
	Particles.Reset();
	const int32 S = Settings.ParticlesPerCellAxis;
	Particles.Reserve(Nx * Ny * S * S);
	for (int32 Y = 0; Y < Ny; ++Y)
		for (int32 X = 0; X < Nx; ++X)
			for (int32 Py = 0; Py < S; ++Py)
				for (int32 Px = 0; Px < S; ++Px)
				{
					FImpulseParticle Particle;
					Particle.PositionCm = Settings.Grid.OriginCm + FVector2f((X + (Px + 0.5f) / S) * H, (Y + (Py + 0.5f) / S) * H);
					Particle.ReferenceImpulseCmPerSecond = EvaluateTaylorGreenVelocity(Particle.PositionCm);
					Particle.ReferenceImpulseGradientPerSecond = EvaluateTaylorGreenVelocityGradient(Particle.PositionCm);
					Particles.Add(Particle);
				}
	Metrics.ParticleCount = Particles.Num();
	UpdateMetrics();
	return true;
}

bool FFlowMapImpulseVortexSimulation2D::Step(FString* OutFailureReason)
{
	if (Particles.IsEmpty())
	{
		if (OutFailureReason) *OutFailureReason = TEXT("Impulse vortex 会话尚未初始化。 ");
		return false;
	}
	const FVector2f Min = Settings.Grid.OriginCm;
	const FVector2f Extent(Settings.Grid.CellCounts.X * Settings.Grid.CellSizeCm, Settings.Grid.CellCounts.Y * Settings.Grid.CellSizeCm);
	for (FImpulseParticle& Particle : Particles)
	{
		const FVector2f V0 = SamplePeriodicVelocity(Particle.PositionCm);
		FVector2f Mid = Particle.PositionCm + V0 * (0.5f * Settings.FixedDeltaSeconds);
		Mid.X = WrapImpulseCoordinate(Mid.X, Min.X, Extent.X);
		Mid.Y = WrapImpulseCoordinate(Mid.Y, Min.Y, Extent.Y);
		const FVector2f Vmid = SamplePeriodicVelocity(Mid);
		Particle.PositionCm += Vmid * Settings.FixedDeltaSeconds;
		Particle.PositionCm.X = WrapImpulseCoordinate(Particle.PositionCm.X, Min.X, Extent.X);
		Particle.PositionCm.Y = WrapImpulseCoordinate(Particle.PositionCm.Y, Min.Y, Extent.Y);
		const FFlowMapMatrix2f Gradient = SamplePeriodicVelocityGradient(Mid);
		FFlowMapMatrix2f BackwardStep;
		BackwardStep.M00 = 1.0f - Settings.FixedDeltaSeconds * Gradient.M00;
		BackwardStep.M01 = -Settings.FixedDeltaSeconds * Gradient.M01;
		BackwardStep.M10 = -Settings.FixedDeltaSeconds * Gradient.M10;
		BackwardStep.M11 = 1.0f - Settings.FixedDeltaSeconds * Gradient.M11;
		Particle.BackwardMapJacobian = MultiplyImpulseMatrices(Particle.BackwardMapJacobian, BackwardStep);
		Metrics.MaximumJacobianDeterminantError = FMath::Max(Metrics.MaximumJacobianDeterminantError, FMath::Abs(Particle.BackwardMapJacobian.Determinant() - 1.0f));
	}
	if (!DepositCurrentImpulse(OutFailureReason)) return false;
	FFlowMapPressureProjectionMetrics2D Projection;
	if (!FFlowMapPressureProjector2D::ProjectPeriodic(VelocityGrid, Settings.PressureIterations, Projection, OutFailureReason)) return false;
	++Metrics.StepsExecuted;
	++Metrics.PressureProjections;
	UpdateMetrics();
	if (Mode == EFlowMapImpulseMode2D::ShortPic || Metrics.StepsExecuted % Settings.LongMapResetSteps == 0)
	{
		ResetParticleMaps();
		++Metrics.MapResets;
	}
	return true;
}

FVector2f FFlowMapImpulseVortexSimulation2D::EvaluateTaylorGreenVelocity(const FVector2f& PositionCm) const
{
	const float Length = Settings.Grid.CellCounts.X * Settings.Grid.CellSizeCm;
	const float K = 2.0f * PI / Length;
	const float X = PositionCm.X - Settings.Grid.OriginCm.X;
	const float Y = PositionCm.Y - Settings.Grid.OriginCm.Y;
	return FVector2f(
		Settings.VortexAmplitudeCmPerSecond * FMath::Sin(K * X) * FMath::Cos(K * Y),
		-Settings.VortexAmplitudeCmPerSecond * FMath::Cos(K * X) * FMath::Sin(K * Y));
}

FFlowMapMatrix2f FFlowMapImpulseVortexSimulation2D::EvaluateTaylorGreenVelocityGradient(const FVector2f& PositionCm) const
{
	const float Length = Settings.Grid.CellCounts.X * Settings.Grid.CellSizeCm;
	const float K = 2.0f * PI / Length;
	const float X = PositionCm.X - Settings.Grid.OriginCm.X;
	const float Y = PositionCm.Y - Settings.Grid.OriginCm.Y;
	FFlowMapMatrix2f Result;
	Result.M00 = Settings.VortexAmplitudeCmPerSecond * K * FMath::Cos(K * X) * FMath::Cos(K * Y);
	Result.M01 = -Settings.VortexAmplitudeCmPerSecond * K * FMath::Sin(K * X) * FMath::Sin(K * Y);
	Result.M10 = Settings.VortexAmplitudeCmPerSecond * K * FMath::Sin(K * X) * FMath::Sin(K * Y);
	Result.M11 = -Settings.VortexAmplitudeCmPerSecond * K * FMath::Cos(K * X) * FMath::Cos(K * Y);
	return Result;
}

FVector2f FFlowMapImpulseVortexSimulation2D::SamplePeriodicVelocity(const FVector2f& PositionCm) const
{
	const int32 Nx = Settings.Grid.CellCounts.X;
	const int32 Ny = Settings.Grid.CellCounts.Y;
	const FVector2f Grid = (PositionCm - Settings.Grid.OriginCm) / Settings.Grid.CellSizeCm;
	const int32 Ux0 = FMath::FloorToInt(Grid.X);
	const int32 Uy0 = FMath::FloorToInt(Grid.Y - 0.5f);
	const float Utx = Grid.X - Ux0;
	const float Uty = Grid.Y - 0.5f - Uy0;
	auto GetU = [this, Nx, Ny](int32 X, int32 Y) { return VelocityGrid.FaceU[WrapImpulseIndex(Y, Ny) * (Nx + 1) + WrapImpulseIndex(X, Nx)]; };
	const float Ua = FMath::Lerp(GetU(Ux0, Uy0), GetU(Ux0 + 1, Uy0), Utx);
	const float Ub = FMath::Lerp(GetU(Ux0, Uy0 + 1), GetU(Ux0 + 1, Uy0 + 1), Utx);
	const float U = FMath::Lerp(Ua, Ub, Uty);
	const float V = PeriodicBilinear(VelocityGrid.FaceV, Nx, Ny, Grid.X - 0.5f, Grid.Y);
	return FVector2f(U, V);
}

FFlowMapMatrix2f FFlowMapImpulseVortexSimulation2D::SamplePeriodicVelocityGradient(const FVector2f& PositionCm) const
{
	const float Epsilon = Settings.Grid.CellSizeCm * 0.5f;
	const FVector2f Dx(Epsilon, 0.0f);
	const FVector2f Dy(0.0f, Epsilon);
	const FVector2f Ddx = (SamplePeriodicVelocity(PositionCm + Dx) - SamplePeriodicVelocity(PositionCm - Dx)) / (2.0f * Epsilon);
	const FVector2f Ddy = (SamplePeriodicVelocity(PositionCm + Dy) - SamplePeriodicVelocity(PositionCm - Dy)) / (2.0f * Epsilon);
	FFlowMapMatrix2f Result;
	Result.M00 = Ddx.X;
	Result.M01 = Ddy.X;
	Result.M10 = Ddx.Y;
	Result.M11 = Ddy.Y;
	return Result;
}

bool FFlowMapImpulseVortexSimulation2D::DepositCurrentImpulse(FString* OutFailureReason)
{
	const int32 Nx = Settings.Grid.CellCounts.X;
	const int32 Ny = Settings.Grid.CellCounts.Y;
	const float H = Settings.Grid.CellSizeCm;
	VelocityGrid.FaceU.Init(0.0f, (Nx + 1) * Ny);
	VelocityGrid.FaceV.Init(0.0f, Nx * (Ny + 1));
	TArray<float> CompactU;
	TArray<float> CompactV;
	CompactU.Init(0.0f, Nx * Ny);
	CompactV.Init(0.0f, Nx * Ny);
	TArray<float> WeightU;
	TArray<float> WeightV;
	WeightU.Init(0.0f, Nx * Ny);
	WeightV.Init(0.0f, Nx * Ny);
	for (const FImpulseParticle& Particle : Particles)
	{
		const FVector2f Impulse(
			Particle.BackwardMapJacobian.M00 * Particle.ReferenceImpulseCmPerSecond.X + Particle.BackwardMapJacobian.M10 * Particle.ReferenceImpulseCmPerSecond.Y,
			Particle.BackwardMapJacobian.M01 * Particle.ReferenceImpulseCmPerSecond.X + Particle.BackwardMapJacobian.M11 * Particle.ReferenceImpulseCmPerSecond.Y);
		const FFlowMapMatrix2f G0B = MultiplyImpulseMatrices(Particle.ReferenceImpulseGradientPerSecond, Particle.BackwardMapJacobian);
		FFlowMapMatrix2f CurrentGradient;
		CurrentGradient.M00 = Particle.BackwardMapJacobian.M00 * G0B.M00 + Particle.BackwardMapJacobian.M10 * G0B.M10;
		CurrentGradient.M01 = Particle.BackwardMapJacobian.M00 * G0B.M01 + Particle.BackwardMapJacobian.M10 * G0B.M11;
		CurrentGradient.M10 = Particle.BackwardMapJacobian.M01 * G0B.M00 + Particle.BackwardMapJacobian.M11 * G0B.M10;
		CurrentGradient.M11 = Particle.BackwardMapJacobian.M01 * G0B.M01 + Particle.BackwardMapJacobian.M11 * G0B.M11;
		if (!FMath::IsFinite(Impulse.X) || !FMath::IsFinite(Impulse.Y))
		{
			if (OutFailureReason) *OutFailureReason = TEXT("Impulse vortex 粒子 impulse 包含非有限值。 ");
			return false;
		}
		const FVector2f Grid = (Particle.PositionCm - Settings.Grid.OriginCm) / H;
		const bool bUseGradient = Mode == EFlowMapImpulseMode2D::LongMapGradient;
		// U faces are integer X / half Y; V faces are half X / integer Y.
		auto DepositFace = [Nx, Ny, H](TArray<float>& Values, TArray<float>& Weights, float X, float Y, float Value, const FVector2f& Gradient, const FVector2f& ParticlePosition, const FVector2f& Origin, bool bUseGradient, bool bUFace)
		{
			const int32 X0 = FMath::FloorToInt(X);
			const int32 Y0 = FMath::FloorToInt(Y);
			const float Tx = X - X0;
			const float Ty = Y - Y0;
			for (int32 Oy = 0; Oy <= 1; ++Oy) for (int32 Ox = 0; Ox <= 1; ++Ox)
			{
				const int32 LogicalX = X0 + Ox;
				const int32 LogicalY = Y0 + Oy;
				const float Weight = (Ox ? Tx : 1.0f - Tx) * (Oy ? Ty : 1.0f - Ty);
				const int32 Index = WrapImpulseIndex(LogicalY, Ny) * Nx + WrapImpulseIndex(LogicalX, Nx);
				const FVector2f FacePosition = Origin + (bUFace ? FVector2f(LogicalX * H, (LogicalY + 0.5f) * H) : FVector2f((LogicalX + 0.5f) * H, LogicalY * H));
				const float Predicted = bUseGradient ? Value + FVector2f::DotProduct(Gradient, FacePosition - ParticlePosition) : Value;
				Values[Index] += Weight * Predicted;
				Weights[Index] += Weight;
			}
		};
		DepositFace(CompactU, WeightU, Grid.X, Grid.Y - 0.5f, Impulse.X, FVector2f(CurrentGradient.M00, CurrentGradient.M01), Particle.PositionCm, Settings.Grid.OriginCm, bUseGradient, true);
		DepositFace(CompactV, WeightV, Grid.X - 0.5f, Grid.Y, Impulse.Y, FVector2f(CurrentGradient.M10, CurrentGradient.M11), Particle.PositionCm, Settings.Grid.OriginCm, bUseGradient, false);
	}
	for (int32 Y = 0; Y < Ny; ++Y) for (int32 X = 0; X < Nx; ++X)
		VelocityGrid.FaceU[Y * (Nx + 1) + X] = WeightU[Y * Nx + X] > UE_SMALL_NUMBER ? CompactU[Y * Nx + X] / WeightU[Y * Nx + X] : 0.0f;
	for (int32 Y = 0; Y < Ny; ++Y) VelocityGrid.FaceU[Y * (Nx + 1) + Nx] = VelocityGrid.FaceU[Y * (Nx + 1)];
	for (int32 Y = 0; Y < Ny; ++Y) for (int32 X = 0; X < Nx; ++X)
		VelocityGrid.FaceV[Y * Nx + X] = WeightV[Y * Nx + X] > UE_SMALL_NUMBER ? CompactV[Y * Nx + X] / WeightV[Y * Nx + X] : 0.0f;
	for (int32 X = 0; X < Nx; ++X) VelocityGrid.FaceV[Ny * Nx + X] = VelocityGrid.FaceV[X];
	return true;
}

void FFlowMapImpulseVortexSimulation2D::ResetParticleMaps()
{
	for (FImpulseParticle& Particle : Particles)
	{
		Particle.ReferenceImpulseCmPerSecond = SamplePeriodicVelocity(Particle.PositionCm);
		Particle.ReferenceImpulseGradientPerSecond = SamplePeriodicVelocityGradient(Particle.PositionCm);
		Particle.BackwardMapJacobian = FFlowMapMatrix2f();
	}
}

void FFlowMapImpulseVortexSimulation2D::UpdateMetrics()
{
	const int32 Nx = Settings.Grid.CellCounts.X;
	const int32 Ny = Settings.Grid.CellCounts.Y;
	const float H = Settings.Grid.CellSizeCm;
	double ErrorSquares = 0.0;
	double ExactSquares = 0.0;
	for (int32 Y = 0; Y < Ny; ++Y) for (int32 X = 0; X < Nx; ++X)
	{
		const FVector2f P = Settings.Grid.OriginCm + FVector2f((X + 0.5f) * H, (Y + 0.5f) * H);
		const FVector2f Error = VelocityGrid.SampleVelocity(P) - EvaluateTaylorGreenVelocity(P);
		const FVector2f Exact = EvaluateTaylorGreenVelocity(P);
		ErrorSquares += Error.SquaredLength();
		ExactSquares += Exact.SquaredLength();
	}
	Metrics.VelocityRmsErrorCmPerSecond = FMath::Sqrt(ErrorSquares / (Nx * Ny));
	Metrics.RelativeVelocityRmsError = FMath::Sqrt(ErrorSquares / FMath::Max(ExactSquares, UE_DOUBLE_SMALL_NUMBER));
	Metrics.DivergenceRms = VelocityGrid.CalculateDivergenceRms();
	Metrics.MaximumAbsoluteDivergence = VelocityGrid.CalculateMaximumAbsoluteDivergence();
	Metrics.KineticEnergyProxy = VelocityGrid.CalculateKineticEnergyProxy();
}
