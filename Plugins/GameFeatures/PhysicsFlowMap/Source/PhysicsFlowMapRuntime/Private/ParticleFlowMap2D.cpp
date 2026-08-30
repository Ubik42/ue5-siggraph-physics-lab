#include "ParticleFlowMap2D.h"

int32 FFlowMapSolidBoundaryRaster2D::CountSolidCells() const
{
	int32 Count = 0;
	for (uint32 Value : SolidCellMask) Count += Value != 0 ? 1 : 0;
	return Count;
}

bool FFlowMapSolidBoundaryRasterizer2D::BuildCircleFromRigidBodyTransforms(
	const FTransform& PreviousTransform,
	const FTransform& CurrentTransform,
	float RadiusCm,
	float FixedDeltaSeconds,
	FFlowMapKinematicCircle2D& OutCircle,
	FString* OutFailureReason)
{
	if (RadiusCm <= 0.0f || FixedDeltaSeconds <= 0.0f || PreviousTransform.ContainsNaN() || CurrentTransform.ContainsNaN())
	{
		if (OutFailureReason) *OutFailureReason = TEXT("运动圆障碍需要有限 Transform、正半径和正固定步。");
		return false;
	}
	const FVector PreviousLocation = PreviousTransform.GetLocation();
	const FVector CurrentLocation = CurrentTransform.GetLocation();
	OutCircle.CenterCm = FVector2f(CurrentLocation.X, CurrentLocation.Y);
	OutCircle.RadiusCm = RadiusCm;
	OutCircle.LinearVelocityCmPerSecond = FVector2f(CurrentLocation.X - PreviousLocation.X, CurrentLocation.Y - PreviousLocation.Y) / FixedDeltaSeconds;
	const float DeltaYawDegrees = FMath::FindDeltaAngleDegrees(PreviousTransform.Rotator().Yaw, CurrentTransform.Rotator().Yaw);
	OutCircle.AngularVelocityRadiansPerSecond = FMath::DegreesToRadians(DeltaYawDegrees) / FixedDeltaSeconds;
	return true;
}

bool FFlowMapSolidBoundaryRasterizer2D::RasterizeCircle(
	const FFlowMapGridSettings2D& Grid,
	const FFlowMapKinematicCircle2D& Circle,
	FFlowMapSolidBoundaryRaster2D& OutRaster,
	FString* OutFailureReason)
{
	const int32 Nx = Grid.CellCounts.X, Ny = Grid.CellCounts.Y;
	if (Nx < 1 || Ny < 1 || Grid.CellSizeCm <= 0.0f || Circle.RadiusCm <= 0.0f
		|| !FMath::IsFinite(Circle.CenterCm.X) || !FMath::IsFinite(Circle.CenterCm.Y)
		|| !FMath::IsFinite(Circle.LinearVelocityCmPerSecond.X) || !FMath::IsFinite(Circle.LinearVelocityCmPerSecond.Y)
		|| !FMath::IsFinite(Circle.AngularVelocityRadiansPerSecond))
	{
		if (OutFailureReason) *OutFailureReason = TEXT("MAC 障碍栅格需要有效网格和有限运动圆参数。");
		return false;
	}
	OutRaster = FFlowMapSolidBoundaryRaster2D();
	OutRaster.Grid = Grid;
	OutRaster.SolidCellMask.Init(0, Nx * Ny);
	OutRaster.SolidFaceUMask.Init(0, (Nx + 1) * Ny);
	OutRaster.SolidFaceVMask.Init(0, Nx * (Ny + 1));
	OutRaster.SolidFaceVelocityU.Init(0.0f, (Nx + 1) * Ny);
	OutRaster.SolidFaceVelocityV.Init(0.0f, Nx * (Ny + 1));
	const float RadiusSquared = FMath::Square(Circle.RadiusCm);
	auto IsSolid = [&Circle, RadiusSquared](const FVector2f& Position) { return FVector2f::DistSquared(Position, Circle.CenterCm) <= RadiusSquared; };
	for (int32 Y = 0; Y < Ny; ++Y) for (int32 X = 0; X < Nx; ++X)
	{
		const FVector2f Position = Grid.OriginCm + FVector2f((X + 0.5f) * Grid.CellSizeCm, (Y + 0.5f) * Grid.CellSizeCm);
		OutRaster.SolidCellMask[Y * Nx + X] = IsSolid(Position) ? 1u : 0u;
	}
	for (int32 Y = 0; Y < Ny; ++Y) for (int32 X = 0; X <= Nx; ++X)
	{
		const int32 Index = Y * (Nx + 1) + X;
		const FVector2f Position = Grid.OriginCm + FVector2f(X * Grid.CellSizeCm, (Y + 0.5f) * Grid.CellSizeCm);
		const bool bLeftCellSolid = X > 0 && OutRaster.SolidCellMask[Y * Nx + X - 1] != 0;
		const bool bRightCellSolid = X < Nx && OutRaster.SolidCellMask[Y * Nx + X] != 0;
		if (IsSolid(Position) || bLeftCellSolid || bRightCellSolid)
		{
			OutRaster.SolidFaceUMask[Index] = 1u;
			OutRaster.SolidFaceVelocityU[Index] = Circle.LinearVelocityCmPerSecond.X - Circle.AngularVelocityRadiansPerSecond * (Position.Y - Circle.CenterCm.Y);
		}
	}
	for (int32 Y = 0; Y <= Ny; ++Y) for (int32 X = 0; X < Nx; ++X)
	{
		const int32 Index = Y * Nx + X;
		const FVector2f Position = Grid.OriginCm + FVector2f((X + 0.5f) * Grid.CellSizeCm, Y * Grid.CellSizeCm);
		const bool bDownCellSolid = Y > 0 && OutRaster.SolidCellMask[(Y - 1) * Nx + X] != 0;
		const bool bUpCellSolid = Y < Ny && OutRaster.SolidCellMask[Y * Nx + X] != 0;
		if (IsSolid(Position) || bDownCellSolid || bUpCellSolid)
		{
			OutRaster.SolidFaceVMask[Index] = 1u;
			OutRaster.SolidFaceVelocityV[Index] = Circle.LinearVelocityCmPerSecond.Y + Circle.AngularVelocityRadiansPerSecond * (Position.X - Circle.CenterCm.X);
		}
	}
	return true;
}

namespace
{
	void SetFailure(FString* OutFailureReason, const TCHAR* Message)
	{
		if (OutFailureReason) *OutFailureReason = Message;
	}

	float Bilinear(const TArray<float>& Values, int32 Width, int32 Height, float X, float Y)
	{
		const float ClampedX = FMath::Clamp(X, 0.0f, static_cast<float>(Width - 1));
		const float ClampedY = FMath::Clamp(Y, 0.0f, static_cast<float>(Height - 1));
		const int32 X0 = FMath::FloorToInt(ClampedX);
		const int32 Y0 = FMath::FloorToInt(ClampedY);
		const int32 X1 = FMath::Min(X0 + 1, Width - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, Height - 1);
		const float Tx = ClampedX - static_cast<float>(X0);
		const float Ty = ClampedY - static_cast<float>(Y0);
		const float A = FMath::Lerp(Values[Y0 * Width + X0], Values[Y0 * Width + X1], Tx);
		const float B = FMath::Lerp(Values[Y1 * Width + X0], Values[Y1 * Width + X1], Tx);
		return FMath::Lerp(A, B, Ty);
	}

	FFlowMapMatrix2f Multiply(const FFlowMapMatrix2f& A, const FFlowMapMatrix2f& B)
	{
		FFlowMapMatrix2f Result;
		Result.M00 = A.M00 * B.M00 + A.M01 * B.M10;
		Result.M01 = A.M00 * B.M01 + A.M01 * B.M11;
		Result.M10 = A.M10 * B.M00 + A.M11 * B.M10;
		Result.M11 = A.M10 * B.M01 + A.M11 * B.M11;
		return Result;
	}
}

bool FFlowMapVelocityGrid2D::Initialize(const FFlowMapGridSettings2D& InSettings, FString* OutFailureReason)
{
	if (InSettings.CellCounts.X < 2 || InSettings.CellCounts.Y < 2 || !FMath::IsFinite(InSettings.CellSizeCm) || InSettings.CellSizeCm <= 0.0f)
	{
		SetFailure(OutFailureReason, TEXT("Flow Map 网格至少需要 2x2 单元且单元尺寸必须为正有限值。"));
		return false;
	}
	Settings = InSettings;
	FaceU.Init(0.0f, (Settings.CellCounts.X + 1) * Settings.CellCounts.Y);
	FaceV.Init(0.0f, Settings.CellCounts.X * (Settings.CellCounts.Y + 1));
	return true;
}

bool FFlowMapVelocityGrid2D::IsValid(FString* OutFailureReason) const
{
	if (FaceU.Num() != (Settings.CellCounts.X + 1) * Settings.CellCounts.Y || FaceV.Num() != Settings.CellCounts.X * (Settings.CellCounts.Y + 1))
	{
		SetFailure(OutFailureReason, TEXT("MAC 面速度数组尺寸与网格合同不一致。"));
		return false;
	}
	for (float Value : FaceU) if (!FMath::IsFinite(Value)) { SetFailure(OutFailureReason, TEXT("U 面速度包含非有限值。")); return false; }
	for (float Value : FaceV) if (!FMath::IsFinite(Value)) { SetFailure(OutFailureReason, TEXT("V 面速度包含非有限值。")); return false; }
	return true;
}

FVector2f FFlowMapVelocityGrid2D::GetMaximumCm() const
{
	return Settings.OriginCm + FVector2f(Settings.CellCounts.X * Settings.CellSizeCm, Settings.CellCounts.Y * Settings.CellSizeCm);
}

FVector2f FFlowMapVelocityGrid2D::SampleVelocity(const FVector2f& PositionCm) const
{
	const FVector2f Grid = (PositionCm - Settings.OriginCm) / Settings.CellSizeCm;
	const float U = Bilinear(FaceU, Settings.CellCounts.X + 1, Settings.CellCounts.Y, Grid.X, Grid.Y - 0.5f);
	const float V = Bilinear(FaceV, Settings.CellCounts.X, Settings.CellCounts.Y + 1, Grid.X - 0.5f, Grid.Y);
	return FVector2f(U, V);
}

FFlowMapMatrix2f FFlowMapVelocityGrid2D::SampleVelocityGradient(const FVector2f& PositionCm) const
{
	const float Epsilon = Settings.CellSizeCm * 0.5f;
	const FVector2f Dx(Epsilon, 0.0f);
	const FVector2f Dy(0.0f, Epsilon);
	const FVector2f Ddx = (SampleVelocity(PositionCm + Dx) - SampleVelocity(PositionCm - Dx)) / (2.0f * Epsilon);
	const FVector2f Ddy = (SampleVelocity(PositionCm + Dy) - SampleVelocity(PositionCm - Dy)) / (2.0f * Epsilon);
	FFlowMapMatrix2f Result;
	Result.M00 = Ddx.X;
	Result.M01 = Ddy.X;
	Result.M10 = Ddx.Y;
	Result.M11 = Ddy.Y;
	return Result;
}

float FFlowMapVelocityGrid2D::CalculateCellDivergence(int32 X, int32 Y) const
{
	check(X >= 0 && X < Settings.CellCounts.X && Y >= 0 && Y < Settings.CellCounts.Y);
	const int32 UWidth = Settings.CellCounts.X + 1;
	const int32 VWidth = Settings.CellCounts.X;
	return (FaceU[Y * UWidth + X + 1] - FaceU[Y * UWidth + X] + FaceV[(Y + 1) * VWidth + X] - FaceV[Y * VWidth + X]) / Settings.CellSizeCm;
}

double FFlowMapVelocityGrid2D::CalculateDivergenceRms() const
{
	double SumSquares = 0.0;
	const int32 Count = Settings.CellCounts.X * Settings.CellCounts.Y;
	for (int32 Y = 0; Y < Settings.CellCounts.Y; ++Y)
		for (int32 X = 0; X < Settings.CellCounts.X; ++X)
			SumSquares += FMath::Square(static_cast<double>(CalculateCellDivergence(X, Y)));
	return Count > 0 ? FMath::Sqrt(SumSquares / Count) : 0.0;
}

float FFlowMapVelocityGrid2D::CalculateMaximumAbsoluteDivergence() const
{
	float Maximum = 0.0f;
	for (int32 Y = 0; Y < Settings.CellCounts.Y; ++Y)
		for (int32 X = 0; X < Settings.CellCounts.X; ++X)
			Maximum = FMath::Max(Maximum, FMath::Abs(CalculateCellDivergence(X, Y)));
	return Maximum;
}

double FFlowMapVelocityGrid2D::CalculateKineticEnergyProxy() const
{
	double Sum = 0.0;
	int32 Count = 0;
	for (int32 Y = 0; Y < Settings.CellCounts.Y; ++Y)
		for (int32 X = 0; X < Settings.CellCounts.X; ++X)
		{
			const FVector2f Center = Settings.OriginCm + FVector2f((X + 0.5f) * Settings.CellSizeCm, (Y + 0.5f) * Settings.CellSizeCm);
			Sum += 0.5 * static_cast<double>(SampleVelocity(Center).SquaredLength());
			++Count;
		}
	return Count > 0 ? Sum / Count : 0.0;
}

void FFlowMapVelocityGrid2D::SetUniformVelocity(const FVector2f& VelocityCmPerSecond)
{
	for (float& Value : FaceU) Value = VelocityCmPerSecond.X;
	for (float& Value : FaceV) Value = VelocityCmPerSecond.Y;
}

void FFlowMapVelocityGrid2D::SetSolidBodyVortex(const FVector2f& CenterCm, float AngularSpeedRadiansPerSecond)
{
	const float H = Settings.CellSizeCm;
	const int32 Nx = Settings.CellCounts.X;
	const int32 Ny = Settings.CellCounts.Y;
	for (int32 Y = 0; Y < Ny; ++Y)
		for (int32 X = 0; X <= Nx; ++X)
		{
			const FVector2f P = Settings.OriginCm + FVector2f(X * H, (Y + 0.5f) * H);
			FaceU[Y * (Nx + 1) + X] = -AngularSpeedRadiansPerSecond * (P.Y - CenterCm.Y);
		}
	for (int32 Y = 0; Y <= Ny; ++Y)
		for (int32 X = 0; X < Nx; ++X)
		{
			const FVector2f P = Settings.OriginCm + FVector2f((X + 0.5f) * H, Y * H);
			FaceV[Y * Nx + X] = AngularSpeedRadiansPerSecond * (P.X - CenterCm.X);
		}
}

void FFlowMapVelocityGrid2D::SetPeriodicCompressionWave(float SpeedAmplitudeCmPerSecond)
{
	const int32 Nx = Settings.CellCounts.X;
	const int32 Ny = Settings.CellCounts.Y;
	const float TwoPi = 2.0f * PI;
	for (int32 Y = 0; Y < Ny; ++Y)
		for (int32 X = 0; X <= Nx; ++X)
			FaceU[Y * (Nx + 1) + X] = SpeedAmplitudeCmPerSecond * FMath::Sin(TwoPi * static_cast<float>(X) / static_cast<float>(Nx));
	for (float& Value : FaceV) Value = 0.0f;
}

bool FFlowMapPressureProjector2D::ProjectPeriodic(FFlowMapVelocityGrid2D& InOutGrid, int32 Iterations, FFlowMapPressureProjectionMetrics2D& OutMetrics, FString* OutFailureReason)
{
	if (!InOutGrid.IsValid(OutFailureReason) || Iterations < 1)
	{
		if (Iterations < 1) SetFailure(OutFailureReason, TEXT("压力投影至少需要一次 Jacobi 迭代。"));
		return false;
	}
	const int32 Nx = InOutGrid.Settings.CellCounts.X;
	const int32 Ny = InOutGrid.Settings.CellCounts.Y;
	const float H = InOutGrid.Settings.CellSizeCm;
	TArray<float> Divergence;
	TArray<float> Pressure;
	TArray<float> NextPressure;
	Divergence.SetNumUninitialized(Nx * Ny);
	Pressure.Init(0.0f, Nx * Ny);
	NextPressure.Init(0.0f, Nx * Ny);
	OutMetrics = FFlowMapPressureProjectionMetrics2D();
	OutMetrics.DivergenceRmsBefore = InOutGrid.CalculateDivergenceRms();
	OutMetrics.MaximumDivergenceBefore = InOutGrid.CalculateMaximumAbsoluteDivergence();
	OutMetrics.Iterations = Iterations;
	for (int32 Y = 0; Y < Ny; ++Y)
		for (int32 X = 0; X < Nx; ++X)
			Divergence[Y * Nx + X] = InOutGrid.CalculateCellDivergence(X, Y);

	for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
	{
		for (int32 Y = 0; Y < Ny; ++Y)
			for (int32 X = 0; X < Nx; ++X)
			{
				const int32 Xm = (X + Nx - 1) % Nx;
				const int32 Xp = (X + 1) % Nx;
				const int32 Ym = (Y + Ny - 1) % Ny;
				const int32 Yp = (Y + 1) % Ny;
				NextPressure[Y * Nx + X] = 0.25f * (Pressure[Y * Nx + Xm] + Pressure[Y * Nx + Xp] + Pressure[Ym * Nx + X] + Pressure[Yp * Nx + X] - H * H * Divergence[Y * Nx + X]);
			}
		Swap(Pressure, NextPressure);
	}

	for (int32 Y = 0; Y < Ny; ++Y)
		for (int32 X = 0; X < Nx; ++X)
		{
			const int32 Left = (X + Nx - 1) % Nx;
			InOutGrid.FaceU[Y * (Nx + 1) + X] -= (Pressure[Y * Nx + X] - Pressure[Y * Nx + Left]) / H;
		}
	for (int32 Y = 0; Y < Ny; ++Y) InOutGrid.FaceU[Y * (Nx + 1) + Nx] = InOutGrid.FaceU[Y * (Nx + 1)];
	for (int32 Y = 0; Y < Ny; ++Y)
		for (int32 X = 0; X < Nx; ++X)
		{
			const int32 Down = (Y + Ny - 1) % Ny;
			InOutGrid.FaceV[Y * Nx + X] -= (Pressure[Y * Nx + X] - Pressure[Down * Nx + X]) / H;
		}
	for (int32 X = 0; X < Nx; ++X) InOutGrid.FaceV[Ny * Nx + X] = InOutGrid.FaceV[X];
	OutMetrics.DivergenceRmsAfter = InOutGrid.CalculateDivergenceRms();
	OutMetrics.MaximumDivergenceAfter = InOutGrid.CalculateMaximumAbsoluteDivergence();
	return FMath::IsFinite(OutMetrics.DivergenceRmsAfter);
}

bool FFlowMapParticleGridTransfer2D::TransferPicToGrid(const TArray<FParticleFlowMapState2D>& Particles, const FFlowMapGridSettings2D& GridSettings, FFlowMapVelocityGrid2D& OutGrid, FString* OutFailureReason)
{
	if (Particles.IsEmpty() || !OutGrid.Initialize(GridSettings, OutFailureReason))
	{
		if (Particles.IsEmpty()) SetFailure(OutFailureReason, TEXT("PIC 传输至少需要一个粒子。"));
		return false;
	}
	TArray<float> WeightU;
	TArray<float> WeightV;
	WeightU.Init(0.0f, OutGrid.FaceU.Num());
	WeightV.Init(0.0f, OutGrid.FaceV.Num());
	const int32 UWidth = GridSettings.CellCounts.X + 1;
	const int32 UHeight = GridSettings.CellCounts.Y;
	const int32 VWidth = GridSettings.CellCounts.X;
	const int32 VHeight = GridSettings.CellCounts.Y + 1;
	const float H = GridSettings.CellSizeCm;
	auto Deposit = [](TArray<float>& Values, TArray<float>& Weights, int32 Width, int32 Height, float X, float Y, float Value)
	{
		const int32 X0 = FMath::FloorToInt(X);
		const int32 Y0 = FMath::FloorToInt(Y);
		for (int32 Oy = 0; Oy <= 1; ++Oy)
			for (int32 Ox = 0; Ox <= 1; ++Ox)
			{
				const int32 Xi = FMath::Clamp(X0 + Ox, 0, Width - 1);
				const int32 Yi = FMath::Clamp(Y0 + Oy, 0, Height - 1);
				const float Weight = (Ox ? X - X0 : 1.0f - (X - X0)) * (Oy ? Y - Y0 : 1.0f - (Y - Y0));
				Values[Yi * Width + Xi] += Weight * Value;
				Weights[Yi * Width + Xi] += Weight;
			}
	};
	for (const FParticleFlowMapState2D& Particle : Particles)
	{
		if (!FMath::IsFinite(Particle.VelocityCmPerSecond.X) || !FMath::IsFinite(Particle.VelocityCmPerSecond.Y))
		{
			SetFailure(OutFailureReason, TEXT("PIC 粒子速度包含非有限值。"));
			return false;
		}
		const FVector2f Grid = (Particle.PositionCm - GridSettings.OriginCm) / H;
		Deposit(OutGrid.FaceU, WeightU, UWidth, UHeight, Grid.X, Grid.Y - 0.5f, Particle.VelocityCmPerSecond.X);
		Deposit(OutGrid.FaceV, WeightV, VWidth, VHeight, Grid.X - 0.5f, Grid.Y, Particle.VelocityCmPerSecond.Y);
	}
	for (int32 Index = 0; Index < OutGrid.FaceU.Num(); ++Index) if (WeightU[Index] > UE_SMALL_NUMBER) OutGrid.FaceU[Index] /= WeightU[Index];
	for (int32 Index = 0; Index < OutGrid.FaceV.Num(); ++Index) if (WeightV[Index] > UE_SMALL_NUMBER) OutGrid.FaceV[Index] /= WeightV[Index];
	return true;
}

bool FFlowMapGradientTransfer2D::TransferScalarToPeriodicCellGrid(const TArray<FFlowMapScalarParticle2D>& Particles, const FFlowMapGridSettings2D& GridSettings, bool bUseGradientTerm, TArray<float>& OutCellValues, FString* OutFailureReason)
{
	if (Particles.IsEmpty() || GridSettings.CellCounts.X < 2 || GridSettings.CellCounts.Y < 2 || GridSettings.CellSizeCm <= 0.0f)
	{
		SetFailure(OutFailureReason, TEXT("梯度 P2G 需要有效周期网格和非空粒子。"));
		return false;
	}
	const int32 Nx = GridSettings.CellCounts.X;
	const int32 Ny = GridSettings.CellCounts.Y;
	const float H = GridSettings.CellSizeCm;
	OutCellValues.Init(0.0f, Nx * Ny);
	TArray<float> Weights;
	Weights.Init(0.0f, Nx * Ny);
	for (const FFlowMapScalarParticle2D& Particle : Particles)
	{
		if (!FMath::IsFinite(Particle.Value) || !FMath::IsFinite(Particle.GradientPerCm.X) || !FMath::IsFinite(Particle.GradientPerCm.Y))
		{
			SetFailure(OutFailureReason, TEXT("梯度 P2G 粒子包含非有限值。"));
			return false;
		}
		const FVector2f Grid = (Particle.PositionCm - GridSettings.OriginCm) / H - FVector2f(0.5f, 0.5f);
		const int32 X0 = FMath::FloorToInt(Grid.X);
		const int32 Y0 = FMath::FloorToInt(Grid.Y);
		const float Tx = Grid.X - X0;
		const float Ty = Grid.Y - Y0;
		for (int32 Oy = 0; Oy <= 1; ++Oy)
			for (int32 Ox = 0; Ox <= 1; ++Ox)
			{
				const int32 LogicalX = X0 + Ox;
				const int32 LogicalY = Y0 + Oy;
				const int32 WrappedX = (LogicalX % Nx + Nx) % Nx;
				const int32 WrappedY = (LogicalY % Ny + Ny) % Ny;
				const float Weight = (Ox ? Tx : 1.0f - Tx) * (Oy ? Ty : 1.0f - Ty);
				const FVector2f LogicalCenter = GridSettings.OriginCm + FVector2f((LogicalX + 0.5f) * H, (LogicalY + 0.5f) * H);
				const float PredictedValue = bUseGradientTerm ? Particle.Value + FVector2f::DotProduct(Particle.GradientPerCm, LogicalCenter - Particle.PositionCm) : Particle.Value;
				const int32 Index = WrappedY * Nx + WrappedX;
				OutCellValues[Index] += Weight * PredictedValue;
				Weights[Index] += Weight;
			}
	}
	for (int32 Index = 0; Index < OutCellValues.Num(); ++Index)
	{
		if (Weights[Index] <= UE_SMALL_NUMBER)
		{
			SetFailure(OutFailureReason, TEXT("梯度 P2G 留下了未覆盖的周期网格单元。"));
			return false;
		}
		OutCellValues[Index] /= Weights[Index];
	}
	return true;
}

bool FParticleFlowMapSimulation2D::Initialize(const FFlowMapVelocityGrid2D& InVelocityGrid, const FParticleFlowMapSettings2D& InSettings, FString* OutFailureReason)
{
	if (!InVelocityGrid.IsValid(OutFailureReason)) return false;
	if (InSettings.ParticlesPerCellAxis < 1 || InSettings.ParticlesPerCellAxis > 8 || InSettings.LongMapResetSteps < 1 || InSettings.ShortMapResetSteps < 1 || InSettings.ShortMapResetSteps > InSettings.LongMapResetSteps || InSettings.MaximumMapDistortion <= 0.0f)
	{
		SetFailure(OutFailureReason, TEXT("粒子密度或长短 Flow Map 重置参数无效。"));
		return false;
	}
	VelocityGrid = InVelocityGrid;
	Settings = InSettings;
	Particles.Reset();
	Metrics = FParticleFlowMapMetrics2D();
	const float H = VelocityGrid.Settings.CellSizeCm;
	const int32 S = Settings.ParticlesPerCellAxis;
	Particles.Reserve(VelocityGrid.Settings.CellCounts.X * VelocityGrid.Settings.CellCounts.Y * S * S);
	for (int32 Y = 0; Y < VelocityGrid.Settings.CellCounts.Y; ++Y)
		for (int32 X = 0; X < VelocityGrid.Settings.CellCounts.X; ++X)
			for (int32 Py = 0; Py < S; ++Py)
				for (int32 Px = 0; Px < S; ++Px)
				{
					FParticleFlowMapState2D Particle;
					Particle.PositionCm = VelocityGrid.Settings.OriginCm + FVector2f((X + (Px + 0.5f) / S) * H, (Y + (Py + 0.5f) / S) * H);
					Particle.VelocityCmPerSecond = VelocityGrid.SampleVelocity(Particle.PositionCm);
					Particle.LongMapOriginCm = Particle.PositionCm;
					Particle.ShortMapOriginCm = Particle.PositionCm;
					Particles.Add(Particle);
				}
	Metrics.ParticleCount = Particles.Num();
	return true;
}

bool FParticleFlowMapSimulation2D::Step(float DeltaSeconds, FString* OutFailureReason)
{
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f || Particles.IsEmpty())
	{
		SetFailure(OutFailureReason, TEXT("Flow Map 固定步必须为正有限值且会话已经初始化。"));
		return false;
	}
	const FVector2f Min = VelocityGrid.GetMinimumCm();
	const FVector2f Max = VelocityGrid.GetMaximumCm();
	const FVector2f Extent = Max - Min;
	bool bDistortionReset = false;
	for (FParticleFlowMapState2D& Particle : Particles)
	{
		const FVector2f V0 = VelocityGrid.SampleVelocity(Particle.PositionCm);
		const FVector2f Mid = Particle.PositionCm + V0 * (0.5f * DeltaSeconds);
		const FVector2f Vmid = VelocityGrid.SampleVelocity(Mid);
		Particle.VelocityCmPerSecond = Vmid;
		const FVector2f Candidate = Particle.PositionCm + Vmid * DeltaSeconds;
		const bool bOutside = Candidate.X < Min.X || Candidate.X >= Max.X || Candidate.Y < Min.Y || Candidate.Y >= Max.Y;
		if (Settings.BoundaryMode == EFlowMapBoundaryMode2D::Periodic)
		{
			Particle.PositionCm.X = Min.X + FMath::Fmod(FMath::Fmod(Candidate.X - Min.X, Extent.X) + Extent.X, Extent.X);
			Particle.PositionCm.Y = Min.Y + FMath::Fmod(FMath::Fmod(Candidate.Y - Min.Y, Extent.Y) + Extent.Y, Extent.Y);
			if (bOutside) ++Metrics.BoundaryWraps;
		}
		else
		{
			if (bOutside) ++Metrics.LostParticles;
			Particle.PositionCm.X = FMath::Clamp(Candidate.X, Min.X, Max.X);
			Particle.PositionCm.Y = FMath::Clamp(Candidate.Y, Min.Y, Max.Y);
		}

		const FFlowMapMatrix2f Gradient = VelocityGrid.SampleVelocityGradient(Mid);
		FFlowMapMatrix2f BackwardStep;
		BackwardStep.M00 = 1.0f - DeltaSeconds * Gradient.M00;
		BackwardStep.M01 = -DeltaSeconds * Gradient.M01;
		BackwardStep.M10 = -DeltaSeconds * Gradient.M10;
		BackwardStep.M11 = 1.0f - DeltaSeconds * Gradient.M11;
		Particle.BackwardMapJacobian = Multiply(Particle.BackwardMapJacobian, BackwardStep);
		const float DeterminantError = FMath::Abs(Particle.BackwardMapJacobian.Determinant() - 1.0f);
		Metrics.MaximumJacobianDeterminantError = FMath::Max(Metrics.MaximumJacobianDeterminantError, DeterminantError);
		Metrics.MaximumLongMapDistanceCm = FMath::Max(Metrics.MaximumLongMapDistanceCm, FVector2f::Distance(Particle.PositionCm, Particle.LongMapOriginCm));
		bDistortionReset |= DeterminantError > Settings.MaximumMapDistortion;
	}
	++Metrics.StepsExecuted;
	if (Metrics.StepsExecuted % Settings.ShortMapResetSteps == 0) ResetShortMap();
	if (Metrics.StepsExecuted % Settings.LongMapResetSteps == 0 || bDistortionReset) ResetLongMap();
	return true;
}

void FParticleFlowMapSimulation2D::ResetLongMap()
{
	for (FParticleFlowMapState2D& Particle : Particles)
	{
		Particle.LongMapOriginCm = Particle.PositionCm;
		Particle.BackwardMapJacobian = FFlowMapMatrix2f();
	}
	++Metrics.LongMapResets;
}

void FParticleFlowMapSimulation2D::ResetShortMap()
{
	for (FParticleFlowMapState2D& Particle : Particles) Particle.ShortMapOriginCm = Particle.PositionCm;
	++Metrics.ShortMapResets;
}
