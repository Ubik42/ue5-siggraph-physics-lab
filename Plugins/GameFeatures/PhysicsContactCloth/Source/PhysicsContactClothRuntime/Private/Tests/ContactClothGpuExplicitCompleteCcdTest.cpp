#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothGpuExplicitTopology.h"
#include "Misc/AutomationTest.h"

namespace ContactClothGpuExplicitCompleteCcdTest
{
bool RunSession(bool bEnableCcd, TArray<FVector3f>& OutPositions, FContactClothGpuExplicitTopologyRealtimeStats& OutStats)
{
	const TArray<FVector3f> InitialPositions = {
		FVector3f(0.0f, 0.0f, 100.0f),
		FVector3f(1000.0f, 0.0f, 100.0f)};
	const TArray<float> InverseMasses = {1.0f, 0.0f};
	const TArray<FIntPoint> Edges = {FIntPoint(0, 1)};
	const TArray<float> RestLengths = {(InitialPositions[0] - InitialPositions[1]).Length()};
	FContactClothGpuExplicitTopologyRealtimeSettings Settings;
	Settings.Iterations = 1;
	Settings.Relaxation = 1.e-6f;
	Settings.FixedDeltaSeconds = 1.0f / 60.0f;
	Settings.GravityCmPerSecondSquared = FVector3f(0.0f, 0.0f, -720000.0f);
	Settings.VelocityDamping = 0.0f;
	Settings.ReadbackIntervalSteps = 1;
	Settings.CollisionThicknessCm = 0.0f;
	Settings.bEnableCompleteCcd = bEnableCcd;
	Settings.CcdSafetyOffsetCm = 0.01f;
	const TArray<FContactClothGpuSphere> Spheres = {FContactClothGpuSphere{FVector3f::ZeroVector, 10.0f}};
	const TArray<FContactClothGpuCapsule> Capsules;
	FContactClothGpuExplicitTopologySimulation Simulation;
	if (!Simulation.Initialize(InitialPositions, InverseMasses, Edges, RestLengths, Settings)
		|| !Simulation.UpdateCollisionPrimitives(Spheres, Capsules)
		|| !Simulation.EnqueueStep())
	{
		Simulation.Shutdown();
		return false;
	}
	Simulation.FlushAndWaitForReadbackForValidation();
	int64 ReadbackStep = 0;
	const bool bConsumed = Simulation.ConsumeLatestPositions(OutPositions, ReadbackStep);
	OutStats = Simulation.GetStats();
	Simulation.Shutdown();
	return bConsumed && ReadbackStep == 1;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothGpuExplicitCompleteCcdTest,
	"PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.CompleteCcdAblation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothGpuExplicitCompleteCcdTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过显式拓扑完整 CCD GPU Sweep；D3D12 报告单独生成。"));
		return true;
	}
	TArray<FVector3f> DiscretePositions;
	TArray<FVector3f> CcdPositions;
	FContactClothGpuExplicitTopologyRealtimeStats DiscreteStats;
	FContactClothGpuExplicitTopologyRealtimeStats CcdStats;
	TestTrue(TEXT("离散显式拓扑会话完成"), ContactClothGpuExplicitCompleteCcdTest::RunSession(false, DiscretePositions, DiscreteStats));
	TestTrue(TEXT("完整 CCD 显式拓扑会话完成"), ContactClothGpuExplicitCompleteCcdTest::RunSession(true, CcdPositions, CcdStats));
	const float DiscreteZ = DiscretePositions.IsValidIndex(0) ? DiscretePositions[0].Z : 0.0f;
	const float CcdZ = CcdPositions.IsValidIndex(0) ? CcdPositions[0].Z : 0.0f;
	const float FixedDrift = CcdPositions.IsValidIndex(1) ? (CcdPositions[1] - FVector3f(1000.0f, 0.0f, 100.0f)).Length() : TNumericLimits<float>::Max();
	AddInfo(FString::Printf(TEXT("Explicit topology complete CCD: discrete_z=%.6f ccd_z=%.6f fixed_index_1_drift_cm=%.9f discrete_passes=%lld ccd_passes=%lld"),
		DiscreteZ, CcdZ, FixedDrift, DiscreteStats.CompleteCcdPassesExecuted, CcdStats.CompleteCcdPassesExecuted));
	TestTrue(TEXT("离散端点投影漏过高速球穿越"), DiscreteZ < -10.0f);
	TestTrue(TEXT("完整 CCD 在球入射面截停动态点"), CcdZ >= 9.99f && CcdZ <= 10.02f);
	TestTrue(TEXT("固定点由逆质量而非数组位置判定"), FixedDrift < 1.e-5f);
	TestEqual(TEXT("离散基线不执行 CCD"), DiscreteStats.CompleteCcdPassesExecuted, static_cast<int64>(0));
	TestEqual(TEXT("启用时每固定步执行一次 CCD"), CcdStats.CompleteCcdPassesExecuted, static_cast<int64>(1));
	return true;
}

#endif
