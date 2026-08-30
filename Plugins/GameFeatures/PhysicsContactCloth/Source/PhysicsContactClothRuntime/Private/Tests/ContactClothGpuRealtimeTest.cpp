#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothGpuJacobi.h"
#include "Misc/AutomationTest.h"
#include "RHI.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothGpuRealtimeTest,
	"PhysicsResearch.ContactCloth.GpuRealtimePersistentBuffers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothGpuRealtimeTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过持久 GPU Buffer 验证；D3D12 报告单独生成。"));
		return true;
	}

	FContactClothGpuRealtimeSettings Settings;
	Settings.Columns = 12;
	Settings.Rows = 10;
	Settings.SpacingCm = 14.0f;
	Settings.Iterations = 20;
	Settings.Relaxation = 0.9f;
	Settings.FixedDeltaSeconds = 1.0f / 60.0f;
	Settings.GravityCmPerSecondSquared = FVector3f(0.0f, 0.0f, -980.0f);
	Settings.VelocityDamping = 0.995f;
	Settings.ReadbackIntervalSteps = 1;
	Settings.SphereRadiusCm = 0.0f;
	Settings.CapsuleRadiusCm = 0.0f;

	TArray<FVector3f> InitialPositions;
	InitialPositions.Reserve(Settings.Columns * Settings.Rows);
	for (int32 Row = 0; Row < Settings.Rows; ++Row)
	{
		for (int32 Column = 0; Column < Settings.Columns; ++Column)
		{
			InitialPositions.Emplace(Column * Settings.SpacingCm, 0.0f, 220.0f - Row * Settings.SpacingCm);
		}
	}

	FContactClothGpuRealtimeSimulation Simulation;
	TestTrue(TEXT("持久 GPU 会话接受有效网格"), Simulation.Initialize(InitialPositions, Settings));
	constexpr int32 StepsToEnqueue = 120;
	bool bAllStepsAccepted = true;
	for (int32 Step = 0; Step < StepsToEnqueue; ++Step)
	{
		bAllStepsAccepted &= Simulation.EnqueueStep();
	}
	TestTrue(TEXT("全部固定步进入渲染队列"), bAllStepsAccepted);
	Simulation.FlushAndWaitForReadbackForValidation();

	TArray<FVector3f> OutputPositions;
	int64 OutputStep = 0;
	const bool bConsumedReadback = Simulation.ConsumeLatestPositions(OutputPositions, OutputStep);
	const FContactClothGpuRealtimeStats Stats = Simulation.GetStats();
	TestTrue(TEXT("异步 GPU 快照已被消费"), bConsumedReadback);
	TestEqual(TEXT("持久 Buffer 执行全部固定步"), Stats.StepsExecuted, static_cast<int64>(StepsToEnqueue));
	TestTrue(TEXT("至少完成一次非阻塞 Readback"), Stats.ReadbacksCompleted > 0);
	TestEqual(TEXT("最终 Readback 对应最后固定步"), OutputStep, static_cast<int64>(StepsToEnqueue));
	TestEqual(TEXT("Readback 粒子数"), OutputPositions.Num(), InitialPositions.Num());

	bool bPinnedRowPreserved = OutputPositions.Num() == InitialPositions.Num();
	for (int32 Column = 0; bPinnedRowPreserved && Column < Settings.Columns; ++Column)
	{
		bPinnedRowPreserved = OutputPositions[Column].Equals(InitialPositions[Column], 1.e-3f);
	}
	TestTrue(TEXT("逐帧 GPU 固定顶边保持不动"), bPinnedRowPreserved);
	const int32 BottomCenter = (Settings.Rows - 1) * Settings.Columns + Settings.Columns / 2;
	TestTrue(TEXT("自由粒子在 GPU 重力积分下下降"), OutputPositions.IsValidIndex(BottomCenter) && OutputPositions[BottomCenter].Z < InitialPositions[BottomCenter].Z - 0.01f);
	const double EdgeError = FContactClothGpuJacobi::MeasureMaximumStructuralEdgeErrorCm(OutputPositions, Settings.Columns, Settings.Rows, Settings.SpacingCm);
	TestTrue(TEXT("逐帧 GPU 输出结构误差有限"), FMath::IsFinite(EdgeError));
	AddInfo(FString::Printf(
		TEXT("Persistent GPU cloth: steps=%lld readbacks=%lld latest_step=%lld particles=%d max_edge_error_cm=%.6f"),
		Stats.StepsExecuted,
		Stats.ReadbacksCompleted,
		OutputStep,
		OutputPositions.Num(),
		EdgeError));
	Simulation.Shutdown();
	return true;
}

#endif
