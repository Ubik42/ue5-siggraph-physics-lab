#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothGpuJacobi.h"
#include "Misc/AutomationTest.h"
#include "RHI.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothGpuResidualReuseTest,
	"PhysicsResearch.ContactCloth.GpuResidualReuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothGpuResidualReuseTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过 GPU 残差复用；D3D12 报告单独生成。"));
		return true;
	}

	FContactClothGpuRealtimeSettings Settings;
	Settings.Columns = 6;
	Settings.Rows = 6;
	Settings.SpacingCm = 14.0f;
	Settings.Iterations = 1;
	Settings.Relaxation = 0.9f;
	Settings.FixedDeltaSeconds = 1.0f / 60.0f;
	Settings.GravityCmPerSecondSquared = FVector3f(0.0f, 0.0f, -980.0f);
	Settings.VelocityDamping = 0.0f;
	Settings.ReadbackIntervalSteps = 1;
	Settings.bEnableResidualReuse = true;
	Settings.ResidualForwardScale = 0.4f;
	Settings.MaximumResidualCm = 100.0f;
	Settings.MaximumResidualCcdCorrectionCm = 100.0f;

	TArray<FVector3f> InitialPositions;
	for (int32 Row = 0; Row < Settings.Rows; ++Row)
	{
		for (int32 Column = 0; Column < Settings.Columns; ++Column)
		{
			InitialPositions.Emplace(Column * Settings.SpacingCm, 0.0f, 160.0f - Row * Settings.SpacingCm);
		}
	}

	FContactClothGpuRealtimeSimulation Simulation;
	TestTrue(TEXT("残差复用 GPU 会话初始化"), Simulation.Initialize(InitialPositions, Settings));
	constexpr int32 StepCount = 12;
	bool bAllStepsAccepted = true;
	for (int32 Step = 0; Step < StepCount; ++Step)
	{
		bAllStepsAccepted &= Simulation.EnqueueStep();
	}
	TestTrue(TEXT("残差复用固定步全部入队"), bAllStepsAccepted);
	Simulation.FlushAndWaitForReadbackForValidation();
	TArray<FVector3f> Positions;
	int64 ReadbackStep = 0;
	TestTrue(TEXT("残差状态随最终位置异步回读"), Simulation.ConsumeLatestPositions(Positions, ReadbackStep));
	const FContactClothGpuRealtimeStats Stats = Simulation.GetStats();
	TestEqual(TEXT("每固定步捕获一次可转发残差"), Stats.ResidualReusePassesExecuted, static_cast<int64>(StepCount));
	TestTrue(TEXT("安全场景产生非零可转发残差"), Stats.MaximumForwardedResidualCm > 1.e-5f);
	TestEqual(TEXT("宽松安全阈值不触发回退"), Stats.ResidualFallbackEvents, static_cast<int64>(0));
	TestEqual(TEXT("最终残差快照对应最后固定步"), ReadbackStep, static_cast<int64>(StepCount));
	bool bPinnedRowPreserved = Positions.Num() == InitialPositions.Num();
	for (int32 Column = 0; bPinnedRowPreserved && Column < Settings.Columns; ++Column)
	{
		bPinnedRowPreserved = Positions[Column].Equals(InitialPositions[Column], 1.e-3f);
	}
	TestTrue(TEXT("残差转发不移动固定顶边"), bPinnedRowPreserved);
	Simulation.Shutdown();

	FContactClothGpuRealtimeSettings TopologySettings = Settings;
	TopologySettings.GravityCmPerSecondSquared = FVector3f::ZeroVector;
	FContactClothGpuRealtimeSimulation TopologySimulation;
	TestTrue(TEXT("拓扑失效 GPU 会话初始化"), TopologySimulation.Initialize(InitialPositions, TopologySettings));
	const FContactClothGpuSphere FarSphere{FVector3f(10000.0f, 10000.0f, 10000.0f), 10.0f};
	TopologySimulation.UpdateCollisionPrimitives(MakeArrayView(&FarSphere, 1), TConstArrayView<FContactClothGpuCapsule>());
	TestTrue(TEXT("碰撞拓扑变化后的固定步入队"), TopologySimulation.EnqueueStep());
	TopologySimulation.FlushAndWaitForReadbackForValidation();
	TArray<FVector3f> TopologyPositions;
	int64 TopologyStep = 0;
	TestTrue(TEXT("拓扑回退状态被读回"), TopologySimulation.ConsumeLatestPositions(TopologyPositions, TopologyStep));
	const FContactClothGpuRealtimeStats TopologyStats = TopologySimulation.GetStats();
	const int64 FreeParticleCount = InitialPositions.Num() - Settings.Columns;
	TestEqual(TEXT("拓扑变化为每个自由粒子记录一次回退事件"), TopologyStats.ResidualFallbackEvents, FreeParticleCount);
	TestEqual(TEXT("拓扑回退后残差归零"), TopologyStats.MaximumForwardedResidualCm, 0.0f);
	TopologySimulation.Shutdown();

	AddInfo(FString::Printf(
		TEXT("GPU residual reuse: steps=%lld max_residual_cm=%.6f safe_fallback_events=%lld topology_fallback_events=%lld"),
		Stats.ResidualReusePassesExecuted,
		Stats.MaximumForwardedResidualCm,
		Stats.ResidualFallbackEvents,
		TopologyStats.ResidualFallbackEvents));
	return true;
}

#endif
