#if WITH_DEV_AUTOMATION_TESTS

#include "FlowMapGpuParticles.h"

#include "Misc/AutomationTest.h"
#include "RHI.h"

namespace FlowMapGpuParticleTest
{
	int32 WrapIndex(int32 Value, int32 Count) { return (Value % Count + Count) % Count; }
	float WrapCoordinate(float Value, float Minimum, float Extent)
	{
		if (Value >= Minimum && Value < Minimum + Extent) return Value;
		return Minimum + FMath::Fmod(FMath::Fmod(Value - Minimum, Extent) + Extent, Extent);
	}
	float SamplePeriodic(const TArray<float>& Values, int32 Stride, int32 Width, int32 Height, float X, float Y)
	{
		const int32 X0 = FMath::FloorToInt(X);
		const int32 Y0 = FMath::FloorToInt(Y);
		const float Tx = X - X0;
		const float Ty = Y - Y0;
		const float A = FMath::Lerp(Values[WrapIndex(Y0, Height) * Stride + WrapIndex(X0, Width)], Values[WrapIndex(Y0, Height) * Stride + WrapIndex(X0 + 1, Width)], Tx);
		const float B = FMath::Lerp(Values[WrapIndex(Y0 + 1, Height) * Stride + WrapIndex(X0, Width)], Values[WrapIndex(Y0 + 1, Height) * Stride + WrapIndex(X0 + 1, Width)], Tx);
		return FMath::Lerp(A, B, Ty);
	}
	FVector2f SampleVelocity(const FFlowMapGpuParticleInput& Input, const FVector2f& Position)
	{
		const FVector2f Grid = (Position - Input.OriginCm) / Input.CellSizeCm;
		return FVector2f(
			SamplePeriodic(Input.FaceU, Input.CellCounts.X + 1, Input.CellCounts.X, Input.CellCounts.Y, Grid.X, Grid.Y - 0.5f),
			SamplePeriodic(Input.FaceV, Input.CellCounts.X, Input.CellCounts.X, Input.CellCounts.Y, Grid.X - 0.5f, Grid.Y));
	}
	FVector4f SampleGradient(const FFlowMapGpuParticleInput& Input, const FVector2f& Position)
	{
		const float Epsilon = Input.CellSizeCm * 0.5f;
		const FVector2f Ddx = (SampleVelocity(Input, Position + FVector2f(Epsilon, 0.0f)) - SampleVelocity(Input, Position - FVector2f(Epsilon, 0.0f))) / (2.0f * Epsilon);
		const FVector2f Ddy = (SampleVelocity(Input, Position + FVector2f(0.0f, Epsilon)) - SampleVelocity(Input, Position - FVector2f(0.0f, Epsilon))) / (2.0f * Epsilon);
		return FVector4f(Ddx.X, Ddy.X, Ddx.Y, Ddy.Y);
	}
	void StepReference(const FFlowMapGpuParticleInput& Input, TArray<FFlowMapGpuParticleState>& Particles)
	{
		const FVector2f Extent(Input.CellCounts.X * Input.CellSizeCm, Input.CellCounts.Y * Input.CellSizeCm);
		for (FFlowMapGpuParticleState& Particle : Particles)
		{
			const FVector2f V0 = SampleVelocity(Input, Particle.PositionCm);
			FVector2f Mid = Particle.PositionCm + V0 * (0.5f * Input.FixedDeltaSeconds);
			Mid.X = WrapCoordinate(Mid.X, Input.OriginCm.X, Extent.X);
			Mid.Y = WrapCoordinate(Mid.Y, Input.OriginCm.Y, Extent.Y);
			const FVector2f Vmid = SampleVelocity(Input, Mid);
			Particle.PositionCm += Vmid * Input.FixedDeltaSeconds;
			Particle.PositionCm.X = WrapCoordinate(Particle.PositionCm.X, Input.OriginCm.X, Extent.X);
			Particle.PositionCm.Y = WrapCoordinate(Particle.PositionCm.Y, Input.OriginCm.Y, Extent.Y);
			const FVector4f G = SampleGradient(Input, Mid);
			const FVector4f S(1.0f - Input.FixedDeltaSeconds * G.X, -Input.FixedDeltaSeconds * G.Y,
				-Input.FixedDeltaSeconds * G.Z, 1.0f - Input.FixedDeltaSeconds * G.W);
			const FVector4f B = Particle.BackwardMapJacobian;
			Particle.BackwardMapJacobian = FVector4f(
				B.X * S.X + B.Y * S.Z, B.X * S.Y + B.Y * S.W,
				B.Z * S.X + B.W * S.Z, B.Z * S.Y + B.W * S.W);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlowMapGpuParticleAdvectionTest,
	"PhysicsResearch.FlowMap.GpuParticleAdvection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlowMapGpuParticleAdvectionTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过 Flow Map GPU 粒子平流；真实 D3D12 报告单独生成。"));
		return true;
	}
	FFlowMapGpuParticleInput Input;
	Input.CellCounts = FIntPoint(32, 32);
	Input.CellSizeCm = 5.0f;
	Input.OriginCm = FVector2f(-80.0f, -80.0f);
	Input.FixedDeltaSeconds = 1.0f / 240.0f;
	const int32 Nx = Input.CellCounts.X;
	const int32 Ny = Input.CellCounts.Y;
	const float H = Input.CellSizeCm;
	const float L = Nx * H;
	const float K = 2.0f * PI / L;
	constexpr float Amplitude = 20.0f;
	Input.FaceU.SetNumUninitialized((Nx + 1) * Ny);
	Input.FaceV.SetNumUninitialized(Nx * (Ny + 1));
	for (int32 Y = 0; Y < Ny; ++Y)
		for (int32 X = 0; X <= Nx; ++X)
		{
			const FVector2f P = Input.OriginCm + FVector2f(X * H, (Y + 0.5f) * H);
			Input.FaceU[Y * (Nx + 1) + X] = Amplitude * FMath::Sin(K * (P.X - Input.OriginCm.X)) * FMath::Cos(K * (P.Y - Input.OriginCm.Y));
		}
	for (int32 Y = 0; Y <= Ny; ++Y)
		for (int32 X = 0; X < Nx; ++X)
		{
			const FVector2f P = Input.OriginCm + FVector2f((X + 0.5f) * H, Y * H);
			Input.FaceV[Y * Nx + X] = -Amplitude * FMath::Cos(K * (P.X - Input.OriginCm.X)) * FMath::Sin(K * (P.Y - Input.OriginCm.Y));
		}
	Input.Particles.Reserve(Nx * Ny);
	for (int32 Y = 0; Y < Ny; ++Y)
		for (int32 X = 0; X < Nx; ++X)
		{
			FFlowMapGpuParticleState Particle;
			Particle.PositionCm = Input.OriginCm + FVector2f((X + 0.37f) * H, (Y + 0.61f) * H);
			Input.Particles.Add(Particle);
		}
	TArray<FFlowMapGpuParticleState> CpuParticles = Input.Particles;
	FFlowMapGpuParticleSettings Settings;
	Settings.ReadbackIntervalSteps = 5;
	FFlowMapGpuParticleSession Session;
	TestTrue(TEXT("GPU 粒子会话初始化排队"), Session.Initialize(Input, Settings));
	constexpr int32 StepCount = 20;
	bool bAllEnqueued = true;
	for (int32 Step = 0; Step < StepCount; ++Step)
	{
		bAllEnqueued &= Session.EnqueueAdvectionStep();
		FlowMapGpuParticleTest::StepReference(Input, CpuParticles);
	}
	TestTrue(TEXT("二十个 GPU 粒子步均排入"), bAllEnqueued);
	TestEqual(TEXT("游戏线程排队计数精确"), Session.GetStats().StepsEnqueued, static_cast<int64>(StepCount));
	Session.FlushAndWaitForReadbackForValidation();
	TArray<FFlowMapGpuParticleState> GpuParticles;
	int64 SnapshotStep = 0;
	TestTrue(TEXT("GPU 粒子异步快照可消费"), Session.ConsumeLatestParticles(GpuParticles, SnapshotStep));
	const FFlowMapGpuParticleStats Stats = Session.GetStats();
	TestTrue(TEXT("GPU 粒子会话已初始化"), Stats.bInitialized);
	TestEqual(TEXT("GPU 粒子执行步精确"), Stats.StepsExecuted, static_cast<int64>(StepCount));
	TestEqual(TEXT("最终粒子快照对应第 20 步"), SnapshotStep, static_cast<int64>(StepCount));
	TestEqual(TEXT("每步恰好一个平流 Pass"), Stats.AdvectionPassesExecuted, static_cast<int64>(StepCount));
	TestTrue(TEXT("至少完成一次成对异步读回"), Stats.ReadbacksCompleted >= 1);
	TestEqual(TEXT("GPU 粒子数量保持"), GpuParticles.Num(), CpuParticles.Num());
	float MaximumPositionError = 0.0f;
	float MaximumJacobianError = 0.0f;
	float MaximumDeterminantDifference = 0.0f;
	for (int32 Index = 0; Index < CpuParticles.Num() && Index < GpuParticles.Num(); ++Index)
	{
		MaximumPositionError = FMath::Max(MaximumPositionError, FVector2f::Distance(CpuParticles[Index].PositionCm, GpuParticles[Index].PositionCm));
		const FVector4f Delta = CpuParticles[Index].BackwardMapJacobian - GpuParticles[Index].BackwardMapJacobian;
		MaximumJacobianError = FMath::Max(MaximumJacobianError, FMath::Max(FMath::Max(FMath::Abs(Delta.X), FMath::Abs(Delta.Y)), FMath::Max(FMath::Abs(Delta.Z), FMath::Abs(Delta.W))));
		const FVector4f CpuB = CpuParticles[Index].BackwardMapJacobian;
		const FVector4f GpuB = GpuParticles[Index].BackwardMapJacobian;
		MaximumDeterminantDifference = FMath::Max(MaximumDeterminantDifference, FMath::Abs((CpuB.X * CpuB.W - CpuB.Y * CpuB.Z) - (GpuB.X * GpuB.W - GpuB.Y * GpuB.Z)));
	}
	TestTrue(TEXT("GPU/CPU 粒子位置最大误差低于 1e-4 cm"), MaximumPositionError < 1.e-4f);
	TestTrue(TEXT("GPU/CPU Jacobian 分量最大误差低于 1e-5"), MaximumJacobianError < 1.e-5f);
	TestTrue(TEXT("GPU/CPU Jacobian 行列式差低于 1e-5"), MaximumDeterminantDifference < 1.e-5f);
	AddInfo(FString::Printf(TEXT("FlowMap GPU particles rhi=%s cells=32x32 particles=%d steps=%d dt=%.9g readback_interval=%d steps_enqueued=%lld steps_executed=%lld advection_passes=%lld readbacks_completed=%lld snapshot_step=%lld max_position_error_cm=%.9g max_jacobian_component_error=%.9g max_jacobian_determinant_difference=%.9g"),
		GDynamicRHI ? GDynamicRHI->GetName() : TEXT("None"), Input.Particles.Num(), StepCount, Input.FixedDeltaSeconds,
		Settings.ReadbackIntervalSteps, Stats.StepsEnqueued, Stats.StepsExecuted, Stats.AdvectionPassesExecuted,
		Stats.ReadbacksCompleted, SnapshotStep, MaximumPositionError, MaximumJacobianError, MaximumDeterminantDifference));
	Session.Shutdown();
	return true;
}

#endif
