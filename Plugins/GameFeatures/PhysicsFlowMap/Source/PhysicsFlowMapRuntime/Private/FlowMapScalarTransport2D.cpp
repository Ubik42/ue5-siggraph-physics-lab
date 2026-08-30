#include "FlowMapScalarTransport2D.h"

namespace
{
	float WrapCoordinate(float Value, float Minimum, float Extent)
	{
		return Minimum + FMath::Fmod(FMath::Fmod(Value - Minimum, Extent) + Extent, Extent);
	}

	int32 WrapIndex(int32 Value, int32 Count)
	{
		return (Value % Count + Count) % Count;
	}
}

bool FFlowMapScalarTransportSimulation2D::Initialize(const FFlowMapScalarTransportSettings2D& InSettings, EFlowMapScalarTransportMode2D InMode, FString* OutFailureReason)
{
	if (InSettings.Grid.CellCounts.X < 4 || InSettings.Grid.CellCounts.Y < 4 || InSettings.Grid.CellSizeCm <= 0.0f ||
		InSettings.ParticlesPerCellAxis < 1 || InSettings.ParticlesPerCellAxis > 8 || InSettings.FixedDeltaSeconds <= 0.0f ||
		InSettings.LongMapResetSteps < 1 || InSettings.FieldCyclesX < 1 || InSettings.FieldCyclesY < 1)
	{
		if (OutFailureReason) *OutFailureReason = TEXT("被动标量 Flow Map 设置无效。网格、粒子密度、固定步、重置周期和场频率必须为正。 ");
		return false;
	}
	Settings = InSettings;
	Mode = InMode;
	Metrics = FFlowMapScalarTransportMetrics2D();
	Particles.Reset();
	const int32 Nx = Settings.Grid.CellCounts.X;
	const int32 Ny = Settings.Grid.CellCounts.Y;
	const int32 S = Settings.ParticlesPerCellAxis;
	const float H = Settings.Grid.CellSizeCm;
	Particles.Reserve(Nx * Ny * S * S);
	for (int32 Y = 0; Y < Ny; ++Y)
		for (int32 X = 0; X < Nx; ++X)
			for (int32 Py = 0; Py < S; ++Py)
				for (int32 Px = 0; Px < S; ++Px)
				{
					FFlowMapScalarParticle2D Particle;
					Particle.PositionCm = Settings.Grid.OriginCm + FVector2f((X + (Px + 0.5f) / S) * H, (Y + (Py + 0.5f) / S) * H);
					Particle.Value = EvaluateExact(Particle.PositionCm, 0.0f);
					Particle.GradientPerCm = EvaluateExactGradient(Particle.PositionCm, 0.0f);
					Particles.Add(Particle);
				}
	Metrics.ParticleCount = Particles.Num();
	if (!FFlowMapGradientTransfer2D::TransferScalarToPeriodicCellGrid(Particles, Settings.Grid, Mode == EFlowMapScalarTransportMode2D::LongMapGradient, CellScalar, OutFailureReason)) return false;
	UpdateMetrics();
	return true;
}

bool FFlowMapScalarTransportSimulation2D::Step(FString* OutFailureReason)
{
	if (Particles.IsEmpty())
	{
		if (OutFailureReason) *OutFailureReason = TEXT("被动标量 Flow Map 会话尚未初始化。 ");
		return false;
	}
	const FVector2f Min = Settings.Grid.OriginCm;
	const FVector2f Extent(Settings.Grid.CellCounts.X * Settings.Grid.CellSizeCm, Settings.Grid.CellCounts.Y * Settings.Grid.CellSizeCm);
	for (FFlowMapScalarParticle2D& Particle : Particles)
	{
		Particle.PositionCm += Settings.UniformVelocityCmPerSecond * Settings.FixedDeltaSeconds;
		Particle.PositionCm.X = WrapCoordinate(Particle.PositionCm.X, Min.X, Extent.X);
		Particle.PositionCm.Y = WrapCoordinate(Particle.PositionCm.Y, Min.Y, Extent.Y);
	}
	++Metrics.StepsExecuted;
	if (!FFlowMapGradientTransfer2D::TransferScalarToPeriodicCellGrid(Particles, Settings.Grid, Mode == EFlowMapScalarTransportMode2D::LongMapGradient, CellScalar, OutFailureReason)) return false;
	UpdateMetrics();
	const bool bReset = Mode == EFlowMapScalarTransportMode2D::ShortPic || Metrics.StepsExecuted % Settings.LongMapResetSteps == 0;
	if (bReset)
	{
		ResampleParticlesFromGrid();
		++Metrics.MapResets;
	}
	return true;
}

float FFlowMapScalarTransportSimulation2D::EvaluateExact(const FVector2f& PositionCm, float TimeSeconds) const
{
	const float Lx = Settings.Grid.CellCounts.X * Settings.Grid.CellSizeCm;
	const float Ly = Settings.Grid.CellCounts.Y * Settings.Grid.CellSizeCm;
	const float X = PositionCm.X - Settings.Grid.OriginCm.X - Settings.UniformVelocityCmPerSecond.X * TimeSeconds;
	const float Y = PositionCm.Y - Settings.Grid.OriginCm.Y - Settings.UniformVelocityCmPerSecond.Y * TimeSeconds;
	const float Kx = 2.0f * PI * Settings.FieldCyclesX / Lx;
	const float Ky = 2.0f * PI * Settings.FieldCyclesY / Ly;
	return FMath::Sin(Kx * X) * FMath::Cos(Ky * Y);
}

FVector2f FFlowMapScalarTransportSimulation2D::EvaluateExactGradient(const FVector2f& PositionCm, float TimeSeconds) const
{
	const float Lx = Settings.Grid.CellCounts.X * Settings.Grid.CellSizeCm;
	const float Ly = Settings.Grid.CellCounts.Y * Settings.Grid.CellSizeCm;
	const float X = PositionCm.X - Settings.Grid.OriginCm.X - Settings.UniformVelocityCmPerSecond.X * TimeSeconds;
	const float Y = PositionCm.Y - Settings.Grid.OriginCm.Y - Settings.UniformVelocityCmPerSecond.Y * TimeSeconds;
	const float Kx = 2.0f * PI * Settings.FieldCyclesX / Lx;
	const float Ky = 2.0f * PI * Settings.FieldCyclesY / Ly;
	return FVector2f(Kx * FMath::Cos(Kx * X) * FMath::Cos(Ky * Y), -Ky * FMath::Sin(Kx * X) * FMath::Sin(Ky * Y));
}

float FFlowMapScalarTransportSimulation2D::SamplePeriodicScalar(const FVector2f& PositionCm) const
{
	const int32 Nx = Settings.Grid.CellCounts.X;
	const int32 Ny = Settings.Grid.CellCounts.Y;
	const FVector2f Grid = (PositionCm - Settings.Grid.OriginCm) / Settings.Grid.CellSizeCm - FVector2f(0.5f, 0.5f);
	const int32 X0 = FMath::FloorToInt(Grid.X);
	const int32 Y0 = FMath::FloorToInt(Grid.Y);
	const float Tx = Grid.X - X0;
	const float Ty = Grid.Y - Y0;
	const float A = FMath::Lerp(CellScalar[WrapIndex(Y0, Ny) * Nx + WrapIndex(X0, Nx)], CellScalar[WrapIndex(Y0, Ny) * Nx + WrapIndex(X0 + 1, Nx)], Tx);
	const float B = FMath::Lerp(CellScalar[WrapIndex(Y0 + 1, Ny) * Nx + WrapIndex(X0, Nx)], CellScalar[WrapIndex(Y0 + 1, Ny) * Nx + WrapIndex(X0 + 1, Nx)], Tx);
	return FMath::Lerp(A, B, Ty);
}

FVector2f FFlowMapScalarTransportSimulation2D::SamplePeriodicGradient(const FVector2f& PositionCm) const
{
	const float H = Settings.Grid.CellSizeCm;
	return FVector2f(
		(SamplePeriodicScalar(PositionCm + FVector2f(H, 0.0f)) - SamplePeriodicScalar(PositionCm - FVector2f(H, 0.0f))) / (2.0f * H),
		(SamplePeriodicScalar(PositionCm + FVector2f(0.0f, H)) - SamplePeriodicScalar(PositionCm - FVector2f(0.0f, H))) / (2.0f * H));
}

void FFlowMapScalarTransportSimulation2D::UpdateMetrics()
{
	const int32 Nx = Settings.Grid.CellCounts.X;
	const int32 Ny = Settings.Grid.CellCounts.Y;
	const float H = Settings.Grid.CellSizeCm;
	const float Time = Metrics.StepsExecuted * Settings.FixedDeltaSeconds;
	double ErrorSquares = 0.0;
	double ExactSquares = 0.0;
	double ScalarSquares = 0.0;
	float MaximumError = 0.0f;
	float Amplitude = 0.0f;
	float ExactAmplitude = 0.0f;
	for (int32 Y = 0; Y < Ny; ++Y)
		for (int32 X = 0; X < Nx; ++X)
		{
			const int32 Index = Y * Nx + X;
			const FVector2f Center = Settings.Grid.OriginCm + FVector2f((X + 0.5f) * H, (Y + 0.5f) * H);
			const float Exact = EvaluateExact(Center, Time);
			const float Error = CellScalar[Index] - Exact;
			ErrorSquares += static_cast<double>(Error) * Error;
			ExactSquares += static_cast<double>(Exact) * Exact;
			ScalarSquares += static_cast<double>(CellScalar[Index]) * CellScalar[Index];
			MaximumError = FMath::Max(MaximumError, FMath::Abs(Error));
			Amplitude = FMath::Max(Amplitude, FMath::Abs(CellScalar[Index]));
			ExactAmplitude = FMath::Max(ExactAmplitude, FMath::Abs(Exact));
		}
	const double Count = static_cast<double>(Nx * Ny);
	Metrics.RmsError = FMath::Sqrt(ErrorSquares / Count);
	Metrics.RelativeRmsError = FMath::Sqrt(ErrorSquares / FMath::Max(ExactSquares, UE_DOUBLE_SMALL_NUMBER));
	Metrics.MaximumAbsoluteError = MaximumError;
	Metrics.ScalarAmplitude = Amplitude;
	Metrics.ExactAmplitude = ExactAmplitude;
	Metrics.ScalarEnergyProxy = ScalarSquares / Count;
	Metrics.ExactEnergyProxy = ExactSquares / Count;
}

void FFlowMapScalarTransportSimulation2D::ResampleParticlesFromGrid()
{
	for (FFlowMapScalarParticle2D& Particle : Particles)
	{
		Particle.Value = SamplePeriodicScalar(Particle.PositionCm);
		Particle.GradientPerCm = SamplePeriodicGradient(Particle.PositionCm);
	}
}
