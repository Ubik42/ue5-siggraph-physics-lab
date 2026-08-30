#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothNonDistanceBarrier.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothNonDistanceBarrierTest,
	"PhysicsResearch.ContactCloth.NonDistanceBarrierSchedule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothNonDistanceBarrierTest::RunTest(const FString& Parameters)
{
	FContactClothNdbSettings Settings;
	Settings.BaseWeight = 3.0;
	Settings.ActivationMultiplier = 2.0;
	Settings.MaximumWeight = 48.0;
	FContactClothNonDistanceBarrier Barrier(Settings);

	constexpr uint64 PersistentContact = 0x100000002ull;
	const double ExpectedWeights[] = {6.0, 12.0, 24.0, 48.0, 48.0};
	for (int32 Iteration = 0; Iteration < UE_ARRAY_COUNT(ExpectedWeights); ++Iteration)
	{
		const FContactClothNdbState State = Barrier.UpdateContact(PersistentContact, true);
		TestEqual(TEXT("连续激活次数逐迭代增长"), State.ConsecutiveActiveIterations, Iteration + 1);
		TestEqual(TEXT("NDB 权重按 K=2 指数增长并限幅"), State.Weight, ExpectedWeights[Iteration]);
	}

	constexpr uint64 SameHistoryDifferentGapA = 0x200000003ull;
	constexpr uint64 SameHistoryDifferentGapB = 0x200000004ull;
	const FContactClothNdbState GapAState = Barrier.UpdateContact(SameHistoryDifferentGapA, true);
	const FContactClothNdbState GapBState = Barrier.UpdateContact(SameHistoryDifferentGapB, true);
	TestEqual(TEXT("相同激活历史的权重与几何距离无关"), GapAState.Weight, GapBState.Weight);

	const uint64 ActiveIds[] = {PersistentContact, SameHistoryDifferentGapA};
	Barrier.EndIteration(ActiveIds);
	FContactClothNdbState DeactivatedState;
	TestTrue(TEXT("失活接触仍可查询用于统计"), Barrier.FindState(SameHistoryDifferentGapB, DeactivatedState));
	TestFalse(TEXT("未在活跃集合中的接触被失活"), DeactivatedState.bActive);
	TestEqual(TEXT("失活后连续计数归零"), DeactivatedState.ConsecutiveActiveIterations, 0);
	const FContactClothNdbState ReactivatedState = Barrier.UpdateContact(SameHistoryDifferentGapB, true);
	TestEqual(TEXT("重新激活从第一次权重重新开始"), ReactivatedState.Weight, 6.0);

	const FContactClothNdbStats Stats = Barrier.GetStats();
	TestEqual(TEXT("追踪接触数量确定"), Stats.TrackedContactCount, 3);
	TestEqual(TEXT("活跃接触数量确定"), Stats.ActiveContactCount, 3);
	TestEqual(TEXT("最大连续激活次数可观测"), Stats.MaximumActivationCount, 5);
	TestEqual(TEXT("最大权重受安全上限约束"), Stats.MaximumWeight, 48.0);
	TestTrue(TEXT("权重限幅次数被记录"), Stats.SaturatedWeightUpdates >= 2);

	Barrier.Reset();
	const FContactClothNdbStats ResetStats = Barrier.GetStats();
	TestEqual(TEXT("重置清空所有接触历史"), ResetStats.TrackedContactCount, 0);
	TestEqual(TEXT("重置清空限幅计数"), ResetStats.SaturatedWeightUpdates, static_cast<int64>(0));
	AddInfo(FString::Printf(
		TEXT("NDB schedule: base=%.1f multiplier=%.1f maximum=%.1f max_activation=%d saturated_updates=%lld"),
		Settings.BaseWeight,
		Settings.ActivationMultiplier,
		Settings.MaximumWeight,
		Stats.MaximumActivationCount,
		Stats.SaturatedWeightUpdates));
	return true;
}

#endif
