#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothGpuJacobi.h"
#include "Misc/AutomationTest.h"
#include "RHI.h"

namespace ContactClothGpuHybridCollisionTest
{
bool RunSession(
	TConstArrayView<FVector3f> InitialPositions,
	const FContactClothGpuRealtimeSettings& Settings,
	int32 StepCount,
	TArray<FVector3f>& OutPositions,
	FContactClothGpuRealtimeStats& OutStats)
{
	FContactClothGpuRealtimeSimulation Simulation;
	if (!Simulation.Initialize(InitialPositions, Settings))
	{
		return false;
	}
	for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
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
	FContactClothGpuCompleteCcdIntegrationTest,
	"PhysicsResearch.ContactCloth.GpuCompleteCcdIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothGpuCompleteCcdIntegrationTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过完整 CCD GPU Sweep；D3D12 报告单独生成。"));
		return true;
	}

	FContactClothGpuRealtimeSettings Settings;
	Settings.Columns = 2;
	Settings.Rows = 2;
	Settings.SpacingCm = 100.0f;
	Settings.Iterations = 1;
	Settings.Relaxation = 0.0f;
	Settings.FixedDeltaSeconds = 1.0f / 60.0f;
	Settings.GravityCmPerSecondSquared = FVector3f(0.0f, 0.0f, -720000.0f);
	Settings.VelocityDamping = 0.0f;
	Settings.ReadbackIntervalSteps = 1;
	Settings.CollisionThicknessCm = 0.0f;
	Settings.CollisionSpheres.Add({FVector3f::ZeroVector, 10.0f});
	Settings.CcdSafetyOffsetCm = 0.01f;

	const TArray<FVector3f> InitialPositions = {
		FVector3f(0.0f, 0.0f, 200.0f),
		FVector3f(100.0f, 0.0f, 200.0f),
		FVector3f(0.0f, 0.0f, 100.0f),
		FVector3f(100.0f, 0.0f, 100.0f)};

	TArray<FVector3f> DiscretePositions;
	FContactClothGpuRealtimeStats DiscreteStats;
	Settings.bEnableCompleteCcd = false;
	TestTrue(
		TEXT("离散基线 GPU 会话完成"),
		ContactClothGpuHybridCollisionTest::RunSession(InitialPositions, Settings, 1, DiscretePositions, DiscreteStats));

	TArray<FVector3f> CcdPositions;
	FContactClothGpuRealtimeStats CcdStats;
	Settings.bEnableCompleteCcd = true;
	TestTrue(
		TEXT("完整 CCD GPU 会话完成"),
		ContactClothGpuHybridCollisionTest::RunSession(InitialPositions, Settings, 1, CcdPositions, CcdStats));

	const int32 ProbeIndex = Settings.Columns;
	TestTrue(TEXT("离散端点投影发生高速穿越"), DiscretePositions.IsValidIndex(ProbeIndex) && DiscretePositions[ProbeIndex].Z < -10.0f);
	TestTrue(TEXT("完整 CCD 在球体入射面截停"), CcdPositions.IsValidIndex(ProbeIndex) && CcdPositions[ProbeIndex].Z >= 9.99f);
	TestEqual(TEXT("离散基线不执行完整 CCD"), DiscreteStats.CompleteCcdPassesExecuted, static_cast<int64>(0));
	TestEqual(TEXT("完整 CCD 每固定步执行一次"), CcdStats.CompleteCcdPassesExecuted, static_cast<int64>(1));
	AddInfo(FString::Printf(
		TEXT("Complete CCD crossing: discrete_z=%.6f ccd_z=%.6f ccd_passes=%lld"),
		DiscretePositions.IsValidIndex(ProbeIndex) ? DiscretePositions[ProbeIndex].Z : 0.0f,
		CcdPositions.IsValidIndex(ProbeIndex) ? CcdPositions[ProbeIndex].Z : 0.0f,
		CcdStats.CompleteCcdPassesExecuted));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothGpuNonDistanceBarrierIntegrationTest,
	"PhysicsResearch.ContactCloth.GpuNonDistanceBarrierIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothGpuNonDistanceBarrierIntegrationTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过 NDB GPU 状态验证；D3D12 报告单独生成。"));
		return true;
	}

	FContactClothGpuRealtimeSettings Settings;
	Settings.Columns = 2;
	Settings.Rows = 2;
	Settings.SpacingCm = 100.0f;
	Settings.Iterations = 1;
	Settings.Relaxation = 0.0f;
	Settings.FixedDeltaSeconds = 1.0f / 60.0f;
	Settings.GravityCmPerSecondSquared = FVector3f::ZeroVector;
	Settings.VelocityDamping = 0.0f;
	Settings.ReadbackIntervalSteps = 1;
	Settings.CollisionThicknessCm = 0.0f;
	Settings.CollisionSpheres.Add({FVector3f::ZeroVector, 10.0f});
	Settings.bEnableNonDistanceBarrier = true;
	Settings.NdbBaseWeight = 0.05f;
	Settings.NdbActivationMultiplier = 2.0f;
	Settings.NdbMaximumWeight = 1024.0f;
	Settings.CollisionPrimitiveCapacity = 1;
	Settings.bEnableCompleteCcd = false;

	const TArray<FVector3f> InitialPositions = {
		FVector3f(0.0f, 0.0f, 200.0f),
		FVector3f(100.0f, 0.0f, 200.0f),
		FVector3f(5.0f, 0.0f, 0.0f),
		FVector3f(100.0f, 0.0f, 100.0f)};

	TArray<FVector3f> FirstStepPositions;
	FContactClothGpuRealtimeStats FirstStepStats;
	TestTrue(
		TEXT("NDB 单步 GPU 会话完成"),
		ContactClothGpuHybridCollisionTest::RunSession(InitialPositions, Settings, 1, FirstStepPositions, FirstStepStats));

	TArray<FVector3f> SustainedPositions;
	FContactClothGpuRealtimeStats SustainedStats;
	constexpr int32 SustainedStepCount = 4;
	TestTrue(
		TEXT("NDB 连续接触 GPU 会话完成"),
		ContactClothGpuHybridCollisionTest::RunSession(InitialPositions, Settings, SustainedStepCount, SustainedPositions, SustainedStats));

	const int32 ProbeIndex = Settings.Columns;
	const float FirstPenetration = FirstStepPositions.IsValidIndex(ProbeIndex)
		? FMath::Max(0.0f, 10.0f - FirstStepPositions[ProbeIndex].Length())
		: TNumericLimits<float>::Max();
	const float SustainedPenetration = SustainedPositions.IsValidIndex(ProbeIndex)
		? FMath::Max(0.0f, 10.0f - SustainedPositions[ProbeIndex].Length())
		: TNumericLimits<float>::Max();
	TestTrue(TEXT("NDB 首次接触保持有限罚投影而非硬投影"), FirstPenetration > 0.01f);
	TestTrue(TEXT("连续激活提高罚权重并降低穿透"), SustainedPenetration < FirstPenetration - 0.01f);
	TestEqual(TEXT("NDB 单步状态更新计数"), FirstStepStats.NdbStateUpdatesExecuted, static_cast<int64>(1));
	TestEqual(TEXT("NDB 每固定步更新持久状态"), SustainedStats.NdbStateUpdatesExecuted, static_cast<int64>(SustainedStepCount));
	TestEqual(TEXT("NDB 用例未借助完整 CCD"), SustainedStats.CompleteCcdPassesExecuted, static_cast<int64>(0));
	AddInfo(FString::Printf(
		TEXT("NDB activation schedule: first_penetration_cm=%.6f sustained_penetration_cm=%.6f state_updates=%lld"),
		FirstPenetration,
		SustainedPenetration,
		SustainedStats.NdbStateUpdatesExecuted));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothGpuHybridAblationMatrixTest,
	"PhysicsResearch.ContactCloth.GpuHybridAblationMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothGpuHybridAblationMatrixTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过 GPU 混合碰撞四象限消融；D3D12 报告单独生成。"));
		return true;
	}

	struct FAblationRow
	{
		const TCHAR* Name;
		float NdbMultiplier;
		bool bCcd;
		float SustainedPenetration = 0.0f;
		float CrossingZ = 0.0f;
		FContactClothGpuRealtimeStats SustainedStats;
		FContactClothGpuRealtimeStats CrossingStats;
	};
	TArray<FAblationRow> Rows = {
		{TEXT("constant_no_ccd"), 1.0f, false},
		{TEXT("ndb_no_ccd"), 2.0f, false},
		{TEXT("constant_ccd"), 1.0f, true},
		{TEXT("full"), 2.0f, true}};

	FContactClothGpuRealtimeSettings BaseSettings;
	BaseSettings.Columns = 2;
	BaseSettings.Rows = 2;
	BaseSettings.SpacingCm = 100.0f;
	BaseSettings.Iterations = 1;
	BaseSettings.Relaxation = 0.0f;
	BaseSettings.FixedDeltaSeconds = 1.0f / 60.0f;
	BaseSettings.VelocityDamping = 0.0f;
	BaseSettings.ReadbackIntervalSteps = 1;
	BaseSettings.CollisionThicknessCm = 0.0f;
	BaseSettings.CollisionSpheres.Add({FVector3f::ZeroVector, 10.0f});
	BaseSettings.bEnableNonDistanceBarrier = true;
	BaseSettings.NdbBaseWeight = 0.05f;
	BaseSettings.NdbMaximumWeight = 1024.0f;
	BaseSettings.CollisionPrimitiveCapacity = 1;
	BaseSettings.CcdSafetyOffsetCm = 0.01f;

	const TArray<FVector3f> SustainedInitial = {
		FVector3f(0.0f, 0.0f, 200.0f),
		FVector3f(100.0f, 0.0f, 200.0f),
		FVector3f(5.0f, 0.0f, 0.0f),
		FVector3f(100.0f, 0.0f, 100.0f)};
	const TArray<FVector3f> CrossingInitial = {
		FVector3f(0.0f, 0.0f, 200.0f),
		FVector3f(100.0f, 0.0f, 200.0f),
		FVector3f(0.0f, 0.0f, 100.0f),
		FVector3f(100.0f, 0.0f, 100.0f)};
	const int32 ProbeIndex = BaseSettings.Columns;

	bool bAllSessionsCompleted = true;
	for (FAblationRow& Row : Rows)
	{
		FContactClothGpuRealtimeSettings SustainedSettings = BaseSettings;
		SustainedSettings.GravityCmPerSecondSquared = FVector3f::ZeroVector;
		SustainedSettings.NdbActivationMultiplier = Row.NdbMultiplier;
		SustainedSettings.bEnableCompleteCcd = Row.bCcd;
		TArray<FVector3f> SustainedPositions;
		bAllSessionsCompleted &= ContactClothGpuHybridCollisionTest::RunSession(
			SustainedInitial, SustainedSettings, 4, SustainedPositions, Row.SustainedStats);
		Row.SustainedPenetration = SustainedPositions.IsValidIndex(ProbeIndex)
			? FMath::Max(0.0f, 10.0f - SustainedPositions[ProbeIndex].Length())
			: TNumericLimits<float>::Max();

		FContactClothGpuRealtimeSettings CrossingSettings = BaseSettings;
		CrossingSettings.GravityCmPerSecondSquared = FVector3f(0.0f, 0.0f, -720000.0f);
		CrossingSettings.NdbActivationMultiplier = Row.NdbMultiplier;
		CrossingSettings.bEnableCompleteCcd = Row.bCcd;
		TArray<FVector3f> CrossingPositions;
		bAllSessionsCompleted &= ContactClothGpuHybridCollisionTest::RunSession(
			CrossingInitial, CrossingSettings, 1, CrossingPositions, Row.CrossingStats);
		Row.CrossingZ = CrossingPositions.IsValidIndex(ProbeIndex) ? CrossingPositions[ProbeIndex].Z : 0.0f;
	}

	TestTrue(TEXT("四象限消融的八个 GPU 会话全部完成"), bAllSessionsCompleted);
	const FAblationRow& ConstantNoCcd = Rows[0];
	const FAblationRow& NdbNoCcd = Rows[1];
	const FAblationRow& ConstantCcd = Rows[2];
	const FAblationRow& Full = Rows[3];
	TestTrue(TEXT("持续接触中 NDB 比常量罚权重更快降低穿透"), NdbNoCcd.SustainedPenetration < ConstantNoCcd.SustainedPenetration * 0.6f);
	TestTrue(TEXT("完整 CCD 不伪装成初始重叠修复器"), FMath::IsNearlyEqual(ConstantCcd.SustainedPenetration, ConstantNoCcd.SustainedPenetration, 1.e-3f));
	TestTrue(TEXT("完整方法保持 NDB 的持续接触收益"), FMath::IsNearlyEqual(Full.SustainedPenetration, NdbNoCcd.SustainedPenetration, 1.e-3f));
	TestTrue(TEXT("无 CCD 的常量罚权重发生高速穿越"), ConstantNoCcd.CrossingZ < -10.0f);
	TestTrue(TEXT("无 CCD 的 NDB 仍会高速穿越"), NdbNoCcd.CrossingZ < -10.0f);
	TestTrue(TEXT("CCD 在常量罚权重配置中截停高速穿越"), ConstantCcd.CrossingZ >= 9.99f);
	TestTrue(TEXT("完整方法截停高速穿越"), Full.CrossingZ >= 9.99f);
	TestEqual(TEXT("完整方法持续接触 NDB 更新四次"), Full.SustainedStats.NdbStateUpdatesExecuted, static_cast<int64>(4));
	TestEqual(TEXT("完整方法持续接触 CCD 执行四次"), Full.SustainedStats.CompleteCcdPassesExecuted, static_cast<int64>(4));
	AddInfo(FString::Printf(
		TEXT("Hybrid ablation matrix: constant_no_ccd_pen=%.6f ndb_no_ccd_pen=%.6f constant_ccd_pen=%.6f full_pen=%.6f constant_no_ccd_z=%.6f ndb_no_ccd_z=%.6f constant_ccd_z=%.6f full_z=%.6f"),
		ConstantNoCcd.SustainedPenetration,
		NdbNoCcd.SustainedPenetration,
		ConstantCcd.SustainedPenetration,
		Full.SustainedPenetration,
		ConstantNoCcd.CrossingZ,
		NdbNoCcd.CrossingZ,
		ConstantCcd.CrossingZ,
		Full.CrossingZ));
	return true;
}

#endif
