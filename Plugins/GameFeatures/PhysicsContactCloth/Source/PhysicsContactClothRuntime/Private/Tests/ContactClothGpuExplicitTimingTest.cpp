#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothGpuExplicitTopology.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothGpuExplicitTimingTest,
	"PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.NonBlockingGpuTimestamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothGpuExplicitTimingTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过真实 GPU timestamp 门；D3D12 报告单独生成。"));
		return true;
	}
	const TArray<FVector3f> InitialPositions = {{0,0,20},{20,0,20},{0,0,0},{20,0,0}};
	const TArray<float> InverseMasses = {0,1,1,1};
	const TArray<FIntPoint> Edges = {{0,1},{0,2},{1,3},{2,3},{1,2}};
	TArray<float> RestLengths;
	for (const FIntPoint& Edge : Edges) RestLengths.Add((InitialPositions[Edge.X] - InitialPositions[Edge.Y]).Length());
	FContactClothGpuExplicitTopologyRealtimeSettings Settings;
	Settings.Iterations = 4;
	Settings.Relaxation = 0.9f;
	Settings.FixedDeltaSeconds = 1.0f / 60.0f;
	Settings.GravityCmPerSecondSquared = FVector3f(0, 0, -980);
	Settings.ReadbackIntervalSteps = 4;
	Settings.CollisionPrimitiveCapacity = 1;
	FContactClothGpuExplicitTopologySimulation Simulation;
	TestTrue(TEXT("GPU timestamp 会话初始化"), Simulation.Initialize(InitialPositions, InverseMasses, Edges, RestLengths, Settings));
	constexpr int32 StepCount = 16;
	for (int32 Step = 0; Step < StepCount; ++Step)
	{
		TestTrue(TEXT("GPU timestamp 固定步排入"), Simulation.EnqueueStep());
	}
	Simulation.FlushAndWaitForReadbackForValidation();
	const FContactClothGpuExplicitTopologyRealtimeStats Stats = Simulation.GetStats();
	Simulation.Shutdown();
	TestTrue(TEXT("D3D12 支持绝对时间查询"), Stats.bGpuTimingSupported);
	TestTrue(TEXT("至少完成一个非阻塞 GPU 时间样本"), Stats.GpuTimingSamplesCompleted > 0);
	TestTrue(TEXT("GPU 时间样本不超过已执行步数"), Stats.GpuTimingSamplesCompleted <= StepCount);
	TestTrue(TEXT("GPU 均值有限且为正"), FMath::IsFinite(Stats.MeanGpuStepMilliseconds) && Stats.MeanGpuStepMilliseconds > 0.0);
	TestTrue(TEXT("GPU 分位数顺序有效"), Stats.P95GpuStepMilliseconds > 0.0
		&& Stats.P99GpuStepMilliseconds >= Stats.P95GpuStepMilliseconds
		&& Stats.MaximumGpuStepMilliseconds >= Stats.P99GpuStepMilliseconds);
	TestTrue(TEXT("timestamp 不改变固定步完成数"), Stats.StepsExecuted == StepCount);
	AddInfo(FString::Printf(TEXT("Explicit GPU timestamp: supported=true steps=%lld samples=%lld skipped=%lld latest_step=%lld mean_ms=%.6f p95_ms=%.6f p99_ms=%.6f max_ms=%.6f scope=full_step_rdg_interval"),
		Stats.StepsExecuted, Stats.GpuTimingSamplesCompleted, Stats.GpuTimingSamplesSkipped, Stats.LatestGpuTimingStep,
		Stats.MeanGpuStepMilliseconds, Stats.P95GpuStepMilliseconds, Stats.P99GpuStepMilliseconds, Stats.MaximumGpuStepMilliseconds));
	return true;
}

#endif
