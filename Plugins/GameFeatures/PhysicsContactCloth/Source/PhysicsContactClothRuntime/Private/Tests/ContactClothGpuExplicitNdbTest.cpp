#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothGpuExplicitTopology.h"
#include "Misc/AutomationTest.h"

namespace ContactClothGpuExplicitNdbTest
{
void Configure(FContactClothGpuExplicitTopologyRealtimeSettings& Settings, float ActivationMultiplier)
{
	Settings.Iterations = 1;
	Settings.Relaxation = 1.e-6f;
	Settings.FixedDeltaSeconds = 1.0f / 60.0f;
	Settings.GravityCmPerSecondSquared = FVector3f::ZeroVector;
	Settings.VelocityDamping = 0.0f;
	Settings.ReadbackIntervalSteps = 1;
	Settings.CollisionThicknessCm = 0.0f;
	Settings.bEnableNonDistanceBarrier = true;
	Settings.NdbBaseWeight = 0.05f;
	Settings.NdbActivationMultiplier = ActivationMultiplier;
	Settings.NdbMaximumWeight = 1024.0f;
	Settings.CollisionPrimitiveCapacity = 1;
	Settings.bEnableCompleteCcd = false;
}

bool RunSession(float ActivationMultiplier, int32 StepCount, TArray<FVector3f>& OutPositions,
	FContactClothGpuExplicitTopologyRealtimeStats& OutStats)
{
	const TArray<FVector3f> InitialPositions = {
		FVector3f(5.0f, 0.0f, 0.0f),
		FVector3f(1000.0f, 0.0f, 0.0f)};
	const TArray<float> InverseMasses = {1.0f, 0.0f};
	const TArray<FIntPoint> Edges = {FIntPoint(0, 1)};
	const TArray<float> RestLengths = {(InitialPositions[0] - InitialPositions[1]).Length()};
	const TArray<FContactClothGpuSphere> Spheres = {FContactClothGpuSphere{FVector3f::ZeroVector, 10.0f}};
	const TArray<FContactClothGpuCapsule> Capsules;
	FContactClothGpuExplicitTopologyRealtimeSettings Settings;
	Configure(Settings, ActivationMultiplier);
	FContactClothGpuExplicitTopologySimulation Simulation;
	if (!Simulation.Initialize(InitialPositions, InverseMasses, Edges, RestLengths, Settings)
		|| !Simulation.UpdateCollisionPrimitives(Spheres, Capsules))
	{
		Simulation.Shutdown();
		return false;
	}
	for (int32 Step = 0; Step < StepCount; ++Step)
	{
		if (!Simulation.EnqueueStep())
		{
			Simulation.Shutdown();
			return false;
		}
	}
	Simulation.FlushAndWaitForReadbackForValidation();
	int64 ReadbackStep = 0;
	const bool bConsumed = Simulation.ConsumeLatestPositions(OutPositions, ReadbackStep);
	OutStats = Simulation.GetStats();
	Simulation.Shutdown();
	return bConsumed && ReadbackStep == StepCount;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothGpuExplicitNdbTest,
	"PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.NonDistanceBarrierAblation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothGpuExplicitNdbTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过显式拓扑 NDB GPU 状态门；D3D12 报告单独生成。"));
		return true;
	}
	constexpr int32 StepCount = 4;
	TArray<FVector3f> ConstantPositions;
	TArray<FVector3f> NdbPositions;
	FContactClothGpuExplicitTopologyRealtimeStats ConstantStats;
	FContactClothGpuExplicitTopologyRealtimeStats NdbStats;
	TestTrue(TEXT("常量罚权重显式拓扑会话完成"), ContactClothGpuExplicitNdbTest::RunSession(1.0f, StepCount, ConstantPositions, ConstantStats));
	TestTrue(TEXT("NDB 增权显式拓扑会话完成"), ContactClothGpuExplicitNdbTest::RunSession(2.0f, StepCount, NdbPositions, NdbStats));
	const float ConstantPenetration = ConstantPositions.IsValidIndex(0) ? FMath::Max(0.0f, 10.0f - ConstantPositions[0].Length()) : TNumericLimits<float>::Max();
	const float NdbPenetration = NdbPositions.IsValidIndex(0) ? FMath::Max(0.0f, 10.0f - NdbPositions[0].Length()) : TNumericLimits<float>::Max();
	const float FixedDrift = NdbPositions.IsValidIndex(1) ? (NdbPositions[1] - FVector3f(1000.0f, 0.0f, 0.0f)).Length() : TNumericLimits<float>::Max();

	FContactClothGpuExplicitTopologyRealtimeSettings CapacitySettings;
	ContactClothGpuExplicitNdbTest::Configure(CapacitySettings, 2.0f);
	const TArray<FVector3f> CapacityPositions = {FVector3f(5.0f, 0.0f, 0.0f), FVector3f(1000.0f, 0.0f, 0.0f)};
	const TArray<float> CapacityInverseMasses = {1.0f, 0.0f};
	const TArray<FIntPoint> CapacityEdges = {FIntPoint(0, 1)};
	const TArray<float> CapacityRestLengths = {995.0f};
	FContactClothGpuExplicitTopologySimulation CapacitySimulation;
	TestTrue(TEXT("容量失败门会话初始化"), CapacitySimulation.Initialize(
		CapacityPositions, CapacityInverseMasses, CapacityEdges, CapacityRestLengths, CapacitySettings));
	const TArray<FContactClothGpuSphere> TooManySpheres = {
		FContactClothGpuSphere{FVector3f::ZeroVector, 10.0f},
		FContactClothGpuSphere{FVector3f(100.0f, 0.0f, 0.0f), 10.0f}};
	const TArray<FContactClothGpuCapsule> NoCapsules;
	TestFalse(TEXT("超过固定容量的碰撞拓扑被拒绝"), CapacitySimulation.UpdateCollisionPrimitives(TooManySpheres, NoCapsules));
	CapacitySimulation.Shutdown();

	AddInfo(FString::Printf(TEXT("Explicit topology NDB: constant_penetration_cm=%.6f ndb_penetration_cm=%.6f ndb_updates=%lld active_pairs=%d maximum_activation=%d fixed_index_1_drift_cm=%.9f capacity_overflow_rejected=true"),
		ConstantPenetration, NdbPenetration, NdbStats.NdbStateUpdatesExecuted, NdbStats.LatestActiveContactPairs,
		NdbStats.LatestMaximumNdbActivationCount, FixedDrift));
	TestTrue(TEXT("常量罚权重保持有限穿透"), ConstantPenetration > 0.01f);
	TestTrue(TEXT("连续激活增权显著降低穿透"), NdbPenetration < ConstantPenetration - 0.25f);
	TestEqual(TEXT("NDB 每固定步更新一次状态"), NdbStats.NdbStateUpdatesExecuted, static_cast<int64>(StepCount));
	TestEqual(TEXT("只有动态探针形成活跃接触对"), NdbStats.LatestActiveContactPairs, 1);
	TestEqual(TEXT("连续激活计数达到四"), NdbStats.LatestMaximumNdbActivationCount, StepCount);
	TestTrue(TEXT("非首位固定点由逆质量保护"), FixedDrift < 1.e-5f);
	return true;
}

#endif
