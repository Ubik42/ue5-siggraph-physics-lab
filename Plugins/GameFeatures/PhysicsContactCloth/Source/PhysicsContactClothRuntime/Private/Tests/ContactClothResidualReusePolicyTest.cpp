#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothResidualReusePolicy.h"
#include "Misc/AutomationTest.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothResidualReusePolicyTest,
	"PhysicsResearch.ContactCloth.ResidualReuseSafetyPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothResidualReusePolicyTest::RunTest(const FString& Parameters)
{
	FContactClothResidualReuseSettings Settings;
	Settings.ForwardScale = 0.4f;
	Settings.MaximumResidualCm = 2.0f;
	Settings.MaximumCcdCorrectionCm = 0.25f;
	FContactClothResidualReusePolicy Policy(Settings);

	FContactClothResidualReuseSample SafeSample;
	SafeSample.MaximumResidualCm = 1.5f;
	SafeSample.MaximumCcdCorrectionCm = 0.1f;
	SafeSample.CachedTopologyRevision = 7;
	SafeSample.CurrentTopologyRevision = 7;
	const FContactClothResidualReuseResult SafeResult = Policy.Evaluate(SafeSample);
	TestTrue(TEXT("安全残差允许跨步转发"), SafeResult.IsAccepted());
	TestEqual(TEXT("安全转发使用配置缩放"), SafeResult.ForwardScale, 0.4f);

	FContactClothResidualReuseSample TopologySample = SafeSample;
	TopologySample.CurrentTopologyRevision = 8;
	TestEqual(TEXT("拓扑版本变化拒绝旧残差"), Policy.Evaluate(TopologySample).Decision, EContactClothResidualReuseDecision::TopologyChanged);
	FContactClothResidualReuseSample LargeResidualSample = SafeSample;
	LargeResidualSample.MaximumResidualCm = 2.01f;
	TestEqual(TEXT("残差越界触发回退"), Policy.Evaluate(LargeResidualSample).Decision, EContactClothResidualReuseDecision::ResidualTooLarge);
	FContactClothResidualReuseSample LargeCcdSample = SafeSample;
	LargeCcdSample.MaximumCcdCorrectionCm = 0.251f;
	TestEqual(TEXT("CCD 大修正使转发失效"), Policy.Evaluate(LargeCcdSample).Decision, EContactClothResidualReuseDecision::CcdCorrectionTooLarge);
	FContactClothResidualReuseSample NonFiniteSample = SafeSample;
	NonFiniteSample.MaximumResidualCm = std::numeric_limits<float>::infinity();
	TestEqual(TEXT("非有限输入触发回退"), Policy.Evaluate(NonFiniteSample).Decision, EContactClothResidualReuseDecision::NonFinite);

	const FContactClothResidualReuseStats& Stats = Policy.GetStats();
	TestEqual(TEXT("评估样本总数"), Stats.SamplesEvaluated, static_cast<int64>(5));
	TestEqual(TEXT("接受样本数"), Stats.AcceptedSamples, static_cast<int64>(1));
	TestEqual(TEXT("拓扑回退数"), Stats.TopologyFallbacks, static_cast<int64>(1));
	TestEqual(TEXT("残差幅度回退数"), Stats.ResidualMagnitudeFallbacks, static_cast<int64>(1));
	TestEqual(TEXT("CCD 修正回退数"), Stats.CcdCorrectionFallbacks, static_cast<int64>(1));
	TestEqual(TEXT("非有限回退数"), Stats.NonFiniteFallbacks, static_cast<int64>(1));
	Policy.Reset();
	TestEqual(TEXT("重置清空策略统计"), Policy.GetStats().SamplesEvaluated, static_cast<int64>(0));
	AddInfo(TEXT("Residual reuse safety: accepted=1 topology=1 residual=1 ccd=1 nonfinite=1 scale=0.4"));
	return true;
}

#endif
