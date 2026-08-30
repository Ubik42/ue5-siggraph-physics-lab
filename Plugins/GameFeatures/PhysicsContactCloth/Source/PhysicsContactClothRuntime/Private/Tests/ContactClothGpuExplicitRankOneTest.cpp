#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothGpuExplicitTopology.h"
#include "ContactClothModalSubspace.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothGpuExplicitRankOneTest,
	"PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.RankOneCollisionAblation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothGpuExplicitRankOneTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过显式拓扑 rank-one GPU 门；D3D12 报告单独生成。"));
		return true;
	}
	const TArray<FVector3f> InitialPositions = {
		{-20,0,20},{0,1,21},{21,-1,19},{-18,0,0},{2,-2,1},{23,1,-1},{-17,2,-20},{3,0,-19},{25,-1,-21}};
	const TArray<float> InverseMasses = {1,0,1,1,1,1,0,1,1};
	const TArray<FIntPoint> Edges = {
		{0,1},{1,2},{3,4},{4,5},{6,7},{7,8},{0,3},{1,4},{2,5},{3,6},{4,7},{5,8},
		{0,4},{1,3},{1,5},{2,4},{3,7},{4,6},{4,8},{5,7}};
	TArray<float> RestLengths;
	for (const FIntPoint& Edge : Edges) RestLengths.Add((InitialPositions[Edge.X] - InitialPositions[Edge.Y]).Length());
	constexpr int32 ModeCount = 4;
	FContactClothModalBasis Basis;
	FContactClothReducedSystem System;
	FContactClothRankOneProjectionCache Cache;
	TArray<double> InverseSystem;
	TestTrue(TEXT("显式 A/B 构建任意拓扑模态"), FContactClothModalSubspace::BuildExplicitTopologyBasis(
		InitialPositions, InverseMasses, Edges, ModeCount, Basis));
	TestTrue(TEXT("显式 A/B 构建任意拓扑缩减系统"), FContactClothModalSubspace::BuildReducedExplicitTopologySystem(
		Basis, InverseMasses, Edges, 1.0, 1.0, System));
	TestTrue(TEXT("显式 A/B 构建 rank-one 缓存"), FContactClothModalSubspace::BuildRankOneProjectionCache(Basis, Cache));
	TestTrue(TEXT("显式 A/B 反演缩减系统"), FContactClothModalSubspace::InvertReducedSystem(System, InverseSystem));

	FContactClothGpuExplicitTopologyRealtimeSettings Settings;
	Settings.Iterations = 1;
	Settings.Relaxation = 1.e-6f;
	Settings.FixedDeltaSeconds = 1.0f / 60.0f;
	Settings.GravityCmPerSecondSquared = FVector3f::ZeroVector;
	Settings.VelocityDamping = 0.0f;
	Settings.ReadbackIntervalSteps = 1;
	Settings.ModalModeCount = ModeCount;
	Settings.ModalCorrectionStrength = 0.25f;
	for (double Value : Basis.Values) Settings.ModalBasisValues.Add(static_cast<float>(Value));
	for (double Value : InverseSystem) Settings.ModalInverseSystemValues.Add(static_cast<float>(Value));
	for (double Value : System.Matrix) Settings.ModalSystemValues.Add(static_cast<float>(Value));
	for (double Value : Cache.Values) Settings.ModalParticleOuterProductValues.Add(static_cast<float>(Value));
	Settings.bEnableNonDistanceBarrier = true;
	Settings.NdbBaseWeight = 0.05f;
	Settings.NdbActivationMultiplier = 2.0f;
	Settings.NdbMaximumWeight = 1024.0f;
	Settings.CollisionPrimitiveCapacity = 1;
	Settings.CollisionThicknessCm = 0.0f;
	Settings.bEnableCompleteCcd = false;
	Settings.RankOneMaximumCorrectionCm = 4.0f;
	const FVector3f SphereCenter(3.0f, -2.0f, -17.0f);
	constexpr float SphereRadius = 8.0f;
	const TArray<FContactClothGpuSphere> Spheres = {{SphereCenter, SphereRadius}};
	const TArray<FContactClothGpuCapsule> Capsules;

	auto Run = [&](bool bRankOne, TArray<FVector3f>& OutPositions, FContactClothGpuExplicitTopologyRealtimeStats& OutStats)
	{
		Settings.bEnableRankOneCollisionHessian = bRankOne;
		FContactClothGpuExplicitTopologySimulation Simulation;
		if (!Simulation.Initialize(InitialPositions, InverseMasses, Edges, RestLengths, Settings)
			|| !Simulation.UpdateCollisionPrimitives(Spheres, Capsules) || !Simulation.EnqueueStep())
		{
			Simulation.Shutdown();
			return false;
		}
		Simulation.FlushAndWaitForReadbackForValidation();
		int64 Step = 0;
		const bool bRead = Simulation.ConsumeLatestPositions(OutPositions, Step);
		OutStats = Simulation.GetStats();
		Simulation.Shutdown();
		return bRead && Step == 1;
	};
	TArray<FVector3f> BaselinePositions;
	TArray<FVector3f> RankOnePositions;
	FContactClothGpuExplicitTopologyRealtimeStats BaselineStats;
	FContactClothGpuExplicitTopologyRealtimeStats RankOneStats;
	TestTrue(TEXT("显式拓扑局部 NDB 基线完成"), Run(false, BaselinePositions, BaselineStats));
	TestTrue(TEXT("显式拓扑 rank-one 会话完成"), Run(true, RankOnePositions, RankOneStats));
	constexpr int32 ProbeIndex = 7;
	const float BaselinePenetration = BaselinePositions.IsValidIndex(ProbeIndex)
		? FMath::Max(0.0f, SphereRadius - (BaselinePositions[ProbeIndex] - SphereCenter).Length()) : TNumericLimits<float>::Max();
	const float RankOnePenetration = RankOnePositions.IsValidIndex(ProbeIndex)
		? FMath::Max(0.0f, SphereRadius - (RankOnePositions[ProbeIndex] - SphereCenter).Length()) : TNumericLimits<float>::Max();
	const float FixedIndexOneDrift = RankOnePositions.IsValidIndex(1) ? (RankOnePositions[1] - InitialPositions[1]).Length() : TNumericLimits<float>::Max();
	const float FixedIndexSixDrift = RankOnePositions.IsValidIndex(6) ? (RankOnePositions[6] - InitialPositions[6]).Length() : TNumericLimits<float>::Max();
	TestEqual(TEXT("关闭时不执行显式 rank-one"), BaselineStats.RankOneCollisionSystemsExecuted, static_cast<int64>(0));
	TestEqual(TEXT("开启时装配求解一次显式 rank-one"), RankOneStats.RankOneCollisionSystemsExecuted, static_cast<int64>(1));
	TestEqual(TEXT("显式模态每步执行"), RankOneStats.ModalCorrectionsExecuted, static_cast<int64>(1));
	TestTrue(TEXT("rank-one 进一步降低探针穿透"), RankOnePenetration < BaselinePenetration - 1.e-4f);
	TestTrue(TEXT("两个非前缀固定点均不漂移"), FixedIndexOneDrift < 1.e-5f && FixedIndexSixDrift < 1.e-5f);
	AddInfo(FString::Printf(TEXT("Explicit rank-one ablation: particles=%d modes=%d baseline_penetration_cm=%.9f rank_one_penetration_cm=%.9f modal_passes=%lld rank_one_systems=%lld fixed_1_drift_cm=%.9f fixed_6_drift_cm=%.9f"),
		InitialPositions.Num(), ModeCount, BaselinePenetration, RankOnePenetration, RankOneStats.ModalCorrectionsExecuted,
		RankOneStats.RankOneCollisionSystemsExecuted, FixedIndexOneDrift, FixedIndexSixDrift));
	return true;
}

#endif
