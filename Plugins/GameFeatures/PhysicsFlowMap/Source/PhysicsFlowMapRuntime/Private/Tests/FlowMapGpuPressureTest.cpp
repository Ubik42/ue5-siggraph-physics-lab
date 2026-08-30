#if WITH_DEV_AUTOMATION_TESTS

#include "FlowMapGpuPressure.h"
#include "ParticleFlowMap2D.h"

#include "Misc/AutomationTest.h"
#include "RHI.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlowMapGpuPressureTruthTest,
	"PhysicsResearch.FlowMap.GpuPressureProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlowMapGpuPressureTruthTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过 Flow Map GPU 压力投影；真实 D3D12 报告单独生成。"));
		return true;
	}
	FFlowMapGridSettings2D Settings;
	Settings.CellCounts = FIntPoint(32, 16);
	Settings.CellSizeCm = 5.0f;
	FFlowMapVelocityGrid2D CpuGrid;
	FString FailureReason;
	TestTrue(TEXT("GPU 压力输入网格生成"), CpuGrid.Initialize(Settings, &FailureReason));
	CpuGrid.SetPeriodicCompressionWave(50.0f);
	FFlowMapGpuPressureInput Input;
	Input.CellCounts = Settings.CellCounts;
	Input.CellSizeCm = Settings.CellSizeCm;
	Input.JacobiIterations = 512;
	Input.FaceU = CpuGrid.FaceU;
	Input.FaceV = CpuGrid.FaceV;
	FFlowMapPressureProjectionMetrics2D CpuMetrics;
	TestTrue(TEXT("CPU 同预算压力真值执行"), FFlowMapPressureProjector2D::ProjectPeriodic(CpuGrid, Input.JacobiIterations, CpuMetrics, &FailureReason));
	const FFlowMapGpuPressureResult Gpu = RunFlowMapGpuPressureForValidation(Input);
	TestTrue(TEXT("GPU RDG 已派发"), Gpu.bDispatched);
	TestTrue(TEXT("GPU U/V 已读回"), Gpu.bReadbackSucceeded);
	TestEqual(TEXT("GPU divergence Pass 精确"), Gpu.DivergencePasses, 1);
	TestEqual(TEXT("GPU Jacobi Pass 精确"), Gpu.JacobiPasses, 512);
	TestEqual(TEXT("GPU apply Pass 精确"), Gpu.ApplyPasses, 2);
	float MaximumFaceError = 0.0f;
	for (int32 Index = 0; Index < CpuGrid.FaceU.Num() && Index < Gpu.FaceU.Num(); ++Index) MaximumFaceError = FMath::Max(MaximumFaceError, FMath::Abs(CpuGrid.FaceU[Index] - Gpu.FaceU[Index]));
	for (int32 Index = 0; Index < CpuGrid.FaceV.Num() && Index < Gpu.FaceV.Num(); ++Index) MaximumFaceError = FMath::Max(MaximumFaceError, FMath::Abs(CpuGrid.FaceV[Index] - Gpu.FaceV[Index]));
	FFlowMapVelocityGrid2D GpuGrid;
	TestTrue(TEXT("GPU 输出网格合同重建"), GpuGrid.Initialize(Settings, &FailureReason));
	GpuGrid.FaceU = Gpu.FaceU;
	GpuGrid.FaceV = Gpu.FaceV;
	const double GpuDivergence = GpuGrid.CalculateDivergenceRms();
	TestTrue(TEXT("GPU/CPU 面速度最大误差低于 1e-4"), MaximumFaceError < 1.e-4f);
	TestTrue(TEXT("GPU 散度与 CPU 同预算一致"), FMath::Abs(GpuDivergence - CpuMetrics.DivergenceRmsAfter) < 1.e-5);
	TestTrue(TEXT("GPU 512 次预算降低散度超过 99%"), GpuDivergence < CpuMetrics.DivergenceRmsBefore * 0.01);
	AddInfo(FString::Printf(TEXT("FlowMap GPU pressure rhi=%s cells=32x16 iterations=512 divergence_passes=%d jacobi_passes=%d apply_passes=%d cpu_divergence_before=%.9g cpu_divergence_after=%.9g gpu_divergence_after=%.9g gpu_to_initial_ratio=%.9g max_face_error=%.9g"),
		GDynamicRHI ? GDynamicRHI->GetName() : TEXT("None"), Gpu.DivergencePasses, Gpu.JacobiPasses, Gpu.ApplyPasses,
		CpuMetrics.DivergenceRmsBefore, CpuMetrics.DivergenceRmsAfter, GpuDivergence,
		GpuDivergence / CpuMetrics.DivergenceRmsBefore, MaximumFaceError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlowMapGpuPressurePersistentSessionTest,
	"PhysicsResearch.FlowMap.GpuPressurePersistentSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlowMapGpuPressurePersistentSessionTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过 Flow Map GPU 持久压力会话；真实 D3D12 报告单独生成。"));
		return true;
	}
	FFlowMapGridSettings2D GridSettings;
	GridSettings.CellCounts = FIntPoint(32, 16);
	GridSettings.CellSizeCm = 5.0f;
	FFlowMapVelocityGrid2D CpuGrid;
	FString FailureReason;
	TestTrue(TEXT("持久会话输入网格生成"), CpuGrid.Initialize(GridSettings, &FailureReason));
	CpuGrid.SetPeriodicCompressionWave(50.0f);
	FFlowMapGpuPressureInput Input;
	Input.CellCounts = GridSettings.CellCounts;
	Input.CellSizeCm = GridSettings.CellSizeCm;
	Input.FaceU = CpuGrid.FaceU;
	Input.FaceV = CpuGrid.FaceV;
	FFlowMapGpuPressureSessionSettings SessionSettings;
	SessionSettings.JacobiIterationsPerStep = 128;
	SessionSettings.ReadbackIntervalSteps = 2;
	FFlowMapGpuPressureSession Session;
	TestTrue(TEXT("GPU 持久压力会话初始化排队"), Session.Initialize(Input, SessionSettings));
	bool bAllStepsEnqueued = true;
	constexpr int32 StepCount = 4;
	for (int32 Step = 0; Step < StepCount; ++Step)
	{
		bAllStepsEnqueued &= Session.EnqueueProjectionStep();
		FFlowMapPressureProjectionMetrics2D CpuStepMetrics;
		if (!FFlowMapPressureProjector2D::ProjectPeriodic(CpuGrid, SessionSettings.JacobiIterationsPerStep, CpuStepMetrics, &FailureReason))
		{
			AddError(FailureReason);
			return false;
		}
	}
	TestTrue(TEXT("连续四步均以非阻塞接口排入"), bAllStepsEnqueued);
	TestEqual(TEXT("游戏线程立即记录四个排队步"), Session.GetStats().StepsEnqueued, static_cast<int64>(StepCount));
	Session.FlushAndWaitForReadbackForValidation();
	TArray<float> GpuFaceU;
	TArray<float> GpuFaceV;
	int64 SnapshotStep = 0;
	TestTrue(TEXT("异步双 Buffer 快照可消费"), Session.ConsumeLatestVelocity(GpuFaceU, GpuFaceV, SnapshotStep));
	const FFlowMapGpuPressureSessionStats Stats = Session.GetStats();
	TestTrue(TEXT("持久会话已在渲染线程初始化"), Stats.bInitialized);
	TestEqual(TEXT("GPU 精确执行四步"), Stats.StepsExecuted, static_cast<int64>(StepCount));
	TestEqual(TEXT("最终快照对应第四步"), SnapshotStep, static_cast<int64>(StepCount));
	TestEqual(TEXT("每步一次散度 Pass"), Stats.DivergencePassesExecuted, static_cast<int64>(StepCount));
	TestEqual(TEXT("四步累计 512 次 Jacobi"), Stats.JacobiPassesExecuted, static_cast<int64>(StepCount * SessionSettings.JacobiIterationsPerStep));
	TestEqual(TEXT("每步两个 Apply Pass"), Stats.ApplyPassesExecuted, static_cast<int64>(StepCount * 2));
	TestTrue(TEXT("至少完成一次异步读回"), Stats.ReadbacksCompleted >= 1);
	float MaximumFaceError = 0.0f;
	for (int32 Index = 0; Index < CpuGrid.FaceU.Num() && Index < GpuFaceU.Num(); ++Index) MaximumFaceError = FMath::Max(MaximumFaceError, FMath::Abs(CpuGrid.FaceU[Index] - GpuFaceU[Index]));
	for (int32 Index = 0; Index < CpuGrid.FaceV.Num() && Index < GpuFaceV.Num(); ++Index) MaximumFaceError = FMath::Max(MaximumFaceError, FMath::Abs(CpuGrid.FaceV[Index] - GpuFaceV[Index]));
	FFlowMapVelocityGrid2D GpuGrid;
	TestTrue(TEXT("持久会话快照网格合同重建"), GpuGrid.Initialize(GridSettings, &FailureReason));
	GpuGrid.FaceU = GpuFaceU;
	GpuGrid.FaceV = GpuFaceV;
	const double CpuDivergence = CpuGrid.CalculateDivergenceRms();
	const double GpuDivergence = GpuGrid.CalculateDivergenceRms();
	TestTrue(TEXT("连续四步 GPU/CPU 面速度误差低于 1e-4"), MaximumFaceError < 1.e-4f);
	TestTrue(TEXT("连续四步 GPU/CPU 散度差低于 1e-5"), FMath::Abs(GpuDivergence - CpuDivergence) < 1.e-5);
	AddInfo(FString::Printf(TEXT("FlowMap GPU pressure session rhi=%s cells=32x16 steps=%d iterations_per_step=%d readback_interval=%d steps_enqueued=%lld steps_executed=%lld readbacks_completed=%lld snapshot_step=%lld divergence_passes=%lld jacobi_passes=%lld apply_passes=%lld cpu_divergence_after=%.9g gpu_divergence_after=%.9g max_face_error=%.9g"),
		GDynamicRHI ? GDynamicRHI->GetName() : TEXT("None"), StepCount, SessionSettings.JacobiIterationsPerStep,
		SessionSettings.ReadbackIntervalSteps, Stats.StepsEnqueued, Stats.StepsExecuted, Stats.ReadbacksCompleted,
		SnapshotStep, Stats.DivergencePassesExecuted, Stats.JacobiPassesExecuted, Stats.ApplyPassesExecuted,
		CpuDivergence, GpuDivergence, MaximumFaceError));
	Session.Shutdown();
	return true;
}

#endif
