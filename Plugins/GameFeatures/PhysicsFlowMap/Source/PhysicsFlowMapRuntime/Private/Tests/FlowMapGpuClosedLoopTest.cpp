#if WITH_DEV_AUTOMATION_TESTS

#include "FlowMapGpuClosedLoop.h"
#include "FlowMapImpulseVortex2D.h"

#include "Misc/AutomationTest.h"
#include "RHI.h"

namespace
{
	FFlowMapGpuClosedLoopInput BuildTaylorGreenGpuInput(const FFlowMapImpulseVortexSettings2D& Settings)
	{
		FFlowMapGpuClosedLoopInput Input;
		Input.CellCounts = Settings.Grid.CellCounts;
		Input.CellSizeCm = Settings.Grid.CellSizeCm;
		Input.OriginCm = Settings.Grid.OriginCm;
		Input.FixedDeltaSeconds = Settings.FixedDeltaSeconds;
		Input.PressureIterations = Settings.PressureIterations;
		Input.LongMapResetSteps = Settings.LongMapResetSteps;
		Input.ReadbackIntervalSteps = 2;
		Input.bUseGradient = true;
		const int32 Nx = Input.CellCounts.X;
		const int32 Ny = Input.CellCounts.Y;
		const float H = Input.CellSizeCm;
		const float K = 2.0f * PI / (Nx * H);
		auto Velocity = [&Input, &Settings, K](const FVector2f& P)
		{
			const FVector2f Local = P - Input.OriginCm;
			const float A = Settings.VortexAmplitudeCmPerSecond;
			return FVector2f(A * FMath::Sin(K * Local.X) * FMath::Cos(K * Local.Y), -A * FMath::Cos(K * Local.X) * FMath::Sin(K * Local.Y));
		};
		auto Gradient = [&Input, &Settings, K](const FVector2f& P)
		{
			const FVector2f Local = P - Input.OriginCm;
			const float AK = Settings.VortexAmplitudeCmPerSecond * K;
			return FVector4f(AK * FMath::Cos(K * Local.X) * FMath::Cos(K * Local.Y), -AK * FMath::Sin(K * Local.X) * FMath::Sin(K * Local.Y),
				AK * FMath::Sin(K * Local.X) * FMath::Sin(K * Local.Y), -AK * FMath::Cos(K * Local.X) * FMath::Cos(K * Local.Y));
		};
		Input.InitialFaceU.SetNumUninitialized((Nx + 1) * Ny);
		Input.InitialFaceV.SetNumUninitialized(Nx * (Ny + 1));
		for (int32 Y = 0; Y < Ny; ++Y) for (int32 X = 0; X <= Nx; ++X)
			Input.InitialFaceU[Y * (Nx + 1) + X] = Velocity(Input.OriginCm + FVector2f(X * H, (Y + 0.5f) * H)).X;
		for (int32 Y = 0; Y <= Ny; ++Y) for (int32 X = 0; X < Nx; ++X)
			Input.InitialFaceV[Y * Nx + X] = Velocity(Input.OriginCm + FVector2f((X + 0.5f) * H, Y * H)).Y;
		for (int32 Y = 0; Y < Ny; ++Y) for (int32 X = 0; X < Nx; ++X)
		{
			FFlowMapGpuImpulseParticle P;
			P.PositionCm = Input.OriginCm + FVector2f((X + 0.5f) * H, (Y + 0.5f) * H);
			P.ReferenceImpulseCmPerSecond = Velocity(P.PositionCm);
			P.ReferenceImpulseGradientPerSecond = Gradient(P.PositionCm);
			Input.Particles.Add(P);
		}
		return Input;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlowMapGpuClosedLoopTest,
	"PhysicsResearch.FlowMap.GpuClosedLoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlowMapGpuClosedLoopTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过 Flow Map GPU 统一闭环；真实 D3D12 报告单独生成。"));
		return true;
	}
	FFlowMapImpulseVortexSettings2D CpuSettings;
	CpuSettings.Grid.CellCounts = FIntPoint(16, 16);
	CpuSettings.Grid.CellSizeCm = 5.0f;
	CpuSettings.Grid.OriginCm = FVector2f(-40.0f, -40.0f);
	CpuSettings.ParticlesPerCellAxis = 1;
	CpuSettings.FixedDeltaSeconds = 1.0f / 240.0f;
	CpuSettings.LongMapResetSteps = 20;
	CpuSettings.PressureIterations = 64;
	CpuSettings.VortexAmplitudeCmPerSecond = 20.0f;
	FFlowMapImpulseVortexSimulation2D Cpu;
	FString FailureReason;
	TestTrue(TEXT("CPU LongMapGradient 闭环初始化"), Cpu.Initialize(CpuSettings, EFlowMapImpulseMode2D::LongMapGradient, &FailureReason));
	FFlowMapGpuClosedLoopInput Input = BuildTaylorGreenGpuInput(CpuSettings);
	FFlowMapGpuClosedLoopSession Session;
	TestTrue(TEXT("GPU 统一闭环初始化排队"), Session.Initialize(Input));
	constexpr int32 StepCount = 4;
	bool bAllEnqueued = true;
	for (int32 Step = 0; Step < StepCount; ++Step)
	{
		bAllEnqueued &= Session.EnqueueStep();
		if (!Cpu.Step(&FailureReason)) { AddError(FailureReason); return false; }
	}
	TestTrue(TEXT("四个统一 GPU 固定步均排入"), bAllEnqueued);
	TestEqual(TEXT("游戏线程排队计数精确"), Session.GetStats().StepsEnqueued, static_cast<int64>(StepCount));
	Session.FlushAndWaitForReadbackForValidation();
	FFlowMapGpuClosedLoopSnapshot Snapshot;
	TestTrue(TEXT("统一速度/粒子快照可消费"), Session.ConsumeLatestSnapshot(Snapshot));
	const FFlowMapGpuClosedLoopStats Stats = Session.GetStats();
	TestTrue(TEXT("统一会话已初始化"), Stats.bInitialized);
	TestEqual(TEXT("统一会话执行四步"), Stats.StepsExecuted, static_cast<int64>(StepCount));
	TestEqual(TEXT("最终统一快照对应第四步"), Snapshot.StepIndex, static_cast<int64>(StepCount));
	TestEqual(TEXT("统一快照包含逐 Cell 最终压力势"),Snapshot.PressurePotential.Num(),Input.CellCounts.X*Input.CellCounts.Y);
	bool bPressurePotentialFinite=true;for(float Value:Snapshot.PressurePotential)bPressurePotentialFinite&=FMath::IsFinite(Value);TestTrue(TEXT("全部 GPU 压力势有限"),bPressurePotentialFinite);
	TestEqual(TEXT("粒子平流 Pass 精确"), Stats.ParticleAdvectionPasses, static_cast<int64>(StepCount));
	TestEqual(TEXT("Impulse U/V Gather Pass 精确"), Stats.ImpulseGatherPasses, static_cast<int64>(StepCount * 2));
	TestEqual(TEXT("散度 Pass 精确"), Stats.DivergencePasses, static_cast<int64>(StepCount));
	TestEqual(TEXT("压力 Jacobi Pass 精确"), Stats.PressureJacobiPasses, static_cast<int64>(StepCount * Input.PressureIterations));
	TestEqual(TEXT("压力 Apply Pass 精确"), Stats.PressureApplyPasses, static_cast<int64>(StepCount * 2));
	TestTrue(TEXT("至少完成一次四 Buffer 异步读回"), Stats.ReadbacksCompleted >= 1);
	const FFlowMapVelocityGrid2D& CpuGrid = Cpu.GetVelocityGrid();
	float MaximumFaceError = 0.0f;
	for (int32 I = 0; I < CpuGrid.FaceU.Num() && I < Snapshot.FaceU.Num(); ++I) MaximumFaceError = FMath::Max(MaximumFaceError, FMath::Abs(CpuGrid.FaceU[I] - Snapshot.FaceU[I]));
	for (int32 I = 0; I < CpuGrid.FaceV.Num() && I < Snapshot.FaceV.Num(); ++I) MaximumFaceError = FMath::Max(MaximumFaceError, FMath::Abs(CpuGrid.FaceV[I] - Snapshot.FaceV[I]));
	FFlowMapVelocityGrid2D GpuGrid;
	TestTrue(TEXT("统一快照 MAC 合同重建"), GpuGrid.Initialize(CpuSettings.Grid, &FailureReason));
	GpuGrid.FaceU = Snapshot.FaceU;
	GpuGrid.FaceV = Snapshot.FaceV;
	const double CpuDivergence = CpuGrid.CalculateDivergenceRms();
	const double GpuDivergence = GpuGrid.CalculateDivergenceRms();
	float MaximumJacobianDeterminantError = 0.0f;
	for (const FVector4f& B : Snapshot.BackwardMapJacobians) MaximumJacobianDeterminantError = FMath::Max(MaximumJacobianDeterminantError, FMath::Abs(B.X * B.W - B.Y * B.Z - 1.0f));
	TestTrue(TEXT("四步统一 GPU/CPU 面速度误差低于 1e-3"), MaximumFaceError < 1.e-3f);
	TestTrue(TEXT("四步统一 GPU/CPU 散度差低于 1e-4"), FMath::Abs(GpuDivergence - CpuDivergence) < 1.e-4);
	TestTrue(TEXT("GPU Backward Map 四步仍近体积保持"), MaximumJacobianDeterminantError < 0.01f);
	AddInfo(FString::Printf(TEXT("FlowMap GPU closed loop rhi=%s cells=16x16 particles=%d steps=%d dt=%.9g pressure_iterations=%d readback_interval=%d steps_enqueued=%lld steps_executed=%lld advection_passes=%lld gather_passes=%lld divergence_passes=%lld jacobi_passes=%lld apply_passes=%lld readbacks_completed=%lld snapshot_step=%lld cpu_divergence=%.9g gpu_divergence=%.9g max_face_error=%.9g max_jacobian_det_error=%.9g"),
		GDynamicRHI ? GDynamicRHI->GetName() : TEXT("None"), Input.Particles.Num(), StepCount, Input.FixedDeltaSeconds,
		Input.PressureIterations, Input.ReadbackIntervalSteps, Stats.StepsEnqueued, Stats.StepsExecuted,
		Stats.ParticleAdvectionPasses, Stats.ImpulseGatherPasses, Stats.DivergencePasses, Stats.PressureJacobiPasses,
		Stats.PressureApplyPasses, Stats.ReadbacksCompleted, Snapshot.StepIndex, CpuDivergence, GpuDivergence,
		MaximumFaceError, MaximumJacobianDeterminantError));
	Session.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlowMapGpuClosedLoopMapResetTest,
	"PhysicsResearch.FlowMap.GpuClosedLoopMapReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlowMapGpuClosedLoopMapResetTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过 Flow Map GPU Map Reset；真实 D3D12 报告单独生成。"));
		return true;
	}
	FFlowMapImpulseVortexSettings2D CpuSettings;
	CpuSettings.Grid.CellCounts = FIntPoint(16, 16);
	CpuSettings.Grid.CellSizeCm = 5.0f;
	CpuSettings.Grid.OriginCm = FVector2f(-40.0f, -40.0f);
	CpuSettings.ParticlesPerCellAxis = 1;
	CpuSettings.FixedDeltaSeconds = 1.0f / 240.0f;
	CpuSettings.LongMapResetSteps = 4;
	CpuSettings.PressureIterations = 64;
	CpuSettings.VortexAmplitudeCmPerSecond = 20.0f;
	FFlowMapImpulseVortexSimulation2D Cpu;
	FString FailureReason;
	TestTrue(TEXT("CPU 两周期 Long Map Gradient 初始化"), Cpu.Initialize(CpuSettings, EFlowMapImpulseMode2D::LongMapGradient, &FailureReason));
	FFlowMapGpuClosedLoopInput Input = BuildTaylorGreenGpuInput(CpuSettings);
	FFlowMapGpuClosedLoopSession Session;
	TestTrue(TEXT("GPU Map Reset 闭环初始化排队"), Session.Initialize(Input));
	constexpr int32 StepCount = 8;
	bool bAllEnqueued = true;
	for (int32 Step = 0; Step < StepCount; ++Step)
	{
		bAllEnqueued &= Session.EnqueueStep();
		if (!Cpu.Step(&FailureReason)) { AddError(FailureReason); return false; }
	}
	TestTrue(TEXT("跨两次重置的八个固定步均排入"), bAllEnqueued);
	Session.FlushAndWaitForReadbackForValidation();
	FFlowMapGpuClosedLoopSnapshot Snapshot;
	TestTrue(TEXT("第二次重置后的四 Buffer 快照可消费"), Session.ConsumeLatestSnapshot(Snapshot));
	const FFlowMapGpuClosedLoopStats Stats = Session.GetStats();
	TestEqual(TEXT("GPU 执行八步"), Stats.StepsExecuted, static_cast<int64>(StepCount));
	TestEqual(TEXT("GPU 粒子平流 Pass 精确"), Stats.ParticleAdvectionPasses, static_cast<int64>(StepCount));
	TestEqual(TEXT("GPU Impulse Gather Pass 精确"), Stats.ImpulseGatherPasses, static_cast<int64>(StepCount * 2));
	TestEqual(TEXT("GPU 散度 Pass 精确"), Stats.DivergencePasses, static_cast<int64>(StepCount));
	TestEqual(TEXT("GPU 压力 Jacobi Pass 精确"), Stats.PressureJacobiPasses, static_cast<int64>(StepCount * Input.PressureIterations));
	TestEqual(TEXT("GPU 压力 Apply Pass 精确"), Stats.PressureApplyPasses, static_cast<int64>(StepCount * 2));
	TestEqual(TEXT("GPU Map Reset Pass 精确"), Stats.MapResetPasses, static_cast<int64>(2));
	TestEqual(TEXT("GPU Long Map 重置次数精确"), Stats.MapResets, static_cast<int64>(2));
	TestEqual(TEXT("CPU Long Map 重置次数精确"), Cpu.GetMetrics().MapResets, 2);
	TestEqual(TEXT("快照来自第二次重置步"), Snapshot.StepIndex, static_cast<int64>(StepCount));
	const FFlowMapVelocityGrid2D& CpuGrid = Cpu.GetVelocityGrid();
	float MaximumFaceError = 0.0f;
	for (int32 I = 0; I < CpuGrid.FaceU.Num() && I < Snapshot.FaceU.Num(); ++I) MaximumFaceError = FMath::Max(MaximumFaceError, FMath::Abs(CpuGrid.FaceU[I] - Snapshot.FaceU[I]));
	for (int32 I = 0; I < CpuGrid.FaceV.Num() && I < Snapshot.FaceV.Num(); ++I) MaximumFaceError = FMath::Max(MaximumFaceError, FMath::Abs(CpuGrid.FaceV[I] - Snapshot.FaceV[I]));
	FFlowMapVelocityGrid2D GpuGrid;
	TestTrue(TEXT("重置后 GPU MAC 网格可重建"), GpuGrid.Initialize(CpuSettings.Grid, &FailureReason));
	GpuGrid.FaceU = Snapshot.FaceU;
	GpuGrid.FaceV = Snapshot.FaceV;
	float MaximumJacobianIdentityError = 0.0f;
	float MaximumJacobianDeterminantError = 0.0f;
	for (const FVector4f& B : Snapshot.BackwardMapJacobians)
	{
		MaximumJacobianIdentityError = FMath::Max(MaximumJacobianIdentityError, FMath::Max(FMath::Max(FMath::Abs(B.X - 1.0f), FMath::Abs(B.Y)), FMath::Max(FMath::Abs(B.Z), FMath::Abs(B.W - 1.0f))));
		MaximumJacobianDeterminantError = FMath::Max(MaximumJacobianDeterminantError, FMath::Abs(B.X * B.W - B.Y * B.Z - 1.0f));
	}
	const double CpuDivergence = CpuGrid.CalculateDivergenceRms();
	const double GpuDivergence = GpuGrid.CalculateDivergenceRms();
	TestTrue(TEXT("跨两次重置后 GPU/CPU 面速度误差低于 1e-3"), MaximumFaceError < 1.e-3f);
	TestTrue(TEXT("跨两次重置后 GPU/CPU 散度差低于 1e-4"), FMath::Abs(GpuDivergence - CpuDivergence) < 1.e-4);
	TestTrue(TEXT("重置步快照中的 B 精确回到单位阵"), MaximumJacobianIdentityError < 1.e-7f);
	TestTrue(TEXT("重置步快照中的 det(B) 精确回到 1"), MaximumJacobianDeterminantError < 1.e-7f);
	AddInfo(FString::Printf(TEXT("FlowMap GPU map reset rhi=%s cells=16x16 particles=%d steps=%d reset_interval=%d cpu_map_resets=%d gpu_map_resets=%lld reset_passes=%lld advection_passes=%lld gather_passes=%lld divergence_passes=%lld jacobi_passes=%lld apply_passes=%lld readbacks_completed=%lld snapshot_step=%lld cpu_divergence=%.9g gpu_divergence=%.9g max_face_error=%.9g max_jacobian_identity_error=%.9g max_jacobian_det_error=%.9g"),
		GDynamicRHI ? GDynamicRHI->GetName() : TEXT("None"), Input.Particles.Num(), StepCount, Input.LongMapResetSteps,
		Cpu.GetMetrics().MapResets, Stats.MapResets, Stats.MapResetPasses, Stats.ParticleAdvectionPasses, Stats.ImpulseGatherPasses,
		Stats.DivergencePasses, Stats.PressureJacobiPasses, Stats.PressureApplyPasses, Stats.ReadbacksCompleted, Snapshot.StepIndex,
		CpuDivergence, GpuDivergence, MaximumFaceError, MaximumJacobianIdentityError, MaximumJacobianDeterminantError));
	Session.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlowMapGpuClosedLoopTimestampTest,
	"PhysicsResearch.FlowMap.GpuClosedLoopTimestamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlowMapGpuClosedLoopTimestampTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过 Flow Map GPU timestamp；真实 D3D12 报告单独生成。"));
		return true;
	}
	FFlowMapImpulseVortexSettings2D Settings;
	Settings.Grid.CellCounts = FIntPoint(16, 16);
	Settings.Grid.CellSizeCm = 5.0f;
	Settings.Grid.OriginCm = FVector2f(-40.0f, -40.0f);
	Settings.ParticlesPerCellAxis = 1;
	Settings.FixedDeltaSeconds = 1.0f / 240.0f;
	Settings.LongMapResetSteps = 4;
	Settings.PressureIterations = 64;
	Settings.VortexAmplitudeCmPerSecond = 20.0f;
	FFlowMapGpuClosedLoopInput Input = BuildTaylorGreenGpuInput(Settings);
	Input.ReadbackIntervalSteps = 8;
	Input.GpuTimingWarmupSteps = 4;
	FFlowMapGpuClosedLoopSession Session;
	TestTrue(TEXT("GPU timestamp Flow Map 会话初始化"), Session.Initialize(Input));
	constexpr int32 StepCount = 32;
	bool bAllEnqueued = true;
	for (int32 Step = 0; Step < StepCount; ++Step) bAllEnqueued &= Session.EnqueueStep();
	TestTrue(TEXT("32 个 GPU timing 固定步均排入"), bAllEnqueued);
	Session.FlushAndWaitForReadbackForValidation();
	const FFlowMapGpuClosedLoopStats Stats = Session.GetStats();
	TArray<int64> TimingSteps;
	TArray<double> TimingMilliseconds;
	Session.GetGpuTimingSamples(TimingSteps, TimingMilliseconds);
	TestTrue(TEXT("D3D12 支持绝对时间查询"), Stats.bGpuTimingSupported);
	TestEqual(TEXT("timestamp 不改变固定步完成数"), Stats.StepsExecuted, static_cast<int64>(StepCount));
	TestTrue(TEXT("至少完成一个非阻塞 GPU 时间样本"), Stats.GpuTimingSamplesCompleted > 0);
	TestEqual(TEXT("每步都被完成或显式记为槽位跳过"), Stats.GpuTimingSamplesCompleted + Stats.GpuTimingSamplesSkipped, static_cast<int64>(StepCount));
	TestEqual(TEXT("原始时间步号数量匹配完成样本"), TimingSteps.Num(), static_cast<int32>(Stats.GpuTimingSamplesCompleted));
	TestEqual(TEXT("原始时间值数量匹配完成样本"), TimingMilliseconds.Num(), static_cast<int32>(Stats.GpuTimingSamplesCompleted));
	TestTrue(TEXT("预热后存在稳态 GPU 样本"), Stats.SteadyStateGpuTimingSamples > 0 && Stats.SteadyStateGpuTimingSamples <= Stats.GpuTimingSamplesCompleted);
	TestTrue(TEXT("完整 GPU 固定步均值有限且为正"), FMath::IsFinite(Stats.MeanGpuStepMilliseconds) && Stats.MeanGpuStepMilliseconds > 0.0);
	TestTrue(TEXT("完整 GPU 固定步分位数顺序有效"), Stats.P95GpuStepMilliseconds > 0.0 && Stats.P99GpuStepMilliseconds >= Stats.P95GpuStepMilliseconds && Stats.MaximumGpuStepMilliseconds >= Stats.P99GpuStepMilliseconds);
	TestTrue(TEXT("稳态 GPU 固定步分位数顺序有效"), Stats.MeanSteadyStateGpuStepMilliseconds > 0.0 && Stats.P95SteadyStateGpuStepMilliseconds > 0.0 && Stats.P99SteadyStateGpuStepMilliseconds >= Stats.P95SteadyStateGpuStepMilliseconds && Stats.MaximumSteadyStateGpuStepMilliseconds >= Stats.P99SteadyStateGpuStepMilliseconds);
	bool bAllSamplesPositive = true;
	for (double Milliseconds : TimingMilliseconds) bAllSamplesPositive &= FMath::IsFinite(Milliseconds) && Milliseconds > 0.0;
	TestTrue(TEXT("全部原始 GPU 时间样本有限且为正"), bAllSamplesPositive);
	AddInfo(FString::Printf(TEXT("FlowMap GPU timestamp rhi=%s steps=%lld samples=%lld skipped=%lld warmup_steps=%d steady_samples=%lld latest_step=%lld latest_ms=%.9g mean_ms=%.9g p95_ms=%.9g p99_ms=%.9g max_ms=%.9g steady_mean_ms=%.9g steady_p95_ms=%.9g steady_p99_ms=%.9g steady_max_ms=%.9g scope=full_step_rdg_interval"),
		GDynamicRHI ? GDynamicRHI->GetName() : TEXT("None"), Stats.StepsExecuted, Stats.GpuTimingSamplesCompleted,
		Stats.GpuTimingSamplesSkipped, Input.GpuTimingWarmupSteps, Stats.SteadyStateGpuTimingSamples, Stats.LatestGpuTimingStep,
		Stats.LatestGpuStepMilliseconds, Stats.MeanGpuStepMilliseconds, Stats.P95GpuStepMilliseconds, Stats.P99GpuStepMilliseconds,
		Stats.MaximumGpuStepMilliseconds, Stats.MeanSteadyStateGpuStepMilliseconds, Stats.P95SteadyStateGpuStepMilliseconds,
		Stats.P99SteadyStateGpuStepMilliseconds, Stats.MaximumSteadyStateGpuStepMilliseconds));
	Session.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlowMapGpuSolidBoundaryTest,
	"PhysicsResearch.FlowMap.GpuClosedLoopSolidBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlowMapGpuSolidBoundaryTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过 Flow Map GPU 固体边界；真实 D3D12 报告单独生成。"));
		return true;
	}
	FFlowMapImpulseVortexSettings2D Settings;
	Settings.Grid.CellCounts = FIntPoint(16, 16);
	Settings.Grid.CellSizeCm = 5.0f;
	Settings.Grid.OriginCm = FVector2f(-40.0f, -40.0f);
	Settings.ParticlesPerCellAxis = 1;
	Settings.FixedDeltaSeconds = 0.1f;
	Settings.LongMapResetSteps = 20;
	Settings.PressureIterations = 256;
	Settings.VortexAmplitudeCmPerSecond = 20.0f;
	FFlowMapGpuClosedLoopInput Input = BuildTaylorGreenGpuInput(Settings);
	Input.ReadbackIntervalSteps = 1;
	FFlowMapGpuClosedLoopSession Session;
	TestTrue(TEXT("GPU 固体边界闭环初始化"), Session.Initialize(Input));
	FFlowMapGpuSolidBoundaryUpdate InvalidBoundary;
	TestFalse(TEXT("尺寸不完整的固体边界更新被拒绝"), Session.UpdateSolidBoundary(InvalidBoundary));
	auto BuildBoundary = [&Settings](const FTransform& Previous, const FTransform& Current, FFlowMapGpuSolidBoundaryUpdate& OutBoundary, FFlowMapSolidBoundaryRaster2D& OutRaster)
	{
		FFlowMapKinematicCircle2D Circle;
		FString FailureReason;
		if (!FFlowMapSolidBoundaryRasterizer2D::BuildCircleFromRigidBodyTransforms(Previous, Current, 12.0f, Settings.FixedDeltaSeconds, Circle, &FailureReason)
			|| !FFlowMapSolidBoundaryRasterizer2D::RasterizeCircle(Settings.Grid, Circle, OutRaster, &FailureReason)) return false;
		OutBoundary.SolidCellMask=OutRaster.SolidCellMask;OutBoundary.SolidFaceUMask=OutRaster.SolidFaceUMask;OutBoundary.SolidFaceVMask=OutRaster.SolidFaceVMask;OutBoundary.SolidFaceVelocityU=OutRaster.SolidFaceVelocityU;OutBoundary.SolidFaceVelocityV=OutRaster.SolidFaceVelocityV;FFlowMapGpuSolidBoundaryUpdate::FSolidCircle GpuCircle;GpuCircle.CenterCm=Circle.CenterCm;GpuCircle.RadiusCm=Circle.RadiusCm;GpuCircle.SafetyOffsetCm=0.05f;OutBoundary.ParticleCollisionCircles.Add(GpuCircle);return true;
	};
	const FTransform Pose0(FRotator(0.0,0.0,0.0),FVector(-2.0,0.0,0.0));
	const FTransform Pose1(FRotator(0.0,6.0,0.0),FVector(0.0,0.0,0.0));
	const FTransform Pose2(FRotator(0.0,12.0,0.0),FVector(2.0,0.0,0.0));
	FFlowMapGpuSolidBoundaryUpdate Boundary1,Boundary2;FFlowMapSolidBoundaryRaster2D Raster1,Raster2;
	TestTrue(TEXT("第一版 Chaos pose 边界生成"),BuildBoundary(Pose0,Pose1,Boundary1,Raster1));
	TestTrue(TEXT("第一版固体边界更新接受"),Session.UpdateSolidBoundary(Boundary1));
	TestTrue(TEXT("第一版边界固定步排入"),Session.EnqueueStep());
	TestTrue(TEXT("第二版 Chaos pose 边界生成"),BuildBoundary(Pose1,Pose2,Boundary2,Raster2));
	TestTrue(TEXT("第二版固体边界更新接受"),Session.UpdateSolidBoundary(Boundary2));
	TestTrue(TEXT("第二版边界固定步排入"),Session.EnqueueStep());
	Session.FlushAndWaitForReadbackForValidation();
	FFlowMapGpuClosedLoopSnapshot Snapshot;
	TestTrue(TEXT("第二版边界后的 GPU 快照可消费"),Session.ConsumeLatestSnapshot(Snapshot));
	const FFlowMapGpuClosedLoopStats Stats=Session.GetStats();
	TestEqual(TEXT("两次合法边界更新被接受"),Stats.SolidBoundaryUpdatesAccepted,static_cast<int64>(2));
	TestEqual(TEXT("两个固定步都应用固体边界"),Stats.SolidBoundaryApplicationsExecuted,static_cast<int64>(2));
	TestEqual(TEXT("最终 GPU 步使用第二版边界"),Stats.LatestSolidBoundaryVersion,static_cast<int64>(2));
	TestEqual(TEXT("最终固体 Cell 计数来自第二版"),Stats.LatestSolidCellCount,Raster2.CountSolidCells());
	TestEqual(TEXT("最终边界包含一个粒子碰撞圆"),Stats.LatestSolidCircleCount,1);
	TestEqual(TEXT("两个固定步都执行粒子固体投影 Pass"),Stats.ParticleSolidProjectionPasses,static_cast<int64>(2));
	TestEqual(TEXT("最终快照对应第二个边界步"),Snapshot.StepIndex,static_cast<int64>(2));
	TestEqual(TEXT("运动固体快照包含逐 Cell 压力势"),Snapshot.PressurePotential.Num(),Input.CellCounts.X*Input.CellCounts.Y);
	bool bSolidPressureFinite=true;for(float Value:Snapshot.PressurePotential)bSolidPressureFinite&=FMath::IsFinite(Value);TestTrue(TEXT("运动固体压力势全部有限"),bSolidPressureFinite);
	float MaximumNoSlipResidual=0.0f;int32 SolidUFaces=0,SolidVFaces=0;
	for(int32 I=0;I<Boundary2.SolidFaceUMask.Num();++I)if(Boundary2.SolidFaceUMask[I]){SolidUFaces++;MaximumNoSlipResidual=FMath::Max(MaximumNoSlipResidual,FMath::Abs(Snapshot.FaceU[I]-Boundary2.SolidFaceVelocityU[I]));}
	for(int32 I=0;I<Boundary2.SolidFaceVMask.Num();++I)if(Boundary2.SolidFaceVMask[I]){SolidVFaces++;MaximumNoSlipResidual=FMath::Max(MaximumNoSlipResidual,FMath::Abs(Snapshot.FaceV[I]-Boundary2.SolidFaceVelocityV[I]));}
	double FluidDivergenceSquares=0.0;float MaximumFluidDivergence=0.0f;int32 FluidCells=0;const int32 Nx=16,Ny=16;
	for(int32 Y=0;Y<Ny;++Y)for(int32 X=0;X<Nx;++X){const int32 Cell=Y*Nx+X;if(Boundary2.SolidCellMask[Cell])continue;const float Div=(Snapshot.FaceU[Y*(Nx+1)+X+1]-Snapshot.FaceU[Y*(Nx+1)+X]+Snapshot.FaceV[(Y+1)*Nx+X]-Snapshot.FaceV[Y*Nx+X])/Settings.Grid.CellSizeCm;FluidDivergenceSquares+=static_cast<double>(Div)*Div;MaximumFluidDivergence=FMath::Max(MaximumFluidDivergence,FMath::Abs(Div));FluidCells++;}
	const double FluidDivergenceRms=FMath::Sqrt(FluidDivergenceSquares/FluidCells);
	float MinimumParticleClearance=MAX_flt;int32 ParticlesInsideSolid=0;const FVector2f FinalCircleCenter(2.0f,0.0f);
	for(const FVector2f& Position:Snapshot.ParticlePositionsCm){const float Clearance=(Position-FinalCircleCenter).Length()-12.0f;MinimumParticleClearance=FMath::Min(MinimumParticleClearance,Clearance);ParticlesInsideSolid+=Clearance<0.0f?1:0;}
	TestTrue(TEXT("GPU Apply 后所有固体面严格跟随第二版刚体速度"),MaximumNoSlipResidual<1.e-6f);
	TestTrue(TEXT("固体感知投影后流体 Cell 散度 RMS 低于 0.05"),FluidDivergenceRms<0.05);
	TestEqual(TEXT("最终快照没有粒子留在运动固体内部"),ParticlesInsideSolid,0);
	TestTrue(TEXT("粒子相对物理表面保留至少 0.049 cm 安全间隙"),MinimumParticleClearance>=0.049f);
	AddInfo(FString::Printf(TEXT("FlowMap GPU solid boundary rhi=%s steps=%lld updates=%lld applications=%lld latest_version=%lld solid_cells=%d solid_u_faces=%d solid_v_faces=%d solid_circles=%d particle_projection_passes=%lld particles_inside_solid=%d minimum_particle_clearance_cm=%.9g max_no_slip_residual=%.9g fluid_divergence_rms=%.9g max_fluid_divergence=%.9g pressure_iterations=%d boundary_source=ue_chaos_pose_versions"),GDynamicRHI?GDynamicRHI->GetName():TEXT("None"),Stats.StepsExecuted,Stats.SolidBoundaryUpdatesAccepted,Stats.SolidBoundaryApplicationsExecuted,Stats.LatestSolidBoundaryVersion,Stats.LatestSolidCellCount,SolidUFaces,SolidVFaces,Stats.LatestSolidCircleCount,Stats.ParticleSolidProjectionPasses,ParticlesInsideSolid,MinimumParticleClearance,MaximumNoSlipResidual,FluidDivergenceRms,MaximumFluidDivergence,Input.PressureIterations));
	Session.Shutdown();return true;
}

#endif
